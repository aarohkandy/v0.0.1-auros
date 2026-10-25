#!/usr/bin/env python3
"""Count writable aliases to one trace file, failing closed on matched fds."""
import argparse
import os
from pathlib import Path


def count_writable_aliases(target_path: Path, proc_root: Path = Path("/proc")) -> int:
    target = os.stat(target_path, follow_symlinks=False)
    count = 0
    for name in proc_root.glob("[0-9]*/fd/[0-9]*"):
        try:
            current = name.stat()
        except OSError:
            continue
        if (current.st_dev, current.st_ino) != (target.st_dev, target.st_ino):
            continue
        # Once an fd matches the protected inode, inability to classify it is
        # evidence failure, never permission to omit a possible writer.
        fdinfo = name.parents[1] / "fdinfo" / name.name
        flags_lines = [line for line in fdinfo.read_text(encoding="ascii").splitlines()
                       if line.startswith("flags:")]
        if len(flags_lines) != 1:
            raise ValueError(f"matched descriptor has ambiguous flags: {name}")
        flags = int(flags_lines[0].split()[1], 8)
        if (flags & os.O_ACCMODE) in (os.O_WRONLY, os.O_RDWR):
            count += 1
    return count


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    parser.add_argument("--proc-root", type=Path, default=Path("/proc"))
    args = parser.parse_args()
    print(count_writable_aliases(args.trace, args.proc_root))


if __name__ == "__main__":
    main()
