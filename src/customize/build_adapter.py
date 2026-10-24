#!/usr/bin/env python3
"""Resolve one raw recipe into typed values for the trusted Forge process."""

from __future__ import annotations

import argparse
import base64
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    from .recipe import (
        BASES,
        CONTRACT_VERSION,
        MAX_RECIPE_BYTES,
        PLAN_SCHEMA,
        RECIPE_SCHEMA,
        RecipeError,
        compile_recipe,
        parse_recipe,
    )
except ImportError:  # Direct execution by build/forge.
    from recipe import (  # type: ignore
        BASES,
        CONTRACT_VERSION,
        MAX_RECIPE_BYTES,
        PLAN_SCHEMA,
        RECIPE_SCHEMA,
        RecipeError,
        compile_recipe,
        parse_recipe,
    )


RECEIPT_SCHEMA = "org.auros.unsigned-build-metadata"
SOURCE_COMMIT = re.compile(r"^[0-9a-f]{40,64}$")


@dataclass(frozen=True)
class AdapterError(ValueError):
    code: str
    path: str
    message: str


@dataclass(frozen=True)
class ResolvedBuildInput:
    base_profile: str
    look: str
    theme: str
    recipe_id: str
    receipt: dict[str, object]

    def transport_line(self) -> str:
        receipt = json.dumps(
            self.receipt, sort_keys=True, separators=(",", ":"), ensure_ascii=True
        ).encode("ascii")
        encoded = base64.b64encode(receipt).decode("ascii")
        return "\t".join(
            (self.base_profile, self.look, self.theme, self.recipe_id, encoded)
        )


def _read_recipe(path: str) -> bytes:
    if path == "-":
        data = sys.stdin.buffer.read(MAX_RECIPE_BYTES + 1)
    else:
        try:
            with Path(path).open("rb") as stream:
                data = stream.read(MAX_RECIPE_BYTES + 1)
        except OSError as exc:
            raise AdapterError("input_error", "$", f"could not read recipe: {exc}") from exc
    if len(data) > MAX_RECIPE_BYTES:
        raise AdapterError(
            "recipe_too_large", "$", f"recipe exceeds the {MAX_RECIPE_BYTES}-byte limit"
        )
    return data


def resolve_recipe(
    data: bytes,
    *,
    expected_base: str | None = None,
    source_commit: str = "unknown",
    source_dirty: bool | None = None,
) -> ResolvedBuildInput:
    """Compile once, then expose only the fields Forge is allowed to apply."""

    if expected_base is not None and expected_base not in BASES:
        raise AdapterError(
            "unsupported_expected_base",
            "$.base",
            "the command-line profile is not a recipe base",
        )
    if source_commit != "unknown" and not SOURCE_COMMIT.fullmatch(source_commit):
        raise AdapterError(
            "invalid_source_commit", "$", "source commit must be a full lowercase hash or unknown"
        )

    plan = compile_recipe(parse_recipe(data))
    base = plan["base_profile"]
    if expected_base is not None and base != expected_base:
        raise AdapterError(
            "base_mismatch",
            "$.base",
            f"recipe base {base!r} does not match command-line profile {expected_base!r}",
        )

    resolved = plan["resolved_preferences"]
    look = resolved["look"]
    theme = resolved["theme"]
    receipt: dict[str, object] = {
        "schema": RECEIPT_SCHEMA,
        "version": CONTRACT_VERSION,
        "attestation": "none",
        "artifact_created": False,
        "recipe": {
            "schema": RECIPE_SCHEMA,
            "plan_schema": PLAN_SCHEMA,
            "id": plan["recipe_id"],
            "compiler_version": plan["compiler_version"],
            "requested_preferences": plan["requested_preferences"],
            "resolved_preferences": resolved,
        },
        "build": {
            "base_profile": base,
            "profile_overrides": plan["profile_overrides"],
            "source_commit": source_commit,
            "source_dirty": source_dirty,
            "required_gates": plan["delivery"]["requires"],
        },
    }
    return ResolvedBuildInput(base, look, theme, plan["recipe_id"], receipt)


def _error_document(error: RecipeError | AdapterError) -> dict[str, object]:
    return {
        "schema": "org.auros.recipe-error",
        "version": CONTRACT_VERSION,
        "error": {"code": error.code, "path": error.path, "message": error.message},
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recipe", help="raw recipe JSON file, or - for stdin")
    parser.add_argument("--expected-base", choices=sorted(BASES))
    parser.add_argument("--source-commit", default="unknown")
    parser.add_argument("--source-dirty", choices=("true", "false", "unknown"), default="unknown")
    args = parser.parse_args(argv)
    dirty = {"true": True, "false": False, "unknown": None}[args.source_dirty]

    try:
        result = resolve_recipe(
            _read_recipe(args.recipe),
            expected_base=args.expected_base,
            source_commit=args.source_commit,
            source_dirty=dirty,
        )
    except (RecipeError, AdapterError) as exc:
        sys.stderr.write(
            json.dumps(
                _error_document(exc),
                sort_keys=True,
                separators=(",", ":"),
                ensure_ascii=True,
            )
            + "\n"
        )
        return 2
    sys.stdout.write(result.transport_line() + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
