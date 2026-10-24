#!/usr/bin/env python3
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fcitx_password_server import PAGE, bounded_focus, bounded_result, write_new


class PasswordResultTest(unittest.TestCase):
    def test_page_requires_a_real_click_and_masks_the_synthetic_value(self):
        self.assertIn(b'type="password"', PAGE)
        self.assertNotIn(b"autofocus", PAGE)
        self.assertIn(b"input:focus", PAGE)
        self.assertIn(b"/focused", PAGE)

    def test_focus_marker_accepts_only_the_exact_bounded_event(self):
        self.assertEqual(bounded_focus(b"focused=1"), "focus_event_observed=yes\n")
        for body in (b"", b"focused=0", b"focused=1&extra=1", b"x" * 65):
            with self.subTest(body=body), self.assertRaises(ValueError):
                bounded_focus(body)

    def test_reduced_outputs_are_new_files_and_never_follow_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "output"
            write_new(output, "reduced=yes\n")
            self.assertEqual(output.read_text(encoding="ascii"), "reduced=yes\n")
            with self.assertRaises(FileExistsError):
                write_new(output, "replacement=yes\n")
            target = root / "target"
            target.write_text("untouched\n", encoding="ascii")
            link = root / "link"
            link.symlink_to(target)
            with self.assertRaises(FileExistsError):
                write_new(link, "overwritten\n")
            self.assertEqual(target.read_text(encoding="ascii"), "untouched\n")

    def test_reduces_exact_synthetic_value_and_zero_events(self):
        self.assertEqual(
            bounded_result(b"value=nihao&starts=0&updates=0&ends=0&compositioninputs=0"),
            "ascii_value_exact=yes\ncomposition_start_count=0\ncomposition_update_count=0\ncomposition_end_count=0\ncomposition_input_count=0\n")

    def test_rejects_extra_missing_duplicate_or_oversized_fields(self):
        full = b"value=nihao&starts=0&updates=0&ends=0&compositioninputs=0"
        for body in (b"value=nihao&starts=0", full + b"&raw=x",
                     full.replace(b"value=nihao", b"value=nihao&value=x"), b"x" * 257):
            with self.subTest(body=body), self.assertRaises(ValueError):
                bounded_result(body)

    def test_records_mismatch_without_retaining_value(self):
        got = bounded_result(b"value=wrong&starts=2&updates=3&ends=4&compositioninputs=5")
        self.assertEqual(got, "ascii_value_exact=no\ncomposition_start_count=2\ncomposition_update_count=3\ncomposition_end_count=4\ncomposition_input_count=5\n")
        self.assertNotIn("wrong", got)


if __name__ == "__main__":
    unittest.main()
