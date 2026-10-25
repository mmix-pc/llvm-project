# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from pathlib import Path
import tempfile
import unittest

from collation_source import SourceReader, directions, symbolic_range, weights


BASE = '''script <LATIN>
collating-symbol <base>
collating-symbol <accent>
collating-symbol <first>
collating-symbol <second>
collating-element <pair> from "<U0061><U0062>"
<base>
<accent>
<first>
<second>
order_start <LATIN>;forward;backward;forward;forward,position
<U0061> <first>;<base>;IGNORE;<U0061>
<pair> <second>;<accent>;IGNORE;<pair>
<U0062> "<first><second>";<base>;IGNORE;<U0062>
order_end'''


class CollationSourceTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def source(self, name, body):
        (self.root / name).write_text('comment_char %\nescape_char /\n'
                                     'LC_COLLATE\n' + body + '\nEND LC_COLLATE\n',
                                     encoding='utf-8')

    def test_contractions_expansions_and_directions(self):
        self.source('base', BASE)
        reader = SourceReader(self.root)
        result = reader.read('base')
        a, pair, b = result['records']
        self.assertEqual([a['source'], pair['source'], b['source']],
                         [[97], [97, 98], [98]])
        self.assertEqual(a['weights'], [[3], [1], [], [5]])
        self.assertEqual(pair['weights'], [[4], [2], [], [6]])
        self.assertEqual(b['weights'], [[3, 4], [1], [], [7]])
        self.assertEqual(a['directions'], [1, 2, 1, 5])
        self.assertEqual(result['undefined'], 'shared-primary-codepoint-tail')
        self.assertEqual(len(result['warnings']), 1)
        self.assertEqual(len(reader.files['base']['sha256']), 64)

    def test_copy_tailoring_moves_and_overrides(self):
        self.source('base', BASE)
        self.source('regional', 'copy "base"\nreorder-after <base>\n'
                    '<second>\n<first>\n<U0061> <second>;<base>;IGNORE;<U0061>\n'
                    'reorder-end')
        reader = SourceReader(self.root)
        result = reader.read('regional')
        self.assertEqual(result['records'][0]['weights'], [[2], [1], [], [4]])
        self.assertEqual(reader.read('base')['records'][0]['weights'][0], [3])
        self.assertEqual(reader.edges, [{'source': 'regional', 'target': 'base'}])
        self.assertEqual(len(result['tailorings'][0]['entries']), 3)

    def test_ranges_and_unicode_spelling(self):
        self.assertEqual(symbolic_range('<S0009>..<S000B>'),
                         ['<S0009>', '<S000A>', '<S000B>'])
        self.source('range', 'script <HAN>\n'
                    'order_start <HAN>;forward;forward;forward;forward,position\n'
                    '<U4E00> <U00004E00>;IGNORE;IGNORE;IGNORE\n'
                    '.. ..;IGNORE;IGNORE;IGNORE\n'
                    '<U4E02> <U4E02>;IGNORE;IGNORE;IGNORE\norder_end')
        result = SourceReader(self.root).read('range')
        self.assertEqual([r['source'] for r in result['records']],
                         [[0x4e00], [0x4e01], [0x4e02]])
        self.assertEqual([r['weights'][0] for r in result['records']], [[1], [2], [3]])

    def test_selected_conditional(self):
        body = BASE.replace('order_start <LATIN>;forward;backward;forward;forward,position',
                            'ifdef DIACRIT_BACKWARD\n'
                            'order_start <LATIN>;forward;backward;forward;forward,position\n'
                            'else\n'
                            'order_start <LATIN>;forward;forward;forward;forward,position\n'
                            'endif')
        self.source('base', body)
        self.assertEqual(SourceReader(self.root).read('base')['records'][0]['directions'],
                         [1, 1, 1, 5])

    def test_invalid_weights_and_directions(self):
        for text in ('"<x>junk"', '""', '<x>;<x>;<x>;<x>;<x>', '<bad',
                     '"' + '<x>' * 257 + '"'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                weights(text, '<x>')
        for text in ('forward', 'forward;forward;forward;position',
                     'forward;forward;forward;forward,forward'):
            with self.assertRaises(ValueError):
                directions(text)

    def test_invalid_structures(self):
        for body in ('unknown directive', 'order_end', 'reorder-end',
                     'else', 'endif', 'ifdef OTHER\nendif',
                     'ifdef DIACRIT_BACKWARD', 'copy "missing";"extra"',
                     BASE + '\nreorder-after <absent>\nreorder-end',
                     BASE.replace('order_end', '.. ..;IGNORE;IGNORE;IGNORE\norder_end'),
                     BASE.replace('<U0061> <first>', '<U0061> <missing>'),
                     BASE.replace('order_end', '<U0061> <first>\norder_end'),
                     BASE.replace('order_end', ''),
                     'collating-symbol <x>\ncollating-symbol <x>',
                     'collating-symbol <S000B>..<S0009>',
                     'collating-symbol <S0000>..<SFFFFFF>'):
            self.source('bad', body)
            with self.subTest(body=body), self.assertRaises(ValueError):
                SourceReader(self.root).read('bad')

    def test_dependency_cycles_and_escape(self):
        self.source('a', 'copy "b"')
        self.source('b', 'copy "a"')
        with self.assertRaises(ValueError):
            SourceReader(self.root).read('a')
        for name in ('..', '../escape', '/absolute'):
            with self.assertRaises(ValueError):
                SourceReader(self.root).read(name)
        with self.assertRaises(FileNotFoundError):
            SourceReader(self.root).read('missing')


if __name__ == '__main__':
    unittest.main()
