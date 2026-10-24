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
EXPECTED_SERVICE_EXE = "/usr/bin/dbus-run-session"
EXPECTED_COMPOSITOR_EXE = "/usr/bin/aurshell"


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
    return parse_process_stat(stat_line)[0]


def parse_process_stat(stat_line: str) -> tuple[int, int]:
    fields = stat_line.rsplit(")", 1)[1].split()
    return int(fields[1]), int(fields[19])


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


def select_compositor(service_main_pid: int, cgroup_pids: set[int],
                      executables: dict[int, str], parents: dict[int, int]) -> int:
    """Select the exact compositor in one authenticated service snapshot."""
    if service_main_pid not in cgroup_pids:
        raise RuntimeError("aurshell.service MainPID is outside its control group")
    if executables.get(service_main_pid) != EXPECTED_SERVICE_EXE:
        raise RuntimeError("aurshell.service MainPID is not the exact session wrapper")
    candidates = sorted(pid for pid in cgroup_pids
                        if executables.get(pid) == EXPECTED_COMPOSITOR_EXE)
    if len(candidates) != 1:
        raise RuntimeError(f"expected one exact aurshell compositor, got {len(candidates)}")
    compositor = candidates[0]
    seen = set()
    current = compositor
    while current != service_main_pid:
        if current in seen:
            raise RuntimeError("compositor ancestry contains a loop")
        seen.add(current)
        if current not in cgroup_pids:
            raise RuntimeError("compositor ancestry left the service control group")
        current = parents.get(current, 0)
        if current <= 0:
            raise RuntimeError("compositor does not descend from service MainPID")
    return compositor


def service_snapshot(control_group: str):
    """Read one exact cgroup, executable, ancestry and start-time snapshot."""
    if not control_group.startswith("/") or control_group == "/" or ".." in control_group.split("/"):
        raise RuntimeError("invalid aurshell.service control group")
    cgroup_root = Path("/sys/fs/cgroup").resolve()
    cgroup_path = (cgroup_root / control_group.lstrip("/")).resolve()
    if cgroup_path.parent != cgroup_root and cgroup_root not in cgroup_path.parents:
        raise RuntimeError("aurshell.service control group escaped cgroup root")
    try:
        cgroup_pids = {int(value) for value in
                       (cgroup_path / "cgroup.procs").read_text(encoding="ascii").splitlines()}
    except (OSError, ValueError) as exc:
        raise RuntimeError("could not read aurshell.service process membership") from exc
    executables = {}
    parents = {}
    starttimes = {}
    for pid in cgroup_pids:
        try:
            executables[pid] = os.readlink(Path("/proc") / str(pid) / "exe")
            parents[pid], starttimes[pid] = parse_process_stat(
                (Path("/proc") / str(pid) / "stat").read_text())
            membership = (Path("/proc") / str(pid) / "cgroup").read_text(encoding="ascii").splitlines()
        except (OSError, ValueError, IndexError) as exc:
            raise RuntimeError("aurshell.service process snapshot changed") from exc
        if f"0::{control_group}" not in membership:
            raise RuntimeError("process disagrees with aurshell.service control group")
    return cgroup_pids, executables, parents, starttimes


def read_service_topology(service_main_pid: int, control_group: str) -> tuple[int, int, int]:
    """Resolve and pin the compositor from two identical service snapshots."""
    first = service_snapshot(control_group)
    cgroup_pids, executables, parents, _starttimes = first
    compositor = select_compositor(service_main_pid, cgroup_pids, executables, parents)
    main_pidfd = os.pidfd_open(service_main_pid, 0)
    compositor_pidfd = -1
    try:
        compositor_pidfd = os.pidfd_open(compositor, 0)
        second = service_snapshot(control_group)
        if second != first:
            raise RuntimeError("aurshell.service topology changed during authentication")
        if not pidfd_alive(main_pidfd) or not pidfd_alive(compositor_pidfd):
            raise RuntimeError("aurshell.service identity exited during authentication")
        if os.readlink(Path("/proc") / str(service_main_pid) / "exe") != EXPECTED_SERVICE_EXE:
            raise RuntimeError("aurshell.service wrapper changed during authentication")
        if os.readlink(Path("/proc") / str(compositor) / "exe") != EXPECTED_COMPOSITOR_EXE:
            raise RuntimeError("aurshell compositor changed during authentication")
        return compositor, main_pidfd, compositor_pidfd
    except Exception:
        if compositor_pidfd >= 0:
            os.close(compositor_pidfd)
        os.close(main_pidfd)
        raise


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


