#!/usr/bin/env python3
import sys
import subprocess
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from fcitx_wayland_trace import (ReducedEvidenceError, analyze, analyze_lifecycle,
                                 analyze_no_popup, analyze_toplevel)

# Exact closure_print syntax from Wayland 1.22.0 src/connection.c lines
# 1286-1291, 1324-1328, and 1332-1345 (Noble's libwayland generation):
# timestamp, two-space outgoing arrow, and interface@decimal-id objects.
# The failed guest removed raw traces as required, so this is source-shaped and
# intentionally not described as a captured run excerpt.
PREFIX = b'''[123456.001]  -> xdg_wm_base@8.get_xdg_surface(new id xdg_surface@20, wl_surface@19)\n[123456.002]  -> xdg_surface@20.get_toplevel(new id xdg_toplevel@21)\n'''
POPUP = b'''[123457.001]  -> xdg_wm_base@8.get_xdg_surface(new id xdg_surface@31, wl_surface@30)\n[123457.002]  -> xdg_surface@31.get_popup(new id xdg_popup@32, xdg_surface@20, xdg_positioner@29)\n[123457.004]  -> wl_surface@30.attach(wl_buffer@44, 0, 0)\n[123457.005]  -> wl_surface@30.commit()\n'''
POPUP = POPUP.replace(b"[123457.004]", b"[123457.003] xdg_popup@32.configure(4, 8, 320, 90)\n[123457.004]")

