#!/usr/bin/env python3
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


builder = module('pack_builder', ROOT / 'tools/build-asset-pack.py')
verifier = module('pack_verifier', ROOT / 'tests/verify-asset-package.py')


class AssetPackTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.resources = self.root / 'resources'
        self.resources.mkdir()
        for index, name in enumerate(json.loads((ROOT / 'asset-pack.json').read_text())['files']):
            (self.resources / name).write_bytes(b'\x89PNG\r\n\x1a\n' + bytes([index]) * 40)
        (self.resources / 'GLOBED-LICENSE.txt').write_text('Boundary fixture license')
        (self.resources / 'CorumSans.ttf').write_bytes(b'Font must stay out of this archive')

    def build(self, output, version=None):
        with contextlib.redirect_stdout(io.StringIO()):
            return builder.build(self.resources, ROOT / 'asset-pack.json', self.root / output, version)

    def test_deterministic_single_zip_all_visuals_digest_and_no_fonts(self):
        first, metadata = self.build('first')
        second, _ = self.build('second')
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with contextlib.redirect_stdout(io.StringIO()):
            verifier.verify_pack(first, first.parent / 'asset-pack-metadata.json')
        self.assertEqual(metadata['sheetSettings']['rankedAssetPackSha256'], hashlib.sha256(first.read_bytes()).hexdigest())
        with zipfile.ZipFile(first) as z:
            self.assertIn('corum-fire-perfect.png', z.namelist())
            self.assertIn('corum-fire-promotion.png', z.namelist())
            self.assertNotIn('CorumSans.ttf', z.namelist())

    def test_update_changes_tag_and_digest_together(self):
        first, a = self.build('first')
        second, b = self.build('second', 'visuals-v2')
        self.assertNotEqual(a['client']['assets']['sha256'], b['client']['assets']['sha256'])
        self.assertIn('/visuals-v2/', b['downloadUrl'])
        self.assertNotEqual(first.read_bytes(), second.read_bytes())

    def test_missing_image_and_accidentally_added_image_refused(self):
        image = self.resources / 'rank-gold-core.png'
        original = image.read_bytes()
        image.unlink()
        with self.assertRaises(ValueError): self.build('missing')
        image.write_bytes(original)
        (self.resources / 'unknown.png').write_bytes(original)
        with self.assertRaises(ValueError): self.build('extra')

    def test_invalid_tag_corrupt_png_and_symlink_refused(self):
        for version in ('../other', '.', '..', 'a/b', '한글', 'x' * 65):
            with self.assertRaises(ValueError): self.build('unsafe', version)
        image = self.resources / 'rank-gold-core.png'
        image.write_bytes(b'not PNG')
        with self.assertRaises(ValueError): self.build('badpng')
        image.unlink()
        image.symlink_to(self.resources / 'rank-red-core.png')
        with self.assertRaises(ValueError): self.build('symlink')

    def test_verifier_refuses_corruption_and_visuals_inside_mod(self):
        pack, _ = self.build('pack')
        pack.write_bytes(pack.read_bytes() + b'corruption')
        with self.assertRaises(AssertionError): verifier.verify_pack(pack, pack.parent / 'asset-pack-metadata.json')
        mod = self.root / 'test.geode'
        def fake_package(extra):
            with zipfile.ZipFile(mod, 'w') as z:
                z.writestr('mod.json', json.dumps({'resources': {'sprites': []}}))
                z.writestr('CorumSans.fnt', '')
                if extra: z.writestr(extra, b'PNG')
        fake_package(None)
        with contextlib.redirect_stdout(io.StringIO()): verifier.verify_mod(mod)
        for extra in ('rank-gold-core.png', 'hwanhee1.corum_ranked/rank-icons.png'):
            fake_package(extra)
            with self.assertRaises(AssertionError): verifier.verify_mod(mod)


if __name__ == '__main__': unittest.main()
