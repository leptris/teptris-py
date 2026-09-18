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
    return m;
}
