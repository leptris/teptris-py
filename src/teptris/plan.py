"""The plan face (teptris-py#173) — build from a spec, walk a
document, typed row results. The py twin of teptris-rs `src/plan.rs`
(`Descriptor::build` over `PlanRow` trees) and teptris-ts
(`planBuild`/`planWalk` over `PlanRowSpec`); with it, Python prices
TOML query walks alongside C/Rust/TS in the serialbench matrix.

    plan = teptris.plan_build([
        "name",                                        # str = scalar
        {"name": "port", "kind": "scalar"},
        {"name": "hosts", "kind": "collection"},       # array of scalars
        {"name": "items", "rows": ["id", "label"]},    # nested: also spans
                                                       # arrays of tables
        {"name": "everything", "kind": "raw"},
    ])
    teptris.plan_walk(plan, toml_string)
    # {"name": "svc", "port": 8080, "hosts": [...],
    #  "items": [{"id": 1}, ...], "everything": {...}}

Row dialect (the ts PlanRowSpec shape): a bare string is a scalar;
`kind` is `"scalar"` (default), `"collection"` or `"raw"`; a row with
`rows` walks a nested table (and arrays of tables) against that sub
spec. Results come back TYPED — real ints/floats/bools/datetimes per
the `loads()` contract — and rows absent from the document (or
mismatched against their planned kind) are ABSENT from the output, the
rs/ts "no nulls" semantics (the ruby-parity `Descriptor` face keeps
them as None instead).

The engine handle is an opaque capsule freed with the Plan object
(same ownership as `compile_schema`); one plan walks any number of
documents. This face is the py twin surface — the compiled plan and
the one-pass walk both run in C (`teptris_plan_build` /
`teptris_plan_walk` via `_native`).
"""
from __future__ import annotations

from collections.abc import Sequence
from typing import Any

from ._native import DecodeError as _DecodeError
from ._native import plan_build as _plan_build_flat
from ._native import plan_emit as _plan_emit

_SCALAR, _COLLECTION, _NESTED, _RAW = 1, 2, 3, 4
_KINDS = {"scalar": _SCALAR, "collection": _COLLECTION, "raw": _RAW}


class Plan:
    """A compiled plan (opaque engine handle). Build once with
    `plan_build`, walk any number of documents with `plan_walk`;
    freed with the object."""

    __slots__ = ("_handle", "_nrows")

    def __init__(self, handle: Any, nrows: int) -> None:
        self._handle = handle
        self._nrows = nrows

    def __repr__(self) -> str:
        return f"<teptris.Plan rows={self._nrows}>"


def _row(row: Any) -> tuple[str, int, list[Any] | None]:
    """One spec entry -> (name, kind, children-or-None), checked."""
    if isinstance(row, str):
        return row, _SCALAR, None
    if not isinstance(row, dict):
        raise TypeError(
            "each plan row is a string name or a "
            '{name, kind?, rows?} dict, got '
            f"{type(row).__name__}")
    name = row.get("name")
    if not isinstance(name, str):
        raise TypeError('a plan row dict carries a string "name"')
    kind_name = row.get("kind", "scalar")
    if kind_name not in _KINDS:
        raise ValueError(
            f'unknown plan row kind {kind_name!r} for "{name}" '
            '(scalar, collection, raw)')
    children = row.get("rows")
    if children is not None:
        if not isinstance(children, (list, tuple)):
            raise TypeError(
                f'plan row "{name}": "rows" is a list of sub rows')
        return name, _NESTED, list(children)
    if "rows" in row:
        # explicit null rows: a nested row needs children
        raise TypeError(f'plan row "{name}": "rows" is a list of sub rows')
    return name, _KINDS[kind_name], None


def _layout(rows: Sequence[Any], flat: list[list[Any]],
            first: list[int]) -> int:
    """Depth-first plan layout — the C contract: plan p owns the
    contiguous row range [first[p], first[p+1]), so a plan's OWN rows
    go first and each nested row's sub-plan is laid out after them.
    The block's index is captured at ENTRY (first.len() - 1): sibling
    nested rows must not capture the same index, and a nested row's
    sub points at its OWN child block even when descendants lay out
    further plans (the rust layout's entry-time index; a
    first.len()-1 read after the recursion would capture the deepest
    descendant instead)."""
    idx = len(first) - 1
    pending: list[tuple[int, list[Any]]] = []
    for row in rows:
        name, kind, children = _row(row)
        if children is None:
            flat.append([name, kind, 0])
        else:
            slot = len(flat)
            flat.append([name, _NESTED, 0])
            pending.append((slot, children))
    for slot, children in pending:
        first.append(len(flat))
        flat[slot][2] = _layout(children, flat, first)
    return idx


def _drop_missing(value: Any) -> Any:
    """The C materializer hands MISSING rows back as None; the plan
    face's contract is ABSENT (rs/ts parity). A present row never
    materializes as None — TOML has no null — so None-valued keys
    (recursively, so nested tables and array-of-tables entries follow
    the same rule) are exactly the missing/mismatched set."""
    if isinstance(value, dict):
        return {k: _drop_missing(v) for k, v in value.items()
                if v is not None}
    if isinstance(value, list):
        return [_drop_missing(v) for v in value]
    return value


def plan_build(rows: Sequence[Any]) -> Plan:
    """Compile a plan from a row spec (see the module docstring for
    the dialect). Raises TypeError on a malformed row and ValueError
    on an empty spec or an engine-rejected one (e.g. nesting past the
    engine's 512 ceiling)."""
    if isinstance(rows, (str, bytes, bytearray)) or not hasattr(rows,
                                                                "__iter__"):
        raise TypeError("plan_build() expects a list of rows")
    flat: list[list[Any]] = []
    first: list[int] = [0]
    _layout(list(rows), flat, first)
    first.append(len(flat))
    if not flat:
        raise ValueError("a plan needs at least one row")
    try:
        handle = _plan_build_flat(flat, first)
    except _DecodeError as e:
        raise ValueError(f"invalid plan spec: {e}") from None
    return Plan(handle, len(flat))


def plan_walk(plan: Plan, toml: str | bytes | bytearray) -> dict[str, Any]:
    """Walk one TOML document against a compiled plan; returns only
    the planned keys as typed results. Raises TOMLDecodeError with
    line/column on malformed input (every public path's contract)."""
    if not isinstance(plan, Plan):
        raise TypeError("plan_walk() expects the plan from plan_build()")
    if isinstance(toml, bytearray):
        toml = bytes(toml)
    try:
        out = _plan_emit(plan._handle, toml)
    except _DecodeError as e:
        from . import TOMLDecodeError  # call-time: no import cycle

        err = TOMLDecodeError(str(e))
        err.line = getattr(e, "line", 0)
        err.column = getattr(e, "column", 0)
        raise err from None
    return _drop_missing(out)
