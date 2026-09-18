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

## Packaging

`pip install teptris` resolves one of 10 `cp39-abi3` wheels —
manylinux/musllinux x86_64, aarch64 and armv7l (32-bit ARM, built
under qemu), macOS arm64 and x86_64, Windows AMD64 and ARM64 —
covering every CPython >= 3.9 including future minors. The extension statically links `libteptris`, so each
wheel is a single self-contained module (no shared-library chain).
Linux wheels build the engine inside each container via cibuildwheel
so musllinux links musl; the manylinux wheels carry the maximal tag
set (glibc >= 2.17).

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
