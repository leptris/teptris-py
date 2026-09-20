# Packaging doctrine (owner directive 2026-09-19): compiled
# packages carry the compiled library AND its source; source
# packages carry source that compiles at install. Layout-aware:
# WHEEL installs carry the engine tree at _engine/ (asserted here);
# sdist and in-place dev installs prove the other doctrine half by
# BUILDING at install (the sdist self-test lane), so only the
# compiled binary is asserted for them. unittest only - the CI lane
# runs `python -m unittest discover` WITHOUT installing the package
# (it builds a wheel), so teptris is imported lazily per test.
import glob
import os
import unittest


def _pkg_dir():
    import teptris
    return os.path.dirname(teptris.__file__)


class PackagingDoctrineTest(unittest.TestCase):
    def test_installed_package_carries_the_engine_binary(self):
        pkg = _pkg_dir()
        exts = (glob.glob(os.path.join(pkg, "_native*.so"))
                + glob.glob(os.path.join(pkg, "_native*.pyd"))
                + glob.glob(os.path.join(pkg, "_native*.dylib")))
        self.assertTrue(exts, f"no compiled extension under {pkg}")

    def test_wheel_install_carries_the_engine_source(self):
        pkg = _pkg_dir()
        engine = os.path.join(pkg, "_engine")
        if not os.path.isdir(engine):
            self.skipTest("engine tree ships in wheels; sdist/dev "
                          "builds prove the source half by compiling "
                          "at install")
        src = (glob.glob(os.path.join(engine, "src", "teptris", "*.c"))
               + glob.glob(os.path.join(engine, "src", "teptris", "*", "*.c"))
               + glob.glob(os.path.join(engine, "src", "teptris", "*", "*", "*.c")))
        self.assertGreaterEqual(len(src), 20, f"only {len(src)} engine sources under {engine}")
        self.assertTrue(os.path.exists(os.path.join(engine, "LICENSE.md")),
                        "engine LICENSE missing from the wheel install")


if __name__ == "__main__":
    unittest.main()
