#!/usr/bin/env python3
"""Run the actual publisher shell; GitHub requests use a local CLI double."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest
import zipfile

WORKFLOW = Path(__file__).resolve().parents[2] / ".github/workflows/publish-ranked-download.yml"


def publisher_script():
    block = WORKFLOW.read_text().split("        run: |\n", 1)[1]
    return textwrap.dedent(block)


# Model gh's error JSON on stdout even when --jq was requested. This is
# important: non-empty command output is not evidence that the API succeeded.
CLI = r'''#!/usr/bin/env python3
import json, os, pathlib, shutil, sys
root = pathlib.Path(os.environ['PUBLISH_FIXTURE'])
state_path = root / 'state.json'
state = json.loads(state_path.read_text())
args = sys.argv[1:]
with (root / 'calls.jsonl').open('a') as calls:
    calls.write(json.dumps(args) + '\n')
def fail(message, code):
    print(json.dumps({'message': message, 'status': str(code)}))
    print(f'gh: {message} (HTTP {code})', file=sys.stderr)
    sys.exit(1)
def save():
    state_path.write_text(json.dumps(state))
if args[0] == 'api':
    endpoint = args[1]
    if state.get('api_error'):
        fail('Fixture API failure', state['api_error'])
    if '/releases/tags/' in endpoint:
        if not state['exists']:
            fail('Not Found', 404)
        print('42')
    elif endpoint.endswith('/releases?per_page=100'):
        assert '--paginate' in args
        if state['exists']:
            print('42')
    else:
        assert endpoint.endswith('/releases/42') and state['exists']
        assert args[-1] == '.draft'
        print(str(state['draft']).lower())
elif args[:2] == ['release', 'create']:
    assert not state['exists'] and '--draft' in args
    assert ('--prerelease' in args) == (os.environ['CHANNEL'] == 'debug')
    state.update(exists=True, draft=True)
    save()
    for file in args[-2:]:
        source = pathlib.Path(file)
        shutil.copyfile(source, root / 'server' / source.name)
        if state.get('interrupt_create'):
            sys.exit(1)
elif args[:2] == ['release', 'download']:
    if not state['exists']:
        print('release not found', file=sys.stderr)
        sys.exit(1)
    name = args[args.index('--pattern') + 1]
    source = root / 'server' / name
    if not source.exists():
        print('no assets match the file pattern', file=sys.stderr)
        sys.exit(1)
    shutil.copyfile(source, pathlib.Path(args[args.index('--dir') + 1]) / name)
elif args[:2] == ['release', 'upload']:
    assert state['exists'] and state['draft'] and '--clobber' not in args
    source = pathlib.Path(args[-1])
    destination = root / 'server' / source.name
    assert not destination.exists()
    shutil.copyfile(source, destination)
elif args[:2] == ['release', 'edit']:
    assert state['exists'] and '--draft=false' in args and '--latest=false' in args
    assert len(list((root / 'server').iterdir())) == 2
    state['draft'] = False
    save()
else:
    raise AssertionError(args)
'''


class ModReleasePublishTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.downloads = self.root / 'downloads'
        self.downloads.mkdir()
        (self.root / 'server').mkdir()
        (self.root / 'gh').write_text(CLI)
        (self.root / 'gh').chmod(0o700)
        self.state = {'exists': False, 'draft': False}

    def prepare(self, channel='debug'):
        self.channel = channel
        self.version = 'v0.5.0-beta.57'
        self.package = self.downloads / f'Corum-Ranked-{self.version}-{channel}.geode'
        source = self.downloads / 'hwanhee1.corum_ranked.geode'
        with zipfile.ZipFile(source, 'w') as archive:
            archive.writestr('corum-build-channel.json', json.dumps({
                'channel': channel, 'version': self.version,
            }))
            archive.writestr('native-fixture', b'not-a-device-build')
        payload = source.read_bytes()
        self.descriptor = self.downloads / f'{channel}.json'
        self.descriptor.write_text(json.dumps({channel: {
            'version': self.version, 'sizeBytes': len(payload),
            'sha256': hashlib.sha256(payload).hexdigest(),
        }}))

    def execute(self):
        (self.root / 'state.json').write_text(json.dumps(self.state))
        # A workflow retry starts with a fresh runner working directory.
        shutil.rmtree(self.root / 'existing', ignore_errors=True)
        env = dict(os.environ, PATH=f'{self.root}{os.pathsep}{os.environ["PATH"]}',
                   PUBLISH_FIXTURE=str(self.root), CHANNEL=self.channel,
                   VERSION=self.version, SOURCE_COMMIT='fixture',
                   SOURCE_REPOSITORY='ybaf100/Corum-Ranked',
                   GITHUB_STEP_SUMMARY=str(self.root / 'summary'))
        # The double never reads a real token or makes network requests.
        env.pop('GH_TOKEN', None)
        env.pop('GITHUB_TOKEN', None)
        result = subprocess.run(['bash', '-c', publisher_script()], cwd=self.root,
                                env=env, capture_output=True, text=True, timeout=15)
        self.state = json.loads((self.root / 'state.json').read_text())
        return result

    def commands(self, name):
        path = self.root / 'calls.jsonl'
        calls = [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []
        return [call for call in calls if call[:2] == ['release', name]]

    def existing_release(self, draft=False, files=True):
        self.state.update(exists=True, draft=draft)
        if files:
            shutil.copyfile(self.downloads / 'hwanhee1.corum_ranked.geode', self.root / 'server' / self.package.name)
            shutil.copyfile(self.descriptor, self.root / 'server' / self.descriptor.name)

    def test_first_release_is_created_for_each_channel(self):
        for channel in ('debug', 'release'):
            with self.subTest(channel=channel):
                self.prepare(channel)
                result = self.execute()
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.state, {'exists': True, 'draft': False})
                self.assertEqual((self.root / 'server' / self.package.name).read_bytes(),
                                 self.package.read_bytes())
                self.assertEqual((self.root / 'server' / self.descriptor.name).read_bytes(),
                                 self.descriptor.read_bytes())
                self.state = {'exists': False, 'draft': False}
                for file in (self.root / 'server').iterdir():
                    file.unlink()
        self.assertEqual(len(self.commands('create')), 2)
        self.assertEqual(len(self.commands('edit')), 2)

    def test_query_errors_cannot_be_mistaken_for_an_existing_release(self):
        self.prepare()
        for status in (401, 403, 404, 500):
            with self.subTest(status=status):
                self.state['api_error'] = status
                result = self.execute()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Cannot list releases', result.stderr)
                self.assertFalse(self.state['exists'])
        for command in ('create', 'download', 'upload', 'edit'):
            self.assertFalse(self.commands(command))

    def test_published_identical_release_is_idempotent(self):
        self.prepare()
        self.existing_release()
        result = self.execute()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.commands('download')), 2)
        self.assertFalse(self.commands('create'))
        self.assertFalse(self.commands('upload'))

    def test_different_published_bytes_are_never_overwritten(self):
        self.prepare()
        self.existing_release()
        for path in (self.package, self.descriptor):
            with self.subTest(file=path.name):
                self.existing_release()
                remote = self.root / 'server' / path.name
                remote.write_bytes(b'different published bytes')
                result = self.execute()
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(remote.read_bytes(), b'different published bytes')
        self.assertFalse(self.commands('upload'))
        self.assertFalse(self.commands('edit'))

    def test_incomplete_published_release_is_not_modified(self):
        self.prepare()
        self.existing_release(files=False)
        result = self.execute()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('cannot be replaced', result.stderr)
        self.assertFalse(self.commands('upload'))
        self.assertFalse(self.commands('edit'))

    def test_empty_draft_resumes_both_files_before_publication(self):
        self.prepare()
        self.existing_release(draft=True, files=False)
        result = self.execute()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.commands('upload')), 2)
        self.assertFalse(self.commands('create'))
        self.assertFalse(self.state['draft'])

    def test_interrupted_first_upload_can_resume_without_overwriting(self):
        self.prepare()
        self.state['interrupt_create'] = True
        result = self.execute()
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(self.state['draft'])
        self.assertEqual(len(list((self.root / 'server').iterdir())), 1)
        self.state.pop('interrupt_create')
        result = self.execute()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.commands('create')), 1)
        self.assertEqual(len(self.commands('upload')), 1)
        self.assertFalse(self.state['draft'])

    def test_bad_artifact_digest_stops_before_any_release_request(self):
        self.prepare()
        metadata = json.loads(self.descriptor.read_text())
        metadata[self.channel]['sha256'] = '0' * 64
        self.descriptor.write_text(json.dumps(metadata))
        result = self.execute()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / 'calls.jsonl').exists())


if __name__ == '__main__':
    unittest.main()
