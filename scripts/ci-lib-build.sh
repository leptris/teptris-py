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
    # script, which does not wire MSVC — vcvars + pgomgr live here).
    # LNK1268 requires the /GENPROFILE link and the /USEPROFILE link
    # to be the same image kind, and the consuming link (setuptools)
    # builds a DLL (.pyd) — so the TRAIN must be a DLL link too:
    #   stage 1: engine /GL bitcode; teptris_shared (DLL) linked
    #            /LTCG /GENPROFILE; plain-CLI links that DLL; the
    #            static archive for setup.py comes from the same /GL
    #            objects
    #   train:   teptris format over the generated corpus — the CLI
    #            drives the instrumented DLL (pgc beside the DLL)
    #   merge:   pgomgr /merge into teptris.pgd
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
rem two trees: MSVC names the shared import library teptris.lib - the
rem same name as the static archive - so one tree with both targets
rem breaks Ninja (multiple rules generate src/teptris.lib)
cmake -B $SRC\bstatic -S $SRC -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DTEPTRIS_BUILD_CLI=OFF -DTEPTRIS_BUILD_SHARED=OFF -DTEPTRIS_BUILD_STATIC=ON -DTEPTRIS_ENABLE_LTO=OFF "-DCMAKE_C_FLAGS_RELEASE=/O2 /GL" || exit /b 1
cmake --build $SRC\bstatic || exit /b 1
cmake -B $SRC\bshared -S $SRC -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DTEPTRIS_BUILD_CLI=ON -DTEPTRIS_BUILD_SHARED=ON -DTEPTRIS_BUILD_STATIC=OFF -DTEPTRIS_ENABLE_LTO=OFF "-DCMAKE_C_FLAGS_RELEASE=/O2 /GL" "-DCMAKE_SHARED_LINKER_FLAGS_RELEASE=/LTCG /GENPROFILE /INCREMENTAL:NO /OPT:REF /OPT:ICF" || exit /b 1
cmake --build $SRC\bshared || exit /b 1
set PATH=$SRC\bshared\src;%PATH%
for %%f in ($SRC\bench-corpus\*.toml) do $SRC\bshared\cli\teptris.exe format %%f >nul || exit /b 1
cd $SRC\bshared\src || exit /b 1
pgomgr /merge teptris.pgd || exit /b 1
CMDEOF
    cmd //c _build_msvc.cmd
    rc=$?
    rm -f _build_msvc.cmd
    exit $rc
    ;;
  *) echo "unsupported platform: $(uname -s)" >&2; exit 1 ;;
esac
ls -la "$SRC/bstatic/src/" "$SRC/bshared/src/"
