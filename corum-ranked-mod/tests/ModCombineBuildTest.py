"""Production channel isolation and CLI merge boundary, without real binaries."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('combine', Path(__file__).parents[1] / 'tools/combine-ranked-packages.py')
merge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(merge)


class CombineBuildTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'inputs'
        self.source.mkdir()
        self.version = 'v0.5.0-beta.122'
        for target, suffix in merge.TARGETS.items():
            folder = self.source / ('corum-platform-release-' + target)
            folder.mkdir()
            with zipfile.ZipFile(folder / 'mod.geode', 'w') as archive:
                archive.writestr('mod.json', json.dumps({'id': merge.MOD, 'version': self.version}))
                archive.writestr(merge.MOD + suffix, 'fixture binary')
        self.calls = []

    def run_cli(self, arguments, check):
        self.assertTrue(check)
        self.assertEqual(arguments[:4], ['geode', 'package', 'merge', '--optimize-pngs'])
        self.calls.append(arguments)
        # Independent file merge double: never a Native build/ABI assertion.
        files = {}
        for name in arguments[4:]:
            with zipfile.ZipFile(name) as archive:
                files.update({key: archive.read(key) for key in archive.namelist()})
        with zipfile.ZipFile(arguments[4], 'w') as archive:
            for name, data in files.items():
                archive.writestr(name, data)

    def test_selected_channel_has_all_five_platforms_and_preserves_original_artifacts(self):
        originals = {p: p.read_bytes() for p in self.source.glob('*/*.geode')}
        final = merge.combine(self.source, 'release', self.version, self.root / 'combined', self.run_cli)
        self.assertEqual(list(final.parent.glob('*.geode')), [final])
        with zipfile.ZipFile(final) as archive:
            self.assertTrue(merge.BINARIES <= set(archive.namelist()))
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(all(p.read_bytes() == data for p, data in originals.items()))

    def test_other_channel_or_missing_platform_cannot_reach_the_cli(self):
        first = self.source / 'corum-platform-release-Win64'
        first.rename(self.source / 'corum-platform-debug-Win64')
        with self.assertRaisesRegex(ValueError, 'Mixed channels'):
            merge.combine(self.source, 'release', self.version, self.root / 'combined', self.run_cli)
        self.assertEqual(self.calls, [])

    def test_same_channel_wrong_version_fails_before_any_merge(self):
        with self.assertRaisesRegex(ValueError, 'identity/version'):
            merge.combine(self.source, 'release', 'v0.5.0-beta.123', self.root / 'combined', self.run_cli)
        self.assertEqual(self.calls, [])

    def test_wrong_native_platform_fails_before_any_merge(self):
        file = self.source / 'corum-platform-release-Win64/mod.geode'
        with zipfile.ZipFile(file, 'a') as archive:
            archive.writestr(merge.MOD + '.ios.dylib', 'wrong platform')
        with self.assertRaisesRegex(ValueError, 'Wrong Native platform'):
            merge.combine(self.source, 'release', self.version, self.root / 'combined', self.run_cli)
        self.assertEqual(self.calls, [])


if __name__ == '__main__':
    unittest.main()
