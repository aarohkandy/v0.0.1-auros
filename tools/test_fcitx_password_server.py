#!/usr/bin/env python3
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fcitx_password_server import PAGE, bounded_completion, bounded_focus, bounded_result, write_new


class PasswordResultTest(unittest.TestCase):
    def test_page_requires_a_real_click_and_masks_the_synthetic_value(self):
        self.assertIn(b'type="password"', PAGE)
        self.assertNotIn(b"autofocus", PAGE)
        self.assertIn(b"input:focus", PAGE)
        self.assertIn(b"/focused", PAGE)
        self.assertIn(b"preventDefault()", PAGE)
        self.assertIn(b"stopImmediatePropagation()", PAGE)
        self.assertIn(b"!x.isTrusted||x.repeat||x.key!=='Enter'||x.isComposing", PAGE)
        self.assertIn(b"x.ctrlKey||x.altKey||x.metaKey||x.shiftKey", PAGE)
        self.assertIn(b"await new Promise(requestAnimationFrame)", PAGE)
        self.assertIn(b"document.activeElement===i", PAGE)
        self.assertNotIn(b"<form", PAGE)
        self.assertNotIn(b"SubmitEvent", PAGE)
        self.assertNotIn(b"addEventListener('submit'", PAGE)

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
        reduced, _ = bounded_result(
            b"value=nihao&starts=0&updates=0&ends=0&compositioninputs=0&focusbefore=yes&focusafter=no")
        self.assertEqual(
            reduced,
            "ascii_value_exact=yes\ncomposition_start_count=0\ncomposition_update_count=0\ncomposition_end_count=0\ncomposition_input_count=0\nactive_element_before_result=yes\n")

    def test_completion_requires_focus_before_and_after_result(self):
        first = b"value=nihao&starts=0&updates=0&ends=0&compositioninputs=0&focusbefore=yes&focusafter=no"
        _, initial = bounded_result(first)
        body = b"value=nihao&starts=0&updates=0&ends=0&compositioninputs=0&focusbefore=yes&focusafter=yes&token=secret"
        self.assertEqual(bounded_completion(body, "secret", initial), "active_element_after_result=yes\n")
        for replacement in (b"focusbefore=no", b"focusafter=no"):
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                bounded_completion(body.replace(replacement.replace(b"no", b"yes"), replacement),
                                   "secret", initial)
        for changed in (body.replace(b"token=secret", b"token=wrong"),
                        body.replace(b"starts=0", b"starts=1"),
                        body.replace(b"value=nihao", b"value=other")):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                bounded_completion(changed, "secret", initial)

    def test_rejects_extra_missing_duplicate_or_oversized_fields(self):
        full = b"value=nihao&starts=0&updates=0&ends=0&compositioninputs=0&focusbefore=yes&focusafter=no"
        for body in (b"value=nihao&starts=0", full + b"&raw=x",
                     full.replace(b"value=nihao", b"value=nihao&value=x"), b"x" * 257):
            with self.subTest(body=body), self.assertRaises(ValueError):
                bounded_result(body)

    def test_records_mismatch_without_retaining_value(self):
        got, _ = bounded_result(b"value=wrong&starts=2&updates=3&ends=4&compositioninputs=5&focusbefore=yes&focusafter=no")
        self.assertEqual(got, "ascii_value_exact=no\ncomposition_start_count=2\ncomposition_update_count=3\ncomposition_end_count=4\ncomposition_input_count=5\nactive_element_before_result=yes\n")
        self.assertNotIn("wrong", got)


if __name__ == "__main__":
    unittest.main()
