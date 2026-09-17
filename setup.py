import os
import sys
from setuptools import setup, Extension

inc = os.environ.get("TEPTRIS_INCLUDE", "../teptris/src/include")
libdir = os.environ.get("TEPTRIS_LIBDIR", "../teptris/build-shared/src")

# Static-first: one self-contained _native module — no soname/DLL
# chain to ship, relocate, or repair (the teptris-ruby #38 lesson,
# applied to wheels so auditwheel/cibuildwheel can work with a plain
# extension). Falls back to the shared lib for local dev trees.
_archive = os.path.join(
    libdir, "teptris.lib" if sys.platform == "win32" else "libteptris.a"
)
if os.path.exists(_archive):
    _link = {"extra_objects": [_archive]}
else:
    # @loader_path is Mach-O; ELF spells it $ORIGIN; PE has neither —
    # the DLL ships beside the .pyd and CPython 3.8+ loads it via
    # LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
    _link = {
        "library_dirs": [libdir],
        "libraries": ["teptris"],
        "extra_link_args": (
            []
            if sys.platform == "win32"
            else [
                "-Wl,-rpath,"
                + ("@loader_path" if sys.platform == "darwin" else "$ORIGIN")
            ]
        ),
    }
ext = Extension(
    "teptris._native",
    sources=["src/teptris/_native.c"],
    include_dirs=[inc],
    **_link,
)
# package_dir spelled here too: raw `setup.py build_ext --inplace`
# (which the wheel workflow runs) doesn't apply the pyproject
# src-layout on older setuptools (3.8 cells resolved one), and then
# the inplace copy targets teptris/ instead of src/teptris/.
setup(ext_modules=[ext], package_dir={"": "src"})
