/* teptris._native — TOML 1.1 parser/writer binding (Python C API).

Eager path: `loads(data)` parses TOML into nested dict/list/leaf objects.
Lazy path: `loads_lazy(data)` returns a `LazyNode` wrapping the parsed
tree — host objects materialize only on access (`[]` for tables/arrays,
`value()` for scalars, `to_dict()` / `to_list()` to flatten).

The C ABI for both paths lives in libteptris (`teptris/teptris.h`). The
lazy twin avoids materializing the intermediate dict/list for the many
shape where only a small subset of the tree is touched (teptris#79). */
#define PY_SSIZE_T_CLEAN
/* abi3 (#20): the datetime C-API is not in the limited API. The
 * cp39-abi3 wheel keeps the cached-callables path; every
 * version-specific wheel (cp310-cp313) builds without the limited
 * API and takes the fast datetime.h path below — setup.py drops
 * py_limited_api in lockstep so those wheels tag cp3NN-cp3NN. */
#if !defined(Py_LIMITED_API) && defined(PY_VERSION_HEX) && \
    PY_VERSION_HEX < 0x030A0000
#define Py_LIMITED_API 0x03090000
#endif
#include <Python.h>
#include "teptris/teptris.h"
#include "teptris/plan.h"

/* version-specific wheels build WITHOUT the limited API: the full
 * datetime.h C-API replaces the generic-call materialization
 * (measured 51% of datetime_heavy load:
 * PyArg_ParseTupleAndKeywords inside type_call). The abi3 line keeps
 * the cached-callables path. */
#if !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x030A0000
#define TEPTRIS_FAST_DT 1
#endif

#ifdef TEPTRIS_FAST_DT
#include <datetime.h>
/* aware datetimes reuse one timezone object per offset: offsets are
 * bounded (~56 real zone offsets), the cache lives for the module.
 * Free-threaded (Py_GIL_DISABLED) builds need the lock: the cache is
 * the only shared mutable state in the module, and the lookup-insert
 * pattern is a read-modify-write. */
static PyObject *tep_tz_cache;
static PyThread_type_lock tep_tz_lock;
#endif

static PyObject *TomlDecodeError;
/* datetime module callables: the load side only needs them below
 * 3.13 (fast path = datetime.h C-API there); the dump side reads
 * them in every build */
static PyObject *tep_dt_datetime, *tep_dt_date, *tep_dt_time,
    *tep_dt_timedelta, *tep_dt_timezone;

/* strong-ref attr read as long; def on missing/invalid (datetime
 * attributes always exist on well-formed instances) */
static long attr_long(PyObject *v, const char *name) {
    PyObject *a = PyObject_GetAttrString(v, name);
    if (a == NULL) { PyErr_Clear(); return 0; }
    long r = PyLong_AsLong(a);
    Py_DECREF(a);
    if (r == -1 && PyErr_Occurred()) { PyErr_Clear(); return 0; }
    return r;
}

static PyObject *obj_from_node(const teptris_node *n) {
    switch (teptris_node_kind(n)) {
    case TEPTRIS_TABLE: {
        size_t len = teptris_node_table_length(n);
        PyObject *h = PyDict_New();
        if (!h) return NULL;
        for (size_t i = 0; i < len; i++) {
            teptris_view key;
            const teptris_node *v = teptris_node_table_at(n, i, &key);
            PyObject *k = PyUnicode_DecodeUTF8(key.ptr, (Py_ssize_t)key.len, "replace");
            if (!k) { Py_DECREF(h); return NULL; }
            PyObject *val = obj_from_node(v);
            if (!val) { Py_DECREF(k); Py_DECREF(h); return NULL; }
            int rc = PyDict_SetItem(h, k, val);
            Py_DECREF(k); Py_DECREF(val);
            if (rc < 0) { Py_DECREF(h); return NULL; }
        }
        return h;
    }
    case TEPTRIS_ARRAY: {
        size_t len = teptris_node_array_length(n);
        PyObject *a = PyList_New((Py_ssize_t)len);
        if (!a) return NULL;
        for (size_t i = 0; i < len; i++) {
            PyObject *v = obj_from_node(teptris_node_array_at(n, i));
            if (!v) { Py_DECREF(a); return NULL; }
            /* SetItem steals v on both success and failure */
            if (PyList_SetItem(a, (Py_ssize_t)i, v) < 0) {
                Py_DECREF(a); return NULL;
            }
        }
        return a;
    }
    case TEPTRIS_STRING: {
        teptris_view s;
        teptris_node_string(n, &s);
        return PyUnicode_DecodeUTF8(s.ptr, (Py_ssize_t)s.len, "replace");
    }
    case TEPTRIS_INTEGER: {
        int64_t v = 0; teptris_node_integer(n, &v);
        return PyLong_FromLongLong(v);
    }
    case TEPTRIS_FLOAT: {
        double v = 0; teptris_node_float(n, &v);
        return PyFloat_FromDouble(v);
    }
    case TEPTRIS_BOOLEAN: {
        bool v = false; teptris_node_boolean(n, &v);
        return PyBool_FromLong(v ? 1 : 0);
    }
    default: {
        teptris_datetime d; teptris_node_datetime(n, &d);
#ifdef TEPTRIS_FAST_DT
        switch (teptris_node_kind(n)) {
        case TEPTRIS_DATE_LOCAL:
            return PyDate_FromDate(d.year, d.month, d.day);
        case TEPTRIS_TIME_LOCAL:
            return PyTime_FromTime(d.hour, d.minute, d.second,
                                   (int)(d.nanosecond / 1000));
        default:
            break;
        }
        if (teptris_node_kind(n) != TEPTRIS_DATETIME_OFFSET) {
            return PyDateTime_FromDateAndTime(
                d.year, d.month, d.day, d.hour, d.minute, d.second,
                (int)(d.nanosecond / 1000));
        }
        /* aware: build once, with the cached per-offset timezone
         * (timezone(timedelta(seconds=off))) - no discarded naive
         * intermediate */
        {
            PyObject *off = PyLong_FromLong((long)d.offset_seconds);
            if (off == NULL) return NULL;
            PyThread_acquire_lock(tep_tz_lock, WAIT_LOCK);
            PyObject *tz = PyDict_GetItemWithError(tep_tz_cache, off);
            if (tz == NULL) {
                if (PyErr_Occurred()) {
                    PyThread_release_lock(tep_tz_lock);
                    Py_DECREF(off);
                    return NULL;
                }
                PyObject *delta = PyDelta_FromDSU(0, d.offset_seconds, 0);
                if (delta == NULL) {
                    PyThread_release_lock(tep_tz_lock);
                    Py_DECREF(off);
                    return NULL;
                }
                tz = PyTimeZone_FromOffset(delta);
                Py_DECREF(delta);
                if (tz == NULL) {
                    PyThread_release_lock(tep_tz_lock);
                    Py_DECREF(off);
                    return NULL;
                }
                if (PyDict_SetItem(tep_tz_cache, off, tz) < 0) {
                    PyThread_release_lock(tep_tz_lock);
                    Py_DECREF(tz);
                    Py_DECREF(off);
                    return NULL;
                }
                Py_DECREF(tz); /* the dict holds the module reference */
            }
            PyThread_release_lock(tep_tz_lock);
            Py_DECREF(off);
            /* tz is borrowed from the append-only cache (no eviction
             * path exists), valid across thread scheduling */
            return PyDateTimeAPI->DateTime_FromDateAndTime(
                d.year, d.month, d.day, d.hour, d.minute, d.second,
                (int)(d.nanosecond / 1000), tz,
                PyDateTimeAPI->DateTimeType);
        }
#else
        /* NOTE: PyObject_CallFunctionObjArgs does NOT steal argument
         * references — every PyLong below is owned and must be
         * released (the old path leaked every boxed argument). */
        switch (teptris_node_kind(n)) {
        case TEPTRIS_DATE_LOCAL: {
            PyObject *a = PyLong_FromLong(d.year);
            PyObject *b = PyLong_FromLong(d.month);
            PyObject *c = PyLong_FromLong(d.day);
            PyObject *r = PyObject_CallFunctionObjArgs(
                tep_dt_date, a, b, c, NULL);
            Py_XDECREF(a); Py_XDECREF(b); Py_XDECREF(c);
            return r;
        }
        case TEPTRIS_TIME_LOCAL: {
            PyObject *a = PyLong_FromLong(d.hour);
            PyObject *b = PyLong_FromLong(d.minute);
            PyObject *c = PyLong_FromLong(d.second);
            PyObject *e = PyLong_FromLong((long)(d.nanosecond / 1000));
            PyObject *r = PyObject_CallFunctionObjArgs(
                tep_dt_time, a, b, c, e, NULL);
            Py_XDECREF(a); Py_XDECREF(b); Py_XDECREF(c); Py_XDECREF(e);
            return r;
        }
        default: break;
        }
        if (teptris_node_kind(n) == TEPTRIS_DATETIME_LOCAL) {
            PyObject *a = PyLong_FromLong(d.year);
            PyObject *b = PyLong_FromLong(d.month);
            PyObject *c = PyLong_FromLong(d.day);
            PyObject *e = PyLong_FromLong(d.hour);
            PyObject *f = PyLong_FromLong(d.minute);
            PyObject *g = PyLong_FromLong(d.second);
            PyObject *h = PyLong_FromLong((long)(d.nanosecond / 1000));
            PyObject *r = PyObject_CallFunctionObjArgs(
                tep_dt_datetime, a, b, c, e, f, g, h, NULL);
            Py_XDECREF(a); Py_XDECREF(b); Py_XDECREF(c);
            Py_XDECREF(e); Py_XDECREF(f); Py_XDECREF(g); Py_XDECREF(h);
            return r;
        }
        /* aware: tz = timezone(timedelta(0, off)); datetime(..., tz) */
        PyObject *d0 = PyLong_FromLong(0);
        PyObject *d1 = PyLong_FromLong((long)d.offset_seconds);
        PyObject *delta = PyObject_CallFunctionObjArgs(
            tep_dt_timedelta, d0, d1, d0, NULL);
        Py_XDECREF(d0); Py_XDECREF(d1);
        if (!delta) return NULL;
        PyObject *tz = PyObject_CallFunctionObjArgs(tep_dt_timezone, delta, NULL);
        Py_DECREF(delta);
        if (!tz) return NULL;
        PyObject *args[8] = {PyLong_FromLong(d.year), PyLong_FromLong(d.month),
                             PyLong_FromLong(d.day), PyLong_FromLong(d.hour),
                             PyLong_FromLong(d.minute),
                             PyLong_FromLong(d.second),
                             PyLong_FromLong((long)(d.nanosecond / 1000)), tz};
        PyObject *aware = PyObject_CallFunctionObjArgs(
            tep_dt_datetime, args[0], args[1], args[2], args[3], args[4],
            args[5], args[6], args[7], NULL);
        for (int i = 0; i < 8; i++) Py_XDECREF(args[i]);
        return aware;
#endif
    }
}
}

