"""teptris — TOML for Python at libleptris speed.

Native extension over libteptris (C11 TOML 1.1 grammar), no fallback. API shape
mirrors tomllib/tomli: loads(str|bytes) -> dict raising TOMLDecodeError;
dumps(obj) -> str in the tomli_w spirit. Datetime mapping mirrors
tomllib: aware/naive datetime, date, time.

Lazy twin: loads_lazy(str|bytes) -> LazyNode (teptris#79) — one parse,
host objects materialize only along the paths actually accessed.
"""
from __future__ import annotations

from collections.abc import Sequence
from importlib.metadata import PackageNotFoundError
from importlib.metadata import version as _pkg_version
from typing import Any, BinaryIO, Union

try:
    __version__ = _pkg_version("teptris")
except PackageNotFoundError:  # running from a source tree
    __version__ = "0.0.0.dev0"

from ._native import DecodeError as _DecodeError
from ._native import LazyNode, dumps
from ._native import engine_version as _engine_version
from ._native import loads as _loads
from ._native import loads_batch as _loads_batch
from ._native import loads_lazy as _loads_lazy

TomlInput = Union[str, bytes, bytearray]


class TOMLDecodeError(_DecodeError, ValueError):
    """Raised on malformed input. Carries ``line`` / ``column`` (1-based)
    for the failure position; 0 when the engine could not attribute one."""

    line: int
    column: int


def _decode_error(e: _DecodeError) -> TOMLDecodeError:
    err = TOMLDecodeError(str(e))
    err.line = getattr(e, "line", 0)
    err.column = getattr(e, "column", 0)
    return err


def loads(data: TomlInput) -> dict[str, Any]:
    if isinstance(data, str):
        data = data.encode("utf-8")
    elif not isinstance(data, (bytes, bytearray)):
        raise TypeError("loads() expects str or bytes")
    try:
        return _loads(bytes(data))
    except _DecodeError as e:
        raise _decode_error(e) from None


def loads_batch(docs: Sequence[TomlInput]) -> list[dict[str, Any]]:
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
        raise _decode_error(e) from None


def loads_lazy(data: TomlInput) -> LazyNode:
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
        raise _decode_error(e) from None


def loads_lazy_batch(docs: Sequence[TomlInput]) -> list[LazyNode]:
    """Lazy twin of loads_batch (the py twin of teptris-ruby's
    load_lazy_batch): parse every document, hand back LazyNode wrappers —
    materialization happens only along accessed paths. Same per-doc
    error semantics as loads_lazy: the first failing document raises
    TOMLDecodeError carrying its line/column.

    Unlike loads_batch this is a python-level loop (one C crossing per
    document, no C batch) — the C batch exists in teptris-ruby for
    GVL-slice reasons py does not have. Use it for the surface, not
    for speed.
    """
    if isinstance(docs, (str, bytes, bytearray)) or not hasattr(docs, "__len__"):
        raise TypeError("loads_lazy_batch() expects a list of str or bytes")
    return [loads_lazy(d) for d in docs]


def load(fp: str | bytes | BinaryIO) -> dict[str, Any]:
    read = getattr(fp, "read", None)
    if read is not None:
        # text streams return str; encode like the other entry points
        data = read()
        if isinstance(data, str):
            data = data.encode("utf-8")
        return loads(data)
    # str | bytes here at runtime; BinaryIO never reaches open()
    with open(fp, "rb") as f:  # type: ignore[arg-type]
        return loads(f.read())


from .descriptor import Descriptor


def engine_version() -> str:
    """The libteptris engine version string (the C core this wheel
    links, not the teptris package version — see importlib.metadata
    for that). Mirrors teptris-ruby's TOML.engine_version."""
    return _engine_version()


__all__ = ["Descriptor", "LazyNode", "TOMLDecodeError", "dumps",
           "engine_version", "load", "loads", "loads_batch",
           "loads_lazy", "loads_lazy_batch"]
