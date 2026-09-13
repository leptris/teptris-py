"""teptris — TOML for Python at libleptris speed.

ctypes binding over libteptris (C11 TOML 1.0). API shape mirrors
tomllib / tomli: ``loads(str) -> dict`` raising ``TOMLDecodeError``,
plus a ``dumps(obj) -> str`` writer in the tomli_w spirit. Datetime
mapping mirrors tomllib: aware/naive ``datetime``, ``date``, ``time``.
"""
from .__core import (
    TOMLDecodeError,
    dumps,
    load,
    loads,
    library_path,
)

__all__ = ["TOMLDecodeError", "dumps", "load", "loads", "library_path"]
