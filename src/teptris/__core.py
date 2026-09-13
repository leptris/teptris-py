"""ctypes core for teptris-py — no C extension, one shared library.

Library resolution (yeptris-py chain): TEPTRIS_LIB_PATH env, vendored
platform directory, the sibling C checkout's build tree, then the
system loader.
"""
import ctypes
import ctypes.util
import struct
import datetime as _dt
import os
from pathlib import Path

__all__ = ["TOMLDecodeError", "dumps", "load", "loads", "library_path"]


class TOMLDecodeError(ValueError):
    def __init__(self, msg, line=0, column=0):
        self.line = line
        self.column = column
        super().__init__(msg)


_NAMES = ("libteptris.dylib", "libteptris.so", "libteptris.dll")


def _candidates():
    out = []
    env = os.environ.get("TEPTRIS_LIB_PATH")
    if env:
        out.append(env)
    here = Path(__file__).resolve().parent
    out.extend(str(p) for p in sorted(here.glob("_platform/*/libteptris.*")))
    for name in _NAMES:
        for build in ("build", "build-release", "build-shared", "build-bench"):
            out.append(str(here.parent.parent / "teptris" / build / "src" / name))
        found = ctypes.util.find_library("teptris")
        if found:
            out.append(found)
    return out


_lib = None
library_path = None
for _c in _candidates():
    try:
        _lib = ctypes.CDLL(_c)
        library_path = _c
        break
    except OSError:
        continue
if _lib is None:
    raise ImportError("libteptris not found (set TEPTRIS_LIB_PATH)")

_OK = 0
_STRING, _INTEGER, _FLOAT, _BOOLEAN = 0, 1, 2, 3
_DT, _DT_LOCAL, _DATE, _TIME = 4, 5, 6, 7
_ARRAY, _TABLE = 8, 9

class _ViewP(ctypes.Structure):
    _fields_ = [("ptr", ctypes.c_char_p), ("len", ctypes.c_size_t)]


class _DateTime(ctypes.Structure):
    _fields_ = [("year", ctypes.c_int32), ("month", ctypes.c_uint8),
                ("day", ctypes.c_uint8), ("hour", ctypes.c_uint8),
                ("minute", ctypes.c_uint8), ("second", ctypes.c_uint8),
                ("_pad", ctypes.c_uint8 * 3), ("nanosecond", ctypes.c_uint32),
                ("offset_seconds", ctypes.c_int32)]


_lib.teptris_parse.restype = ctypes.c_int
_lib.teptris_parse.argtypes = [ctypes.c_char_p, ctypes.c_size_t,
                               ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
_lib.teptris_document_free.argtypes = [ctypes.c_void_p]
_lib.teptris_document_error.restype = ctypes.c_void_p
_lib.teptris_document_root.restype = ctypes.c_void_p
_lib.teptris_document_root.argtypes = [ctypes.c_void_p]
_lib.teptris_node_kind.restype = ctypes.c_int
_lib.teptris_node_kind.argtypes = [ctypes.c_void_p]
_lib.teptris_node_string.argtypes = [ctypes.c_void_p, ctypes.POINTER(_ViewP)]
_lib.teptris_node_integer.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int64)]
_lib.teptris_node_float.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_double)]
_lib.teptris_node_boolean.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_bool)]
_lib.teptris_node_datetime.argtypes = [ctypes.c_void_p, ctypes.POINTER(_DateTime)]
_lib.teptris_node_array_length.restype = ctypes.c_size_t
_lib.teptris_node_array_length.argtypes = [ctypes.c_void_p]
_lib.teptris_node_array_at.restype = ctypes.c_void_p
_lib.teptris_node_array_at.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
_lib.teptris_node_table_length.restype = ctypes.c_size_t
_lib.teptris_node_table_length.argtypes = [ctypes.c_void_p]
_lib.teptris_node_table_at.restype = ctypes.c_void_p
_lib.teptris_node_table_at.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                       ctypes.POINTER(_ViewP)]
_lib.teptris_document_flatten.restype = ctypes.c_int
_lib.teptris_document_flatten.argtypes = [ctypes.c_void_p,
                                          ctypes.POINTER(ctypes.c_void_p),
                                          ctypes.POINTER(ctypes.c_size_t)]
_lib.teptris_flatten_free.argtypes = [ctypes.c_void_p]


class _Error(ctypes.Structure):
    _fields_ = [("status", ctypes.c_int), ("line", ctypes.c_size_t),
                ("column", ctypes.c_size_t), ("message", ctypes.c_char_p)]


def loads(data):
    if isinstance(data, str):
        data = data.encode("utf-8")
    elif not isinstance(data, (bytes, bytearray)):
        raise TypeError("loads() expects str or bytes")
    data = bytes(data)
    handle = ctypes.c_void_p()
    st = _lib.teptris_parse(data, len(data), None, ctypes.byref(handle))
    if st != _OK:
        msg, line, column = "parse failed", 0, 0
        p = _lib.teptris_document_error(handle)
        if p:
            e = ctypes.cast(p, ctypes.POINTER(_Error)).contents
            msg = e.message.decode("utf-8", "replace")
            line, column = e.line, e.column
        _lib.teptris_document_free(handle)
        raise TOMLDecodeError(f"{msg} (line {line} column {column})",
                              line, column)
    try:
        return _loads_flat(handle)
    finally:
        _lib.teptris_document_free(handle)


