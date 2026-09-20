# Packaging doctrine (owner directive 2026-09-19): compiled
# packages carry the compiled library AND its source; source
# packages carry source that compiles at install. This asserts the
# INSTALLED layout - binary beside the vendored engine tree - so a
# packaging regression fails CI, not users. Skipped in dev-override
# mode (TEPTRIS_INCLUDE/TEPTRIS_LIBDIR point at an external engine).
import glob
import os

import pytest

import teptris

pytestmark = pytest.mark.skipif(
    os.environ.get("TEPTRIS_INCLUDE") or os.environ.get("TEPTRIS_LIBDIR"),
    reason="dev engine override: the installed layout is not under test",
)


def test_installed_package_carries_the_engine_binary():
    pkg = os.path.dirname(teptris.__file__)
    exts = glob.glob(os.path.join(pkg, "_native*.so")) + \
        glob.glob(os.path.join(pkg, "_native*.pyd")) + \
        glob.glob(os.path.join(pkg, "_native*.dylib"))
    assert exts, f"no compiled extension under {pkg}"


def test_installed_package_carries_the_engine_source():
    pkg = os.path.dirname(teptris.__file__)
    src = glob.glob(os.path.join(pkg, "_engine", "src", "teptris", "*.c")) + \
        glob.glob(os.path.join(pkg, "_engine", "src", "teptris", "*", "*.c"))
    assert len(src) >= 20, f"only {len(src)} engine sources under {pkg}/_engine"
    assert os.path.exists(os.path.join(pkg, "_engine", "LICENSE.md"))
