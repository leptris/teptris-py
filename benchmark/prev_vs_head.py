import sys
import time
import tracemalloc

# HEAD-vs-previous binding regression check (mirrors teptris-ruby's
# benchmark/prev_vs_head.rb): run inside the interpreter that has the
# wheel under test installed; prints "label metric value" lines for
# the workflow gate. The C engine has its own A/B lane; this guards
# _native.c (materialization, error paths) which it cannot see.
label = sys.argv[1] if len(sys.argv) > 1 else "run"

import teptris  # noqa: E402

SMALL = 'title = "bench"\n[owner]\nname = "a"\nactive = true\nratio = 1.5\n'


def medium_doc():
    out = []
    for t in range(120):
        out.append(f"[table.{t}]\n")
        for i in range(8):
            out.append(f"k{i} = {i * 7}\ns{i} = \"v{i}\"\nf{i} = {i}.25\n")
    return "".join(out)


MEDIUM = medium_doc()
MEDIUM_OBJ = teptris.loads(MEDIUM)


def median3(fn):
    return sorted(fn() for _ in range(3))[1]


def ops_per_second(op, seconds=0.4):
    def measure():
        t0 = time.perf_counter()
        n = 0
        while time.perf_counter() - t0 < seconds:
            op()
            n += 1
        return n / (time.perf_counter() - t0)
    return median3(measure)


def alloc_peak(op):
    tracemalloc.start()
    op()
    _, peak = tracemalloc.get_traced_memory()
    tracemalloc.stop()
    return peak


print(f"{label} load_small {ops_per_second(lambda: teptris.loads(SMALL)):.1f}")
print(f"{label} load_medium {ops_per_second(lambda: teptris.loads(MEDIUM)):.1f}")
print(f"{label} dump_medium {ops_per_second(lambda: teptris.dumps(MEDIUM_OBJ)):.1f}")
print(f"{label} alloc_peak_load_medium {alloc_peak(lambda: teptris.loads(MEDIUM))}")
