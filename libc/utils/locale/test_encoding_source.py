# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import unittest

from encoding_source import encoding_records, normalize_cns, normalize_gb2312


def cns(rows):
    return '# retained notice\nCHARMAP\n' + rows + '\nEND CHARMAP\n# trailer\n'


def gb(rows):
    ascii_rows = '\n'.join('<U%04X> /x%02x ASCII' % (i, i) for i in range(128))
    return 'CHARMAP\n' + ascii_rows + '\n' + rows + '\nEND CHARMAP\n'


class EncodingSourceTest(unittest.TestCase):
    def test_record_keys_and_direction(self):
        g = normalize_gb2312(gb('<U4E2D> /xd6/xd0'))
        c = normalize_cns(cns(r'<U4E2D> \xA1\xA1 |3'))
        rows = encoding_records(g, c)
        self.assertEqual([r['key'] for r in rows], list(range(0x201, 0x207)))
        self.assertEqual(rows[0]['value'], [0x5650, 0x4e2d])
        self.assertEqual(rows[1]['value'], [0x4e2d, 0x5650])
        self.assertEqual(rows[2]['value'], [0x2121, 0x4e2d])
        self.assertEqual(rows[3]['value'], [])
        self.assertEqual(rows[4]['value'], [])
        self.assertEqual(rows[5]['value'], [])

    def test_gb_exact_maps(self):
        result = normalize_gb2312(gb('<U4E2D> /xd6/xd0 CJK\n<U6587> /xce/xc4 CJK'))
        table = result['tables']['GB2312']
        self.assertEqual(table['decode'], [[0x4e44, 0x6587], [0x5650, 0x4e2d]])
        self.assertEqual(table['encode'], [[0x4e2d, 0x5650], [0x6587, 0x4e44]])
        self.assertFalse(result['publishable_locale'])

    def test_cns_directions_and_exclusions(self):
        result = normalize_cns(cns(r'''<U4E2D> \xC4\xE3 |0
<U6587> \x8E\xA2\xA1\xA1 |0
<U4E2D> \xA1\xA2 |3
<U4E2E> \xC4\xE3 |1
<U0041> \x41 |0
<U4E00> \x8E\xA3\xA1\xA1 |0'''))
        one, two = result['tables']['CNS11643_1'], result['tables']['CNS11643_2']
        self.assertEqual(one['decode'], [[0x2122, 0x4e2d], [0x4463, 0x4e2d]])
        self.assertEqual(one['encode'], [[0x4e2d, 0x4463]])
        self.assertEqual(one['non_exact'], [[0x4e2e, 0x4463, 1]])
        self.assertEqual(two['decode'], [[0x2121, 0x6587]])
        self.assertEqual(len(result['excluded_mappings']), 2)
        self.assertIn('retained notice', result['preamble'])
        self.assertIn('trailer', result['trailer'])

    def test_reject_ambiguous_and_duplicate_mappings(self):
        for rows in (r'<U4E2D> \xA1\xA1 |0' + '\n' + r'<U4E2E> \xA1\xA1 |0',
                     r'<U4E2D> \xA1\xA1 |0' + '\n' + r'<U4E2D> \xA1\xA2 |0',
                     r'<U4E2D> \xA1\xA1 |1' + '\n' + r'<U4E2D> \xA1\xA1 |1'):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                normalize_cns(cns(rows))
        with self.assertRaises(ValueError):
            normalize_gb2312(gb('<U4E2D> /xa1/xa1\n<U4E2D> /xa1/xa2'))

    def test_reject_malformed_scalars_and_coordinates(self):
        for row in (r'<UD800> \xA1\xA1 |0', r'<U110000> \xA1\xA1 |0',
                    r'<U4E00> \xA0\xA1 |0', r'<U4E00> \x8E\xB1\xA1\xA1 |0',
                    r'<U4E00> \xA1\xA1 |5', r'<U4E00> \xA1\xA1',
                    r'<U4E00> \xA1\xA1\xA1 |0', 'unsupported'):
            with self.subTest(row=row), self.assertRaises(ValueError):
                normalize_cns(cns(row))

    def test_ascii_and_section_completeness(self):
        with self.assertRaises(ValueError):
            normalize_gb2312('CHARMAP\n<U0000> /x00\nEND CHARMAP\n')
        with self.assertRaises(ValueError):
            normalize_gb2312(gb('<U0080> /x80'))
        for source in ('CHARMAP', 'END CHARMAP\nCHARMAP', cns('') + cns('')):
            with self.subTest(source=source), self.assertRaises(ValueError):
                normalize_cns(source)

    def test_source_order_does_not_choose_an_encoding(self):
        rows = [r'<U4E2D> \xA1\xA1 |0', r'<U4E2D> \xA1\xA2 |3',
                r'<U6587> \x8E\xA2\xA1\xA1 |0']
        self.assertEqual(normalize_cns(cns('\n'.join(rows)))['tables'],
                         normalize_cns(cns('\n'.join(reversed(rows))))['tables'])


if __name__ == '__main__':
    unittest.main()
