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
    # script, which does not wire MSVC - vcvars + pgomgr live here).
    # LNK1268 requires the /GENPROFILE link and the /USEPROFILE link
    # to agree on their option sets (INCREMENTAL, OPT, image kind,
    # EXPORTs...), and the consuming link is the setuptools ext link:
    # a DLL exporting PyInit__native. So the TRAIN is exactly that:
    # a shim PyInit__native + the static /GL archive linked
    # /DLL /LTCG /GENPROFILE, driven by a ctypes loop over the corpus
    # (real parse+emit training), then pgomgr /merge. setup.py adds
    # /LTCG /USEPROFILE:PGD=... at its link when TEPTRIS_MSVC_PGO=1.
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
    cat > _pgotrain.py <<'PYEOF'
import ctypes, glob, os
here = os.path.dirname(os.path.abspath(__file__))
dll = ctypes.CDLL(os.path.join(here, "libteptris-src", "bpgd", "train.pyd"))
dll.teptris_parse.restype = ctypes.c_int
dll.teptris_parse.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p,
                              ctypes.POINTER(ctypes.c_void_p)]
dll.teptris_document_emit.restype = ctypes.c_int
dll.teptris_document_emit.argtypes = [ctypes.c_void_p,
                                      ctypes.POINTER(ctypes.c_char_p),
                                      ctypes.POINTER(ctypes.c_size_t)]
dll.teptris_document_free.argtypes = [ctypes.c_void_p]
for path in sorted(glob.glob(os.path.join(here, "libteptris-src",
                                          "bench-corpus", "*.toml"))):
    data = open(path, "rb").read()
    for _ in range(3):
        doc = ctypes.c_void_p()
        if dll.teptris_parse(data, len(data), None, ctypes.byref(doc)) != 0:
            raise SystemExit("train parse failed: " + path)
        buf = ctypes.c_char_p()
        n = ctypes.c_size_t()
        dll.teptris_document_emit(doc, ctypes.byref(buf), ctypes.byref(n))
        dll.teptris_document_free(doc)
print("training ok")
PYEOF
    cat > _build_msvc.cmd <<CMDEOF
@echo on
call "$VCVARS" $VSARCH || exit /b 1
rem /GL comes from the engine's own knobs (TEPTRIS_PROFILE_*), not
rem hand-rolled CFLAGS - the engine wires MSVC PGO per target
cmake -B $SRC\bstatic -S $SRC -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DTEPTRIS_BUILD_CLI=OFF -DTEPTRIS_BUILD_SHARED=OFF -DTEPTRIS_BUILD_STATIC=ON -DTEPTRIS_ENABLE_LTO=OFF -DTEPTRIS_PROFILE_USE=ON || exit /b 1
cmake --build $SRC\bstatic || exit /b 1
cmake -B $SRC\btrain -S $SRC -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DTEPTRIS_BUILD_CLI=OFF -DTEPTRIS_BUILD_SHARED=OFF -DTEPTRIS_BUILD_STATIC=ON -DTEPTRIS_ENABLE_LTO=OFF -DTEPTRIS_PROFILE_TRAIN=ON || exit /b 1
cmake --build $SRC\btrain --target teptris || exit /b 1
mkdir $SRC\bpgd 2>nul
echo void PyInit__native(void){} > $SRC\btrain\train_shim.c || exit /b 1
cl /O2 /c /Fo$SRC\btrain\train_shim.obj $SRC\btrain\train_shim.c || exit /b 1
rem WHOLEARCHIVE: the shim references no engine symbols, so member-wise
rem pull would leave nothing to instrument (LNK1264). Link through the
rem cl driver with /MD: bare link omits the DLL-CRT defaults the /MD
rem engine objects expect (__imp_realloc & friends, LNK2001).
rem linker-only flags go after /link; cl eats everything before it
cl /LD /MD /O2 /Fe:$SRC\bpgd\train.pyd $SRC\btrain\train_shim.obj /link /LTCG /GENPROFILE /INCREMENTAL:NO /OPT:REF /OPT:ICF /EXPORT:PyInit__native /WHOLEARCHIVE:$SRC\btrain\src\teptris.lib || exit /b 1
python _pgotrain.py || exit /b 1
cd $SRC\bpgd || exit /b 1
pgomgr /merge train.pgd || exit /b 1
CMDEOF
    cmd //c _build_msvc.cmd
    rc=$?
    rm -f _build_msvc.cmd _pgotrain.py
    exit $rc
    ;;
  *) echo "unsupported platform: $(uname -s)" >&2; exit 1 ;;
esac
ls -la "$SRC/bstatic/src/" "$SRC/bpgd/"*.pgd
