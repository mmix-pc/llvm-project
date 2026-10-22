# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from hdrgen.header import HeaderFile
from hdrgen.yaml_to_classes import yaml_to_classes


def header(**fields):
    return yaml_to_classes({"header": "example.h", **fields}, HeaderFile)


def function(guard, result="int"):
    return {"name": "example", "return_type": result,
            "arguments": [], "guard": guard}


class GuardTests(unittest.TestCase):
    def test_macro_import_and_definition(self):
        h = header(macros=[
            {"macro_name": "EXT_VALUES", "macro_header": "extensions.h",
             "guard": "ENABLE_EXT"},
            {"macro_name": "VALUE", "macro_value": 7, "guard": "ENABLE_EXT"},
        ])
        text = h.public_api()
        self.assertIn('#ifdef ENABLE_EXT\n#include "llvm-libc-macros/extensions.h"', text)
        self.assertIn('#ifdef ENABLE_EXT\n\n#define VALUE 7', text)
        self.assertIn("llvm-libc-macros/extensions.h", h.json_data()["includes"])

    def test_unconditional_import_dominates(self):
        h = header(macros=[
            {"macro_name": "A", "macro_header": "values.h", "guard": "EXT"},
            {"macro_name": "B", "macro_header": "values.h"},
        ])
        self.assertEqual(h.public_api().count('"llvm-libc-macros/values.h"'), 1)
        self.assertNotIn("#ifdef EXT", h.public_api())

    def test_explicit_type_guard_survives_inference(self):
        h = header(
            macros=[{"macro_name": "EXT", "macro_header": "features.h"}],
            types=[{"type_name": "example_t", "guard": "EXT"}],
            functions=[function("EXT", "example_t")],
        )
        text = h.public_api()
        self.assertEqual(text.count('"llvm-libc-types/example_t.h"'), 1)
        self.assertIn('#ifdef EXT\n#include "llvm-libc-types/example_t.h"', text)
        self.assertIn("llvm-libc-types/example_t.h", h.json_data()["includes"])

    def test_guarded_compiler_header(self):
        h = header(
            macros=[{"macro_name": "EXT", "macro_header": "features.h"}],
            types=[{"type_name": "uint8_t", "guard": "EXT"}],
        )
        self.assertIn("#ifdef EXT\n#include <stdint.h>", h.public_api())
        self.assertNotIn('"<stdint.h>"', h.public_api())

    def test_merge_keeps_alternate_declarations(self):
        h = header(functions=[function("API_A")])
        h.merge(header(functions=[function("API_B", "long")]))
        h.validate()
        self.assertIn("#ifdef API_A\nint example(void)", h.public_api())
        self.assertIn("#ifdef API_B\nlong example(void)", h.public_api())

    def test_merge_deduplicates_identical_declarations(self):
        h = header(functions=[function("API_A")])
        h.merge(header(functions=[function("API_A")]))
        self.assertEqual(h.public_api().count("example(void)"), 1)

    def test_conflicting_duplicate_rejected(self):
        with self.assertRaisesRegex(ValueError, "Conflicting declarations"):
            header(functions=[function("API_A")]).merge(
                header(functions=[function("API_A", "long")]))
        with self.assertRaisesRegex(ValueError, "Conflicting declarations"):
            header(functions=[function(None), function(None, "long")]).validate()

    def test_invalid_guards_rejected(self):
        for guard in (True, 1, "", "A || B", "A\n#error injected"):
            for fields in (
                {"functions": [function(guard)]},
                {"macros": [{"macro_name": "X", "guard": guard}]},
            ):
                with self.subTest(guard=guard, fields=fields):
                    with self.assertRaisesRegex(ValueError, "preprocessor identifier"):
                        header(**fields)

    def test_enum_order_unchanged_by_validation(self):
        h = header(enums=[{"name": "Z"}, {"name": "A"}])
        before = h.public_api()
        h.validate()
        self.assertEqual(before, h.public_api())

    def test_visibility_policy(self):
        h = header(types=[{"type_name": "example_t"}],
                   objects=[{"object_name": "value", "object_type": "int"}])
        h.apply_visibility({"types": {"example_t": "EXT"},
                            "objects": {"value": "EXT"}})
        self.assertIn('#ifdef EXT\n#include "llvm-libc-types/example_t.h"', h.public_api())
        self.assertIn('#ifdef EXT\nextern int value;', h.public_api())

    def test_visibility_policy_rejects_mistakes(self):
        h = header(functions=[function("EXISTING")])
        for policy in ([], {"functions": {"typo": "EXT"}},
                       {"functions": {"example": "EXT"}},
                       {"functions": []}, {"typo": {}},
                       {"functions": {"example": "A || B"}}):
            with self.subTest(policy=policy):
                with self.assertRaises(ValueError):
                    h.apply_visibility(policy)


if __name__ == "__main__":
    unittest.main()
