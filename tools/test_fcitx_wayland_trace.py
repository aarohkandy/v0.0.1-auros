#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from fcitx_wayland_trace import analyze

# Exact closure_print syntax from Wayland 1.22.0 src/connection.c lines
# 1286-1291, 1324-1328, and 1332-1345 (Noble's libwayland generation):
# timestamp, two-space outgoing arrow, and interface@decimal-id objects.
# The failed guest removed raw traces as required, so this is source-shaped and
# intentionally not described as a captured run excerpt.
PREFIX = b'''[123456.001]  -> xdg_wm_base@8.get_xdg_surface(new id xdg_surface@20, wl_surface@19)\n[123456.002]  -> xdg_surface@20.get_toplevel(new id xdg_toplevel@21)\n'''
POPUP = b'''[123457.001]  -> xdg_wm_base@8.get_xdg_surface(new id xdg_surface@31, wl_surface@30)\n[123457.002]  -> xdg_surface@31.get_popup(new id xdg_popup@32, xdg_surface@20, xdg_positioner@29)\n[123457.004]  -> wl_surface@30.attach(wl_buffer@44, 0, 0)\n[123457.005]  -> wl_surface@30.commit()\n'''
POPUP = POPUP.replace(b"[123457.004]", b"[123457.003] xdg_popup@32.configure(4, 8, 320, 90)\n[123457.004]")

class TraceTest(unittest.TestCase):
    def test_accepts_one_mapped_popup_with_toplevel_parent(self):
        got = analyze(PREFIX + POPUP, len(PREFIX))
        self.assertEqual(got["parent_xdg_surface"], 20)
        self.assertEqual(got["popup_wl_surface"], 30)
        self.assertEqual(got["connection_identity"], "unproven")

    def test_requires_libwayland_object_separator(self):
        # libwayland 1.22 closure logging is interface@id. The former '#'
        # fixtures hid that the parser matched no event in the real guest.
        invented = (PREFIX + POPUP).replace(b"@", b"#")
        with self.assertRaisesRegex(
                ValueError,
                r"got 0 \(surface=0 toplevel=0 popup=0 attach=0 commit=0 configure=0\)"):
            analyze(invented, len(PREFIX))

    def test_merged_namespaces_never_claim_connection_identity(self):
        # WAYLAND_DEBUG inherited by Firefox children has no connection tag;
        # equal object numbers from two streams can satisfy syntax only.
        got = analyze(PREFIX + POPUP, len(PREFIX))
        self.assertEqual(got["parented_popup_syntax_observed"], "yes")
        self.assertEqual(got["connection_identity"], "unproven")

    def test_rejects_unmapped_wrong_parent_and_ambiguous(self):
        with self.assertRaisesRegex(ValueError, "got 0"):
            analyze(PREFIX + POPUP.replace(b".commit()", b".damage(0, 0, 1, 1)"), len(PREFIX))
        wrong = POPUP.replace(b"xdg_surface@20,", b"xdg_surface@99,")
        with self.assertRaisesRegex(ValueError, "got 0"):
            analyze(PREFIX + wrong, len(PREFIX))
        second = (POPUP.replace(b"xdg_surface@31", b"xdg_surface@51")
                       .replace(b"xdg_popup@32", b"xdg_popup@52")
                       .replace(b"wl_surface@30", b"wl_surface@50"))
        with self.assertRaisesRegex(ValueError, "got 2"):
            analyze(PREFIX + POPUP + second, len(PREFIX))

    def test_boundary_must_cover_popup_not_root(self):
        with self.assertRaisesRegex(ValueError, "got 0"):
            analyze(PREFIX + POPUP, len(PREFIX + POPUP))
        with self.assertRaisesRegex(ValueError, "outside"):
            analyze(PREFIX, len(PREFIX) + 1)

    def test_rejects_commit_before_attach_and_layer_shell(self):
        reordered = POPUP.replace(
            b"[123457.004]  -> wl_surface@30.attach(wl_buffer@44, 0, 0)\n[123457.005]  -> wl_surface@30.commit()",
            b"[123457.004]  -> wl_surface@30.commit()\n[123457.005]  -> wl_surface@30.attach(wl_buffer@44, 0, 0)")
        with self.assertRaisesRegex(ValueError, "got 0"):
            analyze(PREFIX + reordered, len(PREFIX))
        with self.assertRaisesRegex(ValueError, "layer-shell"):
            analyze(PREFIX + b"zwlr_layer_shell_v1@9.get_layer_surface()\n" + POPUP, len(PREFIX))

    def test_rejects_unmap_missing_geometry_and_grab(self):
        unmapped = POPUP + b"[123458.001]  -> wl_surface@30.attach(nil, 0, 0)\n[123458.002]  -> wl_surface@30.commit()\n"
        with self.assertRaisesRegex(ValueError, "got 0"):
            analyze(PREFIX + unmapped, len(PREFIX))
        with self.assertRaisesRegex(ValueError, "got 0"):
            analyze(PREFIX + POPUP.replace(b"xdg_popup@32.configure(4, 8, 320, 90)\n", b""), len(PREFIX))
        with self.assertRaisesRegex(ValueError, "input grab"):
            analyze(PREFIX + POPUP.replace(b"[123457.004]", b"[123457.003]  -> xdg_popup@32.grab(wl_seat@4, 7)\n[123457.004]"), len(PREFIX))

    def test_rejects_destroyed_or_reused_parent(self):
        destroyed = b"[123456.003]  -> xdg_surface@20.destroy()\n"
        with self.assertRaisesRegex(ValueError, "got 0"):
            analyze(PREFIX + destroyed + POPUP, len(PREFIX))

if __name__ == "__main__":
    unittest.main()
