# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Normalize the selected LC_CTYPE source profile without publishing a locale.

Classification intervals, case pairs and transliteration alternatives remain
separate. This is not a general localedef parser or a runtime table layout.
"""

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re

from simple_categories import MAX_INPUT, logical_lines, parse_values


SYMBOL = r"<U(?:[0-9A-Fa-f]{4}|[0-9A-Fa-f]{8})>"
CLASSES = frozenset(("upper", "lower", "alpha", "digit", "space", "cntrl",
                     "punct", "graph", "print", "xdigit", "blank"))
MAX_FILES = 64
MAX_RULES = 65536


def character_records(category):
    """Encode classification/case only, not codecs or transliteration."""
    result = []
    names = ("upper", "lower", "alpha", "digit", "space", "cntrl",
             "punct", "graph", "print", "xdigit", "blank")
    for family, fields, first_key in (
            ('classes', names, 1), ('maps', ('toupper', 'tolower', 'totitle'), 0x101)):
        for offset, name in enumerate(fields):
            if name not in category[family]:
                raise ValueError('missing character field: ' + name)
            values = category[family][name]
            result.append(dict(key=first_key + offset, kind='words',
                               value=[word for pair in values for word in pair]))
    return result


def scalar(symbol):
    if re.fullmatch(SYMBOL, symbol) is None:
        raise ValueError("expected a Unicode symbol")
    value = int(symbol[2:-1], 16)
    if value > 0x10FFFF or 0xD800 <= value <= 0xDFFF:
        raise ValueError("invalid Unicode scalar")
    return value


def intervals(text):
    result = []
    for part in text.split(';'):
        match = re.fullmatch(r"\s*(" + SYMBOL + r")(?:\.\.(" + SYMBOL + r"))?\s*", part)
        if not match:
            raise ValueError("unsupported class interval")
        start = scalar(match[1])
        end = scalar(match[2]) if match[2] else start
        if end < start or start <= 0xDFFF and end >= 0xD800:
            raise ValueError("invalid scalar interval")
        result.append([start, end])
    return merge_intervals(result)


def merge_intervals(result):
    merged = []
    for start, end in sorted(result):
        if merged and start <= merged[-1][1] + 1:
            merged[-1][1] = max(end, merged[-1][1])
        else:
            merged.append([start, end])
    return merged


def pairs(text):
    result = {}
    for part in text.split(';'):
        match = re.fullmatch(r"\s*\((" + SYMBOL + r"),(" + SYMBOL + r")\)\s*", part)
        if not match:
            raise ValueError("unsupported case mapping")
        source, target = scalar(match[1]), scalar(match[2])
        if source in result:
            raise ValueError("duplicate case mapping")
        result[source] = target
    return [[key, value] for key, value in sorted(result.items())]


def atom(text, escape):
    """Return one quoted or unquoted sequence and its unconsumed suffix."""
    text = text.lstrip()
    if not text:
        raise ValueError("missing transliteration sequence")
    if text.startswith('"'):
        index = 1
        while index < len(text):
            if text[index] == escape:
                index += 2
            elif text[index] == '"':
                value = parse_values(text[:index + 1], escape)
                return value[0], text[index + 1:]
            else:
                index += 1
        raise ValueError("unclosed transliteration sequence")
    index = 0
    output = []
    while index < len(text) and not text[index].isspace() and text[index] != ';':
        if text[index] == '<':
            match = re.match(SYMBOL, text[index:])
            if not match:
                raise ValueError("unsupported transliteration symbol")
            output.append(chr(scalar(match[0])))
            index += len(match[0])
        elif text[index] == escape:
            if index + 1 == len(text):
                raise ValueError("incomplete sequence escape")
            value = ord(text[index + 1])
            if value == 0 or 0xD800 <= value <= 0xDFFF:
                raise ValueError("invalid escaped scalar")
            output.append(text[index + 1])
            index += 2
        else:
            value = ord(text[index])
            if value == 0 or 0xD800 <= value <= 0xDFFF or text[index] == '"':
                raise ValueError("invalid literal scalar")
            output.append(text[index])
            index += 1
    if not output:
        raise ValueError("empty unquoted sequence")
    return ''.join(output), text[index:]


def alternatives(text, escape):
    result = []
    while True:
        value, tail = atom(text, escape)
        if len(value) > 256:
            raise ValueError("transliteration sequence exceeds bound")
        result.append(value)
        tail = tail.strip()
        if not tail:
            return result
        if not tail.startswith(';') or len(result) >= 256:
            raise ValueError("invalid or excessive alternatives")
        text = tail[1:]


class SourceReader:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.files = {}
        self.cache = {}
        self.stack = []
        self.edges = []
        self.bytes_read = 0

    def read(self, name):
        if re.fullmatch(r"[A-Za-z0-9_@.-]+", name) is None or name in ('.', '..'):
            raise ValueError("invalid source name")
        if name in self.stack:
            raise ValueError("LC_CTYPE dependency cycle")
        if name in self.cache:
            return copy.deepcopy(self.cache[name])
        if len(self.files) >= MAX_FILES:
            raise ValueError("source graph exceeds bound")
        path = (self.root / name).resolve()
        if path.parent != self.root:
            raise ValueError("source escapes root")
        with path.open('rb') as stream:
            data = stream.read(MAX_INPUT + 1)
        self.bytes_read += len(data)
        if len(data) > MAX_INPUT or self.bytes_read > 64 * 1024 * 1024:
            raise ValueError("source bytes exceed bound")
        source = data.decode('utf-8')
        self.files[name] = {"sha256": hashlib.sha256(data).hexdigest(),
                            "preamble": source.split('LC_IDENTIFICATION', 1)[0]
                            .split('LC_CTYPE', 1)[0]}
        self.stack.append(name)
        try:
            result = self._parse(name, source)
            self.cache[name] = result
            return copy.deepcopy(result)
        finally:
            self.stack.pop()

    def _parse(self, name, source):
        result = {"classes": {}, "maps": {}, "transliterations": {},
                  "default_missing": None}
        category = None
        found = False
        in_translit = False
        copied = False
        local_fields = set()
        local_rules = {}
        saw_content = False
        for line, escape in logical_lines(source):
            if re.fullmatch(r'LC_[A-Z_]+', line):
                if category is not None:
                    raise ValueError("nested source category")
                category = line
                if category == 'LC_CTYPE':
                    if found:
                        raise ValueError("duplicate LC_CTYPE")
                    found = True
                continue
            if line.startswith('END '):
                if category != line[4:].strip() or in_translit:
                    raise ValueError("unclosed or mismatched category")
                category = None
                continue
            if category is None:
                raise ValueError("text outside category")
            if category != 'LC_CTYPE':
                continue
            if line in ('translit_start', 'translit_end'):
                opening = line == 'translit_start'
                if opening == in_translit:
                    raise ValueError("unbalanced transliteration block")
                in_translit = opening
                saw_content = True
                continue
            parts = line.split(None, 1)
            if len(parts) != 2:
                raise ValueError("missing directive argument")
            directive, text = parts
            if directive == 'copy' or directive == 'include' and in_translit:
                values = parse_values(text, escape)
                if (directive == 'copy' and (in_translit or copied or saw_content) or
                        len(values) != (1 if directive == 'copy' else 2) or
                        any(type(value) is not str for value in values) or
                        directive == 'include' and values[1] != ''):
                    raise ValueError("unsupported source dependency")
                self.edges.append({"from": name, "to": values[0], "kind": directive})
                dependency = self.read(values[0])
                if directive == 'copy':
                    result = dependency
                    copied = True
                else:
                    if dependency['classes'] or dependency['maps']:
                        raise ValueError("transliteration include contains character fields")
                    result['transliterations'].update(dependency['transliterations'])
                    if result['default_missing'] is None:
                        result['default_missing'] = dependency['default_missing']
                if len(result['transliterations']) > MAX_RULES:
                    raise ValueError("too many transliteration rules")
                continue
            saw_content = True
            if in_translit:
                if directive == 'default_missing':
                    values = alternatives(text, escape)
                    if len(values) != 1:
                        raise ValueError("multiple default replacements")
                    result['default_missing'] = values[0]
                else:
                    key, tail = atom(line, escape)
                    if not key or len(key) > 256 or not tail or not tail[0].isspace():
                        raise ValueError("invalid transliteration key")
                    if key in local_rules:
                        raise ValueError("duplicate local transliteration")
                    local_rules[key] = alternatives(tail, escape)
                    if len(local_rules) > MAX_RULES:
                        raise ValueError("too many transliteration rules")
                continue
            family = None
            if directive in CLASSES:
                family = 'classes'
            elif directive in ('toupper', 'tolower'):
                family = 'maps'
            elif directive in ('class', 'map'):
                match = re.fullmatch(r'"([a-z][a-z0-9_]*)";\s*(.+)', text)
                if not match:
                    raise ValueError("invalid named character field")
                family = 'classes' if directive == 'class' else 'maps'
                directive, text = match[1], match[2]
            if family is None or (family, directive) in local_fields:
                raise ValueError("unsupported or duplicate LC_CTYPE field")
            local_fields.add((family, directive))
            if family == 'classes':
                result[family][directive] = merge_intervals(
                    result[family].get(directive, []) + intervals(text))
            else:
                inherited = dict(result[family].get(directive, []))
                inherited.update(pairs(text))
                result[family][directive] = [[key, value] for key, value in sorted(inherited.items())]
        if category is not None or not found:
            raise ValueError("missing or unclosed LC_CTYPE")
        # Local rules take precedence over imports, even when an include
        # follows a local declaration. Alternative order is never sorted.
        result['transliterations'].update(local_rules)
        if len(result['transliterations']) > MAX_RULES:
            raise ValueError("too many transliteration rules")
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('name')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    reader = SourceReader(args.root)
    category = reader.read(args.name)
    if not CLASSES <= category['classes'].keys() or not {'toupper', 'tolower'} <= category['maps'].keys():
        parser.error('incomplete character tables')
    result = dict(name=args.name, category=category, sources=reader.files,
                  edges=reader.edges, publishable_locale=False)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, ensure_ascii=True, sort_keys=True, indent=2)
        stream.write('\n')


if __name__ == '__main__':
    main()
