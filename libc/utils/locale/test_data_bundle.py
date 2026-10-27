# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from data_bundle import DEFAULT_ROOT, REGIONAL_NAMES, build_bundle, digest
from locale_file import encode_locale


class DataBundleTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.regional = []
        for name in sorted(REGIONAL_NAMES):
            package = self.root / name
            package.mkdir()
            data = encode_locale(dict(name=name, categories=[[]] * 6))
            (package / 'locale.dat').write_bytes(data)
            source = b'% fixture notice\n'
            (package / 'sources').mkdir()
            (package / 'sources/original').write_bytes(source)
            self.write_review(package, dict(
                name=name, locale_sha256=digest(data),
                sources={'original': dict(sha256=digest(source))},
                producer_sha256={'fixture.py': '1' * 64}, runtime_qualified=False))
            self.regional.append(package)
        self.encoding = self.root / 'mapping'
        self.encoding.mkdir()
        data = encode_locale(dict(name='ISO-2022-CN', categories=[[]] * 6))
        (self.encoding / 'ISO-2022-CN.dat').write_bytes(data)
        (self.encoding / 'sources').mkdir()
        source = b'# fixture encoding notice\n'
        for name in ('GB2312', 'CNS11643.ucm'):
            (self.encoding / 'sources' / name).write_bytes(source)
        self.write_review(self.encoding, dict(
            name='ISO-2022-CN', sha256=digest(data), bytes=len(data),
            gb2312=dict(source=dict(sha256=digest(source))),
            cns=dict(source=dict(sha256=digest(source))),
            producers={'fixture.py': '2' * 64}))
        self.licenses = {}
        for name in ('unicode-license.txt', 'icu-LICENSE'):
            path = self.root / name
            path.write_bytes(b'Constructed test license text.\n')
            self.licenses[name] = (path, digest(path.read_bytes()))

    def write_review(self, package, review):
        (package / 'review.json').write_text(json.dumps(review), encoding='utf-8')

    def build(self, output='bundle', **kwargs):
        return build_bundle(self.regional, self.encoding, self.licenses,
                            self.root / output, **kwargs)

    def snapshot(self, root):
        return {str(path.relative_to(root)): path.read_bytes()
                for path in root.rglob('*') if path.is_file()}

    def test_complete_tree_is_reproducible_and_relocatable(self):
        manifest = self.build()
        self.regional.reverse()
        self.assertEqual(manifest, self.build('second'))
        self.assertEqual(self.snapshot(self.root / 'bundle'),
                         self.snapshot(self.root / 'second'))
        self.assertFalse(manifest['runtime_qualified'])
        self.assertEqual(manifest['regional_locales'], sorted(REGIONAL_NAMES))
        for name, identity in manifest['files'].items():
            data = (self.root / 'bundle' / name).read_bytes()
            self.assertEqual(digest(data), identity['sha256'])
            self.assertEqual(len(data), identity['bytes'])
        (self.root / 'bundle').rename(self.root / 'relocated')
        data_root = self.root / 'relocated' / DEFAULT_ROOT.lstrip('/')
        self.assertTrue((data_root / 'tr_TR.UTF-8/locale.dat').is_file())
        self.assertTrue((data_root / 'encodings/ISO-2022-CN.dat').is_file())
        self.assertTrue((data_root / '.provenance/licenses/icu-LICENSE').is_file())
        self.assertFalse((data_root / 'llvm-test.ISO-2022-CN').exists())

    def test_explicit_target_root(self):
        self.build(data_root='/opt/libc/locale')
        self.assertTrue((self.root / 'bundle/opt/libc/locale/en_US.UTF-8/locale.dat').is_file())
        for invalid in ('/', 'relative', '/../escape', '/a/../b', '//server/a', '/a//b', '/a/'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                self.build('invalid', data_root=invalid)
        self.assertFalse((self.root / 'invalid').exists())

    def test_incomplete_or_duplicate_selection(self):
        original = self.regional[:]
        for selection in (original[:2], original + original[:1]):
            self.regional = selection
            with self.assertRaises(ValueError):
                self.build()
        self.assertFalse((self.root / 'bundle').exists())

    def test_source_and_binary_mutation_are_not_published(self):
        for relative in ('sources/original', 'locale.dat'):
            path = self.regional[0] / relative
            original = path.read_bytes()
            path.write_bytes(original + b'changed')
            with self.assertRaises(ValueError):
                self.build()
            self.assertFalse((self.root / 'bundle').exists())
            path.write_bytes(original)

    def test_binary_identity_and_source_path(self):
        package = self.regional[0]
        original = json.loads((package / 'review.json').read_text())
        review = dict(original, name='en_US.UTF-8')
        self.write_review(package, review)
        with self.assertRaises(ValueError):
            self.build()
        for name in ('../original', '/original', '.', '..'):
            review = dict(original, sources={name: dict(sha256='1' * 64)})
            self.write_review(package, review)
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.build()

    def test_missing_or_changed_license_is_rejected(self):
        original = self.licenses.copy()
        del self.licenses['icu-LICENSE']
        with self.assertRaises(ValueError):
            self.build()
        self.licenses = original
        path, identity = self.licenses['icu-LICENSE']
        path.write_bytes(b'new license')
        with self.assertRaises(ValueError):
            self.build()
        self.assertFalse((self.root / 'bundle').exists())

    def test_duplicate_json_and_symlink_input_are_rejected(self):
        path = self.regional[0] / 'review.json'
        original = path.read_bytes()
        path.write_text('{"name":"one", "name":"two"}')
        with self.assertRaises(ValueError):
            self.build()
        path.unlink()
        target = self.root / 'review-copy'
        target.write_bytes(original)
        path.symlink_to(target)
        with self.assertRaises(ValueError):
            self.build()

    def test_existing_output_is_unchanged_and_copy_failure_rolls_back(self):
        self.build()
        expected = self.snapshot(self.root / 'bundle')
        with self.assertRaises(FileExistsError):
            self.build()
        self.assertEqual(expected, self.snapshot(self.root / 'bundle'))
        with patch('data_bundle.shutil.copytree', side_effect=OSError('injected')):
            with self.assertRaises(OSError):
                self.build('failed')
        self.assertFalse((self.root / 'failed').exists())


if __name__ == '__main__':
    unittest.main()
