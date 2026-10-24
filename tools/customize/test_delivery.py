from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.customize.delivery import (  # noqa: E402
    DeliveryError,
    make_manifest,
    prepare,
    validate_request_id,
    validate_source_ref,
    verify_ready,
)


REPOSITORY = "aarohkandy/v0.0.1-auros"
SOURCE = "b" * 40
RUN_ID = 50000000001
BASE_RUN_ID = 40000000001
TAG = "v0.1.0-bbbbbbb"
RECIPE_HEX = "c" * 64
REQUEST_ID = "123e4567-e89b-42d3-a456-426614174000"


class Fixture:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.signed = root / "AurOS-Setup.exe"
        self.signed.write_bytes(b"MZ signed personalized installer")
        self.signed_sha = hashlib.sha256(self.signed.read_bytes()).hexdigest()
        self.image_sha = "2" * 64
        self.image_bytes = 6442450944
        self.unsigned_sha = "3" * 64
        self.base_installer_sha = "4" * 64
        self.piece_one = "5" * 64
        self.piece_two = "6" * 64
        self.pieces = root / "pieces.txt"
        self.pieces.write_text(
            f"# AurOS desktop {TAG}, commit {SOURCE[:12]}, Actions run {BASE_RUN_ID}\n"
            f"base https://github.com/{REPOSITORY}/releases/download/{TAG}/\n"
            f"piece auros-desktop.img.gz.000 100 {self.piece_one}\n"
            f"piece auros-desktop.img.gz.001 50 {self.piece_two}\n",
            encoding="utf-8",
        )
        self.image_sha_path = root / "image.sha256"
        self.image_sha_path.write_text(self.image_sha + "\n", encoding="ascii")
        self.image_bytes_path = root / "image.bytes"
        self.image_bytes_path.write_text(str(self.image_bytes) + "\n", encoding="ascii")
        provenance = {
            "schema": "org.auros.custom-package-provenance",
            "version": 1,
            "state": "unsigned-custom-artifact",
            "signed": False,
            "public_release": False,
            "download_ready": False,
            "repository": REPOSITORY,
            "base_build": {
                "run_id": BASE_RUN_ID,
                "head_sha": SOURCE,
                "tag": TAG,
                "profile": "desktop",
                "installer_sha256": self.base_installer_sha,
                "image_sha256": self.image_sha,
                "image_bytes": self.image_bytes,
                "pieces_sha256": hashlib.sha256(self.pieces.read_bytes()).hexdigest(),
                "pieces_base": f"https://github.com/{REPOSITORY}/releases/download/{TAG}/",
            },
            "recipe": {
                "id": f"sha256:{RECIPE_HEX}",
                "base": "desktop",
                "resolved_preferences": {"look": "dock", "theme": "moss"},
            },
            "custom_installer": {
                "filename": "AurOS-Setup.exe",
                "sha256": self.unsigned_sha,
                "bytes": 1234,
            },
        }
        self.provenance = root / "custom-package-provenance.json"
        self.provenance.write_text(
            json.dumps(provenance, sort_keys=True, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        self.signing = root / "signing-provenance.txt"
        values = {
            "source_commit": SOURCE,
            "build_run": str(RUN_ID),
            "unsigned_wizard_sha256": "7" * 64,
            "signed_wizard_sha256": "8" * 64,
            "build_unsigned_outer_sha256": self.unsigned_sha,
            "rebuilt_unsigned_outer_sha256": "9" * 64,
            "signed_outer_sha256": self.signed_sha,
            "build_info_sha256": "a" * 64,
            "custom_package_provenance_sha256": hashlib.sha256(
                self.provenance.read_bytes()
            ).hexdigest(),
        }
        markers = [
            "verified custom_package_provenance_describes_build_unsigned_outer",
            "verified signed_wizard_is_authenticode_form_of_unsigned_wizard",
            "verified rebuilt_unsigned_outer_embeds_signed_wizard_byte_for_byte",
            "verified signed_outer_is_authenticode_form_of_rebuilt_unsigned_outer",
            "verified signed_outer_embeds_signed_wizard_byte_for_byte",
        ]
        self.signing.write_text(
            "".join(f"{key} {value}\n" for key, value in values.items())
            + "".join(marker + "\n" for marker in markers),
            encoding="ascii",
        )
        self.release_value = {
            "id": 9001,
            "tag_name": TAG,
            "draft": False,
            "prerelease": True,
            "assets": [],
        }
        self.add_asset(
            "pieces.txt",
            self.pieces.stat().st_size,
            hashlib.sha256(self.pieces.read_bytes()).hexdigest(),
        )
        self.add_asset(
            "image.sha256",
            self.image_sha_path.stat().st_size,
            hashlib.sha256(self.image_sha_path.read_bytes()).hexdigest(),
        )
        self.add_asset(
            "image.bytes",
            self.image_bytes_path.stat().st_size,
            hashlib.sha256(self.image_bytes_path.read_bytes()).hexdigest(),
        )
        self.add_asset("AurOS-Setup.exe", 1000, self.base_installer_sha)
        self.add_asset("auros-desktop.img.gz.000", 100, self.piece_one)
        self.add_asset("auros-desktop.img.gz.001", 50, self.piece_two)
        self.release = root / "release.json"
        self.write_release()

    def add_asset(self, name: str, size: int, digest: str) -> None:
        self.release_value["assets"].append(
            {
                "id": 100 + len(self.release_value["assets"]),
                "name": name,
                "size": size,
                "digest": f"sha256:{digest}",
                "browser_download_url": (
                    f"https://github.com/{REPOSITORY}/releases/download/{TAG}/{name}"
                ),
            }
        )

    def write_release(self) -> None:
        self.release.write_text(json.dumps(self.release_value), encoding="utf-8")

    def prepare(self) -> tuple[dict[str, object], Path]:
        output = self.root / "publish"
        plan = prepare(
            provenance_path=self.provenance,
            signing_path=self.signing,
            installer=self.signed,
            release_path=self.release,
            pieces_path=self.pieces,
            image_sha_path=self.image_sha_path,
            image_bytes_path=self.image_bytes_path,
            repository=REPOSITORY,
            source_sha=SOURCE,
            release_source_sha=SOURCE,
            workflow_run_id=RUN_ID,
            signed_sha256=self.signed_sha,
            request_id=REQUEST_ID,
            output_dir=output,
        )
        return plan, output


class DeliveryTests(unittest.TestCase):
    def test_request_id_is_optional_or_canonical_uuid(self) -> None:
        self.assertIsNone(validate_request_id(""))
        self.assertEqual(validate_request_id(REQUEST_ID), REQUEST_ID)
        for value in ("request-1", "../request", REQUEST_ID.upper(), "$(id)"):
            with self.subTest(value=value), self.assertRaises(DeliveryError):
                validate_request_id(value)

    def test_source_ref_is_main_or_exact_verified_release_tag(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            fixture = Fixture(Path(name))
            validate_source_ref("refs/heads/main", SOURCE, fixture.provenance)
            validate_source_ref(f"refs/tags/{TAG}", SOURCE, fixture.provenance)
            for ref, source in (
                ("refs/tags/v0.1.0-aaaaaaa", SOURCE),
                ("refs/heads/feature", SOURCE),
                (f"refs/tags/{TAG}", "d" * 40),
            ):
                with self.subTest(ref=ref, source=source), self.assertRaises(
                    DeliveryError
                ):
                    validate_source_ref(ref, source, fixture.provenance)

    def test_prepares_immutable_recipe_names_from_complete_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            fixture = Fixture(Path(name))
            plan, output = fixture.prepare()
            installer_name = f"AurOS-Custom-{RECIPE_HEX}.exe"
            self.assertEqual(plan["installer"]["name"], installer_name)
            self.assertEqual(plan["request_id"], REQUEST_ID)
            self.assertEqual((output / installer_name).read_bytes(), fixture.signed.read_bytes())
            self.assertEqual(plan["state"], "verified-signed-awaiting-publication")

    def test_rejects_mismatched_source_run_and_signed_hash(self) -> None:
        cases = {
            "source": {"source_sha": "d" * 40},
            "release source": {"release_source_sha": "d" * 40},
            "run": {"workflow_run_id": RUN_ID + 1},
            "signed output": {"signed_sha256": "d" * 64},
        }
        for label, overrides in cases.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as name:
                fixture = Fixture(Path(name))
                arguments = {
                    "provenance_path": fixture.provenance,
                    "signing_path": fixture.signing,
                    "installer": fixture.signed,
                    "release_path": fixture.release,
                    "pieces_path": fixture.pieces,
                    "image_sha_path": fixture.image_sha_path,
                    "image_bytes_path": fixture.image_bytes_path,
                    "repository": REPOSITORY,
                    "source_sha": SOURCE,
                    "release_source_sha": SOURCE,
                    "workflow_run_id": RUN_ID,
                    "signed_sha256": fixture.signed_sha,
                    "request_id": REQUEST_ID,
                    "output_dir": Path(name) / "publish",
                }
                arguments.update(overrides)
                with self.assertRaises(DeliveryError):
                    prepare(**arguments)

    def test_rejects_unsigned_or_incomplete_signing_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            fixture = Fixture(Path(name))
            text = fixture.signing.read_text(encoding="ascii")
            fixture.signing.write_text(
                text.replace(
                    "verified custom_package_provenance_describes_build_unsigned_outer\n",
                    "",
                ),
                encoding="ascii",
            )
            with self.assertRaises(DeliveryError):
                fixture.prepare()

    def test_rejects_recipe_or_base_provenance_changed_after_signing(self) -> None:
        for section, field, value in (
            ("recipe", "id", "sha256:" + "d" * 64),
            ("base_build", "run_id", BASE_RUN_ID + 1),
            ("base_build", "image_sha256", "d" * 64),
        ):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as name:
                fixture = Fixture(Path(name))
                document = json.loads(fixture.provenance.read_text(encoding="utf-8"))
                document[section][field] = value
                fixture.provenance.write_text(
                    json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n",
                    encoding="utf-8",
                )
                with self.assertRaises(DeliveryError):
                    fixture.prepare()

    def test_rejects_release_url_substitution_and_existing_names(self) -> None:
        for mutation in ("url", "exe conflict", "manifest conflict"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as name:
                fixture = Fixture(Path(name))
                if mutation == "url":
                    fixture.release_value["assets"][0][
                        "browser_download_url"
                    ] = "https://attacker.invalid/pieces.txt"
                elif mutation == "exe conflict":
                    fixture.add_asset(f"AurOS-Custom-{RECIPE_HEX}.exe", 1, "d" * 64)
                else:
                    fixture.add_asset(f"AurOS-Custom-{RECIPE_HEX}.json", 1, "d" * 64)
                fixture.write_release()
                with self.assertRaises(DeliveryError):
                    fixture.prepare()

    def test_rejects_wrong_base_image_asset_hash_or_size(self) -> None:
        for field, value in (("digest", "sha256:" + "d" * 64), ("size", 999)):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as name:
                fixture = Fixture(Path(name))
                fixture.release_value["assets"][0][field] = value
                fixture.write_release()
                with self.assertRaises(DeliveryError):
                    fixture.prepare()

    def test_manifest_rejects_substituted_signed_download_url(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            fixture = Fixture(Path(name))
            plan, output = fixture.prepare()
            installer_name = plan["installer"]["name"]
            fixture.add_asset(installer_name, fixture.signed.stat().st_size, fixture.signed_sha)
            fixture.release_value["assets"][-1][
                "browser_download_url"
            ] = "https://attacker.invalid/AurOS.exe"
            fixture.write_release()
            with self.assertRaises(DeliveryError):
                make_manifest(
                    output / "delivery-plan.json",
                    fixture.release,
                    SOURCE,
                    fixture.signed,
                    output / plan["manifest_name"],
                )

    def test_manifest_becomes_ready_only_after_exe_and_json_readback(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            fixture = Fixture(Path(name))
            plan, output = fixture.prepare()
            installer_name = plan["installer"]["name"]
            manifest_name = plan["manifest_name"]
            fixture.add_asset(installer_name, fixture.signed.stat().st_size, fixture.signed_sha)
            fixture.write_release()
            readback_installer = Path(name) / "readback.exe"
            readback_installer.write_bytes(fixture.signed.read_bytes())
            manifest_path = output / manifest_name
            manifest = make_manifest(
                output / "delivery-plan.json",
                fixture.release,
                SOURCE,
                readback_installer,
                manifest_path,
            )
            self.assertEqual(manifest["state"], "download-ready")
            self.assertEqual(manifest["source"]["workflow_run_id"], RUN_ID)
            fixture.add_asset(
                manifest_name,
                manifest_path.stat().st_size,
                hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
            )
            fixture.write_release()
            readback_manifest = Path(name) / "readback.json"
            readback_manifest.write_bytes(manifest_path.read_bytes())
            url = verify_ready(
                output / "delivery-plan.json",
                fixture.release,
                SOURCE,
                manifest_path,
                readback_manifest,
                output / installer_name,
                readback_installer,
            )
            self.assertEqual(url, manifest["installer"]["url"])

    def test_exact_partial_publication_resumes_without_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as name:
            fixture = Fixture(Path(name))
            first, output = fixture.prepare()
            installer_name = first["installer"]["name"]
            manifest_name = first["manifest_name"]
            fixture.add_asset(installer_name, fixture.signed.stat().st_size, fixture.signed_sha)
            fixture.write_release()
            second, second_output = fixture.prepare()
            self.assertTrue(second["existing_installer"])
            self.assertFalse(second["existing_manifest"])
            manifest_path = output / manifest_name
            make_manifest(
                second_output / "delivery-plan.json",
                fixture.release,
                SOURCE,
                fixture.signed,
                manifest_path,
            )
            fixture.add_asset(
                manifest_name,
                manifest_path.stat().st_size,
                hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
            )
            fixture.write_release()
            third, _third_output = fixture.prepare()
            self.assertTrue(third["existing_installer"])
            self.assertTrue(third["existing_manifest"])

    def test_final_gate_rejects_changed_source_release_or_any_base_asset(self) -> None:
        mutations = ["source", "release id"] + [
            f"asset {index}" for index in range(6)
        ]
        for mutation in mutations:
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as name:
                fixture = Fixture(Path(name))
                plan, output = fixture.prepare()
                installer_name = plan["installer"]["name"]
                fixture.add_asset(
                    installer_name, fixture.signed.stat().st_size, fixture.signed_sha
                )
                fixture.write_release()
                source = SOURCE
                if mutation == "source":
                    source = "d" * 40
                elif mutation == "release id":
                    fixture.release_value["id"] += 1
                else:
                    index = int(mutation.split()[1])
                    fixture.release_value["assets"][index]["digest"] = "sha256:" + "d" * 64
                fixture.write_release()
                with self.assertRaises(DeliveryError):
                    make_manifest(
                        output / "delivery-plan.json",
                        fixture.release,
                        source,
                        fixture.signed,
                        output / plan["manifest_name"],
                    )

    def test_ready_verdict_rejects_changed_readbacks(self) -> None:
        for changed in ("installer", "manifest"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as name:
                fixture = Fixture(Path(name))
                plan, output = fixture.prepare()
                installer_name = plan["installer"]["name"]
                manifest_name = plan["manifest_name"]
                fixture.add_asset(
                    installer_name, fixture.signed.stat().st_size, fixture.signed_sha
                )
                fixture.write_release()
                readback_installer = Path(name) / "readback.exe"
                readback_installer.write_bytes(fixture.signed.read_bytes())
                manifest_path = output / manifest_name
                make_manifest(
                    output / "delivery-plan.json",
                    fixture.release,
                    SOURCE,
                    readback_installer,
                    manifest_path,
                )
                fixture.add_asset(
                    manifest_name,
                    manifest_path.stat().st_size,
                    hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
                )
                fixture.write_release()
                readback_manifest = Path(name) / "readback.json"
                readback_manifest.write_bytes(manifest_path.read_bytes())
                if changed == "installer":
                    readback_installer.write_bytes(b"changed")
                else:
                    readback_manifest.write_bytes(b"{}\n")
                with self.assertRaises(DeliveryError):
                    verify_ready(
                        output / "delivery-plan.json",
                        fixture.release,
                        SOURCE,
                        manifest_path,
                        readback_manifest,
                        output / installer_name,
                        readback_installer,
                    )


class WorkflowContractTests(unittest.TestCase):
    def test_publish_requires_successful_signing_and_never_clobbers(self) -> None:
        workflow = (ROOT / ".github/workflows/customize.yml").read_text(encoding="utf-8")
        publish = workflow.split("\n  publish:\n", 1)[1]
        self.assertIn("name: publish", publish)
        self.assertIn("needs.sign.result == 'success'", publish)
        self.assertIn("needs.certify.result == 'success'", publish)
        self.assertIn("needs.certify.outputs.certified == 'true'", publish)
        self.assertIn("needs: [package, sign, certify]", publish)
        self.assertIn("contents: write", publish)
        self.assertNotIn("--clobber", publish)
        self.assertIn("verify-ready", publish)
        self.assertIn("name: auros-custom-delivery", publish)
        self.assertIn("compression-level: 0", publish)
        self.assertLess(
            publish.index("verify-ready"), publish.index("name: auros-custom-delivery")
        )
        self.assertIn("run-name: customize ${{ inputs.request_id }}", workflow)

    def test_custom_certification_cannot_pass_on_a_skipped_journey(self) -> None:
        customize = (ROOT / ".github/workflows/customize.yml").read_text(
            encoding="utf-8"
        )
        certify = customize.split("\n  certify:\n", 1)[1].split(
            "\n  # A ready manifest", 1
        )[0]
        self.assertIn("installer_artifact: auros-setup-signed", certify)
        self.assertIn("installer_sha256: ${{ needs.sign.outputs.signed_sha256 }}", certify)
        self.assertIn("variants: base", certify)
        expected_outputs = {
            "expected_recipe_id": "recipe_id",
            "expected_look": "expected_look",
            "expected_theme": "expected_theme",
        }
        for field, output in expected_outputs.items():
            self.assertIn(f"{field}: ${{{{ needs.package.outputs.{output} }}}}", certify)

        e2e = (ROOT / ".github/workflows/e2e.yml").read_text(encoding="utf-8")
        guard = e2e.split("\n  custom_certification:\n", 1)[1].split(
            "\n  # The AurOS half", 1
        )[0]
        self.assertIn("needs: [gate, e2e]", guard)
        self.assertIn('GATE_VARIANTS" = \'["base"]\'', guard)
        self.assertIn('E2E_RESULT" = success', guard)
        self.assertIn("certified=true", guard)
        self.assertIn("--expect-recipe-id", e2e)
        self.assertIn("--expect-look", e2e)
        self.assertIn("--expect-theme", e2e)

    def test_every_release_e2e_caller_grants_reusable_permissions(self) -> None:
        release = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        callers = release.split("uses: ./.github/workflows/e2e.yml")
        self.assertEqual(len(callers), 3)
        for caller in callers[1:]:
            permissions = caller.split("with:", 1)[0]
            self.assertIn("contents: write", permissions)
            self.assertIn("actions: read", permissions)


if __name__ == "__main__":
    unittest.main()
