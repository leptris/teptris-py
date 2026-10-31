"""The format-preserving edit surface (engine v2)."""

import unittest

import teptris


class EditTest(unittest.TestCase):
    def test_preserves_everything_but_the_value(self):
        src = '# c\nversion = "1.2.3"  # t\n[serve]\nport = 8080\n'
        got = teptris.edit(src, "serve.port", "9090")
        self.assertEqual(got, '# c\nversion = "1.2.3"  # t\n[serve]\nport = 9090\n')

    def test_multiline_and_inline_targets(self):
        src = 'm = """\nline\n"""\nz = 1\n'
        self.assertEqual(teptris.edit(src, "m", '"n"'), 'm = "n"\nz = 1\n')
        self.assertEqual(
            teptris.edit("t = {a = 1}\n", "t", "{a = 2}"), "t = {a = 2}\n"
        )

    def test_crlf_is_preserved(self):
        src = "# c\r\nport = 8080\r\n"
        self.assertEqual(teptris.edit(src, "port", "9090"), "# c\r\nport = 9090\r\n")

    def test_missing_key_raises(self):
        with self.assertRaises(ValueError):
            teptris.edit("a = 1\n", "b", "2")

    def test_malformed_replacement_raises(self):
        with self.assertRaises(ValueError):
            teptris.edit("a = 1\n", "a", "@@@")


if __name__ == "__main__":
    unittest.main()
