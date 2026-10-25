# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import copy
import unittest
from unittest.mock import patch

from collation_category import collation_records


class CollationCategoryTest(unittest.TestCase):
    def category(self):
        return dict(weight_count=7, undefined='shared-primary-codepoint-tail',
                    undefined_directions=[1, 1, 1, 5], records=[
            dict(source=[97], directions=[1, 2, 1, 5], weights=[[3], [1], [], [5]]),
            dict(source=[97, 98], directions=[1, 2, 1, 5], weights=[[4], [2], [], [6]]),
            dict(source=[98], directions=[1, 2, 1, 5], weights=[[3, 4], [1], [], [7]])])

    def test_records(self):
        data = self.category()
        rows = collation_records(data)
        self.assertEqual(rows[0], dict(key=1, kind='words', value=[3, 7, 1, 1, 1, 1, 5]))
        self.assertEqual(rows[1]['value'], [1, 97, 1, 2, 1, 5, 1, 3, 1, 1, 0, 1, 5])
        self.assertEqual(rows[2]['key'], 0x1001)
        data['records'].reverse()
        self.assertEqual(collation_records(data), rows)

    def test_invalid_domains(self):
        for field, values in (
                ('source', [[], [0xd800], [0x110000], [-1], [True], [97] * 257]),
                ('directions', [[1], [1, 2, 3, 4], [1, 2, 1, True]]),
                ('weights', [[], [[0], [], [], []], [[8], [], [], []],
                             [[True], [], [], []], [[1] * 257, [], [], []]])):
            for value in values:
                data = self.category()
                data['records'][0][field] = value
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    collation_records(data)

    def test_duplicates_and_bounds(self):
        data = self.category()
        data['records'].append(copy.deepcopy(data['records'][0]))
        with self.assertRaises(ValueError):
            collation_records(data)
        for field in ('MAX_RULES', 'MAX_WORDS'):
            with patch('collation_category.' + field, 1), self.assertRaises(ValueError):
                collation_records(self.category())
        for count in (-1, True, 262145):
            data = self.category()
            data['weight_count'] = count
            with self.assertRaises(ValueError):
                collation_records(data)

    def test_no_unreviewed_default(self):
        data = self.category()
        data['undefined'] = 'unicode-order'
        with self.assertRaises(ValueError):
            collation_records(data)


if __name__ == '__main__':
    unittest.main()
