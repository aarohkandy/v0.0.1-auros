#!/usr/bin/env python3
import copy
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parent))
import cloud_dependency as dep
import publish_guard as guard

SHA = 'a' * 40
REPO = 'owner/repo'

class DependencyTests(unittest.TestCase):
    def setUp(self):
        self.rows = [dict(path=p, type='blob', mode='100644', sha=SHA) for p in sorted(dep.EXACT | {'cloud/index.js', 'rootfs/usr/share/auros/help/index.html'})]
        self.tree = dict(truncated=False, tree=self.rows)
        self.proof = dict(version=1, repository=REPO, source_sha=SHA, dependency_sha256='d'*64,
            run_id=12, run_attempt=2, deployment_nonce='e'*64,
            api_origin='https://api.example', deployed_url='https://worker.example')
    def digest(self, tree=None):
        return dep.dependency_digest(REPO, SHA, lambda *_: tree or self.tree)
    def test_precise_dependencies_and_unrelated_change(self):
        baseline = self.digest()
        self.rows.append(dict(path='docs/notes.md', type='blob', mode='100644', sha='b'*40))
        self.assertEqual(baseline, self.digest())
        for path in dep.EXACT | {'cloud/index.js', 'rootfs/usr/share/auros/help/index.html'}:
            tree = copy.deepcopy(self.tree)
            next(row for row in tree['tree'] if row['path'] == path)['sha'] = 'c'*40
            self.assertNotEqual(baseline, self.digest(tree), path)
    def test_tree_order_and_modes(self):
        baseline = self.digest()
        self.rows.reverse()
        self.assertEqual(baseline, self.digest())
        self.rows[0]['mode'] = '100755'
        self.assertNotEqual(baseline, self.digest())
    def test_reject_incomplete_symlink_duplicate_missing(self):
        for mutate in (lambda t: t.update(truncated=True), lambda t: t['tree'].pop(),
                       lambda t: t['tree'].append(t['tree'][0]),
                       lambda t: t['tree'][0].update(mode='120000')):
            tree = copy.deepcopy(self.tree); mutate(tree)
            with self.assertRaises(ValueError): self.digest(tree)
    def valid(self, value):
        return dep.validate_proof(value, REPO, SHA, 'd'*64, 12, 2)
    def test_strict_source_attempt_schema_origin(self):
        self.assertEqual(self.valid(self.proof), self.proof)
        for key, value in [('run_attempt', 1), ('source_sha','b'*40), ('dependency_sha256','f'*64),
                           ('repository','other/repo'), ('version', True), ('api_origin','http://api.example'),
                           ('deployment_nonce','bad'), ('deployed_url','https://worker.example/path')]:
            with self.subTest(key=key), self.assertRaises(ValueError): self.valid(dict(self.proof, **{key:value}))
        with self.assertRaises(ValueError): self.valid(dict(self.proof, extra=True))
        with self.assertRaises(ValueError): dep.strict_json(b'{"version":1,"version":1}')
    def test_archive_bound_and_single_fixed_file(self):
        def archive(name, payload):
            stream=io.BytesIO()
            with zipfile.ZipFile(stream,'w') as z: z.writestr(name,payload)
            return stream.getvalue()
        self.assertEqual(dep.unpack_proof(archive('cloud-publication-proof.json',json.dumps(self.proof))), self.proof)
        for data in [b'x'*32769, archive('../proof.json','{}'), archive('cloud-publication-proof.json',' '*8193)]:
            with self.assertRaises(ValueError): dep.unpack_proof(data)
    def test_docs_only_other_sha_reuses_but_changed_inputs_do_not(self):
        candidate = dict(id=12, run_attempt=2, head_sha=SHA, head_branch='main',
            path='.github/workflows/cloud.yml', repository={'full_name':REPO}, event='push',
            status='completed', conclusion='success', run_started_at='2026-10-24T10:00:00Z')
        jobs=[dict(name='deploy',conclusion='success',steps=[dict(name=n,conclusion='success') for n in guard.CLOUD_STEPS])]
        target = copy.deepcopy(self.tree)
        target['tree'].append(dict(path='docs/notes.md', type='blob',mode='100644',sha='f'*40))
        def read(repo, suffix):
            if suffix == f'git/trees/{SHA}?recursive=1': return self.tree
            if suffix == f'git/trees/{"b"*40}?recursive=1': return target
            if 'workflows/' in suffix: return dict(workflow_runs=[candidate],total_count=1)
            if suffix in ('actions/runs/12','actions/runs/12/attempts/2'): return candidate
            if '/jobs?' in suffix: return dict(jobs=jobs,total_count=1)
            raise AssertionError(suffix)
        with patch.object(guard,'artifact_proof',return_value=self.proof) as proof:
            self.assertEqual(guard.wait_cloud(REPO,'b'*40,read=read,deadline_seconds=0),self.proof)
            self.assertEqual(proof.call_args.args[1]['head_sha'], SHA)
        for path in ('cloud/index.js','docs/wish-v1.schema.json','src/customize/recipe-v1.schema.json','tools/site/publish-settings.json'):
            target = copy.deepcopy(self.tree)
            next(row for row in target['tree'] if row['path']==path)['sha']='c'*40
            with self.subTest(path=path), patch.object(guard,'artifact_proof') as proof:
                with self.assertRaisesRegex(ValueError,'absent or timed out'):
                    guard.wait_cloud(REPO,'b'*40,read=read,deadline_seconds=0)
                proof.assert_not_called()

    def test_irrelevant_source_skips_metadata_but_remains_in_fingerprint(self):
        calls=[]
        def read(repo,suffix):
            calls.append(suffix)
            if 'workflows/' not in suffix: raise AssertionError('irrelevant metadata fetched')
            return dict(workflow_runs=[dict(id=11,head_sha='b'*40)],total_count=1)
        snapshot=guard.cloud_snapshot(REPO,'2026-01-01',read,lambda source:False)
        self.assertEqual(snapshot,[dict(id=11,head_sha='b'*40,_irrelevant=True)])
        self.assertEqual(len(calls),1)

    def test_irrelevant_source_change_or_deletion_invalidates_final_snapshot(self):
        candidate=dict(id=12,run_attempt=2,head_sha=SHA,head_branch='main',
            path='.github/workflows/cloud.yml',repository={'full_name':REPO},event='push',
            status='completed',conclusion='success',run_started_at='2026-10-24T10:00:00Z')
        jobs=[dict(name='deploy',conclusion='success',steps=[dict(name=n,conclusion='success') for n in guard.CLOUD_STEPS])]
        for changed in ([],[dict(id=11,head_sha='c'*40)]):
            inventory_calls=0
            metadata=[]
            def read(repo,suffix):
                nonlocal inventory_calls
                if 'workflows/' in suffix:
                    inventory_calls+=1
                    extra=[dict(id=11,head_sha='b'*40)] if inventory_calls==1 else changed
                    return dict(workflow_runs=[candidate]+extra,total_count=1+len(extra))
                metadata.append(suffix)
                if '/jobs?' in suffix:return dict(jobs=jobs,total_count=1)
                if suffix in ('actions/runs/12','actions/runs/12/attempts/2'):return candidate
                raise AssertionError(suffix)
            def digest(repo,source,reader):return 'd'*64 if source==SHA else 'f'*64
            with patch.object(guard,'dependency_digest',side_effect=digest), \
                 patch.object(guard,'artifact_proof',return_value=self.proof):
                with self.assertRaisesRegex(ValueError,'inventory or attempt changed'):
                    guard.wait_cloud(REPO,SHA,read=read,deadline_seconds=0)
            self.assertFalse(any('/runs/11' in suffix for suffix in metadata))

    def test_stable_total_with_empty_later_pages_is_incomplete(self):
        def read(repo, suffix):
            rows = [dict(id=i) for i in range(100)] if suffix.endswith('&page=1') else []
            return dict(workflow_runs=rows,total_count=101)
        with self.assertRaisesRegex(ValueError,'incomplete after bounded pagination'):
            guard.cloud_snapshot(REPO,'2026-01-01',read)

    def test_deleted_run_shifts_pagination_boundary(self):
        def read(repo, suffix):
            if suffix.endswith('&page=1'):
                return dict(workflow_runs=[dict(id=i) for i in range(100)],total_count=102)
            return dict(workflow_runs=[dict(id=101)],total_count=101)
        with self.assertRaisesRegex(ValueError,'total changed'):
            guard.cloud_snapshot(REPO,'2026-01-01',read)

    def test_other_run_rerun_after_snapshot_rejects_success(self):
        base=dict(id=12,run_attempt=1,head_sha=SHA,head_branch='main',
            path='.github/workflows/cloud.yml',repository={'full_name':REPO},event='push',
            status='completed',conclusion='success',run_started_at='2026-10-24T10:00:00Z')
        old=dict(base,id=11,run_started_at='2026-10-24T09:00:00Z')
        for row in (base,old): row['_attempt_started']=guard.datetime.fromisoformat(row['run_started_at'].replace('Z','+00:00'))
        rerun=dict(old,run_attempt=2,conclusion='failure',run_started_at='2026-10-24T11:00:00Z')
        jobs=[dict(name='deploy',conclusion='success',steps=[dict(name=n,conclusion='success') for n in guard.CLOUD_STEPS])]
        def read(repo,suffix):
            if '/jobs?' in suffix: return dict(jobs=jobs,total_count=1)
            return base
        with patch.object(guard,'dependency_digest',return_value='d'*64), \
             patch.object(guard,'cloud_snapshot',side_effect=[[base,old],[base,rerun]]), \
             patch.object(guard,'artifact_proof',return_value=self.proof):
            with self.assertRaisesRegex(ValueError,'inventory or attempt changed'):
                guard.wait_cloud(REPO,SHA,read=read,deadline_seconds=0)

    def test_rolling_window_and_overflow_fail_closed(self):
        calls = []
        def read(repo, suffix):
            calls.append(suffix)
            return dict(workflow_runs=[], total_count=1001)
        with patch.object(guard, 'dependency_digest', return_value='d'*64):
            with self.assertRaisesRegex(ValueError, '1000-run / 31-day'):
                guard.wait_cloud(REPO, SHA, read=read, deadline_seconds=0)
        self.assertIn('created=%3E%3D', calls[0])
        self.assertIn('branch=main', calls[0])

    def test_artifact_exact_attempt_metadata_and_payload(self):
        current = dict(id=12, run_attempt=2, head_sha=SHA)
        artifact = dict(id=99, name='cloud-publication-proof-2', expired=False,
                        size_in_bytes=500, workflow_run=dict(id=12, head_sha=SHA))
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w') as z:
            z.writestr('cloud-publication-proof.json', json.dumps(self.proof))
        def check(rows):
            with patch.object(guard, 'run', return_value=stream.getvalue()):
                return guard.artifact_proof(REPO, current, 'd'*64,
                    read=lambda *_: dict(artifacts=rows, total_count=len(rows)))
        self.assertEqual(check([artifact]), self.proof)
        for rows in [[], [artifact,artifact], [dict(artifact,expired=True)],
                     [dict(artifact,name='cloud-publication-proof-1')],
                     [dict(artifact,workflow_run=dict(id=12,head_sha='b'*40))]]:
            with self.subTest(rows=rows), self.assertRaises(ValueError): check(rows)

    def test_reuse_equal_dependency_and_newest_older_run_rerun_failure(self):
        base = dict(id=12, run_attempt=1, head_sha=SHA, head_branch='main',
            path='.github/workflows/cloud.yml', repository={'full_name':REPO}, event='push',
            status='completed', conclusion='success', run_started_at='2026-10-24T10:00:00Z')
        older = dict(base, id=11, head_sha='b'*40, run_attempt=2,
                     run_started_at='2026-10-24T11:00:00Z', conclusion='failure')
        jobs=[dict(name='deploy',conclusion='success',steps=[dict(name=n,conclusion='success') for n in guard.CLOUD_STEPS])]
        def read(repo, suffix):
            if 'workflows/' in suffix: return dict(workflow_runs=[base,older],total_count=2)
            if suffix in ('actions/runs/11', 'actions/runs/11/attempts/2'): return older
            if suffix in ('actions/runs/12', 'actions/runs/12/attempts/1'): return base
            if '/attempts/2/jobs?' in suffix: return dict(jobs=jobs,total_count=1)
            raise AssertionError(suffix)
        with patch.object(guard,'dependency_digest',return_value='d'*64), patch.object(guard,'artifact_proof') as proof:
            with self.assertRaisesRegex(ValueError,'failed'): guard.wait_cloud(REPO,'c'*40,read=read,deadline_seconds=0)
            proof.assert_not_called()
        older.update(conclusion='success')
        with patch.object(guard,'dependency_digest',return_value='d'*64), patch.object(guard,'artifact_proof',return_value=self.proof):
            self.assertEqual(guard.wait_cloud(REPO,'c'*40,read=read,deadline_seconds=0),self.proof)

if __name__ == '__main__': unittest.main()
