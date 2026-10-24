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

    def test_xres_local_pid_is_server_record_not_window_property(self):
        body = struct.pack("<IIII", 0x04000000, probe.LOCAL_CLIENT_PID, 4, 4242)
        self.assertEqual(probe.parse_xres_ids(body, 1), [(0x04000000, 4242)])

    def test_xres_unknown_id_kind_is_not_misread_as_pid(self):
        body = struct.pack("<IIII", 0x04000000, 1, 4, 0x04000000)
        self.assertEqual(probe.parse_xres_ids(body, 1), [])

    def test_xres_truncated_and_oversized_records_fail_closed(self):
        with self.assertRaises(probe.X11Error):
            probe.parse_xres_ids(b"\0" * 11, 1)
        with self.assertRaises(probe.X11Error):
            probe.parse_xres_ids(struct.pack("<III", 1, 2, 65) + b"\0" * 68, 1)


if __name__ == "__main__":
    unittest.main()
