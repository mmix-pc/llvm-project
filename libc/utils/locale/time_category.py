# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Normalize time text tables; calendar and format semantics are separate."""

FIELDS = ('abday', 'day', 'abmon', 'mon', 'alt_mon', 'ab_alt_mon', 'am_pm',
          'd_t_fmt', 'd_fmt', 't_fmt', 't_fmt_ampm', 'era', 'era_d_fmt',
          'era_t_fmt', 'era_d_t_fmt', 'alt_digits')
COUNTS = (7, 7, 12, 12, 12, 12, 2, 1, 1, 1, 1, None, 1, 1, 1, None)
INHERITED = {'alt_mon': 'mon', 'ab_alt_mon': 'abmon'}
EMPTY = {'era': [], 'era_d_fmt': [''], 'era_t_fmt': [''],
         'era_d_t_fmt': [''], 'alt_digits': []}
EXTENSIONS = {'date_fmt', 'week', 'first_weekday', 'first_workday',
              'cal_direction', 'timezone'}


def time_records(fields):
    if not isinstance(fields, dict) or fields.keys() - set(FIELDS) - EXTENSIONS:
        raise ValueError('unknown time field')
    records = []
    inherited = {}
    empty = []
    for index, (name, count) in enumerate(zip(FIELDS, COUNTS), 1):
        if name in fields:
            values = fields[name]
        elif name in INHERITED:
            parent = INHERITED[name]
            if parent not in fields:
                raise ValueError('missing time field: ' + parent)
            values = fields[parent]
            inherited[name] = parent
        elif name in EMPTY:
            values = EMPTY[name]
            empty.append(name)
        else:
            raise ValueError('missing time field: ' + name)
        maximum = 1024 if name == 'era' else 100
        if (not isinstance(values, list) or
                (count is not None and len(values) != count) or
                (count is None and len(values) > maximum)):
            raise ValueError('invalid time field cardinality: ' + name)
        key = index << 16
        records.append(dict(key=key, kind='words', value=[len(values)]))
        for ordinal, text in enumerate(values, 1):
            if (not isinstance(text, str) or
                    any(ord(ch) == 0 or 0xD800 <= ord(ch) <= 0xDFFF for ch in text)):
                raise ValueError('invalid time string: ' + name)
            if index <= 6 and not text:
                raise ValueError('empty day or month name')
            records.append(dict(key=key + ordinal, kind='string', value=text))
    # Source extensions are retained for review, not silently published as
    # standard time items or claimed as implemented formatting capabilities.
    return {'records': records, 'inherited_fields': inherited,
            'absent_empty_fields': empty,
            'unpublished_extensions': {key: fields[key]
                                       for key in sorted(fields.keys() & EXTENSIONS)}}
