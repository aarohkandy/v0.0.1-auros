#!/usr/bin/env python3
"""Versioned public publishing authority, readable with contents:read."""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import subprocess
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_service_config import exact_https_origin, configuration, site_origin


def settings(data):
    def pairs(items):
        value = {}
        for key, item in items:
            if key in value:
                raise ValueError('duplicate publishing setting')
            value[key] = item
        return value
    if len(data) > 4096:
        raise ValueError('oversized publishing settings')
    value = json.loads(data, object_pairs_hook=pairs)
    if (not isinstance(value, dict) or set(value) != {'version', 'domain', 'api_origin'}
            or type(value['version']) is not int or value['version'] != 1
            or not isinstance(value['domain'], str) or not isinstance(value['api_origin'], str)):
        raise ValueError('invalid publishing settings schema')
    domain = value['domain']
    if domain:
        if not re.fullmatch(r'[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?', domain) or '.' not in domain:
            raise ValueError('domain must be a canonical lower-case DNS name')
        exact_https_origin('https://' + domain, 'publishing domain')
    if value['api_origin']:
        exact_https_origin(value['api_origin'], 'publishing API origin')
    return value


def values(data, repository):
    value = settings(data)
    return {'AUROS_DOMAIN': value['domain'], 'AUROS_WISHES_URL': value['api_origin'] or
            f'https://auros-wishes.{repository.split("/")[0]}.workers.dev'}


def validate_domain(domain, installer_domain, enabled, cname):
    if cname and cname != domain:
        raise ValueError('Pages CNAME differs from versioned publishing domain')
    if enabled and installer_domain != domain:
        raise ValueError('installer AUROS_DOMAIN must match versioned publishing domain before activation')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--settings', type=Path, default=Path('tools/site/publish-settings.json'))
    parser.add_argument('--github-env', type=Path, required=True)
    parser.add_argument('--cloud-check', action='store_true')
    args = parser.parse_args()
    result = values(args.settings.read_bytes(), os.environ['GITHUB_REPOSITORY'])
    if args.cloud_check:
        env = dict(os.environ, **result)
        env.pop('AUROS_REPO', None)
        site = subprocess.check_output(['sh', 'tools/repo.sh', 'site'], env=env, text=True, timeout=10).strip()
        config = configuration(Path('cloud/wrangler.toml'), site, result['AUROS_WISHES_URL'])
        cname = Path('website/CNAME')
        validate_domain(result['AUROS_DOMAIN'], os.environ.get('INSTALLER_DOMAIN', ''), config['enabled'], cname.read_text().strip() if cname.exists() else '')
        result['BUILD_PUBLISHED_ORIGIN'] = site_origin(site)
    with args.github_env.open('a') as out:
        for key, value in result.items():
            out.write(f'{key}={value}\n')
