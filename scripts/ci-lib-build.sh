#!/usr/bin/env bash
# Builds the static libteptris archive for the cibuildwheel matrix.
# PGO two-stage on clang/gcc via the C core script; plain on MSVC
# (LTCG-bound; the setuptools link stays plain). Linux cells do NOT
# use this script: cibuildwheel builds inside each container so the
# archive links the container libc (glibc/musl) it ships with.
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
    # Ninja inside vcvars (cmake's VS generator finds no VS instance on
    # the runner images; vswhere -products * does)
    VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
    VSPATH=$("$VSWHERE" -latest -products '*' -property installationPath)
    [ -n "$VSPATH" ] || { echo "vswhere found no VS install" >&2; exit 1; }
    if [ "${RUNNER_ARCH:-}" = "ARM64" ]; then VSARCH=arm64; else VSARCH=amd64; fi
    # generate a .cmd and run it: inline cmd //c strings get mangled by
    # msys quote conversion; a file sidesteps quoting entirely
    VCVARS=$(cygpath -w "$VSPATH/VC/Auxiliary/Build/vcvarsall.bat")
    cat > _build_msvc.cmd <<CMDEOF
@echo on
call "$VCVARS" $VSARCH || exit /b 1
cmake -B $SRC\build -S $SRC -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DTEPTRIS_BUILD_CLI=OFF -DTEPTRIS_BUILD_SHARED=OFF -DTEPTRIS_BUILD_STATIC=ON -DTEPTRIS_ENABLE_LTO=OFF || exit /b 1
cmake --build $SRC\build || exit /b 1
CMDEOF
    cmd //c _build_msvc.cmd
    rc=$?
    rm -f _build_msvc.cmd
    exit $rc
    ;;
  *) echo "unsupported platform: $(uname -s)" >&2; exit 1 ;;
esac
ls -la "$SRC/build/src/"
