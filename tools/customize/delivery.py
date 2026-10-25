#!/usr/bin/env python3
"""Fail-closed publication records for signed personalized installers."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
from pathlib import Path
from typing import Any


MAX_JSON = 1024 * 1024
COMMIT = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
REPOSITORY = re.compile(r"^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$")
TAG = re.compile(r"^v[0-9A-Za-z.+-]+-[0-9a-f]{7}$")
REQUEST_ID = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"
)
PIECE = re.compile(r"^auros-desktop[.]img[.]gz[.][0-9]{3,}$")


class DeliveryError(ValueError):
    pass


def _fail(message: str) -> None:
    raise DeliveryError(message)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as exc:
        _fail(f"could not hash {path.name}: {exc}")
    return digest.hexdigest()


def _read(path: Path, limit: int = MAX_JSON) -> bytes:
    try:
        data = path.read_bytes()
    except OSError as exc:
        _fail(f"could not read {path.name}: {exc}")
    if len(data) > limit:
        _fail(f"{path.name} exceeds the {limit}-byte limit")
    return data


def _json(path: Path) -> tuple[dict[str, Any], bytes]:
    raw = _read(path)

    def pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
        value: dict[str, Any] = {}
        for key, item in items:
            if key in value:
                _fail(f"{path.name} contains duplicate field {key!r}")
            value[key] = item
        return value

    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=pairs)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        _fail(f"{path.name} is not one UTF-8 JSON document: {exc}")
    if not isinstance(value, dict):
        _fail(f"{path.name} must contain a JSON object")
    return value, raw


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


def _copy(path: Path, output: Path) -> None:
    data = _read(path, 1024 * 1024 * 1024)
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=output.name + ".", dir=output.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
        os.replace(temporary, output)
    except Exception:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def _exact(value: dict[str, Any], fields: set[str], name: str) -> None:
    if set(value) != fields:
        _fail(
            f"{name} fields are invalid "
            f"(missing={sorted(fields - set(value))}, unknown={sorted(set(value) - fields)})"
        )


def validate_request_id(request_id: str) -> str | None:
    if request_id == "":
        return None
    if not REQUEST_ID.fullmatch(request_id):
        _fail("request_id must be an empty value or a canonical lowercase UUID")
    return request_id


def validate_source_ref(ref: str, source_sha: str, provenance_path: Path) -> None:
    provenance, _raw = _json(provenance_path)
    _repository, authority, _base, _recipe = _validate_provenance(provenance)
    if source_sha != authority["head_sha"] or not COMMIT.fullmatch(source_sha):
        _fail("workflow source does not match the packaging authority")
    if ref != "refs/heads/main":
        _fail("custom packaging must run from main")


def _validate_recipe(recipe: Any) -> dict[str, Any]:
    if not isinstance(recipe, dict):
        _fail("recipe must be an object")
    _exact(recipe, {"id", "base", "resolved_preferences"}, "recipe")
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", str(recipe.get("id"))):
        _fail("recipe identity is invalid")
    if recipe.get("base") != "desktop":
        _fail("recipe base is not deliverable")
    preferences = recipe.get("resolved_preferences")
    if not isinstance(preferences, dict):
        _fail("resolved recipe preferences must be an object")
    _exact(preferences, {"look", "theme"}, "resolved recipe preferences")
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
    return recipe


def _validate_provenance(
    value: dict[str, Any],
) -> tuple[str, dict[str, Any], dict[str, Any], dict[str, Any]]:
    _exact(
        value,
        {
            "schema",
            "version",
            "state",
            "signed",
            "public_release",
            "download_ready",
            "repository",
            "authority",
            "base_build",
            "recipe",
            "custom_installer",
        },
        "custom package provenance",
    )
    if (
        value.get("schema") != "org.auros.custom-package-provenance"
        or type(value.get("version")) is not int
        or value.get("version") != 2
    ):
        _fail("custom package provenance schema or version is invalid")
    if value.get("state") != "unsigned-custom-artifact":
        _fail("custom package provenance is not the unsigned build record")
    if any(value.get(field) is not False for field in ("signed", "public_release", "download_ready")):
        _fail("unsigned provenance contains a signed, release, or ready claim")
    repository = value.get("repository")
    if not isinstance(repository, str) or not REPOSITORY.fullmatch(repository):
        _fail("provenance repository is invalid")
    authority = value.get("authority")
    if not isinstance(authority, dict):
        _fail("provenance authority must be an object")
    _exact(authority, {"head_sha"}, "provenance authority")
    if not COMMIT.fullmatch(str(authority.get("head_sha"))):
        _fail("provenance authority commit is invalid")
    base = value.get("base_build")
    if not isinstance(base, dict):
        _fail("base_build must be an object")
    _exact(
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
        _fail("base build run is invalid")
    head_sha = str(base.get("head_sha"))
    tag = str(base.get("tag"))
    if (
        not COMMIT.fullmatch(head_sha)
        or not TAG.fullmatch(tag)
        or tag.rsplit("-", 1)[1] != head_sha[:7]
    ):
        _fail("base source or tag is invalid")
    if base.get("profile") != "desktop":
        _fail("base profile is not deliverable")
    for field in ("installer_sha256", "image_sha256", "pieces_sha256"):
        if not SHA256.fullmatch(str(base.get(field))):
            _fail(f"base {field} is invalid")
    if type(base.get("image_bytes")) is not int or base["image_bytes"] <= 0:
        _fail("base image size is invalid")
    expected_base = f"https://github.com/{repository}/releases/download/{base['tag']}/"
    if base.get("pieces_base") != expected_base:
        _fail("base piece URL does not match repository and tag")
    custom = value.get("custom_installer")
    if not isinstance(custom, dict):
        _fail("custom_installer must be an object")
    _exact(custom, {"filename", "sha256", "bytes"}, "custom_installer")
    if custom.get("filename") != "AurOS-Setup.exe":
        _fail("unsigned custom installer filename is invalid")
    if not SHA256.fullmatch(str(custom.get("sha256"))):
        _fail("unsigned custom installer hash is invalid")
    if type(custom.get("bytes")) is not int or custom["bytes"] <= 0:
        _fail("unsigned custom installer size is invalid")
    return repository, authority, base, _validate_recipe(value.get("recipe"))


def _signing_provenance(path: Path) -> dict[str, str]:
    required_values = {
        "source_commit",
        "build_run",
        "unsigned_wizard_sha256",
        "signed_wizard_sha256",
        "build_unsigned_outer_sha256",
        "rebuilt_unsigned_outer_sha256",
        "signed_outer_sha256",
        "build_info_sha256",
        "custom_package_provenance_sha256",
    }
    required_markers = {
        "verified custom_package_provenance_describes_build_unsigned_outer",
        "verified signed_wizard_is_authenticode_form_of_unsigned_wizard",
        "verified rebuilt_unsigned_outer_embeds_signed_wizard_byte_for_byte",
        "verified signed_outer_is_authenticode_form_of_rebuilt_unsigned_outer",
        "verified signed_outer_embeds_signed_wizard_byte_for_byte",
    }
    values: dict[str, str] = {}
    markers: set[str] = set()
    try:
        lines = _read(path).decode("ascii").splitlines()
    except UnicodeDecodeError:
        _fail("signing provenance is not ASCII")
    for line in lines:
        if line in required_markers:
            if line in markers:
                _fail("signing provenance contains a duplicate verification")
            markers.add(line)
            continue
        key, separator, item = line.partition(" ")
        if not separator or key not in required_values or key in values:
            _fail("signing provenance contains an unknown or duplicate claim")
        values[key] = item
    if set(values) != required_values or markers != required_markers:
        _fail("signing provenance is incomplete")
    for key in required_values - {"source_commit", "build_run"}:
        if not SHA256.fullmatch(values[key]):
            _fail(f"signing provenance {key} is invalid")
    if not COMMIT.fullmatch(values["source_commit"]) or not re.fullmatch(
        r"[1-9][0-9]*", values["build_run"]
    ):
        _fail("signing provenance source or run is invalid")
    return values


def _assets(release: dict[str, Any]) -> dict[str, dict[str, Any]]:
    raw = release.get("assets")
    if not isinstance(raw, list):
        _fail("release assets are missing")
    assets: dict[str, dict[str, Any]] = {}
    for asset in raw:
        if not isinstance(asset, dict) or not isinstance(asset.get("name"), str):
            _fail("release contains a malformed asset")
        name = asset["name"]
        if name in assets:
            _fail(f"release contains duplicate asset {name!r}")
        assets[name] = asset
    return assets


def _asset(
    assets: dict[str, dict[str, Any]],
    name: str,
    repository: str,
    tag: str,
    *,
    sha256: str | None = None,
    size: int | None = None,
) -> dict[str, Any]:
    asset = assets.get(name)
    if asset is None:
        _fail(f"base release is missing {name}")
    if type(asset.get("id")) is not int or asset["id"] <= 0:
        _fail(f"release asset {name} has an invalid ID")
    expected_url = f"https://github.com/{repository}/releases/download/{tag}/{name}"
    if asset.get("browser_download_url") != expected_url:
        _fail(f"release asset {name} has a substituted URL")
    if sha256 is not None and asset.get("digest") != f"sha256:{sha256}":
        _fail(f"release asset {name} has the wrong digest")
    if size is not None and asset.get("size") != size:
        _fail(f"release asset {name} has the wrong size")
    return asset


def _validate_release(
    release: dict[str, Any], repository: str, base: dict[str, Any], release_source_sha: str
) -> dict[str, dict[str, Any]]:
    if release.get("tag_name") != base["tag"]:
        _fail("release tag does not match base provenance")
    if release.get("draft") is not False or release.get("prerelease") is not True:
        _fail("base release must be a published prerelease")
    if not COMMIT.fullmatch(release_source_sha) or release_source_sha != base["head_sha"]:
        _fail("base release does not resolve to the proven source commit")
    return _assets(release)


def _validate_base_assets(
    assets: dict[str, dict[str, Any]],
    repository: str,
    base: dict[str, Any],
    pieces_path: Path,
    image_sha_path: Path,
    image_bytes_path: Path,
) -> list[dict[str, Any]]:
    pieces_raw = _read(pieces_path)
    if hashlib.sha256(pieces_raw).hexdigest() != base["pieces_sha256"]:
        _fail("published pieces.txt does not match base provenance")
    try:
        pieces = pieces_raw.decode("utf-8").splitlines()
        image_sha = _read(image_sha_path).decode("ascii").strip()
        image_bytes_text = _read(image_bytes_path).decode("ascii").strip()
    except UnicodeDecodeError:
        _fail("base image metadata has invalid encoding")
    if image_sha != base["image_sha256"] or image_bytes_text != str(base["image_bytes"]):
        _fail("published image hash or size does not match base provenance")
    expected = [
        f"# AurOS desktop {base['tag']}, commit {base['head_sha'][:12]}, Actions run {base['run_id']}",
        f"base {base['pieces_base']}",
    ]
    if len(pieces) < 3 or pieces[:2] != expected:
        _fail("published pieces.txt has the wrong base identity")
    required = [
        _asset(
            assets,
            "pieces.txt",
            repository,
            base["tag"],
            sha256=base["pieces_sha256"],
            size=len(pieces_raw),
        ),
        _asset(
            assets,
            "image.sha256",
            repository,
            base["tag"],
            sha256=hashlib.sha256(_read(image_sha_path)).hexdigest(),
            size=image_sha_path.stat().st_size,
        ),
        _asset(
            assets,
            "image.bytes",
            repository,
            base["tag"],
            sha256=hashlib.sha256(_read(image_bytes_path)).hexdigest(),
            size=image_bytes_path.stat().st_size,
        ),
        _asset(
            assets,
            "AurOS-Setup.exe",
            repository,
            base["tag"],
            sha256=base["installer_sha256"],
        ),
    ]
    seen: set[str] = set()
    for line in pieces[2:]:
        match = re.fullmatch(r"piece ([^ ]+) ([1-9][0-9]*) ([0-9a-f]{64})", line)
        if not match or not PIECE.fullmatch(match.group(1)) or match.group(1) in seen:
            _fail("published pieces.txt contains an invalid or duplicate piece")
        seen.add(match.group(1))
        required.append(
            _asset(
                assets,
                match.group(1),
                repository,
                base["tag"],
                sha256=match.group(3),
                size=int(match.group(2)),
            )
        )
    return [
        {
            "name": asset["name"],
            "id": asset["id"],
            "size": asset["size"],
            "digest": asset["digest"],
            "url": asset["browser_download_url"],
        }
        for asset in required
    ]


def _validate_base_snapshot(plan: dict[str, Any], release: dict[str, Any]) -> None:
    assets = _assets(release)
    for expected in plan["base_assets"]:
        actual = assets.get(expected["name"])
        if actual is None:
            _fail(f"base release lost {expected['name']}")
        observed = {
            "name": actual.get("name"),
            "id": actual.get("id"),
            "size": actual.get("size"),
            "digest": actual.get("digest"),
            "url": actual.get("browser_download_url"),
        }
        if observed != expected:
            _fail(f"base release asset {expected['name']} changed after validation")


def prepare(
    *,
    provenance_path: Path,
    signing_path: Path,
    installer: Path,
    release_path: Path,
    pieces_path: Path,
    image_sha_path: Path,
    image_bytes_path: Path,
    repository: str,
    source_sha: str,
    release_source_sha: str,
    workflow_run_id: int,
    signed_sha256: str,
    request_id: str,
    output_dir: Path,
) -> dict[str, Any]:
    provenance, provenance_raw = _json(provenance_path)
    proven_repository, authority, base, recipe = _validate_provenance(provenance)
    if repository != proven_repository or not REPOSITORY.fullmatch(repository):
        _fail("workflow repository does not match provenance")
    if source_sha != authority["head_sha"] or not COMMIT.fullmatch(source_sha):
        _fail("workflow source does not match provenance authority")
    if type(workflow_run_id) is not int or workflow_run_id <= 0:
        _fail("custom workflow run ID is invalid")
    signing = _signing_provenance(signing_path)
    if signing["source_commit"] != source_sha or int(signing["build_run"]) != workflow_run_id:
        _fail("signing source or run does not match this custom workflow")
    custom = provenance["custom_installer"]
    if signing["build_unsigned_outer_sha256"] != custom["sha256"]:
        _fail("signing provenance does not bind the unsigned custom installer")
    if signing["custom_package_provenance_sha256"] != hashlib.sha256(
        provenance_raw
    ).hexdigest():
        _fail("signing provenance does not bind the custom provenance bytes")
    actual_signed_sha = _sha256(installer)
    if (
        not SHA256.fullmatch(signed_sha256)
        or signing["signed_outer_sha256"] != signed_sha256
        or actual_signed_sha != signed_sha256
    ):
        _fail("signed installer hash does not match signing evidence")
    try:
        signed_size = installer.stat().st_size
    except OSError as exc:
        _fail(f"could not inspect signed installer: {exc}")
    if signed_size <= 0:
        _fail("signed installer is empty")
    release, _raw = _json(release_path)
    assets = _validate_release(release, repository, base, release_source_sha)
    base_assets = _validate_base_assets(
        assets,
        repository,
        base,
        pieces_path,
        image_sha_path,
        image_bytes_path,
    )
    recipe_hex = recipe["id"][7:]
    installer_name = f"AurOS-Custom-{recipe_hex}.exe"
    manifest_name = f"AurOS-Custom-{recipe_hex}.json"
    existing_installer = installer_name in assets
    existing_manifest = manifest_name in assets
    if existing_installer:
        _asset(
            assets,
            installer_name,
            repository,
            base["tag"],
            sha256=signed_sha256,
            size=signed_size,
        )
    if existing_manifest:
        if not existing_installer:
            _fail("release has a custom manifest without its signed installer")
        manifest_asset = _asset(
            assets, manifest_name, repository, base["tag"]
        )
        if (
            type(manifest_asset.get("size")) is not int
            or manifest_asset["size"] <= 0
            or not re.fullmatch(r"sha256:[0-9a-f]{64}", str(manifest_asset.get("digest")))
        ):
            _fail("existing custom manifest has invalid immutable identity")
    normalized_request_id = validate_request_id(request_id)
    output_dir.mkdir(parents=True, exist_ok=True)
    _copy(installer, output_dir / installer_name)
    plan = {
        "schema": "org.auros.custom-delivery-plan",
        "version": 2,
        "state": "verified-signed-awaiting-publication",
        "request_id": normalized_request_id,
        "repository": repository,
        "authority": {
            "head_sha": source_sha,
            "workflow_run_id": workflow_run_id,
        },
        "release_id": release.get("id"),
        "base_assets": base_assets,
        "base": {
            "head_sha": base["head_sha"],
            "build_run_id": base["run_id"],
            "tag": base["tag"],
            "image_sha256": base["image_sha256"],
            "image_bytes": base["image_bytes"],
        },
        "recipe": recipe,
        "installer": {
            "name": installer_name,
            "sha256": signed_sha256,
            "bytes": signed_size,
        },
        "manifest_name": manifest_name,
        "existing_installer": existing_installer,
        "existing_manifest": existing_manifest,
    }
    if type(plan["release_id"]) is not int or plan["release_id"] <= 0:
        _fail("base release ID is invalid")
    _write_json(output_dir / "delivery-plan.json", plan)
    return plan


PLAN_FIELDS = {
    "schema",
    "version",
    "state",
    "request_id",
    "repository",
    "authority",
    "release_id",
    "base_assets",
    "base",
    "recipe",
    "installer",
    "manifest_name",
    "existing_installer",
    "existing_manifest",
}


def _plan(path: Path) -> dict[str, Any]:
    plan, _raw = _json(path)
    _exact(plan, PLAN_FIELDS, "delivery plan")
    if (
        plan.get("schema") != "org.auros.custom-delivery-plan"
        or type(plan.get("version")) is not int
        or plan.get("version") != 2
    ):
        _fail("delivery plan schema or version is invalid")
    if plan.get("state") != "verified-signed-awaiting-publication":
        _fail("delivery plan state is invalid")
    request_id = plan.get("request_id")
    if request_id is not None and not isinstance(request_id, str):
        _fail("delivery plan request_id is invalid")
    validate_request_id(request_id or "")
    if not isinstance(plan.get("repository"), str) or not REPOSITORY.fullmatch(
        plan["repository"]
    ):
        _fail("delivery plan repository is invalid")
    authority = plan.get("authority")
    if not isinstance(authority, dict):
        _fail("delivery plan authority is invalid")
    _exact(authority, {"head_sha", "workflow_run_id"}, "plan authority")
    if not COMMIT.fullmatch(str(authority.get("head_sha"))):
        _fail("delivery plan authority source is invalid")
    if (
        type(authority.get("workflow_run_id")) is not int
        or authority["workflow_run_id"] <= 0
    ):
        _fail("delivery plan authority run is invalid")
    if type(plan.get("release_id")) is not int or plan["release_id"] <= 0:
        _fail("delivery plan release is invalid")
    for field in ("existing_installer", "existing_manifest"):
        if type(plan.get(field)) is not bool:
            _fail(f"delivery plan {field} is invalid")
    if plan["existing_manifest"] and not plan["existing_installer"]:
        _fail("delivery plan has a manifest without an installer")
    base = plan.get("base")
    if not isinstance(base, dict):
        _fail("delivery plan base is invalid")
    _exact(
        base,
        {"head_sha", "build_run_id", "tag", "image_sha256", "image_bytes"},
        "plan base",
    )
    if type(base.get("build_run_id")) is not int or base["build_run_id"] <= 0:
        _fail("delivery plan base run is invalid")
    if (
        not COMMIT.fullmatch(str(base.get("head_sha")))
        or not TAG.fullmatch(str(base.get("tag")))
        or str(base.get("tag")).rsplit("-", 1)[1] != str(base.get("head_sha"))[:7]
        or not SHA256.fullmatch(str(base.get("image_sha256")))
    ):
        _fail("delivery plan base identity is invalid")
    if type(base.get("image_bytes")) is not int or base["image_bytes"] <= 0:
        _fail("delivery plan image size is invalid")
    recipe = _validate_recipe(plan.get("recipe"))
    installer = plan.get("installer")
    if not isinstance(installer, dict):
        _fail("delivery plan installer is invalid")
    _exact(installer, {"name", "sha256", "bytes"}, "plan installer")
    recipe_hex = recipe["id"][7:]
    if installer.get("name") != f"AurOS-Custom-{recipe_hex}.exe":
        _fail("delivery plan installer name is invalid")
    if not SHA256.fullmatch(str(installer.get("sha256"))):
        _fail("delivery plan installer hash is invalid")
    if type(installer.get("bytes")) is not int or installer["bytes"] <= 0:
        _fail("delivery plan installer size is invalid")
    if plan.get("manifest_name") != f"AurOS-Custom-{recipe_hex}.json":
        _fail("delivery plan manifest name is invalid")
    base_assets = plan.get("base_assets")
    if not isinstance(base_assets, list) or not 5 <= len(base_assets) <= 1000:
        _fail("delivery plan base asset snapshot is invalid")
    names: set[str] = set()
    for asset in base_assets:
        if not isinstance(asset, dict):
            _fail("delivery plan contains a malformed base asset")
        _exact(asset, {"name", "id", "size", "digest", "url"}, "plan base asset")
        name = asset.get("name")
        if not isinstance(name, str) or name in names or not (
            name in {"pieces.txt", "image.sha256", "image.bytes", "AurOS-Setup.exe"}
            or PIECE.fullmatch(name)
        ):
            _fail("delivery plan contains an invalid or duplicate base asset")
        names.add(name)
        if type(asset.get("id")) is not int or asset["id"] <= 0:
            _fail("delivery plan base asset ID is invalid")
        if type(asset.get("size")) is not int or asset["size"] <= 0:
            _fail("delivery plan base asset size is invalid")
        if not re.fullmatch(r"sha256:[0-9a-f]{64}", str(asset.get("digest"))):
            _fail("delivery plan base asset digest is invalid")
        expected_url = (
            f"https://github.com/{plan['repository']}/releases/download/"
            f"{base['tag']}/{name}"
        )
        if asset.get("url") != expected_url:
            _fail("delivery plan base asset URL is invalid")
    if not {"pieces.txt", "image.sha256", "image.bytes", "AurOS-Setup.exe"} <= names:
        _fail("delivery plan is missing required base assets")
    return plan


def _manifest_value(
    plan: dict[str, Any],
    release: dict[str, Any],
    release_source_sha: str,
    readback_installer: Path,
) -> dict[str, Any]:
    base = plan["base"]
    if (
        release.get("id") != plan["release_id"]
        or release.get("tag_name") != base["tag"]
        or release.get("draft") is not False
        or release.get("prerelease") is not True
    ):
        _fail("base prerelease identity changed before manifest creation")
    if release_source_sha != base["head_sha"] or not COMMIT.fullmatch(
        release_source_sha
    ):
        _fail("base release tag no longer resolves to the proven source")
    _validate_base_snapshot(plan, release)
    assets = _assets(release)
    installer = plan["installer"]
    asset = _asset(
        assets,
        installer["name"],
        plan["repository"],
        base["tag"],
        sha256=installer["sha256"],
        size=installer["bytes"],
    )
    if (
        readback_installer.stat().st_size != installer["bytes"]
        or _sha256(readback_installer) != installer["sha256"]
    ):
        _fail("published signed installer readback does not match")
    return {
        "schema": "org.auros.custom-delivery",
        "version": 2,
        "state": "download-ready",
        "request_id": plan["request_id"],
        "repository": plan["repository"],
        "authority": plan["authority"],
        "base": base,
        "recipe": plan["recipe"],
        "installer": {
            "name": installer["name"],
            "url": asset["browser_download_url"],
            "sha256": installer["sha256"],
            "bytes": installer["bytes"],
        },
    }


def make_manifest(
    plan_path: Path,
    release_path: Path,
    release_source_sha: str,
    readback_installer: Path,
    output: Path,
) -> dict[str, Any]:
    plan = _plan(plan_path)
    release, _raw = _json(release_path)
    manifest = _manifest_value(plan, release, release_source_sha, readback_installer)
    _write_json(output, manifest)
    return manifest


def verify_ready(
    plan_path: Path,
    release_path: Path,
    release_source_sha: str,
    expected_manifest: Path,
    readback_manifest: Path,
    expected_installer: Path,
    readback_installer: Path,
) -> str:
    plan = _plan(plan_path)
    expected_raw = _read(expected_manifest)
    if expected_raw != _read(readback_manifest):
        _fail("published delivery manifest readback changed")
    manifest, _raw = _json(readback_manifest)
    release, _release_raw = _json(release_path)
    expected_manifest_value = _manifest_value(
        plan, release, release_source_sha, readback_installer
    )
    if manifest != expected_manifest_value:
        _fail("published delivery manifest claims do not match verified evidence")
    if (
        expected_installer.stat().st_size != readback_installer.stat().st_size
        or _sha256(expected_installer) != _sha256(readback_installer)
    ):
        _fail("final signed installer readback changed")
    assets = _assets(release)
    manifest_asset = _asset(
        assets,
        plan["manifest_name"],
        plan["repository"],
        plan["base"]["tag"],
        sha256=hashlib.sha256(expected_raw).hexdigest(),
        size=len(expected_raw),
    )
    if manifest_asset.get("name") != plan["manifest_name"]:
        _fail("published manifest asset identity changed")
    return manifest["installer"]["url"]


def _outputs(path: Path, plan: dict[str, Any]) -> None:
    values = {
        "tag": plan["base"]["tag"],
        "installer_name": plan["installer"]["name"],
        "manifest_name": plan["manifest_name"],
        "installer_exists": str(plan["existing_installer"]).lower(),
        "manifest_exists": str(plan["existing_manifest"]).lower(),
    }
    with path.open("a", encoding="utf-8", newline="\n") as stream:
        for key, value in values.items():
            if not re.fullmatch(r"[a-z_]+", key) or not re.fullmatch(
                r"[A-Za-z0-9._+-]+", value
            ):
                _fail("refusing unsafe GitHub output")
            stream.write(f"{key}={value}\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    request = sub.add_parser("validate-request-id")
    request.add_argument("--request-id", default="")
    source_ref = sub.add_parser("validate-source-ref")
    source_ref.add_argument("--ref", required=True)
    source_ref.add_argument("--source-sha", required=True)
    source_ref.add_argument("--provenance", required=True, type=Path)
    prepare_parser = sub.add_parser("prepare")
    prepare_parser.add_argument("--provenance", required=True, type=Path)
    prepare_parser.add_argument("--signing-provenance", required=True, type=Path)
    prepare_parser.add_argument("--installer", required=True, type=Path)
    prepare_parser.add_argument("--release", required=True, type=Path)
    prepare_parser.add_argument("--pieces", required=True, type=Path)
    prepare_parser.add_argument("--image-sha", required=True, type=Path)
    prepare_parser.add_argument("--image-bytes", required=True, type=Path)
    prepare_parser.add_argument("--repository", required=True)
    prepare_parser.add_argument("--source-sha", required=True)
    prepare_parser.add_argument("--release-source-sha", required=True)
    prepare_parser.add_argument("--workflow-run-id", required=True, type=int)
    prepare_parser.add_argument("--signed-sha256", required=True)
    prepare_parser.add_argument("--request-id", default="")
    prepare_parser.add_argument("--output-dir", required=True, type=Path)
    prepare_parser.add_argument("--github-output", required=True, type=Path)
    manifest_parser = sub.add_parser("make-manifest")
    manifest_parser.add_argument("--plan", required=True, type=Path)
    manifest_parser.add_argument("--release", required=True, type=Path)
    manifest_parser.add_argument("--release-source-sha", required=True)
    manifest_parser.add_argument("--readback-installer", required=True, type=Path)
    manifest_parser.add_argument("--output", required=True, type=Path)
    verify_parser = sub.add_parser("verify-ready")
    verify_parser.add_argument("--plan", required=True, type=Path)
    verify_parser.add_argument("--release", required=True, type=Path)
    verify_parser.add_argument("--release-source-sha", required=True)
    verify_parser.add_argument("--expected-manifest", required=True, type=Path)
    verify_parser.add_argument("--readback-manifest", required=True, type=Path)
    verify_parser.add_argument("--expected-installer", required=True, type=Path)
    verify_parser.add_argument("--readback-installer", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "validate-request-id":
            validate_request_id(args.request_id)
        elif args.command == "validate-source-ref":
            validate_source_ref(args.ref, args.source_sha, args.provenance)
        elif args.command == "prepare":
            plan = prepare(
                provenance_path=args.provenance,
                signing_path=args.signing_provenance,
                installer=args.installer,
                release_path=args.release,
                pieces_path=args.pieces,
                image_sha_path=args.image_sha,
                image_bytes_path=args.image_bytes,
                repository=args.repository,
                source_sha=args.source_sha,
                release_source_sha=args.release_source_sha,
                workflow_run_id=args.workflow_run_id,
                signed_sha256=args.signed_sha256,
                request_id=args.request_id,
                output_dir=args.output_dir,
            )
            _outputs(args.github_output, plan)
        elif args.command == "make-manifest":
            make_manifest(
                args.plan,
                args.release,
                args.release_source_sha,
                args.readback_installer,
                args.output,
            )
        else:
            print(
                verify_ready(
                    args.plan,
                    args.release,
                    args.release_source_sha,
                    args.expected_manifest,
                    args.readback_manifest,
                    args.expected_installer,
                    args.readback_installer,
                )
            )
    except (DeliveryError, OSError) as exc:
        print(f"custom delivery: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
