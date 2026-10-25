#!/usr/bin/env python3
"""Stage current-main Pages bytes; refuse stale authority or unproved activation.

Only versioned public settings are read through the contents API. No secret values or API response
bodies are printed. All network/subprocess operations have explicit bounds.
"""
import argparse
import base64
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

REPO = re.compile(r'[A-Za-z0-9-]+/[A-Za-z0-9_.-]+')
SHA = re.compile(r'[0-9a-f]{40}')
VARIABLES = ('AUROS_DOMAIN', 'AUROS_WISHES_URL')
CLOUD_STEPS = {'Deploy', 'Worker secrets (stdin only, never echoed)',
               'Custom delivery live preflight', 'Live end to end, with its logs captured'}


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


def wait_cloud(repository, sha, read=api, deadline_seconds=600):
    end = time.monotonic() + deadline_seconds
    while True:
        runs = read(repository, f'actions/workflows/cloud.yml/runs?head_sha={sha}&per_page=100')
        rows = runs.get('workflow_runs')
        if not isinstance(rows, list) or runs.get('total_count', 0) > 100:
            raise ValueError('cloud workflow inventory incomplete')
        if rows:
            latest = max(rows, key=lambda row: row['id'])
            current = read(repository, f'actions/runs/{latest["id"]}')
            attempt = current.get('run_attempt')
            if type(attempt) is not int or attempt < 1:
                raise ValueError('cloud attempt unavailable')
            jobs = None
            if current.get('status') == 'completed':
                response = read(repository, f'actions/runs/{latest["id"]}/attempts/{attempt}/jobs?per_page=100')
                jobs = response.get('jobs')
                if response.get('total_count', 0) > 100:
                    raise ValueError('cloud job inventory incomplete')
            if cloud_run_status(current, jobs, sha, repository):
                return current['id']
        if time.monotonic() >= end:
            raise ValueError('same-source cloud evidence absent or timed out; run cloud on current main first')
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
        wait_cloud(repository, state['source_sha'])
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