def is_stream_socket(sock: socket.socket, *, listener: bool) -> bool:
    if sock.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE) != socket.SOCK_STREAM:
        return False
    return not listener or sock.getsockopt(socket.SOL_SOCKET, socket.SO_ACCEPTCONN) == 1


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
                    if not is_stream_socket(sock, listener=False):
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


def verify_listener(shell_pid: int, shell_pidfd: int, display_path: Path) -> None:
    expected = display_path.stat()
    if not stat.S_ISSOCK(expected.st_mode):
        raise RuntimeError("WAYLAND_DISPLAY path is not a socket")
    path_inodes = unix_path_inodes(display_path)
    if len(path_inodes) != 1:
        raise RuntimeError(f"expected one WAYLAND_DISPLAY socket inode, got {len(path_inodes)}")
    found = 0
    for entry in (Path("/proc") / str(shell_pid) / "fd").iterdir():
        try:
            if not os.readlink(entry).startswith("socket:["):
                continue
            duplicate = duplicate_fd(shell_pidfd, int(entry.name))
            try:
                sock = socket.socket(fileno=duplicate)
                name = sock.getsockname()
                inode = os.fstat(duplicate).st_ino
                valid_listener = is_stream_socket(sock, listener=True)
                sock.detach()
                if (valid_listener and name == str(display_path) and inode in path_inodes):
                    found += 1
            finally:
                os.close(duplicate)
        except (OSError, ValueError):
            continue
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
    parser.add_argument("--service-main-pid", type=int, required=True)
    parser.add_argument("--service-control-group", required=True)
    parser.add_argument("--display-path", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--ready", type=Path, required=True)
    parser.add_argument("--done", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    held = []
    app_pidfd = -1
    service_pidfd = -1
    compositor_pidfd = -1
    try:
        shell_pid, service_pidfd, compositor_pidfd = read_service_topology(
            args.service_main_pid, args.service_control_group)
        verify_listener(shell_pid, compositor_pidfd, args.display_path)
        (app_pidfd, held, wayland_fd, wayland_inode, trace_fd, trace_dev,
         trace_inode, trace_copy) = snapshot(
            args.app_pid, shell_pid, args.display_path, args.trace)
        args.ready.write_text("ready\n", encoding="ascii")
        deadline = time.monotonic() + 180
        while not args.done.exists():
            if time.monotonic() >= deadline:
                raise RuntimeError("connection interval deadline expired")
            time.sleep(0.1)
        before = (wayland_fd, wayland_inode, trace_fd, trace_dev, trace_inode)
        if not pidfd_alive(app_pidfd):
            raise RuntimeError("application exited during connection interval")
        if not pidfd_alive(service_pidfd) or not pidfd_alive(compositor_pidfd):
            raise RuntimeError("authenticated aurshell.service identity exited during interval")
        (second_shell_pid, second_service_pidfd,
         second_compositor_pidfd) = read_service_topology(
            args.service_main_pid, args.service_control_group)
        try:
            if second_shell_pid != shell_pid:
                raise RuntimeError("aurshell compositor identity changed during interval")
            verify_listener(second_shell_pid, second_compositor_pidfd, args.display_path)
        finally:
            os.close(second_compositor_pidfd)
            os.close(second_service_pidfd)
        second = snapshot(
            args.app_pid, shell_pid, args.display_path, args.trace)
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
            "peer_path=exact_wayland_display\nlistener_inode=exact_wayland_display\n"
            "service_main_pid=exact_dbus_run_session\n"
            "compositor_exe=exact_usr_bin_aurshell\n"
            "compositor_in_service_cgroup=yes\n"
            "compositor_descends_from_service_main_pid=yes\n",
            encoding="ascii")
    except Exception as exc:
        detail = str(exc).replace("\n", " ")[:240]
        args.output.write_text(
            f"status=fail\nreason={type(exc).__name__}: {detail}\n", encoding="ascii")
        raise
    finally:
        for value in held:
            os.close(value)
        if app_pidfd >= 0:
            os.close(app_pidfd)
        if compositor_pidfd >= 0:
            os.close(compositor_pidfd)
        if service_pidfd >= 0:
            os.close(service_pidfd)


if __name__ == "__main__":
    main()
