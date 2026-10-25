# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import hashlib
from pathlib import Path
import tempfile
import unittest

from builtin_transliteration import generate


class BuiltinTransliterationTest(unittest.TestCase):
    def test_order_notices_and_reproducibility(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, replacement in (('translit_neutral', '0041'),
                                      ('translit_combining', '0042')):
                (root / name).write_text(
                    'comment_char %\n% Original data notice.\nLC_CTYPE\ntranslit_start\n'
                    '<U00C0> <U' + replacement + '>\ntranslit_end\nEND LC_CTYPE\n')
            text, manifest = generate(root)
            data = bytes.fromhex(''.join(
                line.strip('"').replace('\\x', '')
                for line in text.splitlines() if line.startswith('"')))
            self.assertEqual(manifest['rules'], 1)
            self.assertEqual(manifest['sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(manifest['bytes'], len(data))
            self.assertEqual(data[-4:], b'\0\0\0B')
            for item in manifest['sources'].values():
                self.assertIn('Original data notice.', item['preamble'])
            self.assertEqual((text, manifest), generate(root))

    def test_reject_character_tables(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'translit_neutral').write_text(
                'LC_CTYPE\nupper <U0041>\nEND LC_CTYPE\n')
            with self.assertRaisesRegex(ValueError, 'character tables'):
                generate(root)


if __name__ == '__main__':
    unittest.main()
