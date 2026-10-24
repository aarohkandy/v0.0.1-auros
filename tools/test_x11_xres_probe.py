#!/usr/bin/env python3
import importlib.util
import pathlib
import struct
import unittest


HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("xres_probe", HERE / "x11_xres_probe.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class ParserTests(unittest.TestCase):
    def test_setup_request_is_the_complete_twelve_byte_prefix(self):
        self.assertEqual(probe.setup_request(), b"l\0\x0b\0\0\0\0\0\0\0\0\0")

    def test_setup_root_accounts_for_vendor_padding_and_formats(self):
        body = bytearray(32 + 8 + 16 + 40)
        struct.pack_into("<H", body, 16, 5)
        body[21] = 2
        struct.pack_into("<I", body, 56, 0x12345678)
        self.assertEqual(probe.parse_setup_root(bytes(body)), 0x12345678)

    def test_query_extension_real_wire_offsets_skip_reply_padding(self):
        # Shape captured from the X11 core QueryExtension reply: byte 1 is
        # padding, present and major-opcode are bytes 8 and 9.
        head = bytearray(32)
        head[0] = 1
        head[8] = 1
        head[9] = 145
        self.assertEqual(
            probe.parse_query_extension_reply(bytes(head), "X-Resource"), 145)

    def test_query_extension_absent_and_zero_opcode_fail_closed(self):
        head = bytearray(32)
        head[0] = 1
        with self.assertRaises(probe.X11Error):
            probe.parse_query_extension_reply(bytes(head), "X-Resource")
        head[8] = 1
        with self.assertRaises(probe.X11Error):
            probe.parse_query_extension_reply(bytes(head), "X-Resource")

    def test_xres_local_pid_is_server_record_not_window_property(self):
        body = struct.pack("<IIII", 0x04000000, probe.LOCAL_CLIENT_PID, 4, 4242)
        self.assertEqual(probe.parse_xres_ids(body, 1), [(0x04000000, 4242)])

    def test_xres_query_requests_one_local_pid_spec_only(self):
        self.assertEqual(probe.xres_client_ids_packet(145),
                         struct.pack("<BBHIII", 145, 4, 4, 1, 0,
                                     probe.LOCAL_CLIENT_PID))

    def test_xres_unknown_id_kind_is_rejected(self):
        body = struct.pack("<IIII", 0x04000000, 1, 4, 0x04000000)
        with self.assertRaises(probe.X11Error):
            probe.parse_xres_ids(body, 1)

    def test_xres_mixed_mask_and_extended_pid_are_rejected(self):
        mixed = struct.pack("<IIII", 1, probe.LOCAL_CLIENT_PID | 1, 4, 42)
        extended = struct.pack("<III", 1, probe.LOCAL_CLIENT_PID, 8) + b"x" * 8
        with self.assertRaises(probe.X11Error):
            probe.parse_xres_ids(mixed, 1)
        with self.assertRaises(probe.X11Error):
            probe.parse_xres_ids(extended, 1)

    def test_per_window_query_rejects_two_pid_records(self):
        class Reply:
            def request(self, _packet):
                head = bytearray(32)
                struct.pack_into("<I", head, 8, 2)
                body = b"".join(
                    struct.pack("<IIII", client, probe.LOCAL_CLIENT_PID, 4, pid)
                    for client, pid in ((1, 42), (2, 42)))
                return bytes(head), body
        with self.assertRaises(probe.X11Error):
            probe.XConnection.xres_pid(Reply(), 145, 123)

    def test_xres_truncated_and_oversized_records_fail_closed(self):
        with self.assertRaises(probe.X11Error):
            probe.parse_xres_ids(b"\0" * 11, 1)
        with self.assertRaises(probe.X11Error):
            probe.parse_xres_ids(struct.pack("<III", 1, 2, 65) + b"\0" * 68, 1)

    def test_generic_event_payload_is_consumed_and_bounded(self):
        event = bytearray(32)
        event[0] = 35
        struct.pack_into("<I", event, 4, 3)
        self.assertEqual(probe.event_extra_length(bytes(event)), 12)
        struct.pack_into("<I", event, 4, probe.MAX_REPLY // 4 + 1)
        with self.assertRaises(probe.X11Error):
            probe.event_extra_length(bytes(event))

    def test_inventory_rejects_unbounded_deadlines_before_connecting(self):
        for timeout in (0, -1, 61):
            with self.assertRaises(probe.X11Error):
                probe.inventory(":9876", timeout=timeout)

    def test_empty_windows_are_not_positive_identity_evidence(self):
        empty = {"windows": [], "local_clients": []}
        self.assertEqual(probe.evidence_state(empty, 42), "inconclusive")
        identified = {"windows": [], "local_clients": [{"pid": 42}]}
        self.assertEqual(probe.evidence_state(identified, 42), "identity-only")
        duplicate = {"windows": [], "local_clients": [{"pid": 42}, {"pid": 42}]}
        self.assertEqual(probe.evidence_state(duplicate, 42), "inconclusive")
        mapped = {"local_clients": [],
                  "windows": [{"pid": 42, "mapped": True,
                               "absolute": {"x": 1, "y": 2,
                                            "width": 3, "height": 4}}]}
        self.assertEqual(probe.evidence_state(mapped, 42), "mapped")


if __name__ == "__main__":
    unittest.main()
