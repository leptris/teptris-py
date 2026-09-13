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

static PyMethodDef methods[] = {
    {"loads", ext_load, METH_VARARGS, "Parse TOML into Python objects."},
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
