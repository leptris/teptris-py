# Packaging doctrine (owner directive 2026-09-19): compiled
# packages carry the compiled library AND its source; source
# packages carry source that compiles at install. This asserts the
# INSTALLED layout - binary beside the vendored engine tree - so a
# packaging regression fails CI, not users. unittest only: the CI
# lane runs `python -m unittest discover`. Skipped in dev-override
# mode (TEPTRIS_INCLUDE/TEPTRIS_LIBDIR point at an external engine).
import glob
import os
import unittest

import teptris

PKG = os.path.dirname(teptris.__file__)


@unittest.skipIf(
    os.environ.get("TEPTRIS_INCLUDE") or os.environ.get("TEPTRIS_LIBDIR"),
    "dev engine override: the installed layout is not under test",
)
class PackagingDoctrineTest(unittest.TestCase):
    def test_installed_package_carries_the_engine_binary(self):
        exts = (glob.glob(os.path.join(PKG, "_native*.so"))
                + glob.glob(os.path.join(PKG, "_native*.pyd"))
                + glob.glob(os.path.join(PKG, "_native*.dylib")))
        self.assertTrue(exts, f"no compiled extension under {PKG}")

    def test_installed_package_carries_the_engine_source(self):
        src = (glob.glob(os.path.join(PKG, "_engine", "src", "teptris", "*.c"))
               + glob.glob(os.path.join(PKG, "_engine", "src", "teptris", "*", "*.c"))
               + glob.glob(os.path.join(PKG, "_engine", "src", "teptris", "*", "*", "*.c")))
        self.assertGreaterEqual(len(src), 20, f"only {len(src)} engine sources under {PKG}/_engine")
        self.assertTrue(os.path.exists(os.path.join(PKG, "_engine", "LICENSE.md")))


if __name__ == "__main__":
    unittest.main()
