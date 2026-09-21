"""teptris — TOML for Python at libleptris speed.

Native extension over libteptris (C11 TOML 1.1 grammar), no fallback. API shape
mirrors tomllib/tomli: loads(str|bytes) -> dict raising TOMLDecodeError;
dumps(obj) -> str in the tomli_w spirit. Datetime mapping mirrors
tomllib: aware/naive datetime, date, time.

Lazy twin: loads_lazy(str|bytes) -> LazyNode (teptris#79) — one parse,
host objects materialize only along the paths actually accessed.
"""
from ._native import loads as _loads
from ._native import loads_batch as _loads_batch
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


def loads_batch(docs):
    """Parse a list of TOML documents in ONE C call (teptris-ruby#108
    ask 3, the py twin of teptris-ruby's load_batch). Returns a list of
    dicts with the same datetime contract as `loads`. The first failing
    document raises TOMLDecodeError carrying its line/column.

    Honest perf shape (benchmark/batch_tier.py): per-doc `loads` in a
    tight loop is FASTER on every measured shape (CPython's per-call
    overhead amortizes; the batch adds scratch + tight object
    pressure). Reach for loads_batch when you want the single surface,
    one C crossing, and the per-doc error report — not for speed.
    """
    if isinstance(docs, (str, bytes, bytearray)) or not hasattr(docs, "__len__"):
        raise TypeError("loads_batch() expects a list of str or bytes")
    # encode here, not in C: the loop's plain list build beats
    # per-item PyUnicode_AsUTF8String + SetItem inside the C loop
    # (measured 5.6x vs 15.9x vs per-doc on the 2000-doc tiny shape)
    encoded = []
    for d in docs:
        if isinstance(d, str):
            encoded.append(d.encode("utf-8"))
        elif isinstance(d, (bytes, bytearray)):
            encoded.append(bytes(d))
        else:
            raise TypeError("loads_batch() expects str or bytes entries")
    try:
        return _loads_batch(encoded)
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


__all__ = ["TOMLDecodeError", "dumps", "load", "loads", "loads_batch",
           "loads_lazy", "LazyNode"]
