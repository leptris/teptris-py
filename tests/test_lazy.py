import datetime as dt
import unittest

import teptris


class LoadsLazy(unittest.TestCase):
    def test_root_table_kind_and_len(self):
        d = teptris.loads_lazy("x = 1\ny = 2\n")
        self.assertEqual(d.kind(), "table")
        self.assertEqual(len(d), 2)

    def test_subscript_scalar(self):
        d = teptris.loads_lazy("x = 42\n")
        s = d["x"]
        self.assertEqual(s.kind(), "scalar")
        self.assertEqual(s.value(), 42)

    def test_subscript_nested_table(self):
        d = teptris.loads_lazy("[a]\nb = 1\n[c]\nd = 2\n")
        self.assertEqual(d["a"]["b"].value(), 1)
        self.assertEqual(d["c"]["d"].value(), 2)

    def test_array_iteration(self):
        d = teptris.loads_lazy("""
[[items]]
sku = "a"
[[items]]
sku = "b"
""")
        items = d["items"]
        self.assertEqual(items.kind(), "array")
        self.assertEqual(len(items), 2)
        skus = [it["sku"].value() for it in items]
        self.assertEqual(skus, ["a", "b"])

    def test_table_iteration(self):
        d = teptris.loads_lazy("a = 1\nb = 2\n")
        keys = [k for k, _ in d]
        self.assertEqual(keys, ["a", "b"])

    def test_absent_key_is_none(self):
        d = teptris.loads_lazy("x = 1\n")
        self.assertIsNone(d["absent"])

    def test_to_dict_flattens(self):
        d = teptris.loads_lazy("""
title = "x"
[owner]
name = "t"
""")
        self.assertEqual(d.to_dict(),
                         {"title": "x", "owner": {"name": "t"}})

    def test_to_list_alias(self):
        d = teptris.loads_lazy("x = 1\n")
        self.assertEqual(d.to_list(), {"x": 1})

    def test_value_only_on_scalars(self):
        d = teptris.loads_lazy("[a]\nb = 1\n")
        with self.assertRaises(TypeError):
            d.value()
        with self.assertRaises(TypeError):
            d["a"].value()

    def test_scalar_is_not_iterable(self):
        d = teptris.loads_lazy("x = 1\n")
        with self.assertRaises(TypeError):
            for _ in d["x"]:
                pass

    def test_scalar_subscript_raises(self):
        d = teptris.loads_lazy("x = 1\n")
        with self.assertRaises(TypeError):
            d["x"][0]

    def test_bytes_input(self):
        self.assertEqual(teptris.loads_lazy(b"x = 1\n").to_dict(),
                         {"x": 1})

    def test_empty_document(self):
        self.assertEqual(teptris.loads_lazy("").to_dict(), {})

    def test_decode_error(self):
        with self.assertRaises(teptris.TOMLDecodeError) as ctx:
            teptris.loads_lazy("a = [1,")
        self.assertEqual(ctx.exception.line, 1)
        self.assertEqual(ctx.exception.column, 8)

    def test_datetime_contract(self):
        d = teptris.loads_lazy("""
o = 1979-05-27T07:32:00Z
l = 1979-05-27T07:32:00
dl = 1979-05-27
tl = 07:32:00
""")
        self.assertEqual(d["o"].value(),
                         dt.datetime(1979, 5, 27, 7, 32, tzinfo=dt.timezone.utc))
        self.assertIsInstance(d["l"].value(), dt.datetime)
        self.assertEqual(d["dl"].value(), dt.date(1979, 5, 27))
        self.assertEqual(d["tl"].value(), dt.time(7, 32))

    def test_bad_type_input(self):
        with self.assertRaises(TypeError):
            teptris.loads_lazy(123)

    def test_array_index_out_of_range(self):
        d = teptris.loads_lazy("a = [1, 2, 3]\n")
        with self.assertRaises(IndexError):
            d["a"][99]


class LoadsLazyParity(unittest.TestCase):
    """loads_lazy.to_dict() == loads() over a representative corpus."""

    CORPUS = """
title = "teptris"
version = 1.5
ok = true
nope = false
when = 1979-05-27T07:32:00Z
on = 2026-09-21

[owner]
name = "ronald"
age = 42

[[items]]
sku = "a"
qty = 1

[[items]]
sku = "b"
qty = 2

[meta]
empty_arr = []
nested_arr = [[1, 2], [3, 4]]
"""

    def test_parity(self):
        eager = teptris.loads(self.CORPUS)
        lazy_flat = teptris.loads_lazy(self.CORPUS).to_dict()
        self.assertEqual(eager, lazy_flat)


if __name__ == "__main__":
    unittest.main()
