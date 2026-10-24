from __future__ import annotations

import json
import re
import subprocess
import sys
import unittest
from pathlib import Path

from src.customize.recipe import (
    BASES,
    MAX_RECIPE_BYTES,
    MAX_RECIPE_DEPTH,
    PUBLIC_LOOKS,
    RECIPE_SCHEMA,
    THEMES,
    RecipeError,
    catalogue,
    compile_recipe,
    parse_recipe,
)


ROOT = Path(__file__).resolve().parents[3]
CLI = ROOT / "src" / "customize" / "recipe.py"


def valid(**changes):
    recipe = {"schema": RECIPE_SCHEMA, "version": 1, "base": "desktop"}
    recipe.update(changes)
    return recipe


def assignment(path: Path, key: str) -> str | None:
    pattern = re.compile(rf'^{re.escape(key)}="([^"]*)"')
    for line in path.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            return match.group(1)
    return None


def inherited_profile(profile_id: str) -> dict[str, str]:
    path = ROOT / "profiles" / f"{profile_id}.profile"
    parent = assignment(path, "inherit")
    values = inherited_profile(parent) if parent else {}
    for key in ("profile_id", "profile_name", "theme", "shell_archetype"):
        value = assignment(path, key)
        if value is not None:
            values[key] = value
    return values