static PyObject *ext_load(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *obj;
    if (!PyArg_ParseTuple(args, "O", &obj)) return NULL;
    const char *data = NULL;
    Py_ssize_t len = 0;
    PyObject *keepalive = NULL;
    if (PyUnicode_Check(obj)) {
        keepalive = PyUnicode_AsUTF8String(obj);
        if (!keepalive) return NULL;
        data = PyBytes_AsString(keepalive);
        len = PyBytes_Size(keepalive);
    } else if (PyBytes_Check(obj)) {
        data = PyBytes_AsString(obj);
        len = PyBytes_Size(obj);
    } else {
        PyErr_SetString(PyExc_TypeError, "loads() expects str or bytes");
        return NULL;
    }
    teptris_document *doc = NULL;
    teptris_status st = teptris_parse(data, (size_t)len, NULL, &doc);
    Py_XDECREF(keepalive);
    if (st != TEPTRIS_OK) {
        /* e points into the document's memory: read everything needed
         * BEFORE teptris_document_free — the allocations between here
         * and the attribute writes can reuse the freed block (py3.9's
         * allocator surfaced this as line=0; 3.12 masked it) */
        const teptris_error *e = teptris_document_error(doc);
        size_t line = e->line, column = e->column;
        PyObject *ex = PyObject_CallFunction(TomlDecodeError, "s", e->message);
        teptris_document_free(doc);
        if (ex) {
            PyObject_SetAttrString(ex, "line", PyLong_FromSize_t(line));
            PyObject_SetAttrString(ex, "column", PyLong_FromSize_t(column));
            PyErr_SetObject(TomlDecodeError, ex);
        }
        return NULL;
    }
    PyObject *out = obj_from_node(teptris_document_root(doc));
    teptris_document_free(doc);
    return out;
}

/* -------------------------------------------------------------- batch -- *
 * loads_batch (#108 ask 3, the py twin of teptris-ruby's load_batch):
 * N documents through ONE teptris_parse_batch call. The caller's list
 * is read-only and its bytes objects stay referenced for the duration
 * of the call; scratch is one combined allocation. The first failing
 * document raises DecodeError with its line/column (all docs are
 * freed on that path). */

