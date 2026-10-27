# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Stage reviewed locale packages and their provenance in a fresh target tree.

This verifies artifact identity, not locale semantics or redistribution policy.
The caller must review the inputs and qualify the resulting runtime separately.
"""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import sys
import tempfile

from locale_file import MAGIC, MAX_SIZE, VERSION, unique_object


DEFAULT_ROOT = '/usr/share/llvm-libc/locale'
REGIONAL_NAMES = {'en_US.UTF-8', 'de_DE.UTF-8', 'tr_TR.UTF-8'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_file(path):
    path = Path(path)
    if not stat.S_ISREG(path.lstat().st_mode):
        raise ValueError('expected a regular input file: ' + str(path))
    with path.open('rb') as stream:
        data = stream.read(MAX_SIZE + 1)
    if len(data) > MAX_SIZE:
        raise ValueError('input exceeds the file bound')
    return data


def check_hash(data, expected):
    if (not isinstance(expected, str) or
            re.fullmatch('[0-9a-f]{64}', expected) is None or
            digest(data) != expected):
        raise ValueError('input hash does not match its review')


def basename(name):
    if (not isinstance(name, str) or
            re.fullmatch('[A-Za-z0-9_.@-]+', name) is None or
            name in ('.', '..')):
        raise ValueError('invalid provenance name')
    return name


def check_container(data, name):
    if len(data) < 128 or data[:8] != MAGIC:
        raise ValueError('invalid locale container')
    version, endian, size, categories, offset, length = struct.unpack_from(
        '>IIIIII', data, 8)
    if ((version, endian, size, categories, offset) !=
            (VERSION, 0x01020304, len(data), 6, 128) or
            data[128:128 + length] != name.encode('ascii') + b'\0'):
        raise ValueError('container identity does not match its review')


def package_files(package, encoding=False):
    package = Path(package)
    raw_review = read_file(package / 'review.json')
    review = json.loads(raw_review, object_pairs_hook=unique_object)
    name = basename(review['name'])
    if encoding:
        if name != 'ISO-2022-CN':
            raise ValueError('unexpected encoding package')
        filename = name + '.dat'
        expected = review['sha256']
        sources = {'GB2312': review['gb2312']['source'],
                   'CNS11643.ucm': review['cns']['source']}
        producers = review['producers']
        destination = 'encodings/' + filename
    else:
        if name not in REGIONAL_NAMES:
            raise ValueError('unexpected regional package')
        filename = 'locale.dat'
        expected = review['locale_sha256']
        sources = review['sources']
        producers = review['producer_sha256']
        destination = name + '/' + filename
    if not sources or not producers:
        raise ValueError('missing source or producer identity')
    for producer, identity in producers.items():
        basename(producer)
        if not isinstance(identity, str) or re.fullmatch('[0-9a-f]{64}', identity) is None:
            raise ValueError('invalid producer identity')
    binary = read_file(package / filename)
    check_hash(binary, expected)
    if encoding and review['bytes'] != len(binary):
        raise ValueError('encoding size does not match its review')
    check_container(binary, name)
    prefix = '.provenance/' + name + '/'
    files = {destination: binary, prefix + 'review.json': raw_review}
    for source, identity in sources.items():
        basename(source)
        data = read_file(package / 'sources' / source)
        check_hash(data, identity['sha256'])
        files[prefix + 'sources/' + source] = data
    return name, files


def target_root(value):
    path = PurePosixPath(value)
    if (not path.is_absolute() or path == PurePosixPath('/') or
            '..' in path.parts or str(path) != value or value.startswith('//')):
        raise ValueError('data root must be a normalized target absolute path')
    return path.relative_to('/')


def build_bundle(regional, encoding, licenses, output, data_root=DEFAULT_ROOT):
    """Do not modify an existing sysroot or publish partially verified inputs."""
    output = Path(output)
    if output.exists() or output.is_symlink():
        raise FileExistsError('refusing to replace an existing target tree')
    root = target_root(data_root)
    files = {}
    names = set()
    for package in regional:
        name, incoming = package_files(package)
        if name in names:
            raise ValueError('duplicate regional locale')
        names.add(name)
        files.update(incoming)
    if names != REGIONAL_NAMES:
        raise ValueError('incomplete regional locale selection')
    _, incoming = package_files(encoding, encoding=True)
    files.update(incoming)
    if set(licenses) != {'unicode-license.txt', 'icu-LICENSE'}:
        raise ValueError('both reviewed license texts are required')
    for name, (path, expected) in licenses.items():
        data = read_file(path)
        check_hash(data, expected)
        if not data.strip():
            raise ValueError('empty license text')
        files['.provenance/licenses/' + name] = data

    manifest = dict(
        data_root=data_root, regional_locales=sorted(names),
        encodings=['ISO-2022-CN'], runtime_qualified=False,
        python_version=sys.version,
        producer_sha256={name: digest(read_file(Path(__file__).with_name(name)))
                         for name in ('data_bundle.py', 'locale_file.py')},
        files={str(root / name): dict(sha256=digest(data), bytes=len(data))
               for name, data in sorted(files.items())})
    # Validate and capture all inputs before creating a discoverable tree.
    with tempfile.TemporaryDirectory(dir=output.parent, prefix='.locale-bundle-') as temporary:
        staging = Path(temporary)
        for name, data in sorted(files.items()):
            destination = staging / root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        (staging / 'locale-data-manifest.json').write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        for path in staging.rglob('*'):
            path.chmod(0o755 if path.is_dir() else 0o644)
        output.mkdir()
        try:
            shutil.copytree(staging, output, dirs_exist_ok=True)
            output.chmod(0o755)
        except BaseException:
            shutil.rmtree(output)
            raise
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--regional', type=Path, action='append', required=True)
    parser.add_argument('--encoding', type=Path, required=True)
    parser.add_argument('--unicode-license', type=Path, required=True)
    parser.add_argument('--unicode-license-sha256', required=True)
    parser.add_argument('--icu-license', type=Path, required=True)
    parser.add_argument('--icu-license-sha256', required=True)
    parser.add_argument('--data-root', default=DEFAULT_ROOT)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    licenses = {'unicode-license.txt': (args.unicode_license, args.unicode_license_sha256),
                'icu-LICENSE': (args.icu_license, args.icu_license_sha256)}
    build_bundle(args.regional, args.encoding, licenses, args.output, args.data_root)


if __name__ == '__main__':
    main()
