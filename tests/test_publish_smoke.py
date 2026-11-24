"""The publish smoke (teptris-py#172): the REGISTRY half of the
release gate. The published-artifact smoke installs wheels, so it
stayed green through the wheels-only waves (0.3.30/31/33) — the sdist
gap surfaced only in binding-bench's prev fetch. This smoke reads the
PyPI JSON API and asserts a release carries BOTH halves: wheels AND
the sdist. Run after every publish:

    python3 scripts/publish_smoke.py 0.3.34.0          # one version
    python3 scripts/publish_smoke.py --all             # every release
"""

import importlib.util
import json
import os
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_SCRIPT = os.path.join(_HERE, "..", "scripts", "publish_smoke.py")
_spec = importlib.util.spec_from_file_location("publish_smoke", _SCRIPT)
publish_smoke = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(publish_smoke)


def release(*names: str) -> list[dict]:
    return [{"packagetype": ("sdist" if n.endswith(".tar.gz") else "bdist_wheel"),
             "yanked": False} for n in names]


class PublishSmokeTest(unittest.TestCase):
    def test_wheels_plus_sdist_passes(self):
        problems = publish_smoke.check_release(
            "0.3.29.0", release("teptris-0.3.29.0.tar.gz",
                                "teptris-0.3.29.0-cp312-manylinux.whl"))
        self.assertEqual(problems, [])

    def test_wheels_only_release_fails_with_the_sdist_named(self):
        problems = publish_smoke.check_release(
            "0.3.33.0", release("teptris-0.3.33.0-cp312-manylinux.whl"))
        self.assertEqual(len(problems), 1)
        self.assertIn("sdist", problems[0])
        self.assertIn("0.3.33.0", problems[0])

    def test_sdist_only_release_fails_with_the_wheels_named(self):
        problems = publish_smoke.check_release(
            "0.3.34.0", release("teptris-0.3.34.0.tar.gz"))
        self.assertEqual(len(problems), 1)
        self.assertIn("wheel", problems[0])

    def test_empty_release_fails(self):
        problems = publish_smoke.check_release("0.9.9", [])
        self.assertEqual(len(problems), 1)
        self.assertIn("no files", problems[0])

    def test_yanked_files_do_not_count(self):
        files = [{"packagetype": "bdist_wheel", "yanked": True},
                 {"packagetype": "sdist", "yanked": True}]
        problems = publish_smoke.check_release("0.9.9", files)
        self.assertEqual(len(problems), 1)
        self.assertIn("yanked", problems[0])

    def test_all_shipped_versions_must_carry_both(self):
        report = publish_smoke.check_all({
            "info": {"version": "0.3.33.0"},
            "releases": {
                "0.3.29.0": release("teptris-0.3.29.0.tar.gz",
                                    "teptris-0.3.29.0-cp312.whl"),
                "0.3.33.0": release("teptris-0.3.33.0-cp312.whl"),
            },
        })
        self.assertEqual([line for line in report if "0.3.33.0" in line],
                         [line for line in report if "sdist" in line
                          and "0.3.33.0" in line])

    def test_main_exits_nonzero_on_a_wheels_only_registry(self):
        # the recorded 0.3.33.0 shape: wheels only -> the CLI must fail
        payload = json.dumps({"info": {"version": "0.3.33.0"},
                              "releases": {"0.3.33.0": release(
                                  "teptris-0.3.33.0-cp312.whl")}})
        rc = publish_smoke.main(["publish_smoke.py", "--json", payload,
                                 "0.3.33.0"])
        self.assertNotEqual(rc, 0)

    def test_main_exits_zero_when_both_halves_exist(self):
        payload = json.dumps({"info": {"version": "0.3.34.0"},
                              "releases": {"0.3.34.0": release(
                                  "teptris-0.3.34.0.tar.gz",
                                  "teptris-0.3.34.0-cp312.whl")}})
        rc = publish_smoke.main(["publish_smoke.py", "--json", payload,
                                 "0.3.34.0"])
        self.assertEqual(rc, 0)

    def test_missing_version_is_a_failure(self):
        payload = json.dumps({"info": {"version": "0.3.34.0"},
                              "releases": {}})
        rc = publish_smoke.main(["publish_smoke.py", "--json", payload,
                                 "9.9.9"])
        self.assertNotEqual(rc, 0)


if __name__ == "__main__":
    unittest.main()
