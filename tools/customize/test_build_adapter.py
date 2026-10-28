from __future__ import annotations

import base64
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from src.customize.build_adapter import AdapterError, resolve_recipe  # noqa: E402


def recipe(base="desktop", preferences=None):
    value = {"schema": "org.auros.recipe", "version": 1, "base": base}
    if preferences is not None:
        value["preferences"] = preferences
    return json.dumps(value, separators=(",", ":")).encode("ascii")


def forge(*args, input_bytes=None):
    return subprocess.run(
        [str(ROOT / "build" / "forge"), *args],
        cwd=ROOT,
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


class BuildAdapterTests(unittest.TestCase):
    def test_adapter_returns_typed_fields_and_receipt(self):
        resolved = resolve_recipe(
            recipe("revive", {"look": "rail", "theme": "moss"}),
            source_commit="a" * 40,
            source_dirty=False,
        )
        self.assertEqual(resolved.base_profile, "revive")
        self.assertEqual(resolved.look, "rail")
        self.assertEqual(resolved.theme, "moss")
        self.assertTrue(resolved.recipe_id.startswith("sha256:"))
        self.assertEqual(resolved.receipt["artifact_created"], False)
        self.assertEqual(resolved.receipt["attestation"], "none")
        self.assertEqual(resolved.receipt["schema"], "org.auros.unsigned-build-metadata")
        self.assertEqual(resolved.receipt["build"]["source_commit"], "a" * 40)
        self.assertEqual(resolved.receipt["build"]["source_dirty"], False)
        self.assertEqual(
            resolved.receipt["recipe"]["resolved_preferences"],
            {"look": "rail", "theme": "moss"},
        )

        fields = resolved.transport_line().split("\t")
        self.assertEqual(fields[:4], ["revive", "rail", "moss", resolved.recipe_id])
        self.assertEqual(json.loads(base64.b64decode(fields[4])), resolved.receipt)

    def test_expected_base_mismatch_is_explicit(self):
        with self.assertRaises(AdapterError) as caught:
            resolve_recipe(recipe("revive"), expected_base="desktop")
        self.assertEqual(caught.exception.code, "base_mismatch")

    def test_same_recipe_and_source_produce_identical_metadata(self):
        arguments = {
            "expected_base": "desktop",
            "source_commit": "b" * 40,
            "source_dirty": False,
        }
        first = resolve_recipe(recipe("desktop", {"theme": "moss"}), **arguments)
        second = resolve_recipe(recipe("desktop", {"theme": "moss"}), **arguments)
        self.assertEqual(first.receipt, second.receipt)
        self.assertEqual(first.transport_line(), second.transport_line())

    def test_legacy_resolve_preserves_profile_behavior(self):
        result = forge("resolve", "desktop")
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(
            json.loads(result.stdout),
            {
                "artifact_created": False,
                "base_profile": "desktop",
                "recipe_id": "none",
                "resolved_preferences": {"look": "rail", "theme": "sandstone"},
            },
        )

    def test_forge_resolve_uses_recipe_base_and_overrides(self):
        result = forge(
            "resolve",
            "--recipe",
            "-",
            input_bytes=recipe("revive", {"look": "tiles", "theme": "slate"}),
        )
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        output = json.loads(result.stdout)
        self.assertEqual(output["base_profile"], "revive")
        self.assertEqual(
            output["resolved_preferences"], {"look": "tiles", "theme": "slate"}
        )
        self.assertFalse(output["artifact_created"])

    def test_forge_rejects_explicit_base_mismatch(self):
        result = forge(
            "resolve", "desktop", "--recipe", "-", input_bytes=recipe("revive")
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'"code":"base_mismatch"', result.stderr)

    def test_kiosk_policy_cannot_be_replaced_by_recipe(self):
        rejected = forge(
            "resolve",
            "--recipe",
            "-",
            input_bytes=recipe("school-kiosk", {"look": "tiles"}),
        )
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn(b'"code":"policy_conflict"', rejected.stderr)

        allowed = forge(
            "resolve",
            "--recipe",
            "-",
            input_bytes=recipe("school-kiosk", {"theme": "nocturne"}),
        )
        self.assertEqual(allowed.returncode, 0, allowed.stderr.decode())
        output = json.loads(allowed.stdout)
        self.assertEqual(output["resolved_preferences"]["look"], "locked")
        self.assertEqual(output["resolved_preferences"]["theme"], "nocturne")

    def test_unknown_command_and_shell_syntax_cannot_execute(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "executed"
            attacks = [
                {
                    "schema": "org.auros.recipe",
                    "version": 1,
                    "base": "desktop",
                    "command": f"touch {marker}",
                },
                {
                    "schema": "org.auros.recipe",
                    "version": 1,
                    "base": "desktop",
                    "preferences": {"look": f"$(touch {marker})"},
                },
                {
                    "schema": "org.auros.build-plan",
                    "version": 1,
                    "base_profile": "desktop",
                    "profile_overrides": {"theme": f"x; touch {marker}"},
                },
            ]
            for attack in attacks:
                with self.subTest(attack=attack):
                    result = forge(
                        "resolve",
                        "--recipe",
                        "-",
                        input_bytes=json.dumps(attack).encode("utf-8"),
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertFalse(marker.exists())

    def test_resolve_never_mutates_source_profiles_or_creates_artifact(self):
        profiles = sorted((ROOT / "profiles").glob("*.profile"))
        before = {
            path: hashlib.sha256(path.read_bytes()).hexdigest() for path in profiles
        }
        result = forge("resolve", "--recipe", "-", input_bytes=recipe("office"))
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        after = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in profiles}
        self.assertEqual(before, after)
        self.assertFalse(json.loads(result.stdout)["artifact_created"])


if __name__ == "__main__":
    unittest.main()
