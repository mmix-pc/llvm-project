# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import copy
import unittest

from time_category import COUNTS, FIELDS, time_records


def source():
    return {name: [name + str(i) for i in range(count)]
            for name, count in zip(FIELDS[:11], COUNTS[:11])
            if name not in ('alt_mon', 'ab_alt_mon')}


class TimeCategoryTest(unittest.TestCase):
    def test_tables_and_defaults(self):
        fields = source()
        fields['am_pm'] = ['', '']
        fields['t_fmt_ampm'] = ['']
        result = time_records(fields)
        values = {row['key']: row['value'] for row in result['records']}
        self.assertEqual(values[1 << 16], [7])
        self.assertEqual(values[(5 << 16) + 1], 'mon0')
        self.assertEqual(values[(6 << 16) + 12], 'abmon11')
        self.assertEqual(values[(7 << 16) + 2], '')
        self.assertEqual(values[12 << 16], [0])
        self.assertEqual(values[16 << 16], [0])
        self.assertEqual(result['inherited_fields'],
                         {'alt_mon': 'mon', 'ab_alt_mon': 'abmon'})
        self.assertEqual(len(result['absent_empty_fields']), 5)

    def test_explicit_values_and_extensions(self):
        fields = source()
        fields['alt_mon'] = ['\u015eubat'] * 12
        fields['era'] = ['+:1:2000/01/01:+*:Example:%EC %Ey']
        fields['alt_digits'] = ['zero', 'one']
        fields['week'] = [7, 19971130, 4]
        original = copy.deepcopy(fields)
        result = time_records(fields)
        values = {row['key']: row['value'] for row in result['records']}
        self.assertEqual(values[(5 << 16) + 1], '\u015eubat')
        self.assertEqual(values[12 << 16], [1])
        self.assertEqual(values[(12 << 16) + 1], fields['era'][0])
        self.assertEqual(result['unpublished_extensions'], {'week': fields['week']})
        self.assertEqual(fields, original)
        self.assertNotIn('alt_mon', result['inherited_fields'])

    def test_missing_and_malformed_fields(self):
        for name in source():
            fields = source()
            del fields[name]
            with self.subTest(missing=name), self.assertRaises(ValueError):
                time_records(fields)
        for bad in ([], ['short'], [None] * 7, ['\ud800'] * 7,
                    ['\0'] * 7, [''] * 7):
            fields = source()
            fields['day'] = bad
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                time_records(fields)
        fields = source()
        fields['unknown'] = ['x']
        with self.assertRaises(ValueError):
            time_records(fields)

    def test_variable_tables_are_bounded(self):
        for field, maximum in [('era', 1024), ('alt_digits', 100)]:
            fields = source()
            fields[field] = ['x'] * maximum
            time_records(fields)
            fields[field].append('x')
            with self.assertRaises(ValueError):
                time_records(fields)


if __name__ == '__main__':
    unittest.main()
