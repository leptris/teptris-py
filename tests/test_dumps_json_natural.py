"""Natural-JSON dumps (engine 0.3.0): the JSON a host wants — real
numbers, booleans, RFC 3339 datetime strings. The documented lossy
mappings: non-finite floats become null, and a datetime is
indistinguishable from a same-shaped string."""
import datetime as dt
import json
import unittest

import teptris


class DumpsJsonNaturalTest(unittest.TestCase):
    def test_native_scalars(self):
        obj = {"i": 42, "f": 3.5, "t": True, "fa": False, "s": "hi"}
        self.assertEqual(
            json.loads(teptris.dumps_json_natural(obj)), obj)

    def test_datetimes_as_rfc3339_strings(self):
        obj = {
            "at": dt.datetime(2026, 10, 9, 12, 34, 56,
                              tzinfo=dt.timezone.utc),
            "d": dt.date(2026, 10, 9),
        }
        out = json.loads(teptris.dumps_json_natural(obj))
        self.assertEqual(out["at"], "2026-10-09T12:34:56Z")
        self.assertEqual(out["d"], "2026-10-09")

    def test_nonfinite_floats_become_null(self):
        out = json.loads(teptris.dumps_json_natural(
            {"nan": float("nan"), "inf": float("inf"), "ok": 1.0}))
        self.assertIsNone(out["nan"])
        self.assertIsNone(out["inf"])
        self.assertEqual(out["ok"], 1.0)

    def test_nested_roundtrip(self):
        obj = {"title": "x", "nums": [1, 2, 3],
               "nest": {"deep": {"v": False}},
               "rows": [{"id": 1}, {"id": 2}]}
        self.assertEqual(json.loads(teptris.dumps_json_natural(obj)), obj)

    def test_views_a_loaded_document(self):
        doc = (
            'title = "demo"\n'
            "[owner]\n"
            'name = "ron"\n'
            "dob = 2026-10-09T10:00:00Z\n"
            "[[items]]\n"
            "id = 1\n"
            'tags = ["a", "b"]\n'
        )
        out = json.loads(teptris.dumps_json_natural(teptris.loads(doc)))
        self.assertEqual(out, {
            "title": "demo",
            "owner": {"name": "ron", "dob": "2026-10-09T10:00:00Z"},
            "items": [{"id": 1, "tags": ["a", "b"]}],
        })

    def test_rejects_non_dict_roots(self):
        with self.assertRaises(TypeError):
            teptris.dumps_json_natural([1, 2])


if __name__ == "__main__":
    unittest.main()
