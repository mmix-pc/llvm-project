# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Normalize the selected collation sources, not arbitrary localedef input.

Keep contractions, expansions and per-section directions. Unsupported syntax
is an error. No weights are inferred from comments or from Unicode code order.
"""

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re

from ctype_source import scalar
from simple_categories import MAX_INPUT, logical_lines, parse_values

SYMBOL = r"<[^<>\s;\"]+>"
MAX_SYMBOLS = 262144
MAX_SEQUENCE = 256
MAX_FILES = 16


def symbol(text):
    if re.fullmatch(SYMBOL, text) is None:
        raise ValueError('invalid collation symbol')
    if re.fullmatch(r'<U(?:[0-9A-Fa-f]{4}|[0-9A-Fa-f]{8})>', text):
        return '<U%08X>' % scalar(text)
    return text


def symbolic_range(text):
    match = re.fullmatch(r'<([A-Za-z])([0-9A-F]+)>\.\.<([A-Za-z])([0-9A-F]+)>', text)
    if not match:
        return [symbol(text)]
    first, last = int(match[2], 16), int(match[4], 16)
    if (match[1] != match[3] or len(match[2]) != len(match[4]) or
            first > last or last - first >= MAX_SYMBOLS):
        raise ValueError('invalid symbolic range')
    return [symbol('<%s%0*X>' % (match[1], len(match[2]), value))
            for value in range(first, last + 1)]


def weights(text, own):
    result = []
    for field in text.split(';') if text else []:
        field = field.strip()
        if field == 'IGNORE':
            result.append([])
        elif field.startswith('"') and field.endswith('"'):
            value = field[1:-1]
            tokens = re.findall(SYMBOL, value)
            if ''.join(tokens) != value or not tokens or len(tokens) > MAX_SEQUENCE:
                raise ValueError('invalid weight expansion')
            result.append([symbol(token) for token in tokens])
        else:
            result.append([own if field == '..' else symbol(field)])
    if len(result) > 4:
        raise ValueError('too many collation levels')
    return result + [[own] for _ in range(4 - len(result))]


def directions(text):
    result = []
    for field in text.split(';'):
        terms = field.split(',')
        if (not terms or terms[0] not in ('forward', 'backward') or
                terms[1:] not in ([], ['position'])):
            raise ValueError('invalid collation direction')
        result.append((1 if terms[0] == 'forward' else 2) |
                      (4 if len(terms) == 2 else 0))
    if len(result) != 4:
        raise ValueError('expected four collation levels')
    return result


class Table:
    def __init__(self):
        self.symbols = set()
        self.elements = {}
        self.scripts = set()
        self.entries = {}
        self.order = []
        self.sections = {}
        self.last_section = None
        self.tailorings = []

    def declare(self, name, sequence=None):
        if name in self.symbols or name in self.elements:
            raise ValueError('duplicate collating declaration: ' + name)
        if len(self.symbols) + len(self.elements) >= MAX_SYMBOLS:
            raise ValueError('collating declarations exceed bound')
        if sequence is None:
            self.symbols.add(name)
        else:
            self.elements[name] = sequence

    def insert(self, name, text, section, cursor=None):
        if len(self.entries) >= MAX_SYMBOLS and name not in self.entries:
            raise ValueError('collation entries exceed bound')
        sequence = self.elements.get(name)
        if sequence is None and name.startswith('<U'):
            try:
                sequence = [scalar(name)]
            except ValueError:
                pass
        if sequence is None and name not in self.symbols:
            raise ValueError('undeclared collating symbol: ' + name)
        if sequence is not None and section is None:
            raise ValueError('character outside ordered section')
        if name in self.entries:
            if cursor is None:
                raise ValueError('duplicate collation entry: ' + name)
            if cursor == name:
                raise ValueError('self-referencing reorder')
            self.order.remove(name)
        if cursor is None:
            self.order.append(name)
        else:
            self.order.insert(self.order.index(cursor) + 1, name)
        self.entries[name] = dict(source=sequence, section=section,
                                  weights=weights(text, name))

    def normalized(self):
        rank = {name: i + 1 for i, name in enumerate(self.order)}
        records = []
        sequences = set()
        for name in self.order:
            entry = self.entries[name]
            if entry['source'] is None:
                continue
            key = tuple(entry['source'])
            if key in sequences:
                raise ValueError('duplicate collating sequence')
            sequences.add(key)
            try:
                resolved = [[rank[value] for value in level]
                            for level in entry['weights']]
            except KeyError as error:
                raise ValueError('unresolved collation weight: ' + str(error)) from error
            records.append(dict(symbol=name, source=list(key), weights=resolved,
                                directions=self.sections[entry['section']]))
        records.sort(key=lambda record: record['source'])
        if self.last_section is None:
            raise ValueError('missing ordered collation section')
        return dict(records=records, weight_count=len(rank),
                    undefined='shared-primary-codepoint-tail',
                    undefined_directions=self.sections[self.last_section],
                    warnings=['No UNDEFINED rule: append unlisted Unicode scalars '
                              'with a shared primary weight and codepoint-ordered '
                              'subsequent weights.'],
                    sections=self.sections, tailorings=self.tailorings)


class SourceReader:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.files = {}
        self.edges = []
        self.stack = []
        self.cache = {}
        self.bytes_read = 0

    def read(self, name):
        return self._read(name).normalized()

    def _read(self, name):
        if re.fullmatch(r'[A-Za-z0-9_@.-]+', name) is None or name in ('.', '..'):
            raise ValueError('invalid source name')
        if name in self.stack:
            raise ValueError('LC_COLLATE dependency cycle')
        if name in self.cache:
            return copy.deepcopy(self.cache[name])
        if len(self.files) >= MAX_FILES:
            raise ValueError('source graph exceeds bound')
        path = (self.root / name).resolve()
        if path.parent != self.root:
            raise ValueError('source escapes root')
        with path.open('rb') as stream:
            data = stream.read(MAX_INPUT + 1)
        self.bytes_read += len(data)
        if len(data) > MAX_INPUT or self.bytes_read > 64 * 1024 * 1024:
            raise ValueError('source bytes exceed bound')
        source = data.decode('utf-8')
        self.files[name] = dict(sha256=hashlib.sha256(data).hexdigest(),
                               preamble=source.split('LC_IDENTIFICATION', 1)[0]
                               .split('LC_COLLATE', 1)[0])
        self.stack.append(name)
        try:
            result = self._parse(name, source)
            self.cache[name] = result
            return copy.deepcopy(result)
        finally:
            self.stack.pop()

    def _parse(self, name, source):
        table = Table()
        category = None
        found = copied = False
        section = cursor = None
        # The selected sources have one optional, unset DIACRIT_BACKWARD block.
        condition = None
        pending_range = None
        for line, escape in logical_lines(source):
            if re.fullmatch(r'LC_[A-Z_]+', line):
                if category is not None:
                    raise ValueError('nested source category')
                category = line
                if category == 'LC_COLLATE':
                    if found:
                        raise ValueError('duplicate LC_COLLATE')
                    found = True
                continue
            if line.startswith('END '):
                if (category != line[4:].strip() or section is not None or
                        cursor is not None or condition is not None or pending_range):
                    raise ValueError('unclosed collation block')
                category = None
                continue
            if category is None:
                raise ValueError('text outside category')
            if category != 'LC_COLLATE':
                continue
            if line.startswith('ifdef '):
                if line != 'ifdef DIACRIT_BACKWARD' or condition is not None:
                    raise ValueError('unsupported conditional')
                condition = False
                continue
            if line == 'else':
                if condition is not False:
                    raise ValueError('unexpected else')
                condition = True
                continue
            if line == 'endif':
                if condition is None:
                    raise ValueError('unexpected endif')
                condition = None
                continue
            if condition is False:
                continue
            parts = line.split(None, 1)
            directive, text = parts[0], parts[1] if len(parts) == 2 else ''
            if pending_range and not directive.startswith('<'):
                raise ValueError('missing range endpoint')
            if directive == 'copy':
                values = parse_values(text, escape)
                if (copied or table.entries or table.symbols or table.elements or
                        table.scripts or section or cursor or len(values) != 1 or
                        not isinstance(values[0], str)):
                    raise ValueError('invalid collation copy')
                self.edges.append(dict(source=name, target=values[0]))
                table = self._read(values[0])
                copied = True
            elif directive == 'script':
                value = symbol(text)
                if value in table.scripts:
                    raise ValueError('duplicate script')
                table.scripts.add(value)
            elif directive == 'collating-symbol':
                for value in symbolic_range(text):
                    table.declare(value)
            elif directive == 'collating-element':
                match = re.fullmatch('(' + SYMBOL + r')\s+from\s+(.*)', text)
                if not match:
                    raise ValueError('invalid collating element')
                values = parse_values(match[2], escape)
                if (len(values) != 1 or not isinstance(values[0], str) or
                        not 1 <= len(values[0]) <= MAX_SEQUENCE):
                    raise ValueError('invalid collating sequence')
                table.declare(symbol(match[1]), [ord(ch) for ch in values[0]])
            elif directive == 'order_start':
                fields = text.split(';', 1)
                if section or cursor or len(fields) != 2:
                    raise ValueError('invalid order start')
                section = symbol(fields[0])
                if section not in table.scripts or section in table.sections:
                    raise ValueError('unknown or duplicate section')
                table.sections[section] = directions(fields[1])
                table.last_section = section
            elif directive == 'order_end':
                if section is None or text:
                    raise ValueError('unexpected order end')
                section = None
            elif directive == 'reorder-after':
                anchor = symbol(text)
                if section or anchor not in table.entries:
                    raise ValueError('invalid reorder anchor')
                cursor = anchor
                table.tailorings.append(dict(after=anchor, entries=[]))
            elif directive == 'reorder-end':
                if cursor is None or text:
                    raise ValueError('unexpected reorder end')
                cursor = None
            elif directive == '..':
                if not section or not table.order or cursor or pending_range:
                    raise ValueError('invalid collation range')
                start = scalar(table.order[-1])
                weights(text, table.order[-1])
                pending_range = (start, text)
            elif directive.startswith('<'):
                value = symbol(directive)
                if pending_range:
                    start, pattern = pending_range
                    end = scalar(value)
                    if end <= start or end - start > MAX_SYMBOLS:
                        raise ValueError('invalid character range')
                    for code in range(start + 1, end):
                        table.insert(symbol('<U%08X>' % code), pattern, section)
                    pending_range = None
                table.insert(value, text, table.last_section if cursor else section, cursor)
                if cursor:
                    table.tailorings[-1]['entries'].append(value)
                    cursor = value
            else:
                raise ValueError('unsupported collation directive: ' + directive)
        if not found or category is not None:
            raise ValueError('missing or unclosed LC_COLLATE')
        return table


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True)
    parser.add_argument('--locale', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    reader = SourceReader(args.source_root)
    category = reader.read(args.locale)
    result = dict(locale=args.locale, category=category, sources=reader.files,
                  dependencies=reader.edges, publishable=False)
    Path(args.output).write_text(json.dumps(result, ensure_ascii=True, indent=2) + '\n')


if __name__ == '__main__':
    main()
