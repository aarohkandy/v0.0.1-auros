#!/usr/bin/env python3
"""Hold and verify one Mousepad-to-AurWL connection for a test interval.

Injected into profiletest scratch images only. It emits bounded identities and
counts, never protocol contents or typed text.
"""
import argparse
import ctypes
import errno
import os
import platform
import socket
import stat
import struct
import select
import time
from pathlib import Path

SYS_PIDFD_GETFD_X86_64 = 438
SYS_KCMP_X86_64 = 312
KCMP_FILE = 0


def descendants_from_parents(pid: int, parents: dict[int, int]) -> list[int]:
    """Return every transitive child from one consistent parent snapshot."""
    found, frontier = set(), {pid}
    while frontier:
        children = {child for child, parent in parents.items()
                    if parent in frontier and child not in found}
        found.update(children)
        frontier = children
    return sorted(found)


def parse_ppid(stat_line: str) -> int:
    return int(stat_line.rsplit(")", 1)[1].split()[1])


def descendants(pid: int) -> list[int]:
    parents = {}
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            # comm is parenthesized and may itself contain spaces.
            parents[int(entry.name)] = parse_ppid((entry / "stat").read_text())
        except (OSError, ValueError, IndexError):
            continue
    return descendants_from_parents(pid, parents)


def fd_writers(path: Path) -> list[tuple[int, int]]:
    wanted = path.stat()
    result = []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            entries = list((proc / "fd").iterdir())
        except OSError:
            continue
        for entry in entries:
            try:
                current = entry.stat()
                flags_line = next(line for line in (proc / "fdinfo" / entry.name).read_text().splitlines()
                                  if line.startswith("flags:"))
                flags = int(flags_line.split()[1], 8)
            except OSError:
                continue
            except (StopIteration, ValueError):
                continue
            if (current.st_dev, current.st_ino) == (wanted.st_dev, wanted.st_ino):
                if fd_flags_writable(flags):
                    result.append((int(proc.name), int(entry.name)))
    return sorted(result)


def fd_flags_writable(flags: int) -> bool:
    return (flags & os.O_ACCMODE) in (os.O_WRONLY, os.O_RDWR)


def duplicate_fd(pidfd: int, target_fd: int) -> int:
    if platform.machine() != "x86_64":
        raise OSError(errno.ENOSYS, "pidfd_getfd diagnostic supports x86_64 only")
    libc = ctypes.CDLL(None, use_errno=True)
    result = libc.syscall(SYS_PIDFD_GETFD_X86_64, pidfd, target_fd, 0)
    if result < 0:
        value = ctypes.get_errno()
        raise OSError(value, os.strerror(value))
    return result


def same_open_file_description(first_fd: int, second_fd: int) -> bool:
    if platform.machine() != "x86_64":
        raise OSError(errno.ENOSYS, "kcmp diagnostic supports x86_64 only")
    libc = ctypes.CDLL(None, use_errno=True)
    pid = os.getpid()
    result = libc.syscall(SYS_KCMP_X86_64, pid, pid, KCMP_FILE, first_fd, second_fd)
    if result < 0:
        value = ctypes.get_errno()
        raise OSError(value, os.strerror(value))
    return result == 0


def socket_inventory(pid: int, pidfd: int, shell_pid: int, display_path: Path):
    held = []
    matches = []
    duplicate_errors = []
    try:
        entries = sorted((Path("/proc") / str(pid) / "fd").iterdir(),
                         key=lambda item: int(item.name))
        for entry in entries:
            try:
                link = os.readlink(entry)
            except OSError as exc:
                raise RuntimeError("application FD inventory changed") from exc
            if not link.startswith("socket:["):
                continue
            try:
                duplicate = duplicate_fd(pidfd, int(entry.name))
            except OSError as exc:
                duplicate_errors.append(exc)
                continue
            held.append(duplicate)
            try:
                sock = socket.socket(fileno=duplicate)
                try:
                    if sock.family != socket.AF_UNIX:
                        continue
                    try:
                        peer_name = sock.getpeername()
                    except OSError as exc:
                        if exc.errno in (errno.ENOTCONN, errno.EINVAL):
                            continue
                        raise
                    if peer_name != str(display_path):
                        continue
                    peer = struct.unpack("3i", sock.getsockopt(
                        socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("3i")))
                finally:
                    sock.detach()
            except (OSError, ValueError) as exc:
                raise RuntimeError("application socket inventory incomplete") from exc
            if peer[0] == shell_pid:
                matches.append((int(entry.name), duplicate))
        if duplicate_errors:
            raise RuntimeError("pidfd_getfd could not inspect every application socket")
        return held, matches
    except Exception:
        for value in held:
            os.close(value)
        raise

def close_snapshot(pidfd: int, held: list[int]) -> None:
    for value in held:
        os.close(value)
    os.close(pidfd)


def _close_failed_snapshot(pidfd: int, held: list[int]) -> None:
    try:
        for value in held:
            os.close(value)
    finally:
        os.close(pidfd)