static PyObject *ext_loads_batch(PyObject *self, PyObject *docs) {
    (void)self;
    if (!PyList_Check(docs)) {
        PyErr_SetString(PyExc_TypeError, "loads_batch() expects a list");
        return NULL;
    }
    Py_ssize_t n = PyList_Size(docs);
    if (n == 0) return PyList_New(0);
    /* one combined scratch blob: data ptrs, lens, doc ptrs, statuses */
    size_t blob = (size_t)n * (sizeof(const char *) + sizeof(size_t) +
                               sizeof(teptris_document *) +
                               sizeof(teptris_status));
    char *scratch = (char *)PyMem_Malloc(blob);
    if (!scratch) return PyErr_NoMemory();
    const char **data = (const char **)scratch;
    size_t *lens = (size_t *)(scratch + (size_t)n * sizeof(const char *));
    teptris_document **docs_out =
        (teptris_document **)(scratch + (size_t)n * (sizeof(const char *) +
                                                    sizeof(size_t)));
    teptris_status *statuses =
        (teptris_status *)(scratch + (size_t)n *
                                       (sizeof(const char *) + sizeof(size_t) +
                                        sizeof(teptris_document *)));
    PyObject *bytes_list = PyList_New((Py_ssize_t)n);
    if (!bytes_list) { PyMem_Free(scratch); return NULL; }
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *item = PyList_GetItem(docs, i); /* borrowed */
        PyObject *bytes;
        if (PyUnicode_Check(item)) {
            bytes = PyUnicode_AsUTF8String(item);
            if (!bytes) {
                Py_DECREF(bytes_list);
                PyMem_Free(scratch);
                return NULL;
            }
        } else if (PyBytes_Check(item)) {
            bytes = item;
            Py_INCREF(bytes);
        } else {
            Py_DECREF(bytes_list);
            PyMem_Free(scratch);
            PyErr_SetString(PyExc_TypeError,
                            "loads_batch() expects str or bytes entries");
            return NULL;
        }
        if (PyList_SetItem(bytes_list, i, bytes) < 0) { /* steals */ PyMem_Free(scratch); return NULL; }
        data[i] = PyBytes_AsString(bytes);
        lens[i] = (size_t)PyBytes_Size(bytes);
    }
    teptris_status batch =
        teptris_parse_batch(data, lens, (size_t)n, NULL, docs_out, statuses);
    if (batch != TEPTRIS_OK) {
        Py_DECREF(bytes_list);
        PyMem_Free(scratch);
        if (batch == TEPTRIS_ERR_ALLOC) return PyErr_NoMemory();
        PyErr_SetString(PyExc_RuntimeError, "batch parse failed");
        return NULL;
    }
    Py_ssize_t first_fail = -1;
    for (Py_ssize_t i = 0; i < n; i++) {
        if (statuses[i] != TEPTRIS_OK && first_fail == -1) first_fail = i;
    }
    if (first_fail != -1) {
        const teptris_error *e = teptris_document_error(docs_out[first_fail]);
        size_t line = e->line, column = e->column;
        PyObject *ex =
            PyObject_CallFunction(TomlDecodeError, "s", e->message);
        for (Py_ssize_t i = 0; i < n; i++) teptris_document_free(docs_out[i]);
        Py_DECREF(bytes_list);
        PyMem_Free(scratch);
        if (ex) {
            PyObject_SetAttrString(ex, "line", PyLong_FromSize_t(line));
            PyObject_SetAttrString(ex, "column", PyLong_FromSize_t(column));
            PyErr_SetObject(TomlDecodeError, ex);
        }
        return NULL;
    }
    PyObject *out = PyList_New((Py_ssize_t)n);
    if (!out) {
        for (Py_ssize_t i = 0; i < n; i++) teptris_document_free(docs_out[i]);
        Py_DECREF(bytes_list);
        PyMem_Free(scratch);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *obj = obj_from_node(teptris_document_root(docs_out[i]));
        teptris_document_free(docs_out[i]);
        if (!obj || PyList_SetItem(out, i, obj) < 0) {
            /* SetItem steals on success; on failure obj is ours to
             * drop and the bail path frees the rest. */
            Py_XDECREF(obj);
            Py_DECREF(out);
            Py_DECREF(bytes_list);
            PyMem_Free(scratch);
            return NULL;
        }
    }
    Py_DECREF(bytes_list);
    PyMem_Free(scratch);
    return out;
}

/* --------------------------------------------------------------- lazy -- *
 * LazyNode (#79, Python twin of teptris-ruby's LazyValue): one
 * parse, host objects materialize along the paths actually accessed.
 *
 * Lifetime: every wrapper holds a strong ref to a `LazyOwner` that
 * owns the parsed document + the borrowed-bytes copy. `LazyNode` is
 * GC-tracked (tp_traverse) so the cycle collector breaks the
 * wrapper → owner edge if the user drops the only outer reference. */

typedef struct {
    PyObject_HEAD
    PyObject *owner;
    const teptris_node *node;
} LazyNode;

typedef struct {
    PyObject_HEAD
    teptris_document *doc;
    PyObject *input;
} LazyOwner;

static PyTypeObject *LazyNodeType = NULL, *LazyOwnerType = NULL;

static void lazy_node_dealloc(PyObject *self) {
    PyObject_GC_UnTrack(self);
    Py_XDECREF(((LazyNode *)self)->owner);
    PyObject_GC_Del(self);
}

static int lazy_node_traverse(PyObject *self, visitproc visit, void *arg) {
    Py_VISIT(((LazyNode *)self)->owner);
    return 0;
}

static int lazy_node_clear(PyObject *self) {
    Py_CLEAR(((LazyNode *)self)->owner);
    return 0;
}

static void lazy_owner_dealloc(PyObject *self) {
    LazyOwner *o = (LazyOwner *)self;
    if (o->doc != NULL) teptris_document_free(o->doc);
    Py_XDECREF(o->input);
    PyObject_GC_Del(self);
}

static int lazy_owner_traverse(PyObject *self, visitproc visit, void *arg) {
    Py_VISIT(((LazyOwner *)self)->input);
    return 0;
}

static int lazy_owner_clear(PyObject *self) {
    Py_CLEAR(((LazyOwner *)self)->input);
    return 0;
}

static PyObject *lazy_wrap(LazyOwner *owner, const teptris_node *n) {
    LazyNode *self = PyObject_GC_New(LazyNode, LazyNodeType);
    if (self == NULL) return NULL;
    Py_INCREF(owner);
    self->owner = (PyObject *)owner;
    self->node = n;
    PyObject_GC_Track(self);
    return (PyObject *)self;
}

static PyObject *lazy_kind(PyObject *self, PyObject *Py_UNUSED(ignored)) {
    PyObject *out;
    switch (teptris_node_kind(((LazyNode *)self)->node)) {
    case TEPTRIS_TABLE: out = PyUnicode_FromString("table"); break;
    case TEPTRIS_ARRAY: out = PyUnicode_FromString("array"); break;
    default: out = PyUnicode_FromString("scalar"); break;
    }
    return out;
}

static Py_ssize_t lazy_len_sq(PyObject *self) {
    const teptris_node *n = ((LazyNode *)self)->node;
    switch (teptris_node_kind(n)) {
    case TEPTRIS_TABLE:
        return (Py_ssize_t)teptris_node_table_length(n);
    case TEPTRIS_ARRAY:
        return (Py_ssize_t)teptris_node_array_length(n);
    default:
        PyErr_SetString(PyExc_TypeError, "scalar has no len()");
        return -1;
    }
}

static PyObject *lazy_value(PyObject *self, PyObject *Py_UNUSED(ignored)) {
    teptris_kind k = teptris_node_kind(((LazyNode *)self)->node);
    switch (k) {
    case TEPTRIS_TABLE:
    case TEPTRIS_ARRAY:
        PyErr_SetString(PyExc_TypeError,
                        "value() only on scalars; use [] for containers");
        return NULL;
    default:
        return obj_from_node(((LazyNode *)self)->node);
    }
}

static PyObject *lazy_subscript(PyObject *self, PyObject *key) {
    LazyNode *self_n = (LazyNode *)self;
    const teptris_node *n = self_n->node;
    const teptris_node *child = NULL;
    switch (teptris_node_kind(n)) {
    case TEPTRIS_TABLE: {
        if (!PyUnicode_Check(key)) {
            PyErr_SetString(PyExc_TypeError, "table key must be str");
            return NULL;
        }
        /* PyUnicode_AsUTF8AndSize is 3.10+; stay compatible with the
         * 3.9 stable ABI by going through bytes. */
        PyObject *kb = PyUnicode_AsUTF8String(key);
        if (kb == NULL) return NULL;
        const char *k = PyBytes_AsString(kb);
        Py_ssize_t kl = PyBytes_Size(kb);
        child = teptris_node_table_get(n, k, (size_t)kl);
        Py_DECREF(kb);
        break;
    }
    case TEPTRIS_ARRAY: {
        Py_ssize_t len = (Py_ssize_t)teptris_node_array_length(n);
        Py_ssize_t i = PyNumber_AsSsize_t(key, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred()) return NULL;
        if (i < 0) i += len;
        if (i < 0 || i >= len) {
            PyErr_SetString(PyExc_IndexError, "array index out of range");
            return NULL;
        }
        child = teptris_node_array_at(n, (size_t)i);
        break;
    }
    default:
        PyErr_SetString(PyExc_TypeError, "scalar is not subscriptable");
        return NULL;
    }
    if (child == NULL) {
        Py_RETURN_NONE;
    }
    return lazy_wrap((LazyOwner *)self_n->owner, child);
}

