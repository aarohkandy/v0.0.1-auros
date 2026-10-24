#!/usr/bin/env python3
"""Validate and record the exact green base used by custom packaging."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from src.customize.recipe import (  # noqa: E402
    RecipeError,
    compile_recipe,
    parse_recipe,
    validate_recipe,
)
from tools.customize.installer_recipe import DELIVERABLE_BASES  # noqa: E402


RUN_WORKFLOW = ".github/workflows/build.yml"
INSTALLER_ARTIFACT = "auros-installer"
METADATA_ARTIFACT = "auros-base-metadata"
SHA256 = re.compile(r"^[0-9a-f]{64}$")
COMMIT = re.compile(r"^[0-9a-f]{40}$")
PIECE_NAME = re.compile(r"^auros-desktop[.]img[.]gz[.][0-9]{3,}$")
MAX_TEXT = 1024 * 1024


class BaseBuildError(ValueError):
    pass


def _fail(message: str) -> None:
    raise BaseBuildError(message)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _read_text(path: Path) -> str:
    try:
        data = path.read_bytes()
    except OSError as exc:
        _fail(f"could not read {path.name}: {exc}")
    if len(data) > MAX_TEXT:
        _fail(f"{path.name} exceeds the {MAX_TEXT}-byte metadata limit")
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        _fail(f"{path.name} is not UTF-8")


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, sort_keys=True, separators=(",", ":"))
            stream.write("\n")
        os.replace(temporary, path)
    except Exception:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def _exact_fields(value: dict[str, Any], expected: set[str], name: str) -> None:
    if set(value) != expected:
        missing = sorted(expected - set(value))
        unknown = sorted(set(value) - expected)
        _fail(f"{name} fields are invalid (missing={missing}, unknown={unknown})")


def _strict_json(data: bytes) -> dict[str, Any]:
    def pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
        value: dict[str, Any] = {}
        for key, item in items:
            if key in value:
                _fail(f"provenance contains duplicate field {key!r}")
            value[key] = item
        return value

    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=pairs)
    except UnicodeDecodeError:
        _fail("provenance is not UTF-8")
    except json.JSONDecodeError as exc:
        _fail(f"provenance is not one JSON document: {exc}")
    if not isinstance(value, dict):
        _fail("provenance must be a JSON object")
    return value


def validate_unsigned_provenance(value: dict[str, Any], installer: Path) -> None:
    """Validate every claim retained beside a later signed installer."""

    _exact_fields(
        value,
        {
            "schema",
            "version",
            "state",
            "signed",
            "public_release",
            "download_ready",
            "repository",
            "base_build",
            "recipe",
            "custom_installer",
        },
        "provenance",
    )
    if (
        value.get("schema") != "org.auros.custom-package-provenance"
        or type(value.get("version")) is not int
        or value.get("version") != 1
    ):
        _fail("provenance schema or version is invalid")
    if value.get("state") != "unsigned-custom-artifact":
        _fail("provenance does not describe an unsigned custom artifact")
    for field in ("signed", "public_release", "download_ready"):
        if value.get(field) is not False:
            _fail(f"provenance {field} must remain false")

    repository = value.get("repository")
    if not isinstance(repository, str) or not re.fullmatch(
        r"[A-Za-z0-9-]+/[A-Za-z0-9._-]+", repository
    ):
        _fail("provenance repository is invalid")
    base = value.get("base_build")
    if not isinstance(base, dict):
        _fail("provenance base_build must be an object")
    _exact_fields(
        base,
        {
            "run_id",
            "head_sha",
            "tag",
            "profile",
            "installer_sha256",
            "image_sha256",
            "image_bytes",
            "pieces_sha256",
            "pieces_base",
        },
        "base_build",
    )
    if type(base.get("run_id")) is not int or base["run_id"] <= 0:
        _fail("base build run ID is invalid")
    if not COMMIT.fullmatch(str(base.get("head_sha"))):
        _fail("base build source commit is invalid")
    if not re.fullmatch(r"v[0-9A-Za-z.+-]+-[0-9a-f]{7}", str(base.get("tag"))):
        _fail("base build tag is invalid")
    if base.get("profile") != "desktop":
        _fail("base build profile is not deliverable")
    for field in ("installer_sha256", "image_sha256", "pieces_sha256"):
        if not SHA256.fullmatch(str(base.get(field))):
            _fail(f"base build {field} is invalid")
    if type(base.get("image_bytes")) is not int or base["image_bytes"] <= 0:
        _fail("base image size is invalid")
    expected_base = (
        f"https://github.com/{repository}/releases/download/{base['tag']}/"
    )
    if base.get("pieces_base") != expected_base:
        _fail("base pieces URL does not match the repository and tag")

    recipe = value.get("recipe")
    if not isinstance(recipe, dict):
        _fail("provenance recipe must be an object")
    _exact_fields(recipe, {"id", "base", "resolved_preferences"}, "recipe")
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", str(recipe.get("id"))):
        _fail("recipe identity is invalid")
    if recipe.get("base") != "desktop":
        _fail("recipe base is not deliverable")
    preferences = recipe.get("resolved_preferences")
    if not isinstance(preferences, dict):
        _fail("resolved recipe preferences must be an object")
    _exact_fields(preferences, {"look", "theme"}, "resolved preferences")
    if preferences.get("look") not in {"dock", "rail", "taskbar", "tiles"}:
        _fail("resolved recipe look is invalid")
    if preferences.get("theme") not in {
        "ember",
        "moss",
        "nocturne",
        "sandstone",
        "slate",
    }:
        _fail("resolved recipe theme is invalid")

    custom = value.get("custom_installer")
    if not isinstance(custom, dict):
        _fail("custom_installer must be an object")
    _exact_fields(custom, {"filename", "sha256", "bytes"}, "custom_installer")
    if custom.get("filename") != "AurOS-Setup.exe":
        _fail("custom installer filename is invalid")
    try:
        size = installer.stat().st_size
    except OSError as exc:
        _fail(f"could not inspect unsigned custom installer: {exc}")
    if type(custom.get("bytes")) is not int or custom["bytes"] != size:
        _fail("custom installer size does not match provenance")
    recorded_sha = custom.get("sha256")
    if not SHA256.fullmatch(str(recorded_sha)) or recorded_sha != _sha256(installer):
        _fail("custom installer hash does not match provenance")


def preserve_provenance(provenance: Path, installer: Path, output: Path) -> str:
    """Validate and atomically preserve the original unsigned manifest bytes."""

    try:
        raw = provenance.read_bytes()
    except OSError as exc:
        _fail(f"could not read provenance: {exc}")
    if len(raw) > MAX_TEXT:
        _fail(f"provenance exceeds the {MAX_TEXT}-byte metadata limit")
    validate_unsigned_provenance(_strict_json(raw), installer)
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=output.name + ".", dir=output.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(raw)
        os.replace(temporary, output)
    except OSError as exc:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        _fail(f"could not preserve provenance: {exc}")
    return hashlib.sha256(raw).hexdigest()


def validate_run(
    run: dict[str, Any],
    artifacts: list[dict[str, Any]],
    *,
    repository: str,
    run_id: int,
    head_sha: str,
    raw_recipe: bytes,
) -> dict[str, Any]:
    """Validate API data and return inert request/provenance input."""

    if not re.fullmatch(r"[A-Za-z0-9-]+/[A-Za-z0-9._-]+", repository):
        _fail("repository is not an owner/name")
    if run_id <= 0 or not COMMIT.fullmatch(head_sha):
        _fail("run ID or packaging checkout SHA is invalid")
    if run.get("id") != run_id:
        _fail("GitHub returned a different build run")
    if run.get("name") != "build" or run.get("path") != RUN_WORKFLOW:
        _fail("base run is not build.yml")
    if run.get("event") != "push" or run.get("head_branch") != "main":
        _fail("base run was not a main-branch push")
    if run.get("status") != "completed" or run.get("conclusion") != "success":
        _fail("base build has not completed successfully")
    if run.get("head_sha") != head_sha:
        _fail("base build commit does not match the packaging checkout")
    run_repo = (run.get("repository") or {}).get("full_name")
    if not isinstance(run_repo, str) or run_repo.lower() != repository.lower():
        _fail("base run belongs to a different repository")

    selected: dict[str, int] = {}
    for name in (INSTALLER_ARTIFACT, METADATA_ARTIFACT):
        matches = [
            item
            for item in artifacts
            if item.get("name") == name and not item.get("expired", False)
        ]
        if len(matches) != 1:
            _fail(f"base run must have exactly one unexpired {name!r} artifact")
        artifact_id = matches[0].get("id")
        if type(artifact_id) is not int or artifact_id <= 0:
            _fail(f"{name!r} has an invalid artifact ID")
        selected[name] = artifact_id

    plan = compile_recipe(parse_recipe(raw_recipe))
    if plan["base_profile"] not in DELIVERABLE_BASES:
        _fail(f"profile {plan['base_profile']!r} is not enabled for packaging")

    return {
        "schema": "org.auros.custom-package-request",
        "version": 1,
        "state": "validated-inputs",
        "repository": repository,
        "base_build": {
            "run_id": run_id,
            "head_sha": head_sha,
            "workflow": RUN_WORKFLOW,
            "branch": "main",
            "artifacts": {
                "installer": {"name": INSTALLER_ARTIFACT, "id": selected[INSTALLER_ARTIFACT]},
                "metadata": {"name": METADATA_ARTIFACT, "id": selected[METADATA_ARTIFACT]},
            },
        },
        "recipe": {
            "id": plan["recipe_id"],
            "base": plan["base_profile"],
            "resolved_preferences": plan["resolved_preferences"],
        },
    }


def validate_files(
    request: dict[str, Any], metadata_dir: Path, installer: Path
) -> dict[str, Any]:
    """Cross-check the downloaded small metadata and normal base installer."""

    if request.get("schema") != "org.auros.custom-package-request":
        _fail("request file has the wrong schema")
    base_build = request.get("base_build") or {}
    repository = request.get("repository")
    head_sha = base_build.get("head_sha")
    run_id = base_build.get("run_id")
    if not isinstance(repository, str) or not COMMIT.fullmatch(str(head_sha)) or type(run_id) is not int:
        _fail("request file has invalid base identity")

    pieces_path = metadata_dir / "pieces.txt"
    sha_path = metadata_dir / "image.sha256"
    bytes_path = metadata_dir / "image.bytes"
    info_path = metadata_dir / "build-info.txt"
    pieces = _read_text(pieces_path)
    image_sha = _read_text(sha_path).strip()
    image_bytes_text = _read_text(bytes_path).strip()
    info = _read_text(info_path)
    if not SHA256.fullmatch(image_sha) or not re.fullmatch(r"[1-9][0-9]*", image_bytes_text):
        _fail("image.sha256 or image.bytes is invalid")
    image_bytes = int(image_bytes_text)

    first = re.search(
        r"^tag ([A-Za-z0-9][A-Za-z0-9.+_-]*) \(os_version [^,()]+, commit ([0-9a-f]{40}), run ([0-9]+)\)$",
        info,
        re.MULTILINE,
    )
    if not first or first.group(2) != head_sha or int(first.group(3)) != run_id:
        _fail("build-info does not identify the selected run and commit")
    tag = first.group(1)

    image_line = re.search(
        r"^image ([0-9]+) bytes, sha256 ([0-9a-f]{64}); gzip [0-9]+ bytes \(sha256 [0-9a-f]{64}\) in [0-9]+ pieces$",
        info,
        re.MULTILINE,
    )
    if not image_line or int(image_line.group(1)) != image_bytes or image_line.group(2) != image_sha:
        _fail("build-info image hash/size does not match the metadata files")
    pieces_line = re.search(
        r"^pieces[.]txt ([0-9]+) bytes, sha256 ([0-9a-f]{64})$", info, re.MULTILINE
    )
    if not pieces_line or int(pieces_line.group(1)) != pieces_path.stat().st_size or pieces_line.group(2) != _sha256(pieces_path):
        _fail("pieces.txt hash/size does not match build-info")

    lines = pieces.splitlines()
    expected_comment = f"# AurOS desktop {tag}, commit {head_sha[:12]}, Actions run {run_id}"
    expected_base = f"https://github.com/{repository}/releases/download/{tag}/"
    if len(lines) < 3 or lines[0] != expected_comment or lines[1] != f"base {expected_base}":
        _fail("pieces.txt does not name the selected repository, tag, run, and commit")
    names: set[str] = set()
    for line in lines[2:]:
        match = re.fullmatch(r"piece ([^ ]+) ([1-9][0-9]*) ([0-9a-f]{64})", line)
        if not match or not PIECE_NAME.fullmatch(match.group(1)):
            _fail("pieces.txt contains an invalid piece entry")
        if match.group(1) in names:
            _fail("pieces.txt contains a duplicate piece")
        names.add(match.group(1))

    try:
        installer_bytes = installer.stat().st_size
    except OSError as exc:
        _fail(f"could not inspect the base installer: {exc}")
    installer_sha = _sha256(installer)
    setup_line = re.search(
        r"^AurOS-Setup[.]exe ([0-9]+) bytes, sha256 ([0-9a-f]{64})$", info, re.MULTILINE
    )
    if not setup_line or int(setup_line.group(1)) != installer_bytes or setup_line.group(2) != installer_sha:
        _fail("downloaded base installer does not match build-info")

    return {
        "schema": "org.auros.custom-package-provenance",
        "version": 1,
        "state": "validated-base-pending-unsigned-build",
        "signed": False,
        "public_release": False,
        "download_ready": False,
        "repository": repository,
        "base_build": {
            "run_id": run_id,
            "head_sha": head_sha,
            "tag": tag,
            "profile": "desktop",
            "installer_sha256": installer_sha,
            "image_sha256": image_sha,
            "image_bytes": image_bytes,
            "pieces_sha256": _sha256(pieces_path),
            "pieces_base": expected_base,
        },
        "recipe": request["recipe"],
    }


def finish_provenance(provenance: dict[str, Any], installer: Path) -> dict[str, Any]:
    if provenance.get("state") != "validated-base-pending-unsigned-build":
        _fail("provenance is not ready to record a custom build")
    try:
        size = installer.stat().st_size
        with installer.open("rb") as stream:
            prefix = stream.read(2)
    except OSError as exc:
        _fail(f"could not inspect custom installer: {exc}")
    if prefix != b"MZ" or size <= 0:
        _fail("custom installer is not a Windows executable")
    result = dict(provenance)
    result.update(
        {
            "state": "unsigned-custom-artifact",
            "custom_installer": {
                "filename": installer.name,
                "sha256": _sha256(installer),
                "bytes": size,
            },
        }
    )
    return result


def package_build_info(
    provenance: dict[str, Any], installer: Path, source_sha: str, run_id: int
) -> str:
    """Build the signer-facing identity file without making a release claim."""

    if provenance.get("state") != "unsigned-custom-artifact":
        _fail("finished provenance is required for package build-info")
    if not COMMIT.fullmatch(source_sha) or run_id <= 0:
        _fail("custom package source or run identity is invalid")
    base = provenance.get("base_build") or {}
    tag = base.get("tag")
    match = re.fullmatch(r"v([0-9A-Za-z.+-]+)-[0-9a-f]{7}", str(tag))
    if not match:
        _fail("base tag has no usable AurOS version")
    custom = provenance.get("custom_installer") or {}
    if custom.get("sha256") != _sha256(installer) or custom.get("bytes") != installer.stat().st_size:
        _fail("custom installer changed after provenance was recorded")
    recipe_id = str((provenance.get("recipe") or {}).get("id", ""))
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", recipe_id):
        _fail("provenance has no valid recipe identity")
    return (
        f"tag custom-{recipe_id[7:19]} (os_version {match.group(1)}, commit {source_sha}, run {run_id})\n"
        "state unsigned-custom-artifact; not published or download-ready\n"
        f"base tag {tag}, run {base.get('run_id')}, image sha256 {base.get('image_sha256')}\n"
        f"AurOS-Setup.exe {custom['bytes']} bytes, sha256 {custom['sha256']}\n"
    )


def _github_get(path: str, token: str, api_url: str) -> dict[str, Any]:
    request = urllib.request.Request(
        api_url.rstrip("/") + path,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)
    except (OSError, urllib.error.HTTPError, json.JSONDecodeError) as exc:
        _fail(f"GitHub API request failed: {exc}")


def _append_outputs(path: Path, values: dict[str, str]) -> None:
    with path.open("a", encoding="utf-8", newline="\n") as stream:
        for name, value in values.items():
            if not re.fullmatch(r"[a-z_]+", name) or not re.fullmatch(r"[0-9]+", value):
                _fail("refusing an unsafe GitHub output")
            stream.write(f"{name}={value}\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    validate = sub.add_parser("validate-run")
    validate.add_argument("--repository", required=True)
    validate.add_argument("--run-id", required=True, type=int)
    validate.add_argument("--head-sha", required=True)
    validate.add_argument("--recipe-env", default="AUROS_RECIPE_JSON")
    validate.add_argument("--recipe-output", required=True, type=Path)
    validate.add_argument("--request", required=True, type=Path)
    validate.add_argument("--github-output", required=True, type=Path)
    verify = sub.add_parser("verify-files")
    verify.add_argument("--request", required=True, type=Path)
    verify.add_argument("--metadata", required=True, type=Path)
    verify.add_argument("--installer", required=True, type=Path)
    verify.add_argument("--provenance", required=True, type=Path)
    finish = sub.add_parser("finish")
    finish.add_argument("--provenance", required=True, type=Path)
    finish.add_argument("--installer", required=True, type=Path)
    finish.add_argument("--build-info", required=True, type=Path)
    finish.add_argument("--source-sha", required=True)
    finish.add_argument("--run-id", required=True, type=int)
    preserve = sub.add_parser("preserve-provenance")
    preserve.add_argument("--provenance", required=True, type=Path)
    preserve.add_argument("--installer", required=True, type=Path)
    preserve.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)

    try:
        if args.command == "validate-run":
            token = os.environ.get("GH_TOKEN", "")
            raw = os.environ.get(args.recipe_env)
            if not token or raw is None:
                _fail("GH_TOKEN and the recipe environment variable are required")
            api = os.environ.get("GITHUB_API_URL", "https://api.github.com")
            run = _github_get(f"/repos/{args.repository}/actions/runs/{args.run_id}", token, api)
            artifact_doc = _github_get(
                f"/repos/{args.repository}/actions/runs/{args.run_id}/artifacts?per_page=100",
                token,
                api,
            )
            artifacts = artifact_doc.get("artifacts")
            if not isinstance(artifacts, list) or artifact_doc.get("total_count") != len(artifacts):
                _fail("artifact listing is incomplete or malformed")
            result = validate_run(
                run,
                artifacts,
                repository=args.repository,
                run_id=args.run_id,
                head_sha=args.head_sha,
                raw_recipe=raw.encode("utf-8"),
            )
            _write_json(args.request, result)
            _write_json(args.recipe_output, validate_recipe(parse_recipe(raw)))
            _append_outputs(
                args.github_output,
                {
                    "installer_artifact_id": str(result["base_build"]["artifacts"]["installer"]["id"]),
                    "metadata_artifact_id": str(result["base_build"]["artifacts"]["metadata"]["id"]),
                    "base_run_id": str(result["base_build"]["run_id"]),
                },
            )
        elif args.command == "verify-files":
            request = json.loads(_read_text(args.request))
            _write_json(
                args.provenance,
                validate_files(request, args.metadata, args.installer),
            )
        elif args.command == "finish":
            provenance = json.loads(_read_text(args.provenance))
            finished = finish_provenance(provenance, args.installer)
            _write_json(args.provenance, finished)
            args.build_info.write_text(
                package_build_info(finished, args.installer, args.source_sha, args.run_id),
                encoding="utf-8",
            )
        else:
            print(preserve_provenance(args.provenance, args.installer, args.output))
    except (BaseBuildError, RecipeError, json.JSONDecodeError, UnicodeEncodeError, OSError) as exc:
        print(f"base build: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
