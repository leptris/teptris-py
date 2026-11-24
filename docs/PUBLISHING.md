# Publishing checklist (the wave release runbook)

The delivery channel is **manual twine from the cibuildwheel run's
artifacts** — GitHub never touches PyPI. One release = ONE delivery
unit: **wheels + sdist together** (teptris-py#172: 0.3.30/31/33 shipped
wheels-only because the upload grabbed the `wheels-*` artifacts and
skipped the `sdist` one; the wheel smokes stayed green because they
install wheels).

The pipeline now enforces the unit: every tag run assembles a single
**`publish-set` artifact** (all wheels + the sdist, asserted both
halves present). Download one artifact, upload every file in it.

## Pre-publish (per release wave)

1. **Assemble from `publish-set`, never from `wheels-*`:**

   ```
   gh run download <cibuildwheel-run-id> -R leptris/teptris-py \
       -n publish-set -D dist
   ls dist/   # N wheels + exactly 1 .tar.gz — BOTH halves, or STOP
   ```

2. **The set matches the wave**: every filename carries the released
   version (`teptris-{version}-...`), and the version equals the tag
   (`v{version}`) — wheel pins and version move in the same PR
   (family law), so a mismatch means the wrong run's artifacts.

3. **Metadata sanity before upload** (`twine check` rejects the
   broken-metadata class):

   ```
   twine check dist/*
   ```

4. **Upload the whole set, one command** (PEP 740 attestations per
   the delivery doctrine, twine >= 6.1):

   ```
   twine upload --attestations dist/*
   ```

   Never `twine upload dist/*.whl` — a glob that names only one half
   is exactly the #172 failure mode.

## Post-publish (same session, before calling the wave done)

5. **Registry smoke — both halves on PyPI** (the #172 gate the wheel
   smokes could not catch):

   ```
   python3 scripts/publish_smoke.py {version}
   ```

   `OK` requires live wheels AND a live sdist on the registry. Exit
   nonzero on any wheels-only release; `--all` sweeps every shipped
   version.

6. **Published-wheel smoke** (the standing doctrine gate): install
   from PyPI, `loads`/`dumps` roundtrip, `teptris.engine_version()`
   == the engine pin, datetime materialization is real
   (`datetime.time` for local times), zero bytes on stdout/stderr.

## Repairing a wheels-only release that already shipped

PyPI accepts a NEW filename on an EXISTING version — no re-release
needed:

1. Find the wave-day cibuildwheel run:
   `gh run list -R leptris/teptris-py --workflow cibuildwheel`.
2. Download the ORIGINAL `sdist` artifact — never rebuild at HEAD and
   hand-rename (the sdist must carry the released binding version;
   the original artifact is the released source):

   ```
   gh run download <run-id> -R leptris/teptris-py -n sdist -D /tmp/sdist
   twine upload /tmp/sdist/teptris-<ver>.tar.gz
   ```

3. Re-run `python3 scripts/publish_smoke.py {version}` — it must exit 0.

Prefer the original artifacts for every backfill (done for
0.3.30.0/0.3.31.0/0.3.33.0 per issue #172).
