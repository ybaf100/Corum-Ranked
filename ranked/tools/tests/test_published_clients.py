import hashlib
import importlib.util
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch
import urllib.error
import zipfile

spec = importlib.util.spec_from_file_location("barrier", Path(__file__).parents[1] / "verify-published-clients.py")
barrier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(barrier)
VERSION = "v0.5.0-beta.102"


def fixture(channel, stamp=None, binaries=True):
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as package:
        package.writestr("mod.json", json.dumps({"id": "hwanhee1.corum_ranked", "version": VERSION}))
        package.writestr("corum-build-channel.json", json.dumps(stamp or {"channel": channel, "version": VERSION}))
        for name in barrier.PLATFORMS if binaries else []:
            package.writestr(name, "binary")
    data = output.getvalue()
    descriptor = json.dumps({channel: {"version": VERSION, "sizeBytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), **({"sourceSha": stamp["sourceSha"]} if stamp and "sourceSha" in stamp else {})}}).encode()
    return descriptor, data


class PublishedClientBarrierTest(unittest.TestCase):
    def test_full_packages_verify_for_both_channels(self):
        for channel in ("release", "debug"):
            with patch.object(barrier, "fetch", side_effect=fixture(channel)):
                self.assertEqual(barrier.verify_channel("https://cdn.test", VERSION, channel)["version"], VERSION)

    def test_bad_hash_channel_version_or_missing_platform_blocks_activation(self):
        good, data = fixture("release")
        cases = [(good, data+b"corrupt"), fixture("release", {"channel": "debug", "version": VERSION}),
                 fixture("release", {"channel": "release", "version": "v0.5.0-beta.101"}), fixture("release", binaries=False)]
        for descriptor, payload in cases:
            with patch.object(barrier, "fetch", side_effect=[descriptor, payload]), self.assertRaises(ValueError):
                barrier.verify_channel("https://cdn.test", VERSION, "release")

    def test_same_version_with_missing_or_mixed_source_sha_fails_closed(self):
        for channel in ('release', 'debug'):
            stamp = {'channel':channel, 'version':VERSION, 'sourceSha':'a'*40}
            with patch.object(barrier, 'fetch', side_effect=fixture(channel, stamp)):
                self.assertEqual(barrier.verify_channel('https://cdn.test', VERSION, channel, 'a'*40)['sourceSha'], 'a'*40)
            for data in (fixture(channel), fixture(channel, stamp)):
                with patch.object(barrier, 'fetch', side_effect=data), self.assertRaisesRegex(ValueError, 'source SHA'):
                    barrier.verify_channel('https://cdn.test', VERSION, channel, 'b'*40)
            descriptor, package = fixture(channel, stamp)
            altered = json.loads(descriptor); altered[channel]['sourceSha'] = 'b'*40
            with patch.object(barrier, 'fetch', side_effect=[json.dumps(altered).encode(), package]), self.assertRaisesRegex(ValueError, 'source SHA'):
                barrier.verify_channel('https://cdn.test', VERSION, channel, 'a'*40)

    def test_waits_for_missing_channel_without_redownloading_verified_release(self):
        calls = []
        def verify(base, version, channel):
            calls.append(channel)
            if calls.count("debug") == 1:
                raise urllib.error.HTTPError("https://cdn.test", 404, "not yet published", {}, None)
            return {"channel": channel}
        with patch.object(barrier, "verify_channel", side_effect=verify), patch.object(barrier.time, "sleep"):
            result = barrier.wait_for_clients("https://cdn.test", VERSION, 10)
        self.assertEqual(set(result), {"release", "debug"})
        self.assertEqual(calls, ["release", "debug", "debug"])

    def test_missing_publication_times_out_and_corruption_fails_without_retry(self):
        error = urllib.error.HTTPError("https://cdn.test", 404, "missing", {}, None)
        with patch.object(barrier, "verify_channel", side_effect=error), self.assertRaises(TimeoutError):
            barrier.wait_for_clients("https://cdn.test", VERSION, 0)
        with patch.object(barrier, "verify_channel", side_effect=ValueError("bad hash")), self.assertRaises(ValueError):
            barrier.wait_for_clients("https://cdn.test", VERSION, 10)


if __name__ == "__main__":
    unittest.main()
