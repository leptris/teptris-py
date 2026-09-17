#!/usr/bin/env bash
# Builds the static libteptris archive for the cibuildwheel matrix.
# PGO two-stage on clang/gcc via the C core script; plain on MSVC
# (LTCG-bound; the setuptools link stays plain). Runs ON the runner
# (cibuildwheel containers see the tree via /project).
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=libteptris-src
python3 "$SRC/scripts/gen_bench_corpus.py" "$SRC/bench-corpus"
case "$(uname -s)" in
  Darwin|Linux)
    TEPTRIS_PGO_LTO=1 bash "$SRC/scripts/build-pgo.sh" "$SRC" "$SRC/build" "$SRC/bench-corpus"
    ;;
  MINGW*|MSYS*)
    # plain static: MSVC PGO is LTCG-bound and setuptools adds no /LTCG
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSPATH=%%i"
    if [ "${RUNNER_ARCH:-}" = "ARM64" ]; then VSARCH=arm64; else VSARCH=amd64; fi
    cmd //c "call \"%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat\" $VSARCH && cmake -B $SRC\\build -S $SRC -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DTEPTRIS_BUILD_CLI=OFF -DTEPTRIS_BUILD_SHARED=OFF -DTEPTRIS_BUILD_STATIC=ON -DTEPTRIS_ENABLE_LTO=OFF -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=%CD%\\$SRC\\build\\src && cmake --build $SRC\\build"
    ;;
esac
ls -la "$SRC/build/src/"
