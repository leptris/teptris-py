import datetime as dt
import sys
import unittest

sys.path.insert(0, "src")
import teptris  # noqa: E402

try:
    import tomli

    HAVE_TOMLI = True
except ImportError:
    HAVE_TOMLI = False


class Loads(unittest.TestCase):
    def test_scalars(self):
        self.assertEqual(
            teptris.loads("a = 1\nb = 1.5\nc = 'x'\nd = true\ne = [1, 2]"),
            {"a": 1, "b": 1.5, "c": "x", "d": True, "e": [1, 2]})

    def test_nested_and_aot(self):
        doc = teptris.loads(
            "root = true\n[a]\nx = 1\n[[aot]]\nn = 'first'\n[[aot]]\nn = 'second'\n")
        self.assertEqual(doc, {
            "root": True, "a": {"x": 1},
            "aot": [{"n": "first"}, {"n": "second"}]})

    def test_datetimes_tomllib_contract(self):
        doc = teptris.loads(
            "o = 1979-05-27T07:32:00Z\nl = 1979-05-27T07:32:00\n"
            "d = 1979-05-27\nt = 07:32:00\n")
        self.assertEqual(doc["o"],
                         dt.datetime(1979, 5, 27, 7, 32, tzinfo=dt.timezone.utc))
        self.assertEqual(doc["l"], dt.datetime(1979, 5, 27, 7, 32))
        self.assertEqual(doc["d"], dt.date(1979, 5, 27))
        self.assertEqual(doc["t"], dt.time(7, 32))

    def test_error_carries_line_column(self):
        with self.assertRaises(teptris.TOMLDecodeError) as cm:
            teptris.loads("a = 1\nb = ?\n")
        self.assertEqual(cm.exception.line, 2)
        self.assertEqual(cm.exception.column, 5)

    def test_dumps_roundtrip(self):
        obj = {"title": "teptris", "nested": {"x": 1},
               "aot": [{"n": 1}, {"n": 2}], "f": 1.5}
        self.assertEqual(teptris.loads(teptris.dumps(obj)), obj)

    def test_bytes_input(self):
        self.assertEqual(teptris.loads(b"a = 1"), {"a": 1})

    @unittest.skipUnless(HAVE_TOMLI, "tomli not installed")
    def test_tomli_parity(self):
        cases = [
            "a = 1\nb = -2\nc = 0xFF\n",
            "s = 'lit'\nt = \"esc\\t\"\n",
            "f = 3.14\ng = 1e6\n",
            "[t]\nx = 1\n[t.u]\ny = [1, 2]\n",
            "[[a]]\nn = 1\n[[a]]\nn = 2\n",
            "d = 1979-05-27\no = 1979-05-27T07:32:00Z\nl = 1979-05-27T07:32:00\nt = 07:32:00\n",
            "k.'quoted key' = 1\n",
        ]
        for i, doc in enumerate(cases):
            with self.subTest(case=i):
                self.assertEqual(teptris.loads(doc), tomli.loads(doc))


if __name__ == "__main__":
    unittest.main()


class Dumps(unittest.TestCase):
    def test_scalars_and_roundtrip(self):
        obj = {"s": "hello", "i": -42, "f": 3.14, "b": True,
               "arr": [1, 2.5, "x"], "sub": {"k": 1}}
        self.assertEqual(teptris.loads(teptris.dumps(obj)), obj)

    def test_datetimes_exact(self):
        import datetime as dt
        tz = dt.timezone(dt.timedelta(hours=5, minutes=30))
        obj = {"aware": dt.datetime(2026, 9, 15, 10, 30, 15, 500000, tzinfo=tz),
               "naive": dt.datetime(2026, 9, 15, 10, 30, 15),
               "d": dt.date(2026, 9, 15),
               "t": dt.time(7, 32)}
        out = teptris.dumps(obj)
        self.assertIn("aware = 2026-09-15T10:30:15.5+05:30", out)
        self.assertIn("naive = 2026-09-15T10:30:15", out)
        self.assertEqual(teptris.loads(out), obj)

    def test_float_edges(self):
        import math
        obj = {"zero": 0.0, "neg_zero": -0.0, "nan": float("nan"),
               "inf": float("inf"), "neg_inf": float("-inf")}
        out = teptris.loads(teptris.dumps(obj))
        self.assertEqual(out["zero"], 0.0)
        self.assertTrue(math.copysign(1.0, out["neg_zero"]) < 0)
        self.assertTrue(math.isnan(out["nan"]))
        self.assertEqual(out["inf"], float("inf"))
        self.assertEqual(out["neg_inf"], float("-inf"))

    def test_mixed_and_aot(self):
        obj = {"mix": [{"q": 1}, 2], "nested": [[1, 2], [3]],
               "aot": [{"n": 1}, {"n": 2}]}
        out = teptris.dumps(obj)
        self.assertIn("mix = [{q = 1}, 2]", out)
        self.assertIn("nested = [[1, 2], [3]]", out)
        self.assertEqual(teptris.loads(out), obj)

    def test_rejects(self):
        with self.assertRaises(TypeError):
            teptris.dumps([1, 2])
        with self.assertRaises(TypeError):
            teptris.dumps({"x": object()})
        with self.assertRaises(TypeError):
            teptris.dumps({1: 2})

    def test_special_strings(self):
        obj = {"esc": "a\"b\\c\nd", "uni": "héllo", "lit": "no 'quotes' here"}
        out = teptris.dumps(obj)
        self.assertIn("\"a\\\"b\\\\c\\nd\"", out)
        self.assertEqual(teptris.loads(out), obj)
