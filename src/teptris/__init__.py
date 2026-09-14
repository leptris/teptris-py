"""teptris — TOML for Python at libleptris speed.

Native extension over libteptris (C11 TOML 1.1 grammar), no fallback. API shape
mirrors tomllib/tomli: loads(str|bytes) -> dict raising TOMLDecodeError;
dumps(obj) -> str in the tomli_w spirit. Datetime mapping mirrors
tomllib: aware/naive datetime, date, time.
"""
from ._native import loads as _loads
from ._native import dumps
from ._native import DecodeError as _DecodeError


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


def load(fp):
    if hasattr(fp, "read"):
        return loads(fp.read())
    with open(fp, "rb") as f:
        return loads(f.read())


__all__ = ["TOMLDecodeError", "dumps", "load", "loads"]
