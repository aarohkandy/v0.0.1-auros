#!/usr/bin/env python3
import tempfile
import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fcitx_marker_visibility import analyze


def frame(path, bands=(), rectangle=None, width=1280, height=800):
    pixels = bytearray(b"\xff" * width * height * 3)
    areas = list(bands)
    if rectangle:
        areas.append(rectangle)
    for left, top, right, bottom in areas:
        for y in range(top, bottom):
            for x in range(left, right):
                offset = (y * width + x) * 3
                pixels[offset:offset + 3] = b"\x00\x00\x00"
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + pixels)


class MarkerVisibilityTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.before = Path(self.directory.name) / "before.ppm"
        self.after = Path(self.directory.name) / "after.ppm"
        frame(self.before)

    def tearDown(self):
        self.directory.cleanup()

    def test_accepts_multiline_marker_and_clicks_changed_pixel(self):
        bands = [(250, 100 + line * 20, 300 + line * 16, 108 + line * 20)
                 for line in range(24)]
        frame(self.after, bands=bands)
        result = analyze(self.before, self.after)
        self.assertEqual(result["status"], "pass")
        self.assertGreaterEqual(result["marker_pattern_steps"], 20)
        self.assertGreater(result["click_pixel_delta"], 24)

    def test_rejects_unrelated_large_change_without_line_signature(self):
        frame(self.after, rectangle=(200, 200, 1100, 350))
        result = analyze(self.before, self.after)
        self.assertGreater(result["changed_samples"] * 100,
                           result["total_samples"] * 3)
        self.assertEqual(result["status"], "fail")

    def test_rejects_small_multiline_notification(self):
        bands = [(400, 100 + line * 12, 700, 106 + line * 12) for line in range(8)]
        frame(self.after, bands=bands)
        self.assertEqual(analyze(self.before, self.after)["status"], "fail")

    def test_rejects_many_unrelated_equal_width_stripes(self):
        bands = [(250, 100 + line * 20, 950, 108 + line * 20) for line in range(24)]
        frame(self.after, bands=bands)
        self.assertEqual(analyze(self.before, self.after)["status"], "fail")


if __name__ == "__main__":
    unittest.main()
