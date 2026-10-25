#!/usr/bin/env python3
import os
import tempfile
import unittest
from pathlib import Path
from typing import Optional

from tools.fcitx_trace_writers import count_writable_aliases


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


if __name__ == "__main__":
    unittest.main()
