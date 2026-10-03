"""Tree-shaped schema-descriptor materialization (teptris#46) — the
py twin of teptris-ruby's Teptris::Descriptor: compile a plan tree
once, then materialize a TOML document against it in ONE native pass —
unplanned keys are never materialized.

    descriptor = Descriptor.build({"children": [
        {"name": "name", "kind": "scalar"},
        {"name": "port", "kind": "scalar"},
        {"name": "hosts", "kind": "collection"},
        {"name": "items", "kind": "nested", "plan": {
            "children": [{"name": "id", "kind": "scalar"}]}},
        {"name": "everything_else", "kind": "raw"},
    ]})
    descriptor.walk(toml_string)  # {"name": "svc", ...}

Row kinds: "scalar" (any TOML value), "collection" (array of scalars),
"nested" (recurse via plan; also spans arrays of tables), "raw" (the
untouched native subtree). Rows absent from the document read as None.
"""
from __future__ import annotations

from typing import Any

from ._native import plan_build, plan_emit

_KINDS = {"scalar": 1, "collection": 2, "nested": 3, "raw": 4}


class Descriptor:
    def __init__(self, rows: list[list[Any]], first_row: list[int],
                 handle: Any) -> None:
        self.rows = rows
        self.first_row = first_row
        self._handle = handle

    @classmethod
    def build(cls, tree: dict[str, Any]) -> Descriptor:
        # Pre-order plan numbering (root = plan 0) with each plan's rows
        # in a DISJOINT reserved range — interleaved appending leaks
        # siblings that follow a nested child into the sub-plan's range
        # (the ruby 0.2.29 bug, mirrored here by construction).
        plans: list[dict[str, Any]] = []
        index_of: dict[int, int] = {}

        def collect(t: dict[str, Any]) -> None:
            if id(t) not in index_of:
                index_of[id(t)] = len(plans)
                plans.append(t)
            for ch in t.get("children", []):
                if _KINDS[ch["kind"]] == 3:
                    collect(ch["plan"])

        collect(tree)

        first_row = [0]
        for t in plans:
            first_row.append(first_row[-1] + len(t.get("children", [])))
        rows: list[list[Any]] = [[] for _ in range(first_row[-1])]
        for i, t in enumerate(plans):
            for j, ch in enumerate(t.get("children", [])):
                kind = _KINDS[ch["kind"]]
                sub = index_of[id(ch["plan"])] if kind == 3 else 0
                rows[first_row[i] + j] = [str(ch["name"]), kind, sub]

        handle = plan_build(rows, first_row)
        return cls(rows, first_row, handle)

    def walk(self, toml: str | bytes) -> dict[str, Any]:
        return plan_emit(self._handle, toml)
