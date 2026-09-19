import glob
import os
import sys
from setuptools import setup, Extension

# Vendored engine (sdist builds): when the C tree sits at vendor/,
# compile it straight into the extension with the installing
# interpreter's compiler — no cmake, no prebuilt archive. Precedence
# over the archive path so a coincidental sibling teptris checkout
# can never hijack an sdist install.
# RELATIVE paths only: setuptools' manifest check rejects absolute
# source paths when older setuptools builds the sdist (seen with the
# 3.9-era resolution; setup.py always runs from the project root).
_engine_src = sorted(
    glob.glob(os.path.join("vendor", "libteptris", "src", "teptris",
                           "**", "*.c"), recursive=True))

inc = os.environ.get("TEPTRIS_INCLUDE", "../teptris/src/include")
libdir = os.environ.get("TEPTRIS_LIBDIR", "../teptris/build-shared/src")

# Static-first: one self-contained _native module — no soname/DLL
# chain to ship, relocate, or repair (the teptris-ruby #38 lesson,
# applied to wheels so auditwheel/cibuildwheel can work with a plain
# extension). Falls back to the shared lib for local dev trees.
_archive = os.path.join(
    libdir, "teptris.lib" if sys.platform == "win32" else "libteptris.a"
)
if _engine_src:
    ext = Extension(
        "teptris._native",
        sources=["src/teptris/_native.c"] + _engine_src,
        include_dirs=["vendor/libteptris/src",
                      "vendor/libteptris/src/include"],
        py_limited_api=True,
    )
elif os.path.exists(_archive):
    ext = Extension(
        "teptris._native",
        sources=["src/teptris/_native.c"],
        include_dirs=[inc],
        py_limited_api=True,
        extra_objects=[_archive],
    )
else:
    # @loader_path is Mach-O; ELF spells it $ORIGIN; PE has neither —
    # the DLL ships beside the .pyd and CPython 3.8+ loads it via
    # LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
    ext = Extension(
        "teptris._native",
        sources=["src/teptris/_native.c"],
        include_dirs=[inc],
        py_limited_api=True,
        library_dirs=[libdir],
        libraries=["teptris"],
        extra_link_args=(
            []
            if sys.platform == "win32"
            else [
                "-Wl,-rpath,"
                + ("@loader_path" if sys.platform == "darwin" else "$ORIGIN")
            ]
        ),
    )
# package_dir spelled here too: raw `setup.py build_ext --inplace`
# (which the wheel workflow runs) doesn't apply the pyproject
# src-layout on older setuptools (3.8 cells resolved one), and then
# the inplace copy targets teptris/ instead of src/teptris/.
# NOTE: package_data lives in pyproject.toml ([tool.setuptools.
# package-data]) — when both exist, the pyproject table WINS over
# this dict (the 0.2.19 lesson: engine sources silently absent).
# The engine source rides inside every wheel at teptris/_engine/
# (populated by CIBW_BEFORE_BUILD; the recompile path).
setup(ext_modules=[ext], package_dir={"": "src"},
      options={"bdist_wheel": {"py_limited_api": "cp39"}})
