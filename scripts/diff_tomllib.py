#!/usr/bin/env python3
"""Binding-level differential: teptris.loads vs stdlib tomllib over the
checked-in corpus. The engine's semantic differential (in the teptris
repo) proves the parse; this proves the BINDING's materialization -
datetime classes, type mapping, nesting. nan normalizes to None on
both sides (nan != nan by IEEE). Zero divergences = pass.

Run: python3 scripts/diff_tomllib.py [corpus-dir]
"""
import math
import sys
from pathlib import Path

try:
    import tomllib  # py311+ (CI)
except ModuleNotFoundError:  # py<3.11 dev hosts
    import tomli as tomllib

try:
    import teptris  # the installed binding (CI)
except ModuleNotFoundError:  # dev host: the in-tree build
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
    import teptris


def norm(v):
    if isinstance(v, bool):
        return v
    if isinstance(v, dict):
        return {k: norm(x) for k, x in v.items()}
    if isinstance(v, list):
        return [norm(x) for x in v]
    if isinstance(v, float) and math.isnan(v):
        return None
    return v


def diff(a, b, path, out):
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a:
                out.append(f"{path}.{k}: missing on teptris side")
            elif k not in b:
                out.append(f"{path}.{k}: missing on tomllib side")
            else:
                diff(a[k], b[k], f"{path}.{k}", out)
            if len(out) > 4:
                return
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append(f"{path}: len {len(a)} vs {len(b)}")
            return
        for i, (x, y) in enumerate(zip(a, b)):
            diff(x, y, f"{path}[{i}]", out)
            if len(out) > 4:
                return
    elif a != b and repr(a) != repr(b):
        out.append(f"{path}: teptris={a!r} tomllib={b!r}")


corpus = Path(sys.argv[1] if len(sys.argv) > 1 else "test/corpus")
fails = 0
checked = 0
for f in sorted(corpus.glob("*.toml")):
    src = f.read_bytes()
    got = norm(teptris.loads(src))
    exp = norm(tomllib.loads(src.decode("utf-8")))
    out = []
    diff(got, exp, "$", out)
    checked += 1
    if out:
        print(f"DIVERGENCE {f.name}:")
        for d in out[:5]:
            print(f"  {d}")
        fails += 1
print(f"differential: {checked} files, {fails} divergences")
sys.exit(1 if fails else 0)
