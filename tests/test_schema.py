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
        # unevaluatedProperties is enforced since engine 0.3.22
        schema = teptris.compile_schema('{"unevaluatedProperties":false}')
        self.assertEqual(len(schema.validate("a = 1\n")), 1)
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

class TaploParityTest(unittest.TestCase):
    """The engine-0.3.21 surface: draft-07 keywords taplo's lens
    cannot reach plus the exact-decimal multipleOf."""

    def test_dependencies_both_forms(self):
        deps = teptris.compile_schema(
            '{"dependencies":{"credit_card":["billing_address"]}}'
        )
        self.assertEqual(len(deps.validate("a = 1\ncredit_card = 1\n")), 1)
        self.assertIs(
            deps.valid("credit_card = 1\nbilling_address = 'x'\n"), True
        )
        schema_form = teptris.compile_schema(
            '{"dependencies":{"credit_card":{"required":["b"]}}}'
        )
        self.assertEqual(len(schema_form.validate("credit_card = 1\n")), 1)

    def test_tuple_items_and_additional_items(self):
        tuple_ = teptris.compile_schema(
            '{"properties":{"t":{"items":[{"type":"string"}],'
            '"additionalItems":false}}}'
        )
        self.assertIs(tuple_.valid("t = ['x']\n"), True)
        self.assertEqual(len(tuple_.validate("t = ['x', 1]\n")), 1)

    def test_multiple_of_is_exact_across_decimals(self):
        mult = teptris.compile_schema(
            '{"properties":{"k":{"multipleOf":0.1}}}'
        )
        self.assertIs(mult.valid("k = 0.3\n"), True)
        self.assertIs(mult.valid("k = 0.35\n"), False)

    def test_formats_taplo_cannot_enforce(self):
        uuid = teptris.compile_schema(
            '{"properties":{"k":{"format":"uuid"}}}'
        )
        self.assertIs(
            uuid.valid("k = '123e4567-e89b-12d3-a456-426614174000'\n"), True
        )
        self.assertIs(uuid.valid("k = 'nope'\n"), False)
        req = teptris.compile_schema(
            '{"properties":{"k":{"format":"semver-requirement"}}}'
        )
        self.assertIs(req.valid("k = '^1.2'\n"), True)
        self.assertIs(req.valid("k = 'not a req'\n"), False)


class SotaSurfaceTest(unittest.TestCase):
    """The engine-0.3.22 surface: the unevaluated* annotation model,
    dynamic refs, contentSchema, and the TOML-native local formats."""

    def test_unevaluated_properties_cousin_rule(self):
        cousin = teptris.compile_schema(
            '{"allOf":[{"properties":{"a":{}}},'
            '{"unevaluatedProperties":false}]}'
        )
        self.assertEqual(len(cousin.validate("a = 1\n")), 1)

        adjacent = teptris.compile_schema(
            '{"properties":{"a":{}},"unevaluatedProperties":false}'
        )
        self.assertIs(adjacent.valid("a = 1\n"), True)
        self.assertIs(adjacent.valid("a = 1\nb = 2\n"), False)

    def test_dynamic_ref_innermost_anchor(self):
        tree = teptris.compile_schema(
            '{"$dynamicAnchor":"node","type":"object","properties":'
            '{"value":{"type":"integer"},"children":{"type":"array",'
            '"items":{"$dynamicRef":"#node"}}}}'
        )
        self.assertIs(tree.valid("value = 1\n[[children]]\nvalue = 2\n"), True)
        self.assertIs(tree.valid("value = 1\n[[children]]\nvalue = 'x'\n"), False)

    def test_content_schema_asserts_decoded_content(self):
        content = teptris.compile_schema(
            '{"properties":{"k":{"contentEncoding":"base64",'
            '"contentMediaType":"application/json",'
            '"contentSchema":{"type":"object","required":["n"]}}}}'
        )
        self.assertIs(content.valid("k = 'eyJuIjogMX0='\n"), True)
        self.assertIs(content.valid("k = 'W10='\n"), False)

    def test_toml_native_local_formats(self):
        local = teptris.compile_schema(
            '{"properties":{"k":{"format":"time-local"}}}'
        )
        self.assertIs(local.valid("k = 07:32:00\n"), True)
        self.assertIs(local.valid("k = '07:32:00Z'\n"), False)


class BuiltInMetaschemaTest(unittest.TestCase):
    """The vendored metaschemas resolve in memory since engine 0.3.23."""

    def test_metaschema_ref_compiles_and_validates(self):
        schema = teptris.compile_schema(
            '{"$ref":"https://json-schema.org/draft/2020-12/schema"}'
        )
        self.assertEqual(len(schema.validate('type = "string"\n')), 0)
        self.assertEqual(len(schema.validate("type = 123\n")), 1)

    def test_draft07_empty_fragment_form(self):
        schema = teptris.compile_schema(
            '{"$ref":"http://json-schema.org/draft-07/schema#"}'
        )
        self.assertEqual(len(schema.validate('description = "d"\n')), 0)

    def test_engine_version(self):
        self.assertEqual(teptris.engine_version(), "0.3.23")
