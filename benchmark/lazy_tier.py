"""loads vs loads_lazy across three shapes.

Reports the wall-clock for the eager and lazy paths so the CI lane can
track them. The lazy variant only wins when materialization is
deferred (touching a small fraction of the tree) — the eager path is
the floor for "flatten everything" workloads.
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
    print(f"  {label:<32} best-of-{repeat}: {best*1000:7.2f} ms")
    return best


# Big table (2000 entries) — eager is the floor; lazy is competitive
BIG = ("[t]\n" + "\n".join(f"k{i} = {i}" for i in range(2000)) + "\n")


# Deep doc — touch only a small fraction of the tree (lazy-favorable)
DEEP = """
title = "teptris"
[meta]
version = 1
ok = true
[[items]]
sku = "a"
qty = 1
tags = [1, 2, 3]
note = "long string we never touch"
[[items]]
sku = "b"
qty = 2
tags = [4, 5, 6]
note = "another untouched string"
[[items]]
sku = "c"
qty = 3
tags = [7, 8, 9]
note = "yet another untouched string"
""".strip()


print("== BIG table (2000 entries) — touch everything ==")
b1 = bench("loads (eager flatten)", 50, lambda: teptris.loads(BIG))
b2 = bench("loads_lazy + flatten", 50, lambda: teptris.loads_lazy(BIG).to_dict())
print(f"  ratio (lazy/eager): {b2/b1:.2f}x   (<1 = lazy faster)")

print("\n== DEEP doc — touch only one path ==")
b3 = bench("loads (eager flatten)", 500, lambda: teptris.loads(DEEP))
b4 = bench("loads_lazy (1-path)", 500, lambda: teptris.loads_lazy(DEEP)["title"].value())
b5 = bench("loads_lazy + flatten", 500, lambda: teptris.loads_lazy(DEEP).to_dict())
print(f"  1-path vs eager flatten: {b3/b4:.2f}x   (>1 = lazy faster)")
print(f"  full-flatten vs eager:  {b3/b5:.2f}x   (lazy pays to flatten)")
