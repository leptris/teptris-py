"""teptris — TOML for Python at libleptris speed.

Native extension over libteptris (C11 TOML 1.1 grammar), no fallback. API shape
mirrors tomllib/tomli: loads(str|bytes) -> dict raising TOMLDecodeError;
dumps(obj) -> str in the tomli_w spirit. Datetime mapping mirrors
tomllib: aware/naive datetime, date, time.

Lazy twin: loads_lazy(str|bytes) -> LazyNode (teptris#79) — one parse,
host objects materialize only along the paths actually accessed.
"""
from ._native import loads as _loads
from ._native import loads_lazy as _loads_lazy
from ._native import dumps
from ._native import DecodeError as _DecodeError
from ._native import LazyNode


class TOMLDecodeError(_DecodeError, ValueError):
    pass


def loads(data):
    if isinstance(data, str):
        data = data.encode("utf-8")
    elif not isinstance(data, (bytes, bytearray)):
        raise TypeError("loads() expects str or bytes")
    try:
        return _loads(bytes(data))
    except _DecodeError as e:
        err = TOMLDecodeError(str(e))
        err.line = getattr(e, "line", 0)
        err.column = getattr(e, "column", 0)
        raise err from None


def loads_lazy(data):
    """One parse, host objects materialize only along the paths actually
    accessed (teptris#79). Same datetime contract as `loads`.

    The returned LazyNode wraps the parsed tree; the document and the
    borrowed-bytes input are kept alive by the wrapper until the GC
    drops it. Use `to_dict()` / `to_list()` to flatten eagerly.
    """
    if isinstance(data, str):
        data = data.encode("utf-8")
    elif not isinstance(data, (bytes, bytearray)):
        raise TypeError("loads_lazy() expects str or bytes")
    try:
        return _loads_lazy(bytes(data))
    except _DecodeError as e:
        err = TOMLDecodeError(str(e))
        err.line = getattr(e, "line", 0)
        err.column = getattr(e, "column", 0)
        raise err from None


def load(fp):
    if hasattr(fp, "read"):
        return loads(fp.read())
    with open(fp, "rb") as f:
        return loads(f.read())


__all__ = ["TOMLDecodeError", "dumps", "load", "loads", "loads_lazy",
           "LazyNode"]
