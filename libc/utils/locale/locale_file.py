#!/usr/bin/env python3
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Encode normalized category records, not glibc locale source syntax."""

import argparse
import json
from pathlib import Path
import re
import struct


MAGIC = b"LLVMLCL\0"
VERSION = 1
MAX_SIZE = 64 * 1024 * 1024
KINDS = {"string": 1, "words": 2, "bytes": 3, "reference": 4}


def unsigned(value):
    if type(value) is not int or not 0 <= value <= 0xFFFFFFFF:
        raise ValueError("expected a 32-bit unsigned integer")
    return value


def words(values):
    if not isinstance(values, list) or len(values) > MAX_SIZE // 4:
        raise ValueError("invalid word array")
    return b"".join(struct.pack(">I", unsigned(value)) for value in values)


def payload(kind, value):
    if kind == "string":
        if not isinstance(value, str) or "\0" in value:
            raise ValueError("expected a NUL-free string")
        # This is the container's textual representation, not a locale codec.
        if len(value) >= MAX_SIZE:
            raise ValueError("string exceeds the file bound")
        return value.encode("utf-8") + b"\0"
    if kind == "words":
        return words(value)
    if kind == "bytes":
        if not isinstance(value, list) or len(value) > MAX_SIZE:
            raise ValueError("invalid byte array")
        if any(type(byte) is not int or not 0 <= byte <= 255 for byte in value):
            raise ValueError("invalid byte")
        return bytes(value)
    if kind == "reference":
        if not isinstance(value, list) or len(value) != 3:
            raise ValueError("expected category, key and terminal kind")
        category, key, terminal = value
        if unsigned(category) >= 6 or unsigned(key) == 0:
            raise ValueError("invalid reference target")
        if terminal not in ("string", "words", "bytes"):
            raise ValueError("reference must name a terminal kind")
        return words([category, key, KINDS[terminal]])
    raise ValueError("unsupported item kind")


def encode_locale(document):
    if not isinstance(document, dict) or set(document) != {"name", "categories"}:
        raise ValueError("expected name and six categories")
    name = document["name"]
    if (not isinstance(name, str) or not 1 <= len(name) <= 255 or
            re.fullmatch(r"[A-Za-z0-9_.@-]+", name) is None or
            name in (".", "..")):
        raise ValueError("invalid locale name")
    name_bytes = name.encode("ascii") + b"\0"
    categories = document["categories"]
    if not isinstance(categories, list) or len(categories) != 6:
        raise ValueError("expected six categories")
    encoded = []
    types = []
    references = []
    total = 128 + len(name_bytes)
    for category in categories:
        if not isinstance(category, list) or len(category) > MAX_SIZE // 16:
            raise ValueError("invalid category")
        items = {}
        item_types = {}
        total += 16 * len(category)
        if total > MAX_SIZE:
            raise ValueError("locale exceeds the file bound")
        for item in category:
            if not isinstance(item, dict) or set(item) != {"key", "kind", "value"}:
                raise ValueError("expected item key, kind and value")
            key = unsigned(item["key"])
            if not key or key in items:
                raise ValueError("zero or duplicate item key")
            kind = item["kind"]
            value = payload(kind, item["value"])
            total += len(value)
            if total > MAX_SIZE:
                raise ValueError("locale exceeds the file bound")
            items[key] = (KINDS[kind], value)
            item_types[key] = kind
            if kind == "reference":
                references.append(item["value"])
        encoded.append(items)
        types.append(item_types)
    for category, key, terminal in references:
        if types[category].get(key) != terminal:
            raise ValueError("missing or nonterminal reference target")

    directory = bytearray()
    sections = bytearray()
    offset = 128 + len(name_bytes)
    for category, items in enumerate(encoded):
        table = bytearray()
        data = bytearray()
        table_size = 16 * len(items)
        for key, (kind, value) in sorted(items.items()):
            table += struct.pack(">IIII", key, kind, table_size + len(data), len(value))
            data += value
        length = len(table) + len(data)
        directory += struct.pack(">IIII", category, offset, length, len(items))
        sections += table + data
        offset += length
    assert offset == total
    header = MAGIC + struct.pack(">IIIIII", VERSION, 0x01020304, total,
                                 6, 128, len(name_bytes))
    return header + directory + name_bytes + sections


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON member: " + key)
        result[key] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.input.open("rb") as stream:
        source = stream.read(MAX_SIZE + 1)
    if len(source) > MAX_SIZE:
        parser.error("normalized input exceeds the file bound")
    document = json.loads(source.decode("utf-8"),
                          object_pairs_hook=unique_object)
    output = encode_locale(document)
    # No replacement of installed or previously qualified data is implicit.
    with args.output.open("xb") as stream:
        stream.write(output)


if __name__ == "__main__":
    main()
