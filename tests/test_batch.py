import datetime as dt
import sys
import unittest

sys.path.insert(0, "src")
import teptris  # noqa: E402


class LoadsBatch(unittest.TestCase):
    def test_parity_with_loads(self):
        docs = ["a = 1\n", "b = \"x\"\n", "[t]\nk = [1, 2]\n"]
        expected = [teptris.loads(d) for d in docs]
        self.assertEqual(teptris.loads_batch(docs), expected)

    def test_empty_list(self):
        self.assertEqual(teptris.loads_batch([]), [])

    def test_datetime_contract(self):
        out = teptris.loads_batch([
            "o = 1979-05-27T07:32:00Z\n",
            "d = 1979-05-27\n",
            "t = 07:32:00\n",
        ])
        self.assertEqual(out[0]["o"],
                         dt.datetime(1979, 5, 27, 7, 32, tzinfo=dt.timezone.utc))
        self.assertEqual(out[1]["d"], dt.date(1979, 5, 27))
        self.assertEqual(out[2]["t"], dt.time(7, 32))

    def test_bytes_entries(self):
        self.assertEqual(teptris.loads_batch([b"x = 2\n"]), [{"x": 2}])

    def test_first_failure_raises_with_position(self):
        with self.assertRaises(teptris.TOMLDecodeError) as ctx:
            teptris.loads_batch(["a = 1\n", "b = ?\n", "c = 3\n"])
        self.assertEqual(ctx.exception.line, 1)
        self.assertEqual(ctx.exception.column, 5)

    def test_rejects_non_list(self):
        with self.assertRaises(TypeError):
            teptris.loads_batch("a = 1\n")

    def test_rejects_non_string_entries(self):
        with self.assertRaises(TypeError):
            teptris.loads_batch([1])
        with self.assertRaises(TypeError):
            teptris.loads_batch(["a = 1\n", None])

    def test_roundtrip_many(self):
        docs = [f"id = {i}\nname = \"row-{i}\"\n" for i in range(500)]
        out = teptris.loads_batch(docs)
        self.assertEqual(len(out), 500)
        self.assertEqual(out[499], {"id": 499, "name": "row-499"})


if __name__ == "__main__":
    unittest.main()
