#!/usr/bin/env python3

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools import fcitx_profile_snapshot as subject


PROFILE = """[Groups/0]
Name=Default
Default Layout=fr
DefaultIM=pinyin

[Groups/0/Items/0]
Name=keyboard-fr
Layout=

[Groups/0/Items/1]
Name=pinyin
Layout=

[GroupOrder]
0=Default
"""


class SnapshotTests(unittest.TestCase):
    def test_profile_identity_parses_nul_entries_without_newline_forgery(self):
        expected = {"uid": "1000", "home": "/home/auros", "xdg_config_home": "absent"}
        self.assertEqual(subject.profile_identity(
            b"HOME=/home/auros\0OTHER=x\nHOME=/wrong\0", 1000, 1000, "/home/auros"), expected)
        for environ in (
                b"HOME=/wrong\0OTHER=x\nHOME=/home/auros\0",
                b"HOME=/home/auros\0HOME=/home/auros\0",
                b"HOME=/home/auros\0XDG_CONFIG_HOME=/wrong\0",
                b"HOME=/home/auros\0OTHER=x\nXDG_CONFIG_HOME=/home/auros/.config\0XDG_CONFIG_HOME=/wrong\0"):
            with self.subTest(environ=environ), self.assertRaises(subject.SnapshotError):
                subject.profile_identity(environ, 1000, 1000, "/home/auros")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def write(self, content: bytes = PROFILE.encode()) -> Path:
        path = self.root / "profile"
        path.write_bytes(content)
        return path

    def test_known_profile_preserves_order_and_safe_tokens(self):
        value = subject.snapshot(self.write(), "runtime", "start")
        subject.validate(value, "runtime", "start")
        self.assertEqual(value["raw_sha256"], subject.digest(PROFILE.encode()))
        self.assertEqual([item["line"] for item in value["records"]], list(range(1, 16)))
        self.assertEqual(value["records"][0]["section"], "Groups/0")
        self.assertEqual(value["records"][2]["key"], "Default Layout")
        self.assertEqual(value["records"][2]["value"], "fr")

    def test_comments_and_unknown_tokens_are_hashed_only(self):
        secret = "private group label and comment"
        data = f"# {secret}\n[Custom/{secret}]\nUnknown={secret}\n".encode()
        value = subject.snapshot(self.write(data), "template", "end")
        encoded = json.dumps(value)
        self.assertNotIn(secret, encoded)
        self.assertEqual(value["records"][0]["kind"], "comment")
        self.assertNotIn("section", value["records"][1])
        self.assertNotIn("key", value["records"][2])
        self.assertNotIn("value", value["records"][2])
        subject.validate(value, "template", "end")

    def test_duplicate_section_and_key_are_refused(self):
        for content in (
            b"[Groups/0]\nName=Default\n[Groups/0]\n",
            b"[Groups/0]\nName=Default\nName=pinyin\n",
        ):
            with self.subTest(content=content):
                with self.assertRaises(subject.SnapshotError):
                    subject.snapshot(self.write(content), "runtime", "start")

    def test_unsafe_files_and_text_are_refused(self):
        target = self.write()
        link = self.root / "profile-link"
        link.symlink_to(target)
        cases = [
            (link, "symlink"),
            (self.write(b"x" * (subject.MAX_BYTES + 1)), "oversize"),
            (self.write(b"[Groups/0]\nName=bad\0value\n"), "nul"),
            (self.write(b"[Groups/0]\nName=\xff\n"), "utf8"),
        ]
        for path, label in cases:
            with self.subTest(label=label):
                with self.assertRaises(subject.SnapshotError):
                    subject.snapshot(path, "runtime", "start")

    def test_validator_rejects_unknown_cleartext_and_extra_fields(self):
        value = subject.snapshot(self.write(), "runtime", "start")
        value["records"][1]["value"] = "private"
        with self.assertRaises(subject.SnapshotError):
            subject.validate(value, "runtime", "start")
        value = subject.snapshot(self.write(), "runtime", "start")
        value["unexpected"] = True
        with self.assertRaises(subject.SnapshotError):
            subject.validate(value, "runtime", "start")

    def test_validator_rejects_duplicate_structural_records(self):
        value = subject.snapshot(self.write(), "runtime", "start")
        value["records"].insert(1, dict(value["records"][0]))
        for number, record in enumerate(value["records"], 1):
            record["line"] = number
        value["line_count"] += 1
        value["raw_bytes"] += value["records"][0]["raw_bytes"]
        with self.assertRaises(subject.SnapshotError):
            subject.validate(value, "runtime", "start")

    def test_cli_writes_atomically_and_validates_exact_phase(self):
        source = self.write()
        output = self.root / "snapshot.json"
        script = Path(subject.__file__)
        created = subprocess.run(
            [sys.executable, script, "create", "--source", "runtime", "--phase", "start",
             "--input", source, "--output", output], check=False, capture_output=True, text=True)
        self.assertEqual(created.returncode, 0, created.stderr)
        valid = subprocess.run(
            [sys.executable, script, "validate", "--source", "runtime", "--phase", "start", output],
            check=False, capture_output=True, text=True)
        self.assertEqual(valid.returncode, 0, valid.stderr)
        wrong = subprocess.run(
            [sys.executable, script, "validate", "--source", "runtime", "--phase", "end", output],
            check=False, capture_output=True, text=True)
        self.assertNotEqual(wrong.returncode, 0)

    def boundary_values(self):
        values = {}
        for source in ("runtime", "template"):
            for round_number in (1, 2):
                for phase in ("start", "end"):
                    values[f"{source}{round_number}{phase}"] = subject.snapshot(
                        self.write(), source, phase)
        return values

    def test_comparison_allows_only_cross_boot_comments_and_blanks(self):
        values = self.boundary_values()
        changed = b"[Groups/0]\n# group name\nName=Default\n# layout\nDefault Layout=fr\n# input method name\nDefaultIM=pinyin\n\n[Groups/0/Items/0]\n# name\nName=keyboard-fr\n# layout\nLayout=\n\n[Groups/0/Items/1]\n# name\nName=pinyin\n# layout\nLayout=\n\n[GroupOrder]\n0=Default\n\n"
        for phase in ("start", "end"):
            values[f"runtime2{phase}"] = subject.snapshot(
                self.write(changed), "runtime", phase)
        result = subject.compare_boundaries(
            values, (values["runtime1start"]["raw_sha256"],
                     values["runtime2start"]["raw_sha256"]))
        self.assertEqual(result["cross_boot_raw_equal"], "no")
        self.assertEqual(result["cross_boot_comment_blank_only"], "yes")

    def test_comparison_rejects_every_other_boundary_change(self):
        mutations = {
            "within_round": ("runtime1end", PROFILE + "\n"),
            "template_comment": ("template2end", "# comment\n" + PROFILE),
            "value": ("runtime2start", PROFILE.replace("DefaultIM=pinyin", "DefaultIM=keyboard-fr")),
            "order": ("runtime2start", PROFILE.replace("Name=Default\nDefault Layout=fr", "Default Layout=fr\nName=Default")),
            "inline_comment": ("runtime2start", PROFILE.replace("DefaultIM=pinyin", "DefaultIM=pinyin # comment")),
        }
        for label, (key, content) in mutations.items():
            with self.subTest(label=label):
                values = self.boundary_values()
                source = "runtime" if key.startswith("runtime") else "template"
                phase = "start" if key.endswith("start") else "end"
                values[key] = subject.snapshot(self.write(content.encode()), source, phase)
                hashes = (values["runtime1start"]["raw_sha256"],
                          values["runtime2start"]["raw_sha256"])
                with self.assertRaises(subject.SnapshotError):
                    subject.compare_boundaries(values, hashes)

    def test_comparison_rejects_process_hash_mismatch(self):
        values = self.boundary_values()
        with self.assertRaisesRegex(subject.SnapshotError, "process evidence"):
            subject.compare_boundaries(values, ("0" * 64, values["runtime2start"]["raw_sha256"]))


if __name__ == "__main__":
    unittest.main()