static PyObject *lazy_iter(PyObject *self) {
    LazyNode *self_n = (LazyNode *)self;
    const teptris_node *n = self_n->node;
    PyObject *list = PyList_New(0);
    if (list == NULL) return NULL;
    switch (teptris_node_kind(n)) {
    case TEPTRIS_TABLE: {
        size_t len = teptris_node_table_length(n);
        for (size_t i = 0; i < len; i++) {
            teptris_view key;
            const teptris_node *v = teptris_node_table_at(n, i, &key);
            PyObject *k = PyUnicode_DecodeUTF8(key.ptr, (Py_ssize_t)key.len, "replace");
            if (k == NULL) goto iter_fail;
            PyObject *wrapped = lazy_wrap((LazyOwner *)self_n->owner, v);
            if (wrapped == NULL) { Py_DECREF(k); goto iter_fail; }
            PyObject *pair = PyTuple_Pack(2, k, wrapped);
            Py_DECREF(k);
            Py_DECREF(wrapped);
            if (pair == NULL) goto iter_fail;
            if (PyList_Append(list, pair) < 0) { Py_DECREF(pair); goto iter_fail; }
            Py_DECREF(pair);
        }
        break;
    }
    case TEPTRIS_ARRAY: {
        size_t len = teptris_node_array_length(n);
        for (size_t i = 0; i < len; i++) {
            PyObject *wrapped = lazy_wrap(
                (LazyOwner *)self_n->owner, teptris_node_array_at(n, i));
            if (wrapped == NULL) goto iter_fail;
            if (PyList_Append(list, wrapped) < 0) {
                Py_DECREF(wrapped);
                goto iter_fail;
            }
            Py_DECREF(wrapped);
        }
        break;
    }
    default:
        PyErr_SetString(PyExc_TypeError, "scalar is not iterable");
        goto iter_fail;
    }
    {
        PyObject *it = PyObject_GetIter(list);
        Py_DECREF(list);
        return it;
    }
iter_fail:
    Py_DECREF(list);
    return NULL;
}

static PyObject *lazy_to_python(PyObject *self, PyObject *Py_UNUSED(ignored)) {
    return obj_from_node(((LazyNode *)self)->node);
}

static PyMethodDef LazyNode_methods[] = {
    {"kind",   lazy_kind,       METH_NOARGS, "Return 'table' | 'array' | 'scalar'."},
    {"value",  lazy_value,      METH_NOARGS, "Materialize a scalar (errors on containers)."},
    {"to_dict",lazy_to_python,  METH_NOARGS, "Eagerly flatten to nested dict/list (tables/arrays)."},
    {"to_list",lazy_to_python,  METH_NOARGS, "Alias of to_dict (root table or array)."},
    {NULL, NULL, 0, NULL}
};

static PyType_Slot LazyNode_slots[] = {
    {Py_tp_dealloc,  lazy_node_dealloc},
    {Py_tp_traverse, lazy_node_traverse},
    {Py_tp_clear,    lazy_node_clear},
    {Py_tp_methods,  LazyNode_methods},
    {Py_tp_getattro, (void *)PyObject_GenericGetAttr},
    {Py_tp_iter,     lazy_iter},
    {Py_sq_length,   lazy_len_sq},
    {Py_mp_subscript, lazy_subscript},
    {0, NULL}
};

static PyType_Spec LazyNode_spec = {
    "teptris._native.LazyNode",  /* name */
    sizeof(LazyNode),            /* basicsize */
    0,                           /* itemsize */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    LazyNode_slots
};

static PyType_Slot LazyOwner_slots[] = {
    {Py_tp_dealloc,  lazy_owner_dealloc},
    {Py_tp_traverse, lazy_owner_traverse},
    {Py_tp_clear,    lazy_owner_clear},
    {0, NULL}
};

static PyType_Spec LazyOwner_spec = {
    "teptris._native.LazyOwner",
    sizeof(LazyOwner),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    LazyOwner_slots
};

static PyObject *ext_lazy_load(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *obj;
    if (!PyArg_ParseTuple(args, "O", &obj)) return NULL;
    const char *data = NULL;
    Py_ssize_t len = 0;
    PyObject *bytes = NULL;
    if (PyUnicode_Check(obj)) {
        bytes = PyUnicode_AsUTF8String(obj);
        if (bytes == NULL) return NULL;
        data = PyBytes_AsString(bytes);
        len = PyBytes_Size(bytes);
    } else if (PyBytes_Check(obj)) {
        bytes = PyBytes_FromObject(obj);  /* own a copy */
        if (bytes == NULL) return NULL;
        data = PyBytes_AsString(bytes);
        len = PyBytes_Size(bytes);
    } else {
        PyErr_SetString(PyExc_TypeError, "loads_lazy() expects str or bytes");
        return NULL;
    }
    LazyOwner *owner =
        (LazyOwner *)PyObject_GC_New(LazyOwner, LazyOwnerType);
    if (owner == NULL) {
        Py_DECREF(bytes);
        return NULL;
    }
    owner->doc = NULL;
    owner->input = bytes;
    PyObject_GC_Track(owner); /* tracked even on the error path so
                               * PyObject_GC_Del is the correct free */
    teptris_status st = teptris_parse(data, (size_t)len, NULL, &owner->doc);
    if (st != TEPTRIS_OK) {
        const teptris_error *e = teptris_document_error(owner->doc);
        size_t line = e->line, column = e->column;
        PyObject *ex = PyObject_CallFunction(TomlDecodeError, "s", e->message);
        teptris_document_free(owner->doc);
        owner->doc = NULL; /* the owner's dealloc must not double-free */
        owner->input = NULL; /* transfer the bytes-ref ownership to the
                              * error path; Py_DECREF(owner) below will
                              * then NOT decref the bytes (avoiding UAF) */
        Py_DECREF(owner);
        Py_DECREF(bytes);
        if (ex != NULL) {
            PyObject_SetAttrString(ex, "line", PyLong_FromSize_t(line));
            PyObject_SetAttrString(ex, "column", PyLong_FromSize_t(column));
            PyErr_SetObject(TomlDecodeError, ex);
        }
        return NULL;
    }
    PyObject *root = lazy_wrap(owner, teptris_document_root(owner->doc));
    /* root holds the only owner ref; both are GC-tracked. The bytes
     * object is alive through owner->input. */
    return root;
}

/* ------------------------------------------------------------------ dump */

static int build_table(teptris_builder *b, PyObject *obj);

/* limited API: attribute reads only */
static PyObject *dt_tzinfo(PyObject *v) {
    return PyObject_GetAttrString(v, "tzinfo");
}

static int dump_check(teptris_status st) {
    if (st == TEPTRIS_OK) return 0;
    if (st == TEPTRIS_ERR_ALLOC) { PyErr_NoMemory(); return -1; }
    PyErr_SetString(PyExc_TypeError, teptris_status_string(st));
    return -1;
}

