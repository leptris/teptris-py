# Changelog

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
