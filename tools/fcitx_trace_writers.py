#!/usr/bin/env python3
"""Count writable aliases to one trace file, failing closed on matched fds."""
import argparse
import errno
import os
from pathlib import Path
from pathlib import PurePosixPath


def count_writable_aliases(target_path: Path, proc_root: Path = Path("/proc")) -> int:
    target = os.stat(target_path, follow_symlinks=False)
    count = 0
    inspected_fds = 0
    processes = 0
    with os.scandir(proc_root) as process_entries:
        process_dirs = []
        for entry in process_entries:
            if not entry.name.isdigit():
                continue
            processes += 1
            if processes > 65536:
                raise ValueError("process inventory exceeds 65536 entries")
            process_dirs.append(Path(entry.path))
    for process_dir in process_dirs:
        try:
            fd_entries = os.scandir(process_dir / "fd")
        except OSError as exc:
            if exc.errno in (errno.ENOENT, errno.ESRCH):
                continue
            raise
        with fd_entries:
            for entry in fd_entries:
                if not entry.name.isdigit():
                    continue
                inspected_fds += 1
                if inspected_fds > 1048576:
                    raise ValueError("descriptor inventory exceeds 1048576 entries")
                name = Path(entry.path)
                try:
                    current = name.stat()
                except OSError as exc:
                    if exc.errno in (errno.ENOENT, errno.ESRCH):
                        continue
                    raise
                if (current.st_dev, current.st_ino) != (target.st_dev, target.st_ino):
                    continue
                # Once an fd matches the protected inode, inability to classify it is
                # evidence failure, never permission to omit a possible writer.
                fdinfo = process_dir / "fdinfo" / entry.name
                flags_lines = [line for line in fdinfo.read_text(encoding="ascii").splitlines()
                               if line.startswith("flags:")]
                if len(flags_lines) != 1:
                    raise ValueError(f"matched descriptor has ambiguous flags: {name}")
                flags = int(flags_lines[0].split()[1], 8)
                if (flags & os.O_ACCMODE) in (os.O_WRONLY, os.O_RDWR):
                    count += 1
    return count


def cgroup_state(cgroup_root: Path, control_group: str) -> str:
    """Return empty/removed after bounded reads of this cgroup and descendants."""
    path = PurePosixPath(control_group)
    if not control_group.startswith("/") or ".." in path.parts or path == PurePosixPath("/"):
        raise ValueError("invalid unit control group")
    directory_flags = os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0)
    root_fd = os.open(cgroup_root, directory_flags)
    target_fd = root_fd
    try:
        try:
            for component in path.parts[1:]:
                next_fd = os.open(component, directory_flags, dir_fd=target_fd)
                if target_fd != root_fd:
                    os.close(target_fd)
                target_fd = next_fd
        except FileNotFoundError:
            if target_fd != root_fd:
                os.close(target_fd)
            return "removed"
        except BaseException:
            if target_fd != root_fd:
                os.close(target_fd)
            raise
    finally:
        os.close(root_fd)
    pending = [target_fd]
    seen = 0
    try:
        while pending:
            current_fd = pending.pop()
            try:
                seen += 1
                if seen > 256:
                    raise ValueError("unit cgroup tree exceeds 256 nodes")
                with os.scandir(current_fd) as entries:
                    for entry in entries:
                        if entry.is_symlink():
                            raise ValueError("unit cgroup contains a symlink")
                        if entry.is_dir(follow_symlinks=False):
                            pending.append(os.open(entry.name, directory_flags, dir_fd=current_fd))
                proc_fd = os.open("cgroup.procs", os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0),
                                  dir_fd=current_fd)
                try:
                    data = os.read(proc_fd, 65537)
                finally:
                    os.close(proc_fd)
                if len(data) > 65536:
                    raise ValueError("unit cgroup.procs exceeds 65536 bytes")
                tokens = data.split()
                if any(not token.isdigit() for token in tokens):
                    raise ValueError("unit cgroup.procs is malformed")
                if tokens:
                    return "occupied"
            finally:
                os.close(current_fd)
        return "empty"
    finally:
        for descriptor in pending:
            os.close(descriptor)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path, nargs="?")
    parser.add_argument("--proc-root", type=Path, default=Path("/proc"))
    parser.add_argument("--cgroup-root", type=Path)
    parser.add_argument("--control-group")
    args = parser.parse_args()
    if args.cgroup_root is not None or args.control_group is not None:
        if args.trace is not None or args.cgroup_root is None or args.control_group is None:
            parser.error("cgroup mode requires --cgroup-root and --control-group only")
        print(cgroup_state(args.cgroup_root, args.control_group))
    elif args.trace is not None:
        print(count_writable_aliases(args.trace, args.proc_root))
    else:
        parser.error("trace is required outside cgroup mode")


if __name__ == "__main__":
    main()
