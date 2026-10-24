# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import copy
import json
import struct
import unittest

from locale_file import encode_locale, unique_object


def fixture():
    return {"name": "sample", "categories": [
        [{"key": 10, "kind": "string", "value": "hello"},
         {"key": 20, "kind": "words", "value": [0x12345678, category]},
         {"key": 30, "kind": "bytes", "value": [0, 255, 128]}]
        for category in range(6)]}


class LocaleFileTest(unittest.TestCase):
    def test_fixed_endian_layout(self):
        data = encode_locale(fixture())
        self.assertEqual(len(data), 525)
        self.assertEqual(data[:8], b"LLVMLCL\0")
        self.assertEqual(struct.unpack_from(">6I", data, 8),
                         (1, 0x01020304, 525, 6, 128, 7))
        self.assertEqual(data[128:135], b"sample\0")
        for category in range(6):
            offset = 135 + 65 * category
            self.assertEqual(struct.unpack_from(">4I", data, 32 + 16 * category),
                             (category, offset, 65, 3))
            self.assertEqual(data[offset:offset + 16],
                             b"\0\0\0\x0a\0\0\0\x01\0\0\0\x30\0\0\0\x06")
            self.assertEqual(data[offset + 48:offset + 54], b"hello\0")
            self.assertEqual(data[offset + 54:offset + 58], b"\x12\x34\x56\x78")

    def test_reproducible_key_order(self):
        doc = fixture()
        reordered = copy.deepcopy(doc)
        for category in reordered["categories"]:
            category.reverse()
        self.assertEqual(encode_locale(doc), encode_locale(reordered))

    def test_invalid_name(self):
        for name in ("", ".", "..", "a/b", "a\\b", "a\0b", "a" * 256, "\u00e9"):
            doc = fixture()
            doc["name"] = name
            with self.subTest(name=name), self.assertRaises(ValueError):
                encode_locale(doc)

    def test_duplicate_or_invalid_key(self):
        for key in (0, -1, 20, True, 0x100000000):
            doc = fixture()
            doc["categories"][0][0]["key"] = key
            with self.subTest(key=key), self.assertRaises(ValueError):
                encode_locale(doc)

    def test_invalid_payloads(self):
        for kind, value in (("string", "a\0b"), ("words", [-1]),
                            ("words", [True]), ("words", [0x100000000]),
                            ("bytes", [256]), ("bytes", [-1]),
                            ("reference", [0, 1]), ("unknown", [])):
            doc = fixture()
            doc["categories"][0][0].update(kind=kind, value=value)
            with self.subTest(kind=kind, value=value), self.assertRaises(ValueError):
                encode_locale(doc)

    def test_references(self):
        doc = fixture()
        doc["categories"][5][2].update(kind="reference", value=[0, 10, "string"])
        data = encode_locale(doc)
        self.assertEqual(len(data), 534)
        self.assertEqual(struct.unpack_from(">3I", data, len(data) - 12),
                         (0, 10, 1))
        for value in ([6, 10, "string"], [0, 11, "string"], [0, 10, "words"],
                      [5, 30, "string"], [5, 30, "reference"]):
            doc["categories"][5][2]["value"] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                encode_locale(doc)

    def test_unknown_schema_and_duplicate_json(self):
        for doc in ({}, {"name": "C", "categories": []},
                    dict(fixture(), extra=1)):
            with self.assertRaises(ValueError):
                encode_locale(doc)
        with self.assertRaises(ValueError):
            json.loads('{"name":"C","name":"other"}', object_pairs_hook=unique_object)


if __name__ == "__main__":
    unittest.main()
