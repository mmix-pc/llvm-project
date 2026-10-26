# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import unittest

from ctype_source import CLASSES
from locale_file import encode_locale
from validation_locale import BUILTIN_CATEGORY_KEY, NAME, compose


class ValidationLocaleTest(unittest.TestCase):
    def test_real_category_and_explicit_inheritance(self):
        characters = dict(classes={name: [] for name in CLASSES},
                          maps={name: [] for name in ('toupper', 'tolower', 'totitle')},
                          transliterations={}, default_missing='?')
        gb = dict(tables={'GB2312': dict(decode=[[0x2121, 0x4e2d]],
                                        encode=[[0x4e2d, 0x2121]])})
        cns = dict(tables={name: dict(decode=[], encode=[])
                           for name in ('CNS11643_1', 'CNS11643_2')})
        document = compose(characters, gb, cns)
        self.assertEqual(document['name'], NAME)
        ctype = {item['key']: item for item in document['categories'][0]}
        self.assertEqual(ctype[0x200]['value'], 'ISO-2022-CN')
        self.assertEqual(ctype[0x201]['value'], [0x2121, 0x4e2d])
        self.assertEqual(ctype[0x202]['value'], [0x4e2d, 0x2121])
        self.assertTrue(all(key in ctype for key in range(1, 12)))
        for category in document['categories'][1:]:
            self.assertEqual(category, [dict(key=BUILTIN_CATEGORY_KEY,
                                            kind='string', value='C')])
        self.assertTrue(encode_locale(document).startswith(b'LLVMLCL'))

    def test_incomplete_character_data_is_not_synthesized(self):
        with self.assertRaises(ValueError):
            compose(dict(classes={}, maps={}), {}, {})


if __name__ == '__main__':
    unittest.main()
