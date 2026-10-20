#!/usr/bin/env python3
"""Native tier: the retained-document roundtrip (loads_doc +
dumps_doc - zero Python objects materialized) vs the competitors'
dict roundtrip (their loads + their dumps). This is the native-doc
API's home cell: reserialization of an unchanged document, the config
pipeline shape.

Run: python3 benchmark/native_tier.py [corpus-dir] [reps]
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
import rtoml

import teptris

corpus = Path(sys.argv[1] if len(sys.argv) > 1
              else "../teptris/bench-corpus")
reps = int(sys.argv[2]) if len(sys.argv) > 2 else 40


def bench(fn, src):
    fn(src)
    best = float("inf")
    for _ in range(reps):
        t0 = time.monotonic()
        fn(src)
        best = min(best, time.monotonic() - t0)
    return best * 1000.0


for path in sorted(corpus.glob("*.toml")):
    src = path.read_text()
    cells = []
    for name, fn in [
        ("teptris native", lambda s: teptris.dumps_doc(teptris.loads_doc(s))),
        ("rtoml dict", lambda s: rtoml.dumps(rtoml.loads(s))),
    ]:
        ms = bench(fn, src)
        cells.append(f"{name} {ms:7.2f} ms {len(src) / 1048576 / (ms / 1000):6.1f} MB/s")
    print(f"{path.name:<20} " + " | ".join(cells))
