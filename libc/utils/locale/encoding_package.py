# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Build the controlled ISO-2022-CN mapping package from reviewed source files."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil

from encoding_source import encoding_records, read_mapping
from locale_file import encode_locale


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gb2312', type=Path, required=True)
    parser.add_argument('--cns', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    gb = read_mapping(args.gb2312, 'GB2312')
    cns = read_mapping(args.cns, 'CNS11643')
    data = encode_locale(dict(name='ISO-2022-CN', categories=[
        encoding_records(gb, cns), [], [], [], [], []]))
    # Never overwrite an admitted package or reinterpret a locale as a codec.
    args.output.mkdir()
    (args.output / 'ISO-2022-CN.dat').write_bytes(data)
    sources = args.output / 'sources'
    sources.mkdir()
    for path, label, review in ((args.gb2312, 'GB2312', gb),
                                (args.cns, 'CNS11643.ucm', cns)):
        destination = sources / label
        shutil.copyfile(path, destination)
        if hashlib.sha256(destination.read_bytes()).hexdigest() != review['source']['sha256']:
            raise ValueError('mapping source changed while packaging')
    report = dict(name='ISO-2022-CN', sha256=hashlib.sha256(data).hexdigest(),
                  bytes=len(data), gb2312=gb, cns=cns,
                  producers={name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                             for name in ('encoding_package.py', 'encoding_source.py', 'locale_file.py')})
    (args.output / 'review.json').write_text(
        json.dumps(report, ensure_ascii=True, indent=2, sort_keys=True) + '\n',
        encoding='utf-8')


if __name__ == '__main__':
    main()
