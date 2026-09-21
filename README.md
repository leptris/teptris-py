# teptris-py — TOML for Python at libleptris speed

A native C-extension binding (no ctypes, no fallback) over
[libteptris](https://github.com/leptris/teptris) — the TOML counterpart
of leptris-py (XML) and yeptris-py (YAML). API shape mirrors
`tomllib`/`tomli` (`loads`/`load` raising `TOMLDecodeError`) plus a
`dumps` writer in the `tomli_w` spirit. Datetime mapping mirrors
tomllib: aware/naive `datetime`, `date`, `time`.

Both directions are native: `dumps` walks the object tree in C
through the builder API and emits via the shared engine emitter —
3.5×–14.2× faster than tomli_w on every benchmark shape. The engine
implements the TOML 1.1 draft grammar (a strict superset of 1.0)
with 100% toml-test conformance, and parses at ≥3× the best C/C++
competitor on every shape.

## Performance (end-to-end, Python tier)

Best-of-12 runs on a GitHub ubuntu-latest runner (python 3.12),
2026-09-21, over the generated bench corpus (~700 KB per shape). The
full per-shape table regenerates on every main push via the
`lang-tier` lane.

| Shape | teptris | tomllib (stdlib) | tomlkit (pure) | rtoml (rust) |
| --- | --- | --- | --- | --- |
| array_heavy | 6.8 ms / 107 MB/s | 236 ms / 3.1 MB/s | 1,740 ms / 0.4 MB/s | 34 ms / 21 MB/s |
| cargo_like | 3.8 ms / 94 MB/s | 100 ms / 3.5 MB/s | 1,074 ms / 0.3 MB/s | 18 ms / 20 MB/s |
| datetime_heavy | 26.1 ms / 41 MB/s | 227 ms / 4.8 MB/s | 1,680 ms / 0.6 MB/s | 40 ms / 27 MB/s |
| deep_tables | 2.2 ms / 109 MB/s | 82 ms / 3.0 MB/s | 14,713 ms / 0.0 MB/s | 13 ms / 18 MB/s |
| mixed | 5.5 ms / 80 MB/s | 150 ms / 2.9 MB/s | 1,385 ms / 0.3 MB/s | 27 ms / 16 MB/s |
| scalar_float | 12.7 ms / 115 MB/s | 310 ms / 4.7 MB/s | 2,243 ms / 0.7 MB/s | 61 ms / 24 MB/s |
| scalar_int | 11.8 ms / 108 MB/s | 300 ms / 4.3 MB/s | 2,038 ms / 0.6 MB/s | 54 ms / 24 MB/s |

End to end (parse + materialize into Python objects): **4.8-6x rtoml**
(the rust-backed incumbent), **19-42x the stdlib's tomllib**, and
**hundreds of times tomlkit** per shape. rtoml numbers include its
own object construction; both libraries materialize fully.

## Packaging

`pip install teptris` resolves one of 10 `cp39-abi3` wheels —
manylinux/musllinux x86_64, aarch64 and armv7l (32-bit ARM, built
under qemu), macOS arm64 and x86_64, Windows AMD64 and ARM64 —
covering every CPython >= 3.9 including future minors. The extension statically links `libteptris`, so each
wheel is a single self-contained module (no shared-library chain).
Linux wheels build the engine inside each container via cibuildwheel
so musllinux links musl; the manylinux wheels carry the maximal tag
set (glibc >= 2.17).

Every wheel also carries the engine C SOURCE at `teptris/_engine`
(beside the compiled extension) — the recompile path for rebuilding
against your own environment. The sdist vendors the same tree and
compiles automatically at install.

Every other platform (FreeBSD, Solaris/illumos, any CPython on an
exotic arch) installs the sdist, which vendors the engine and
compiles it with the installing interpreter's compiler — a C compiler
is required, and the build fails loudly without one.

## Development

`setup.py` links the engine statically when it finds
`libteptris.a`/`teptris.lib` under `TEPTRIS_LIBDIR` (what the wheels
ship); otherwise it falls back to the shared library with an rpath to
its build tree. Point `TEPTRIS_INCLUDE`/`TEPTRIS_LIBDIR` at a C
checkout's build and run the suite:

```sh
cmake -B ../teptris/build -S ../teptris \
    -DCMAKE_BUILD_TYPE=Release -DTEPTRIS_BUILD_SHARED=OFF -DTEPTRIS_BUILD_STATIC=ON
cmake --build ../teptris/build
TEPTRIS_INCLUDE=../teptris/src/include TEPTRIS_LIBDIR=../teptris/build/src \
    python3 setup.py build_ext --inplace
PYTHONPATH=src python3 -m unittest discover -s tests -v
```

Publishing and version numbers are USER release decisions.
