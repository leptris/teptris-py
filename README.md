# teptris-py — TOML for Python at libleptris speed

A ctypes binding (no C extension) over
[libteptris](https://github.com/leptris/teptris) — the TOML counterpart
of leptris-py (XML) and yeptris-py (YAML). API shape mirrors
`tomllib`/`tomli` (`loads`/`load` raising `TOMLDecodeError`) plus a
`dumps` writer in the `tomli_w` spirit. Datetime mapping mirrors
tomllib: aware/naive `datetime`, `date`, `time`.

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
