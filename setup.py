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
    # @loader_path is Mach-O; ELF spells the loader-relative rpath $ORIGIN
    extra_link_args=[
        "-Wl,-rpath," + ("@loader_path" if sys.platform == "darwin" else "$ORIGIN")
    ],
)
setup(ext_modules=[ext])
