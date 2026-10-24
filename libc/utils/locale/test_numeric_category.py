# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import unittest

from numeric_category import numeric_records


def fields(grouping=None):
    return {"decimal_point": ["."], "thousands_sep": [","],
            "grouping": [3, 3] if grouping is None else grouping}


class NumericCategoryTest(unittest.TestCase):
    def test_required_fields(self):
        for name in fields():
            value = fields()
            del value[name]
            with self.assertRaises(ValueError):
                numeric_records(value)
        with self.assertRaises(ValueError):
            numeric_records(dict(fields(), unknown=[1]))

    def test_single_scalar(self):
        for name in ("decimal_point", "thousands_sep"):
            for invalid in ([], ["a", "b"], [1], ["ab"], ["\0"], ["\ud800"]):
                value = fields()
                value[name] = invalid
                with self.assertRaises(ValueError):
                    numeric_records(value)
        value = fields()
        value["decimal_point"] = ["\u066b"]
        value["thousands_sep"] = ["\u202f"]
        self.assertEqual(numeric_records(value)[1]["value"], "\u202f")
        value["thousands_sep"] = [""]
        numeric_records(value)
        value["decimal_point"] = [""]
        with self.assertRaises(ValueError):
            numeric_records(value)

    def test_grouping_representation(self):
        for source, expected in (([3, 3], [3, 3, 0]), ([3, 2], [3, 2, 0]),
                                 ([-1], [127, 0]), ([0], [127, 0]),
                                 ([3, -1], [3, 127, 0]), ([3, 0], [3, 127, 0])):
            self.assertEqual(numeric_records(fields(source))[2]["value"], expected)

    def test_invalid_grouping(self):
        for invalid in ([], [True], [127], [-2], [0, 3], [-1, 3], ["3"]):
            with self.subTest(grouping=invalid), self.assertRaises(ValueError):
                numeric_records(fields(invalid))


if __name__ == "__main__":
    unittest.main()
