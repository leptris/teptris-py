import glob
import os
import sys
from setuptools import setup, Extension

# Vendored engine (sdist builds): when the C tree sits at vendor/,
# compile it straight into the extension with the installing
# interpreter's compiler — no cmake, no prebuilt archive. Precedence
# over the archive path so a coincidental sibling teptris checkout
# can never hijack an sdist install.
_vendor = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "vendor", "libteptris")
_engine_src = sorted(
    glob.glob(os.path.join(_vendor, "src", "teptris", "**", "*.c"),
              recursive=True))

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
        include_dirs=[os.path.join(_vendor, "src"),
                      os.path.join(_vendor, "src", "include")],
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
setup(ext_modules=[ext], package_dir={"": "src"},
      options={"bdist_wheel": {"py_limited_api": "cp39"}})
