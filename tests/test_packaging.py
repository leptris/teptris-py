# Packaging doctrine (owner directive 2026-09-19): compiled
# packages carry the compiled library AND its source; source
# packages carry source that compiles at install. Layout-aware:
# WHEEL installs carry the engine tree at _engine/ (asserted here);
# sdist and in-place dev installs prove the other doctrine half by
# BUILDING at install (the sdist self-test lane), so only the
# compiled binary is asserted for them. unittest only - the CI lane
# runs `python -m unittest discover`.
import glob
import os
import unittest

import teptris

PKG = os.path.dirname(teptris.__file__)
ENGINE = os.path.join(PKG, "_engine")
IS_WHEEL_INSTALL = os.path.isdir(ENGINE)


class PackagingDoctrineTest(unittest.TestCase):
    def test_installed_package_carries_the_engine_binary(self):
        exts = (glob.glob(os.path.join(PKG, "_native*.so"))
                + glob.glob(os.path.join(PKG, "_native*.pyd"))
                + glob.glob(os.path.join(PKG, "_native*.dylib")))
        self.assertTrue(exts, f"no compiled extension under {PKG}")

    @unittest.skipIf(not IS_WHEEL_INSTALL,
                     "engine tree ships in wheels; sdist/dev builds "
                     "prove the source half by compiling at install")
    def test_wheel_install_carries_the_engine_source(self):
        src = (glob.glob(os.path.join(ENGINE, "src", "teptris", "*.c"))
               + glob.glob(os.path.join(ENGINE, "src", "teptris", "*", "*.c"))
               + glob.glob(os.path.join(ENGINE, "src", "teptris", "*", "*", "*.c")))
        self.assertGreaterEqual(len(src), 20, f"only {len(src)} engine sources under {ENGINE}")
        self.assertTrue(os.path.exists(os.path.join(ENGINE, "LICENSE.md")),
                        "engine LICENSE missing from the wheel install")


if __name__ == "__main__":
    unittest.main()
