import os
import sys
from setuptools import setup, Extension

inc = os.environ.get("TEPTRIS_INCLUDE", "../teptris/src/include")
libdir = os.environ.get("TEPTRIS_LIBDIR", "../teptris/build-shared/src")

ext = Extension(
    "teptris._native",
    sources=["src/teptris/_native.c"],
    include_dirs=[inc],
    library_dirs=[libdir],
    libraries=["teptris"],
    # @loader_path is Mach-O; ELF spells the loader-relative rpath $ORIGIN;
    # PE has neither — the DLL ships beside the .pyd and CPython 3.8+
    # loads it via LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
    extra_link_args=(
        []
        if sys.platform == "win32"
        else ["-Wl,-rpath," + ("@loader_path" if sys.platform == "darwin" else "$ORIGIN")]
    ),
)
# package_dir spelled here too: raw `setup.py build_ext --inplace`
# (which the wheel workflow runs) doesn't apply the pyproject
# src-layout on older setuptools (3.8 cells resolved one), and then
# the inplace copy targets teptris/ instead of src/teptris/.
setup(ext_modules=[ext], package_dir={"": "src"})