/* keyed scalar; key == NULL means array element */
static int put_scalar(teptris_builder *b, const char *key, size_t klen,
                      PyObject *v) {
    if (PyBool_Check(v)) {
        return dump_check(teptris_builder_put_boolean(b, key, klen,
                                                      v == Py_True));
    }
    if (PyLong_Check(v)) {
        long long i = PyLong_AsLongLong(v);
        if (i == -1 && PyErr_Occurred()) return -1;
        return dump_check(teptris_builder_put_integer(b, key, klen, i));
    }
    if (PyFloat_Check(v)) {
        return dump_check(teptris_builder_put_float(
            b, key, klen, PyFloat_AsDouble(v)));
    }
    if (PyUnicode_Check(v)) {
        PyObject *kb = PyUnicode_AsUTF8String(v);
        if (!kb) return -1;
        int rc = dump_check(teptris_builder_put_string(
            b, key, klen, PyBytes_AsString(kb), (size_t)PyBytes_Size(kb)));
        Py_DECREF(kb);
        return rc;
    }
    /* datetime first: datetime subclasses date */
    if (PyObject_TypeCheck(v, (PyTypeObject *)tep_dt_datetime)) {
        teptris_datetime dt;
        memset(&dt, 0, sizeof(dt));
#ifdef TEPTRIS_FAST_DT
        /* struct reads: no attribute crossings (utcoffset stays a
         * real call — it is Python-level by design) */
        dt.year = (int32_t)PyDateTime_GET_YEAR(v);
        dt.month = (uint8_t)PyDateTime_GET_MONTH(v);
        dt.day = (uint8_t)PyDateTime_GET_DAY(v);
        dt.hour = (uint8_t)PyDateTime_DATE_GET_HOUR(v);
        dt.minute = (uint8_t)PyDateTime_DATE_GET_MINUTE(v);
        dt.second = (uint8_t)PyDateTime_DATE_GET_SECOND(v);
        dt.nanosecond =
            (uint32_t)PyDateTime_DATE_GET_MICROSECOND(v) * 1000u;
        PyObject *tz = PyDateTime_DATE_GET_TZINFO(v); /* borrowed */
        if (tz == Py_None) {
            return dump_check(teptris_builder_put_datetime(
                b, key, klen, TEPTRIS_DATETIME_LOCAL, &dt));
        }
        PyObject *off = PyObject_CallMethod(tz, "utcoffset", "O", v);
        if (!off) return -1;
        if (off != Py_None && PyDelta_Check(off)) {
            dt.offset_seconds = PyDateTime_DELTA_GET_DAYS(off) * 86400 +
                                PyDateTime_DELTA_GET_SECONDS(off);
        }
        Py_DECREF(off);
        return dump_check(teptris_builder_put_datetime(
            b, key, klen, TEPTRIS_DATETIME_OFFSET, &dt));
#else
        dt.year = (int32_t)attr_long(v, "year");
        dt.month = (uint8_t)attr_long(v, "month");
        dt.day = (uint8_t)attr_long(v, "day");
        dt.hour = (uint8_t)attr_long(v, "hour");
        dt.minute = (uint8_t)attr_long(v, "minute");
        dt.second = (uint8_t)attr_long(v, "second");
        dt.nanosecond = (uint32_t)attr_long(v, "microsecond") * 1000u;
        PyObject *tz = dt_tzinfo(v);
        if (tz == Py_None) {
            Py_DECREF(tz);
            return dump_check(teptris_builder_put_datetime(
                b, key, klen, TEPTRIS_DATETIME_LOCAL, &dt));
        }
        PyObject *off = PyObject_CallMethod(tz, "utcoffset", "O", v);
        Py_DECREF(tz);
        if (!off) return -1;
        if (off != Py_None) {
            dt.offset_seconds =
                attr_long(off, "days") * 86400 +
                attr_long(off, "seconds");
        }
        Py_DECREF(off);
        return dump_check(teptris_builder_put_datetime(
            b, key, klen, TEPTRIS_DATETIME_OFFSET, &dt));
#endif
    }
    if (PyObject_TypeCheck(v, (PyTypeObject *)tep_dt_date)) {
        teptris_datetime dt;
        memset(&dt, 0, sizeof(dt));
#ifdef TEPTRIS_FAST_DT
        dt.year = (int32_t)PyDateTime_GET_YEAR(v);
        dt.month = (uint8_t)PyDateTime_GET_MONTH(v);
        dt.day = (uint8_t)PyDateTime_GET_DAY(v);
#else
        dt.year = (int32_t)attr_long(v, "year");
        dt.month = (uint8_t)attr_long(v, "month");
        dt.day = (uint8_t)attr_long(v, "day");
#endif
        return dump_check(teptris_builder_put_datetime(
            b, key, klen, TEPTRIS_DATE_LOCAL, &dt));
    }
    if (PyObject_TypeCheck(v, (PyTypeObject *)tep_dt_time)) {
        teptris_datetime dt;
        memset(&dt, 0, sizeof(dt));
#ifdef TEPTRIS_FAST_DT
        dt.hour = (uint8_t)PyDateTime_TIME_GET_HOUR(v);
        dt.minute = (uint8_t)PyDateTime_TIME_GET_MINUTE(v);
        dt.second = (uint8_t)PyDateTime_TIME_GET_SECOND(v);
        dt.nanosecond =
            (uint32_t)PyDateTime_TIME_GET_MICROSECOND(v) * 1000u;
#else
        dt.hour = (uint8_t)attr_long(v, "hour");
        dt.minute = (uint8_t)attr_long(v, "minute");
        dt.second = (uint8_t)attr_long(v, "second");
        dt.nanosecond = (uint32_t)attr_long(v, "microsecond") * 1000u;
#endif
        return dump_check(teptris_builder_put_datetime(
            b, key, klen, TEPTRIS_TIME_LOCAL, &dt));
    }
    PyErr_Format(PyExc_TypeError, "cannot dump %.200R",
                 (PyObject *)Py_TYPE(v));
    return -1;
}

/* element position: dict -> table element, list -> nested array */
static int build_value(teptris_builder *b, PyObject *v) {
    if (Py_EnterRecursiveCall(" while dumping a value")) return -1;
    int rc;
    if (PyDict_Check(v)) {
        rc = dump_check(teptris_builder_open_table(b, NULL, 0));
        if (rc == 0) rc = build_table(b, v);
        if (rc == 0) rc = dump_check(teptris_builder_close(b));
    } else if (PyList_Check(v)) {
        rc = dump_check(teptris_builder_open_array(b, NULL, 0));
        Py_ssize_t n = PyList_Size(v);
        for (Py_ssize_t i = 0; rc == 0 && i < n; i++) {
            rc = build_value(b, PyList_GetItem(v, i));
        }
        if (rc == 0) rc = dump_check(teptris_builder_close(b));
    } else {
        rc = put_scalar(b, NULL, 0, v);
    }
    Py_LeaveRecursiveCall();
    return rc;
}

