#!/usr/bin/env python3
"""Retain profile images and bind successful runtime evidence.

Runtime records describe bounded evidence only. Consumers must validate the
completed source run and live backing bytes; a record never authorizes delivery.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
import re
import tempfile
from pathlib import Path
from typing import Any, BinaryIO


SCHEMA = "auros-profile-image-v1"
DIAGNOSTIC_SCHEMA = "auros-profile-diagnostic-v1"
RUNTIME_SCHEMA = "auros-profile-runtime-image-v1"
WORKFLOW = ".github/workflows/profiles.yml"
PROFILE = "multilingual"
PIECE_PREFIX = "auros-multilingual.img.gz."
MANIFEST_NAME = "multilingual-image-v1.json"
ARTIFACT_NAME = "profile-multilingual"
MAX_MANIFEST_BYTES = 65536
MAX_ARTIFACT_BYTES = 128 * 1024 * 1024
MAX_PIECE_BYTES = 94371840
MAX_PIECES = 256
MAX_GZIP_BYTES = MAX_PIECE_BYTES * MAX_PIECES
MAX_IMAGE_BYTES = 64 * 1024 * 1024 * 1024
SHA = re.compile(r"[0-9a-f]{40}")
SHA256 = re.compile(r"[0-9a-f]{64}")
REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
PIECE = re.compile(r"auros-multilingual\.img\.gz\.([0-9]{3})")
RUNTIME_PROFILES = {"multilingual", "revive"}


class RetentionError(ValueError):
    pass


def fail(message: str) -> None:
    raise RetentionError(message)


def _integer(value: Any, name: str, low: int, high: int) -> int:
    if type(value) is not int or not low <= value <= high:
        fail(f"{name} must be an integer from {low} through {high}")
    return value


def _exact(obj: Any, keys: set[str], name: str) -> dict[str, Any]:
    if type(obj) is not dict or set(obj) != keys:
        fail(f"{name} has unknown or missing fields")
    return obj


def _hash(path: Path) -> tuple[int, str]:
    size = 0
    digest = hashlib.sha256()
    with path.open("rb") as src:
        while block := src.read(1024 * 1024):
            size += len(block)
            digest.update(block)
    return size, digest.hexdigest()


def _write_json(path: Path, value: dict[str, Any]) -> None:
    data = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()
    if len(data) > MAX_MANIFEST_BYTES:
        fail("manifest is larger than 64 KiB")
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as dst:
            dst.write(data)
        os.replace(temporary, path)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def _parse_json(raw: bytes, name: str) -> dict[str, Any]:
    def pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
        value: dict[str, Any] = {}
        for key, item in items:
            if key in value:
                fail(f"{name} has duplicate fields")
            value[key] = item
        return value

    try:
        value = json.loads(raw, object_pairs_hook=pairs)
    except (UnicodeError, json.JSONDecodeError) as exc:
        fail(f"{name} is not valid JSON: {exc}")
    if type(value) is not dict:
        fail(f"{name} must be a JSON object")
    return value


def _load_json(path: Path, name: str, limit: int = MAX_MANIFEST_BYTES) -> tuple[dict[str, Any], bytes]:
    try:
        if path.is_symlink() or not path.is_file() or path.stat().st_size > limit:
            fail(f"{name} is missing, is a link, or is too large")
        raw = path.read_bytes()
    except OSError as exc:
        fail(f"{name} is not valid JSON: {exc}")
    value = _parse_json(raw, name)
    return value, raw


def validate_manifest(value: Any, repository: str | None = None,
                      run_id: int | None = None, source_sha: str | None = None) -> dict[str, Any]:
    root = _exact(value, {"schema", "profile", "repository", "workflow", "image_run_id",
                          "image_source_sha", "image", "pieces"}, "image manifest")
    if root["schema"] != SCHEMA or root["profile"] != PROFILE or root["workflow"] != WORKFLOW:
        fail("image manifest identifies the wrong schema, profile, or workflow")
    if type(root["repository"]) is not str or not REPOSITORY.fullmatch(root["repository"]):
        fail("image manifest repository is invalid")
    if repository is not None and root["repository"] != repository:
        fail("image manifest belongs to another repository")
    _integer(root["image_run_id"], "image_run_id", 1, 10**20)
    if run_id is not None and root["image_run_id"] != run_id:
        fail("image manifest belongs to another run")
    if type(root["image_source_sha"]) is not str or not SHA.fullmatch(root["image_source_sha"]):
        fail("image_source_sha is invalid")
    if source_sha is not None and root["image_source_sha"] != source_sha:
        fail("image manifest source SHA does not match the source run")
    image = _exact(root["image"], {"bytes", "sha256", "gzip_bytes", "gzip_sha256"}, "image facts")
    _integer(image["bytes"], "image bytes", 1, MAX_IMAGE_BYTES)
    _integer(image["gzip_bytes"], "gzip bytes", 1, MAX_GZIP_BYTES)
    if type(image["sha256"]) is not str or not SHA256.fullmatch(image["sha256"]):
        fail("image sha256 is invalid")
    if type(image["gzip_sha256"]) is not str or not SHA256.fullmatch(image["gzip_sha256"]):
        fail("gzip sha256 is invalid")
    if type(root["pieces"]) is not list or not 1 <= len(root["pieces"]) <= MAX_PIECES:
        fail("pieces must be a nonempty bounded list")
    total = 0
    for index, item in enumerate(root["pieces"]):
        piece = _exact(item, {"name", "bytes", "sha256"}, f"piece {index}")
        match = PIECE.fullmatch(piece["name"]) if type(piece["name"]) is str else None
        if not match or int(match.group(1)) != index:
            fail("piece names must be safe, ordered, and contiguous from 000")
        size = _integer(piece["bytes"], f"piece {index} bytes", 1, MAX_PIECE_BYTES)
        if index + 1 < len(root["pieces"]) and size != MAX_PIECE_BYTES:
            fail("every non-final piece must use the 90 MiB convention")
        if type(piece["sha256"]) is not str or not SHA256.fullmatch(piece["sha256"]):
            fail(f"piece {index} sha256 is invalid")
        total += size
    if total != image["gzip_bytes"]:
        fail("piece sizes do not add up to gzip_bytes")
    return root


def create_manifest(image_path: Path, gzip_path: Path, pieces_dir: Path,
                    repository: str, run_id: int, source_sha: str) -> dict[str, Any]:
    if not REPOSITORY.fullmatch(repository) or not SHA.fullmatch(source_sha):
        fail("repository or source SHA is invalid")
    _integer(run_id, "run_id", 1, 10**20)
    if image_path.is_symlink() or gzip_path.is_symlink():
        fail("image inputs may not be links")
    image_bytes, image_sha = _hash(image_path)
    gzip_bytes, gzip_sha = _hash(gzip_path)
    _integer(image_bytes, "image bytes", 1, MAX_IMAGE_BYTES)
    _integer(gzip_bytes, "gzip bytes", 1, MAX_GZIP_BYTES)
    paths = sorted(pieces_dir.glob(PIECE_PREFIX + "*"))
    pieces = []
    combined = hashlib.sha256()
    combined_bytes = 0
    for index, path in enumerate(paths):
        if path.is_symlink() or not path.is_file() or path.name != f"{PIECE_PREFIX}{index:03d}":
            fail("piece files must be regular, ordered, and contiguous from 000")
        size, digest = _hash(path)
        _integer(size, f"piece {index} bytes", 1, MAX_PIECE_BYTES)
        if index + 1 < len(paths) and size != MAX_PIECE_BYTES:
            fail("every non-final piece must use the 90 MiB convention")
        with path.open("rb") as src:
            while block := src.read(1024 * 1024):
                combined.update(block)
        combined_bytes += size
        pieces.append({"name": path.name, "bytes": size, "sha256": digest})
    if not pieces or len(pieces) > MAX_PIECES:
        fail("piece file count is outside the bounded range")
    if combined_bytes != gzip_bytes or combined.hexdigest() != gzip_sha:
        fail("pieces do not reconstruct the supplied gzip")
    value = {
        "schema": SCHEMA,
        "profile": PROFILE,
        "repository": repository,
        "workflow": WORKFLOW,
        "image_run_id": run_id,
        "image_source_sha": source_sha,
        "image": {"bytes": image_bytes, "sha256": image_sha,
                  "gzip_bytes": gzip_bytes, "gzip_sha256": gzip_sha},
        "pieces": pieces,
    }
    return validate_manifest(value, repository, run_id, source_sha)


def validate_source(run: Any, artifacts: Any, repository: str, run_id: int) -> tuple[str, int]:
    if not REPOSITORY.fullmatch(repository):
        fail("repository is invalid")
    _integer(run_id, "run_id", 1, 10**20)
    if type(run) is not dict:
        fail("source run response is invalid")
    head_repo = run.get("head_repository")
    checks = [
        run.get("id") == run_id,
        run.get("status") == "completed",
        run.get("event") == "push",
        run.get("head_branch") == "main",
        run.get("path") == WORKFLOW,
        type(head_repo) is dict and head_repo.get("full_name") == repository,
        type(run.get("head_sha")) is str and SHA.fullmatch(run["head_sha"]) is not None,
    ]
    if not all(checks):
        fail("source run is not a completed main push of profiles.yml in this repository")
    if type(artifacts) is not dict or type(artifacts.get("artifacts")) is not list:
        fail("source artifact response is invalid")
    total = artifacts.get("total_count")
    if type(total) is not int or total != len(artifacts["artifacts"]) or total > 100:
        fail("source artifact response is incomplete or unbounded")
    matches = []
    for artifact in artifacts["artifacts"]:
        if type(artifact) is not dict or artifact.get("name") != ARTIFACT_NAME or artifact.get("expired") is not False:
            continue
        workflow_run = artifact.get("workflow_run")
        if type(workflow_run) is not dict or workflow_run.get("id") != run_id:
            continue
        aid = artifact.get("id")
        size = artifact.get("size_in_bytes")
        if type(aid) is not int or aid <= 0 or type(size) is not int or not 1 <= size <= MAX_ARTIFACT_BYTES:
            fail("source artifact has an invalid id or unbounded size")
        matches.append(aid)
    if len(matches) != 1:
        fail("source run must have exactly one live profile-multilingual artifact")
    return run["head_sha"], matches[0]


def validate_release(manifest: dict[str, Any], manifest_raw: bytes, release: Any,
                     repository: str, run_id: int) -> list[tuple[int, str, int]]:
    validate_manifest(manifest, repository, run_id)
    if type(release) is not dict or release.get("tag_name") != f"profiles-{run_id}" \
            or release.get("prerelease") is not True or type(release.get("assets")) is not list:
        fail("release is not the source run's profiles prerelease")
    assets: dict[str, Any] = {}
    asset_ids: set[int] = set()
    for asset in release["assets"]:
        if type(asset) is not dict or type(asset.get("name")) is not str:
            fail("release has malformed asset metadata")
        if asset["name"] in assets:
            fail("release has duplicate asset names")
        asset_id = asset.get("id")
        if type(asset_id) is not int or asset_id <= 0 or asset_id in asset_ids:
            fail("release has a missing or duplicate asset id")
        asset_ids.add(asset_id)
        assets[asset["name"]] = asset
    expected = [(MANIFEST_NAME, len(manifest_raw), hashlib.sha256(manifest_raw).hexdigest())]
    expected += [(item["name"], item["bytes"], item["sha256"]) for item in manifest["pieces"]]
    expected_piece_names = {item["name"] for item in manifest["pieces"]}
    release_piece_names = {name for name in assets if name.startswith(PIECE_PREFIX)}
    if release_piece_names != expected_piece_names:
        fail("release piece set does not exactly match immutable manifest evidence")
    plan = []
    for name, size, digest in expected:
        asset = assets.get(name)
        if asset is None or asset.get("size") != size or asset.get("digest") != f"sha256:{digest}":
            fail(f"release asset {name} does not match immutable manifest evidence")
        plan.append((asset["id"], name, size))
    return plan


def validate_plan(anchor: dict[str, Any], anchor_raw: bytes, release: Any,
                  repository: str, run_id: int,
                  source_sha: str) -> tuple[dict[str, Any], list[tuple[int, str, int]]]:
    anchor = validate_manifest(anchor, repository, run_id, source_sha)
    plan = validate_release(anchor, anchor_raw, release, repository, run_id)
    return anchor, plan


def validate_downloaded_manifest(anchor_raw: bytes, release_raw: bytes) -> None:
    if anchor_raw != release_raw:
        fail("downloaded release manifest differs from immutable artifact evidence")


class PieceReader(io.RawIOBase):
    def __init__(self, paths: list[Path]):
        self.paths = iter(paths)
        self.current: BinaryIO | None = None

    def readable(self) -> bool:
        return True

    def readinto(self, target: bytearray) -> int:
        view = memoryview(target)
        written = 0
        while written < len(view):
            if self.current is None:
                try:
                    self.current = next(self.paths).open("rb")
                except StopIteration:
                    break
            n = self.current.readinto(view[written:])
            if n:
                written += n
            else:
                self.current.close()
                self.current = None
        return written

    def close(self) -> None:
        if self.current is not None:
            self.current.close()
        super().close()


def restore(manifest: dict[str, Any], pieces_dir: Path, output: Path) -> None:
    manifest = validate_manifest(manifest)
    expected_names = [item["name"] for item in manifest["pieces"]]
    actual = sorted(path.name for path in pieces_dir.glob(PIECE_PREFIX + "*"))
    if actual != expected_names:
        fail("downloaded piece set does not exactly match the manifest")
    gzip_digest = hashlib.sha256()
    gzip_bytes = 0
    paths = []
    for item in manifest["pieces"]:
        path = pieces_dir / item["name"]
        if path.is_symlink() or not path.is_file():
            fail(f"piece {item['name']} is missing or is a link")
        size, digest = _hash(path)
        if size != item["bytes"] or digest != item["sha256"]:
            fail(f"piece {item['name']} failed size or hash verification")
        with path.open("rb") as src:
            while block := src.read(1024 * 1024):
                gzip_bytes += len(block)
                gzip_digest.update(block)
        paths.append(path)
    image = manifest["image"]
    if gzip_bytes != image["gzip_bytes"] or gzip_digest.hexdigest() != image["gzip_sha256"]:
        fail("combined gzip failed size or hash verification")
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=output.name + ".", dir=output.parent)
    image_digest = hashlib.sha256()
    image_bytes = 0
    try:
        with os.fdopen(fd, "wb") as dst, PieceReader(paths) as joined, gzip.GzipFile(fileobj=joined) as src:
            while block := src.read(1024 * 1024):
                image_bytes += len(block)
                if image_bytes > image["bytes"]:
                    fail("unpacked image is larger than its immutable manifest")
                image_digest.update(block)
                if block.count(0) == len(block):
                    dst.seek(len(block), os.SEEK_CUR)
                else:
                    dst.write(block)
            dst.truncate(image_bytes)
        if image_bytes != image["bytes"] or image_digest.hexdigest() != image["sha256"]:
            fail("unpacked image failed size or hash verification")
        os.replace(temporary, output)
    except (OSError, EOFError, gzip.BadGzipFile) as exc:
        fail(f"could not unpack retained image: {exc}")
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def diagnostic(manifest: dict[str, Any], test_run_id: int, test_source_sha: str,
               result: str) -> dict[str, Any]:
    manifest = validate_manifest(manifest)
    _integer(test_run_id, "test_run_id", 1, 10**20)
    if not SHA.fullmatch(test_source_sha) or result not in {"pass", "fail"}:
        fail("diagnostic test SHA or result is invalid")
    return {
        "schema": DIAGNOSTIC_SCHEMA,
        "state": "diagnostic-retained-image",
        "result": result,
        "repository": manifest["repository"],
        "profile": PROFILE,
        "image_run_id": manifest["image_run_id"],
        "image_source_sha": manifest["image_source_sha"],
        "image_sha256": manifest["image"]["sha256"],
        "image_bytes": manifest["image"]["bytes"],
        "test_run_id": test_run_id,
        "test_source_sha": test_source_sha,
    }


def _file_fact(value: Any, name: str, max_bytes: int = MAX_GZIP_BYTES) -> dict[str, Any]:
    value = _exact(value, {"name", "bytes", "sha256"}, name)
    if type(value["name"]) is not str or not value["name"] or "/" in value["name"] \
            or "\\" in value["name"] or value["name"] in {".", ".."}:
        fail(f"{name} has an unsafe name")
    _integer(value["bytes"], f"{name} bytes", 1, max_bytes)
    if type(value["sha256"]) is not str or not SHA256.fullmatch(value["sha256"]):
        fail(f"{name} sha256 is invalid")
    return value


def validate_runtime_record(value: Any, repository: str | None = None,
                            run_id: int | None = None, source_sha: str | None = None,
                            profile: str | None = None,
                            run_attempt: int | None = None) -> dict[str, Any]:
    if repository is not None and (type(repository) is not str or not REPOSITORY.fullmatch(repository)):
        fail("expected runtime repository is invalid")
    if run_id is not None:
        _integer(run_id, "expected runtime run_id", 1, 10**20)
    if source_sha is not None and (type(source_sha) is not str or not SHA.fullmatch(source_sha)):
        fail("expected runtime source SHA is invalid")
    if profile is not None and (type(profile) is not str or profile not in RUNTIME_PROFILES):
        fail("expected runtime profile is invalid")
    if run_attempt is not None:
        _integer(run_attempt, "expected runtime run_attempt", 1, 10**9)
    root = _exact(value, {"schema", "state", "purpose", "profile", "repository", "workflow",
                          "image_run_id", "image_source_sha", "source_manifest",
                          "runtime_proof", "image", "storage", "availability"},
                  "runtime image record")
    if type(root["profile"]) is not str:
        fail("runtime image profile is invalid")
    if root["schema"] != RUNTIME_SCHEMA or root["state"] != "runtime-certified-evidence-only" \
            or root["purpose"] != "profile-runtime-evidence" \
            or root["workflow"] != WORKFLOW or root["profile"] not in RUNTIME_PROFILES:
        fail("runtime image record has the wrong schema, state, profile, or workflow")
    if profile is not None and root["profile"] != profile:
        fail("runtime image record identifies another profile")
    if type(root["repository"]) is not str or not REPOSITORY.fullmatch(root["repository"]):
        fail("runtime image repository is invalid")
    if repository is not None and root["repository"] != repository:
        fail("runtime image record belongs to another repository")
    _integer(root["image_run_id"], "image_run_id", 1, 10**20)
    if run_id is not None and root["image_run_id"] != run_id:
        fail("runtime image record belongs to another run")
    if type(root["image_source_sha"]) is not str or not SHA.fullmatch(root["image_source_sha"]):
        fail("runtime image source SHA is invalid")
    if source_sha is not None and root["image_source_sha"] != source_sha:
        fail("runtime image record belongs to another source SHA")

    source = _exact(root["source_manifest"], {"name", "schema", "bytes", "sha256"},
                    "source manifest identity")
    expected_source = ((MANIFEST_NAME, SCHEMA) if root["profile"] == PROFILE
                       else ("manifest.json", "auros-revive-retained-v1"))
    if (source["name"], source["schema"]) != expected_source:
        fail("runtime image record names the wrong source manifest")
    _integer(source["bytes"], "source manifest bytes", 1, MAX_MANIFEST_BYTES)
    if type(source["sha256"]) is not str or not SHA256.fullmatch(source["sha256"]):
        fail("source manifest sha256 is invalid")

    proof = _exact(root["runtime_proof"], {"run_id", "run_attempt", "source_sha", "job", "mode"},
                   "runtime proof")
    _integer(proof["run_id"], "runtime proof run_id", 1, 10**20)
    _integer(proof["run_attempt"], "runtime proof run_attempt", 1, 10**9)
    if run_attempt is not None and proof["run_attempt"] != run_attempt:
        fail("runtime proof belongs to another run attempt")
    if type(proof["source_sha"]) is not str or not SHA.fullmatch(proof["source_sha"]):
        fail("runtime proof source SHA is invalid")
    if type(proof["job"]) is not str or type(proof["mode"]) is not str:
        fail("runtime proof job or mode is invalid")
    expected_mode = ("multilingual-functional-v1" if root["profile"] == PROFILE
                     else "revive-functional-v1")
    if (proof["run_id"] != root["image_run_id"] or
            proof["source_sha"] != root["image_source_sha"] or
            proof["job"] != root["profile"] or proof["mode"] != expected_mode):
        fail("runtime proof is not the image's exact functional profile job")

    image = _exact(root["image"], {"bytes", "sha256", "compression",
                                   "compressed_bytes", "compressed_sha256"}, "runtime image facts")
    _integer(image["bytes"], "runtime image bytes", 1, MAX_IMAGE_BYTES)
    _integer(image["compressed_bytes"], "runtime compressed bytes", 1, MAX_GZIP_BYTES)
    if image["compression"] != "gzip" or any(
            type(image[key]) is not str or not SHA256.fullmatch(image[key])
            for key in ("sha256", "compressed_sha256")):
        fail("runtime image hashes or compression are invalid")

    storage = root["storage"]
    if root["profile"] == "revive":
        storage = _exact(storage, {"type", "artifact_name", "manifest_name", "files"},
                         "runtime image storage")
        if (storage["type"], storage["artifact_name"], storage["manifest_name"]) != (
                "actions-artifact", "profile-revive-runtime-image", "manifest.json"):
            fail("Revive runtime image storage identity is invalid")
        if type(storage["files"]) is not list or len(storage["files"]) != 1:
            fail("Revive runtime image storage must contain one gzip")
        item = _file_fact(storage["files"][0], "Revive runtime image")
        if item["name"] != "auros-revive.img.gz":
            fail("Revive runtime image filename is invalid")
    else:
        storage = _exact(storage, {"type", "tag", "manifest_name", "files"},
                         "runtime image storage")
        if (storage["type"], storage["tag"], storage["manifest_name"]) != (
                "profile-prerelease-pieces", f"profiles-{root['image_run_id']}", MANIFEST_NAME):
            fail("Multilingual runtime image storage identity is invalid")
        if type(storage["files"]) is not list or not 1 <= len(storage["files"]) <= MAX_PIECES:
            fail("Multilingual runtime image storage has an invalid piece count")
        for index, entry in enumerate(storage["files"]):
            item = _file_fact(entry, f"Multilingual piece {index}", MAX_PIECE_BYTES)
            match = PIECE.fullmatch(item["name"])
            if not match or int(match.group(1)) != index:
                fail("Multilingual runtime image pieces are not safe, ordered, and contiguous")
            if index + 1 < len(storage["files"]) and item["bytes"] != MAX_PIECE_BYTES:
                fail("every non-final runtime image piece must use the 90 MiB convention")
    if sum(item["bytes"] for item in storage["files"]) != image["compressed_bytes"]:
        fail("runtime storage sizes do not equal compressed_bytes")
    if root["profile"] == "revive" and storage["files"][0]["sha256"] != image["compressed_sha256"]:
        fail("Revive runtime storage hash differs from compressed_sha256")
    availability = _exact(root["availability"], {"metadata_retention", "backing_retention", "access",
                                                    "delivery_authorized"}, "runtime availability")
    expected_backing = ("profile-prerelease-pruned" if root["profile"] == PROFILE
                        else "actions-artifact-2-days")
    if type(availability["delivery_authorized"]) is not bool:
        fail("runtime delivery authority flag must be boolean")
    if (availability["metadata_retention"], availability["backing_retention"],
            availability["access"], availability["delivery_authorized"]) != (
                "actions-artifact-30-days", expected_backing,
                "requires-live-backing-validation", False):
        fail("runtime image availability or delivery authority is invalid")
    return root


def runtime_record(source_raw: bytes, profile: str, repository: str,
                   run_id: int, run_attempt: int, source_sha: str) -> dict[str, Any]:
    if type(source_raw) is not bytes or type(profile) is not str or profile not in RUNTIME_PROFILES \
            or type(repository) is not str or not REPOSITORY.fullmatch(repository) \
            or type(source_sha) is not str or not SHA.fullmatch(source_sha):
        fail("runtime profile or source identity is invalid")
    _integer(run_id, "run_id", 1, 10**20)
    _integer(run_attempt, "run_attempt", 1, 10**9)
    if not 1 <= len(source_raw) <= MAX_MANIFEST_BYTES:
        fail("source manifest is empty or oversized")
    source = _parse_json(source_raw, "retained image manifest")
    if profile == PROFILE:
        manifest = validate_manifest(source, repository, run_id, source_sha)
        image = manifest["image"]
        source_name, source_schema = MANIFEST_NAME, SCHEMA
        files = [dict(item) for item in manifest["pieces"]]
        storage = {"type": "profile-prerelease-pieces", "tag": f"profiles-{run_id}",
                   "manifest_name": MANIFEST_NAME, "files": files}
    else:
        manifest = _exact(source, {"schema", "profile", "repository", "workflow", "source_sha",
                                   "image_run_id", "raw", "compressed", "filename"},
                          "Revive retained image manifest")
        _integer(manifest["image_run_id"], "Revive image_run_id", 1, 10**20)
        if (manifest["schema"], manifest["profile"], manifest["repository"],
                manifest["workflow"], manifest["source_sha"], manifest["image_run_id"],
                manifest["filename"]) != ("auros-revive-retained-v1", "revive", repository,
                                           WORKFLOW, source_sha, run_id, "auros-revive.img.gz"):
            fail("Revive retained image manifest has the wrong identity")
        raw = _exact(manifest["raw"], {"bytes", "sha256"}, "Revive raw image facts")
        compressed = _exact(manifest["compressed"], {"bytes", "sha256"},
                            "Revive compressed image facts")
        _integer(raw["bytes"], "Revive raw image bytes", 1, MAX_IMAGE_BYTES)
        _integer(compressed["bytes"], "Revive compressed image bytes", 1, MAX_GZIP_BYTES)
        if any(type(item["sha256"]) is not str or not SHA256.fullmatch(item["sha256"])
               for item in (raw, compressed)):
            fail("Revive retained image hash is invalid")
        image = {"bytes": raw["bytes"], "sha256": raw["sha256"],
                 "gzip_bytes": compressed["bytes"], "gzip_sha256": compressed["sha256"]}
        source_name, source_schema = "manifest.json", "auros-revive-retained-v1"
        storage = {"type": "actions-artifact", "artifact_name": "profile-revive-runtime-image",
                   "manifest_name": source_name,
                   "files": [{"name": manifest["filename"], "bytes": compressed["bytes"],
                              "sha256": compressed["sha256"]}]}
    value = {
        "schema": RUNTIME_SCHEMA,
        "state": "runtime-certified-evidence-only",
        "purpose": "profile-runtime-evidence",
        "profile": profile,
        "repository": repository,
        "workflow": WORKFLOW,
        "image_run_id": run_id,
        "image_source_sha": source_sha,
        "source_manifest": {"name": source_name, "schema": source_schema,
                            "bytes": len(source_raw), "sha256": hashlib.sha256(source_raw).hexdigest()},
        "runtime_proof": {"run_id": run_id, "run_attempt": run_attempt,
                          "source_sha": source_sha, "job": profile,
                          "mode": f"{profile}-functional-v1"},
        "image": {"bytes": image["bytes"], "sha256": image["sha256"], "compression": "gzip",
                  "compressed_bytes": image["gzip_bytes"],
                  "compressed_sha256": image["gzip_sha256"]},
        "storage": storage,
        "availability": {"metadata_retention": "actions-artifact-30-days",
                         "backing_retention": ("profile-prerelease-pruned" if profile == PROFILE
                                               else "actions-artifact-2-days"),
                         "access": "requires-live-backing-validation",
                         "delivery_authorized": False},
    }
    return validate_runtime_record(value, repository, run_id, source_sha, profile, run_attempt)


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("create")
    create.add_argument("--image", type=Path, required=True)
    create.add_argument("--gzip", type=Path, required=True)
    create.add_argument("--pieces", type=Path, required=True)
    create.add_argument("--repository", required=True)
    create.add_argument("--run-id", type=int, required=True)
    create.add_argument("--source-sha", required=True)
    create.add_argument("--output", type=Path, required=True)
    source = sub.add_parser("source")
    source.add_argument("--run-json", type=Path, required=True)
    source.add_argument("--artifacts-json", type=Path, required=True)
    source.add_argument("--repository", required=True)
    source.add_argument("--run-id", type=int, required=True)
    source.add_argument("--github-output", type=Path, required=True)
    plan = sub.add_parser("plan")
    plan.add_argument("--anchor", type=Path, required=True)
    plan.add_argument("--release-json", type=Path, required=True)
    plan.add_argument("--repository", required=True)
    plan.add_argument("--run-id", type=int, required=True)
    plan.add_argument("--source-sha", required=True)
    plan.add_argument("--download-plan", type=Path, required=True)
    unpack = sub.add_parser("restore")
    unpack.add_argument("--anchor", type=Path, required=True)
    unpack.add_argument("--release-manifest", type=Path, required=True)
    unpack.add_argument("--pieces", type=Path, required=True)
    unpack.add_argument("--output", type=Path, required=True)
    diag = sub.add_parser("diagnostic")
    diag.add_argument("--anchor", type=Path, required=True)
    diag.add_argument("--test-run-id", type=int, required=True)
    diag.add_argument("--test-source-sha", required=True)
    diag.add_argument("--result", choices=("pass", "fail"), required=True)
    diag.add_argument("--output", type=Path, required=True)
    certify = sub.add_parser("certify")
    certify.add_argument("--source-manifest", type=Path, required=True)
    certify.add_argument("--profile", choices=sorted(RUNTIME_PROFILES), required=True)
    certify.add_argument("--repository", required=True)
    certify.add_argument("--run-id", type=int, required=True)
    certify.add_argument("--run-attempt", type=int, required=True)
    certify.add_argument("--source-sha", required=True)
    certify.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "create":
            value = create_manifest(args.image, args.gzip, args.pieces, args.repository,
                                    args.run_id, args.source_sha)
            _write_json(args.output, value)
        elif args.command == "source":
            run, _ = _load_json(args.run_json, "source run", 1024 * 1024)
            artifacts, _ = _load_json(args.artifacts_json, "source artifacts", 4 * 1024 * 1024)
            source_sha, artifact_id = validate_source(run, artifacts, args.repository, args.run_id)
            with args.github_output.open("a", encoding="utf-8") as out:
                out.write(f"source_sha={source_sha}\nartifact_id={artifact_id}\n")
        elif args.command == "plan":
            anchor, anchor_raw = _load_json(args.anchor, "immutable image manifest")
            release, _ = _load_json(args.release_json, "source release", 4 * 1024 * 1024)
            _, downloads = validate_plan(anchor, anchor_raw, release, args.repository,
                                         args.run_id, args.source_sha)
            args.download_plan.parent.mkdir(parents=True, exist_ok=True)
            with args.download_plan.open("x", encoding="ascii") as out:
                for asset_id, name, size in downloads:
                    out.write(f"{asset_id}\t{name}\t{size}\n")
        elif args.command == "restore":
            anchor, anchor_raw = _load_json(args.anchor, "immutable image manifest")
            _, release_raw = _load_json(args.release_manifest, "downloaded release manifest")
            validate_downloaded_manifest(anchor_raw, release_raw)
            restore(anchor, args.pieces, args.output)
        elif args.command == "diagnostic":
            anchor, _ = _load_json(args.anchor, "immutable image manifest")
            _write_json(args.output, diagnostic(anchor, args.test_run_id,
                                                args.test_source_sha, args.result))
        else:
            _, source_raw = _load_json(args.source_manifest, "retained image manifest")
            _write_json(args.output, runtime_record(source_raw, args.profile,
                                                    args.repository, args.run_id, args.run_attempt,
                                                    args.source_sha))
    except RetentionError as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
