#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from fcitx_wayland_trace import analyze, analyze_lifecycle, analyze_no_popup

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

    def test_lifecycle_requires_unmap_then_fresh_return_map(self):
        away = b'''[123458.001]  -> wl_surface@30.attach(nil, 0, 0)\n[123458.002]  -> wl_surface@30.commit()\n'''
        b_input = b"[123458.500]  -> wl_display@1.sync(new id wl_callback@99)\n"
        returned = b'''[123459.001]  -> wl_surface@30.attach(wl_buffer@45, 0, 0)\n[123459.002]  -> wl_surface@30.commit()\n'''
        raw = PREFIX + POPUP + away + b_input + returned
        got = analyze_lifecycle(raw, len(PREFIX), len(PREFIX + POPUP),
                                len(PREFIX + POPUP + away),
                                len(PREFIX + POPUP + away + b_input))
        self.assertEqual(got["initial_unmapped_before_b_input"], "yes")
        self.assertEqual(got["return_map_epoch"], "fresh")
        self.assertEqual(got["return_popup_reused"], "yes")
        redraw = b'''[123459.003]  -> wl_surface@30.attach(wl_buffer@46, 0, 0)\n[123459.004]  -> wl_surface@30.commit()\n'''
        got = analyze_lifecycle(raw + redraw, len(PREFIX), len(PREFIX + POPUP),
                                len(PREFIX + POPUP + away),
                                len(PREFIX + POPUP + away + b_input))
        self.assertEqual(got["return_map_epoch"], "fresh")

    def test_lifecycle_rejects_stays_mapped_and_mapped_away(self):
        return_map = b'''[123459.001]  -> wl_surface@30.attach(wl_buffer@45, 0, 0)\n[123459.002]  -> wl_surface@30.commit()\n'''
        separator = b"[123458.000]  -> wl_display@1.sync(new id wl_callback@99)\n"
        with self.assertRaisesRegex(ValueError, "did not unmap"):
            late_unmap = b'''wl_surface@30.attach(nil, 0, 0)\nwl_surface@30.commit()\n'''
            analyze_lifecycle(PREFIX + POPUP + separator + late_unmap + return_map, len(PREFIX),
                              len(PREFIX + POPUP), len(PREFIX + POPUP + separator),
                              len(PREFIX + POPUP + separator + late_unmap))
        away = b'''[123458.001]  -> wl_surface@30.attach(nil, 0, 0)\n[123458.002]  -> wl_surface@30.commit()\n[123458.003]  -> wl_surface@30.attach(wl_buffer@45, 0, 0)\n[123458.004]  -> wl_surface@30.commit()\n'''
        with self.assertRaisesRegex(ValueError, "mapped during the away epoch"):
            analyze_lifecycle(PREFIX + POPUP + away + separator + return_map, len(PREFIX),
                              len(PREFIX + POPUP), len(PREFIX + POPUP + away),
                              len(PREFIX + POPUP + away + separator))

    def test_lifecycle_tracks_every_candidate_surface_while_b_has_focus(self):
        second = b'''xdg_wm_base@8.get_xdg_surface(new id xdg_surface@51, wl_surface@50)\nxdg_surface@51.get_popup(new id xdg_popup@52, xdg_surface@20, xdg_positioner@29)\n'''
        unmap = b'''wl_surface@30.attach(nil, 0, 0)\nwl_surface@30.commit()\n'''
        b_input = b"wl_display@1.sync(new id wl_callback@99)\n"
        bad_map = b'''wl_surface@50.attach(wl_buffer@60, 0, 0)\nwl_surface@50.commit()\n'''
        with self.assertRaisesRegex(ValueError, "mapped during the away epoch"):
            analyze_lifecycle(PREFIX + POPUP + second + unmap + b_input + bad_map,
                              len(PREFIX), len(PREFIX + POPUP + second),
                              len(PREFIX + POPUP + second + unmap),
                              len(PREFIX + POPUP + second + unmap + b_input + bad_map))
        transient = (b'''wl_surface@50.attach(wl_buffer@60, 0, 0)\nwl_surface@50.commit()\n'''
                     b'''wl_surface@50.attach(nil, 0, 0)\nwl_surface@50.commit()\n''')
        with self.assertRaisesRegex(ValueError, "mapped during the away epoch"):
            analyze_lifecycle(PREFIX + POPUP + second + unmap + transient + b_input + bad_map,
                              len(PREFIX), len(PREFIX + POPUP + second),
                              len(PREFIX + POPUP + second + unmap + transient),
                              len(PREFIX + POPUP + second + unmap + transient + b_input + bad_map))

    def test_no_popup_rejects_creation_configure_and_grab(self):
        boundary = len(PREFIX)
        self.assertEqual(analyze_no_popup(PREFIX, boundary)["observed_candidate_popup"], "no")
        for event in (
            b"xdg_surface@31.get_popup(new id xdg_popup@32, xdg_surface@20, xdg_positioner@29)\n",
            b"xdg_popup@32.configure(4, 8, 320, 90)\n",
            b"xdg_popup@32.grab(wl_seat@4, 7)\n",
        ):
            with self.subTest(event=event), self.assertRaisesRegex(ValueError, "popup activity"):
                analyze_no_popup(PREFIX + event, boundary)
        created_before = PREFIX + b'''xdg_wm_base@8.get_xdg_surface(new id xdg_surface@31, wl_surface@30)\nxdg_surface@31.get_popup(new id xdg_popup@32, xdg_surface@20, xdg_positioner@29)\n'''
        mapped_after = b'''wl_surface@30.attach(wl_buffer@44, 0, 0)\nwl_surface@30.commit()\n'''
        with self.assertRaisesRegex(ValueError, "popup activity"):
            analyze_no_popup(created_before + mapped_after, len(created_before))
        mapped_before = created_before + mapped_after
        with self.assertRaisesRegex(ValueError, "already mapped"):
            analyze_no_popup(mapped_before + b"wl_display@1.sync(new id wl_callback@99)\n",
                             len(mapped_before))

if __name__ == "__main__":
    unittest.main()