/* table position: keyed members */
static int build_table(teptris_builder *b, PyObject *obj) {
    if (Py_EnterRecursiveCall(" while dumping a table")) return -1;
    PyObject *k, *v;
    Py_ssize_t pos = 0;
    int rc = 0;
    while (rc == 0 && PyDict_Next(obj, &pos, &k, &v)) {
        if (!PyUnicode_Check(k)) {
            PyErr_SetString(PyExc_TypeError, "keys must be strings");
            rc = -1;
            break;
        }
        PyObject *kb = PyUnicode_AsUTF8String(k);
        if (!kb) { rc = -1; break; }
        const char *ks = PyBytes_AsString(kb);
        size_t klen = (size_t)PyBytes_Size(kb);
        if (PyDict_Check(v)) {
            rc = dump_check(teptris_builder_open_table(b, ks, klen));
            if (rc == 0) rc = build_table(b, v);
            if (rc == 0) rc = dump_check(teptris_builder_close(b));
        } else if (PyList_Check(v)) {
            Py_ssize_t n = PyList_Size(v);
            bool all_dict = n > 0;
            for (Py_ssize_t i = 0; i < n; i++) {
                if (!PyDict_Check(PyList_GetItem(v, i))) {
                    all_dict = false;
                    break;
                }
            }
            rc = dump_check(all_dict
                ? teptris_builder_open_array(b, ks, klen)
                : teptris_builder_open_inline_array(b, ks, klen));
            for (Py_ssize_t i = 0; rc == 0 && i < n; i++) {
                rc = build_value(b, PyList_GetItem(v, i));
            }
            if (rc == 0) rc = dump_check(teptris_builder_close(b));
        } else {
            rc = put_scalar(b, ks, klen, v);
        }
        Py_DECREF(kb);
    }
    Py_LeaveRecursiveCall();
    return rc;
}

static PyObject *ext_dumps(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *obj;
    if (!PyArg_ParseTuple(args, "O", &obj)) return NULL;
    if (!PyDict_Check(obj)) {
        PyErr_SetString(PyExc_TypeError,
                        "dumps() expects a dict at the top level");
        return NULL;
    }
    teptris_builder *b = teptris_builder_new();
    if (!b) return PyErr_NoMemory();
    if (build_table(b, obj) < 0) {
        teptris_builder_free(b);
        return NULL;
    }
    teptris_document *doc = NULL;
    teptris_status st = teptris_builder_finish(b, &doc);
    if (st != TEPTRIS_OK) {
        teptris_builder_free(b);
        PyErr_SetString(PyExc_TypeError, teptris_status_string(st));
        return NULL;
    }
    char *buf = NULL;
    size_t len = 0;
    st = teptris_document_emit(doc, &buf, &len);
    teptris_document_free(doc);
    teptris_builder_free(b); /* finish() transferred (and freed) the doc */
    if (st != TEPTRIS_OK) return PyErr_NoMemory();
    PyObject *out = PyUnicode_DecodeUTF8(buf, (Py_ssize_t)len, "strict");
    free(buf);
    return out;
}

static PyObject *ext_version(PyObject *self, PyObject *ignored) {
    (void)self; (void)ignored;
    return PyUnicode_FromString(teptris_version_string());
}


/* ----------------------------------------------------------- plan -- *
 * Descriptor mode — the py twin of teptris-ruby's Teptris::Descriptor
 * (teptris#46): compile a plan tree once, then materialize a TOML
 * document against it in ONE native pass; unplanned keys are never
 * materialized. plan_build takes the flattened spec (rows: list of
 * [name, kind, sub]; first_row: plan_count+1 offsets) and returns an
 * opaque capsule; plan_emit walks one document against it. */

static void plan_capsule_destructor(PyObject *cap) {
    teptris_plan *p =
        (teptris_plan *)PyCapsule_GetPointer(cap, "teptris.plan");
    if (p) teptris_plan_free(p);
}

static PyObject *ext_plan_build(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *rows, *first_row;
    if (!PyArg_ParseTuple(args, "OO", &rows, &first_row)) return NULL;
    if (!PyList_Check(rows) || !PyList_Check(first_row)) {
        PyErr_SetString(PyExc_TypeError, "plan_build expects two lists");
        return NULL;
    }
    Py_ssize_t nrows = PyList_Size(rows);
    Py_ssize_t nplans = PyList_Size(first_row) - 1;
    if (nplans < 1 || nrows < 1) {
        PyErr_SetString(TomlDecodeError, "plan needs >= 1 plan and >= 1 row");
        return NULL;
    }
    teptris_plan_row *crows = calloc((size_t)nrows, sizeof(*crows));
    /* (nplans+1) entries — the ruby port's byte-sized-malloc lesson */
    uint32_t *cfirst = malloc(((size_t)nplans + 1) * sizeof(*cfirst));
    if (!crows || !cfirst) {
        free(crows);
        free(cfirst);
        return PyErr_NoMemory();
    }
    for (Py_ssize_t i = 0; i < nrows; i++) {
        PyObject *row = PyList_GetItem(rows, i); /* borrowed */
        if (!PyList_Check(row) || PyList_Size(row) < 3) {
            goto bad_row;
        }
        /* PyUnicode_AsUTF8 and the PyList_GET_* macros are not in
         * the limited API (the abi3 builds force Py_LIMITED_API). */
        PyObject *name = PyList_GetItem(row, 0);
        if (!PyUnicode_Check(name)) goto bad_row;
        PyObject *name_utf8 = PyUnicode_AsUTF8String(name);
        if (!name_utf8) goto bad_row;
        crows[i].name = strdup(PyBytes_AsString(name_utf8));
        Py_DECREF(name_utf8);
        unsigned long kind = PyLong_AsUnsignedLong(PyList_GetItem(row, 1));
        unsigned long sub = PyLong_AsUnsignedLong(PyList_GetItem(row, 2));
        if (PyErr_Occurred()) goto bad_row;
        crows[i].kind = (uint8_t)kind;
        crows[i].sub = (uint32_t)sub;
        continue;
    bad_row:
        for (Py_ssize_t j = 0; j < i; j++) free((void *)crows[j].name);
        free(crows);
        free(cfirst);
        PyErr_SetString(PyExc_TypeError,
                        "each plan row is [name, kind, sub]");
        return NULL;
    }
    for (Py_ssize_t i = 0; i <= nplans; i++) {
        unsigned long v = PyLong_AsUnsignedLong(
            PyList_GetItem(first_row, i));
        if (PyErr_Occurred()) {
            for (Py_ssize_t j = 0; j < nrows; j++)
                free((void *)crows[j].name);
            free(crows);
            free(cfirst);
            return NULL;
        }
        cfirst[i] = (uint32_t)v;
    }
    teptris_plan_spec spec = {TEPTRIS_PLAN_ABI_VERSION, (uint32_t)nplans,
                              crows, cfirst};
    teptris_status st;
    teptris_plan *plan = teptris_plan_build(&spec, &st);
    for (Py_ssize_t j = 0; j < nrows; j++) free((void *)crows[j].name);
    free(crows);
    free(cfirst);
    if (plan == NULL) {
        PyErr_Format(TomlDecodeError, "invalid plan spec (status %d)",
                     (int)st);
        return NULL;
    }
    return PyCapsule_New(plan, "teptris.plan", plan_capsule_destructor);
}

/* One scalar from a plan result. Datetimes go through the cached
 * datetime classes (limited-API safe — no PyDateTimeAPI struct use,
 * mirroring obj_from_node's generic branch). */
