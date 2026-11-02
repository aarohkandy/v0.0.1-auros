#!/usr/bin/env python3
import copy
import gzip
import hashlib
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import profile_image as pi


REPO = "owner/repository"
RUN = 40123456789
ATTEMPT = 3
SOURCE = "a" * 40
TEST_SOURCE = "b" * 40


def canonical(value: dict) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


class ProfileImageTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.image = self.root / "image.img"
        self.image.write_bytes(b"header" + b"\0" * 9000 + b"tail")
        self.gzip = self.root / "image.gz"
        with gzip.open(self.gzip, "wb", compresslevel=9) as out:
            out.write(self.image.read_bytes())
        self.pieces = self.root / "pieces"
        self.pieces.mkdir()
        data = self.gzip.read_bytes()
        self.piece_bytes = max(1, len(data) // 2)
        for index, start in enumerate(range(0, len(data), self.piece_bytes)):
            (self.pieces / f"{pi.PIECE_PREFIX}{index:03d}").write_bytes(
                data[start:start + self.piece_bytes])
        self.limit = mock.patch.object(pi, "MAX_PIECE_BYTES", self.piece_bytes)
        self.limit.start()
        self.manifest = pi.create_manifest(
            self.image, self.gzip, self.pieces, REPO, RUN, SOURCE)

    def tearDown(self) -> None:
        self.limit.stop()
        self.temp.cleanup()

    def run_json(self) -> dict:
        return {"id": RUN, "status": "completed", "conclusion": "failure", "event": "push",
                "head_branch": "main", "head_sha": SOURCE, "path": pi.WORKFLOW,
                "head_repository": {"full_name": REPO}}

    def artifacts_json(self) -> dict:
        return {"total_count": 1,
                "artifacts": [{"id": 77, "name": pi.ARTIFACT_NAME, "expired": False,
                               "size_in_bytes": 1000, "workflow_run": {"id": RUN}}]}

    def revive_run_json(self) -> dict:
        value = self.run_json()
        value.update({"run_attempt": ATTEMPT,
                      "run_started_at": "2026-10-24T10:00:00Z",
                      "updated_at": "2026-10-24T10:30:00Z"})
        return value

    def revive_jobs_json(self) -> dict:
        return {"total_count": 1, "jobs": [{
            "id": 91, "run_id": RUN, "run_attempt": ATTEMPT, "name": "revive",
            "status": "completed", "conclusion": "failure",
            "started_at": "2026-10-24T10:01:00Z", "completed_at": "2026-10-24T10:29:00Z",
            "steps": [
                {"name": "build/all revive, no browser fallback", "conclusion": "success"},
                {"name": "how big it is", "conclusion": "success"},
                {"name": "boot it to its desktop under KVM", "conclusion": "failure"},
                {"name": "retain exact Revive image for bounded diagnostic reuse",
                 "conclusion": "success"},
            ],
        }]}

    def revive_artifacts_json(self) -> dict:
        return {"total_count": 1, "artifacts": [{
            "id": 88, "name": "profile-revive-runtime-image", "expired": False,
            "size_in_bytes": 1000, "digest": "sha256:" + "c" * 64,
            "created_at": "2026-10-24T10:28:00Z", "workflow_run": {"id": RUN},
        }]}

    def revive_manifest(self) -> dict:
        image = self.image.read_bytes()
        packed = self.gzip.read_bytes()
        return {
            "schema": "auros-revive-retained-v1", "profile": "revive",
            "repository": REPO, "workflow": pi.WORKFLOW, "source_sha": SOURCE,
            "image_run_id": RUN,
            "raw": {"bytes": len(image), "sha256": hashlib.sha256(image).hexdigest()},
            "compressed": {"bytes": len(packed), "sha256": hashlib.sha256(packed).hexdigest()},
            "filename": "auros-revive.img.gz",
        }

    def revive_zip(self, path: Path, manifest=None, extras=None) -> None:
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as archive:
            archive.writestr("manifest.json", canonical(manifest or self.revive_manifest()))
            archive.writestr("auros-revive.img.gz", self.gzip.read_bytes())
            for name, data in (extras or {}).items():
                archive.writestr(name, data)

    def release_json(self, raw: bytes) -> dict:
        assets = [{"id": 100, "name": pi.MANIFEST_NAME, "size": len(raw),
                   "digest": "sha256:" + hashlib.sha256(raw).hexdigest()}]
        assets += [{"id": 101 + index, "name": piece["name"], "size": piece["bytes"],
                    "digest": "sha256:" + piece["sha256"]}
                   for index, piece in enumerate(self.manifest["pieces"])]
        return {"tag_name": f"profiles-{RUN}", "prerelease": True, "assets": assets}

    def test_round_trip_restores_exact_sparse_image(self) -> None:
        output = self.root / "out" / "auros-multilingual.img"
        pi.restore(self.manifest, self.pieces, output)
        self.assertEqual(output.read_bytes(), self.image.read_bytes())
        self.assertEqual(pi.validate_manifest(self.manifest, REPO, RUN, SOURCE), self.manifest)

    def test_manifest_rejects_unknown_field_and_path_piece(self) -> None:
        for mutate in (
            lambda value: value.update({"claim": "trusted"}),
            lambda value: value["pieces"][0].update(name="../image.gz.000"),
        ):
            value = copy.deepcopy(self.manifest)
            mutate(value)
            with self.assertRaises(pi.RetentionError):
                pi.validate_manifest(value)

    def test_manifest_rejects_gap_duplicate_and_oversize(self) -> None:
        values = []
        gap = copy.deepcopy(self.manifest)
        gap["pieces"][-1]["name"] = pi.PIECE_PREFIX + "009"
        values.append(gap)
        duplicate = copy.deepcopy(self.manifest)
        duplicate["pieces"][-1]["name"] = pi.PIECE_PREFIX + "000"
        values.append(duplicate)
        oversize = copy.deepcopy(self.manifest)
        oversize["pieces"][0]["bytes"] = self.piece_bytes + 1
        values.append(oversize)
        for value in values:
            with self.assertRaises(pi.RetentionError):
                pi.validate_manifest(value)

    def test_manifest_rejects_foreign_identity(self) -> None:
        for kwargs in ({"repository": "elsewhere/repo"}, {"run_id": RUN + 1},
                       {"source_sha": TEST_SOURCE}):
            with self.assertRaises(pi.RetentionError):
                pi.validate_manifest(self.manifest, **kwargs)

    def test_source_accepts_failed_harness_run_with_one_live_artifact(self) -> None:
        self.assertEqual(pi.validate_source(self.run_json(), self.artifacts_json(), REPO, RUN),
                         (SOURCE, 77))

    def test_source_rejects_foreign_branch_repo_workflow_event_and_status(self) -> None:
        mutations = [
            ("head_branch", "topic"), ("path", ".github/workflows/build.yml"),
            ("event", "workflow_dispatch"), ("status", "in_progress"),
        ]
        for key, value in mutations:
            run = self.run_json()
            run[key] = value
            with self.assertRaises(pi.RetentionError):
                pi.validate_source(run, self.artifacts_json(), REPO, RUN)

    def test_revive_source_binds_latest_attempt_successful_image_and_artifact(self) -> None:
        value = pi.validate_revive_source(self.revive_run_json(), self.revive_jobs_json(),
                                          self.revive_artifacts_json(), REPO, RUN)
        self.assertEqual(value, {"source_sha": SOURCE, "run_attempt": ATTEMPT,
                                 "artifact_id": 88, "artifact_bytes": 1000,
                                 "artifact_sha256": "c" * 64})

    def test_revive_source_rejects_attempt_step_and_artifact_ambiguity(self) -> None:
        cases = []
        jobs = self.revive_jobs_json(); jobs["jobs"][0]["run_attempt"] = ATTEMPT - 1; cases.append((jobs, self.revive_artifacts_json()))
        jobs = self.revive_jobs_json(); jobs["jobs"][0]["steps"][1]["conclusion"] = "failure"; cases.append((jobs, self.revive_artifacts_json()))
        jobs = self.revive_jobs_json(); jobs["jobs"][0]["steps"][-1]["conclusion"] = "skipped"; cases.append((jobs, self.revive_artifacts_json()))
        artifacts = self.revive_artifacts_json(); artifacts["artifacts"][0]["created_at"] = "2026-10-24T09:59:59Z"; cases.append((self.revive_jobs_json(), artifacts))
        artifacts = self.revive_artifacts_json(); artifacts["artifacts"][0]["created_at"] = "2026-10-24T10:29:01Z"; cases.append((self.revive_jobs_json(), artifacts))
        artifacts = self.revive_artifacts_json(); artifacts["artifacts"].append(copy.deepcopy(artifacts["artifacts"][0])); artifacts["total_count"] = 2; cases.append((self.revive_jobs_json(), artifacts))
        for jobs, artifacts in cases:
            with self.assertRaises(pi.RetentionError):
                pi.validate_revive_source(self.revive_run_json(), jobs, artifacts, REPO, RUN)

    def test_revive_source_rejects_untrusted_run_and_artifact_metadata(self) -> None:
        with self.assertRaises(pi.RetentionError):
            pi.validate_revive_source(self.revive_run_json(), self.revive_jobs_json(),
                                      self.revive_artifacts_json(), [], RUN)
        for key, replacement in (("event", "workflow_dispatch"), ("head_branch", "topic"),
                                 ("path", ".github/workflows/build.yml"),
                                 ("status", "in_progress"), ("conclusion", "cancelled")):
            run = self.revive_run_json(); run[key] = replacement
            with self.assertRaises(pi.RetentionError):
                pi.validate_revive_source(run, self.revive_jobs_json(),
                                          self.revive_artifacts_json(), REPO, RUN)
        for key, replacement in (("expired", True), ("digest", "sha256:" + "x" * 64),
                                 ("name", "profile-revive"), ("size_in_bytes", 0)):
            artifacts = self.revive_artifacts_json(); artifacts["artifacts"][0][key] = replacement
            with self.assertRaises(pi.RetentionError):
                pi.validate_revive_source(self.revive_run_json(), self.revive_jobs_json(),
                                          artifacts, REPO, RUN)

    def test_revive_zip_restores_exact_sparse_image_and_records_artifact(self) -> None:
        archive = self.root / "revive.zip"; self.revive_zip(archive)
        output = self.root / "out" / "auros-revive.img"
        manifest_output = self.root / "anchor" / "manifest.json"
        result = pi.restore_revive_zip(archive, output, manifest_output, REPO, RUN, SOURCE,
                                       archive.stat().st_size,
                                       hashlib.sha256(archive.read_bytes()).hexdigest())
        self.assertEqual(output.read_bytes(), self.image.read_bytes())
        self.assertEqual(json.loads(manifest_output.read_text()), self.revive_manifest())
        diagnostic = pi.revive_diagnostic(result, RUN + 1, TEST_SOURCE, "fail", ATTEMPT,
                                          88, hashlib.sha256(archive.read_bytes()).hexdigest(),
                                          REPO, RUN, SOURCE)
        self.assertEqual(diagnostic["profile"], "revive")
        self.assertEqual(diagnostic["source_run_attempt"], ATTEMPT)
        self.assertEqual(diagnostic["source_artifact_id"], 88)
        self.assertFalse(diagnostic["delivery_authorized"])

    def test_revive_zip_rejects_paths_extras_links_and_tampering(self) -> None:
        cases = []
        extra = self.root / "extra.zip"; self.revive_zip(extra, extras={"extra": b"x"}); cases.append(extra)
        path = self.root / "path.zip"; self.revive_zip(path, extras={"../escape": b"x"}); cases.append(path)
        link = self.root / "link.zip"
        with zipfile.ZipFile(link, "w") as archive:
            archive.writestr("manifest.json", canonical(self.revive_manifest()))
            info = zipfile.ZipInfo("auros-revive.img.gz"); info.create_system = 3; info.external_attr = 0o120777 << 16
            archive.writestr(info, self.gzip.read_bytes())
        cases.append(link)
        wrong = copy.deepcopy(self.revive_manifest()); wrong["raw"]["sha256"] = "0" * 64
        tampered = self.root / "tampered.zip"; self.revive_zip(tampered, wrong); cases.append(tampered)
        corrupt = self.root / "corrupt.zip"
        with zipfile.ZipFile(corrupt, "w", compression=zipfile.ZIP_STORED) as archive:
            archive.writestr("manifest.json", canonical(self.revive_manifest()))
            packed = bytearray(self.gzip.read_bytes()); packed[len(packed) // 2] ^= 1
            archive.writestr("auros-revive.img.gz", packed)
        cases.append(corrupt)
        duplicate = self.root / "duplicate-json.zip"
        manifest_raw = canonical(self.revive_manifest()).replace(
            b'{\n  "compressed"', b'{\n  "schema": "duplicate",\n  "compressed"', 1)
        with zipfile.ZipFile(duplicate, "w") as archive:
            archive.writestr("manifest.json", manifest_raw)
            archive.writestr("auros-revive.img.gz", self.gzip.read_bytes())
        cases.append(duplicate)
        for archive in cases:
            output = self.root / f"{archive.stem}.img"
            with self.assertRaises(pi.RetentionError):
                pi.restore_revive_zip(archive, output, self.root / "manifest-out.json",
                                      REPO, RUN, SOURCE, archive.stat().st_size,
                                      hashlib.sha256(archive.read_bytes()).hexdigest())
            self.assertFalse(output.exists())

    def test_revive_zip_rejects_archive_identity_and_expansion_bounds(self) -> None:
        archive = self.root / "revive.zip"; self.revive_zip(archive)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        for size, sha in ((archive.stat().st_size + 1, digest), (archive.stat().st_size, "0" * 64)):
            with self.assertRaises(pi.RetentionError):
                pi.restore_revive_zip(archive, self.root / "bad.img", self.root / "bad.json",
                                      REPO, RUN, SOURCE, size, sha)
        manifest = self.revive_manifest()
        manifest["raw"]["bytes"] = manifest["compressed"]["bytes"] * (pi.MAX_REVIVE_RATIO + 1)
        ratio = self.root / "ratio.zip"; self.revive_zip(ratio, manifest)
        with self.assertRaises(pi.RetentionError):
            pi.restore_revive_zip(ratio, self.root / "ratio.img", self.root / "ratio.json",
                                  REPO, RUN, SOURCE, ratio.stat().st_size,
                                  hashlib.sha256(ratio.read_bytes()).hexdigest())
        run = self.run_json()
        run["head_repository"]["full_name"] = "foreign/repo"
        with self.assertRaises(pi.RetentionError):
            pi.validate_source(run, self.artifacts_json(), REPO, RUN)

    def test_source_rejects_missing_expired_duplicate_and_large_artifact(self) -> None:
        cases = [{"total_count": 0, "artifacts": []}]
        expired = self.artifacts_json()
        expired["artifacts"][0]["expired"] = True
        cases.append(expired)
        duplicate = self.artifacts_json()
        duplicate["artifacts"].append(copy.deepcopy(duplicate["artifacts"][0]))
        duplicate["artifacts"][1]["id"] = 78
        duplicate["total_count"] = 2
        cases.append(duplicate)
        large = self.artifacts_json()
        large["artifacts"][0]["size_in_bytes"] = pi.MAX_ARTIFACT_BYTES + 1
        cases.append(large)
        for artifacts in cases:
            with self.assertRaises(pi.RetentionError):
                pi.validate_source(self.run_json(), artifacts, REPO, RUN)

    def test_release_requires_prerelease_tag_and_exact_digests(self) -> None:
        raw = (json.dumps(self.manifest, indent=2, sort_keys=True) + "\n").encode()
        release = self.release_json(raw)
        pi.validate_release(self.manifest, raw, release, REPO, RUN)
        for mutation in ("tag", "release", "digest", "size", "missing", "duplicate", "extra"):
            bad = copy.deepcopy(release)
            if mutation == "tag": bad["tag_name"] = "profiles-1"
            if mutation == "release": bad["prerelease"] = False
            if mutation == "digest": bad["assets"][1]["digest"] = "sha256:" + "0" * 64
            if mutation == "size": bad["assets"][1]["size"] += 1
            if mutation == "missing": bad["assets"].pop()
            if mutation == "duplicate": bad["assets"].append(copy.deepcopy(bad["assets"][0]))
            if mutation == "extra":
                bad["assets"].append({"id": 999, "name": pi.PIECE_PREFIX + "999", "size": 1,
                                      "digest": "sha256:" + "0" * 64})
            with self.assertRaises(pi.RetentionError):
                pi.validate_release(self.manifest, raw, bad, REPO, RUN)

    def test_plan_rejects_release_manifest_substitution(self) -> None:
        raw = (json.dumps(self.manifest, indent=2, sort_keys=True) + "\n").encode()
        release = self.release_json(raw)
        _, plan = pi.validate_plan(self.manifest, raw, release, REPO, RUN, SOURCE)
        self.assertEqual([name for _, name, _ in plan],
                         [pi.MANIFEST_NAME] + [p["name"] for p in self.manifest["pieces"]])
        changed = copy.deepcopy(release)
        changed["assets"][0]["digest"] = "sha256:" + "0" * 64
        with self.assertRaises(pi.RetentionError):
            pi.validate_plan(self.manifest, raw, changed, REPO, RUN, SOURCE)
        pi.validate_downloaded_manifest(raw, raw)
        with self.assertRaises(pi.RetentionError):
            pi.validate_downloaded_manifest(raw, raw + b" ")

    def test_restore_rejects_missing_extra_tampered_and_truncated_pieces(self) -> None:
        cases = []
        missing = self.root / "missing"; missing.mkdir()
        for path in self.pieces.iterdir():
            if path.name.endswith("000"): path.replace(missing / path.name)
            else: (missing / path.name).write_bytes(path.read_bytes())
        # Put the moved source piece back for later cases.
        (self.pieces / (pi.PIECE_PREFIX + "000")).write_bytes(
            (missing / (pi.PIECE_PREFIX + "000")).read_bytes())
        (missing / (pi.PIECE_PREFIX + "000")).unlink()
        cases.append(missing)
        extra = self.root / "extra"; extra.mkdir()
        for path in self.pieces.iterdir(): (extra / path.name).write_bytes(path.read_bytes())
        (extra / (pi.PIECE_PREFIX + "999")).write_bytes(b"x")
        cases.append(extra)
        tampered = self.root / "tampered"; tampered.mkdir()
        for path in self.pieces.iterdir(): (tampered / path.name).write_bytes(path.read_bytes())
        first = tampered / (pi.PIECE_PREFIX + "000")
        first.write_bytes(b"x" + first.read_bytes()[1:])
        cases.append(tampered)
        for directory in cases:
            with self.assertRaises(pi.RetentionError):
                pi.restore(self.manifest, directory, self.root / (directory.name + ".img"))

    def test_restore_rejects_manifest_with_wrong_raw_claim(self) -> None:
        wrong = copy.deepcopy(self.manifest)
        wrong["image"]["sha256"] = "0" * 64
        with self.assertRaises(pi.RetentionError):
            pi.restore(wrong, self.pieces, self.root / "wrong.img")

    def test_diagnostic_is_always_explicit_and_keeps_both_sources(self) -> None:
        value = pi.diagnostic(self.manifest, RUN + 8, TEST_SOURCE, "fail")
        self.assertEqual(value["state"], "diagnostic-retained-image")
        self.assertEqual(value["image_source_sha"], SOURCE)
        self.assertEqual(value["test_source_sha"], TEST_SOURCE)
        self.assertEqual(value["result"], "fail")

    def test_runtime_record_normalizes_multilingual_delivery_identity(self) -> None:
        value = pi.runtime_record(canonical(self.manifest),
                                  "multilingual", REPO, RUN, ATTEMPT, SOURCE)
        self.assertEqual(value["schema"], "auros-profile-runtime-image-v1")
        self.assertEqual(value["state"], "runtime-certified-evidence-only")
        self.assertEqual(value["purpose"], "profile-runtime-evidence")
        self.assertEqual(value["runtime_proof"], {
            "job": "multilingual", "mode": "multilingual-functional-v1",
            "run_id": RUN, "run_attempt": ATTEMPT, "source_sha": SOURCE,
        })
        self.assertEqual(value["storage"]["type"], "profile-prerelease-pieces")
        self.assertEqual(value["storage"]["tag"], f"profiles-{RUN}")
        self.assertEqual(value["storage"]["files"], self.manifest["pieces"])
        self.assertEqual(value["availability"], {
            "metadata_retention": "actions-artifact-30-days",
            "backing_retention": "profile-prerelease-pruned",
            "access": "requires-live-backing-validation", "delivery_authorized": False,
        })
        self.assertEqual(pi.validate_runtime_record(value, REPO, RUN, SOURCE,
                                                    "multilingual", ATTEMPT), value)
        with self.assertRaises(pi.RetentionError):
            pi.validate_runtime_record(value, REPO, RUN, SOURCE, "multilingual", ATTEMPT + 1)
        for kwargs in ({"run_id": True}, {"run_attempt": True}, {"profile": []},
                       {"repository": []}, {"source_sha": []}):
            with self.assertRaises(pi.RetentionError):
                pi.validate_runtime_record(value, **kwargs)

    def test_runtime_record_normalizes_revive_artifact_without_copying_image(self) -> None:
        raw_image = self.image.read_bytes()
        packed = self.gzip.read_bytes()
        source = {
            "schema": "auros-revive-retained-v1", "profile": "revive",
            "repository": REPO, "workflow": pi.WORKFLOW,
            "source_sha": SOURCE, "image_run_id": RUN,
            "raw": {"bytes": len(raw_image), "sha256": hashlib.sha256(raw_image).hexdigest()},
            "compressed": {"bytes": len(packed), "sha256": hashlib.sha256(packed).hexdigest()},
            "filename": "auros-revive.img.gz",
        }
        value = pi.runtime_record(canonical(source), "revive", REPO, RUN, ATTEMPT, SOURCE)
        self.assertEqual(value["runtime_proof"]["mode"], "revive-functional-v1")
        self.assertEqual(value["storage"], {
            "type": "actions-artifact", "artifact_name": "profile-revive-runtime-image",
            "manifest_name": "manifest.json",
            "files": [{"name": "auros-revive.img.gz", "bytes": len(packed),
                       "sha256": hashlib.sha256(packed).hexdigest()}],
        })

    def test_runtime_record_rejects_identity_hash_size_and_path_tampering(self) -> None:
        value = pi.runtime_record(canonical(self.manifest),
                                  "multilingual", REPO, RUN, ATTEMPT, SOURCE)
        mutations = []
        for path, replacement in (
            (("repository",), "foreign/repo"),
            (("image_run_id",), RUN + 1),
            (("image_source_sha",), TEST_SOURCE),
            (("runtime_proof", "job"), "revive"),
            (("image", "sha256"), "0" * 63),
            (("storage", "files", 0, "name"), "../image.gz.000"),
            (("storage", "files", 0, "bytes"), 0),
        ):
            changed = copy.deepcopy(value)
            target = changed
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = replacement
            mutations.append(changed)
        extra = copy.deepcopy(value)
        extra["public_ready"] = True
        mutations.append(extra)
        for changed in mutations:
            with self.assertRaises(pi.RetentionError):
                pi.validate_runtime_record(changed, REPO, RUN, SOURCE, "multilingual")

    def test_runtime_record_rejects_json_type_confusion(self) -> None:
        value = pi.runtime_record(canonical(self.manifest),
                                  "multilingual", REPO, RUN, ATTEMPT, SOURCE)
        for path, replacement in (
            (("profile",), []),
            (("image_run_id",), True),
            (("runtime_proof", "run_id"), float(RUN)),
            (("runtime_proof", "run_attempt"), True),
            (("runtime_proof", "source_sha"), {}),
            (("runtime_proof", "job"), []),
            (("availability", "delivery_authorized"), 0),
        ):
            changed = copy.deepcopy(value)
            target = changed
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = replacement
            with self.assertRaises(pi.RetentionError):
                pi.validate_runtime_record(changed, REPO, RUN, SOURCE, "multilingual")

        revive = {
            "schema": "auros-revive-retained-v1", "profile": "revive",
            "repository": REPO, "workflow": pi.WORKFLOW,
            "source_sha": SOURCE, "image_run_id": True,
            "raw": {"bytes": 1, "sha256": "0" * 64},
            "compressed": {"bytes": 1, "sha256": "1" * 64},
            "filename": "auros-revive.img.gz",
        }
        with self.assertRaises(pi.RetentionError):
            pi.runtime_record(canonical(revive), "revive", REPO, 1, ATTEMPT, SOURCE)

    def test_runtime_record_digest_is_bound_to_the_parsed_source_bytes(self) -> None:
        raw = canonical(self.manifest)
        value = pi.runtime_record(raw, "multilingual", REPO, RUN, ATTEMPT, SOURCE)
        self.assertEqual(value["source_manifest"]["sha256"], hashlib.sha256(raw).hexdigest())
        foreign = copy.deepcopy(self.manifest)
        foreign["repository"] = "foreign/repository"
        with self.assertRaises(pi.RetentionError):
            pi.runtime_record(canonical(foreign), "multilingual", REPO, RUN, ATTEMPT, SOURCE)

    def test_runtime_source_parser_rejects_duplicate_fields(self) -> None:
        path = self.root / "duplicate.json"
        path.write_text('{"schema":"first","schema":"second"}\n')
        with self.assertRaises(pi.RetentionError):
            pi._load_json(path, "retained image manifest")

    def test_workflow_records_only_successful_functional_profile_metadata(self) -> None:
        workflow = (Path(__file__).parents[1] / ".github/workflows/profiles.yml").read_text()
        self.assertIn("python3 tools/profile_image.py certify", workflow)
        self.assertIn("profile-${{ matrix.profile }}-runtime-image-metadata", workflow)
        self.assertIn("retention-days: 30", workflow)
        self.assertIn("--run-attempt \"$GITHUB_RUN_ATTEMPT\"", workflow)
        self.assertIn("!ev/*-runtime-image-v1.json", workflow)
        self.assertIn("multilingual-image-v1.json", workflow)
        self.assertIn("retained-revive/manifest.json", workflow)

    def test_workflow_dispatches_exactly_one_retained_profile_without_rebuilding(self) -> None:
        workflow = (Path(__file__).parents[1] / ".github/workflows/profiles.yml").read_text()
        self.assertIn("retained_revive_run_id:", workflow)
        self.assertIn("validate_revive_source", Path(pi.__file__).read_text())
        revive = workflow.split("\n  revive-retest:\n", 1)[1].split("\n  summary:\n", 1)[0]
        self.assertIn("github.event_name == 'workflow_dispatch'", revive)
        self.assertIn("inputs.retained_revive_run_id != ''", revive)
        self.assertIn("inputs.retained_multilingual_run_id == ''", revive)
        self.assertIn("actions/runs/$SOURCE_RUN/jobs?filter=latest&per_page=100", revive)
        self.assertIn("PROFILETEST_REVIVE_FUNCTIONAL=1", revive)
        self.assertIn("profile-revive-diagnostic", revive)
        self.assertNotIn("build/all", revive)
        self.assertNotIn("debootstrap", revive)
        self.assertNotIn("runtime-image-metadata", revive)
        self.assertNotIn("delivery_authorized=true", revive)


if __name__ == "__main__":
    unittest.main()
