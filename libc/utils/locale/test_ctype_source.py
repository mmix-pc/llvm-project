# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from pathlib import Path
import tempfile
import unittest

from ctype_source import (CLASSES, SourceReader, alternatives, atom,
                          character_records, intervals, pairs)


class CTypeSourceTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def source(self, name, body):
        (self.root / name).write_text('comment_char %\nescape_char /\n'
                                     'LC_CTYPE\n' + body + '\nEND LC_CTYPE\n',
                                     encoding='utf-8')

    def test_intervals_and_mapping(self):
        self.assertEqual(intervals('<U0042>;<U0041>..<U0043>;<U0044>'), [[65, 68]])
        self.assertEqual(pairs('(<U0069>,<U0130>);(<U0049>,<U0131>)'),
                         [[73, 305], [105, 304]])

    def test_invalid_scalar_ranges(self):
        for text in ('<UD800>', '<U00110000>', '<UD7FF>..<UE000>',
                     '<U0042>..<U0041>', '<U0041>...', '<U0041>;'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                intervals(text)
        for text in ('(<U0041>,<UD800>)', '(<U0041>,<U0042>);(<U0041>,<U0043>)'):
            with self.assertRaises(ValueError):
                pairs(text)

    def test_sequences(self):
        self.assertEqual(atom('<U0061><U0308> tail', '/'), ('a\u0308', ' tail'))
        self.assertEqual(alternatives('"<U0041><U0308>";"AE";""', '/'),
                         ['A\u0308', 'AE', ''])
        self.assertEqual(alternatives('"/<//3";\u00ab;<U0022>', '/'), ['</3', '\u00ab', '"'])
        for text in ('"open', '<unknown>', '"a" trailing', '"a";', ';', '/\0', '/\ud800'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                alternatives(text, '/')

    def test_copy_and_turkish_override(self):
        self.source('base', 'upper <U0041>..<U005A>\n'
                    'toupper (<U0061>,<U0041>);(<U0069>,<U0049>)')
        self.source('regional', 'copy "base"\nupper <U0130>\ntoupper (<U0069>,<U0130>)')
        reader = SourceReader(self.root)
        result = reader.read('regional')
        self.assertEqual(result['classes']['upper'], [[65, 90], [304, 304]])
        self.assertEqual(result['maps']['toupper'], [[97, 65], [105, 304]])
        result['maps']['toupper'][0][1] = 0
        self.assertEqual(reader.read('regional')['maps']['toupper'], [[97, 65], [105, 304]])
        self.assertEqual(len(reader.files), 2)
        self.assertEqual(reader.edges, [{'from': 'regional', 'to': 'base', 'kind': 'copy'}])

    def test_nested_transliteration_and_local_priority(self):
        self.source('leaf', 'translit_start\n<U00C4> "A"\ntranslit_end')
        self.source('middle', 'translit_start\ninclude "leaf";""\n'
                    '<U0300> ""\ntranslit_end')
        self.source('regional', 'translit_start\n<U00C4> "A<U0308>";"AE"\n'
                    'include "middle";""\ndefault_missing <U003F>\ntranslit_end')
        result = SourceReader(self.root).read('regional')
        self.assertEqual(result['transliterations']['\u00c4'], ['A\u0308', 'AE'])
        self.assertEqual(result['transliterations']['\u0300'], [''])
        self.assertEqual(result['default_missing'], '?')

    def test_named_fields(self):
        self.source('named', 'class "combining";<U0300>..<U0301>\n'
                    'map "totitle";(<U0069>,<U0130>)')
        result = SourceReader(self.root).read('named')
        self.assertEqual(result['classes']['combining'], [[768, 769]])
        self.assertEqual(result['maps']['totitle'], [[105, 304]])

    def test_invalid_structures_and_unknown_syntax(self):
        for body in ('translit_end', 'translit_start', 'unknown <U0041>',
                     'upper <U0041>\nupper <U0042>', 'copy "absent";"extra"',
                     'translit_start\ninclude "absent";"UTF-8"\ntranslit_end',
                     'translit_start\n<U0041> "a"\n<U0041> "b"\ntranslit_end'):
            self.source('invalid', body)
            with self.subTest(body=body), self.assertRaises(ValueError):
                SourceReader(self.root).read('invalid')

    def test_cycles_missing_files_and_path_escape(self):
        self.source('a', 'copy "b"')
        self.source('b', 'copy "a"')
        with self.assertRaises(ValueError):
            SourceReader(self.root).read('a')
        with self.assertRaises(FileNotFoundError):
            SourceReader(self.root).read('missing')
        for name in ('..', '../escape', '/absolute'):
            with self.assertRaises(ValueError):
                SourceReader(self.root).read(name)

    def test_character_record_keys(self):
        category = {'classes': {name: [] for name in CLASSES},
                    'maps': {name: [] for name in ('toupper', 'tolower', 'totitle')}}
        category['classes']['upper'] = [[65, 90]]
        category['maps']['toupper'] = [[105, 304]]
        records = character_records(category)
        self.assertEqual([r['key'] for r in records], list(range(1, 12)) +
                         [0x101, 0x102, 0x103])
        self.assertEqual(records[0]['value'], [65, 90])
        self.assertEqual(records[11]['value'], [105, 304])
        del category['maps']['tolower']
        with self.assertRaises(ValueError):
            character_records(category)


if __name__ == '__main__':
    unittest.main()
