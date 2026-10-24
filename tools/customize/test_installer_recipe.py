from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.customize.installer_recipe import (  # noqa: E402
    InstallerRecipeError,
    compile_installer_recipe,
)


def recipe(base: str = "desktop", preferences: dict[str, str] | None = None) -> bytes:
    value: dict[str, object] = {
        "schema": "org.auros.recipe",
        "version": 1,
        "base": base,
    }
    if preferences is not None:
        value["preferences"] = preferences
    return json.dumps(value, separators=(",", ":")).encode("ascii")


class InstallerRecipeTests(unittest.TestCase):
    def test_compiles_resolved_desktop_values_deterministically(self) -> None:
        raw = recipe("desktop", {"look": "dock", "theme": "moss"})
        first = compile_installer_recipe(raw, "desktop")
        second = compile_installer_recipe(raw, "desktop")
        self.assertEqual(first, second)
        self.assertIn('#define AUROS_RECIPE_BASE "desktop"', first)
        self.assertIn('#define AUROS_RECIPE_LOOK "dock"', first)
        self.assertIn('#define AUROS_RECIPE_THEME "moss"', first)
        self.assertRegex(first, r'#define AUROS_RECIPE_ID "sha256:[0-9a-f]{64}"')

    def test_omitted_preferences_use_contract_defaults(self) -> None:
        header = compile_installer_recipe(recipe(), "desktop")
        self.assertIn('#define AUROS_RECIPE_LOOK "rail"', header)
        self.assertIn('#define AUROS_RECIPE_THEME "sandstone"', header)

    def test_trusted_image_profile_must_match(self) -> None:
        with self.assertRaisesRegex(InstallerRecipeError, "does not match"):
            compile_installer_recipe(recipe("desktop"), "revive")

    def test_profiles_without_delivery_evidence_fail_closed(self) -> None:
        for base in ("multilingual", "office", "revive", "school-kiosk"):
            with self.subTest(base=base), self.assertRaisesRegex(
                InstallerRecipeError, "not enabled"
            ):
                compile_installer_recipe(recipe(base), base)

    def test_unknown_fields_and_injection_strings_are_rejected(self) -> None:
        attacks = (
            b'{"schema":"org.auros.recipe","version":1,"base":"desktop",'
            b'"command":"calc.exe"}',
            b'{"schema":"org.auros.recipe","version":1,"base":"desktop",'
            b'"preferences":{"look":"rail\\\"; system(\\\"calc\\\")"}}',
        )
        for raw in attacks:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                compile_installer_recipe(raw, "desktop")

    def test_failed_cli_removes_stale_generated_header(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            raw = temporary / "recipe.json"
            output = temporary / "aurbridge-recipe.h"
            raw.write_bytes(recipe("revive"))
            output.write_text("stale", encoding="ascii")
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/customize/installer_recipe.py"),
                    str(raw),
                    "--image-profile",
                    "revive",
                    "--output",
                    str(output),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(result.returncode, 2)
            self.assertFalse(output.exists())

    def _compile_and_run(self, generated: str | None, source: str) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            if generated is not None:
                (temporary / "aurbridge-recipe.h").write_text(generated, encoding="ascii")
            harness = temporary / "harness.c"
            harness.write_text(source, encoding="ascii")
            binary = temporary / "recipe-test"
            result = subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(temporary),
                    "-I",
                    str(ROOT / "src/aurbridge"),
                    str(ROOT / "src/aurbridge/recipe.c"),
                    str(harness),
                    "-o",
                    str(binary),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            ran = subprocess.run(
                [str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
            )
            self.assertEqual(ran.returncode, 0, ran.stderr.decode())

    def test_helper_preserves_generic_defaults_without_recipe(self) -> None:
        self._compile_and_run(
            None,
            r'''
#include <string.h>
#include "recipe.h"
int main(void) {
    if (ab_recipe_status() != AB_RECIPE_ABSENT) return 1;
    if (ab_recipe_baked() != 0) return 1;
    if (strcmp(ab_recipe_image_profile(), "desktop")) return 2;
    if (*ab_recipe_id_for_selection("desktop", "taskbar", "sandstone")) return 3;
    return 0;
}
''',
        )

    def test_helper_identity_only_matches_unchanged_final_choices(self) -> None:
        generated = compile_installer_recipe(
            recipe("desktop", {"look": "tiles", "theme": "nocturne"}), "desktop"
        )
        self._compile_and_run(
            generated,
            r'''
#include <string.h>
#include "recipe.h"
int main(void) {
    const ab_baked_recipe *r = ab_recipe_baked();
    if (ab_recipe_status() != AB_RECIPE_VALID) return 1;
    if (!r || strcmp(r->look, "tiles") || strcmp(r->theme, "nocturne")) return 1;
    if (!*ab_recipe_id_for_selection("desktop", "tiles", "nocturne")) return 2;
    if (*ab_recipe_id_for_selection("desktop", "dock", "nocturne")) return 3;
    if (*ab_recipe_id_for_selection("desktop", "tiles", "moss")) return 4;
    return 0;
}
''',
        )

    def test_helper_rejects_a_tampered_generated_header(self) -> None:
        generated = """\
#define AUROS_RECIPE_PRESENT 1
#define AUROS_RECIPE_BASE "desktop"
#define AUROS_RECIPE_LOOK "../rail"
#define AUROS_RECIPE_THEME "sandstone"
#define AUROS_RECIPE_ID "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
"""
        self._compile_and_run(
            generated,
            r'''
#include <string.h>
#include "recipe.h"
int main(void) {
    if (ab_recipe_status() != AB_RECIPE_INVALID) return 1;
    if (ab_recipe_baked() != 0) return 2;
    if (*ab_recipe_image_profile()) return 3;
    return 0;
}
''',
        )

    def test_personalized_choice_receipt_is_required_and_exact(self) -> None:
        generated = compile_installer_recipe(
            recipe("desktop", {"look": "tiles", "theme": "moss"}), "desktop"
        )
        expected_id = re.search(r'AUROS_RECIPE_ID "([^"]+)"', generated)
        self.assertIsNotNone(expected_id)
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            (temporary / "aurbridge-recipe.h").write_text(generated, encoding="ascii")
            harness = temporary / "harness.c"
            harness.write_text(
                r'''
#include <stdio.h>
#include <string.h>
#include "phases.h"
int ab_test_write_choices(const ab_choice *, int, const char *, ab_say,
                          void *, char *, size_t);
static int fail_put;
static char receipt[2048];
int plat_file_put(const char *path, const void *buf, size_t n,
                  char *why, size_t wn) {
    (void)path;
    if (fail_put) {
        snprintf(why, wn, "injected write failure");
        return -1;
    }
    if (n >= sizeof receipt) return -1;
    memcpy(receipt, buf, n); receipt[n] = 0;
    return 0;
}
static void quiet(const char *line, void *unused) {(void)line; (void)unused;}
int main(void) {
    ab_choice c = {0}; char why[256] = {0};
    strcpy(c.profile, "desktop"); strcpy(c.shell_archetype, "tiles");
    strcpy(c.theme, "moss");
    if (ab_test_write_choices(&c, 0, "/esp", quiet, 0, why, sizeof why)) return 1;
    if (!strstr(receipt, "recipe_id=@RID@\n")) return 2;
    strcpy(c.shell_archetype, "dock"); receipt[0] = 0;
    if (ab_test_write_choices(&c, 0, "/esp", quiet, 0, why, sizeof why)) return 3;
    if (strstr(receipt, "recipe_id=")) return 4;
    fail_put = 1; strcpy(c.shell_archetype, "tiles");
    if (ab_test_write_choices(&c, 0, "/esp", quiet, 0, why, sizeof why) != -1) return 5;
    if (!strstr(why, "Nothing will be installed")) return 6;
    strcpy(c.shell_archetype, "dock"); why[0] = 0;
    if (ab_test_write_choices(&c, 0, "/esp", quiet, 0, why, sizeof why) != -1) return 7;
    return 0;
}
'''.replace("@RID@", expected_id.group(1)),
                encoding="ascii",
            )
            common = [
                "cc", "-std=c11", "-ffunction-sections", "-fdata-sections",
                "-I", str(temporary), "-I", str(ROOT / "src/aurbridge"),
                "-I", str(ROOT / "src/aurstage"),
            ]
            objects = []
            for source, extra in (
                (ROOT / "src/aurbridge/phases.c", ["-DAUROS_CHOICE_TEST"]),
                (ROOT / "src/aurbridge/recipe.c", []),
                (harness, []),
            ):
                obj = temporary / (source.stem + ".o")
                result = subprocess.run(
                    common + extra + ["-c", str(source), "-o", str(obj)],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                objects.append(obj)
            binary = temporary / "choice-receipt-test"
            dead_strip = ["-Wl,-dead_strip"] if sys.platform == "darwin" else ["-Wl,--gc-sections"]
            result = subprocess.run(
                ["cc", *dead_strip, *(str(obj) for obj in objects), "-o", str(binary)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            ran = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(ran.returncode, 0, ran.stderr.decode())


if __name__ == "__main__":
    unittest.main()
