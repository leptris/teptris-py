# Changelog

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
