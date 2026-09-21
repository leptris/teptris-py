#!/usr/bin/env python3
"""End-to-end Python tier (TODO.impl/09): teptris.loads vs the Python
TOML libraries users actually choose between: tomllib (stdlib 3.11+),
tomli (backport), tomlkit (pure-python), rtoml (rust-backed
incumbent). Competitors are OPTIONAL - a missing library drops its
column instead of blocking the lane.

Run:
  TEPTRIS_LIB_PATH=../teptris/build-shared/src/libteptris.dylib \
    python3 benchmark/lang_tier.py [corpus-dir] [reps]
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
import teptris  # noqa: E402

try:
    import tomllib  # stdlib, 3.11+
except ImportError:
    try:
        import tomli as tomllib  # backport
    except ImportError:
        tomllib = None

try:
    import tomlkit
except ImportError:
    tomlkit = None

try:
    import rtoml
except ImportError:
    rtoml = None

corpus = Path(sys.argv[1] if len(sys.argv) > 1
              else Path(__file__).resolve().parent.parent.parent / "teptris" / "bench-corpus")
reps = int(sys.argv[2]) if len(sys.argv) > 2 else 12


def bench(fn, src):
    for _ in range(2):
        fn(src)
    best = float("inf")
    for _ in range(reps):
        t0 = time.monotonic()
        fn(src)
        best = min(best, time.monotonic() - t0)
    return best * 1000.0


for path in sorted(corpus.glob("*.toml")):
    src = path.read_bytes()
    cells = []
    libs = [("teptris", teptris.loads)]
    if tomllib is not None:
        libs.append(("tomllib", lambda b: tomllib.loads(b.decode("utf-8"))))
    if tomlkit is not None:
        libs.append(("tomlkit", lambda b: tomlkit.parse(b.decode("utf-8"))))
    if rtoml is not None:
        libs.append(("rtoml", lambda b: rtoml.loads(b.decode("utf-8"))))
    for name, fn in libs:
        try:
            ms = bench(fn, src)
            cells.append(f"{name} {ms:7.2f} ms {len(src) / 1048576 / (ms / 1000):6.1f} MB/s")
        except Exception as e:  # noqa: BLE001
            cells.append(f"{name} ERROR: {str(e)[:50]}")
    print(f"{path.name:<20} {cells[0]} | {cells[1]}")
