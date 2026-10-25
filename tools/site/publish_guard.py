#!/usr/bin/env python3
"""Stage current-main Pages bytes; refuse stale authority or unproved activation.

Only versioned public settings are read through the contents API. No secret values or API response
bodies are printed. All network/subprocess operations have explicit bounds.
"""
import argparse
import base64
from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_service_config import configuration
from publish_settings import settings, values, validate_domain
from cloud_dependency import dependency_digest, unpack_proof, validate_proof

REPO = re.compile(r'[A-Za-z0-9-]+/[A-Za-z0-9_.-]+')
SHA = re.compile(r'[0-9a-f]{40}')
VARIABLES = ('AUROS_DOMAIN', 'AUROS_WISHES_URL')
CLOUD_STEPS = {'Deploy', 'Worker secrets (stdin only, never echoed)',
               'Custom delivery live preflight', 'Live end to end, with its logs captured',
               'Public endpoint identifies the deployed Worker', 'Bind reusable cloud publication proof'}


def run(*args, env=None, timeout=30):
    value = subprocess.run(args, env=env, capture_output=True, timeout=timeout, check=False)
    if value.returncode:
        raise ValueError(f'{args[0]} failed; publication refused')
    return value.stdout


def api(repository, suffix):
    data = run('gh', 'api', f'repos/{repository}/{suffix}', timeout=25)
    if len(data) > 4 * 1024 * 1024:
        raise ValueError('oversized GitHub response')
    return json.loads(data)


def authority(repository, read=api):
    ref = read(repository, 'git/ref/heads/main')
    sha = ref.get('object', {}).get('sha', '')
    if not SHA.fullmatch(sha):
        raise ValueError('current main SHA unavailable')
    content = read(repository, f'contents/tools/site/publish-settings.json?ref={sha}')
    if content.get('encoding') != 'base64' or content.get('type') != 'file':
        raise ValueError('versioned publishing authority unavailable')
    encoded = content.get('content', '')
    if not isinstance(encoded, str) or len(encoded) > 8192:
        raise ValueError('invalid publishing authority encoding')
    raw = base64.b64decode(''.join(encoded.split()), validate=True)
    settings(raw)
    return {'source_sha': sha, 'settings_sha256': hashlib.sha256(raw).hexdigest(),
            'variables': values(raw, repository)}


def environment(repository, state):
    env = dict(os.environ)
    env['GITHUB_REPOSITORY'] = repository
    env.pop('AUROS_REPO', None)
    env['AUROS_DOMAIN'] = state['variables']['AUROS_DOMAIN']
    env['AUROS_WISHES_URL'] = state['variables']['AUROS_WISHES_URL'] or f'https://auros-wishes.{repository.split("/")[0]}.workers.dev'
    return env


