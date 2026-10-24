# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Normalize the admitted GB2312 and ICU CNS planes for ISO-2022-CN.

This is mapping-data preparation, not a state machine or a locale publisher.
ICU direction flags are preserved; reverse-only mappings are never inverted.
"""

import hashlib
from pathlib import Path
import re

MAX_INPUT = 8 * 1024 * 1024
MAX_ROWS = 200000


def _body(text):
    if len(text) > MAX_INPUT:
        raise ValueError('mapping input exceeds bound')
    lines = text.splitlines()
    starts = [i for i, line in enumerate(lines) if line.strip() == 'CHARMAP']
    ends = [i for i, line in enumerate(lines) if line.strip() == 'END CHARMAP']
    if len(starts) != 1 or len(ends) != 1 or starts[0] >= ends[0]:
        raise ValueError('expected one complete CHARMAP')
    start, end = starts[0], ends[0]
    if end - start - 1 > MAX_ROWS:
        raise ValueError('mapping row count exceeds bound')
    return lines[start + 1:end], '\n'.join(lines[:start]), '\n'.join(lines[end + 1:])


def _scalar(text):
    value = int(text, 16)
    if value > 0x10ffff or 0xd800 <= value <= 0xdfff:
        raise ValueError('mapping is not a Unicode scalar')
    return value


def _position(raw):
    if len(raw) != 2 or any(not 0xa1 <= b <= 0xfe for b in raw):
        raise ValueError('invalid 94 by 94 mapping coordinate')
    return ((raw[0] - 0x80) << 8) | (raw[1] - 0x80)


class _Table:
    def __init__(self):
        self.decode = {}
        self.encode = {}
        self.fallbacks = []
        self.seen = set()

    def add(self, position, scalar, direction):
        identity = (position, scalar, direction)
        if identity in self.seen:
            raise ValueError('duplicate mapping row')
        self.seen.add(identity)
        if direction in (0, 3):
            if position in self.decode:
                raise ValueError('ambiguous decoding coordinate')
            self.decode[position] = scalar
        if direction == 0:
            if scalar in self.encode:
                raise ValueError('ambiguous exact encoding')
            self.encode[scalar] = position
        elif direction != 3:
            # Preserve, but do not publish fallbacks as exact representations.
            self.fallbacks.append([scalar, position, direction])

    def result(self):
        return {'decode': [list(p) for p in sorted(self.decode.items())],
                'encode': [list(p) for p in sorted(self.encode.items())],
                'non_exact': sorted(self.fallbacks)}


def normalize_gb2312(text):
    body, preamble, trailer = _body(text)
    table = _Table()
    ascii_map = {}
    for line in body:
        if not line.strip() or line.lstrip().startswith('%'):
            continue
        match = re.fullmatch(r'\s*<U([0-9a-fA-F]{4,8})>\s+((?:/x[0-9a-fA-F]{2})+)(?:\s+.*)?', line)
        if not match:
            raise ValueError('unsupported GB2312 mapping row')
        scalar = _scalar(match[1])
        raw = bytes.fromhex(match[2].replace('/x', ''))
        if len(raw) == 1:
            if raw[0] >= 128 or scalar != raw[0] or raw[0] in ascii_map:
                raise ValueError('invalid or duplicate ASCII mapping')
            ascii_map[raw[0]] = scalar
        else:
            table.add(_position(raw), scalar, 0)
    if len(ascii_map) != 128:
        raise ValueError('incomplete ASCII repertoire')
    return {'tables': {'GB2312': table.result()}, 'preamble': preamble,
            'trailer': trailer, 'publishable_locale': False}


def normalize_cns(text):
    body, preamble, trailer = _body(text)
    tables = {1: _Table(), 2: _Table()}
    excluded = []
    for line in body:
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        match = re.fullmatch(r'<U([0-9a-fA-F]{4,8})>\s+((?:\\x[0-9a-fA-F]{2})+)\s+\|([0-4])', line)
        if not match:
            raise ValueError('unsupported CNS mapping row')
        scalar, direction = _scalar(match[1]), int(match[3])
        raw = bytes.fromhex(match[2].replace('\\x', ''))
        if len(raw) == 2:
            plane, position = 1, _position(raw)
        elif len(raw) == 4 and raw[0] == 0x8e and 0xa1 <= raw[1] <= 0xb0:
            plane, position = raw[1] - 0xa0, _position(raw[2:])
        elif len(raw) == 1:
            plane, position = 0, raw[0]
        else:
            raise ValueError('invalid EUC-TW byte sequence')
        if plane in tables:
            tables[plane].add(position, scalar, direction)
        else:
            excluded.append([scalar, raw.hex(), direction])
    return {'tables': {'CNS11643_' + str(k): v.result() for k, v in tables.items()},
            'excluded_mappings': excluded, 'preamble': preamble, 'trailer': trailer,
            'publishable_locale': False}


def read_mapping(path, kind):
    with Path(path).open('rb') as stream:
        data = stream.read(MAX_INPUT + 1)
    if len(data) > MAX_INPUT:
        raise ValueError('mapping input exceeds bound')
    if kind not in ('GB2312', 'CNS11643'):
        raise ValueError('unsupported mapping source')
    result = (normalize_gb2312 if kind == 'GB2312' else normalize_cns)(data.decode('utf-8'))
    result['source'] = {'path': str(path), 'sha256': hashlib.sha256(data).hexdigest()}
    return result


def encoding_records(gb, cns):
    tables = [gb['tables']['GB2312'], cns['tables']['CNS11643_1'],
              cns['tables']['CNS11643_2']]
    return [dict(key=0x201 + 2 * i + direction, kind='words',
                 value=[word for pair in table[name] for word in pair])
            for i, table in enumerate(tables)
            for direction, name in enumerate(('decode', 'encode'))]
