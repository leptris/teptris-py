"""loads_batch vs per-doc loads across many-small shapes.

Honest shape (mirrors teptris-ruby's benchmark/batch_tier.rb): eager
batch is NOT faster than per-doc for tiny/medium corpora — Python's
per-call overhead amortizes through the loop, and the batch adds
scratch allocation + tight object-construction pressure. The value is
the single API surface, per-doc error report, and one C crossing; for
large docs the batch is competitive-to-better. Measure before adopting.
"""
import gc
import time

import teptris


def bench(label, repeat, fn):
    best = float("inf")
    for _ in range(repeat):
        gc.collect()
        t0 = time.monotonic()
        fn()
        best = min(best, time.monotonic() - t0)
    print(f"  {label:<28} best-of-{repeat}: {best*1000:7.2f} ms")
    return best


TINY = [f"id = {i}\nname = \"row-{i}\"\ntags = [1, 2, 3]\n"
        for i in range(2000)]
MEDIUM = [(f"id = {i}\ntitle = \"row {i}\"\n[meta]\nscore = 1.5\n"
           f"tags = [\"a\", \"b\", \"c\"]\n[[meta.children]]\nname = \"x\"\n"
           f"[[meta.children]]\nname = \"y\"\n") for i in range(500)]
LARGE = [("[block]\n" + "\n".join(f"v{j} = {j}.{i}" for j in range(200)) + "\n")
         for i in range(100)]

for name, corpus in [("tiny (2000 x 3-line)", TINY),
                     ("medium (500 x 8-line aot)", MEDIUM),
                     ("large (100 x 200-line)", LARGE)]:
    print(name)
    per = bench("loads (per-doc)", 6,
                lambda: [teptris.loads(s) for s in corpus])
    bat = bench("loads_batch (one call)", 6,
                lambda: teptris.loads_batch(corpus))
    print(f"  batch/per-doc ratio: {bat/per:.2f}x  (<1 = batch faster)\n")
