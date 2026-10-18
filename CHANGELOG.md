# Changelog

## 0.3.4.1 (2026-10-18)

- **Engine v0.3.4** — all five workflow pins move from v0.3.3 to
  v0.3.4: `teptris_plan_build` no longer writes debug output to stderr
  (the v0.3.2 depth gate shipped an unguarded DBG fprintf — one line
  per plan row in every host). Family law: pin moves in the release
  PR — version = {engine 0.3.4}.{iteration 1}.

## 0.3.3.1 (2026-10-18)

- **Engine v0.3.3** — all five workflow pins move from v0.3.1 to
  v0.3.3, carrying the plan depth cap (engine v0.3.2:
  `teptris_plan_build` rejects plans deeper than 512 instead of
  overflowing the stack) and the dom clone NULL-memcpy UB guards
  (engine v0.3.3, found by the new fuzz_dump harness). Family law:
  the pin moves in the release PR — version = {engine 0.3.3}.{iteration 1}.

## 0.3.1.1 (2026-10-16)

- **Corrected engine build**: carries libteptris **0.3.1** for real —
  the 0.3.1.0 train built from the v0.3.0 engine pin while the
  version declared 0.3.1 (the family law: the version declares the
  engine inside). All workflow engine pins move to v0.3.1.
- 0.3.1.0 is a wrong-engine twin and a yank candidate (owner call).
- CI: **pypa/cibuildwheel v3.0.1 → v4.3.1** — the last stale major
  in the family's lanes, verified by the 0.3.1.0 train (26/26 green;
  v4 removed the cpython-freethreading enable group).

## 0.3.1.0 (2026-10-16, WRONG-ENGINE TWIN — yank candidate)

- Intended as the first libteptris **0.3.1** build but the wheel lane's v0.3.0 pin made it carry engine 0.3.0 (the 0.3.1
  bump is CLI tooling — the library surface is unchanged from
  0.3.0, so bindings are behavior-identical).
- CI: **pypa/cibuildwheel v3.0.1 → v4.3.1** — the last stale major
  in the family's lanes, verified by this full train.

## 0.3.0.1 (2026-10-09)

- `dumps_json_natural(obj)` — natural-JSON emit for hosts (engine
  0.3.0's fastest emit mode): real numbers, booleans, RFC 3339
  datetime strings, non-finite floats as `None`. Shared builder
  phase with `dumps`; typed in `_native.pyi`; the ruby twin shipped
  as teptris-ruby 0.3.0.2.

## 0.3.0.0 (2026-10-07)

- **Versioning realignment**: this and future versions follow the
  family law `{engine version}.{binding iteration}` — the version
  declares the engine shipped inside (0.3.0.0 = libteptris 0.3.0,
  first binding build). The engine pin across all workflows moves
  to v0.3.0.
- Carries libteptris 0.3.0 (natural-JSON emit mode in the engine
  API) plus this binding's 0.2.27/0.2.28 content (typing, Descriptor,
  engine_version, loads_lazy_batch, lint cleanup).

## 0.2.28 (2026-10-07)

- Style/lint cleanup of the python layer (ruff, #110): import
  ordering, `collections.abc` imports, PEP 604 unions in annotations
  and the `_native.pyi` stub — no behavior changes.
- CI: ruff gate beside mypy; every wheel's smoke includes the
  200-thread parse hammer (tz-cache under threads; GIL-disabled on
  the free-threaded wheels).

## 0.2.27 (2026-10-05)

- `teptris.__version__` — the installed package version (from
  importlib.metadata; `"0.0.0.dev0"` when running from a source tree).
- `[project.urls]` — Repository / Changelog / Issues links on PyPI.
- `CHANGELOG.md` shipped in the repo (this file).
- CI: every wheel's smoke now runs a 200-thread parse hammer (tz-cache
  under threads on every platform; GIL-disabled on the cp314t wheels).

## 0.2.26 (2026-10-04)

- `teptris.Descriptor` — planned-key materialization (the twin of
  teptris-ruby's `Teptris::Descriptor`): compile a plan tree once,
  materialize documents against it in one native pass; unplanned keys
  are never materialized. Row kinds: `scalar`, `collection`, `nested`
  (spans arrays of tables), `raw`. Absent rows read `None`.
- PEP 561 typing: `py.typed` + `_native.pyi` shipped; `mypy` gate in
  CI. `LazyNode` fully typed (subscript overloads, iteration, len).
- `engine_version()` — the linked libteptris version string.
- `loads_lazy_batch([...])` — the lazy twin of `loads_batch`.
- Windows ARM64 wheels link plain (VS2026 makes the impossible PGO
  request fatal there; VS2022 merely warned).
- README documents all of the above.

## 0.2.25 (2026-09-30)

- 60 wheels: cp314 and free-threaded cp314t lines added on all
  platforms (tz cache lock-guarded for `Py_GIL_DISABLED`).

## 0.2.24 (2026-09-30)

- Datetime leak fix on the generic path (0 blocks; ships everywhere).
- Datetime C-API fast paths on version-specific cp310-cp313 wheels:
  −56% `datetime_heavy` loads, −72% dumps (measured darwin/arm64).
- MSVC PGO (x64) on version-specific Windows wheels; ARM64 plain.
- Two-invocation cibuildwheel architecture (abi3 + version-specific).
