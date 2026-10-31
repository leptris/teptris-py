"""The format-preserving edit surface (engine v2)."""

import pytest

import teptris


def test_preserves_everything_but_the_value():
    src = '# c\nversion = "1.2.3"  # t\n[serve]\nport = 8080\n'
    got = teptris.edit(src, "serve.port", "9090")
    assert got == '# c\nversion = "1.2.3"  # t\n[serve]\nport = 9090\n'


def test_multiline_and_inline_targets():
    src = 'm = """\nline\n"""\nz = 1\n'
    assert teptris.edit(src, "m", '"n"') == 'm = "n"\nz = 1\n'
    assert teptris.edit("t = {a = 1}\n", "t", "{a = 2}") == "t = {a = 2}\n"


def test_crlf_is_preserved():
    src = "# c\r\nport = 8080\r\n"
    assert teptris.edit(src, "port", "9090") == "# c\r\nport = 9090\r\n"


def test_missing_key_raises():
    with pytest.raises(ValueError):
        teptris.edit("a = 1\n", "b", "2")


def test_malformed_replacement_raises():
    with pytest.raises(ValueError):
        teptris.edit("a = 1\n", "a", "@@@")