class RecipeTests(unittest.TestCase):
    def assert_error(self, code, value):
        with self.assertRaises(RecipeError) as caught:
            compile_recipe(value)
        self.assertEqual(caught.exception.code, code)
        return caught.exception

    def test_minimal_recipe_resolves_profile_defaults(self):
        plan = compile_recipe(valid())
        self.assertEqual(plan["base_profile"], "desktop")
        self.assertEqual(plan["requested_preferences"], {})
        self.assertEqual(plan["resolved_preferences"], {"look": "rail", "theme": "sandstone"})
        self.assertEqual(plan["profile_overrides"], {})
        self.assertEqual(plan["delivery"]["state"], "validated-not-built")
        self.assertFalse(plan["delivery"]["artifact_created"])

    def test_allowlisted_preferences_compile_to_internal_keys(self):
        plan = compile_recipe(
            valid(preferences={"look": "dock", "theme": "nocturne"})
        )
        self.assertEqual(
            plan["profile_overrides"],
            {"shell_archetype": "dock", "theme": "nocturne"},
        )

    def test_input_key_order_does_not_change_plan(self):
        left = parse_recipe(
            b'{"schema":"org.auros.recipe","version":1,"base":"desktop",'
            b'"preferences":{"look":"dock","theme":"moss"}}'
        )
        right = parse_recipe(
            b'{"preferences":{"theme":"moss","look":"dock"},"base":"desktop",'
            b'"version":1,"schema":"org.auros.recipe"}'
        )
        self.assertEqual(compile_recipe(left), compile_recipe(right))

    def test_omitted_and_explicit_default_remain_distinguishable(self):
        omitted = compile_recipe(valid())
        explicit = compile_recipe(valid(preferences={"look": "rail"}))
        self.assertEqual(omitted["resolved_preferences"], explicit["resolved_preferences"])
        self.assertNotEqual(omitted["requested_preferences"], explicit["requested_preferences"])
        self.assertNotEqual(omitted["recipe_id"], explicit["recipe_id"])

    def test_unknown_top_level_field_is_rejected(self):
        error = self.assert_error("unknown_field", valid(command="id"))
        self.assertEqual(error.path, "$.command")

    def test_unknown_preference_is_rejected(self):
        error = self.assert_error(
            "unknown_field", valid(preferences={"package_url": "https://bad.invalid/x"})
        )
        self.assertEqual(error.path, "$.preferences.package_url")

    def test_path_shell_and_url_injection_values_are_rejected(self):
        for attack in ("../../etc/shadow", "rail;id", "$(id)", "https://bad.invalid/x"):
            with self.subTest(attack=attack):
                self.assert_error(
                    "unsupported_value", valid(preferences={"look": attack})
                )

    def test_school_kiosk_look_is_fixed(self):
        error = self.assert_error(
            "policy_conflict",
            valid(base="school-kiosk", preferences={"look": "tiles"}),
        )
        self.assertEqual(error.path, "$.preferences.look")
        plan = compile_recipe(valid(base="school-kiosk"))
        self.assertEqual(plan["resolved_preferences"]["look"], "locked")

    def test_school_kiosk_allows_build_time_theme_without_unlocking_it(self):
        plan = compile_recipe(
            valid(base="school-kiosk", preferences={"theme": "nocturne"})
        )
        self.assertEqual(plan["profile_overrides"], {"theme": "nocturne"})
        self.assertEqual(plan["resolved_preferences"]["look"], "locked")

    def test_wrong_types_are_rejected(self):
        for value in (None, [], "dock", 1):
            with self.subTest(value=value):
                self.assert_error("wrong_type", valid(preferences=value))
        self.assert_error(
            "wrong_type",
            {"schema": RECIPE_SCHEMA, "version": True, "base": "desktop"},
        )

    def test_missing_and_future_contracts_are_rejected(self):
        self.assert_error("missing_field", {"schema": RECIPE_SCHEMA, "version": 1})
        self.assert_error("unsupported_version", valid(version=2))
        self.assert_error("unsupported_schema", valid(schema="org.auros.recipe.evil"))

    def test_duplicate_fields_at_any_depth_are_rejected(self):
        for raw in (
            b'{"schema":"org.auros.recipe","schema":"org.auros.recipe",'
            b'"version":1,"base":"desktop"}',
            b'{"schema":"org.auros.recipe","version":1,"base":"desktop",'
            b'"preferences":{"look":"rail","look":"dock"}}',
        ):
            with self.subTest(raw=raw):
                with self.assertRaises(RecipeError) as caught:
                    parse_recipe(raw)
                self.assertEqual(caught.exception.code, "duplicate_field")

    def test_nonfinite_numbers_and_trailing_documents_are_rejected(self):
        for raw in (
            b'{"schema":"org.auros.recipe","version":NaN,"base":"desktop"}',
            b'{"schema":"org.auros.recipe","version":Infinity,"base":"desktop"}',
            b'{} {}',
        ):
            with self.subTest(raw=raw):
                with self.assertRaises(RecipeError):
                    parse_recipe(raw)

    def test_size_cap_is_enforced_before_parse(self):
        with self.assertRaises(RecipeError) as caught:
            parse_recipe(b" " * (MAX_RECIPE_BYTES + 1))
        self.assertEqual(caught.exception.code, "recipe_too_large")

    def test_invalid_utf8_and_excessive_depth_are_explicit_errors(self):
        with self.assertRaises(RecipeError) as caught:
            parse_recipe(b"\xff")
        self.assertEqual(caught.exception.code, "invalid_encoding")
        with self.assertRaises(RecipeError) as caught:
            parse_recipe(("[" * 1200 + "0" + "]" * 1200).encode("ascii"))
        self.assertEqual(caught.exception.code, "recipe_too_deep")

    def test_depth_limit_ignores_brackets_inside_strings(self):
        nested = "[" * MAX_RECIPE_DEPTH + "0" + "]" * MAX_RECIPE_DEPTH
        self.assertIsInstance(parse_recipe('{"unknown":"[[[{{{"}'), dict)
        with self.assertRaises(RecipeError) as caught:
            parse_recipe(nested)
        self.assertEqual(caught.exception.code, "wrong_type")

    def test_unicode_confusable_field_does_not_match_contract(self):
        recipe = valid()
        recipe["bas\u0435"] = recipe.pop("base")  # final character is Cyrillic
        self.assert_error("unknown_field", recipe)

    def test_catalogue_and_json_schema_match_implementation(self):
        schema = json.loads((ROOT / "src/customize/recipe-v1.schema.json").read_text())
        self.assertEqual(set(schema["properties"]["base"]["enum"]), set(BASES))
        self.assertEqual(
            set(schema["properties"]["preferences"]["properties"]["look"]["enum"]),
            set(PUBLIC_LOOKS),
        )
        self.assertEqual(
            set(schema["properties"]["preferences"]["properties"]["theme"]["enum"]),
            set(THEMES),
        )
        self.assertEqual({item["id"] for item in catalogue()["bases"]}, set(BASES))

    def test_every_delivery_requirement_reaches_catalogue_and_plan(self):
        catalogue_bases = {item["id"]: item for item in catalogue()["bases"]}
        for base_id, info in BASES.items():
            with self.subTest(base=base_id):
                expected = info["delivery_requirements"]
                self.assertEqual(catalogue_bases[base_id]["delivery"]["requires"], expected)
                self.assertEqual(
                    compile_recipe(valid(base=base_id))["delivery"]["requires"],
                    expected,
                )

    def test_catalogue_labels_and_defaults_match_profiles(self):
        for profile_id, expected in BASES.items():
            with self.subTest(profile=profile_id):
                actual = inherited_profile(profile_id)
                self.assertEqual(actual["profile_id"], profile_id)
                self.assertEqual(actual["profile_name"], expected["label"])
                self.assertEqual(actual["theme"], expected["default_theme"])
                self.assertEqual(actual["shell_archetype"], expected["default_look"])

    def test_catalogue_theme_and_look_ids_exist_in_repository(self):
        self.assertEqual(
            set(THEMES), {path.stem for path in (ROOT / "themes").glob("*.theme")}
        )
        for look_id in PUBLIC_LOOKS:
            self.assertTrue((ROOT / "shells" / f"{look_id}.shell").is_file())

    def test_cli_emits_machine_readable_error_and_exit_two(self):
        result = subprocess.run(
            [sys.executable, str(CLI), "compile"],
            input=b'{"schema":"org.auros.recipe","version":1,"base":"../desktop"}',
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, b"")
        error = json.loads(result.stderr)
        self.assertEqual(error["error"]["code"], "unsupported_value")
        self.assertEqual(error["error"]["path"], "$.base")


if __name__ == "__main__":
    unittest.main()
