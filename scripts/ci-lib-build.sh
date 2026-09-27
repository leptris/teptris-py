#!/usr/bin/env bash
# Builds the static libteptris archive for the cibuildwheel matrix.
# PGO two-stage everywhere: clang/gcc via the C core script; MSVC via
# a /GL + /GENPROFILE CLI train, with the pgd applied later at the
# setuptools link (setup.py /LTCG /USEPROFILE). Linux cells do NOT
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
    # MSVC PGO two-stage (the unix branch delegates to the C core
    # script, which does not wire MSVC — vcvars + pgomgr live here):
    #   stage 1: engine /GL bitcode, CLI linked /LTCG /GENPROFILE
    #   train:   teptris format over the generated corpus (pgc files)
    #   merge:   pgomgr /merge into the pgd
    # The pgd then rides to the setuptools link (setup.py adds
    # /LTCG /USEPROFILE:PGD=... when TEPTRIS_MSVC_PGO=1) — for a
    # static lib, LTCG only happens at the consuming link.
    # Ninja inside vcvars (cmake's VS generator finds no VS instance on
    # the runner images; vswhere -products * does)
    VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
    VSPATH=$("$VSWHERE" -latest -products '*' -property installationPath)
    [ -n "$VSPATH" ] || { echo "vswhere found no VS install" >&2; exit 1; }
    if [ "${RUNNER_ARCH:-}" = "ARM64" ]; then VSARCH=arm64; else VSARCH=amd64; fi
    # generate a .cmd and run it: inline cmd //c strings get mangled by
    # msys quote conversion; a file sidesteps quoting entirely
    VCVARS=$(cygpath -w "$VSPATH/VC/Auxiliary/Build/vcvarsall.bat")
    python3 "$SRC/scripts/gen_bench_corpus.py" "$SRC/bench-corpus"
    cat > _build_msvc.cmd <<CMDEOF
@echo on
call "$VCVARS" $VSARCH || exit /b 1
cmake -B $SRC\build -S $SRC -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DTEPTRIS_BUILD_CLI=ON -DTEPTRIS_BUILD_SHARED=OFF -DTEPTRIS_BUILD_STATIC=ON -DTEPTRIS_ENABLE_LTO=OFF "-DCMAKE_C_FLAGS_RELEASE=/O2 /GL" "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/LTCG /GENPROFILE" || exit /b 1
cmake --build $SRC\build --target teptris_cli || exit /b 1
cd $SRC\build\cli || exit /b 1
for %%f in (..\..\bench-corpus\*.toml) do teptris.exe format %%f >nul || exit /b 1
pgomgr /merge teptris.pgd || exit /b 1
CMDEOF
    cmd //c _build_msvc.cmd
    rc=$?
    rm -f _build_msvc.cmd
    exit $rc
    ;;
  *) echo "unsupported platform: $(uname -s)" >&2; exit 1 ;;
esac
ls -la "$SRC/build/src/"
