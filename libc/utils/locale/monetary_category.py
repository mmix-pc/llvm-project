# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Normalize the monetary fields present in the selected regional profile."""

from numeric_category import grouping_bytes


STRINGS = ('int_curr_symbol', 'currency_symbol', 'mon_decimal_point',
           'mon_thousands_sep', 'positive_sign', 'negative_sign')
INTEGERS = ('int_frac_digits', 'frac_digits', 'p_cs_precedes', 'p_sep_by_space',
            'n_cs_precedes', 'n_sep_by_space', 'p_sign_posn', 'n_sign_posn',
            'int_p_cs_precedes', 'int_p_sep_by_space', 'int_n_cs_precedes',
            'int_n_sep_by_space', 'int_p_sign_posn', 'int_n_sign_posn')
LIMITS = (126, 126, 1, 2, 1, 2, 4, 4, 1, 2, 1, 2, 4, 4)
INHERITED = {name: name[4:] for name in INTEGERS[8:]}


def monetary_records(fields):
    required = set(STRINGS) | set(INTEGERS[:8]) | {'mon_grouping'}
    allowed = required | set(INHERITED)
    if not isinstance(fields, dict) or not required <= fields.keys() <= allowed:
        raise ValueError('missing or unknown monetary field')
    records = []
    inherited = {}
    for key, name in enumerate(STRINGS, 1):
        value = fields[name]
        if (not isinstance(value, list) or len(value) != 1 or
                not isinstance(value[0], str)):
            raise ValueError('expected a monetary string: ' + name)
        text = value[0]
        if any(ord(ch) == 0 or 0xD800 <= ord(ch) <= 0xDFFF for ch in text):
            raise ValueError('invalid monetary scalar')
        if name in ('mon_decimal_point', 'mon_thousands_sep') and len(text) > 1:
            raise ValueError('monetary separator must be one character or empty')
        if name == 'int_curr_symbol' and text and (
                len(text) != 4 or any(not 'A' <= ch <= 'Z' for ch in text[:3])):
            raise ValueError('invalid international currency symbol shape')
        records.append(dict(key=key, kind='string', value=text))
    records.append(dict(key=7, kind='bytes', value=grouping_bytes(fields['mon_grouping'])))
    for key, (name, maximum) in enumerate(zip(INTEGERS, LIMITS), 8):
        if name in fields:
            value = fields[name]
        else:
            # Selected glibc data semantics: absent international formatting
            # fields inherit their local counterpart, not a fabricated C value.
            parent = INHERITED[name]
            value = fields[parent]
            inherited[name] = parent
        if (not isinstance(value, list) or len(value) != 1 or
                type(value[0]) is not int or not -1 <= value[0] <= maximum):
            raise ValueError('invalid monetary number: ' + name)
        records.append(dict(key=key, kind='words', value=[127 if value[0] == -1 else value[0]]))
    return {'records': records, 'inherited_fields': inherited}