static PyObject *plan_scalar(const teptris_plan_result *res, uint32_t row) {
    switch (teptris_plan_result_value_kind_at(res, row)) {
    case TEPTRIS_STRING: {
        teptris_view s;
        if (teptris_plan_result_string_at(res, row, &s) != TEPTRIS_OK)
            Py_RETURN_NONE;
        return PyUnicode_DecodeUTF8(s.ptr, (Py_ssize_t)s.len, "replace");
    }
    case TEPTRIS_INTEGER: {
        int64_t v = 0;
        if (teptris_plan_result_integer_at(res, row, &v) != TEPTRIS_OK)
            Py_RETURN_NONE;
        return PyLong_FromLongLong(v);
    }
    case TEPTRIS_FLOAT: {
        double v = 0;
        if (teptris_plan_result_float_at(res, row, &v) != TEPTRIS_OK)
            Py_RETURN_NONE;
        return PyFloat_FromDouble(v);
    }
    case TEPTRIS_BOOLEAN: {
        bool v = false;
        if (teptris_plan_result_boolean_at(res, row, &v) != TEPTRIS_OK)
            Py_RETURN_NONE;
        return PyBool_FromLong(v ? 1 : 0);
    }
    default: {
        teptris_datetime d;
        if (teptris_plan_result_datetime_at(res, row, &d) != TEPTRIS_OK)
            Py_RETURN_NONE;
        uint8_t vk = teptris_plan_result_value_kind_at(res, row);
        PyObject *r = NULL;
        if (vk == TEPTRIS_DATE_LOCAL) {
            r = PyObject_CallFunction(tep_dt_date, "lll",
                                      (long)d.year, (long)d.month,
                                      (long)d.day);
        } else if (vk == TEPTRIS_TIME_LOCAL) {
            r = PyObject_CallFunction(tep_dt_time, "llll",
                                      (long)d.hour, (long)d.minute,
                                      (long)d.second,
                                      (long)(d.nanosecond / 1000));
        } else {
            PyObject *y = PyLong_FromLong(d.year);
            PyObject *mo = PyLong_FromLong(d.month);
            PyObject *dy = PyLong_FromLong(d.day);
            PyObject *h = PyLong_FromLong(d.hour);
            PyObject *mi = PyLong_FromLong(d.minute);
            PyObject *s = PyLong_FromLong(d.second);
            PyObject *us =
                PyLong_FromLong((long)(d.nanosecond / 1000));
            if (vk == TEPTRIS_DATETIME_LOCAL) {
                r = PyObject_CallFunctionObjArgs(
                    tep_dt_datetime, y, mo, dy, h, mi, s, us, NULL);
            } else { /* DATETIME_OFFSET */
                /* PyDelta_FromDSU is not in the limited API (the
                 * abi3 builds force Py_LIMITED_API) — construct via
                 * the cached timedelta class, limited-API safe. */
                PyObject *delta =
                    PyObject_CallFunction(tep_dt_timedelta, "iii",
                                          0, (int)d.offset_seconds, 0);
                PyObject *tz = delta
                    ? PyObject_CallFunctionObjArgs(tep_dt_timezone,
                                                   delta, NULL)
                    : NULL;
                Py_XDECREF(delta);
                if (tz) {
                    r = PyObject_CallFunctionObjArgs(
                        tep_dt_datetime, y, mo, dy, h, mi, s, us, tz,
                        NULL);
                    Py_DECREF(tz);
                }
            }
            Py_XDECREF(y); Py_XDECREF(mo); Py_XDECREF(dy);
            Py_XDECREF(h); Py_XDECREF(mi); Py_XDECREF(s);
            Py_XDECREF(us);
        }
        return r;
    }
    }
}

/* Element accessors for COLLECTION rows — element-indexed, distinct
 * from the row-level accessors in plan_scalar. */
static PyObject *plan_elem_scalar(const teptris_plan_result *res,
                                  uint32_t row, uint32_t j) {
    switch (teptris_plan_result_array_value_kind_at(res, row, j)) {
    case TEPTRIS_STRING: {
        teptris_view s;
        if (teptris_plan_result_array_string_at(res, row, j, &s) !=
            TEPTRIS_OK)
            Py_RETURN_NONE;
        return PyUnicode_DecodeUTF8(s.ptr, (Py_ssize_t)s.len, "replace");
    }
    case TEPTRIS_INTEGER: {
        int64_t v = 0;
        if (teptris_plan_result_array_integer_at(res, row, j, &v) !=
            TEPTRIS_OK)
            Py_RETURN_NONE;
        return PyLong_FromLongLong(v);
    }
    case TEPTRIS_FLOAT: {
        double v = 0;
        if (teptris_plan_result_array_float_at(res, row, j, &v) !=
            TEPTRIS_OK)
            Py_RETURN_NONE;
        return PyFloat_FromDouble(v);
    }
    case TEPTRIS_BOOLEAN: {
        bool v = false;
        if (teptris_plan_result_array_boolean_at(res, row, j, &v) !=
            TEPTRIS_OK)
            Py_RETURN_NONE;
        return PyBool_FromLong(v ? 1 : 0);
    }
    default: {
        teptris_datetime d;
        if (teptris_plan_result_array_datetime_at(res, row, j, &d) !=
            TEPTRIS_OK)
            Py_RETURN_NONE;
        uint8_t vk =
            teptris_plan_result_array_value_kind_at(res, row, j);
        PyObject *r = NULL;
        if (vk == TEPTRIS_DATE_LOCAL) {
            r = PyObject_CallFunction(tep_dt_date, "lll",
                                      (long)d.year, (long)d.month,
                                      (long)d.day);
        } else if (vk == TEPTRIS_TIME_LOCAL) {
            r = PyObject_CallFunction(tep_dt_time, "llll",
                                      (long)d.hour, (long)d.minute,
                                      (long)d.second,
                                      (long)(d.nanosecond / 1000));
        } else {
            PyObject *y = PyLong_FromLong(d.year);
            PyObject *mo = PyLong_FromLong(d.month);
            PyObject *dy = PyLong_FromLong(d.day);
            PyObject *h = PyLong_FromLong(d.hour);
            PyObject *mi = PyLong_FromLong(d.minute);
            PyObject *s = PyLong_FromLong(d.second);
            PyObject *us =
                PyLong_FromLong((long)(d.nanosecond / 1000));
            if (vk == TEPTRIS_DATETIME_LOCAL) {
                r = PyObject_CallFunctionObjArgs(
                    tep_dt_datetime, y, mo, dy, h, mi, s, us, NULL);
            } else { /* DATETIME_OFFSET */
                /* PyDelta_FromDSU is not in the limited API (the
                 * abi3 builds force Py_LIMITED_API) — construct via
                 * the cached timedelta class, limited-API safe. */
                PyObject *delta =
                    PyObject_CallFunction(tep_dt_timedelta, "iii",
                                          0, (int)d.offset_seconds, 0);
                PyObject *tz = delta
                    ? PyObject_CallFunctionObjArgs(tep_dt_timezone,
                                                   delta, NULL)
                    : NULL;
                Py_XDECREF(delta);
                if (tz) {
                    r = PyObject_CallFunctionObjArgs(
                        tep_dt_datetime, y, mo, dy, h, mi, s, us, tz,
                        NULL);
                    Py_DECREF(tz);
                }
            }
            Py_XDECREF(y); Py_XDECREF(mo); Py_XDECREF(dy);
            Py_XDECREF(h); Py_XDECREF(mi); Py_XDECREF(s);
            Py_XDECREF(us);
        }
        return r;
    }
    }
}

/* Recursive assembly: one dict per plan. Array-of-tables entries and
 * NESTED rows recurse with borrowed views (freed here, per the plan.h
 * view contract); RAW rows convert through obj_from_node. */
