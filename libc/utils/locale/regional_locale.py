# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Compose the selected UTF-8 regional data and its source review.

This producer does not install data or qualify public locale APIs. The runtime
must complete semantic admission before publishing a generated category.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

from collation_category import collation_records
from collation_source import SourceReader as CollationSource
from ctype_source import SourceReader as CTypeSource, character_records
from locale_file import encode_locale
from messages_category import messages_records
from monetary_category import monetary_records
from numeric_category import numeric_records
from simple_categories import MAX_INPUT, read_source
from time_category import time_records
from transliteration_category import transliteration_records

LOCALES = ('en_US', 'de_DE', 'tr_TR')


def compose(name, characters, collation, simple):
    if name not in LOCALES:
        raise ValueError('locale outside the selected source profile')
    monetary = monetary_records(simple['LC_MONETARY'])
    calendar = time_records(simple['LC_TIME'])
    messages = messages_records(simple['LC_MESSAGES'])
    ctype = character_records(characters) + transliteration_records(characters)
    ctype.append(dict(key=0x200, kind='string', value='UTF-8'))
    document = dict(name=name + '.UTF-8', categories=[
        ctype, numeric_records(simple['LC_NUMERIC']), calendar['records'],
        collation_records(collation), monetary['records'], messages['records']])
    review = dict(
        monetary_inheritance=monetary['inherited_fields'],
        time={key: value for key, value in calendar.items() if key != 'records'},
        messages={key: value for key, value in messages.items() if key != 'records'},
        collation_warnings=collation['warnings'],
        transliteration_default_missing=characters['default_missing'])
    return document, review


def build(source_root, name):
    if name not in LOCALES:
        raise ValueError('locale outside the selected source profile')
    source_root = Path(source_root).resolve()
    path = (source_root / name).resolve()
    if path.parent != source_root:
        raise ValueError('source escapes root')
    simple = read_source(path)
    character_reader = CTypeSource(source_root)
    characters = character_reader.read(name)
    collation_reader = CollationSource(source_root)
    collation = collation_reader.read(name)
    document, review = compose(name, characters, collation, simple['categories'])
    sources = dict(character_reader.files)
    for source, identity in collation_reader.files.items():
        if source in sources and sources[source]['sha256'] != identity['sha256']:
            raise ValueError('source changed during category generation')
        sources[source] = identity
    if sources[name]['sha256'] != simple['source']['sha256']:
        raise ValueError('source changed during category generation')
    review.update(name=document['name'], sources=sources,
                  ctype_dependencies=character_reader.edges,
                  collation_dependencies=collation_reader.edges,
                  unparsed_categories=simple['unparsed_categories'],
                  runtime_qualified=False)
    return encode_locale(document), review


def write_package(source_root, name, output):
    output = Path(output)
    if output.exists():
        raise FileExistsError('refusing to replace an existing locale package')
    binary, review = build(source_root, name)
    # Do not leave a discoverable locale.dat if provenance collection fails.
    with tempfile.TemporaryDirectory(dir=output.parent, prefix='.locale-') as temporary:
        staging = Path(temporary)
        notices = staging / 'sources'
        notices.mkdir()
        for source, identity in review['sources'].items():
            original = (Path(source_root) / source).resolve()
            if original.parent != Path(source_root).resolve():
                raise ValueError('source escapes root during notice collection')
            with original.open('rb') as stream:
                data = stream.read(MAX_INPUT + 1)
            if hashlib.sha256(data).hexdigest() != identity['sha256']:
                raise ValueError('source changed before notice collection')
            (notices / source).write_bytes(data)
        (staging / 'locale.dat').write_bytes(binary)
        review['locale_sha256'] = hashlib.sha256(binary).hexdigest()
        review['producer_sha256'] = {
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(Path(__file__).parent.glob('*.py'))
            if not path.name.startswith('test_')}
        (staging / 'review.json').write_text(json.dumps(review, indent=2, sort_keys=True) + '\n')
        # The destination is an explicit new staging directory, not a system
        # locale path. A failed copy is removed before returning an error.
        output.mkdir()
        try:
            shutil.copytree(staging, output, dirs_exist_ok=True)
        except Exception:
            if output.exists():
                shutil.rmtree(output)
            raise
    return review


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--locale', choices=LOCALES, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    review = write_package(args.source_root, args.locale, args.output)
    for warning in review['collation_warnings']:
        print('warning: ' + warning)


if __name__ == '__main__':
    main()
