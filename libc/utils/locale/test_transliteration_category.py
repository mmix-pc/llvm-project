# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import unittest
from unittest.mock import patch

from transliteration_category import transliteration_records


class TransliterationCategoryTest(unittest.TestCase):
    def records(self, rules):
        return transliteration_records(dict(transliterations=rules))

    def test_ordered_alternatives_and_deletion(self):
        rows = self.records({'\u00e4': ['ae', 'a'], '\u0301': ['']})
        self.assertEqual(rows[0]['value'], [2])
        self.assertEqual(rows[1], dict(key=0x10000, kind='words',
                                      value=[1, 0xe4, 2, 2, 97, 101, 1, 97]))
        self.assertEqual(rows[2]['value'], [1, 0x301, 1, 0])

    def test_deterministic_scalar_keys_and_multi_character_input(self):
        rules = {'a\u0301': ['\u00e1'], 'a': ['A'], '\U00020000': ['?']}
        rows = self.records(rules)
        self.assertEqual(rows, self.records(dict(reversed(list(rules.items())))))
        self.assertEqual(rows[2]['value'], [2, 97, 0x301, 1, 1, 0xe1])
        self.assertEqual(rows[3]['value'], [1, 0x20000, 1, 1, 63])

    def test_invalid_sequences(self):
        for rules in ({'': ['a']}, {'a': []}, {'a': 'x'}, {'\0': ['a']},
                      {'a': ['\0']}, {'\ud800': ['a']}, {'a': ['\udfff']},
                      {'a' * 257: ['x']}, {'a': ['x' * 257]},
                      {'a': ['x'] * 257}):
            with self.subTest(rules=rules), self.assertRaises(ValueError):
                self.records(rules)

    def test_resource_bounds(self):
        with patch('transliteration_category.MAX_WORDS', 4):
            with self.assertRaises(ValueError):
                self.records({'a': ['b']})
        with patch('transliteration_category.MAX_RULES', 1):
            with self.assertRaises(ValueError):
                self.records({'a': ['b'], 'c': ['d']})

    def test_empty_repertoire_and_separate_fallback(self):
        self.assertEqual(self.records({}), [dict(key=0x300, kind='words', value=[0])])
        category = dict(transliterations={'a': ['b']}, default_missing='ignored')
        self.assertEqual(transliteration_records(category), self.records({'a': ['b']}))


if __name__ == '__main__':
    unittest.main()
