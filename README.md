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

## Development

The package loads a shared `libteptris` — from `TEPTRIS_LIB_PATH`, a
vendored `platform/` directory, the sibling C checkout's build tree, or
the system loader.

```sh
cmake -B ../teptris/build-shared -S ../teptris \
    -DCMAKE_BUILD_TYPE=Release -DTEPTRIS_BUILD_SHARED=ON -DTEPTRIS_BUILD_STATIC=OFF
cmake --build ../teptris/build-shared
TEPTRIS_LIB_PATH=../teptris/build-shared/src/libteptris.dylib \
    python3 -m unittest discover -s tests -v
```

Publishing and version numbers are USER release decisions.
