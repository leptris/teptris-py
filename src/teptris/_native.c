/* teptris._native — TOML 1.1 parser/writer binding (Python C API).

Eager path: `loads(data)` parses TOML into nested dict/list/leaf objects.
Lazy path: `loads_lazy(data)` returns a `LazyNode` wrapping the parsed
tree — host objects materialize only on access (`[]` for tables/arrays,
`value()` for scalars, `to_dict()` / `to_list()` to flatten).

The C ABI for both paths lives in libteptris (`teptris/teptris.h`). The
lazy twin avoids materializing the intermediate dict/list for the many
shape where only a small subset of the tree is touched (teptris#79). */
#define PY_SSIZE_T_CLEAN
#ifndef Py_LIMITED_API
/* abi3 (#20): the datetime C-API is not in the limited API, so all
 * datetime work goes through cached callables/attributes instead */
#define Py_LIMITED_API 0x03090000
#endif
#include <Python.h>
#include "teptris/teptris.h"

static PyObject *TomlDecodeError;
/* datetime module callables (limited API: the C-API is unavailable) */
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
        switch (teptris_node_kind(n)) {
        case TEPTRIS_DATE_LOCAL:
            return PyObject_CallFunctionObjArgs(
                tep_dt_date, PyLong_FromLong(d.year), PyLong_FromLong(d.month),
                PyLong_FromLong(d.day), NULL);
        case TEPTRIS_TIME_LOCAL:
            return PyObject_CallFunctionObjArgs(
                tep_dt_time, PyLong_FromLong(d.hour),
                PyLong_FromLong(d.minute), PyLong_FromLong(d.second),
                PyLong_FromLong((long)(d.nanosecond / 1000)), NULL);
        default: {
            PyObject *us = PyLong_FromLong((long)(d.nanosecond / 1000));
            if (!us) return NULL;
            if (teptris_node_kind(n) == TEPTRIS_DATETIME_LOCAL)
                return PyObject_CallFunctionObjArgs(
                    tep_dt_datetime, PyLong_FromLong(d.year),
                    PyLong_FromLong(d.month), PyLong_FromLong(d.day),
                    PyLong_FromLong(d.hour), PyLong_FromLong(d.minute),
                    PyLong_FromLong(d.second), us, NULL);
            Py_DECREF(us);
            /* aware: tz = timezone(timedelta(0, off)); datetime(..., tz) */
            PyObject *delta = PyObject_CallFunctionObjArgs(
                tep_dt_timedelta, PyLong_FromLong(0),
                PyLong_FromLong((long)d.offset_seconds), PyLong_FromLong(0),
                NULL);
            if (!delta) return NULL;
            PyObject *tz = PyObject_CallFunctionObjArgs(
                tep_dt_timezone, delta, NULL);
            Py_DECREF(delta);
            if (!tz) return NULL;
            PyObject *aware = PyObject_CallFunctionObjArgs(
                tep_dt_datetime, PyLong_FromLong(d.year),
                PyLong_FromLong(d.month), PyLong_FromLong(d.day),
                PyLong_FromLong(d.hour), PyLong_FromLong(d.minute),
                PyLong_FromLong(d.second),
                PyLong_FromLong((long)(d.nanosecond / 1000)), tz, NULL);
            Py_DECREF(tz);
            return aware;
        }}
    }}
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
    }
    if (PyObject_TypeCheck(v, (PyTypeObject *)tep_dt_date)) {
        teptris_datetime dt;
        memset(&dt, 0, sizeof(dt));
        dt.year = (int32_t)attr_long(v, "year");
        dt.month = (uint8_t)attr_long(v, "month");
        dt.day = (uint8_t)attr_long(v, "day");
        return dump_check(teptris_builder_put_datetime(
            b, key, klen, TEPTRIS_DATE_LOCAL, &dt));
    }
    if (PyObject_TypeCheck(v, (PyTypeObject *)tep_dt_time)) {
        teptris_datetime dt;
        memset(&dt, 0, sizeof(dt));
        dt.hour = (uint8_t)attr_long(v, "hour");
        dt.minute = (uint8_t)attr_long(v, "minute");
        dt.second = (uint8_t)attr_long(v, "second");
        dt.nanosecond = (uint32_t)attr_long(v, "microsecond") * 1000u;
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

static PyMethodDef methods[] = {
    {"loads", ext_load, METH_VARARGS, "Parse TOML into Python objects."},
    {"loads_batch", ext_loads_batch, METH_O,
     "Parse a list of TOML documents in one C call."},
    {"loads_lazy", ext_lazy_load, METH_VARARGS,
     "Parse TOML into a LazyNode; host objects materialize on access."},
    {"dumps", ext_dumps, METH_VARARGS,
     "Serialize a dict tree to canonical TOML via the shared emitter."},
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
