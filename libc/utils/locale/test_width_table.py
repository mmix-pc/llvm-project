# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import unittest
from width_table import parse


class WidthTableTest(unittest.TestCase):
    def test_normalize(self):
        self.assertEqual(parse('WIDTH\n<U4E00>...<U9FFF> 2\n<U0300> 0\nEND WIDTH\n'),
                         [(0x300, 0x300, 0), (0x4e00, 0x9fff, 2)])

    def test_reject_invalid(self):
        bodies = ('', '<U0300> 3', '<U0301>...<U0300> 0',
                  '<UD7FF>...<UE000> 1', '<U00110000> 2',
                  '<U0300>...<U036F> 0\n<U036F> 2')
        for body in bodies:
            with self.subTest(body=body), self.assertRaises(ValueError):
                parse('WIDTH\n' + body + '\nEND WIDTH\n')
        with self.assertRaises(ValueError):
            parse('WIDTH\n<U0300> 0\nEND WIDTH\nWIDTH\nEND WIDTH\n')


if __name__ == '__main__':
    unittest.main()
