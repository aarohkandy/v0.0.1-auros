#!/usr/bin/env python3
import os
import errno
import unittest
from pathlib import Path
from unittest import mock
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fcitx_connection_probe as probe


class ConnectionProbeTest(unittest.TestCase):
    def test_descendants_are_transitive(self):
        self.assertEqual(probe.descendants_from_parents(
                10, {11: 10, 12: 11, 13: 99, 14: 12}), [11, 12, 14])
        self.assertEqual(probe.parse_ppid("123 (name with spaces) S 77 1 2 3"), 77)
        self.assertFalse(probe.fd_flags_writable(os.O_RDONLY))
        self.assertTrue(probe.fd_flags_writable(os.O_WRONLY))
        self.assertTrue(probe.fd_flags_writable(os.O_RDWR | os.O_APPEND))

    def test_socket_inventory_fails_when_pidfd_introspection_is_unavailable(self):
        entries = [mock.Mock(name="3")]
        entries[0].name = "3"
        with mock.patch.object(probe.os, "readlink", return_value="socket:[1]"), \
             mock.patch.object(probe.os, "close"), \
             mock.patch.object(probe.Path, "iterdir", return_value=entries), \
             mock.patch.object(probe, "duplicate_fd", side_effect=PermissionError()):
            with self.assertRaisesRegex(RuntimeError, "pidfd_getfd"):
                probe.socket_inventory(11, 41, 22, Path("/run/user/1000/wayland-0"))

    def test_socket_inventory_fails_if_only_some_sockets_are_inspectable(self):
        entries = [mock.Mock(name="3"), mock.Mock(name="4")]
        entries[0].name, entries[1].name = "3", "4"
        with mock.patch.object(probe.os, "readlink", return_value="socket:[1]"), \
             mock.patch.object(probe.os, "close"), \
             mock.patch.object(probe.Path, "iterdir", return_value=entries), \
             mock.patch.object(probe, "duplicate_fd", side_effect=[51, PermissionError()]), \
             mock.patch.object(probe.socket, "socket") as sock:
            sock.return_value.family = probe.socket.AF_INET
            with self.assertRaisesRegex(RuntimeError, "every application socket"):
                probe.socket_inventory(11, 41, 22, Path("/run/user/1000/wayland-0"))

    def test_socket_inventory_fails_on_post_duplicate_inspection_error(self):
        entry = mock.Mock(name="3"); entry.name = "3"
        broken = mock.Mock()
        broken.family = probe.socket.AF_UNIX
        broken.getpeername.side_effect = OSError(errno.EIO, "broken")
        with mock.patch.object(probe.os, "readlink", return_value="socket:[1]"), \
             mock.patch.object(probe.os, "close"), \
             mock.patch.object(probe.Path, "iterdir", return_value=[entry]), \
             mock.patch.object(probe, "duplicate_fd", return_value=51), \
             mock.patch.object(probe.socket, "socket", return_value=broken):
            with self.assertRaisesRegex(RuntimeError, "inventory incomplete"):
                probe.socket_inventory(11, 41, 22, Path("/run/user/1000/wayland-0"))
            broken.detach.assert_called_once()

    def test_unix_path_inodes_reads_the_kernel_socket_inode(self):
        content = ("Num RefCount Protocol Flags Type St Inode Path\n"
                   "0000: 2 0 00010000 0001 01 12345 /run/user/1000/wayland-0\n"
                   "0001: 2 0 00000000 0001 03 54321\n")
        with mock.patch.object(probe.Path, "read_text", return_value=content):
            self.assertEqual(probe.unix_path_inodes(
                Path("/run/user/1000/wayland-0")), {12345})

    def test_snapshot_rejects_children_multiple_connections_and_writers(self):
        path = Path("/trace")
        cases = [
            ([12], [(11, 7)], (1, [2], [(9, 2)]), "descendants=1"),
            ([], [(11, 7), (12, 8)], (1, [2], [(9, 2)]), "writer count=2"),
            ([], [(11, 7)], (1, [2, 3], [(9, 2), (10, 3)]), "socket count=2"),
        ]
        for children, writers, inventory, error in cases:
            with self.subTest(error=error), \
                 mock.patch.object(probe, "descendants", return_value=children), \
                 mock.patch.object(probe, "fd_writers", return_value=writers), \
                 mock.patch.object(probe, "socket_inventory", return_value=(inventory[1], inventory[2])), \
                 mock.patch.object(probe.os, "pidfd_open", return_value=inventory[0], create=True), \
                 mock.patch.object(probe, "pidfd_alive", return_value=True), \
                 mock.patch.object(probe.os, "close"):
                with self.assertRaisesRegex(RuntimeError, error):
                    probe.snapshot(11, 22, Path("/wayland-0"), path)

    def test_snapshot_accepts_one_stable_writer_and_connection(self):
        with mock.patch.object(probe, "descendants", return_value=[]), \
             mock.patch.object(probe, "fd_writers", return_value=[(11, 7)]), \
             mock.patch.object(probe, "socket_inventory", return_value=([2], [(9, 2)])), \
             mock.patch.object(probe.os, "pidfd_open", return_value=1, create=True), \
             mock.patch.object(probe, "pidfd_alive", return_value=True), \
             mock.patch.object(probe, "duplicate_fd", return_value=5), \
             mock.patch.object(probe.os, "fstat", side_effect=[mock.Mock(st_dev=8, st_ino=44), mock.Mock(st_ino=123)]), \
             mock.patch.object(probe.Path, "stat", return_value=mock.Mock(st_dev=8, st_ino=44)):
            self.assertEqual(probe.snapshot(11, 22, Path("/wayland-0"), Path("/trace")),
                             (1, [2, 5], 9, 123, 7, 8, 44, 5))

    def test_open_description_comparison_fails_closed(self):
        fake = mock.Mock()
        fake.syscall.return_value = -1
        fake.get_errno.return_value = 1
        with mock.patch.object(probe.ctypes, "CDLL", return_value=fake), \
             mock.patch.object(probe.platform, "machine", return_value="x86_64"):
            with self.assertRaises(OSError):
                probe.same_open_file_description(3, 4)
        for result, expected in ((0, True), (1, False)):
            fake = mock.Mock()
            fake.syscall.return_value = result
            with self.subTest(result=result), \
                 mock.patch.object(probe.ctypes, "CDLL", return_value=fake), \
                 mock.patch.object(probe.platform, "machine", return_value="x86_64"):
                self.assertEqual(probe.same_open_file_description(3, 4), expected)


if __name__ == "__main__":
    unittest.main()
