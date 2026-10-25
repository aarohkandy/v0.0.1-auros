import importlib.util
import json
import pathlib
import tempfile
import unittest


MODULE_PATH = pathlib.Path(__file__).with_name("build_service_config.py")
SPEC = importlib.util.spec_from_file_location("build_service_config", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class BuildServiceConfigTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.wrangler = self.root / "wrangler.toml"
        self.output = self.root / "site" / "build-service.json"

    def tearDown(self):
        self.temp.cleanup()

    def write(self, enabled="0", allowed=""):
        self.wrangler.write_text(
            '[vars]\nBUILD_REQUESTS_ENABLED = "' + enabled + '"\n'
            'BUILD_ALLOWED_ORIGIN = "' + allowed + '"\n', encoding="utf-8")

    def invoke(self, site="https://owner.github.io/project/", api=""):
        return MODULE.main(["--wrangler", str(self.wrangler), "--site-url", site,
                            "--api-origin", api, "--output", str(self.output)])

    def test_disabled_emits_only_inert_configuration_without_api(self):
        self.write()
        self.assertEqual(self.invoke(api="not even parsed"), 0)
        self.assertEqual(json.loads(self.output.read_text()), {"enabled": False, "api_origin": ""})

    def test_enabled_emits_exact_worker_origin(self):
        self.write("1", "https://owner.github.io")
        self.assertEqual(self.invoke(api="https://worker.example"), 0)
        self.assertEqual(json.loads(self.output.read_text()), {"enabled": True, "api_origin": "https://worker.example"})

    def test_enabled_refuses_site_origin_mismatch(self):
        self.write("1", "https://different.example")
        self.assertEqual(self.invoke(api="https://worker.example"), 1)
        self.assertFalse(self.output.exists())

    def test_enabled_refuses_missing_or_non_origin_worker_url(self):
        self.write("1", "https://owner.github.io")
        for value in ("", "http://worker.example", "https://worker.example/path", "https://user@worker.example", "https://worker.example:99999", "https://wörker.example"):
            with self.subTest(value=value):
                self.output.unlink(missing_ok=True)
                self.assertEqual(self.invoke(api=value), 1)
                self.assertFalse(self.output.exists())

    def test_enabled_refuses_website_as_worker(self):
        self.write("1", "https://owner.github.io")
        self.assertEqual(self.invoke(api="https://owner.github.io"), 1)

    def test_ambiguous_or_invalid_switch_fails_closed(self):
        for text in ('[vars]\nBUILD_REQUESTS_ENABLED = "yes"\n',
                     '[vars]\nBUILD_REQUESTS_ENABLED = "0"\nBUILD_REQUESTS_ENABLED = "1"\n',
                     'BUILD_REQUESTS_ENABLED = "0"\n'):
            with self.subTest(text=text):
                self.wrangler.write_text(text, encoding="utf-8")
                self.output.unlink(missing_ok=True)
                self.assertEqual(self.invoke(), 1)
                self.assertFalse(self.output.exists())

    def test_pages_only_enables_the_staged_artifact_after_preflight(self):
        root = pathlib.Path(__file__).resolve().parents[2]
        workflow = (root / ".github/workflows/pages.yml").read_text(encoding="utf-8")
        source = json.loads((root / "website/build-service.json").read_text(encoding="utf-8"))
        self.assertEqual(source, {"enabled": False, "api_origin": ""})
        self.assertIn('--output "$RUNNER_TEMP/site/build-service.json"', workflow)
        self.assertNotIn('--output "website/build-service.json"', workflow)
        self.assertEqual(workflow.count('node cloud/test/build-preflight.mjs "$AUROS_WISHES_URL" on "$origin"'), 2)
        self.assertLess(workflow.index("Configure the optional custom-build service in the staged site"),
                        workflow.index("actions/upload-pages-artifact@"))


if __name__ == "__main__":
    unittest.main()
