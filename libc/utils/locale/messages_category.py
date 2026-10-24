# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Normalize message text without claiming locale-aware regex validation."""

FIELDS = ('yesexpr', 'noexpr', 'yesstr', 'nostr')


def messages_records(fields):
    if not isinstance(fields, dict) or fields.keys() - set(FIELDS):
        raise ValueError('unknown messages field')
    records = []
    empty = []
    for key, name in enumerate(FIELDS, 1):
        if name in fields:
            values = fields[name]
        elif name in ('yesstr', 'nostr'):
            values = ['']
            empty.append(name)
        else:
            raise ValueError('missing messages expression: ' + name)
        if (not isinstance(values, list) or len(values) != 1 or
                not isinstance(values[0], str) or
                any(ord(ch) == 0 or 0xD800 <= ord(ch) <= 0xDFFF
                    for ch in values[0])):
            raise ValueError('invalid messages string: ' + name)
        records.append(dict(key=key, kind='string', value=values[0]))
    return {'records': records, 'absent_empty_fields': empty}