static PyObject *plan_asm(const teptris_plan *p,
                          const teptris_plan_result *res,
                          uint32_t plan_idx) {
    uint32_t n = teptris_plan_row_count(p, plan_idx);
    PyObject *h = PyDict_New();
    if (!h) return NULL;
    for (uint32_t i = 0; i < n; i++) {
        PyObject *k = PyUnicode_FromString(
            teptris_plan_row_name_at(p, plan_idx, i));
        if (!k) { Py_DECREF(h); return NULL; }
        PyObject *val = NULL;
        switch (teptris_plan_result_kind_at(res, i)) {
        case TEPTRIS_PLAN_SCALAR_RESULT:
            val = plan_scalar(res, i);
            break;
        case TEPTRIS_PLAN_ARRAY: {
            uint32_t alen = teptris_plan_result_array_len_at(res, i);
            val = PyList_New((Py_ssize_t)alen);
            if (!val) break;
            for (uint32_t j = 0; j < alen; j++) {
                PyObject *ev = NULL;
                if (teptris_plan_result_array_kind_at(res, i, j) ==
                    TEPTRIS_PLAN_TABLE) {
                    teptris_plan_result *sub =
                        teptris_plan_result_array_entry_at(res, i, j);
                    if (sub != NULL) {
                        ev = plan_asm(
                            p, sub,
                            teptris_plan_row_sub_at(p, plan_idx, i));
                        teptris_plan_result_view_free(sub);
                    }
                } else {
                    ev = plan_elem_scalar(res, i, j);
                }
                if (!ev) { Py_DECREF(val); val = NULL; break; }
                Py_INCREF(ev);
                if (PyList_SetItem(val, (Py_ssize_t)j, ev) < 0) {
                    Py_DECREF(ev);
                    Py_DECREF(val);
                    val = NULL;
                    break;
                }
            }
            break;
        }
        case TEPTRIS_PLAN_TABLE: {
            teptris_plan_result *sub =
                teptris_plan_result_row_view(res, i);
            if (sub != NULL) {
                val = plan_asm(p, sub,
                               teptris_plan_row_sub_at(p, plan_idx, i));
                teptris_plan_result_view_free(sub);
            }
            break;
        }
        case TEPTRIS_PLAN_RAW_RESULT: {
            const teptris_node *raw =
                teptris_plan_result_raw_at(res, i);
            if (raw != NULL) val = obj_from_node(raw);
            break;
        }
        default:
            break; /* MISSING -> None */
        }
        if (!val) {
            val = Py_None;
            Py_INCREF(val);
        }
        int rc = PyDict_SetItem(h, k, val);
        Py_DECREF(k);
        Py_DECREF(val);
        if (rc < 0) { Py_DECREF(h); return NULL; }
    }
    return h;
}

static PyObject *ext_plan_emit(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *cap, *obj;
    if (!PyArg_ParseTuple(args, "OO", &cap, &obj)) return NULL;
    teptris_plan *plan =
        (teptris_plan *)PyCapsule_GetPointer(cap, "teptris.plan");
    if (!plan) return NULL;
    const char *data = NULL;
    Py_ssize_t len = 0;
    PyObject *keepalive = NULL;
    if (PyUnicode_Check(obj)) {
        keepalive = PyUnicode_AsUTF8String(obj);
        if (!keepalive) return NULL;
        data = PyBytes_AsString(keepalive);
        len = PyBytes_Size(keepalive);
    } else if (PyBytes_Check(obj)) {
        data = PyBytes_AsString(obj);
        len = PyBytes_Size(obj);
    } else {
        PyErr_SetString(PyExc_TypeError,
                        "plan_emit expects str or bytes");
        return NULL;
    }
    teptris_document *doc = NULL;
    teptris_status st = teptris_parse(data, (size_t)len, NULL, &doc);
    Py_XDECREF(keepalive);
    if (st != TEPTRIS_OK) {
        /* read e BEFORE the free — it points into the document */
        const teptris_error *e = teptris_document_error(doc);
        size_t line = e->line, column = e->column;
        PyObject *ex =
            PyObject_CallFunction(TomlDecodeError, "s", e->message);
        teptris_document_free(doc);
        if (ex) {
            PyObject_SetAttrString(ex, "line", PyLong_FromSize_t(line));
            PyObject_SetAttrString(ex, "column",
                                   PyLong_FromSize_t(column));
            PyErr_SetObject(TomlDecodeError, ex);
        }
        return NULL;
    }
    teptris_plan_result *res =
        teptris_plan_walk(plan, teptris_document_root(doc), &st);
    if (res == NULL) {
        teptris_document_free(doc);
        PyErr_SetString(TomlDecodeError, "plan walk failed");
        return NULL;
    }
    PyObject *out = plan_asm(plan, res, 0);
    teptris_plan_result_free(res);
    teptris_document_free(doc);
    return out;
}

static PyMethodDef methods[] = {
    {"loads", ext_load, METH_VARARGS, "Parse TOML into Python objects."},
    {"loads_batch", ext_loads_batch, METH_O,
     "Parse a list of TOML documents in one C call."},
    {"loads_lazy", ext_lazy_load, METH_VARARGS,
     "Parse TOML into a LazyNode; host objects materialize on access."},
    {"dumps", ext_dumps, METH_VARARGS,
     "Serialize a dict tree to canonical TOML via the shared emitter."},
    {"engine_version", ext_version, METH_NOARGS,
     "Return the libteptris engine version string."},
    {"plan_build", ext_plan_build, METH_VARARGS,
     "Compile a Descriptor plan (rows, first_row)."},
    {"plan_emit", ext_plan_emit, METH_VARARGS,
     "Materialize one TOML document against a plan."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef mod = {PyModuleDef_HEAD_INIT, "teptris._native",
    "Native teptris binding (no fallback).", -1, methods};

PyMODINIT_FUNC PyInit__native(void) {
    PyObject *m = PyModule_Create(&mod);
    if (!m) return NULL;
    PyObject *dtmod = PyImport_ImportModule("datetime");
    if (dtmod == NULL) { Py_DECREF(m); return NULL; }
    tep_dt_datetime = PyObject_GetAttrString(dtmod, "datetime");
    tep_dt_date = PyObject_GetAttrString(dtmod, "date");
    tep_dt_time = PyObject_GetAttrString(dtmod, "time");
    tep_dt_timedelta = PyObject_GetAttrString(dtmod, "timedelta");
    tep_dt_timezone = PyObject_GetAttrString(dtmod, "timezone");
    Py_DECREF(dtmod);
    if (!tep_dt_datetime || !tep_dt_date || !tep_dt_time ||
        !tep_dt_timedelta || !tep_dt_timezone) {
        Py_DECREF(m);
        return NULL;
    }
#ifdef TEPTRIS_FAST_DT
    PyDateTime_IMPORT;
    if (PyDateTimeAPI == NULL) { Py_DECREF(m); return NULL; }
    tep_tz_cache = PyDict_New();
    if (tep_tz_cache == NULL) { Py_DECREF(m); return NULL; }
    tep_tz_lock = PyThread_allocate_lock();
    if (tep_tz_lock == NULL) { Py_DECREF(m); return NULL; }
#endif
    TomlDecodeError = PyErr_NewException("teptris._native.DecodeError", NULL, NULL);
    Py_INCREF(TomlDecodeError);
    PyModule_AddObject(m, "DecodeError", TomlDecodeError);
    LazyNodeType = (PyTypeObject *)PyType_FromSpec(&LazyNode_spec);
    LazyOwnerType = (PyTypeObject *)PyType_FromSpec(&LazyOwner_spec);
    if (LazyNodeType == NULL || LazyOwnerType == NULL) {
        Py_DECREF(m);
        return NULL;
    }
    Py_INCREF(LazyNodeType);
    Py_INCREF(LazyOwnerType);
    PyModule_AddObject(m, "LazyNode", (PyObject *)LazyNodeType);
    PyModule_AddObject(m, "LazyOwner", (PyObject *)LazyOwnerType);
    return m;
}
