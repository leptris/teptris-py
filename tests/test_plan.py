"""The plan face (issue #173): build from a spec, walk a document,
typed row results — the py twin of teptris-rs src/plan.rs (Descriptor
build/walk over PlanRow trees) and teptris-ts (planBuild/planWalk over
PlanRowSpec). Semantics are owned by the engine's plan suite; this
file keeps the binding shape honest (rs/ts dialect, absent-on-missing)."""

import datetime
import unittest

import teptris

DOC = """\
name = 'svc'
port = 8080
ratio = 0.5
stable = true
built = 1979-05-27
at = 1979-05-27T07:32:00-07:00
seen = 07:32:00
hosts = ['a.example', 'b.example']
counts = [1, 2]
empty = []

[meta]
title = 'plan tier'
revision = 7
draft = false

[[items]]
id = 1
label = 'first'

[[items]]
id = 2
label = 'second'

[extra]
value = 42
nested = {a = 1}
"""


def spec() -> list:
    """The full-spec plan over DOC: every row kind, every materialized
    type."""
    return [
        "name",                                    # str shorthand = scalar
        {"name": "port", "kind": "scalar"},
        {"name": "ratio", "kind": "scalar"},
        {"name": "stable", "kind": "scalar"},
        {"name": "built", "kind": "scalar"},
        {"name": "at", "kind": "scalar"},
        {"name": "seen", "kind": "scalar"},
        {"name": "hosts", "kind": "collection"},
        {"name": "counts", "kind": "collection"},
        {"name": "empty", "kind": "collection"},
        {"name": "meta", "rows": ["title", "revision", "draft"]},
        {"name": "items", "kind": "collection", "rows": ["id", "label"]},
        {"name": "extra", "kind": "raw"},          # untouched subtree
        {"name": "absent_key", "kind": "scalar"},
        {"name": "absent_table", "rows": ["x"]},
    ]


class PlanBuildTest(unittest.TestCase):
    def test_build_returns_an_opaque_plan(self):
        plan = teptris.plan_build(spec())
        self.assertEqual(type(plan).__name__, "Plan")

    def test_empty_spec_is_rejected(self):
        with self.assertRaises(ValueError):
            teptris.plan_build([])

    def test_row_shapes_are_checked(self):
        with self.assertRaises(TypeError):
            teptris.plan_build([42])
        with self.assertRaises(TypeError):
            teptris.plan_build([{"kind": "scalar"}])  # no name
        with self.assertRaises(TypeError):
            teptris.plan_build([{"name": 7}])
        with self.assertRaises(TypeError):
            teptris.plan_build([{"name": "a", "rows": "not-a-list"}])
        with self.assertRaises(ValueError):
            teptris.plan_build([{"name": "a", "kind": "nan"}])

    def test_nested_sibling_rows_get_distinct_sub_plans(self):
        # the layout contract: two sibling nested rows must land on
        # DIFFERENT sub-plan indexes (the ts 0.1.x flat[] patch bug
        # class); both walk their own keys
        doc = "[a]\ntitle = 't'\n[b]\nrevision = 7\n"
        plan = teptris.plan_build(
            [{"name": "a", "rows": ["title"]}, {"name": "b", "rows": ["revision"]}]
        )
        self.assertEqual(teptris.plan_walk(plan, doc),
                         {"a": {"title": "t"}, "b": {"revision": 7}})

    def test_deep_nesting_walks_each_level(self):
        doc = "[a]\nx = 1\n[a.b]\ny = 2\n[a.b.c]\nz = 3\n"
        plan = teptris.plan_build(
            [{"name": "a", "rows": ["x", {"name": "b", "rows": ["y", {"name": "c", "rows": ["z"]}]}]}]
        )
        self.assertEqual(
            teptris.plan_walk(plan, doc),
            {"a": {"x": 1, "b": {"y": 2, "c": {"z": 3}}}},
        )


class PlanWalkTest(unittest.TestCase):
    def test_typed_row_results(self):
        out = teptris.plan_walk(teptris.plan_build(spec()), DOC)
        self.assertEqual(out["name"], "svc")
        self.assertEqual(out["port"], 8080)
        self.assertIsInstance(out["port"], int)
        self.assertEqual(out["ratio"], 0.5)
        self.assertIs(out["stable"], True)
        self.assertEqual(out["built"], datetime.date(1979, 5, 27))
        self.assertEqual(
            out["at"], datetime.datetime(1979, 5, 27, 7, 32, 0,
                                         tzinfo=out["at"].tzinfo)
        )
        self.assertEqual(out["seen"], datetime.time(7, 32))
        self.assertEqual(out["hosts"], ["a.example", "b.example"])
        self.assertEqual(out["counts"], [1, 2])
        self.assertEqual(out["empty"], [])

    def test_missing_rows_are_absent_no_nulls(self):
        # rs/ts semantics: missing and type-mismatched rows are
        # ABSENT from the output — never None placeholders
        out = teptris.plan_walk(teptris.plan_build(spec()), DOC)
        self.assertNotIn("absent_key", out)
        self.assertNotIn("absent_table", out)
        self.assertEqual(sorted(out), sorted(
            ["name", "port", "ratio", "stable", "built", "at", "seen",
             "hosts", "counts", "empty", "meta", "items", "extra"]))

    def test_nested_missing_rows_are_absent_inside_their_table(self):
        plan = teptris.plan_build([{"name": "meta", "rows": ["title", "ghost"]}])
        self.assertEqual(teptris.plan_walk(plan, DOC),
                         {"meta": {"title": "plan tier"}})

    def test_type_mismatched_rows_are_absent(self):
        plan = teptris.plan_build([{"name": "meta", "kind": "scalar"}])
        self.assertEqual(teptris.plan_walk(plan, DOC), {})
        plan = teptris.plan_build([{"name": "name", "rows": ["x"]}])
        self.assertEqual(teptris.plan_walk(plan, DOC), {})

    def test_collection_of_tables_walks_the_nested_plan(self):
        out = teptris.plan_walk(teptris.plan_build(spec()), DOC)
        self.assertEqual(out["items"], [{"id": 1, "label": "first"},
                                        {"id": 2, "label": "second"}])

    def test_raw_row_takes_the_untouched_subtree(self):
        out = teptris.plan_walk(teptris.plan_build(spec()), DOC)
        self.assertEqual(out["extra"],
                         {"value": 42, "nested": {"a": 1}})

    def test_one_plan_walks_many_documents(self):
        plan = teptris.plan_build(["name", "port"])
        self.assertEqual(teptris.plan_walk(plan, "name = 'a'\nport = 1\n"),
                         {"name": "a", "port": 1})
        self.assertEqual(teptris.plan_walk(plan, "name = 'b'\n"),
                         {"name": "b"})

    def test_walk_accepts_bytes_like_the_other_entry_points(self):
        plan = teptris.plan_build(["name"])
        self.assertEqual(teptris.plan_walk(plan, b"name = 'x'\n"),
                         {"name": "x"})

    def test_parse_errors_raise_tomldecodeerror_with_position(self):
        plan = teptris.plan_build(["name"])
        try:
            teptris.plan_walk(plan, "name = \n")
            self.fail("broken TOML must raise")
        except teptris.TOMLDecodeError as e:
            self.assertGreater(e.line, 0)
            self.assertGreater(e.column, 0)


if __name__ == "__main__":
    unittest.main()
