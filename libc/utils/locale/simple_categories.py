#!/usr/bin/env python3
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Normalize selected simple locale-source categories for semantic review.

This does not emit publishable locale.dat files or parse LC_CTYPE/LC_COLLATE.
Unknown directives in a selected category are errors, not ignored extensions.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re


MAX_INPUT = 8 * 1024 * 1024
SCHEMAS = {
    "LC_NUMERIC": {
        "decimal_point": (str, 1), "thousands_sep": (str, 1),
        "grouping": (int, None),
    },
    "LC_MONETARY": {
        **{name: (str, 1) for name in (
            "int_curr_symbol", "currency_symbol", "mon_decimal_point",
            "mon_thousands_sep", "positive_sign", "negative_sign")},
        "mon_grouping": (int, None),
        **{name: (int, 1) for name in (
            "int_frac_digits", "frac_digits", "p_cs_precedes", "p_sep_by_space",
            "n_cs_precedes", "n_sep_by_space", "p_sign_posn", "n_sign_posn",
            "int_p_cs_precedes", "int_p_sep_by_space", "int_n_cs_precedes",
            "int_n_sep_by_space", "int_p_sign_posn", "int_n_sign_posn")},
    },
    "LC_TIME": {
        "abday": (str, 7), "day": (str, 7),
        "abmon": (str, 12), "mon": (str, 12),
        "alt_mon": (str, 12), "ab_alt_mon": (str, 12), "am_pm": (str, 2),
        **{name: (str, 1) for name in (
            "d_t_fmt", "d_fmt", "t_fmt", "t_fmt_ampm", "date_fmt",
            "era_d_fmt", "era_t_fmt", "era_d_t_fmt")},
        "era": (str, None), "alt_digits": (str, None),
        "week": (int, 3), "first_weekday": (int, 1),
        "first_workday": (int, 1), "cal_direction": (int, 1),
        "timezone": (str, 1),
    },
    "LC_MESSAGES": {name: (str, 1) for name in
                    ("yesexpr", "noexpr", "yesstr", "nostr")},
}


def logical_lines(source):
    escape, comment = "\\", "#"
    pending = ""
    content_started = False
    for physical in source.splitlines():
        if not pending:
            directive = re.fullmatch(r"\s*(escape_char|comment_char)\s+(\S)\s*", physical)
            if directive:
                if content_started:
                    raise ValueError("syntax directive after category content")
                if directive[1] == "escape_char":
                    escape = directive[2]
                else:
                    comment = directive[2]
                continue
        quoted = False
        # Carry quoted-string state across physical continuation lines.
        index = 0
        line = pending + physical
        while index < len(line):
            ch = line[index]
            if ch == escape:
                if index + 1 == len(line):
                    pending = line[:-1]
                    break
                index += 2
                continue
            if ch == '"':
                quoted = not quoted
            elif ch == comment and not quoted:
                line = line[:index]
                index = len(line)
                continue
            index += 1
        else:
            if quoted:
                raise ValueError("unterminated quoted string")
            pending = ""
            if line.strip():
                content_started = True
                yield line.strip(), escape
            continue
        # The only loop break is an escaped physical newline.
    if pending:
        raise ValueError("incomplete physical continuation")


def parse_values(text, escape):
    values = []
    index = 0
    while True:
        while index < len(text) and text[index].isspace():
            index += 1
        if index == len(text):
            raise ValueError("missing value")
        if text[index] == '"':
            index += 1
            output = []
            while index < len(text) and text[index] != '"':
                ch = text[index]
                if ch == escape:
                    index += 1
                    if index == len(text):
                        raise ValueError("incomplete escape")
                    if text[index] == '\0' or 0xD800 <= ord(text[index]) <= 0xDFFF:
                        raise ValueError("invalid escaped scalar")
                    output.append(text[index])
                    index += 1
                elif ch == '<':
                    scalar = re.match(r"<U([0-9A-Fa-f]{4}|[0-9A-Fa-f]{8})>", text[index:])
                    if not scalar:
                        raise ValueError("unsupported symbolic character")
                    value = int(scalar[1], 16)
                    if value == 0 or value > 0x10FFFF or 0xD800 <= value <= 0xDFFF:
                        raise ValueError("invalid scalar in string")
                    output.append(chr(value))
                    index += len(scalar[0])
                else:
                    if ch == '\0' or 0xD800 <= ord(ch) <= 0xDFFF:
                        raise ValueError("invalid scalar in string")
                    output.append(ch)
                    index += 1
            if index == len(text):
                raise ValueError("unterminated string")
            values.append("".join(output))
            index += 1
        else:
            number = re.match(r"-?[0-9]+", text[index:])
            if not number:
                raise ValueError("expected a string or integer")
            value = int(number[0])
            if not -(1 << 31) <= value < (1 << 31):
                raise ValueError("integer outside supported range")
            values.append(value)
            index += len(number[0])
        while index < len(text) and text[index].isspace():
            index += 1
        if index == len(text):
            return values
        if text[index] != ';':
            raise ValueError("unexpected text after value")
        index += 1


def parse_categories(source):
    if len(source) > MAX_INPUT:
        raise ValueError("source exceeds bound")
    result = {}
    current = None
    excluded = []
    for line, escape in logical_lines(source):
        if re.fullmatch(r"LC_[A-Z_]+", line):
            if current is not None:
                raise ValueError("nested category")
            current = line
            if current in result or current in excluded:
                raise ValueError("duplicate category")
            if current in SCHEMAS:
                result[current] = {}
            else:
                excluded.append(current)
            continue
        if line.startswith("END "):
            if current != line[4:].strip():
                raise ValueError("mismatched category end")
            current = None
            continue
        if current is None:
            raise ValueError("unexpected source outside a category")
        if current not in SCHEMAS:
            continue
        parts = line.split(None, 1)
        if len(parts) != 2 or parts[0] not in SCHEMAS[current]:
            raise ValueError("unsupported " + current + " directive: " + line)
        key, raw = parts
        if key in result[current]:
            raise ValueError("duplicate category field: " + key)
        values = parse_values(raw, escape)
        kind, count = SCHEMAS[current][key]
        if any(type(value) is not kind for value in values):
            raise ValueError("wrong field type: " + key)
        if count is not None and len(values) != count:
            raise ValueError("wrong field cardinality: " + key)
        result[current][key] = values
    if current is not None:
        raise ValueError("unclosed category")
    if result.keys() != SCHEMAS.keys():
        raise ValueError("missing selected category")
    return {"categories": result, "unparsed_categories": excluded}


def read_source(path):
    with path.open("rb") as stream:
        data = stream.read(MAX_INPUT + 1)
    if len(data) > MAX_INPUT:
        raise ValueError("source exceeds bound")
    result = parse_categories(data.decode("utf-8"))
    result["source"] = {"path": str(path), "sha256": hashlib.sha256(data).hexdigest()}
    # Retain the original notice; this is provenance, not a license verdict.
    result["preamble"] = data.decode("utf-8").split("LC_IDENTIFICATION", 1)[0]
    result["publishable_locale"] = False
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = read_source(args.source)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=True, indent=2, sort_keys=True)
        stream.write("\n")


if __name__ == "__main__":
    main()
