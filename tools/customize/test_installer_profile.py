from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.customize.installer_profile import (  # noqa: E402
    ProfileError,
    check_name,
    compile_profile,
)

PROFILES = ROOT / "profiles"


def define(header: str, name: str) -> str:
    match = re.search(rf"^#define {name} (.*)$", header, re.M)
    assert match, name
    return match.group(1)


class InstallerProfileTests(unittest.TestCase):
    def scratch(self, **files: str) -> Path:
        directory = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, directory, True)
        shutil.copy(PROFILES / "desktop.profile", directory)
        for name, text in files.items():
            (directory / f"{name.replace('_', '-')}.profile").write_text(text)
        return directory

    def test_every_checked_in_profile_compiles_to_its_own_name(self) -> None:
        for path in sorted(PROFILES.glob("*.profile")):
            name = path.stem
            with self.subTest(profile=name):
                header = compile_profile(name)
                self.assertEqual(define(header, "AUROS_PROFILE_ID"), f'"{name}"')

    def test_desktop_keeps_the_wizards_own_start(self) -> None:
        header = compile_profile("desktop")
        self.assertEqual(define(header, "AUROS_PROFILE_LOOK"), '""')
        self.assertEqual(define(header, "AUROS_PROFILE_THEME"), '""')
        self.assertEqual(define(header, "AUROS_PROFILE_LOOK_OFFERED"), "1")

    def test_a_profile_that_inherits_desktop_starts_on_its_own_look(self) -> None:
        header = compile_profile("office")
        self.assertEqual(define(header, "AUROS_PROFILE_LOOK"), '"rail"')
        self.assertEqual(define(header, "AUROS_PROFILE_THEME"), '"slate"')
        self.assertEqual(define(header, "AUROS_PROFILE_LOOK_OFFERED"), "1")
        header = compile_profile("revive")
        self.assertEqual(define(header, "AUROS_PROFILE_LOOK"), '"taskbar"')
        self.assertEqual(define(header, "AUROS_PROFILE_THEME"), '"ember"')

    def test_a_locked_profile_is_not_offered_a_look(self) -> None:
        header = compile_profile("school-kiosk")
        self.assertEqual(define(header, "AUROS_PROFILE_LOOK_OFFERED"), "0")
        self.assertEqual(define(header, "AUROS_PROFILE_LOOK"), '""')
        self.assertEqual(define(header, "AUROS_PROFILE_THEME"), '""')

    def test_a_new_profile_needs_only_its_file(self) -> None:
        directory = self.scratch(
            mahjong_lab='inherit="desktop"\nprofile_id="mahjong-lab"\n'
            'theme="moss"\nshell_archetype="tiles"\npackages_apps="gnome-mahjongg"\n'
        )
        header = compile_profile("mahjong-lab", directory)
        self.assertEqual(define(header, "AUROS_PROFILE_ID"), '"mahjong-lab"')
        self.assertEqual(define(header, "AUROS_PROFILE_LOOK"), '"tiles"')
        self.assertEqual(define(header, "AUROS_PROFILE_THEME"), '"moss"')

    def test_each_reason_to_lock_the_look_locks_it(self) -> None:
        cases = {
            "kiosk": 'kiosk_mode="yes"',
            "settings": 'allow_settings_change="no"',
            "theme": 'allow_theme_change="no"',
            "workbench": 'shell_archetype="workbench"',  # no card can show it
        }
        for label, line in cases.items():
            with self.subTest(label):
                directory = self.scratch(
                    locked=f'inherit="desktop"\nprofile_id="locked"\n{line}\n'
                )
                header = compile_profile("locked", directory)
                self.assertEqual(define(header, "AUROS_PROFILE_LOOK_OFFERED"), "0")

    def test_doubt_is_an_error_never_desktop(self) -> None:
        for name in ("", "Office", "../office", "office/", "a b", "a.b", "-x", "x-",
                     "a" * 33, "nosuch", "desktop\n"):
            with self.subTest(name=name), self.assertRaises(ProfileError):
                compile_profile(name)
        with self.assertRaises(ProfileError):
            check_name("Desktop")

    def test_the_file_name_and_profile_id_must_agree(self) -> None:
        directory = self.scratch(
            liar='inherit="desktop"\nprofile_id="desktop"\n'
        )
        with self.assertRaisesRegex(ProfileError, "profile_id"):
            compile_profile("liar", directory)

    def test_cycles_and_shell_expressions_are_refused(self) -> None:
        directory = self.scratch(
            a='inherit="b"\nprofile_id="a"\n', b='inherit="a"\nprofile_id="b"\n'
        )
        with self.assertRaisesRegex(ProfileError, "cycle"):
            compile_profile("a", directory)
        directory = self.scratch(
            sneaky='inherit="desktop"\nprofile_id="sneaky"\ntheme="$(id)"\n'
        )
        with self.assertRaisesRegex(ProfileError, "plain text"):
            compile_profile("sneaky", directory)

    def test_unknown_theme_or_shell_is_refused(self) -> None:
        directory = self.scratch(
            bad_theme='inherit="desktop"\nprofile_id="bad-theme"\ntheme="nosuchtheme"\n',
            bad_shell='inherit="desktop"\nprofile_id="bad-shell"\nshell_archetype="nosuch"\n',
        )
        with self.assertRaisesRegex(ProfileError, "theme"):
            compile_profile("bad-theme", directory)
        with self.assertRaisesRegex(ProfileError, "shell"):
            compile_profile("bad-shell", directory)

    def test_cli_leaves_no_header_behind_on_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "aurbridge-profile.h"
            script = str(ROOT / "tools/customize/installer_profile.py")
            ok = subprocess.run([sys.executable, script, "office", "--output", str(out)])
            self.assertEqual(ok.returncode, 0)
            self.assertTrue(out.exists())
            bad = subprocess.run(
                [sys.executable, script, "nosuch", "--output", str(out)],
                stderr=subprocess.PIPE,
            )
            self.assertEqual(bad.returncode, 2)
            self.assertFalse(out.exists())  # yesterday's valid header is gone

    # ── the C side: what the wizard and the engine do with the header ──
    def run_c(self, header: str | None, body: str) -> None:
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            if header is not None:
                (tmp / "aurbridge-profile.h").write_text(header, encoding="ascii")
            (tmp / "t.c").write_text(
                '#include <string.h>\n#include "recipe.h"\n#include "profile_image.h"\n'
                f"int main(void) {{\n{body}\nreturn 0; }}\n"
            )
            cc = [
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(tmp),
                "-I", str(ROOT / "src/aurbridge"), str(ROOT / "src/aurbridge/recipe.c"),
                str(ROOT / "src/aurbridge/profile_image.c"), str(tmp / "t.c"),
                "-o", str(tmp / "t"),
            ]
            built = subprocess.run(cc, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(built.returncode, 0, built.stderr.decode())
            ran = subprocess.run([str(tmp / "t")], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(ran.returncode, 0, f"check {ran.returncode} failed")

    def test_c_office_installer_installs_office(self) -> None:
        self.run_c(
            compile_profile("office"),
            r'''
char shell[32] = "dock", theme[32] = "sandstone", name[48];
if (ab_recipe_status() != AB_RECIPE_ABSENT) return 1;
if (strcmp(ab_recipe_image_profile(), "office")) return 2;
if (ab_profile_image_name(ab_recipe_image_profile(), name, sizeof name) || strcmp(name, "auros-office.img")) return 3;
if (!ab_profile_look_offered() || strcmp(ab_profile_look(), "rail") || strcmp(ab_profile_theme(), "slate")) return 4;
ab_profile_apply_look(shell, sizeof shell, theme, sizeof theme);
if (strcmp(shell, "dock") || strcmp(theme, "slate")) return 5;  /* her look, the profile's theme */
''',
        )

    def test_c_locked_installer_asks_nothing_and_writes_nothing(self) -> None:
        self.run_c(
            compile_profile("school-kiosk"),
            r'''
char shell[32] = "taskbar", theme[32] = "sandstone";
if (strcmp(ab_recipe_image_profile(), "school-kiosk")) return 1;
if (ab_profile_look_offered()) return 2;
ab_profile_apply_look(shell, sizeof shell, theme, sizeof theme);
if (shell[0] || theme[0]) return 3;
''',
        )

    def test_c_default_build_is_desktop_exactly_as_before(self) -> None:
        self.run_c(
            None,
            r'''
char shell[32] = "taskbar", theme[32] = "sandstone";
if (ab_recipe_status() != AB_RECIPE_ABSENT) return 1;
if (strcmp(ab_recipe_image_profile(), "desktop")) return 2;
if (!ab_profile_look_offered() || ab_profile_look()[0] || ab_profile_theme()[0]) return 3;
ab_profile_apply_look(shell, sizeof shell, theme, sizeof theme);
if (strcmp(shell, "taskbar") || strcmp(theme, "sandstone")) return 4;
''',
        )

    def test_c_a_tampered_profile_header_is_refused_not_desktop(self) -> None:
        for bad in ("../x", "Desktop", "", "a b", "-x"):
            with self.subTest(bad=bad):
                self.run_c(
                    f'#define AUROS_PROFILE_ID "{bad}"\n',
                    r'''
if (ab_recipe_status() != AB_RECIPE_INVALID) return 1;
if (ab_recipe_image_profile()[0]) return 2;   /* empty: the wizard refuses to start */
if (ab_profile_look_offered()) return 3;
''',
                )
        self.run_c(
            '#define AUROS_PROFILE_ID "office"\n#define AUROS_PROFILE_LOOK "../rail"\n',
            "if (ab_recipe_status() != AB_RECIPE_INVALID) return 1;",
        )


if __name__ == "__main__":
    unittest.main()
