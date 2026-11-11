"""Check the production DAG and evaluate its actual activation expression.
No remote mutation, secrets, mocked native physics or publication is performed.
"""
from pathlib import Path
import re
import unittest
import yaml
ROOT = Path(__file__).resolve().parents[3]
W = ROOT / '.github/workflows'


def workflow(name):
    # BaseLoader preserves GitHub's YAML 1.2 `on` and expression strings.
    return yaml.load((W / name).read_text(), Loader=yaml.BaseLoader)


def dependencies(job):
    needs = job.get('needs', [])
    return [needs] if isinstance(needs, str) else needs


def actual_activation(results, publish='true', deploy='true'):
    expression = workflow('ranked-release.yml')['jobs']['activate']['if']
    expression = expression.replace('always()', 'True').replace('&&', ' and ').replace('||', ' or ')
    expression = expression.replace('needs.policy.outputs.publish', repr(publish)).replace('needs.policy.outputs.deploy', repr(deploy))
    expression = re.sub(r'needs\.([\w-]+)\.result', lambda m: repr(results.get(m[1], 'skipped')), expression)
    return bool(eval(expression, {'__builtins__': {}}, {}))


class ReleaseWorkflowTest(unittest.TestCase):
    def test_debug_builds_four_platforms_after_metadata_without_debug_off_jobs(self):
        jobs = workflow('build-ranked-debug.yml')['jobs']
        self.assertEqual(dependencies(jobs['build']), ['metadata'])
        self.assertEqual({c['target'] for c in jobs['build']['strategy']['matrix']['config']},
                         {'Win64', 'iOS', 'Android32', 'Android64'})
        for name in ('ios-debug-off', 'additional-debug-off', 'publish-download', 'full-validation'):
            self.assertNotIn(name, jobs)
        self.assertEqual(jobs['tests']['uses'], './.github/workflows/ranked-native.yml')
        suite = (W / 'ranked-native.yml').read_text()
        self.assertIn('node --test --test-concurrency=2 corum-ranked-mod/tests/*.mjs', suite)
        self.assertIn('SpectatorRemotePolicyTest.cpp', suite)

    def test_pipeline_is_validate_debug_build_debug_publish_then_lightsail(self):
        jobs = workflow('ranked-release.yml')['jobs']
        self.assertNotIn('release', jobs)
        self.assertNotIn('publish-release', jobs)
        self.assertEqual(set(dependencies(jobs['publish-debug'])), {'policy', 'validation', 'debug'})
        self.assertNotIn('always()', jobs['publish-debug']['if'])
        self.assertEqual(set(dependencies(jobs['activate'])), {'policy', 'validation', 'debug', 'image', 'publish-debug'})

    def test_workflows_run_on_main_or_manually_only(self):
        for path in W.glob('*.yml'):
            triggers = workflow(path.name).get('on', {})
            with self.subTest(workflow=path.name):
                self.assertNotIn('pull_request', triggers)
                push = triggers.get('push') if isinstance(triggers, dict) else None
                if isinstance(push, dict) and 'branches' in push:
                    self.assertEqual(push['branches'], ['main'])
        for name in ('build-ranked-debug.yml', 'build-ranked-mod.yml', 'ranked-ci.yml', 'deploy-ranked-lightsail.yml',
                     'build-mod.yml', 'build-installer.yml'):
            self.assertNotIn('push', workflow(name)['on'])

    def test_failed_or_missing_debug_and_test_results_cannot_activate(self):
        names = ('validation', 'debug', 'image', 'publish-debug')
        success = dict.fromkeys(names, 'success')
        self.assertTrue(actual_activation(success))
        for name in names:
            for status in ('failure', 'cancelled', 'skipped'):
                with self.subTest(name=name, status=status):
                    self.assertFalse(actual_activation({**success, name: status}))
        self.assertFalse(actual_activation(success, deploy='false'))

    def test_build_only_does_not_disable_server_decision_or_make_a_client(self):
        run = next(s for s in workflow('ranked-release.yml')['jobs']['policy']['steps'] if s.get('id') == 'policy')['run']
        code = run.split("python3 - <<'PY'\n", 1)[1].rsplit('\nPY', 1)[0]
        import os
        import tempfile
        from unittest.mock import patch
        # A version-changing main push releases without markers; markers stay
        # accepted, and [build only] / [no-deploy] remain opt-outs.
        cases = [('[publish-r2] [deploy-lightsail]', 'false', True, True), ('[build only] [deploy-lightsail]', 'false', False, True),
                 ('[publish-r2] [no-deploy]', 'false', True, False), ('[no-deploy]', 'false', False, False), ('ordinary', 'false', False, False),
                 ('ordinary', 'true', True, True), ('[no-deploy]', 'true', True, False), ('[build only]', 'true', False, True)]
        for message, changed, publish, deploy in cases:
            with tempfile.NamedTemporaryFile() as out, patch.dict(os.environ, {'MESSAGE':message, 'PUBLISH':'false', 'AUTO_DEPLOY':'false', 'EVENT':'push', 'VERSION_CHANGED':changed, 'GITHUB_OUTPUT':out.name}):
                exec(code, {})
                value = Path(out.name).read_text()
                self.assertIn('publish='+str(publish).lower(), value)
                self.assertIn('deploy='+str(deploy).lower(), value)

    def test_stale_check_occurs_after_upload_before_restricted_activation(self):
        jobs = workflow('deploy-ranked-lightsail.yml')['jobs']
        step = next(s for s in jobs['build-and-deploy']['steps'] if s.get('id') == 'activation')
        script = step['run']
        self.assertLess(script.index('scp "'), script.index('latest="'))
        self.assertLess(script.index('latest="'), script.index('sudo /usr/local/sbin/corum-ranked-activate'))
        self.assertIn('exit 0', script[script.index('if [[ "$latest" !='):])
        self.assertIn("echo 'activated=false'", script)
        self.assertIn('already active', script)
        self.assertIn('org.opencontainers.image.revision', script)
        self.assertIn('StrictHostKeyChecking=yes', script)
        self.assertIn('--expected-sha "$GITHUB_SHA"', (W / 'deploy-ranked-lightsail.yml').read_text())
        self.assertEqual(workflow('deploy-ranked-lightsail.yml')['jobs']['build-and-deploy']['concurrency']['cancel-in-progress'], 'false')

    def test_same_run_same_commit_artifacts_and_tested_toolchain_pins(self):
        jobs = workflow('ranked-release.yml')['jobs']
        self.assertIn('${{ github.sha }}', jobs['activate']['with']['prepared_image'])
        for name in ('build-ranked-mod.yml', 'build-ranked-debug.yml'):
            text = (W / name).read_text()
            self.assertNotIn('build-geode-mod@main', text)
            self.assertIn('a0f7676e6fcd1a584f6d0c1c800239008412defc', text)
            self.assertIn('cli: v3.9.0', text)
            self.assertIn('--source-sha "$GITHUB_SHA"', text)
            self.assertNotIn('combine: true', text)
            self.assertIn('pattern: corum-platform-', text)
            self.assertIn('combine-ranked-packages.py', text)
            self.assertIn('bindings-ref: 70986055f526da15318779703f0e904be822f162', text)
        self.assertIn('SOURCE_SHA', (W / 'ranked-release.yml').read_text())


if __name__ == '__main__':
    unittest.main()
