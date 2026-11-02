#!/usr/bin/env python3
"""Exact Git-object dependency identity and bounded cloud publication proof."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))

SHA = re.compile(r'[0-9a-f]{40}')
HEX = re.compile(r'[0-9a-f]{64}')
EXACT = frozenset({'.github/workflows/cloud.yml', 'docs/wish-v1.schema.json',
    'src/customize/recipe-v1.schema.json', 'tools/site/publish-settings.json',
    'tools/site/publish_settings.py', 'tools/site/build_service_config.py',
    'tools/site/require_current_main.sh', 'tools/site/cloud_dependency.py', 'tools/repo.sh'})
PREFIXES = ('cloud/', 'rootfs/usr/share/auros/help/')
FIELDS = {'version', 'repository', 'source_sha', 'dependency_sha256', 'run_id',
          'run_attempt', 'deployment_nonce', 'api_origin', 'deployed_url'}


def dependency_digest(repository, sha, read):
    if not isinstance(sha, str) or not SHA.fullmatch(sha):
        raise ValueError('invalid cloud dependency source')
    tree = read(repository, f'git/trees/{sha}?recursive=1')
    rows = tree.get('tree')
    if tree.get('truncated') is not False or not isinstance(rows, list) or len(rows) > 50000:
        raise ValueError('cloud dependency tree incomplete or oversized')
    selected = {}
    for row in rows:
        path = row.get('path')
        if not isinstance(path, str) or len(path) > 4096 or path.startswith('/') or '..' in path.split('/'):
            raise ValueError('invalid dependency tree path')
        if path not in EXACT and not path.startswith(PREFIXES):
            continue
        if row.get('type') == 'tree':
            continue
        if (path in selected or row.get('type') != 'blob' or row.get('mode') not in ('100644', '100755')
                or not isinstance(row.get('sha'), str) or not SHA.fullmatch(row['sha'])):
            raise ValueError('invalid cloud dependency object')
        selected[path] = (row['mode'], row['sha'])
    if not EXACT.issubset(selected) or any(not any(p.startswith(prefix) for p in selected) for prefix in PREFIXES):
        raise ValueError('required cloud dependencies absent')
    digest = hashlib.sha256(b'auros-cloud-dependencies-v1\0')
    for path, (mode, oid) in sorted(selected.items()):
        digest.update(f'{path}\0{mode}\0{oid}\n'.encode())
    return digest.hexdigest()


def strict_json(raw):
    if len(raw) > 8192:
        raise ValueError('cloud proof oversized')
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate cloud proof field')
            result[key] = value
        return result
    return json.loads(raw, object_pairs_hook=pairs)


def validate_proof(value, repository, sha, digest, run_id, attempt):
    from build_service_config import exact_https_origin
    if not isinstance(value, dict) or set(value) != FIELDS:
        raise ValueError('invalid cloud proof schema')
    expected = dict(version=1, repository=repository, source_sha=sha,
                    dependency_sha256=digest, run_id=run_id, run_attempt=attempt)
    if any(type(value[k]) is not type(v) or value[k] != v for k, v in expected.items()):
        raise ValueError('cloud proof source or attempt mismatch')
    if not isinstance(value['deployment_nonce'], str) or not HEX.fullmatch(value['deployment_nonce']):
        raise ValueError('invalid deployment identity nonce')
    for key in ('api_origin', 'deployed_url'):
        if not isinstance(value[key], str) or exact_https_origin(value[key], key) != value[key]:
            raise ValueError('invalid deployment proof origin')
    return value


def unpack_proof(data):
    if len(data) > 32768:
        raise ValueError('cloud proof archive oversized')
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            entries = archive.infolist()
            if (len(entries) != 1 or entries[0].filename != 'cloud-publication-proof.json'
                    or entries[0].file_size > 8192 or entries[0].flag_bits & 1):
                raise ValueError('invalid cloud proof archive')
            return strict_json(archive.read(entries[0]))
    except (zipfile.BadZipFile, RuntimeError) as error:
        raise ValueError('invalid cloud proof archive') from error


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    def read(repository, suffix):
        result = subprocess.run(['gh', 'api', f'repos/{repository}/{suffix}'],
                                capture_output=True, timeout=25, check=True)
        if len(result.stdout) > 4 * 1024**2:
            raise ValueError('oversized GitHub tree response')
        return json.loads(result.stdout)
    repository, sha = os.environ['GITHUB_REPOSITORY'], os.environ['GITHUB_SHA']
    value = dict(version=1, repository=repository, source_sha=sha,
                 dependency_sha256=dependency_digest(repository, sha, read),
                 run_id=int(os.environ['GITHUB_RUN_ID']), run_attempt=int(os.environ['GITHUB_RUN_ATTEMPT']),
                 deployment_nonce=os.environ['DEPLOYMENT_NONCE'], api_origin=os.environ['AUROS_WISHES_URL'],
                 deployed_url=os.environ['DEPLOYED_URL'])
    validate_proof(value, repository, sha, value['dependency_sha256'], value['run_id'], value['run_attempt'])
    args.output.write_text(json.dumps(value, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
