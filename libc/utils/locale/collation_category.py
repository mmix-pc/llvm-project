# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Encode explicit rules and the selected implicit UNDEFINED policy."""

MAX_RULES = 131072
MAX_SEQUENCE = 256
MAX_WEIGHT = 262144
MAX_WORDS = 8 * 1024 * 1024


def collation_records(category):
    records = category['records']
    count = category['weight_count']
    if (not isinstance(records, list) or len(records) > MAX_RULES or
            type(count) is not int or not 0 <= count <= MAX_WEIGHT or
            category['undefined'] != 'shared-primary-codepoint-tail'):
        raise ValueError('invalid collation header')
    fallback = category['undefined_directions']
    if (not isinstance(fallback, list) or len(fallback) != 4 or
            any(type(value) is not int or value not in (1, 2, 5, 6) for value in fallback)):
        raise ValueError('invalid undefined directions')
    result = [dict(key=1, kind='words', value=[len(records), count, 1, *fallback])]
    previous = None
    total = 0
    for index, record in enumerate(sorted(records, key=lambda row: row['source'])):
        source, directions, weights = (record[key] for key in ('source', 'directions', 'weights'))
        if (not isinstance(source, list) or not 1 <= len(source) <= MAX_SEQUENCE or
                any(type(value) is not int or value < 0 or value > 0x10ffff or
                    0xd800 <= value <= 0xdfff for value in source) or
                previous is not None and source <= previous):
            raise ValueError('invalid or duplicate collation key')
        if (not isinstance(directions, list) or len(directions) != 4 or
                any(type(value) is not int or value not in (1, 2, 5, 6)
                    for value in directions) or
                any((direction & 4) != (default & 4)
                    for direction, default in zip(directions, fallback)) or
                not isinstance(weights, list) or len(weights) != 4):
            raise ValueError('invalid collation levels')
        words = [len(source), *source, *directions]
        for level in weights:
            if (not isinstance(level, list) or len(level) > MAX_SEQUENCE or
                    any(type(value) is not int or not 1 <= value <= count for value in level)):
                raise ValueError('invalid collation weights')
            words.extend([len(level), *level])
        total += len(words)
        if total > MAX_WORDS:
            raise ValueError('collation data exceeds bound')
        result.append(dict(key=0x1000 + index, kind='words', value=words))
        previous = source
    return result
