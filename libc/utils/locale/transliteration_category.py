# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Serialize normalized transliteration alternatives without choosing a locale."""

MAX_RULES = 65536
MAX_SEQUENCE = 256
MAX_ALTERNATIVES = 256
MAX_WORDS = 4 * 1024 * 1024


def scalars(text, empty=False):
    if not isinstance(text, str) or len(text) > MAX_SEQUENCE or not text and not empty:
        raise ValueError('invalid transliteration sequence length')
    values = [ord(ch) for ch in text]
    if any(value == 0 or 0xD800 <= value <= 0xDFFF for value in values):
        raise ValueError('invalid transliteration scalar')
    return values


def transliteration_records(category):
    rules = category['transliterations']
    if not isinstance(rules, dict) or len(rules) > MAX_RULES:
        raise ValueError('invalid transliteration rule count')
    result = [dict(key=0x300, kind='words', value=[len(rules)])]
    total = 0
    # Python's Unicode ordering is scalar ordering, not UTF-8 byte ordering.
    for index, source in enumerate(sorted(rules)):
        key = scalars(source)
        alternatives = rules[source]
        if (not isinstance(alternatives, list) or not alternatives or
                len(alternatives) > MAX_ALTERNATIVES):
            raise ValueError('invalid transliteration alternative count')
        words = [len(key), *key, len(alternatives)]
        for text in alternatives:
            value = scalars(text, empty=True)
            words.extend([len(value), *value])
        total += len(words)
        if total > MAX_WORDS:
            raise ValueError('transliteration payload exceeds bound')
        result.append(dict(key=0x10000 + index, kind='words', value=words))
    # Source default_missing remains in the normalized review. Runtime fallback
    # is a separate conversion policy and must not silently inherit that field.
    return result
