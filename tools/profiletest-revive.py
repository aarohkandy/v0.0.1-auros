#!/usr/bin/env python3
"""Bounded Revive Firefox/AbiWord probe and strict evidence reduction.

No Gnumeric, network browsing, performance, physical-PC or Windows-delivery
claim. Keyboard input is supplied by the host's QMP channel, never by DOM or
application automation. This program only seeds an EMPTY document.
"""
import argparse
import hashlib
import struct
import zlib
import http.server
import json
import os
from pathlib import Path
import pwd
import re
import secrets
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

STATE = Path('/var/lib/auros-profiletest/revive')
RUNTIME = Path('/tmp/auros-profiletest-revive')
HOST = Path('/run/auros-profiletest-host')
SENTINEL = 'reviveletter'
BROWSER = 'reviveproof'


def read_json(path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate evidence key')
            result[key] = value
        return result
    data = Path(path).read_bytes()
    if len(data) > 65536:
        raise ValueError('oversized evidence')
    return json.loads(data, object_pairs_hook=pairs)


def document_text(path):
    data = Path(path).read_bytes()
    if len(data) > 1048576 or b'<!ENTITY' in data:
        raise ValueError('unbounded or external document')
    root = ET.fromstring(data)
    local = lambda tag: tag.rsplit('}', 1)[-1]
    if local(root.tag) != 'abiword':
        raise ValueError('not an AbiWord document')
    return '\n'.join(''.join(p.itertext()) for p in root.iter()
                     if local(p.tag) == 'p')


def identity(pid, unit, group, executable, uid):
    proc = Path('/proc') / str(pid)
    actual = os.readlink(proc / 'exe')
    stat = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
    owner = (proc / 'status').read_text()
    cgroup = (proc / 'cgroup').read_text().splitlines()
    if actual != executable or f'0::{group}' not in cgroup:
        raise ValueError('process executable or unit cgroup mismatch')
    if not re.search(rf'^Uid:\s+{uid}\s+{uid}\s+{uid}\s+{uid}$', owner, re.M):
        raise ValueError('process owner mismatch')
    return dict(pid=pid, start_ticks=int(stat[19]), exe=actual, unit=unit,
                cgroup=group, uid=uid)


def screenshot_binding(ocr_path):
    ocr_path = Path(ocr_path)
    image = ocr_path.with_name(ocr_path.name.removesuffix('.ocr.txt') + '.png')
    data = image.read_bytes()
    if not data.startswith(b'\x89PNG\r\n\x1a\n') or len(data) > 5000000:
        raise ValueError('invalid screenshot')
    offset = 8
    chunks = []
    packed = bytearray()
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError('truncated PNG')
        length, kind = struct.unpack('>I4s', data[offset:offset+8])
        end = offset + 12 + length
        if end > len(data):
            raise ValueError('truncated PNG chunk')
        content = data[offset+8:end-4]
        crc = struct.unpack('>I', data[end-4:end])[0]
        if zlib.crc32(kind + content) & 0xffffffff != crc:
            raise ValueError('PNG checksum mismatch')
        chunks.append(kind)
        if kind == b'IHDR' and content != struct.pack('>IIBBBBB', 1280, 800, 8, 2, 0, 0, 0):
            raise ValueError('unexpected screenshot dimensions or encoding')
        if kind == b'IDAT':
            packed.extend(content)
        if kind == b'IEND' and content:
            raise ValueError('invalid PNG end')
        offset = end
    if chunks != [b'IHDR', b'IDAT', b'IEND']:
        raise ValueError('screenshot is not the host encoder output')
    decoder = zlib.decompressobj()
    raw = decoder.decompress(bytes(packed), 800 * (1280 * 3 + 1) + 1)
    if (len(raw) != 800 * (1280 * 3 + 1) or not decoder.eof or decoder.unused_data
            or decoder.unconsumed_tail or any(raw[y * (1280 * 3 + 1)] != 0 for y in range(800))):
        raise ValueError('invalid screenshot pixel stream')
    text = ocr_path.read_bytes()
    if len(text) > 65536:
        raise ValueError('oversized OCR')
    return {'image_sha256': hashlib.sha256(data).hexdigest(),
            'ocr_sha256': hashlib.sha256(text).hexdigest()}


def bind(path):
    value = screenshot_binding(path)
    Path(str(path) + '.binding.json').write_text(json.dumps(value) + '\n')


def visible(path):
    name = Path(path).name
    words = {'browser-ready.ocr.txt': ('revivebrowser',),
             'firefox.ocr.txt': ('revivebrowser', BROWSER),
             'abiword.ocr.txt': (SENTINEL,),
             'letter-ready.ocr.txt': ('abiword',)}.get(name)
    if words is None:
        raise ValueError('unknown visible application evidence')
    text = Path(path).read_text()
    if len(text) > 65536:
        raise ValueError('oversized OCR evidence')
    tokens = re.findall(r'[a-z]+', text.lower())
    if name == 'letter-ready.ocr.txt':
        if 'abiword' not in tokens:
            raise ValueError('AbiWord window not visible')
        return
    if any(tokens.count(word) != 1 for word in words):
        raise ValueError('exact unique visible words missing')


def reduce(directory):
    directory = Path(directory)
    result = read_json(directory / 'result.json')
    if set(result) != {'schema', 'status', 'apps', 'oom_before', 'oom_after',
                       'earlyoom_kills', 'mem_kib', 'cleanup', 'reason'}:
        raise ValueError('unexpected result fields')
    if result['schema'] != 'revive-runtime-v1' or result['status'] != 'pass' or result['reason']:
        raise ValueError('guest did not finish')
    if not 1800000 <= result['mem_kib'] <= 2097152:
        raise ValueError('probe was not run with the 2 GiB budget')
    if any(type(result[k]) is not int or result[k] < 0 for k in ('oom_before', 'oom_after', 'earlyoom_kills')):
        raise ValueError('invalid OOM counters')
    if result['oom_before'] != result['oom_after'] or result['earlyoom_kills'] != 0 or result['cleanup'] is not True:
        raise ValueError('OOM or incomplete cleanup')
    if set(result['apps']) != {'firefox', 'abiword'}:
        raise ValueError('missing application')
    for app, record in result['apps'].items():
        if set(record) != {'before', 'after', 'stopped'} or record['stopped'] is not True:
            raise ValueError('application lifecycle incomplete')
        before, after = record['before'], record['after']
        if before != after or set(before) != {'pid', 'start_ticks', 'exe', 'unit', 'cgroup', 'uid'}:
            raise ValueError('application identity changed')
        expected = {'firefox': ('/usr/lib/firefox/firefox-bin', '/opt/firefox/firefox-bin'),
                    'abiword': ('/usr/bin/abiword',)}[app]
        if before['exe'] not in expected or before['unit'] != f'auros-profiletest-revive-{app}.service':
            raise ValueError('wrong executable or unit')
        if not before['cgroup'].endswith('/' + before['unit']) or any(
                type(before[k]) is not int or before[k] <= 0 for k in ('pid', 'start_ticks', 'uid')):
            raise ValueError('invalid process identity')
    browser = read_json(directory / 'browser.json')
    if browser != {'value': BROWSER, 'trusted_keys': len(BROWSER), 'focused': True, 'trusted_submit': True}:
        raise ValueError('browser did not receive real focused keyboard input')
    if document_text(directory / 'letter.abw') != SENTINEL:
        raise ValueError('saved document has wrong text')
    for app, words in (('firefox', ('revivebrowser', BROWSER)), ('abiword', (SENTINEL,))):
        visible(directory / f'{app}.ocr.txt')
        ocr = directory / f'{app}.ocr.txt'
        if read_json(str(ocr) + '.binding.json') != screenshot_binding(ocr):
            raise ValueError('OCR does not belong to this screenshot')
    return 'PASS: Firefox keyboard submission and AbiWord saved document under 2 GiB; Gnumeric unproven'


def guest():
    STATE.mkdir(parents=True, exist_ok=True)
    RUNTIME.mkdir(mode=0o755, exist_ok=True)
    report = dict(schema='revive-runtime-v1', status='fail', apps={}, oom_before=-1,
                  oom_after=-2, earlyoom_kills=-1, mem_kib=0, cleanup=False, reason='unexpected exit')
    def say(message):
        with open('/dev/ttyS0', 'w') as serial:
            serial.write('\nPROFILETEST ' + message + '\n')
    def run(*args):
        return subprocess.check_output(args, text=True, timeout=25).strip()
    uid = os.stat('/run/auros').st_uid
    account = pwd.getpwuid(uid)
    env = ['env', 'HOME=' + account.pw_dir, 'XDG_RUNTIME_DIR=/run/user/' + str(uid),
           'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/' + str(uid) + '/bus']
    def user(*args):
        return run('runuser', '-u', account.pw_name, '--', *env, *args)
    def ctl(*args):
        return user('systemctl', '--user', *args)
    def oom():
        return int(dict(line.split() for line in Path('/proc/vmstat').read_text().splitlines())['oom_kill'])
    sequence = 0
    def action(verb):
        nonlocal sequence
        sequence += 1
        say(f'revive-do={sequence} {verb}')
        end = time.monotonic() + 100
        while time.monotonic() < end:
            if (HOST / f'failed-{sequence}').exists():
                raise ValueError('host action failed: ' + verb)
            if int((HOST / 'acks').read_text()) >= sequence:
                return
            time.sleep(.25)
        raise ValueError('host action timed out: ' + verb)
    active = {}
    server = None
    start = str(int(time.time()))
    def stop(app):
        pid, unit, group, exe = active[app]
        ctl('stop', unit)
        end = time.monotonic() + 10
        while time.monotonic() < end:
            group_path = Path('/sys/fs/cgroup' + group)
            processes = []
            if group_path.exists():
                for path in group_path.rglob('cgroup.procs'):
                    processes.extend(path.read_text().split())
            if not Path('/proc', str(pid)).exists() and not processes:
                del active[app]
                return
            time.sleep(.2)
        raise ValueError('application processes survived stop')
    def launch(app, exe, *args):
        unit = f'auros-profiletest-revive-{app}.service'
        expected_exe = str(Path(exe).with_name('firefox-bin')) if app == 'firefox' else exe
        user('systemd-run', '--user', '--quiet', '--unit=' + unit,
             '--property=Type=exec', '--property=TimeoutStopSec=5', '--', exe, *args)
        end = time.monotonic() + 20
        while time.monotonic() < end:
            pid = int(ctl('show', '-p', 'MainPID', '--value', unit))
            if pid:
                group = ctl('show', '-p', 'ControlGroup', '--value', unit)
                active[app] = (pid, unit, group, expected_exe)
                # The packaged Firefox launcher execs firefox-bin. Wait for
                # that exact binary, not for the transient launcher identity.
                if os.readlink(Path('/proc') / str(pid) / 'exe') != expected_exe:
                    time.sleep(.2)
                    continue
                record = identity(pid, unit, group, expected_exe, uid)
                report['apps'][app] = {'before': record, 'after': None, 'stopped': False}
                return
            time.sleep(.2)
        raise ValueError('application did not start')
    def finish(app):
        pid, unit, group, exe = active[app]
        report['apps'][app]['after'] = identity(pid, unit, group, exe, uid)
        stop(app)
        report['apps'][app]['stopped'] = True
    try:
        if any(STATE.iterdir()) or any(RUNTIME.iterdir()):
            raise ValueError('stale probe state refused')
        HOST.mkdir(parents=True, exist_ok=True)
        run('modprobe', '9pnet_virtio')
        run('mount', '-t', '9p', '-o', 'trans=virtio,version=9p2000.L,ro,cache=none', 'profiletest', str(HOST))
        report['mem_kib'] = int(re.search(r'^MemTotal:\s+(\d+)', Path('/proc/meminfo').read_text(), re.M)[1])
        report['oom_before'] = oom()
        token = secrets.token_hex(16)
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path != '/' + token:
                    self.send_error(404); return
                page = '''<!doctype html><meta charset="utf-8"><style>body{font:36px sans-serif;background:white}input{font-size:36px;width:80%%}</style><h1>revivebrowser</h1><input autofocus><script>
const input=document.querySelector('input');let keys=0;
input.addEventListener('keydown',e=>{if(e.isTrusted&&e.key.length===1)keys++;
if(e.key==='Enter'){e.preventDefault();fetch('/%s',{method:'POST',body:JSON.stringify({value:input.value,trusted_keys:keys,focused:document.activeElement===input,trusted_submit:e.isTrusted})});}});
</script>''' % token
                data = page.encode()
                self.send_response(200); self.send_header('Content-Type', 'text/html'); self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)
            def do_POST(self):
                length = int(self.headers.get('Content-Length', '0'))
                if self.path != '/' + token or not 0 < length < 1024 or (STATE / 'browser.json').exists():
                    self.send_error(400); return
                data = self.rfile.read(length)
                json.loads(data)
                pending = STATE / '.browser.json.pending'
                pending.write_bytes(data)
                pending.replace(STATE / 'browser.json')
                self.send_response(204); self.end_headers()
            def log_message(self, *_):
                pass
        server = http.server.HTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        profile = RUNTIME / 'browser'
        profile.mkdir()
        (profile / 'user.js').write_text('user_pref("browser.shell.checkDefaultBrowser", false);\nuser_pref("browser.aboutwelcome.enabled", false);\nuser_pref("browser.startup.homepage_override.mstone", "ignore");\n')
        run('chown', '-R', str(uid), str(RUNTIME))
        firefox = next((p for p in ('/usr/lib/firefox/firefox', '/opt/firefox/firefox') if Path(p).is_file()), None)
        if not firefox:
            raise ValueError('known Firefox executable unavailable')
        launch('firefox', firefox, '--no-remote', '--new-instance', '--profile', str(profile), f'http://127.0.0.1:{server.server_port}/{token}')
        action('revive-visible browser-ready')
        action('revive-type browser')
        action('key ret')
        action('revive-visible firefox')
        deadline = time.monotonic() + 10
        while not (STATE / 'browser.json').exists() and time.monotonic() < deadline:
            time.sleep(.2)
        if read_json(STATE / 'browser.json') != {'value': BROWSER, 'trusted_keys': len(BROWSER), 'focused': True, 'trusted_submit': True}:
            raise ValueError('browser input mismatch')
        finish('firefox')
        server.shutdown(); server.server_close(); server = None
        document = RUNTIME / 'letter.abw'
        document.write_text('<?xml version="1.0"?><abiword xmlns="http://www.abisource.com/awml.dtd" version="1.0"><section><p></p></section></abiword>')
        os.chown(document, uid, account.pw_gid)
        if document_text(document) != '':
            raise ValueError('document was not empty')
        launch('abiword', '/usr/bin/abiword', str(document))
        action('revive-visible letter-ready')
        action('revive-type letter')
        action('chord ctrl s')
        action('revive-visible abiword')
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                if document_text(document) == SENTINEL:
                    break
            except (ET.ParseError, OSError):
                pass  # A bounded retry while the application replaces its file.
            time.sleep(.5)
        if document_text(document) != SENTINEL:
            raise ValueError('saved AbiWord text differs')
        (STATE / 'letter.abw').write_bytes(document.read_bytes())
        finish('abiword')
        report['status'], report['reason'] = 'pass', ''
    except Exception as error:
        report['reason'] = str(error)[:500]
    finally:
        try:
            for app in list(active):
                stop(app)
            if server:
                server.shutdown(); server.server_close()
            report['cleanup'] = True
            report['oom_after'] = oom()
            earlyoom = run('journalctl', '-b', '-u', 'earlyoom.service', '--since=@' + start, '--no-pager', '-o', 'cat')
            report['earlyoom_kills'] = len(re.findall(r'(?i)(sending SIG(?:TERM|KILL)|killed process)', earlyoom))
            (STATE / 'earlyoom.log').write_text(earlyoom)
            if report['oom_before'] != report['oom_after'] or report['earlyoom_kills']:
                report['status'], report['reason'] = 'fail', 'OOM activity during applications'
        except Exception as error:
            report['status'], report['reason'] = 'fail', 'cleanup: ' + str(error)[:500]
        (STATE / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        os.sync()
        say('revive-probe=' + report['status'])
        say('done')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=('guest', 'reduce', 'visible', 'bind'))
    parser.add_argument('directory', nargs='?')
    args = parser.parse_args()
    if args.mode == 'guest':
        guest()
    elif args.mode == 'bind':
        bind(args.directory)
    elif args.mode == 'visible':
        try:
            visible(args.directory)
        except (ValueError, OSError):
            raise SystemExit(1)
    else:
        print(reduce(args.directory))
