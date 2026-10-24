from __future__ import annotations

import json
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
    if (ab_recipe_baked() != 0) return 1;
    if (strcmp(ab_recipe_image_profile(), "desktop")) return 2;
    return 0;
}
''',
        )


if __name__ == "__main__":
    unittest.main()
