import os
from setuptools import setup, Extension

inc = os.environ.get("TEPTRIS_INCLUDE", "../teptris/src/include")
libdir = os.environ.get("TEPTRIS_LIBDIR", "../teptris/build-shared/src")

ext = Extension(
    "teptris._native",
    sources=["src/teptris/_native.c"],
    include_dirs=[inc],
    library_dirs=[libdir],
    libraries=["teptris"],
    extra_link_args=["-Wl,-rpath,@loader_path"],
)
setup(ext_modules=[ext])
