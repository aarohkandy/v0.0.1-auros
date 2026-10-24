#!/usr/bin/env python3
import copy
import gzip
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import profile_image as pi


REPO = "owner/repository"
RUN = 40123456789
SOURCE = "a" * 40
TEST_SOURCE = "b" * 40


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


if __name__ == "__main__":
    unittest.main()