class TraceTest(unittest.TestCase):
    def test_toplevel_requires_one_mapped_ordinary_surface(self):
        mapped = PREFIX + b'''[123456.003]  -> wl_surface@19.attach(wl_buffer@40, 0, 0)\n[123456.004]  -> wl_surface@19.commit()\n'''
        got = analyze_toplevel(mapped, 0)
        self.assertEqual(got["mapped_toplevel_count"], 1)
        self.assertEqual(got["toplevel_wl_surface"], 19)
        for invalid in (
            PREFIX,
            mapped.replace(b"wl_buffer@40", b"nil"),
            mapped + PREFIX.replace(b"xdg_surface@20", b"xdg_surface@50")
                                  .replace(b"xdg_toplevel@21", b"xdg_toplevel@51")
                                  .replace(b"wl_surface@19", b"wl_surface@49")
                   + b"wl_surface@49.attach(wl_buffer@60, 0, 0)\nwl_surface@49.commit()\n",
            mapped + b"zwlr_layer_shell_v1@9.get_layer_surface()\n",
        ):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                analyze_toplevel(invalid, 0)

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

    def test_no_popup_failure_classifies_phases_objects_and_unmatched_events(self):
        focus = PREFIX
        before_input = b"xdg_popup@90.configure(1, 2, 3, 4)\n"
        input_boundary = len(focus + before_input)
        created = b'''xdg_wm_base@8.get_xdg_surface(new id xdg_surface@31, wl_surface@30)
xdg_surface@31.get_popup(new id xdg_popup@32, xdg_surface@20, xdg_positioner@29)
xdg_popup@32.configure(4, 8, 320, 90)
wl_surface@30.attach(wl_buffer@44, 0, 0)
wl_surface@30.commit()
'''
        screenshot_boundary = len(focus + before_input + created)
        unmapped = b'''wl_surface@30.attach(nil, 0, 0)
wl_surface@30.commit()
'''
        submit_boundary = len(focus + before_input + created + unmapped)
        after_submit = b'''xdg_popup@32.grab(wl_seat@4, 7)
xdg_surface@31.destroy()
'''
        raw = focus + before_input + created + unmapped + after_submit
        with self.assertRaises(ReducedEvidenceError) as caught:
            analyze_no_popup(raw, len(focus), input_boundary,
                             screenshot_boundary, submit_boundary)
        got = caught.exception.evidence
        self.assertEqual(got["focus_to_input_configure_events"], 1)
        self.assertEqual(got["candidate_popup_configures_unmatched"], 1)
        self.assertEqual(got["input_to_screenshot_popup_events"], 1)
        self.assertEqual(got["input_to_screenshot_map_events"], 1)
        self.assertEqual(got["input_to_screenshot_configure_events"], 1)
        self.assertEqual(got["candidate_popup_configures_matched"], 1)
        self.assertEqual(got["after_submit_grab_events"], 1)
        self.assertEqual(got["submit_to_stop_grab_events"], 1)
        self.assertEqual(got["after_stop_grab_events"], 0)
        self.assertEqual(got["candidate_popup_grabs_matched"], 1)
        self.assertEqual(got["candidate_objects_destroyed_after_focus"], 1)
        self.assertEqual(got["connection_identity"], "unproven")
        self.assertEqual(got["popup_attribution"], "unproven")
        self.assertRegex(
            str(got["candidate_object_1"]),
            r"child:31,popup:32,wl:30,parent:20,parent_role:toplevel,.*"
            r"map_count:1,unmap_count:1,configure_count:1,grab_count:1,destroyed:yes")

    def test_no_popup_distinguishes_response_from_shutdown(self):
        before_stop = b"xdg_popup@32.configure(1, 2, 3, 4)\n"
        crossing_stop = b"xdg_popup@33.configure(1, 2, 3, 4)\n"
        after_stop = b"xdg_popup@33.configure(1, 2, 3, 4)\n"
        submit = len(PREFIX)
        stop = len(PREFIX + before_stop) + 5
        with self.assertRaises(ReducedEvidenceError) as caught:
            analyze_no_popup(PREFIX + before_stop + crossing_stop + after_stop, submit,
                             submit, submit, submit, stop)
        got = caught.exception.evidence
        self.assertEqual(got["submit_to_stop_configure_events"], 1)
        self.assertEqual(got["after_stop_configure_events"], 2)
        self.assertEqual(got["after_submit_configure_events"], 3)
        self.assertEqual(got["stop_boundary_byte"], stop)

    def test_no_popup_failure_output_is_bounded_and_contains_no_trace_text(self):
        secret = b"synthetic-password-value"
        repeated = b"".join(
            (f"xdg_popup@{100 + index}.configure(1, 2, 3, 4)\n".encode("ascii") + secret + b"\n")
            for index in range(40))
        raw = PREFIX + repeated
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "trace"
            trace.write_bytes(raw)
            run = subprocess.run(
                [sys.executable, str(Path(__file__).with_name("fcitx_wayland_trace.py")),
                 str(trace), "--after-byte", str(len(PREFIX)), "--no-popup"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn(b"candidate_popup_configures=40\n", run.stdout)
        self.assertIn(b"reduced_events_recorded=32\n", run.stdout)
        self.assertIn(b"reduced_events_omitted=8\n", run.stdout)
        self.assertNotIn(secret, run.stdout + run.stderr)
        self.assertNotIn(b"xdg_popup@", run.stdout + run.stderr)
        self.assertNotIn(b"Traceback", run.stderr)

    def test_no_popup_caps_candidate_object_records(self):
        popups = b"".join(
            f"xdg_wm_base@8.get_xdg_surface(new id xdg_surface@{200 + index}, wl_surface@{300 + index})\n"
            f"xdg_surface@{200 + index}.get_popup(new id xdg_popup@{400 + index}, xdg_surface@20, xdg_positioner@29)\n"
            .encode("ascii") for index in range(20))
        with self.assertRaises(ReducedEvidenceError) as caught:
            analyze_no_popup(PREFIX + popups, len(PREFIX))
        got = caught.exception.evidence
        self.assertEqual(got["candidate_objects_relevant"], 20)
        self.assertEqual(got["candidate_objects_recorded"], 16)
        self.assertEqual(got["candidate_objects_omitted"], 4)
        self.assertIn("candidate_object_16", got)
        self.assertNotIn("candidate_object_17", got)

    def test_no_popup_success_records_ordered_boundaries_and_zero_activity(self):
        raw = PREFIX + b"wl_display@1.sync(new id wl_callback@99)\n"
        got = analyze_no_popup(raw, len(PREFIX), len(PREFIX),
                               len(raw), len(raw))
        self.assertEqual(got["observed_candidate_popup"], "no")
        self.assertEqual(got["connection_identity"], "unproven")
        self.assertEqual(got["popup_attribution"], "unproven")
        self.assertEqual(got["candidate_objects_relevant"], 0)
        self.assertEqual(got["input_to_screenshot_popup_events"], 0)
        self.assertEqual(got["focus_boundary_byte"], len(PREFIX))
        self.assertEqual(got["screenshot_boundary_byte"], len(raw))

    def test_no_popup_rejects_unordered_phase_boundaries(self):
        with self.assertRaisesRegex(ValueError, "not ordered"):
            analyze_no_popup(PREFIX, 4, 3, 5, 6)

    def test_no_popup_treats_a_boundary_crossing_line_as_sensitive(self):
        event = b"xdg_surface@31.get_popup(new id xdg_popup@32, xdg_surface@20, xdg_positioner@29)\n"
        raw = PREFIX + event
        boundary = len(PREFIX) + len(event) // 2
        with self.assertRaisesRegex(ValueError, "popup activity"):
            analyze_no_popup(raw, boundary, boundary, len(raw), len(raw))

    def test_no_popup_keeps_destroyed_surface_ids_fail_closed(self):
        created = PREFIX + b'''xdg_wm_base@8.get_xdg_surface(new id xdg_surface@31, wl_surface@30)
xdg_surface@31.get_popup(new id xdg_popup@32, xdg_surface@20, xdg_positioner@29)
xdg_surface@31.destroy()
wl_surface@30.destroy()
'''
        reused_map = b'''wl_surface@30.attach(wl_buffer@44, 0, 0)
wl_surface@30.commit()
'''
        with self.assertRaises(ReducedEvidenceError) as caught:
            analyze_no_popup(created + reused_map, len(created))
        got = caught.exception.evidence
        self.assertEqual(got["candidate_popup_maps"], 1)
        self.assertIn("type:map", str(got["reduced_event_1"]))
        self.assertIn("popup:0,child:0,candidate:no", str(got["reduced_event_1"]))
        self.assertEqual(got["candidate_objects_relevant"], 0)

if __name__ == "__main__":
    unittest.main()
