"""The schema surface: compile-once/validate-many over the engine's
JSON Schema 2020-12 practical subset. Semantics are owned by the
engine's unit suite; this file keeps the binding honest."""

import json
import unittest

import teptris

MANIFEST = json.dumps(
    {
        "type": "object",
        "properties": {
            "name": {"type": "string", "minLength": 1},
            "port": {"type": "integer", "minimum": 1},
        },
        "required": ["name", "port"],
    }
)


class SchemaTest(unittest.TestCase):
    def test_compile_and_validate(self):
        schema = teptris.compile_schema(MANIFEST)
        self.assertEqual(schema.validate("name = 'srv'\nport = 8080\n"), [])
        self.assertIs(schema.valid("name = 'srv'\nport = 8080\n"), True)

    def test_violations_carry_path_schema_path_message(self):
        schema = teptris.compile_schema(MANIFEST)
        bad = schema.validate("name = ''\nport = 0\n")
        self.assertEqual(len(bad), 2)
        self.assertEqual(bad[0]["path"], "name")
        self.assertEqual(bad[0]["schema_path"], "#/properties.name")
        self.assertIn("minLength", bad[0]["message"])
        self.assertEqual(bad[1]["path"], "port")
        missing = schema.validate("port = 1\n")
        self.assertEqual(len(missing), 1)
        self.assertEqual(missing[0]["path"], "")
        self.assertIn("name", missing[0]["message"])
        self.assertIs(schema.valid("port = 1\n"), False)

    def test_unsupported_constructs_named_at_compile(self):
        with self.assertRaises(ValueError, msg="unevaluatedProperties"):
            teptris.compile_schema('{"unevaluatedProperties":false}')
        try:
            teptris.compile_schema('{"$ref":"https://x.example/s.json"}')
            self.fail("remote $ref must be rejected")
        except ValueError as e:
            self.assertIn("remote", str(e))
        try:
            teptris.compile_schema('{"type":}')
            self.fail("broken JSON must be rejected")
        except ValueError as e:
            self.assertIn("error", str(e))

    def test_unknown_keywords_are_extensions(self):
        schema = teptris.compile_schema(
            '{"x-vendor":true,"properties":{"k":{"type":"integer"}}}'
        )
        self.assertIs(schema.valid("k = 1\n"), True)
        self.assertIs(schema.valid("k = 's'\n"), False)

    def test_the_four_policies(self):
        strict = teptris.compile_schema('{"properties":{"k":{"enum":[1]}}}')
        self.assertIs(strict.valid("k = 1\n"), True)
        self.assertIs(strict.valid("k = 1.0\n"), False)  # float never integer

        dt = teptris.compile_schema(
            '{"properties":{"k":{"type":"string","format":"date-time"}}}'
        )
        self.assertIs(dt.valid("k = 2026-01-02T03:04:05Z\n"), True)
        self.assertIs(dt.valid("k = 'soon'\n"), False)

        lo = teptris.compile_schema('{"properties":{"k":{"minimum":0}}}')
        self.assertIs(lo.valid("k = nan\n"), False)
        self.assertIs(lo.valid("k = inf\n"), True)

        opt = teptris.compile_schema(
            '{"properties":{"k":{"type":["string",null]}}}'
        )
        self.assertIs(opt.valid("k = 's'\n"), True)
        self.assertIs(opt.valid("j = 1\n"), True)
        self.assertIs(opt.valid("k = 1\n"), False)

    def test_recursive_refs(self):
        tree = teptris.compile_schema(
            '{"properties":{"v":{"type":"integer"},'
            '"kids":{"type":"array","items":{"$ref":"#"}}},"required":["v"]}'
        )
        self.assertIs(
            tree.valid("v = 0\n[[kids]]\nv = 1\n[[kids.kids]]\nv = 2\n"), True
        )
        bad = tree.validate("v = 0\n[[kids]]\nv = 'x'\n")
        self.assertEqual(len(bad), 1)
        self.assertEqual(bad[0]["path"], "kids.0.v")

    def test_parse_failures_raise_tomldecodeerror(self):
        schema = teptris.compile_schema("{}")
        with self.assertRaises(teptris.TOMLDecodeError):
            schema.validate("a = = 1\n")

    def test_bytes_input(self):
        schema = teptris.compile_schema(MANIFEST)
        self.assertIs(schema.valid(b"name = 'a'\nport = 1\n"), True)


if __name__ == "__main__":
    unittest.main()
