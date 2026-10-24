# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import unittest

from simple_categories import logical_lines, parse_categories, parse_values


def source(numeric='decimal_point "."'):
    return ('comment_char %\nescape_char /\nLC_NUMERIC\n' + numeric +
            '\nEND LC_NUMERIC\nLC_MONETARY\nint_frac_digits 2\n'
            'END LC_MONETARY\nLC_TIME\nam_pm "";""\nEND LC_TIME\n'
            'LC_MESSAGES\nyesexpr "^[yY]"\nEND LC_MESSAGES\n')


class SimpleCategoryTest(unittest.TestCase):
    def test_literals_and_unicode_symbols(self):
        self.assertEqual(parse_values('"M\u00e4rz";"<U20BA>";"<U0001F600>"', '/'),
                         ['M\u00e4rz', '\u20ba', '\U0001f600'])

    def test_escapes_and_comments(self):
        self.assertEqual(list(logical_lines(
            'comment_char %\nescape_char /\nd_fmt "%m//%d//%Y" % note\n')),
            [('d_fmt "%m//%d//%Y"', '/')])
        self.assertEqual(parse_values('"%m//%d//%Y"', '/'), ['%m/%d/%Y'])
        self.assertEqual(parse_values('"a/"b";"/<x>"', '/'), ['a"b', '<x>'])

    def test_continuation(self):
        lines = list(logical_lines('escape_char /\nam_pm "one";/\n"two"\n'))
        self.assertEqual(lines, [('am_pm "one";"two"', '/')])
        lines = list(logical_lines('escape_char /\nname "one/\ntwo"\n'))
        self.assertEqual(lines, [('name "onetwo"', '/')])

    def test_incomplete_lines(self):
        for text in ('escape_char /\nfoo /', 'foo "bar'):
            with self.assertRaises(ValueError):
                list(logical_lines(text))

    def test_invalid_values(self):
        for text in ('"<UD800>"', '"<U00110000>"', '"<U0000>"',
                     '"<unknown>"', '"a\0b"', '"/\0"', '"open', '1;', '1junk',
                     '2147483648', '"x" "y"'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                parse_values(text, '/')

    def test_selected_categories(self):
        doc = parse_categories(source())
        self.assertEqual(doc['categories']['LC_NUMERIC']['decimal_point'], ['.'])
        self.assertEqual(doc['categories']['LC_TIME']['am_pm'], ['', ''])
        self.assertEqual(doc['categories']['LC_MONETARY']['int_frac_digits'], [2])

    def test_unknown_and_duplicate_fields(self):
        for numeric in ('copy "C"', 'unknown 1', 'grouping "3"',
                        'decimal_point ".";","',
                        'decimal_point "."\ndecimal_point ","'):
            with self.subTest(numeric=numeric), self.assertRaises(ValueError):
                parse_categories(source(numeric))

    def test_structural_errors(self):
        for text in (source().replace('END LC_NUMERIC', 'END LC_TIME'),
                     source().replace('END LC_TIME', ''), source() + source(),
                     'unexpected\n' + source(), 'LC_NUMERIC\nEND LC_NUMERIC\n'):
            with self.assertRaises(ValueError):
                parse_categories(text)

    def test_excluded_categories_are_explicit(self):
        doc = parse_categories(source() + 'LC_CTYPE\ncopy "i18n"\nEND LC_CTYPE\n')
        self.assertEqual(doc['unparsed_categories'], ['LC_CTYPE'])
        self.assertNotIn('LC_CTYPE', doc['categories'])

    def test_late_syntax_directives(self):
        with self.assertRaises(ValueError):
            parse_categories(source() + 'escape_char !\n')


if __name__ == '__main__':
    unittest.main()
