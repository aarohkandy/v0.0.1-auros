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
    def test_success_claims_distinguish_application_and_probe_writers(self):
        claims = probe.SUCCESS_CLAIMS.splitlines()
        self.assertIn("application_trace_writers_at_boundaries=1", claims)
        self.assertIn("foreign_trace_writers_at_boundaries=0", claims)
        self.assertIn("authenticated_probe_trace_writer_at_end=1", claims)
        self.assertNotIn("trace_writers_at_boundaries=1", claims)

    def test_selects_exact_compositor_below_real_service_wrapper_shape(self):
        # aurshell.service is Type=simple and ExecStart begins with
        # dbus-run-session. The wrapper is MainPID; its aurshell child owns
        # WAYLAND_DISPLAY, while the private bus is a sibling.
        pids = {100, 101, 102}
        executables = {
            100: "/usr/bin/dbus-run-session",
            101: "/usr/bin/dbus-daemon",
            102: "/usr/bin/aurshell",
        }
        parents = {100: 1, 101: 100, 102: 100}
        self.assertEqual(probe.select_compositor(100, pids, executables, parents), 102)

    def test_compositor_identity_rejects_wrong_wrapper_or_ambiguity(self):
        pids = {100, 101, 102}
        parents = {100: 1, 101: 100, 102: 100}
        valid = {100: "/usr/bin/dbus-run-session", 101: "/usr/bin/aurshell",
                 102: "/usr/bin/dbus-daemon"}
        cases = [
            (99, pids, valid, parents, "outside its PAM session scope"),
            (100, pids, {**valid, 100: "/usr/bin/aurshell"}, parents,
             "not the exact session wrapper"),
            (100, pids, {**valid, 102: "/usr/bin/aurshell"}, parents,
             "expected one exact aurshell compositor, got 2"),
        ]
        for main, members, executables, links, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(RuntimeError, message):
                probe.select_compositor(main, members, executables, links)

    def test_compositor_identity_rejects_out_of_cgroup_ancestry(self):
        pids = {100, 102}
        executables = {100: "/usr/bin/dbus-run-session", 102: "/usr/bin/aurshell"}
        for parents, message in (
                ({100: 1, 102: 77}, "left the PAM session scope"),
                ({100: 1, 102: 102}, "ancestry contains a loop")):
            with self.subTest(message=message), self.assertRaisesRegex(RuntimeError, message):
                probe.select_compositor(100, pids, executables, parents)

    def test_descendants_are_transitive(self):
        self.assertEqual(probe.descendants_from_parents(
                10, {11: 10, 12: 11, 13: 99, 14: 12}), [11, 12, 14])
        stat = "123 (name with spaces) " + " ".join(
            ["S", "77"] + ["0"] * 17 + ["999"])
        self.assertEqual(probe.parse_process_stat(stat), (77, 999))
        self.assertEqual(probe.parse_ppid(stat), 77)
        self.assertFalse(probe.fd_flags_writable(os.O_RDONLY))
        self.assertTrue(probe.fd_flags_writable(os.O_WRONLY))
        self.assertTrue(probe.fd_flags_writable(os.O_RDWR | os.O_APPEND))

    def test_service_topology_rejects_stale_systemd_identity_even_same_tick(self):
        first = ({100, 102},
                 {100: "/usr/bin/dbus-run-session", 102: "/usr/bin/aurshell"},
                 {100: 1, 102: 100}, {100: 10, 102: 20})
        path = mock.Mock()
        path.is_dir.return_value = True
        expected = (100, "/system.slice/aurshell.service", "a" * 32)
        with mock.patch.object(probe, "cgroup_path", return_value=path), \
             mock.patch.object(probe, "process_cgroup", return_value="/user.slice/user-1000.slice/session-c1.scope"), \
             mock.patch.object(probe, "session_snapshot", side_effect=[first, first]), \
             mock.patch.object(probe, "systemd_service_properties", side_effect=[
                 expected, (999, expected[1], "b" * 32)]), \
             mock.patch.object(probe.os, "pidfd_open", side_effect=[31, 32], create=True), \
             mock.patch.object(probe, "pidfd_alive", return_value=True), \
             mock.patch.object(probe.os, "readlink", return_value="/usr/bin/dbus-run-session"), \
             mock.patch.object(probe, "process_uid", return_value=1000), \
             mock.patch.object(probe, "process_invocation_id", return_value="a" * 32), \
             mock.patch.object(probe.os, "close") as close:
            with self.assertRaisesRegex(RuntimeError, "properties changed during"):
                probe.read_service_topology(
                    100, "/system.slice/aurshell.service", 1000, "a" * 32)
            self.assertEqual(close.call_count, 2)

    def test_service_topology_binds_pam_scope_and_systemd_invocation(self):
        snapshot = ({100, 101, 102},
                    {100: "/usr/bin/dbus-run-session", 101: "/usr/bin/dbus-daemon",
                     102: "/usr/bin/aurshell"},
                    {100: 1, 101: 100, 102: 100}, {100: 10, 101: 11, 102: 12})
        path = mock.Mock(); path.is_dir.return_value = True
        scope = "/user.slice/user-1000.slice/session-c1.scope"
        with mock.patch.object(probe, "cgroup_path", return_value=path), \
             mock.patch.object(probe, "process_cgroup", return_value=scope), \
             mock.patch.object(probe, "session_snapshot", side_effect=[snapshot, snapshot]), \
             mock.patch.object(probe, "systemd_service_properties", return_value=(
                 100, "/system.slice/aurshell.service", "a" * 32)), \
             mock.patch.object(probe.os, "pidfd_open", side_effect=[31, 32], create=True), \
             mock.patch.object(probe, "pidfd_alive", return_value=True), \
             mock.patch.object(probe.os, "readlink", side_effect=[
                 "/usr/bin/dbus-run-session", "/usr/bin/aurshell"]), \
             mock.patch.object(probe, "process_uid", return_value=1000), \
             mock.patch.object(probe, "process_invocation_id", return_value="a" * 32):
            self.assertEqual(probe.read_service_topology(
                100, "/system.slice/aurshell.service", 1000, "a" * 32),
                (102, 31, 32, scope))

    def test_session_scope_rejects_arbitrary_or_wrong_user_cgroup(self):
        for value in ("/system.slice/aurshell.service",
                      "/user.slice/user-999.slice/session-c1.scope",
                      "/user.slice/user-1000.slice/app.scope"):
            with self.subTest(value=value), self.assertRaisesRegex(
                    RuntimeError, "expected user PAM session scope"):
                probe.session_snapshot(value, 1000)

    def test_socket_semantics_require_stream_and_listener_accept(self):
        endpoint = mock.Mock()
        endpoint.getsockopt.return_value = probe.socket.SOCK_DGRAM
        self.assertFalse(probe.is_stream_socket(endpoint, listener=False))
        endpoint.getsockopt.side_effect = [probe.socket.SOCK_STREAM, 0]
        self.assertFalse(probe.is_stream_socket(endpoint, listener=True))
        endpoint.getsockopt.side_effect = [probe.socket.SOCK_STREAM, 1]
        self.assertTrue(probe.is_stream_socket(endpoint, listener=True))

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
        broken.getsockopt.return_value = probe.socket.SOCK_STREAM
        broken.getpeername.side_effect = OSError(errno.EIO, "broken")
        evidence = {}
        with mock.patch.object(probe.os, "readlink", return_value="socket:[1]"), \
             mock.patch.object(probe.os, "close"), \
             mock.patch.object(probe.Path, "iterdir", return_value=[entry]), \
             mock.patch.object(probe, "duplicate_fd", return_value=51), \
             mock.patch.object(probe.socket, "socket", return_value=broken):
            with self.assertRaisesRegex(RuntimeError, "inventory incomplete"):
                probe.socket_inventory(
                    11, 41, 22, Path("/run/user/1000/wayland-0"), evidence)
            broken.detach.assert_called_once()
        self.assertEqual(evidence, {
            "application_socket_fd_count": 1,
            "application_socket_inspection_error_count": 1,
            "matching_compositor_connection_count": 0,
        })

    def test_socket_inventory_records_fd_enumeration_failure(self):
        evidence = {}
        with mock.patch.object(
                probe.Path, "iterdir", side_effect=OSError(errno.EACCES, "denied")):
            with self.assertRaisesRegex(RuntimeError, "FD inventory unavailable"):
                probe.socket_inventory(
                    11, 41, 22, Path("/run/user/1000/wayland-0"), evidence)
        self.assertEqual(evidence, {
            "application_socket_fd_count": 0,
            "application_socket_inspection_error_count": 1,
            "matching_compositor_connection_count": 0,
        })

    def test_unix_path_listener_reads_typed_kernel_record(self):
        content = ("Num RefCount Protocol Flags Type St Inode Path\n"
                   "0000: 2 0 00010000 0001 01 12345 /run/user/1000/wayland-0\n"
                   "0001: 2 0 00000000 0001 03 54321 /run/user/1000/wayland-0\n"
                   "0002: 2 0 00000000 0001 03 54322 /run/user/1000/wayland-0\n"
                   "0003: 2 0 00000000 0001 03 54323 /run/user/1000/wayland-0\n")
        with mock.patch.object(probe.Path, "read_text", return_value=content):
            self.assertEqual(probe.unix_path_listener(
                Path("/run/user/1000/wayland-0")), (12345, 4))

    def test_unix_path_listener_rejects_foreign_rebind(self):
        content = ("Num RefCount Protocol Flags Type St Inode Path\n"
                   "0000: 2 0 00010000 0001 01 12345 /run/user/1000/wayland-0\n"
                   "0001: 2 0 00010000 0001 01 54321 /run/user/1000/wayland-0\n")
        evidence = {}
        with mock.patch.object(probe.Path, "read_text", return_value=content):
            with self.assertRaisesRegex(RuntimeError, "got 2"):
                probe.unix_path_listener(Path("/run/user/1000/wayland-0"), evidence)
        self.assertEqual(evidence["wayland_path_inode_count"], 2)
        self.assertEqual(evidence["global_matching_listener_count"], 2)

    def test_listener_allows_accepted_wayland_sockets_with_same_path(self):
        path = Path("/run/user/1000/wayland-0")
        entry = mock.Mock(); entry.name = "7"
        listener = mock.Mock()
        listener.family = probe.socket.AF_UNIX
        listener.getsockname.return_value = str(path)
        listener.getsockopt.side_effect = [probe.socket.SOCK_STREAM, 1]
        evidence = {}
        def listener_record(_path, record):
            record.update(wayland_path_inode_count=4, global_matching_listener_count=1)
            return 22, 4
        with mock.patch.object(probe.Path, "stat", return_value=mock.Mock(st_mode=0o140777)), \
             mock.patch.object(probe, "unix_path_listener", side_effect=listener_record), \
             mock.patch.object(probe, "authenticate_display_path"), \
             mock.patch.object(probe.Path, "iterdir", return_value=[entry]), \
             mock.patch.object(probe.os, "readlink", return_value="socket:[22]"), \
             mock.patch.object(probe, "duplicate_fd", return_value=51), \
             mock.patch.object(probe.socket, "socket", return_value=listener), \
             mock.patch.object(probe.os, "fstat", return_value=mock.Mock(st_ino=22)), \
             mock.patch.object(probe.os, "close"):
            probe.verify_listener(102, 32, path, evidence)
        self.assertEqual(evidence, {
            "wayland_path_inode_count": 4,
            "global_matching_listener_count": 1,
            "compositor_socket_fd_count": 1,
            "matching_accepting_listener_fd_count": 1,
            "matching_accepting_listener_open_description_count": 1,
            "matching_accepting_listener_count": 1,
            "after_wayland_path_inode_count": 4,
            "after_global_matching_listener_count": 1,
        })

    def test_listener_allows_duplicate_fds_for_one_listening_description(self):
        path = Path("/run/user/1000/wayland-0")
        entries = [mock.Mock(), mock.Mock()]
        entries[0].name, entries[1].name = "7", "8"
        listeners = [mock.Mock(), mock.Mock()]
        for listener in listeners:
            listener.family = probe.socket.AF_UNIX
            listener.getsockname.return_value = str(path)
            listener.getsockopt.side_effect = [probe.socket.SOCK_STREAM, 1]
        evidence = {}
        with mock.patch.object(probe.Path, "stat", return_value=mock.Mock(
                st_mode=0o140777, st_dev=1, st_ino=9)), \
             mock.patch.object(probe, "unix_path_listener", side_effect=[(22, 4), (22, 5)]), \
             mock.patch.object(probe, "authenticate_display_path"), \
             mock.patch.object(probe.Path, "iterdir", return_value=entries), \
             mock.patch.object(probe.os, "readlink", return_value="socket:[22]"), \
             mock.patch.object(probe, "duplicate_fd", side_effect=[51, 52]), \
             mock.patch.object(probe.socket, "socket", side_effect=listeners), \
             mock.patch.object(probe.os, "fstat", side_effect=[
                 mock.Mock(st_ino=22), mock.Mock(st_ino=22)]), \
             mock.patch.object(probe, "same_open_file_description", return_value=True), \
             mock.patch.object(probe.os, "close"):
            probe.verify_listener(964, 32, path, evidence)

    def test_listener_rejects_two_distinct_listening_descriptions(self):
        path = Path("/run/user/1000/wayland-0")
        entries = [mock.Mock(), mock.Mock()]
        entries[0].name, entries[1].name = "7", "8"
        listeners = [mock.Mock(), mock.Mock()]
        for listener in listeners:
            listener.family = probe.socket.AF_UNIX
            listener.getsockname.return_value = str(path)
            listener.getsockopt.side_effect = [probe.socket.SOCK_STREAM, 1]
        with mock.patch.object(probe.Path, "stat", return_value=mock.Mock(
                st_mode=0o140777, st_dev=1, st_ino=9)), \
             mock.patch.object(probe, "unix_path_listener", side_effect=[(22, 4), (22, 5)]), \
             mock.patch.object(probe, "authenticate_display_path"), \
             mock.patch.object(probe.Path, "iterdir", return_value=entries), \
             mock.patch.object(probe.os, "readlink", return_value="socket:[22]"), \
             mock.patch.object(probe, "duplicate_fd", side_effect=[51, 52]), \
             mock.patch.object(probe.socket, "socket", side_effect=listeners), \
             mock.patch.object(probe.os, "fstat", return_value=mock.Mock(st_ino=22)), \
             mock.patch.object(probe, "same_open_file_description", return_value=False), \
             mock.patch.object(probe.os, "close"):
            with self.assertRaisesRegex(RuntimeError, "open description, got 2"):
                probe.verify_listener(964, 32, path)

    def test_listener_rejects_path_rebind_after_compositor_inspection(self):
        path = Path("/run/user/1000/wayland-0")
        entry = mock.Mock(); entry.name = "7"
        listener = mock.Mock(); listener.family = probe.socket.AF_UNIX
        listener.getsockname.return_value = str(path)
        listener.getsockopt.side_effect = [probe.socket.SOCK_STREAM, 1]
        with mock.patch.object(probe.Path, "stat", return_value=mock.Mock(
                st_mode=0o140777, st_dev=1, st_ino=9)), \
             mock.patch.object(probe, "unix_path_listener", side_effect=[(22, 4), (99, 4)]), \
             mock.patch.object(probe, "authenticate_display_path"), \
             mock.patch.object(probe.Path, "iterdir", return_value=[entry]), \
             mock.patch.object(probe.os, "readlink", return_value="socket:[22]"), \
             mock.patch.object(probe, "duplicate_fd", return_value=51), \
             mock.patch.object(probe.socket, "socket", return_value=listener), \
             mock.patch.object(probe.os, "fstat", return_value=mock.Mock(st_ino=22)), \
             mock.patch.object(probe.os, "close"):
            with self.assertRaisesRegex(RuntimeError, "listener changed"):
                probe.verify_listener(102, 32, path)

    def test_listener_retains_second_scan_failure_counts(self):
        path = Path("/run/user/1000/wayland-0")
        entry = mock.Mock(); entry.name = "7"
        listener = mock.Mock(); listener.family = probe.socket.AF_UNIX
        listener.getsockname.return_value = str(path)
        listener.getsockopt.side_effect = [probe.socket.SOCK_STREAM, 1]
        scans = 0
        def listener_scan(_path, record):
            nonlocal scans
            scans += 1
            record["wayland_path_inode_count"] = 5
            record["global_matching_listener_count"] = scans
            if scans == 2:
                raise RuntimeError("expected one global WAYLAND_DISPLAY listening record, got 2")
            return 22, 5
        evidence = {}
        with mock.patch.object(probe.Path, "stat", return_value=mock.Mock(
                st_mode=0o140777, st_dev=1, st_ino=9)), \
             mock.patch.object(probe, "unix_path_listener", side_effect=listener_scan), \
             mock.patch.object(probe, "authenticate_display_path"), \
             mock.patch.object(probe.Path, "iterdir", return_value=[entry]), \
             mock.patch.object(probe.os, "readlink", return_value="socket:[22]"), \
             mock.patch.object(probe, "duplicate_fd", return_value=51), \
             mock.patch.object(probe.socket, "socket", return_value=listener), \
             mock.patch.object(probe.os, "fstat", return_value=mock.Mock(st_ino=22)), \
             mock.patch.object(probe.os, "close"):
            with self.assertRaisesRegex(RuntimeError, "got 2"):
                probe.verify_listener(102, 32, path, evidence)
        self.assertEqual(evidence["after_wayland_path_inode_count"], 5)
        self.assertEqual(evidence["after_global_matching_listener_count"], 2)

    def test_current_display_path_requires_exact_compositor_peer(self):
        client = mock.MagicMock()
        client.__enter__.return_value = client
        client.getsockopt.return_value = probe.struct.pack("3i", 999, 1000, 1000)
        with mock.patch.object(probe.socket, "socket", return_value=client), \
             mock.patch.object(probe.socket, "SO_PEERCRED", 17, create=True):
            with self.assertRaisesRegex(RuntimeError, "wrong peer"):
                probe.authenticate_display_path(Path("/run/user/1000/wayland-0"), 102)

    def test_listener_rejects_partial_compositor_socket_inspection(self):
        path = Path("/run/user/1000/wayland-0")
        entries = [mock.Mock(), mock.Mock()]
        entries[0].name, entries[1].name = "7", "8"
        listener = mock.Mock()
        listener.family = probe.socket.AF_UNIX
        listener.getsockname.return_value = str(path)
        listener.getsockopt.side_effect = [probe.socket.SOCK_STREAM, 1]
        with mock.patch.object(probe.Path, "stat", return_value=mock.Mock(st_mode=0o140777)), \
             mock.patch.object(probe, "unix_path_listener", return_value=(22, 4)), \
             mock.patch.object(probe.Path, "iterdir", return_value=entries), \
             mock.patch.object(probe.os, "readlink", return_value="socket:[22]"), \
             mock.patch.object(probe, "duplicate_fd", side_effect=[51, PermissionError()]), \
             mock.patch.object(probe.socket, "socket", return_value=listener), \
             mock.patch.object(probe.os, "fstat", return_value=mock.Mock(st_ino=22)), \
             mock.patch.object(probe.os, "close"):
            with self.assertRaisesRegex(RuntimeError, "every compositor socket FD"):
                probe.verify_listener(102, 32, path)

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

    def test_snapshot_retains_bounded_assumption_counts_on_failure(self):
        evidence = {}
        with mock.patch.object(probe, "descendants", return_value=[12]), \
             mock.patch.object(probe, "fd_writers", return_value=[(11, 7)]), \
             mock.patch.object(probe, "socket_inventory", return_value=([2], [(9, 2)])), \
             mock.patch.object(probe.os, "pidfd_open", return_value=1, create=True), \
             mock.patch.object(probe, "pidfd_alive", return_value=True), \
             mock.patch.object(probe.os, "close"):
            with self.assertRaisesRegex(RuntimeError, "descendants=1"):
                probe.snapshot(11, 22, Path("/wayland-0"), Path("/trace"), evidence)
        self.assertEqual(evidence["application_descendant_count"], 1)
        self.assertEqual(evidence["trace_writer_count"], 1)

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

    def test_end_snapshot_accepts_only_its_retained_trace_writer(self):
        evidence = {}
        probe_pid = os.getpid()
        with mock.patch.object(probe, "descendants", return_value=[]), \
             mock.patch.object(probe, "fd_writers", return_value=[
                 (11, 7), (probe_pid, 5)]), \
             mock.patch.object(probe, "socket_inventory", return_value=([2], [(9, 2)])), \
             mock.patch.object(probe.os, "pidfd_open", return_value=1, create=True), \
             mock.patch.object(probe, "pidfd_alive", return_value=True), \
             mock.patch.object(probe, "duplicate_fd", return_value=6), \
             mock.patch.object(probe.os, "fstat", side_effect=[
                 mock.Mock(st_dev=8, st_ino=44), mock.Mock(st_ino=123)]), \
             mock.patch.object(probe.Path, "stat", return_value=mock.Mock(
                 st_dev=8, st_ino=44)):
            self.assertEqual(probe.snapshot(
                11, 22, Path("/wayland-0"), Path("/trace"), evidence,
                retained_trace_fd=5),
                (1, [2, 6], 9, 123, 7, 8, 44, 6))
        self.assertEqual(evidence["trace_writer_count"], 2)
        self.assertEqual(evidence["application_trace_writer_count"], 1)
        self.assertEqual(evidence["probe_trace_writer_count"], 1)
        self.assertEqual(evidence["foreign_trace_writer_count"], 0)

    def test_end_snapshot_rejects_unrecognized_second_trace_writer(self):
        evidence = {}
        with mock.patch.object(probe, "descendants", return_value=[]), \
             mock.patch.object(probe, "fd_writers", return_value=[
                 (11, 7), (os.getpid(), 6)]), \
             mock.patch.object(probe, "socket_inventory", return_value=([2], [(9, 2)])), \
             mock.patch.object(probe.os, "pidfd_open", return_value=1, create=True), \
             mock.patch.object(probe, "pidfd_alive", return_value=True), \
             mock.patch.object(probe.os, "close"):
            with self.assertRaisesRegex(RuntimeError, "trace writer count=2"):
                probe.snapshot(
                    11, 22, Path("/wayland-0"), Path("/trace"), evidence,
                    retained_trace_fd=5)
        self.assertEqual(evidence["probe_trace_writer_count"], 0)
        self.assertEqual(evidence["foreign_trace_writer_count"], 1)

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
