"""tomli_w-shaped TOML writer (pure Python, mirrors teptris-ruby dump)."""
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
