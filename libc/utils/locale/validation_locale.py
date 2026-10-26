# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Compose the isolated ISO-2022-CN validation locale, not regional Chinese data.

The ordinary runtime reads the same container and codecs as other locale data.
No production install target includes this package.
"""

import argparse
from pathlib import Path

from ctype_source import SourceReader, character_records
from encoding_source import encoding_records, read_mapping
from locale_file import encode_locale
from regional_locale import publish_package
from transliteration_category import transliteration_records

NAME = 'llvm-test.ISO-2022-CN'
BUILTIN_CATEGORY_KEY = 0x7fff0001


def compose(characters, gb, cns):
    ctype = character_records(characters) + transliteration_records(characters)
    ctype += [dict(key=0x200, kind='string', value='ISO-2022-CN')]
    ctype += encoding_records(gb, cns)
    return dict(name=NAME, categories=[ctype] + [
        [dict(key=BUILTIN_CATEGORY_KEY, kind='string', value='C')]
        for _ in range(5)])


def build(source_root, gb2312, cns):
    source_root = Path(source_root).resolve()
    reader = SourceReader(source_root)
    characters = reader.read('i18n')
    gb = read_mapping(gb2312, 'GB2312')
    cn = read_mapping(cns, 'CNS11643')
    document = compose(characters, gb, cn)
    sources = dict(reader.files)
    inputs = {name: source_root / name for name in sources}
    for name, path, data in (('mapping-GB2312', gb2312, gb),
                              ('mapping-CNS11643.ucm', cns, cn)):
        if name in sources:
            raise ValueError('notice name collides with character source')
        sources[name] = data['source']
        inputs[name] = Path(path).resolve()
    review = dict(name=NAME, sources=sources, ctype_dependencies=reader.edges,
                  gb2312=gb, cns=cn, runtime_qualified=False,
                  deployment='isolated validation only',
                  other_categories='C', regional_chinese_data=False)
    return encode_locale(document), review, inputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--gb2312', type=Path, required=True)
    parser.add_argument('--cns', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError('refusing to replace an existing locale package')
    data, review, inputs = build(args.source_root, args.gb2312, args.cns)
    publish_package(data, review, inputs, args.output)


if __name__ == '__main__':
    main()
