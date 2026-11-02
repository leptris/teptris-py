"""The schema surface: compile-once/validate-many over the engine's
JSON Schema 2020-12 practical subset. Semantics are owned by the
engine's unit suite; this file keeps the binding honest."""

import json

import pytest

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


def test_compile_and_validate():
    schema = teptris.compile_schema(MANIFEST)
    assert schema.validate("name = 'srv'\nport = 8080\n") == []
    assert schema.valid("name = 'srv'\nport = 8080\n") is True


def test_violations_carry_path_schema_path_message():
    schema = teptris.compile_schema(MANIFEST)
    bad = schema.validate("name = ''\nport = 0\n")
    assert len(bad) == 2
    assert bad[0]["path"] == "name"
    assert bad[0]["schema_path"] == "#/properties.name"
    assert "minLength" in bad[0]["message"]
    assert bad[1]["path"] == "port"
    missing = schema.validate("port = 1\n")
    assert len(missing) == 1
    assert missing[0]["path"] == ""
    assert "name" in missing[0]["message"]
    assert schema.valid("port = 1\n") is False


def test_unsupported_constructs_named_at_compile():
    with pytest.raises(ValueError, match="unevaluatedProperties"):
        teptris.compile_schema('{"unevaluatedProperties":false}')
    with pytest.raises(ValueError, match="remote"):
        teptris.compile_schema('{"$ref":"https://x.example/s.json"}')
    with pytest.raises(ValueError, match="error"):
        teptris.compile_schema('{"type":}')


def test_unknown_keywords_are_extensions():
    schema = teptris.compile_schema(
        '{"x-vendor":true,"properties":{"k":{"type":"integer"}}}'
    )
    assert schema.valid("k = 1\n") is True
    assert schema.valid("k = 's'\n") is False


def test_the_four_policies():
    strict = teptris.compile_schema('{"properties":{"k":{"enum":[1]}}}')
    assert strict.valid("k = 1\n") is True
    assert strict.valid("k = 1.0\n") is False  # float never integer

    dt = teptris.compile_schema(
        '{"properties":{"k":{"type":"string","format":"date-time"}}}'
    )
    assert dt.valid("k = 2026-01-02T03:04:05Z\n") is True
    assert dt.valid("k = 'soon'\n") is False

    lo = teptris.compile_schema('{"properties":{"k":{"minimum":0}}}')
    assert lo.valid("k = nan\n") is False
    assert lo.valid("k = inf\n") is True

    opt = teptris.compile_schema(
        '{"properties":{"k":{"type":["string",null]}}}'
    )
    assert opt.valid("k = 's'\n") is True
    assert opt.valid("j = 1\n") is True
    assert opt.valid("k = 1\n") is False


def test_recursive_refs():
    tree = teptris.compile_schema(
        '{"properties":{"v":{"type":"integer"},'
        '"kids":{"type":"array","items":{"$ref":"#"}}},"required":["v"]}'
    )
    assert tree.valid("v = 0\n[[kids]]\nv = 1\n[[kids.kids]]\nv = 2\n")
    bad = tree.validate("v = 0\n[[kids]]\nv = 'x'\n")
    assert len(bad) == 1
    assert bad[0]["path"] == "kids.0.v"


def test_parse_failures_raise_tomldecodeerror():
    schema = teptris.compile_schema("{}")
    with pytest.raises(teptris.TOMLDecodeError):
        schema.validate("a = = 1\n")


def test_bytes_input():
    schema = teptris.compile_schema(MANIFEST)
    assert schema.valid(b"name = 'a'\nport = 1\n") is True
