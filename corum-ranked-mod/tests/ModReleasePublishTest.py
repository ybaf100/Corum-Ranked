#!/usr/bin/env python3
"""Exercise the production R2 publisher with an in-memory S3 boundary."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT.parent / '.github/workflows/publish-ranked-download.yml'
spec = importlib.util.spec_from_file_location('publish_mod_r2', ROOT / 'tools/publish-mod-r2.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)
BASE = 'https://downloads.example.org'


class MemoryR2(publisher.R2Client):
    def __init__(self):
        self.objects, self.calls, self.fail_key = {}, [], None

    def send(self, method, key, payload=b'', headers=None, max_bytes=4096):
        self.calls.append((method, key, headers))
        if key == self.fail_key:
            raise publisher.PublishError('R2 upload failed', 503)
        if method == 'PUT':
            if headers.get('if-none-match') != '*':
                raise AssertionError('Every write must be conditional')
            if headers.get('x-amz-meta-sha256') != hashlib.sha256(payload).hexdigest():
                raise AssertionError('Object digest does not match')
            if key in self.objects:
                raise publisher.PublishError('Already present', 412)
            self.objects[key] = payload
            return b''
        if method != 'GET' or key not in self.objects:
            raise publisher.PublishError('Not found', 404)
        if len(self.objects[key]) > max_bytes:
            raise publisher.PublishError('Existing R2 object exceeds the expected size')
        return self.objects[key]


class ModReleasePublishTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.client = MemoryR2()
        self.channel, self.version = 'debug', 'v0.5.0-beta.66'
        self.source = self.directory / (publisher.MOD_ID + '.geode')

    def prepare(self, channel='debug', marker=None, omit=None):
        self.channel = channel
        with zipfile.ZipFile(self.source, 'w') as archive:
            archive.writestr('mod.json', json.dumps({'id': publisher.MOD_ID, 'version': self.version}))
            archive.writestr('corum-build-channel.json', json.dumps(
                marker or {'channel': channel, 'version': self.version}))
            for binary in sorted(publisher.NATIVE_BINARIES - {omit}):
                archive.writestr(binary, b'fixture-not-a-device-build')
        self.descriptor = self.directory / (channel + '.json')
        self.refresh_descriptor()

    def refresh_descriptor(self):
        payload = self.source.read_bytes()
        self.descriptor.write_text(json.dumps({self.channel: {
            'version': self.version, 'sha256': hashlib.sha256(payload).hexdigest(),
            'sizeBytes': len(payload),
        }}))

    def execute(self):
        return publisher.publish(self.directory, self.channel, self.version, BASE, self.client)

    def keys(self):
        prefix = 'ranked/' + self.channel + '-' + self.version
        return (prefix + '/Corum-Ranked-' + self.version + '-' + self.channel + '.geode',
                prefix + '/' + self.channel + '.json')

    def test_first_publication_validates_both_channels_and_commits_descriptor_last(self):
        for channel in ('debug', 'release'):
            with self.subTest(channel=channel):
                self.prepare(channel)
                package, descriptor = self.keys()
                url = self.execute()
                self.assertEqual(url, BASE + '/' + package)
                self.assertEqual(self.client.objects[package], self.source.read_bytes())
                data = json.loads(self.client.objects[descriptor])[channel]
                self.assertEqual(data['downloadUrl'], url)
                self.assertEqual(data['sha256'], hashlib.sha256(self.source.read_bytes()).hexdigest())
                self.assertEqual([key for method, key, _ in self.client.calls if method == 'PUT'][-2:],
                                 [package, descriptor])

    def test_identical_retry_is_idempotent(self):
        self.prepare()
        self.execute()
        original = dict(self.client.objects)
        self.execute()
        self.assertEqual(self.client.objects, original)
        self.assertEqual(sum(method == 'GET' for method, _, _ in self.client.calls), 4)

    def test_different_package_bytes_are_never_overwritten(self):
        self.prepare()
        self.execute()
        original = dict(self.client.objects)
        with zipfile.ZipFile(self.source, 'a') as archive:
            archive.writestr('different-content', b'different')
        self.refresh_descriptor()
        with self.assertRaises(publisher.PublishError):
            self.execute()
        self.assertEqual(self.client.objects, original)

    def test_different_descriptor_is_never_overwritten(self):
        self.prepare()
        self.execute()
        _, descriptor = self.keys()
        self.client.objects[descriptor] = b'previous published descriptor'
        original = dict(self.client.objects)
        with self.assertRaises(publisher.PublishError):
            self.execute()
        self.assertEqual(self.client.objects, original)

    def test_partial_upload_can_resume_without_advertising_a_missing_package(self):
        self.prepare()
        package, descriptor = self.keys()
        self.client.fail_key = package
        with self.assertRaises(publisher.PublishError):
            self.execute()
        self.assertEqual(self.client.objects, {})
        self.client.fail_key = descriptor
        with self.assertRaises(publisher.PublishError):
            self.execute()
        self.assertEqual(set(self.client.objects), {package})
        self.client.fail_key = None
        self.execute()
        self.assertEqual(set(self.client.objects), {package, descriptor})

    def test_bad_digest_size_or_download_path_stops_before_any_r2_write(self):
        for key, value in [('sha256', '0' * 64), ('sizeBytes', 0),
                           ('downloadUrl', 'https://another.example.org/mod.geode')]:
            with self.subTest(key=key):
                self.prepare()
                data = json.loads(self.descriptor.read_text())
                data[self.channel][key] = value
                self.descriptor.write_text(json.dumps(data))
                with self.assertRaises(publisher.PublishError):
                    self.execute()
                self.assertEqual(self.client.calls, [])

    def test_wrong_channel_or_missing_platform_stops_before_any_r2_write(self):
        for options in ({'marker': {'channel': 'release', 'version': self.version}},
                        {'omit': publisher.MOD_ID + '.android64.so'}):
            with self.subTest(options=options):
                self.prepare(**options)
                with self.assertRaises(publisher.PublishError):
                    self.execute()
                self.assertEqual(self.client.calls, [])

    def test_public_origin_rejects_credentials_and_non_https_urls(self):
        self.assertEqual(publisher.public_base(BASE + '/'), BASE)
        for base in ('http://downloads.example.org', 'https://user:pass@example.org',
                     BASE + '/path', BASE + '?query=1', BASE + '#fragment', ''):
            with self.subTest(base=base), self.assertRaises(publisher.PublishError):
                publisher.public_base(base)

    def test_ranked_publication_uses_r2_without_github_release_commands(self):
        workflow = WORKFLOW.read_text()
        self.assertIn('publish-mod-r2.py', workflow)
        self.assertNotIn('gh release', workflow)
        self.assertNotIn('RANKED_RELEASE_TOKEN', workflow)
        for name in ('build-ranked-debug.yml', 'build-ranked-mod.yml'):
            caller = (WORKFLOW.parent / name).read_text()
            self.assertIn("vars.RANKED_PUBLISH_R2 == 'true'", caller)
            self.assertIn("github.ref == 'refs/heads/main'", caller)
            self.assertNotIn('RANKED_PUBLISH_RELEASES', caller)


if __name__ == '__main__':
    unittest.main()