def config_bytes(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def tree_digest(directory):
    digest = hashlib.sha256()
    paths = sorted(Path(directory).rglob('*'))
    if not paths or len(paths) > 2000:
        raise ValueError('invalid staged tree size')
    total = 0
    for path in paths:
        if path.is_symlink():
            raise ValueError('staged tree contains symlink')
        if path.is_dir():
            continue
        data = path.read_bytes()
        total += len(data)
        if total > 64 * 1024 * 1024:
            raise ValueError('staged tree exceeds bound')
        digest.update(path.relative_to(directory).as_posix().encode() + b'\0')
        digest.update(hashlib.sha256(data).digest())
    return digest.hexdigest()


def cloud_run_status(run_value, jobs, sha, repository):
    if (run_value.get('head_sha') != sha or run_value.get('head_branch') != 'main'
            or run_value.get('path') != '.github/workflows/cloud.yml'
            or run_value.get('repository', {}).get('full_name') != repository
            or run_value.get('event') not in ('push', 'workflow_dispatch')):
        raise ValueError('cloud evidence source mismatch')
    if run_value.get('status') != 'completed':
        return False
    if run_value.get('conclusion') != 'success':
        raise ValueError('same-source cloud workflow failed')
    if not isinstance(jobs, list):
        raise ValueError('cloud job evidence missing')
    deploy = [job for job in jobs if job.get('name') == 'deploy']
    if len(deploy) != 1 or deploy[0].get('conclusion') != 'success':
        raise ValueError('cloud deployment job did not succeed')
    steps = deploy[0].get('steps', [])
    for name in CLOUD_STEPS:
        matching = [step for step in steps if step.get('name') == name]
        if len(matching) != 1 or matching[0].get('conclusion') != 'success':
            raise ValueError('cloud deployment/live gates missing, skipped or failed')
    return True


def artifact_proof(repository, current, digest, read=api):
    attempt = current['run_attempt']
    response = read(repository, f'actions/runs/{current["id"]}/artifacts?per_page=100')
    rows = response.get('artifacts')
    if not isinstance(rows, list) or response.get('total_count') != len(rows) or len(rows) > 100:
        raise ValueError('cloud proof artifact inventory incomplete')
    matches = [row for row in rows if row.get('name') == f'cloud-publication-proof-{attempt}']
    if len(matches) != 1:
        raise ValueError('exact-attempt cloud proof absent or ambiguous')
    artifact = matches[0]
    if (artifact.get('expired') is not False or type(artifact.get('id')) is not int
            or type(artifact.get('size_in_bytes')) is not int
            or not 0 < artifact['size_in_bytes'] <= 32768
            or artifact.get('workflow_run', {}).get('id') != current['id']
            or artifact.get('workflow_run', {}).get('head_sha') != current['head_sha']):
        raise ValueError('cloud proof artifact metadata invalid')
    data = run('gh', 'api', f'repos/{repository}/actions/artifacts/{artifact["id"]}/zip', timeout=25)
    return validate_proof(unpack_proof(data), repository, current['head_sha'], digest,
                          current['id'], attempt)


def cloud_snapshot(repository, since, read, matches=None):
    rows = []
    expected_total = None
    for page in range(1, 11):
        response = read(repository, f'actions/workflows/cloud.yml/runs?branch=main&created=%3E%3D{since}&per_page=100&page={page}')
        batch, total = response.get('workflow_runs'), response.get('total_count')
        if not isinstance(batch, list) or len(batch) > 100 or type(total) is not int or not 0 <= total <= 1000:
            raise ValueError('cloud workflow inventory incomplete or exceeds 1000-run / 31-day bound')
        if expected_total is None:
            expected_total = total
        elif total != expected_total:
            raise ValueError('cloud inventory total changed during pagination')
        rows.extend(batch)
        if len(rows) >= total:
            if len(rows) != total:
                raise ValueError('cloud workflow inventory changed during read')
            break
    if len(rows) != expected_total:
        raise ValueError('cloud workflow inventory incomplete after bounded pagination')
    if any(type(row.get('id')) is not int for row in rows) or len({row['id'] for row in rows}) != len(rows):
        raise ValueError('cloud workflow inventory invalid or repeated a run')
    attempts = []
    for row in rows:
        source = row.get('head_sha')
        if not isinstance(source, str) or not SHA.fullmatch(source):
            raise ValueError('cloud inventory source unavailable')
        if matches is not None and not matches(source):
            # Retain every run/source in the full inventory fingerprint, but an
            # immutable, different dependency set cannot supply reusable evidence.
            attempts.append(dict(id=row['id'], head_sha=source, _irrelevant=True))
            continue
        current = read(repository, f'actions/runs/{row["id"]}')
        attempt = current.get('run_attempt')
        if current.get('id') != row['id'] or current.get('head_sha') != source or type(attempt) is not int or attempt < 1:
            raise ValueError('cloud latest attempt unavailable')
        exact = read(repository, f'actions/runs/{row["id"]}/attempts/{attempt}')
        if any(exact.get(k) != current.get(k) for k in ('id', 'run_attempt', 'head_sha', 'status', 'conclusion')):
            raise ValueError('cloud attempt source mismatch')
        try:
            started = datetime.fromisoformat(exact['run_started_at'].replace('Z', '+00:00'))
            if started.tzinfo is None:
                raise ValueError('cloud attempt start timezone missing')
        except (KeyError, TypeError, AttributeError, ValueError) as error:
            raise ValueError('cloud attempt start evidence missing') from error
        exact['_attempt_started'] = started
        attempts.append(exact)
    return attempts


def wait_cloud(repository, sha, read=api, deadline_seconds=600):
    end = time.monotonic() + deadline_seconds
    original_read = read
    def read(repo, suffix):
        if deadline_seconds > 0 and time.monotonic() >= end:
            raise ValueError('cloud proof inventory exceeded publication deadline')
        return original_read(repo, suffix)
    target = dependency_digest(repository, sha, read)
    cache = {sha: target}
    def matches(source):
        if source not in cache:
            cache[source] = dependency_digest(repository, source, read)
        return cache[source] == target
    # GitHub permits reruns only within 30 days; one extra day covers this
    # workflow's bounded execution. https://docs.github.com/en/actions/how-tos/manage-workflow-runs/re-run-workflows-and-jobs
    since = (datetime.now(timezone.utc) - timedelta(days=31)).strftime('%Y-%m-%dT%H:%M:%SZ')
    while True:
        attempts = cloud_snapshot(repository, since, read, matches)
        latest = None
        # Inspect attempts newest first; older candidates cannot override the first
        # matching dependency set, even when that newest attempt failed.
        for row in sorted((item for item in attempts if not item.get('_irrelevant')),
                          key=lambda row: (row['_attempt_started'], row['id']), reverse=True):
            source = row.get('head_sha')
            if source not in cache:
                cache[source] = dependency_digest(repository, source, read)
            if cache[source] == target:
                latest = row
                break
        if latest is not None:
            current = read(repository, f'actions/runs/{latest["id"]}')
            attempt = current.get('run_attempt')
            if type(attempt) is not int or attempt < 1 or attempt != latest['run_attempt']:
                raise ValueError('cloud attempt changed after freshness ordering')
            source = current.get('head_sha')
            if source != latest['head_sha']:
                raise ValueError('cloud run source changed')
            jobs = None
            if current.get('status') == 'completed':
                response = read(repository, f'actions/runs/{latest["id"]}/attempts/{attempt}/jobs?per_page=100')
                jobs = response.get('jobs')
                if not isinstance(jobs, list) or response.get('total_count') != len(jobs) or len(jobs) > 100:
                    raise ValueError('cloud job inventory incomplete')
            if cloud_run_status(current, jobs, source, repository):
                proof = artifact_proof(repository, current, target, read)
                # Re-read all eligible attempts, including nonselected old run IDs:
                # a newly failed rerun must never leave an older success usable.
                refreshed = cloud_snapshot(repository, since, read, matches)
                keys = ('id', 'run_attempt', 'head_sha', 'status', 'conclusion',
                        'run_started_at', 'head_branch', 'path', 'event')
                def identity(snapshot):
                    return sorted(tuple(row.get(key) for key in keys) for row in snapshot)
                if identity(refreshed) != identity(attempts):
                    raise ValueError('cloud inventory or attempt changed while validating proof')
                return proof
        if time.monotonic() >= end:
            raise ValueError('matching cloud evidence absent or timed out; run cloud on current main first')
        time.sleep(min(10, max(0, end - time.monotonic())))


def latest_release(repository, read=api):
    release = read(repository, 'releases/latest')
    if release.get('draft') is not False or release.get('prerelease') is not False:
        raise ValueError('latest public release unavailable')
    assets = [item for item in release.get('assets', []) if item.get('name') == 'AurOS-Setup.exe']
    if len(assets) != 1 or type(assets[0].get('size')) is not int or not 0 < assets[0]['size'] <= 256 * 1024**2:
        raise ValueError('latest installer asset unavailable or oversized')
    asset = assets[0]
    url = asset.get('browser_download_url', '')
    from urllib.parse import quote
    tag = release.get('tag_name')
    if not isinstance(tag, str) or url != f'https://github.com/{repository}/releases/download/{quote(tag, safe="")}/AurOS-Setup.exe':
        raise ValueError('latest installer URL does not identify this repository')
    return {'release_id': release['id'], 'asset_id': asset['id'], 'tag': tag,
            'url': url, 'size': asset['size'], 'digest': asset.get('digest')}


def prepare(repository, directory, state_path):
    state = authority(repository)
    if hashlib.sha256(Path('tools/site/publish-settings.json').read_bytes()).hexdigest() != state['settings_sha256']:
        raise ValueError('checkout settings differ from current main')
    checkout = run('git', 'rev-parse', 'HEAD').decode().strip()
    if checkout != state['source_sha']:
        raise ValueError('checkout is no longer current main; rerun Pages on main')
    env = environment(repository, state)
    run('sh', 'tools/site/stamp.sh', 'website', str(directory), env=env)
    site_url = run('sh', 'tools/repo.sh', 'site', env=env).decode().strip()
    value = configuration(Path('cloud/wrangler.toml'), site_url, env['AUROS_WISHES_URL'])
    cname = api(repository, 'pages').get('cname') or ''
    validate_domain(env['AUROS_DOMAIN'], os.environ.get('INSTALLER_DOMAIN', ''), value['enabled'], cname)
    local_cname = directory / 'CNAME'
    if local_cname.exists():
        validate_domain(env['AUROS_DOMAIN'], os.environ.get('INSTALLER_DOMAIN', ''), value['enabled'], local_cname.read_text().strip())
    (directory / 'build-service.json').write_bytes(config_bytes(value))
    release = latest_release(repository)
    with tempfile.TemporaryDirectory() as temp:
        installer = Path(temp) / 'setup.exe'
        run('curl', '--fail', '--silent', '--show-error', '--location', '--max-time', '120',
            '--max-filesize', str(256 * 1024**2), '--output', str(installer), release['url'], timeout=130)
        data = installer.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if len(data) != release['size'] or (release['digest'] is not None and release['digest'] != 'sha256:' + digest):
            raise ValueError('latest installer bytes disagree with release metadata')
    (directory / 'release.json').write_bytes(config_bytes({'tag': release['tag'], 'file': 'AurOS-Setup.exe',
        'sha256': digest, 'run': os.environ['GITHUB_RUN_ID']}))
    state.update(repository=repository, release=release, tree_sha256=tree_digest(directory))
    state_path.write_text(json.dumps(state) + '\n')
    # Versioned public values only; validation above prevents GITHUB_ENV line injection.
    with open(os.environ['GITHUB_ENV'], 'a') as output:
        for name in VARIABLES:
            output.write(f'{name}={env[name]}\n')
    print('Prepared current-main site and verified the actual latest installer hash.')


def verify_current(state, current, staged_bytes, expected_bytes, tree_sha256, release):
    if any(state.get(key) != current.get(key) for key in ('source_sha', 'variables', 'settings_sha256')):
        raise ValueError('main or versioned publishing settings changed; rerun Pages on main')
    if staged_bytes != expected_bytes or tree_sha256 != state['tree_sha256']:
        raise ValueError('staged site changed after its browser check')
    if state['release'] != release:
        raise ValueError('latest release changed during publication; rerun Pages on main')


def guard(repository, directory, state_path):
    state = json.loads(state_path.read_text())
    if state.get('repository') != repository:
        raise ValueError('publishing repository changed')
    def verify():
        current = authority(repository)
        env = environment(repository, current)
        site_url = run('sh', 'tools/repo.sh', 'site', env=env).decode().strip()
        expected = configuration(Path('cloud/wrangler.toml'), site_url, env['AUROS_WISHES_URL'])
        validate_domain(env['AUROS_DOMAIN'], os.environ.get('INSTALLER_DOMAIN', ''), expected['enabled'], api(repository, 'pages').get('cname') or '')
        verify_current(state, current, (directory / 'build-service.json').read_bytes(),
                       config_bytes(expected), tree_digest(directory), latest_release(repository))
        return expected, env, site_url
    expected, env, site_url = verify()
    if expected['enabled']:
        proof = wait_cloud(repository, state['source_sha'])
        if proof['api_origin'] != env['AUROS_WISHES_URL']:
            raise ValueError('cloud proof API origin differs from current publishing authority')
        run('node', 'cloud/test/deployment-preflight.mjs', proof['api_origin'],
            proof['source_sha'], proof['deployment_nonce'], proof['deployed_url'], timeout=90)
        from urllib.parse import urlsplit
        parsed = urlsplit(site_url)
        run('node', 'cloud/test/build-preflight.mjs', env['AUROS_WISHES_URL'], 'on',
            f'{parsed.scheme}://{parsed.netloc}', timeout=60)
    # Re-read mutable authority after any cloud wait, immediately before deploy.
    verify()
    print('Publication guard passed: current source, versioned settings, tested bytes and release match.')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=('prepare', 'guard'))
    parser.add_argument('--directory', required=True, type=Path)
    parser.add_argument('--state', required=True, type=Path)
    args = parser.parse_args()
    repository = os.environ['GITHUB_REPOSITORY']
    if not REPO.fullmatch(repository):
        parser.error('invalid repository')
    try:
        (prepare if args.mode == 'prepare' else guard)(repository, args.directory, args.state)
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        print('Pages publication refused: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
