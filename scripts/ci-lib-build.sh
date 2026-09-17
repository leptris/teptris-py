#!/usr/bin/env bash
# Builds the static libteptris archive for the cibuildwheel matrix.
# PGO two-stage on clang/gcc via the C core script; plain on MSVC
# (LTCG-bound; the setuptools link stays plain). Runs ON the runner
# (cibuildwheel containers see the tree via /project).
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=libteptris-src
case "$(uname -s)" in
  Darwin|Linux)
    # PGO two-stage via the C core script (corpus only feeds training)
    python3 "$SRC/scripts/gen_bench_corpus.py" "$SRC/bench-corpus"
    TEPTRIS_PGO_LTO=1 bash "$SRC/scripts/build-pgo.sh" "$SRC" "$SRC/build" "$SRC/bench-corpus"
    ;;
  MINGW*|MSYS*)
    # plain static: MSVC PGO is LTCG-bound and setuptools adds no /LTCG.
    # VS generator (not Ninja+vcvars): finds MSVC itself, no cmd quoting.
    if [ "${RUNNER_ARCH:-}" = "ARM64" ]; then VSARCH=ARM64; else VSARCH=x64; fi
    OUT="$(pwd -W)/$SRC/build/src"
    cmake -B "$SRC/build" -S "$SRC" -G "Visual Studio 17 2022" -A "$VSARCH" \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DTEPTRIS_BUILD_CLI=OFF \
      -DTEPTRIS_BUILD_SHARED=OFF -DTEPTRIS_BUILD_STATIC=ON -DTEPTRIS_ENABLE_LTO=OFF \
      -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="$OUT"
    cmake --build "$SRC/build" --config Release
    ;;
  *) echo "unsupported platform: $(uname -s)" >&2; exit 1 ;;
esac
ls -la "$SRC/build/src/"
