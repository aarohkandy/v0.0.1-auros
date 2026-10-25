#!/usr/bin/env python3
"""Adversarial publication ordering/evidence checks; no network or deployment."""
import copy
import base64
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('publish_guard', Path(__file__).with_name('publish_guard.py'))
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)
SHA = 'a' * 40
REPOSITORY = 'owner/repo'


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.run = dict(id=12, run_attempt=1, head_sha=SHA, head_branch='main',
                        path='.github/workflows/cloud.yml', repository={'full_name': REPOSITORY},
                        event='push', status='completed', conclusion='success')
        self.jobs = [dict(name='deploy', conclusion='success', steps=[
            dict(name=name, conclusion='success') for name in guard.CLOUD_STEPS])]
        self.current = dict(source_sha=SHA, settings_sha256='e' * 64, variables={'AUROS_DOMAIN': '', 'AUROS_WISHES_URL': ''})
        self.state = dict(self.current, tree_sha256='b' * 64, release={'id': 1})

    def test_versioned_authority_uses_contents_not_variables_api(self):
        data = b'{"version":1,"domain":"","api_origin":""}'
        calls = []
        def read(repo, suffix):
            calls.append(suffix)
            if suffix == 'git/ref/heads/main':
                return {'object': {'sha': SHA}}
            self.assertEqual(suffix, 'contents/tools/site/publish-settings.json?ref=' + SHA)
            return {'type': 'file', 'encoding': 'base64', 'content': base64.b64encode(data).decode()}
        current = guard.authority(REPOSITORY, read=read)
        self.assertEqual(current['source_sha'], SHA)
        self.assertEqual(current['variables']['AUROS_WISHES_URL'], 'https://auros-wishes.owner.workers.dev')
        self.assertFalse(any('actions/variables' in call for call in calls))

    def test_strict_settings_schema_and_origins(self):
        from publish_settings import settings
        self.assertEqual(settings(b'{"version":1,"domain":"","api_origin":""}')['domain'], '')
        hostile = [b'{"version":true,"domain":"","api_origin":""}',
                   b'{"version":1,"domain":"","api_origin":"","extra":1}',
                   b'{"version":1,"domain":"","domain":"evil.example","api_origin":""}']
        for domain, origin in [('UPPER.example', ''), ('site.example:443', ''), ('-bad.example', ''),
                               ('', 'http://worker.example'), ('', 'https://worker.example/'),
                               ('', 'https://user@worker.example'), ('', 'https://worker.example/path')]:
            hostile.append(json.dumps(dict(version=1, domain=domain, api_origin=origin)).encode())
        for raw in hostile:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                settings(raw)

    def test_cname_and_installer_domain_alignment(self):
        from publish_settings import validate_domain
        validate_domain('', '', True, '')
        validate_domain('site.example', 'site.example', True, 'site.example')
        for args in [('site.example', '', True, ''), ('site.example', 'other.example', True, ''),
                     ('site.example', 'site.example', False, 'wrong.example')]:
            with self.subTest(args=args), self.assertRaises(ValueError):
                validate_domain(*args)
        # Incident disable never waits for installer-domain migration.
        validate_domain('site.example', '', False, '')

    def test_unreadable_authority_fails_not_disabled(self):
        with self.assertRaises(ValueError):
            guard.authority(REPOSITORY, read=lambda *_: {'error': 'denied'})

    def test_exact_success(self):
        self.assertTrue(guard.cloud_run_status(self.run, self.jobs, SHA, REPOSITORY))

    def test_source_repository_workflow_branch_event_mismatch(self):
        for key, value in [('head_sha', 'c' * 40), ('head_branch', 'feature'),
                           ('path', '.github/workflows/test.yml'), ('event', 'pull_request'),
                           ('repository', {'full_name': 'attacker/repo'})]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                guard.cloud_run_status(dict(self.run, **{key: value}), self.jobs, SHA, REPOSITORY)

    def test_running_is_not_success(self):
        self.assertFalse(guard.cloud_run_status(dict(self.run, status='in_progress'), None, SHA, REPOSITORY))

    def test_failed_cloud_never_reuses_old_success(self):
        for conclusion in ('failure', 'cancelled', 'timed_out', 'skipped', None):
            with self.subTest(conclusion=conclusion), self.assertRaises(ValueError):
                guard.cloud_run_status(dict(self.run, conclusion=conclusion), self.jobs, SHA, REPOSITORY)

    def test_skipped_secrets_probe_only_and_missing_step(self):
        with self.assertRaises(ValueError):
            guard.cloud_run_status(self.run, [], SHA, REPOSITORY)
        for required in guard.CLOUD_STEPS:
            jobs = copy.deepcopy(self.jobs)
            for step in jobs[0]['steps']:
                if step['name'] == required:
                    step['conclusion'] = 'skipped'
            with self.subTest(required=required), self.assertRaises(ValueError):
                guard.cloud_run_status(self.run, jobs, SHA, REPOSITORY)
        jobs = copy.deepcopy(self.jobs); jobs[0]['steps'].pop()
        with self.assertRaises(ValueError):
            guard.cloud_run_status(self.run, jobs, SHA, REPOSITORY)

    def test_absent_cloud_evidence_times_out_without_publish(self):
        with self.assertRaisesRegex(ValueError, 'absent or timed out'):
            guard.wait_cloud(REPOSITORY, SHA, read=lambda *_: {'workflow_runs': [], 'total_count': 0}, deadline_seconds=0)

    def test_latest_attempt_not_old_success(self):
        calls = []
        def read(repo, suffix):
            calls.append(suffix)
            if 'workflows/' in suffix:
                return {'workflow_runs': [{'id': 11}, {'id': 12}], 'total_count': 2}
            if suffix == 'actions/runs/12':
                return dict(self.run, conclusion='failure', run_attempt=2)
            if suffix == 'actions/runs/12/attempts/2/jobs?per_page=100':
                return {'jobs': self.jobs, 'total_count': 1}
            raise AssertionError(suffix)
        with self.assertRaises(ValueError):
            guard.wait_cloud(REPOSITORY, SHA, read=read, deadline_seconds=0)
        self.assertTrue(any('/attempts/2/jobs' in item for item in calls))

    def verify(self, current=None, staged=b'config', expected=b'config', tree=None, release=None):
        guard.verify_current(self.state, current or self.current, staged, expected,
                             tree or self.state['tree_sha256'], release or self.state['release'])

    def test_current_equal_authority(self):
        self.verify()

    def test_stale_main_configuration_release_or_tree(self):
        for kwargs in ({'current': dict(self.current, source_sha='c' * 40)},
                       {'current': dict(self.current, variables={'AUROS_DOMAIN': 'changed.example'})},
                       {'staged': b'activated'}, {'tree': 'd' * 64}, {'release': {'id': 2}}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                self.verify(**kwargs)

    def test_tree_digest_catches_replacement_and_symlink(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp); page = directory / 'index.html'; page.write_text('one')
            before = guard.tree_digest(directory)
            page.write_text('two')
            self.assertNotEqual(before, guard.tree_digest(directory))
            (directory / 'link').symlink_to(page)
            with self.assertRaises(ValueError):
                guard.tree_digest(directory)

    def test_disabled_guard_never_waits_for_cloud(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp); staged = directory / 'site'; staged.mkdir()
            (staged / 'build-service.json').write_bytes(guard.config_bytes({'enabled': False}))
            state = dict(self.state, repository=REPOSITORY, tree_sha256=guard.tree_digest(staged))
            path = directory / 'state.json'; path.write_text(json.dumps(state))
            with patch.object(guard, 'authority', return_value=self.current), \
                 patch.object(guard, 'run', return_value=b'https://owner.github.io/repo/'), \
                 patch.object(guard, 'configuration', return_value={'enabled': False}), \
                 patch.object(guard, 'latest_release', return_value=state['release']), \
                 patch.object(guard, 'api', return_value={'cname': None}), \
                 patch.object(guard, 'wait_cloud') as cloud:
                guard.guard(REPOSITORY, staged, path)
            cloud.assert_not_called()

    def test_enabled_guard_rechecks_authority_after_cloud(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp); staged = directory / 'site'; staged.mkdir()
            value = {'enabled': True, 'api_origin': 'https://builds.example'}
            (staged / 'build-service.json').write_bytes(guard.config_bytes(value))
            state = dict(self.state, repository=REPOSITORY, tree_sha256=guard.tree_digest(staged))
            path = directory / 'state.json'; path.write_text(json.dumps(state))
            changed = dict(self.current, source_sha='c' * 40)
            with patch.object(guard, 'authority', side_effect=[self.current, changed]), \
                 patch.object(guard, 'run', return_value=b'https://owner.github.io/repo/'), \
                 patch.object(guard, 'configuration', return_value=value), \
                 patch.object(guard, 'latest_release', return_value=state['release']), \
                 patch.object(guard, 'api', return_value={'cname': None}), \
                 patch.object(guard, 'wait_cloud') as cloud, self.assertRaises(ValueError):
                guard.guard(REPOSITORY, staged, path)
            cloud.assert_called_once_with(REPOSITORY, SHA)


if __name__ == '__main__':
    unittest.main()
