# CLAUDE.md — teptris-py

## Mission

Be the best TOML parser and builder ever made — in C, Ruby, Rust, and
JS — measured, not claimed. This binding's bar, and the family
ledger/plan, live in the engine's
[BEST-EVER.md](https://github.com/leptris/teptris/blob/main/BEST-EVER.md).
"At parity" or "unmeasured" is an open gap.

## Operating laws (this binding)

- Version = `{engine version}.{binding iteration}` (current: 0.3.4.1 =
  engine 0.3.4, iteration 1). The engine pin is `ref:` in **all five**
  workflows (ci, cibuildwheel, lang-tier, binding-bench, +1) — **a
  version bump moves every pin in the same PR**.
- Releases: tag `v{version}` → the cibuildwheel train (60 wheels +
  sdist) → manual twine upload (the delivery doctrine) → published
  PyPI smoke.
- Every release is gated by a published-wheel smoke: install from
  PyPI, loads/dumps roundtrip, `teptris.engine_version()` == the pin,
  datetime materialization is real (`datetime.time` for local times),
  and zero bytes on stdout/stderr.
- API shape mirrors `tomllib`/`tomli` (`loads` raising
  `TOMLDecodeError`) + Descriptor; the native extension IS the
  binding — no fallback, ever.
- Bench doctrine: vs tomllib/rtoml, best-of-≥40, numbers carry
  date+host.
- All changes through PRs (rebase-merge); the full wheel train
  rehearses in the PR when release machinery changes.
