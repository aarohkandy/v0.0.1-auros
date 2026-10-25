#!/usr/bin/env python3
import os
import tempfile
import unittest
from pathlib import Path
from typing import Optional
from unittest import mock

from tools.fcitx_trace_writers import cgroup_state, count_writable_aliases


class TraceWriterTest(unittest.TestCase):
    def fixture(self, flags: Optional[int]):
        root = Path(self.directory.name)
        trace = root / "trace"
        trace.write_bytes(b"")
        fd = root / "proc/123/fd/4"
        fd.parent.mkdir(parents=True)
        os.link(trace, fd)
        if flags is not None:
            info = root / "proc/123/fdinfo/4"
            info.parent.mkdir(parents=True)
            info.write_text(f"flags:\t{flags:o}\n", encoding="ascii")
        return trace, root / "proc"

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.directory.cleanup()

    def test_counts_writable_matching_descriptor(self):
        trace, proc = self.fixture(os.O_WRONLY)
        self.assertEqual(count_writable_aliases(trace, proc), 1)

    def test_matched_descriptor_with_unreadable_fdinfo_fails_closed(self):
        trace, proc = self.fixture(None)
        with self.assertRaises(OSError):
            count_writable_aliases(trace, proc)

    def test_read_only_matching_descriptor_is_not_a_writer(self):
        trace, proc = self.fixture(os.O_RDONLY)
        self.assertEqual(count_writable_aliases(trace, proc), 0)

    def test_process_inventory_permission_error_fails_closed(self):
        trace = Path(self.directory.name) / "trace"
        trace.write_bytes(b"")
        with mock.patch("tools.fcitx_trace_writers.os.scandir",
                        side_effect=PermissionError("denied")):
            with self.assertRaises(PermissionError):
                count_writable_aliases(trace, Path(self.directory.name) / "proc")

    def test_cgroup_reads_content_in_root_and_descendants(self):
        root = Path(self.directory.name) / "cgroup"
        unit = root / "user.slice/unit.scope"
        child = unit / "child"
        child.mkdir(parents=True)
        (unit / "cgroup.procs").write_bytes(b"")
        (child / "cgroup.procs").write_bytes(b"4321\n")
        # Ordinary files report size from content, unlike cgroupfs. Mock stat()
        # is intentionally irrelevant: the probe must read the bytes.
        with mock.patch.object(Path, "stat", return_value=mock.Mock(st_size=0)):
            self.assertEqual(cgroup_state(root, "/user.slice/unit.scope"), "occupied")
        (child / "cgroup.procs").write_bytes(b"")
        self.assertEqual(cgroup_state(root, "/user.slice/unit.scope"), "empty")

    def test_cgroup_removed_and_read_errors_are_distinct(self):
        root = Path(self.directory.name) / "cgroup"
        root.mkdir()
        self.assertEqual(cgroup_state(root, "/removed.scope"), "removed")
        unit = root / "broken.scope"
        unit.mkdir()
        with self.assertRaises(OSError):
            cgroup_state(root, "/broken.scope")
        for path in ("", "/", "/../escape"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                cgroup_state(root, path)

    def test_cgroup_rejects_target_and_ancestor_symlinks(self):
        root = Path(self.directory.name) / "cgroup"
        real = root / "real/child.scope"
        real.mkdir(parents=True)
        (real / "cgroup.procs").write_bytes(b"")
        (root / "link.scope").symlink_to(real, target_is_directory=True)
        (root / "ancestor").symlink_to(root / "real", target_is_directory=True)
        for path in ("/link.scope", "/ancestor/child.scope"):
            with self.subTest(path=path), self.assertRaises(OSError):
                cgroup_state(root, path)


if __name__ == "__main__":
    unittest.main()
