#!/usr/bin/env python3
import hashlib
import importlib.util
import json
import sys
sys.dont_write_bytecode = True
import tempfile
import unittest
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_mod_release", root / "tools/prepare-mod-release.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class ModReleaseBuildTest(unittest.TestCase):
    def test_channels_have_independent_filenames_markers_and_exact_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "combined.geode"
            with zipfile.ZipFile(source, "w") as package:
                package.writestr("mod.json", json.dumps({"id": builder.MOD_ID, "version": "v0.5.0-beta.56"}))
                for binary in builder.BINARIES:
                    package.writestr(binary, b"native-platform-fixture")
                package.writestr("font.fnt", b"preserved-font")
                package.writestr(builder.MARKER, "stale marker")
            paths = []
            for channel in ("debug", "release"):
                result = builder.prepare(source, channel, "v0.5.0-beta.56", base / "out")
                paths.append(result)
                with zipfile.ZipFile(result) as package:
                    self.assertEqual(json.loads(package.read(builder.MARKER)), {"channel": channel, "version": "v0.5.0-beta.56"})
                    self.assertEqual(package.namelist().count(builder.MARKER), 1)
                    self.assertEqual(package.read("font.fnt"), b"preserved-font")
                descriptor = json.loads((base / "out" / f"{channel}.json").read_text())[channel]
                self.assertEqual(descriptor["sha256"], hashlib.sha256(result.read_bytes()).hexdigest())
                self.assertEqual(descriptor["sizeBytes"], result.stat().st_size)
            self.assertNotEqual(paths[0], paths[1])
            self.assertNotEqual(paths[0].read_bytes(), paths[1].read_bytes())
            for channel, version in (("latest", "v0.5.0-beta.56"), ("debug", "../../escape"), ("release", "v0.5.0-beta.57")):
                with self.assertRaises(ValueError):
                    builder.prepare(source, channel, version, base / "out")

    def test_incomplete_combined_package_cannot_publish(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "missing.geode"
            with zipfile.ZipFile(path, "w") as package:
                package.writestr("mod.json", json.dumps({"id": builder.MOD_ID, "version": "v0.5.0-beta.56"}))
            with self.assertRaises(ValueError):
                builder.prepare(path, "debug", "v0.5.0-beta.56", Path(directory) / "out")


if __name__ == "__main__":
    unittest.main()
