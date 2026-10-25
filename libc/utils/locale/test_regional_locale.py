# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from ctype_source import CLASSES
from monetary_category import STRINGS, INTEGERS
from regional_locale import compose, write_package


class RegionalLocaleTest(unittest.TestCase):
    def inputs(self):
        characters = dict(classes={key: [] for key in CLASSES},
                          maps={key: [] for key in ('toupper', 'tolower', 'totitle')},
                          transliterations={}, default_missing='?')
        collation = dict(records=[], weight_count=0,
                         undefined='shared-primary-codepoint-tail',
                         undefined_directions=[1, 1, 1, 5], warnings=['implicit UNDEFINED'])
        monetary = {key: [''] for key in STRINGS}
        monetary.update({key: [-1] for key in INTEGERS[:8]})
        monetary['mon_grouping'] = [-1]
        calendar = {key: [key] * count for key, count in (
            ('abday', 7), ('day', 7), ('abmon', 12), ('mon', 12))}
        calendar.update(am_pm=['AM', 'PM'], d_t_fmt=['%x %X'], d_fmt=['%m/%d/%y'],
                        t_fmt=['%H:%M:%S'], t_fmt_ampm=['%I:%M:%S %p'])
        simple = dict(LC_NUMERIC=dict(decimal_point=['.'], thousands_sep=[''], grouping=[-1]),
                      LC_MONETARY=monetary, LC_TIME=calendar,
                      LC_MESSAGES=dict(yesexpr=['^[yY]'], noexpr=['^[nN]']))
        return characters, collation, simple

    def test_complete_composition_and_encoding(self):
        document, review = compose('en_US', *self.inputs())
        self.assertEqual(document['name'], 'en_US.UTF-8')
        self.assertEqual(len(document['categories']), 6)
        self.assertTrue(all(document['categories']))
        encoding = [row for row in document['categories'][0] if row['key'] == 0x200]
        self.assertEqual(encoding, [dict(key=0x200, kind='string', value='UTF-8')])
        self.assertEqual(review['time']['inherited_fields']['alt_mon'], 'mon')
        self.assertEqual(review['messages']['absent_empty_fields'], ['yesstr', 'nostr'])
        self.assertEqual(len(review['monetary_inheritance']), 6)

    def test_invalid_profile_and_missing_category(self):
        for name in ('C', '../en_US', 'en_US.UTF-8', 'unknown'):
            with self.assertRaises(ValueError):
                compose(name, *self.inputs())
        characters, collation, simple = self.inputs()
        del simple['LC_TIME']['d_fmt']
        with self.assertRaises(ValueError):
            compose('de_DE', characters, collation, simple)

    def test_notice_copy_hashes_and_exclusive_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            source.mkdir()
            original = b'% retained notice\nLC_COLLATE\nEND LC_COLLATE\n'
            (source / 'en_US').write_bytes(original)
            binary = b'constructed producer fixture'
            review = dict(sources={'en_US': dict(sha256=hashlib.sha256(original).hexdigest())},
                          runtime_qualified=False)
            output = root / 'generated'
            with patch('regional_locale.build', return_value=(binary, review)):
                write_package(source, 'en_US', output)
                self.assertEqual((output / 'sources/en_US').read_bytes(), original)
                self.assertEqual((output / 'locale.dat').read_bytes(), binary)
                saved = json.loads((output / 'review.json').read_text())
                self.assertEqual(saved['locale_sha256'], hashlib.sha256(binary).hexdigest())
                self.assertFalse(saved['runtime_qualified'])
                with self.assertRaises(FileExistsError):
                    write_package(source, 'en_US', output)
                self.assertEqual((output / 'locale.dat').read_bytes(), binary)

    def test_failure_does_not_publish_or_remove_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'en_US').write_bytes(b'changed')
            review = dict(sources={'en_US': dict(sha256='0' * 64)})
            output = root / 'generated'
            with patch('regional_locale.build', return_value=(b'data', review)):
                with self.assertRaises(ValueError):
                    write_package(root, 'en_US', output)
                self.assertFalse(output.exists())
            review = dict(sources={})
            with patch('regional_locale.build', return_value=(b'data', review)):
                with patch('regional_locale.shutil.copytree', side_effect=OSError('injected')):
                    with self.assertRaises(OSError):
                        write_package(root, 'en_US', output)
                self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
