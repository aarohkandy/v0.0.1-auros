#!/usr/bin/env python3
"""Validate an AurOS recipe and compile it to a typed build request.

This module deliberately does not create or source a shell profile.  Its output
is data for a future trusted build adapter, not an image or an installer.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, NoReturn


RECIPE_SCHEMA = "org.auros.recipe"
PLAN_SCHEMA = "org.auros.build-plan"
CATALOGUE_SCHEMA = "org.auros.recipe-catalogue"
ERROR_SCHEMA = "org.auros.recipe-error"
CONTRACT_VERSION = 1
COMPILER_VERSION = "1.0.0"
MAX_RECIPE_BYTES = 16 * 1024
MAX_RECIPE_DEPTH = 8

PUBLIC_LOOKS = {
    "dock": {
        "label": "Like a Mac",
        "description": "Regular programs stay in the same place along the edge.",
    },
    "rail": {
        "label": "Big and simple",
        "description": "Big buttons, with open things kept side by side.",
    },
    "taskbar": {
        "label": "Like Windows",
        "description": "A familiar bottom bar with a button for every open window.",
    },
    "tiles": {
        "label": "One thing at a time",
        "description": "A page of big buttons; one program fills the screen.",
    },
}

THEMES = {
    "ember": {
        "label": "Ember",
        "description": "Warm charcoal and banked firelight.",
    },
    "moss": {
        "label": "Moss",
        "description": "Desaturated greens and warm stone.",
    },
    "nocturne": {
        "label": "Light writing on a dark background",
        "description": "A dark background with light writing.",
    },
    "sandstone": {
        "label": "Light",
        "description": "Dark writing on light paper.",
    },
    "slate": {
        "label": "Slate",
        "description": "Cool neutral grey, with restrained colour.",
    },
}

# These labels and defaults mirror profiles/*.profile.  The test suite checks
# that relationship so website copy cannot silently drift from the images.
BASES = {
    "desktop": {
        "label": "AurOS Desktop",
        "description": "The standard AurOS experience for a personal computer.",
        "default_look": "rail",
        "default_theme": "sandstone",
        "delivery_requirements": ["trusted-builder", "release-gates"],
    },
    "multilingual": {
        "label": "AurOS Multilingual",
        "description": "Ships with Chinese, French, Spanish and Arabic ready to go.",
        "default_look": "rail",
        "default_theme": "sandstone",
        "delivery_requirements": [
            "trusted-builder",
            "release-gates",
            "input-method-integration",
        ],
    },
    "office": {
        "label": "AurOS for Organisations",
        "description": "A managed fleet with centrally chosen software and updates.",
        "default_look": "rail",
        "default_theme": "slate",
        "delivery_requirements": [
            "trusted-builder",
            "release-gates",
            "managed-update-channel",
        ],
    },
    "revive": {
        "label": "AurOS Revive",
        "description": "For a computer with 2 GB of memory and a spinning disk.",
        "default_look": "taskbar",
        "default_theme": "ember",
        "delivery_requirements": ["trusted-builder", "profile-release-gates"],
    },
    "school-kiosk": {
        "label": "AurOS for Schools (Kiosk)",
        "description": "Locked to a single browser, with no installs or settings.",
        "default_look": "locked",
        "default_theme": "sandstone",
        "delivery_requirements": [
            "trusted-builder",
            "release-gates",
            "managed-update-channel",
        ],
    },
}


@dataclass(frozen=True)
class RecipeError(ValueError):
    """A stable, machine-readable recipe validation failure."""

    code: str
    path: str
    message: str

    def __str__(self) -> str:
        return f"{self.path}: {self.message}"


def _fail(code: str, path: str, message: str) -> NoReturn:
    raise RecipeError(code, path, message)


def _pairs_without_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            _fail("duplicate_field", "$", f"field {key!r} appears more than once")
        result[key] = value
    return result


def _reject_constant(value: str) -> NoReturn:
    _fail("invalid_json_number", "$", f"JSON number {value!r} is not permitted")


def _check_json_depth(text: str) -> None:
    """Reject deep JSON before decoder recursion differs across Python versions."""

    depth = 0
    in_string = False
    escaped = False
    for character in text:
        if in_string:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_string = False
            continue
        if character == '"':
            in_string = True
        elif character in "[{":
            depth += 1
            if depth > MAX_RECIPE_DEPTH:
                _fail(
                    "recipe_too_deep",
                    "$",
                    f"recipe exceeds the {MAX_RECIPE_DEPTH}-level nesting limit",
                )
        elif character in "]}":
            depth -= 1


def parse_recipe(data: bytes | str) -> dict[str, Any]:
    """Parse JSON with a small input cap and strict JSON number/object rules."""

    if isinstance(data, str):
        try:
            raw = data.encode("utf-8")
        except UnicodeEncodeError:
            _fail("invalid_encoding", "$", "recipe must be valid UTF-8")
    else:
        raw = data
    if len(raw) > MAX_RECIPE_BYTES:
        _fail(
            "recipe_too_large",
            "$",
            f"recipe exceeds the {MAX_RECIPE_BYTES}-byte limit",
        )
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        _fail("invalid_encoding", "$", "recipe must be valid UTF-8")
    _check_json_depth(text)
    try:
        value = json.loads(
            text,
            object_pairs_hook=_pairs_without_duplicates,
            parse_constant=_reject_constant,
        )
    except RecipeError:
        raise
    except (json.JSONDecodeError, RecursionError) as exc:
        _fail("invalid_json", "$", f"recipe is not one complete JSON document: {exc}")
    if not isinstance(value, dict):
        _fail("wrong_type", "$", "recipe must be a JSON object")
    return value


def _exact_fields(value: dict[str, Any], allowed: set[str], path: str) -> None:
    unknown = sorted(set(value) - allowed)
    if unknown:
        _fail("unknown_field", f"{path}.{unknown[0]}", "field is not supported")


def _enum(value: Any, allowed: dict[str, Any], path: str) -> str:
    if not isinstance(value, str):
        _fail("wrong_type", path, "must be a string")
    if value not in allowed:
        _fail("unsupported_value", path, "value is not supported by recipe v1")
    return value


def validate_recipe(value: dict[str, Any]) -> dict[str, Any]:
    """Return a validated recipe containing only contract fields."""

    _exact_fields(value, {"schema", "version", "base", "preferences"}, "$")
    for required in ("schema", "version", "base"):
        if required not in value:
            _fail("missing_field", f"$.{required}", "field is required")
    if value["schema"] != RECIPE_SCHEMA:
        _fail("unsupported_schema", "$.schema", f"must be {RECIPE_SCHEMA!r}")
    if type(value["version"]) is not int:  # bool is an int in Python
        _fail("wrong_type", "$.version", "must be an integer")
    if value["version"] != CONTRACT_VERSION:
        _fail("unsupported_version", "$.version", "only recipe version 1 is supported")
    base = _enum(value["base"], BASES, "$.base")

    preferences = value.get("preferences", {})
    if not isinstance(preferences, dict):
        _fail("wrong_type", "$.preferences", "must be an object")
    _exact_fields(preferences, {"look", "theme"}, "$.preferences")

    clean_preferences: dict[str, str] = {}
    if "look" in preferences:
        look = _enum(preferences["look"], PUBLIC_LOOKS, "$.preferences.look")
        if base == "school-kiosk":
            _fail(
                "policy_conflict",
                "$.preferences.look",
                "school-kiosk has a fixed locked look",
            )
        clean_preferences["look"] = look
    if "theme" in preferences:
        clean_preferences["theme"] = _enum(
            preferences["theme"], THEMES, "$.preferences.theme"
        )

    clean: dict[str, Any] = {
        "schema": RECIPE_SCHEMA,
        "version": CONTRACT_VERSION,
        "base": base,
    }
    if "preferences" in value:
        clean["preferences"] = clean_preferences
    return clean


def _canonical_bytes(value: dict[str, Any]) -> bytes:
    # Contract strings are ASCII enums.  Sorting keys makes input object order
    # irrelevant and is straightforward to reproduce with a JS stable sorter.
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")


def compile_recipe(value: dict[str, Any]) -> dict[str, Any]:
    """Compile a parsed recipe to deterministic, non-executable build-plan data."""

    recipe = validate_recipe(value)
    base = recipe["base"]
    requested = recipe.get("preferences", {})
    base_info = BASES[base]
    resolved = {
        "look": requested.get("look", base_info["default_look"]),
        "theme": requested.get("theme", base_info["default_theme"]),
    }
    overrides: dict[str, str] = {}
    if "look" in requested:
        overrides["shell_archetype"] = requested["look"]
    if "theme" in requested:
        overrides["theme"] = requested["theme"]

    return {
        "schema": PLAN_SCHEMA,
        "version": CONTRACT_VERSION,
        "compiler_version": COMPILER_VERSION,
        "recipe_id": "sha256:" + hashlib.sha256(_canonical_bytes(recipe)).hexdigest(),
        "base_profile": base,
        "requested_preferences": requested,
        "resolved_preferences": resolved,
        "profile_overrides": overrides,
        "delivery": {
            "state": "validated-not-built",
            "artifact_created": False,
            "requires": base_info["delivery_requirements"],
        },
    }


def catalogue() -> dict[str, Any]:
    return {
        "schema": CATALOGUE_SCHEMA,
        "version": CONTRACT_VERSION,
        "compiler_version": COMPILER_VERSION,
        "bases": [
            {
                "id": base_id,
                "label": info["label"],
                "description": info["description"],
                "default_look": info["default_look"],
                "default_theme": info["default_theme"],
                "delivery": {
                    "state": "recipe-only",
                    "requires": info["delivery_requirements"],
                },
            }
            for base_id, info in sorted(BASES.items())
        ],
        "looks": [dict(id=item_id, **info) for item_id, info in sorted(PUBLIC_LOOKS.items())],
        "themes": [dict(id=item_id, **info) for item_id, info in sorted(THEMES.items())],
    }


def _json_line(value: dict[str, Any]) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def _read_limited(path: str) -> bytes:
    if path == "-":
        data = sys.stdin.buffer.read(MAX_RECIPE_BYTES + 1)
    else:
        try:
            with Path(path).open("rb") as stream:
                data = stream.read(MAX_RECIPE_BYTES + 1)
        except OSError as exc:
            _fail("input_error", "$", f"could not read recipe: {exc}")
    if len(data) > MAX_RECIPE_BYTES:
        _fail("recipe_too_large", "$", f"recipe exceeds the {MAX_RECIPE_BYTES}-byte limit")
    return data


def _error_document(error: RecipeError) -> dict[str, Any]:
    return {
        "schema": ERROR_SCHEMA,
        "version": CONTRACT_VERSION,
        "error": {"code": error.code, "path": error.path, "message": error.message},
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    compile_parser = sub.add_parser("compile", help="validate and compile one recipe")
    compile_parser.add_argument("recipe", nargs="?", default="-", help="JSON file or - for stdin")
    sub.add_parser("catalogue", help="print stable website choice metadata")
    args = parser.parse_args(argv)

    try:
        result = (
            catalogue()
            if args.command == "catalogue"
            else compile_recipe(parse_recipe(_read_limited(args.recipe)))
        )
    except RecipeError as exc:
        sys.stderr.write(_json_line(_error_document(exc)) + "\n")
        return 2
    sys.stdout.write(_json_line(result) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
