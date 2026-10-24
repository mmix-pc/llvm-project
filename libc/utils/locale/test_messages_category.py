# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import unittest

from messages_category import FIELDS, messages_records


class MessagesCategoryTest(unittest.TestCase):
    def test_explicit_values(self):
        fields = dict(zip(FIELDS, [['^[eE]'], ['^[hH]'], ['evet'], ['hay\u0131r']]))
        result = messages_records(fields)
        self.assertEqual(result['absent_empty_fields'], [])
        self.assertEqual([r['value'] for r in result['records']],
                         [fields[name][0] for name in FIELDS])
        self.assertEqual([r['key'] for r in result['records']], [1, 2, 3, 4])

    def test_defaults_and_uninterpreted_regex(self):
        # Syntax validation is deliberately not delegated to Python's regex
        # engine, whose grammar and character classes are not the locale ABI.
        result = messages_records({'yesexpr': ['['], 'noexpr': ['']})
        self.assertEqual(result['absent_empty_fields'], ['yesstr', 'nostr'])
        self.assertEqual([r['value'] for r in result['records']], ['[', '', '', ''])

    def test_missing_expression(self):
        for name in ('yesexpr', 'noexpr'):
            fields = {'yesexpr': ['yes'], 'noexpr': ['no']}
            del fields[name]
            with self.assertRaises(ValueError):
                messages_records(fields)

    def test_malformed_fields(self):
        for name in FIELDS:
            for value in ([], ['a', 'b'], 'str', [1], ['\0'], ['\ud800']):
                fields = {'yesexpr': ['yes'], 'noexpr': ['no'], name: value}
                with self.subTest(name=name, value=value), self.assertRaises(ValueError):
                    messages_records(fields)
        with self.assertRaises(ValueError):
            messages_records({'yesexpr': ['yes'], 'noexpr': ['no'], 'unknown': ['']})


if __name__ == '__main__':
    unittest.main()
