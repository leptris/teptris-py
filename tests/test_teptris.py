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
