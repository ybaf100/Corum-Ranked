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
    def test_platform_builds_start_after_metadata_independently_of_native_tests(self):
        jobs = workflow('build-ranked-debug.yml')['jobs']
        for name in ('build', 'ios-debug-off', 'additional-debug-off'):
            self.assertEqual(dependencies(jobs[name]), ['metadata'])
        self.assertEqual(jobs['tests']['uses'], './.github/workflows/ranked-native.yml')
        suite = (W / 'ranked-native.yml').read_text()
        self.assertIn('node --test --test-concurrency=2 corum-ranked-mod/tests/*.mjs', suite)
        self.assertIn('SpectatorRemotePolicyTest.cpp', suite)

    def test_both_publications_require_the_complete_debug_release_and_server_workflows(self):
        jobs = workflow('ranked-release.yml')['jobs']
        for name in ('publish-release', 'publish-debug'):
            self.assertEqual(set(dependencies(jobs[name])), {'policy', 'validation', 'release', 'debug'})
            self.assertNotIn('always()', jobs[name]['if'])
        debug = workflow('build-ranked-debug.yml')['jobs']
        self.assertTrue({'tests', 'ios-debug-off', 'additional-debug-off', 'full-validation'} <= set(dependencies(debug['publish-download'])))
        self.assertEqual(len(debug['build']['strategy']['matrix']['config']), 5)
        self.assertEqual(len(debug['additional-debug-off']['strategy']['matrix']['config']), 4)

    def test_failed_or_missing_channel_and_test_results_cannot_activate(self):
        names = ('validation', 'release', 'debug', 'image', 'publish-release', 'publish-debug')
        success = dict.fromkeys(names, 'success')
        self.assertTrue(actual_activation(success))
        for name in names:
            for status in ('failure', 'cancelled', 'skipped'):
                with self.subTest(name=name, status=status):
                    self.assertFalse(actual_activation({**success, name: status}))
        self.assertFalse(actual_activation(success, deploy='false'))

    def test_build_only_does_not_disable_server_decision_or_make_a_client(self):
        run = workflow('ranked-release.yml')['jobs']['policy']['steps'][0]['run']
        code = run.split("python3 - <<'PY'\n", 1)[1].rsplit('\nPY', 1)[0]
        import os
        import tempfile
        from unittest.mock import patch
        for message, publish, deploy in [('[publish-r2] [deploy-lightsail]', True, True), ('[build only] [deploy-lightsail]', False, True), ('[publish-r2] [no-deploy]', True, False), ('[no-deploy]', False, False), ('ordinary', False, False)]:
            with tempfile.NamedTemporaryFile() as out, patch.dict(os.environ, {'MESSAGE':message, 'PUBLISH':'false', 'AUTO_DEPLOY':'false', 'EVENT':'push', 'GITHUB_OUTPUT':out.name}):
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
