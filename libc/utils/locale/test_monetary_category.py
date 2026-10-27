# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import unittest

from monetary_category import INTEGERS, STRINGS, monetary_records


def fields():
    result = {name: [''] for name in STRINGS}
    result.update({name: [-1] for name in INTEGERS[:8]})
    result['mon_grouping'] = [-1]
    return result


class MonetaryCategoryTest(unittest.TestCase):
    def test_inheritance_and_explicit_values(self):
        value = fields()
        value['p_sep_by_space'] = [0]
        value['int_p_sep_by_space'] = [1]
        result = monetary_records(value)
        self.assertEqual(len(result['records']), 21)
        self.assertEqual(result['records'][16]['value'], [1])
        self.assertNotIn('int_p_sep_by_space', result['inherited_fields'])
        self.assertEqual(result['inherited_fields']['int_n_sign_posn'], 'n_sign_posn')
        self.assertEqual(result['records'][-1]['value'], [127])

    def test_missing_and_unknown_fields(self):
        for name in fields():
            value = fields()
            del value[name]
            with self.assertRaises(ValueError):
                monetary_records(value)
        with self.assertRaises(ValueError):
            monetary_records(dict(fields(), unknown=[1]))

    def test_number_ranges(self):
        for name, number in [('frac_digits', 127), ('p_cs_precedes', 2),
                             ('p_sep_by_space', 3), ('p_sign_posn', 5),
                             ('int_frac_digits', -2), ('n_sign_posn', True)]:
            value = fields()
            value[name] = [number]
            with self.subTest(name=name), self.assertRaises(ValueError):
                monetary_records(value)

    def test_strings(self):
        value = fields()
        value['currency_symbol'] = ['\u20ba']
        value['int_curr_symbol'] = ['TRY ']
        self.assertEqual(monetary_records(value)['records'][1]['value'], '\u20ba')
        for name, text in [('int_curr_symbol', 'USD'), ('int_curr_symbol', 'usd '),
                           ('mon_decimal_point', '..'), ('negative_sign', '\0'),
                           ('currency_symbol', '\ud800')]:
            value = fields()
            value[name] = [text]
            with self.subTest(name=name), self.assertRaises(ValueError):
                monetary_records(value)

    def test_sign_combinations(self):
        for prefix in ('', 'int_'):
            for positive in range(-1, 5):
                for negative in range(-1, 5):
                    for signs in (('', ''), ('same', 'same'), ('', '-'), ('+', '-')):
                        value = fields()
                        value['positive_sign'] = [signs[0]]
                        value['negative_sign'] = [signs[1]]
                        value[prefix + 'p_sign_posn'] = [positive]
                        value[prefix + 'n_sign_posn'] = [negative]
                        invalid = (positive == negative == 0 or
                                   positive > 0 and negative > 0 and signs[0] == signs[1])
                        with self.subTest(prefix=prefix, positive=positive,
                                          negative=negative, signs=signs):
                            if invalid:
                                with self.assertRaises(ValueError):
                                    monetary_records(value)
                            else:
                                self.assertEqual(len(monetary_records(value)['records']), 21)

    def test_international_signs_checked_independently(self):
        value = fields()
        value.update(p_sign_posn=[0], n_sign_posn=[1],
                     int_p_sign_posn=[1], int_n_sign_posn=[0])
        monetary_records(value)
        value['int_n_sign_posn'] = [1]
        with self.assertRaisesRegex(ValueError, 'indistinguishable'):
            monetary_records(value)
        value['negative_sign'] = ['-']
        monetary_records(value)
        value['p_sign_posn'] = value['n_sign_posn'] = [0]
        with self.assertRaisesRegex(ValueError, 'parentheses'):
            monetary_records(value)


if __name__ == '__main__':
    unittest.main()
