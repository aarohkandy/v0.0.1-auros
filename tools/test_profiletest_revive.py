"""Reducers must reject false success, identity drift and unrelated OCR text."""
import copy
import gzip
import importlib.util
import json
from pathlib import Path
import tempfile
import struct
import zlib
import unittest

spec = importlib.util.spec_from_file_location('revive', Path(__file__).with_name('profiletest-revive.py'))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class ReviveTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        apps = {}
        for app, exe in [('firefox', '/usr/lib/firefox/firefox-bin'), ('abiword', '/usr/bin/abiword')]:
            unit = f'auros-profiletest-revive-{app}.service'
            identity = dict(pid=100, start_ticks=123, uid=1000, exe=exe, unit=unit,
                            cgroup='/user.slice/user-1000.slice/' + unit)
            apps[app] = dict(before=identity, after=copy.deepcopy(identity), stopped=True)
            def chunk(kind, data):
                return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
            png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1280, 800, 8, 2, 0, 0, 0))
            png += chunk(b'IDAT', zlib.compress(b'\0' * (800 * (1280 * 3 + 1)) )) + chunk(b'IEND', b'')
            (self.path / f'{app}.png').write_bytes(png)
        self.result = dict(schema='revive-runtime-v1', status='pass', apps=apps,
                           oom_before=0, oom_after=0, earlyoom_kills=0,
                           mem_kib=2000000, cleanup=True, reason='')
        self.browser = dict(value=probe.BROWSER, trusted_keys=len(probe.BROWSER), focused=True, trusted_submit=True)
        self.write_result()
        self.write_browser()
        (self.path / 'firefox.ocr.txt').write_text('revivebrowser\nreviveproof')
        (self.path / 'abiword.ocr.txt').write_text('reviveletter')
        self.document('reviveletter')
        for app in ('firefox', 'abiword'):
            probe.bind(self.path / f'{app}.ocr.txt')

    def document(self, text):
        (self.path / 'letter.abw').write_text(f'<abiword xmlns="http://www.abisource.com/awml.dtd"><section><p>{text}</p></section></abiword>')

    def write_result(self):
        (self.path / 'result.json').write_text(json.dumps(self.result))

    def write_browser(self):
        (self.path / 'browser.json').write_text(json.dumps(self.browser))

    def refused(self):
        with self.assertRaises((ValueError, OSError)):
            probe.reduce(self.path)

    def test_valid(self):
        self.assertIn('Gnumeric unproven', probe.reduce(self.path))

    def test_missing_app(self):
        del self.result['apps']['abiword']; self.write_result(); self.refused()

    def test_wrong_identity(self):
        original = copy.deepcopy(self.result)
        for field, value in [('exe', '/tmp/abiword'), ('unit', 'other.service'), ('uid', 0), ('start_ticks', 0), ('cgroup', '/different')]:
            with self.subTest(field=field):
                self.result = copy.deepcopy(original)
                for phase in ('before', 'after'):
                    self.result['apps']['abiword'][phase][field] = value
                self.write_result(); self.refused()

    def test_pid_reuse_or_restart(self):
        self.result['apps']['firefox']['after']['start_ticks'] += 1
        self.write_result(); self.refused()

    def test_unstopped(self):
        self.result['apps']['firefox']['stopped'] = False
        self.write_result(); self.refused()

    def test_oom_memory_cleanup_and_incomplete(self):
        original = copy.deepcopy(self.result)
        for field, value in [('oom_after', 1), ('earlyoom_kills', 1), ('mem_kib', 4000000), ('cleanup', False), ('status', 'fail'), ('reason', 'incomplete')]:
            with self.subTest(field=field):
                self.result = dict(original, **{field: value})
                self.write_result(); self.refused()

    def test_untrusted_or_unfocused_browser(self):
        original = dict(self.browser)
        for field, value in [('trusted_keys', 0), ('trusted_submit', False), ('focused', False), ('value', 'wrong')]:
            with self.subTest(field=field):
                self.browser = dict(original, **{field: value})
                self.write_browser(); self.refused()

    def test_document_contains_is_not_exact(self):
        for text in ('', 'prefixreviveletter', 'reviveletter suffix', 'reviveletter<p>extra</p>'):
            self.document(text); self.refused()

    def test_formatted_document(self):
        self.document('<c props="font-weight:bold">revive</c>letter')
        self.assertIn('PASS', probe.reduce(self.path))

    def test_external_dtd_not_fetched(self):
        path = self.path / 'letter.abw'
        path.write_text('<!DOCTYPE abiword SYSTEM "file:///does-not-exist"><abiword><section><p>reviveletter</p></section></abiword>')
        self.assertEqual(probe.document_text(path), 'reviveletter')

    def test_entity_expansion_rejected(self):
        (self.path / 'letter.abw').write_text('<!DOCTYPE abiword [<!ENTITY x "reviveletter">]><abiword><p>&x;</p></abiword>')
        self.refused()

    def test_ocr_exact_and_unique(self):
        for text in ('revivebrowser', 'revivebrowser reviveproofs', 'revivebrowser reviveproof reviveproof'):
            (self.path / 'firefox.ocr.txt').write_text(text); self.refused()

    def test_duplicate_json_keys(self):
        p = self.path / 'browser.json'
        p.write_text(p.read_text()[:-1] + ',"value":"reviveproof"}')
        self.refused()

    def test_missing_screenshot(self):
        (self.path / 'abiword.png').unlink(); self.refused()

    def test_truncated_png(self):
        (self.path / 'abiword.png').write_bytes(b'\x89PNG\r\n\x1a\n')
        self.refused()

    def test_ocr_binding_mismatch(self):
        (self.path / 'abiword.ocr.txt').write_text('reviveletter different screenshot')
        self.refused()

    def test_image_binding_mismatch(self):
        p = self.path / 'abiword.ocr.txt.binding.json'
        data = json.loads(p.read_text()); data['image_sha256'] = '0' * 64
        p.write_text(json.dumps(data)); self.refused()

    def test_retained_image_identity_and_bytes(self):
        image = self.path / 'image'; image.write_bytes(b'exact image')
        packed = self.path / 'auros-revive.img.gz'
        packed.write_bytes(gzip.compress(image.read_bytes()))
        manifest = probe.retain(image, self.path, 'a' * 40, 'owner/repo', 123)
        self.assertEqual(manifest['raw']['bytes'], 11)
        self.assertEqual(manifest['profile'], 'revive')
        self.assertEqual(manifest['image_run_id'], 123)
        self.assertEqual(manifest['source_sha'], 'a' * 40)
        self.assertEqual(json.loads((self.path / 'manifest.json').read_text()), manifest)
        packed.write_bytes(gzip.compress(b'different image'))
        with self.assertRaises(ValueError):
            probe.retain(image, self.path, 'a' * 40, 'owner/repo', 123)
        self.assertFalse((self.path / 'manifest.json').exists())

    def test_retention_refuses_invalid_source(self):
        for sha, repo, run in [('main', 'owner/repo', 1), ('a' * 40, 'owner/repo/extra', 1), ('a' * 40, 'owner/repo', 0)]:
            with self.assertRaises(ValueError):
                probe.retain('missing', self.path, sha, repo, run)

    def test_unknown_fields(self):
        self.result['unreviewed'] = True
        self.write_result(); self.refused()


if __name__ == '__main__':
    unittest.main()