def load(fp):
    if hasattr(fp, "read"):
        return loads(fp.read())
    with open(fp, "rb") as f:
        return loads(f.read())


_U32 = struct.Struct("<I")
_I64 = struct.Struct("<q")
_F64 = struct.Struct("<d")
_DTS = struct.Struct("<qBBBBBIq")  # year mon day hour min sec ns offset (25 B)


def _loads_flat(handle):
    bufp, lenp = ctypes.c_void_p(), ctypes.c_size_t()
    st = _lib.teptris_document_flatten(handle, ctypes.byref(bufp),
                                       ctypes.byref(lenp))
    if st != _OK:
        raise TOMLDecodeError("flatten failed")
    try:
        data = ctypes.string_at(bufp.value, lenp.value)
        value, _ = _decode(data, 0)
        return value
    finally:
        _lib.teptris_flatten_free(bufp.value)


def _decode(d, pos):
    tag = d[pos]
    if tag == 0x01:  # table
        (n,) = _U32.unpack_from(d, pos + 1)
        pos += 5
        out = {}
        for _ in range(n):
            (kl,) = _U32.unpack_from(d, pos)
            pos += 4
            key = d[pos:pos + kl].decode("utf-8")
            pos += kl
            val, pos = _decode(d, pos)
            out[key] = val
        return out, pos
    if tag == 0x02:  # array
        (n,) = _U32.unpack_from(d, pos + 1)
        pos += 5
        arr = []
        for _ in range(n):
            v, pos = _decode(d, pos)
            arr.append(v)
        return arr, pos
    if tag == 0x03:  # string
        (l,) = _U32.unpack_from(d, pos + 1)
        return d[pos + 5:pos + 5 + l].decode("utf-8"), pos + 5 + l
    if tag == 0x04:
        return _I64.unpack_from(d, pos + 1)[0], pos + 9
    if tag == 0x05:
        return _F64.unpack_from(d, pos + 1)[0], pos + 9
    if tag == 0x06:
        return False, pos + 1
    if tag == 0x07:
        return True, pos + 1
    if 0x08 <= tag <= 0x0B:
        y, mon, day, h, mi, sec, ns, off = _DTS.unpack_from(d, pos + 1)
        micro = ns // 1000
        if tag == 0x0A:
            return _dt.date(y, mon, day), pos + 26
        if tag == 0x0B:
            return _dt.time(h, mi, sec, micro), pos + 26
        base = _dt.datetime(y, mon, day, h, mi, sec, micro)
        if tag == 0x08:
            return base.replace(tzinfo=_dt.timezone(_dt.timedelta(seconds=off))), pos + 26
        return base, pos + 26
    raise TOMLDecodeError(f"corrupt flat buffer at {pos}")


# ---- dumps (tomli_w-shaped) ---------------------------------------------

_ESCAPES = {"\\": "\\\\", '"': '\\"', "\b": "\\b", "\f": "\\f",
            "\n": "\\n", "\r": "\\r", "\t": "\\t"}


def _key(k):
    k = str(k)
    if k.isascii() and k and all(c.isalnum() or c in "_-" for c in k):
        return k
    if "'" not in k and all(ord(c) >= 0x20 for c in k):
        return f"'{k}'"
    return '"' + "".join(_ESCAPES.get(c, c) for c in k) + '"'


def _value(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        if v != v:
            return "nan"
        if v == float("inf"):
            return "inf"
        if v == float("-inf"):
            return "-inf"
        s = repr(v)
        return s if any(c in s for c in ".eE") else s + ".0"
    if isinstance(v, str):
        if "'" not in v and all(ord(c) >= 0x20 for c in v):
            return f"'{v}'"
        return '"' + "".join(_ESCAPES.get(c, c) for c in v) + '"'
    if isinstance(v, _dt.datetime):
        s = v.isoformat()
        return s if v.tzinfo is not None else s
    if isinstance(v, _dt.date):
        return v.isoformat()
    if isinstance(v, _dt.time):
        return v.isoformat()
    if isinstance(v, list):
        return "[" + ", ".join(_value(e) for e in v) + "]"
    if isinstance(v, dict):
        return "{" + ", ".join(f"{_key(k)} = {_value(e)}"
                               for k, e in v.items()) + "}"
    raise TypeError(f"cannot dump {type(v).__name__}")


def _is_aot(v):
    return isinstance(v, list) and v and all(isinstance(e, dict) for e in v)


def _emit_table(obj, out, prefix):
    for k, v in obj.items():
        if isinstance(v, dict) or _is_aot(v):
            continue
        out.append(f"{_key(k)} = {_value(v)}\n")
    for k, v in obj.items():
        if not (isinstance(v, dict) or _is_aot(v)):
            continue
        path = f"{prefix}.{_key(k)}" if prefix else _key(k)
        if _is_aot(v):
            for item in v:
                out.append(f"[[{path}]]\n")
                _emit_table(item, out, path)
        else:
            out.append(f"[{path}]\n")
            _emit_table(v, out, path)


def dumps(obj):
    if not isinstance(obj, dict):
        raise TypeError("dumps() expects a dict at the top level")
    out = []
    _emit_table(obj, out, None)
    return "".join(out)
