#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <datetime.h>
#include "teptris/teptris.h"

static PyObject *TomlDecodeError;

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
            PyList_SET_ITEM(a, (Py_ssize_t)i, v);
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
            return PyDate_FromDate(d.year, d.month, d.day);
        case TEPTRIS_TIME_LOCAL:
            return PyTime_FromTime(d.hour, d.minute, d.second,
                                   (int)(d.nanosecond / 1000));
        default: {
            if (teptris_node_kind(n) == TEPTRIS_DATETIME_LOCAL)
                return PyDateTime_FromDateAndTime(d.year, d.month, d.day,
                    d.hour, d.minute, d.second, (int)(d.nanosecond / 1000));
            /* aware datetime: construct naive then set tzinfo via replace() */
            int off = d.offset_seconds;
            PyObject *delta = PyDelta_FromDSU(0, off, 0);
            if (!delta) return NULL;
            PyObject *tz = PyTimeZone_FromOffset(delta);
            Py_DECREF(delta);
            if (!tz) return NULL;
            /* datetime(y, m, d, h, mi, s, us, tz) — positional ctor */
            PyObject *aware = PyObject_CallFunctionObjArgs(
                (PyObject *)PyDateTimeAPI->DateTimeType,
                PyLong_FromLong(d.year), PyLong_FromLong(d.month),
                PyLong_FromLong(d.day), PyLong_FromLong(d.hour),
                PyLong_FromLong(d.minute), PyLong_FromLong(d.second),
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
    Py_buffer view;
    if (PyObject_GetBuffer(obj, &view, PyBUF_SIMPLE) < 0) return NULL;
    const char *data = (const char *)view.buf;
    Py_ssize_t len = (Py_ssize_t)view.len;
    teptris_document *doc = NULL;
    teptris_status st = teptris_parse(data, (size_t)len, NULL, &doc);
    PyBuffer_Release(&view);
    if (st != TEPTRIS_OK) {
        const teptris_error *e = teptris_document_error(doc);
        PyObject *ex = PyObject_CallFunction(TomlDecodeError, "s", e->message);
        teptris_document_free(doc);
        if (ex) {
            PyObject_SetAttrString(ex, "line", PyLong_FromSize_t(e->line));
            PyObject_SetAttrString(ex, "column", PyLong_FromSize_t(e->column));
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
            b, key, klen, PyFloat_AS_DOUBLE(v)));
    }
    if (PyUnicode_Check(v)) {
        Py_ssize_t len = 0;
        const char *s = PyUnicode_AsUTF8AndSize(v, &len);
        if (!s) return -1;
        return dump_check(teptris_builder_put_string(b, key, klen, s,
                                                     (size_t)len));
    }
    /* PyDateTime_Check first: datetime subclasses date */
    if (PyDateTime_Check(v)) {
        teptris_datetime dt;
        memset(&dt, 0, sizeof(dt));
        dt.year = PyDateTime_GET_YEAR(v);
        dt.month = PyDateTime_GET_MONTH(v);
        dt.day = PyDateTime_GET_DAY(v);
        dt.hour = PyDateTime_DATE_GET_HOUR(v);
        dt.minute = PyDateTime_DATE_GET_MINUTE(v);
        dt.second = PyDateTime_DATE_GET_SECOND(v);
        dt.nanosecond =
            (uint32_t)PyDateTime_DATE_GET_MICROSECOND(v) * 1000u;
        PyObject *tz = PyDateTime_DATE_GET_TZINFO(v);
        if (tz != Py_None) {
            PyObject *off = PyObject_CallMethod(tz, "utcoffset", "O", v);
            if (!off) return -1;
            if (off != Py_None) {
                dt.offset_seconds =
                    PyDateTime_DELTA_GET_DAYS(off) * 86400 +
                    PyDateTime_DELTA_GET_SECONDS(off);
            }
            Py_DECREF(off);
            return dump_check(teptris_builder_put_datetime(
                b, key, klen, TEPTRIS_DATETIME_OFFSET, &dt));
        }
        return dump_check(teptris_builder_put_datetime(
            b, key, klen, TEPTRIS_DATETIME_LOCAL, &dt));
    }
    if (PyDate_Check(v)) {
        teptris_datetime dt;
        memset(&dt, 0, sizeof(dt));
        dt.year = PyDateTime_GET_YEAR(v);
        dt.month = PyDateTime_GET_MONTH(v);
        dt.day = PyDateTime_GET_DAY(v);
        return dump_check(teptris_builder_put_datetime(
            b, key, klen, TEPTRIS_DATE_LOCAL, &dt));
    }
    if (PyTime_Check(v)) {
        teptris_datetime dt;
        memset(&dt, 0, sizeof(dt));
        dt.hour = PyDateTime_TIME_GET_HOUR(v);
        dt.minute = PyDateTime_TIME_GET_MINUTE(v);
        dt.second = PyDateTime_TIME_GET_SECOND(v);
        dt.nanosecond =
            (uint32_t)PyDateTime_TIME_GET_MICROSECOND(v) * 1000u;
        return dump_check(teptris_builder_put_datetime(
            b, key, klen, TEPTRIS_TIME_LOCAL, &dt));
    }
    PyErr_Format(PyExc_TypeError, "cannot dump %.200s",
                 Py_TYPE(v)->tp_name);
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
        Py_ssize_t n = PyList_GET_SIZE(v);
        for (Py_ssize_t i = 0; rc == 0 && i < n; i++) {
            rc = build_value(b, PyList_GET_ITEM(v, i));
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
        Py_ssize_t klen = 0;
        const char *ks = PyUnicode_AsUTF8AndSize(k, &klen);
        if (!ks) { rc = -1; break; }
        if (PyDict_Check(v)) {
            rc = dump_check(teptris_builder_open_table(b, ks, (size_t)klen));
            if (rc == 0) rc = build_table(b, v);
            if (rc == 0) rc = dump_check(teptris_builder_close(b));
        } else if (PyList_Check(v)) {
            Py_ssize_t n = PyList_GET_SIZE(v);
            bool all_dict = n > 0;
            for (Py_ssize_t i = 0; i < n; i++) {
                if (!PyDict_Check(PyList_GET_ITEM(v, i))) {
                    all_dict = false;
                    break;
                }
            }
            rc = dump_check(all_dict
                ? teptris_builder_open_array(b, ks, (size_t)klen)
                : teptris_builder_open_inline_array(b, ks, (size_t)klen));
            for (Py_ssize_t i = 0; rc == 0 && i < n; i++) {
                rc = build_value(b, PyList_GET_ITEM(v, i));
            }
            if (rc == 0) rc = dump_check(teptris_builder_close(b));
        } else {
            rc = put_scalar(b, ks, (size_t)klen, v);
        }
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
    PyDateTime_IMPORT;
    TomlDecodeError = PyErr_NewException("teptris._native.DecodeError", NULL, NULL);
    Py_INCREF(TomlDecodeError);
    PyModule_AddObject(m, "DecodeError", TomlDecodeError);
    return m;
}
