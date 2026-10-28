#!/usr/bin/env python3
"""Compile the shipped staging command-line builder and check both boot paths.

The normal boot menu is the independent policy oracle: staging must keep its
kernel protections when it becomes the first desktop without a reboot.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BootArgs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="auros-bootargs-")
        cls.addClassCleanup(cls.tmp.cleanup)
        tmp = Path(cls.tmp.name)
        (tmp / "main.c").write_text(r'''
#include "bootargs.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    char out[1024];
    if (argc == 2) {
        const char *extra = ab_stage_test_args();
        printf("%s", extra ? extra : "");
        return 0;
    }
    if (argc != 5) return 3;
    int rc = ab_stage_cmdline(out, (size_t)atoi(argv[4]), argv[2], argv[3], atoi(argv[1]));
    printf("%s", out);
    return rc ? 2 : 0;
}
''')
        cls.exe = str(tmp / "bootargs")
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "src/aurbridge"), str(tmp / "main.c"),
                        str(ROOT / "src/aurbridge/bootargs.c"), "-o", cls.exe], check=True)
        cls.winexe = str(tmp / "bootargs-windows-policy")
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-D_WIN32", "-I", str(ROOT / "src/aurbridge"), str(tmp / "main.c"),
                        str(ROOT / "src/aurbridge/bootargs.c"), "-o", cls.winexe], check=True)

    def args_for(self, direct=0, profile="desktop", extra="", cap=1024):
        return subprocess.run([self.exe, str(direct), profile, extra, str(cap)],
                              capture_output=True, text=True)

    def test_both_paths_match_normal_kernel_policy(self):
        menu = (ROOT / "rootfs/usr/lib/auros/grub.cfg.in").read_text()
        normal = next(line.split() for line in menu.splitlines()
                      if line.strip().startswith("linux ") and "quiet splash" in line)
        policy = normal[normal.index("init_on_alloc=1"):]
        self.assertGreaterEqual(len(policy), 8)
        for direct in (0, 1):
            with self.subTest(direct=direct):
                result = self.args_for(direct)
                self.assertEqual(result.returncode, 0, result.stderr)
                args = result.stdout.split()
                for option in policy:
                    self.assertIn(option, args)
                self.assertIn("aurstage.install", args)
                self.assertIn("aurstage.profile=desktop", args)
                self.assertIn("gpt", args)
                self.assertEqual(result.stdout.startswith("initrd="), bool(direct))

    def test_test_options_precede_mandatory_policy(self):
        result = self.args_for(extra="aurstage.min_gb=1 console=ttyS0,115200n8 panic=0")
        self.assertEqual(result.returncode, 0)
        self.assertLess(result.stdout.index("panic=0"), result.stdout.index("panic=10"))

    def test_all_profiles(self):
        for profile in (ROOT / "profiles").glob("*.profile"):
            self.assertEqual(self.args_for(profile=profile.stem).returncode, 0, profile.stem)

    def test_truncation_is_refused_and_output_cleared(self):
        for cap, extra in ((1, ""), (32, ""), (1024, "x" * 1024)):
            result = self.args_for(cap=cap, extra=extra)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")

    def test_grub_injection_is_refused(self):
        for value in ("\nreboot", "x;reboot", "$root", "x'", 'x"', "x{", "x}", "x\\y", "x\t"):
            for field in ("profile", "extra"):
                result = self.args_for(**{field: value})
                self.assertEqual(result.returncode, 2, (field, value))
                self.assertEqual(result.stdout, "")

    def test_empty_profile_refused(self):
        self.assertEqual(self.args_for(profile="").returncode, 2)

    def test_windows_never_accepts_environment_kernel_overrides(self):
        for extra in ("init=/bin/sh", "rd.break", "apparmor=0", "lockdown=none",
                      "module_blacklist=ext4", "aurstage.die_at=commit"):
            env = dict(os.environ, AURBRIDGE_STAGING_KARGS=extra)
            result = subprocess.run([self.winexe, "environment"], env=env,
                                    capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "")
            result = subprocess.run([self.winexe, "0", "desktop", extra, "1024"],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
        result = subprocess.run([self.winexe, "0", "desktop", "", "1024"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0)
        self.assertIn("lockdown=integrity", result.stdout.split())


if __name__ == "__main__":
    unittest.main()