def unix_path_inodes(display_path: Path) -> set[int]:
    result = set()
    for line in Path("/proc/net/unix").read_text(encoding="ascii").splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 8 and fields[7] == str(display_path):
            try:
                result.add(int(fields[6]))
            except ValueError:
                raise RuntimeError("invalid /proc/net/unix inode")
    return result


def verify_listener(shell_pid: int, display_path: Path) -> None:
    expected = display_path.stat()
    if not stat.S_ISSOCK(expected.st_mode):
        raise RuntimeError("WAYLAND_DISPLAY path is not a socket")
    pidfd = os.pidfd_open(shell_pid, 0)
    path_inodes = unix_path_inodes(display_path)
    if len(path_inodes) != 1:
        raise RuntimeError(f"expected one WAYLAND_DISPLAY socket inode, got {len(path_inodes)}")
    found = 0
    try:
        for entry in (Path("/proc") / str(shell_pid) / "fd").iterdir():
            try:
                if not os.readlink(entry).startswith("socket:["):
                    continue
                duplicate = duplicate_fd(pidfd, int(entry.name))
                try:
                    sock = socket.socket(fileno=duplicate)
                    name = sock.getsockname()
                    inode = os.fstat(duplicate).st_ino
                    sock.detach()
                    if name == str(display_path) and inode in path_inodes:
                        found += 1
                finally:
                    os.close(duplicate)
            except (OSError, ValueError):
                continue
    finally:
        os.close(pidfd)
    if found != 1:
        raise RuntimeError(f"expected one exact AurWL listener, got {found}")


def snapshot(app_pid: int, shell_pid: int, display_path: Path, trace: Path):
    pidfd = os.pidfd_open(app_pid, 0)
    held = []
    try:
        if not pidfd_alive(pidfd):
            raise RuntimeError("application exited before identity snapshot")
        children = descendants(app_pid)
        writers = fd_writers(trace)
        held, matches = socket_inventory(app_pid, pidfd, shell_pid, display_path)
        if children:
            raise RuntimeError(f"application descendants={len(children)}")
        if len(writers) != 1 or writers[0][0] != app_pid:
            raise RuntimeError(f"trace writer count={len(writers)}")
        if len(matches) != 1:
            raise RuntimeError(f"compositor socket count={len(matches)}")
        trace_copy = duplicate_fd(pidfd, writers[0][1])
        held.append(trace_copy)
        trace_stat = os.fstat(trace_copy)
        wanted = trace.stat()
        if (trace_stat.st_dev, trace_stat.st_ino) != (wanted.st_dev, wanted.st_ino):
            raise RuntimeError("trace writer changed during inspection")
        return (pidfd, held, matches[0][0], os.fstat(matches[0][1]).st_ino,
                writers[0][1], trace_stat.st_dev, trace_stat.st_ino, trace_copy)
    except Exception:
        _close_failed_snapshot(pidfd, held)
        raise


def pidfd_alive(pidfd: int) -> bool:
    poller = select.poll()
    poller.register(pidfd, select.POLLIN)
    return not poller.poll(0)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app-pid", type=int, required=True)
    parser.add_argument("--shell-pid", type=int, required=True)
    parser.add_argument("--display-path", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--ready", type=Path, required=True)
    parser.add_argument("--done", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    held = []
    app_pidfd = -1
    try:
        verify_listener(args.shell_pid, args.display_path)
        (app_pidfd, held, wayland_fd, wayland_inode, trace_fd, trace_dev,
         trace_inode, trace_copy) = snapshot(
            args.app_pid, args.shell_pid, args.display_path, args.trace)
        args.ready.write_text("ready\n", encoding="ascii")
        deadline = time.monotonic() + 180
        while not args.done.exists():
            if time.monotonic() >= deadline:
                raise RuntimeError("connection interval deadline expired")
            time.sleep(0.1)
        before = (wayland_fd, wayland_inode, trace_fd, trace_dev, trace_inode)
        if not pidfd_alive(app_pidfd):
            raise RuntimeError("application exited during connection interval")
        second = snapshot(
            args.app_pid, args.shell_pid, args.display_path, args.trace)
        (second_pidfd, second_held, second_wayland_fd, second_wayland_inode,
         second_trace_fd, second_trace_dev, second_trace_inode,
         second_trace_copy) = second
        try:
            stable = (pidfd_alive(app_pidfd) and same_open_file_description(
                trace_copy, second_trace_copy) and before == (
                    second_wayland_fd, second_wayland_inode, second_trace_fd,
                    second_trace_dev, second_trace_inode))
        finally:
            close_snapshot(second_pidfd, second_held)
        if not stable:
            raise RuntimeError("connection or trace sink changed during interval")
        args.output.write_text(
            "status=pass\nchildren_at_boundaries=0\ncompositor_connections_at_boundaries=1\n"
            "trace_writers_at_boundaries=1\npeer_pid=exact_aurshell\n"
            "peer_path=exact_wayland_display\nlistener_inode=exact_wayland_display\n",
            encoding="ascii")
    except Exception as exc:
        args.output.write_text(f"status=fail\nreason={type(exc).__name__}\n", encoding="ascii")
        raise
    finally:
        for value in held:
            os.close(value)
        if app_pidfd >= 0:
            os.close(app_pidfd)


if __name__ == "__main__":
    main()
