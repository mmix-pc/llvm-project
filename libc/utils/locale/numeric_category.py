# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Build the private NUMERIC schema from normalized locale-source fields."""


def grouping_bytes(grouping):
    if not isinstance(grouping, list) or not grouping:
        raise ValueError("missing grouping")
    output = []
    for index, width in enumerate(grouping):
        if type(width) is not int or not -1 <= width <= 126:
            raise ValueError("invalid grouping width")
        if width <= 0:
            if index + 1 != len(grouping):
                raise ValueError("grouping stop must be last")
            # Source zero means stop, not the lconv repeat-last terminator.
            output.append(127)
        else:
            output.append(width)
    output.append(0)
    return output


def numeric_records(fields):
    if not isinstance(fields, dict) or set(fields) != {
            "decimal_point", "thousands_sep", "grouping"}:
        raise ValueError("NUMERIC requires exactly three fields")
    result = []
    for key, field in enumerate(("decimal_point", "thousands_sep"), 1):
        value = fields[field]
        if (not isinstance(value, list) or len(value) != 1 or
                not isinstance(value[0], str)):
            raise ValueError("expected one string: " + field)
        text = value[0]
        if len(text) > 1 or (field == "decimal_point" and not text):
            raise ValueError("expected a single character: " + field)
        if text and (ord(text) == 0 or 0xD800 <= ord(text) <= 0xDFFF):
            raise ValueError("invalid Unicode scalar: " + field)
        result.append({"key": key, "kind": "string", "value": text})
    result.append({"key": 3, "kind": "bytes",
                   "value": grouping_bytes(fields['grouping'])})
    return result
