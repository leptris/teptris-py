#!/usr/bin/env python3
"""Dumps tier: teptris.dumps vs the python competitors with a
serializer. tomllib is loads-only (stdlib scope) — N/A by design;
rtoml.dumps (rust-backed) is the perf-relevant comparator; tomlkit
(pure-python) rides along for completeness. Materialization is
untimed; the timed region is the serialization of the SAME object.

Run:
  python3 benchmark/dump_tier.py [corpus-dir] [reps]
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
import rtoml
import tomlkit

import teptris

corpus = Path(sys.argv[1] if len(sys.argv) > 1
              else "../teptris/bench-corpus")
reps = int(sys.argv[2]) if len(sys.argv) > 2 else 40


def bench(fn, obj):
    fn(obj)
    best = float("inf")
    for _ in range(reps):
        t0 = time.monotonic()
        fn(obj)
        best = min(best, time.monotonic() - t0)
    return best * 1000.0


for path in sorted(corpus.glob("*.toml")):
    src = path.read_text()
    doc = teptris.loads(src)
    rdoc = rtoml.loads(src)
    tdoc = tomlkit.parse(src)
    cells = []
    import tomli_w
    for name, fn, obj in [
        ("teptris", teptris.dumps, doc),
        ("rtoml", rtoml.dumps, rdoc),
        ("tomli-w", tomli_w.dumps, doc),
        ("tomlkit", tomlkit.dumps, tdoc),
    ]:
        ms = bench(fn, obj)
        cells.append(f"{name} {ms:8.2f} ms {len(src) / 1048576 / (ms / 1000):6.1f} MB/s")
    print(f"{path.name:<20} " + " | ".join(cells))
