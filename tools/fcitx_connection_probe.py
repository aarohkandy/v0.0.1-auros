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
import re
import socket
import stat
import struct
import subprocess
import select
import time
from pathlib import Path

SYS_PIDFD_GETFD_X86_64 = 438
SYS_KCMP_X86_64 = 312
KCMP_FILE = 0
EXPECTED_SERVICE_EXE = "/usr/bin/dbus-run-session"
EXPECTED_COMPOSITOR_EXE = "/usr/bin/aurshell"
EXPECTED_SERVICE_CONTROL_GROUP = "/system.slice/aurshell.service"
INVOCATION_RE = re.compile(r"[0-9a-f]{32}")
SESSION_RE = re.compile(r"/user\.slice/user-([0-9]+)\.slice/session-[A-Za-z0-9_.:-]+\.scope")


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
        raise RuntimeError("aurshell.service MainPID is outside its PAM session scope")
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
            raise RuntimeError("compositor ancestry left the PAM session scope")
        current = parents.get(current, 0)
        if current <= 0:
            raise RuntimeError("compositor does not descend from service MainPID")
    return compositor


def cgroup_path(control_group: str) -> Path:
    if not control_group.startswith("/") or control_group == "/" or ".." in control_group.split("/"):
        raise RuntimeError("invalid control group")
    cgroup_root = Path("/sys/fs/cgroup").resolve()
    result = (cgroup_root / control_group.lstrip("/")).resolve()
    if result.parent != cgroup_root and cgroup_root not in result.parents:
        raise RuntimeError("control group escaped cgroup root")
    return result


def process_cgroup(pid: int) -> str:
    try:
        lines = (Path("/proc") / str(pid) / "cgroup").read_text(encoding="ascii").splitlines()
    except OSError as exc:
        raise RuntimeError("could not read process control group") from exc
    unified = [line[3:] for line in lines if line.startswith("0::")]
    if len(unified) != 1:
        raise RuntimeError("process has no unique unified control group")
    return unified[0]


def process_uid(pid: int) -> int:
    try:
        line = next(value for value in (Path("/proc") / str(pid) / "status").read_text().splitlines()
                    if value.startswith("Uid:"))
        values = [int(value) for value in line.split()[1:]]
    except (OSError, StopIteration, ValueError) as exc:
        raise RuntimeError("could not authenticate process uid") from exc
    if len(values) != 4 or len(set(values)) != 1:
        raise RuntimeError("process uid identities disagree")
    return values[0]


def process_invocation_id(pid: int) -> str:
    try:
        values = [value.split(b"=", 1)[1].decode("ascii")
                  for value in (Path("/proc") / str(pid) / "environ").read_bytes().split(b"\0")
                  if value.startswith(b"INVOCATION_ID=")]
    except (OSError, UnicodeDecodeError) as exc:
        raise RuntimeError("could not authenticate service invocation") from exc
    if len(values) != 1 or not INVOCATION_RE.fullmatch(values[0]):
        raise RuntimeError("process has no unique valid service invocation id")
    return values[0]


def systemd_service_properties() -> tuple[int, str, str]:
    try:
        completed = subprocess.run(
            ["systemctl", "show", "aurshell.service", "--no-pager",
             "--property=MainPID", "--property=ControlGroup", "--property=InvocationID"],
            check=True, capture_output=True, text=True, timeout=5,
            env={**os.environ, "LC_ALL": "C"})
    except (OSError, subprocess.SubprocessError) as exc:
        raise RuntimeError("could not authenticate current aurshell.service properties") from exc
    properties = {}
    for line in completed.stdout.splitlines():
        key, separator, value = line.partition("=")
        if not separator or key not in {"MainPID", "ControlGroup", "InvocationID"} or key in properties:
            raise RuntimeError("invalid aurshell.service property response")
        properties[key] = value
    if set(properties) != {"MainPID", "ControlGroup", "InvocationID"}:
        raise RuntimeError("incomplete aurshell.service property response")
    try:
        main_pid = int(properties["MainPID"])
    except ValueError as exc:
        raise RuntimeError("invalid aurshell.service MainPID") from exc
    return main_pid, properties["ControlGroup"], properties["InvocationID"]


def session_snapshot(session_control_group: str, expected_uid: int):
    """Read one exact PAM session cgroup process topology snapshot."""
    match = SESSION_RE.fullmatch(session_control_group)
    if not match or int(match.group(1)) != expected_uid:
        raise RuntimeError("MainPID is not in the expected user PAM session scope")
    path = cgroup_path(session_control_group)
    try:
        cgroup_pids = {int(value) for value in
                       (path / "cgroup.procs").read_text(encoding="ascii").splitlines()}
    except (OSError, ValueError) as exc:
        raise RuntimeError("could not read PAM session process membership") from exc
    executables = {}
    parents = {}
    starttimes = {}
    for pid in cgroup_pids:
        try:
            executables[pid] = os.readlink(Path("/proc") / str(pid) / "exe")
            parents[pid], starttimes[pid] = parse_process_stat(
                (Path("/proc") / str(pid) / "stat").read_text())
        except (OSError, ValueError, IndexError) as exc:
            raise RuntimeError("PAM session process snapshot changed") from exc
        if process_cgroup(pid) != session_control_group:
            raise RuntimeError("process disagrees with PAM session control group")
    return cgroup_pids, executables, parents, starttimes


def read_service_topology(service_main_pid: int, service_control_group: str,
                          expected_uid: int, invocation_id: str) -> tuple[int, int, int, str]:
    """Bind systemd's service identity to its PAM-migrated compositor."""
    # systemd.exec(5), PAMName=: pam_systemd commonly migrates MainPID into
    # its session scope; children belong to that scope while MainPID remains
    # associated with the original service. Authenticate both associations.
    if service_control_group != EXPECTED_SERVICE_CONTROL_GROUP:
        raise RuntimeError("unexpected aurshell.service control group")
    if not cgroup_path(service_control_group).is_dir():
        raise RuntimeError("aurshell.service control group is unavailable")
    if not INVOCATION_RE.fullmatch(invocation_id):
        raise RuntimeError("invalid aurshell.service invocation id")
    expected_properties = (service_main_pid, service_control_group, invocation_id)
    if systemd_service_properties() != expected_properties:
        raise RuntimeError("aurshell.service properties changed before authentication")
    main_pidfd = os.pidfd_open(service_main_pid, 0)
    compositor_pidfd = -1
    try:
        if not pidfd_alive(main_pidfd):
            raise RuntimeError("aurshell.service MainPID exited during authentication")
        if os.readlink(Path("/proc") / str(service_main_pid) / "exe") != EXPECTED_SERVICE_EXE:
            raise RuntimeError("aurshell.service MainPID is not the exact session wrapper")
        if process_uid(service_main_pid) != expected_uid:
            raise RuntimeError("aurshell.service MainPID has the wrong uid")
        if process_invocation_id(service_main_pid) != invocation_id:
            raise RuntimeError("aurshell.service MainPID has the wrong invocation id")
        session_control_group = process_cgroup(service_main_pid)
        first = session_snapshot(session_control_group, expected_uid)
        cgroup_pids, executables, parents, _starttimes = first
        compositor = select_compositor(service_main_pid, cgroup_pids, executables, parents)
        compositor_pidfd = os.pidfd_open(compositor, 0)
        second = session_snapshot(session_control_group, expected_uid)
        if second != first:
            raise RuntimeError("PAM session topology changed during authentication")
        if systemd_service_properties() != expected_properties:
            raise RuntimeError("aurshell.service properties changed during authentication")
        if not pidfd_alive(main_pidfd) or not pidfd_alive(compositor_pidfd):
            raise RuntimeError("aurshell.service identity exited during authentication")
        if os.readlink(Path("/proc") / str(compositor) / "exe") != EXPECTED_COMPOSITOR_EXE:
            raise RuntimeError("aurshell compositor changed during authentication")
        if process_uid(service_main_pid) != expected_uid or process_uid(compositor) != expected_uid:
            raise RuntimeError("aurshell service identity has the wrong uid")
        if (process_invocation_id(service_main_pid) != invocation_id
                or process_invocation_id(compositor) != invocation_id):
            raise RuntimeError("aurshell processes disagree with systemd invocation id")
        return compositor, main_pidfd, compositor_pidfd, session_control_group
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


def socket_inventory(pid: int, pidfd: int, shell_pid: int, display_path: Path,
                     evidence=None):
    if evidence is None:
        evidence = {}
    held = []
    matches = []
    duplicate_errors = []
    evidence["application_socket_fd_count"] = 0
    evidence["application_socket_inspection_error_count"] = 0
    evidence["matching_compositor_connection_count"] = 0
    try:
        try:
            entries = sorted((Path("/proc") / str(pid) / "fd").iterdir(),
                             key=lambda item: int(item.name))
        except (OSError, ValueError) as exc:
            evidence["application_socket_inspection_error_count"] += 1
            raise RuntimeError("application FD inventory unavailable") from exc
        for entry in entries:
            try:
                link = os.readlink(entry)
            except OSError as exc:
                evidence["application_socket_inspection_error_count"] += 1
                raise RuntimeError("application FD inventory changed") from exc
            if not link.startswith("socket:["):
                continue
            evidence["application_socket_fd_count"] += 1
            try:
                duplicate = duplicate_fd(pidfd, int(entry.name))
            except OSError as exc:
                duplicate_errors.append(exc)
                evidence["application_socket_inspection_error_count"] = len(duplicate_errors)
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
                evidence["application_socket_inspection_error_count"] += 1
                raise RuntimeError("application socket inventory incomplete") from exc
            if peer[0] == shell_pid:
                matches.append((int(entry.name), duplicate))
                evidence["matching_compositor_connection_count"] = len(matches)
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


def unix_path_listener(display_path: Path, evidence=None) -> tuple[int, int]:
    if evidence is None:
        evidence = {}
    listeners = []
    path_count = 0
    for line in Path("/proc/net/unix").read_text(encoding="ascii").splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 8 and fields[7] == str(display_path):
            path_count += 1
            try:
                flags = int(fields[3], 16)
                socket_type = int(fields[4], 16)
                state = int(fields[5], 16)
                inode = int(fields[6])
            except ValueError:
                raise RuntimeError("invalid /proc/net/unix WAYLAND_DISPLAY record")
            if (socket_type == socket.SOCK_STREAM and flags & 0x00010000
                    and state == 0x01):
                listeners.append(inode)
    evidence["wayland_path_inode_count"] = path_count
    evidence["global_matching_listener_count"] = len(listeners)
    if len(listeners) != 1:
        raise RuntimeError(
            f"expected one global WAYLAND_DISPLAY listening record, got {len(listeners)}")
    return listeners[0], path_count


def authenticate_display_path(display_path: Path, shell_pid: int) -> None:
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(2)
            client.connect(str(display_path))
            peer = struct.unpack("3i", client.getsockopt(
                socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("3i")))
    except OSError as exc:
        raise RuntimeError("could not connect to current WAYLAND_DISPLAY path") from exc
    if peer[0] != shell_pid:
        raise RuntimeError("current WAYLAND_DISPLAY path has the wrong peer")


def verify_listener(shell_pid: int, shell_pidfd: int, display_path: Path,
                    evidence=None) -> None:
    if evidence is None:
        evidence = {}
    expected = display_path.stat()
    if not stat.S_ISSOCK(expected.st_mode):
        raise RuntimeError("WAYLAND_DISPLAY path is not a socket")
    listener_inode, _path_count = unix_path_listener(display_path, evidence)
    matching_listener_fds = []
    socket_fds = 0
    evidence["compositor_socket_fd_count"] = 0
    evidence["matching_accepting_listener_fd_count"] = 0
    evidence["matching_accepting_listener_open_description_count"] = 0
    evidence["matching_accepting_listener_count"] = 0
    try:
        for entry in (Path("/proc") / str(shell_pid) / "fd").iterdir():
            try:
                link = os.readlink(entry)
            except OSError as exc:
                raise RuntimeError("compositor FD inventory changed") from exc
            if not link.startswith("socket:["):
                continue
            socket_fds += 1
            evidence["compositor_socket_fd_count"] = socket_fds
            duplicate = -1
            keep = False
            try:
                duplicate = duplicate_fd(shell_pidfd, int(entry.name))
                try:
                    sock = socket.socket(fileno=duplicate)
                    try:
                        name = sock.getsockname()
                        inode = os.fstat(duplicate).st_ino
                        valid_listener = (sock.family == socket.AF_UNIX
                                          and is_stream_socket(sock, listener=True))
                    finally:
                        sock.detach()
                    if (valid_listener and name == str(display_path)
                            and inode == listener_inode):
                        matching_listener_fds.append(duplicate)
                        keep = True
                        evidence["matching_accepting_listener_fd_count"] = len(
                            matching_listener_fds)
                finally:
                    if duplicate >= 0 and not keep:
                        os.close(duplicate)
            except (OSError, ValueError) as exc:
                raise RuntimeError("could not inspect every compositor socket FD") from exc
        unique_listener_fds = []
        try:
            for duplicate in matching_listener_fds:
                if not any(same_open_file_description(duplicate, known)
                           for known in unique_listener_fds):
                    unique_listener_fds.append(duplicate)
        except OSError as exc:
            raise RuntimeError("could not compare compositor listener descriptions") from exc
        evidence["matching_accepting_listener_open_description_count"] = len(
            unique_listener_fds)
        evidence["matching_accepting_listener_count"] = len(unique_listener_fds)
        authenticate_display_path(display_path, shell_pid)
        after = display_path.stat()
        if (after.st_dev, after.st_ino) != (expected.st_dev, expected.st_ino):
            raise RuntimeError("WAYLAND_DISPLAY path changed during authentication")
        after_evidence = {}
        try:
            after_listener_inode, _after_count = unix_path_listener(display_path, after_evidence)
        finally:
            evidence.update({f"after_{key}": value for key, value in after_evidence.items()})
        if after_listener_inode != listener_inode:
            raise RuntimeError("WAYLAND_DISPLAY listener changed during authentication")
        if len(unique_listener_fds) != 1:
            raise RuntimeError(
                "expected one exact AurWL listener open description, "
                f"got {len(unique_listener_fds)}")
    finally:
        for duplicate in matching_listener_fds:
            os.close(duplicate)


def snapshot(app_pid: int, shell_pid: int, display_path: Path, trace: Path,
             evidence=None):
    if evidence is None:
        evidence = {}
    pidfd = os.pidfd_open(app_pid, 0)
    held = []
    try:
        if not pidfd_alive(pidfd):
            raise RuntimeError("application exited before identity snapshot")
        children = descendants(app_pid)
        writers = fd_writers(trace)
        evidence["application_descendant_count"] = len(children)
        evidence["trace_writer_count"] = len(writers)
        held, matches = socket_inventory(
            app_pid, pidfd, shell_pid, display_path, evidence)
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
        evidence["trace_sink_identity_exact"] = "yes"
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
    parser.add_argument("--service-invocation-id", required=True)
    parser.add_argument("--expected-uid", type=int, required=True)
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
    observed = {}
    try:
        (shell_pid, service_pidfd, compositor_pidfd,
         session_control_group) = read_service_topology(
            args.service_main_pid, args.service_control_group,
            args.expected_uid, args.service_invocation_id)
        observed.update({
            "service_main_pid": args.service_main_pid,
            "compositor_pid": shell_pid,
            "service_control_group": args.service_control_group,
            "pam_session_control_group": session_control_group,
        })
        start_listener_evidence = {}
        try:
            verify_listener(shell_pid, compositor_pidfd, args.display_path,
                            start_listener_evidence)
        finally:
            observed.update({f"start_{key}": value
                             for key, value in start_listener_evidence.items()})
        start_application_evidence = {}
        try:
            (app_pidfd, held, wayland_fd, wayland_inode, trace_fd, trace_dev,
             trace_inode, trace_copy) = snapshot(
                args.app_pid, shell_pid, args.display_path, args.trace,
                start_application_evidence)
        finally:
            observed.update({f"start_{key}": value
                             for key, value in start_application_evidence.items()})
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
         second_compositor_pidfd, second_session_control_group) = read_service_topology(
            args.service_main_pid, args.service_control_group,
            args.expected_uid, args.service_invocation_id)
        try:
            if (second_shell_pid != shell_pid
                    or second_session_control_group != session_control_group):
                raise RuntimeError("aurshell compositor identity changed during interval")
            end_listener_evidence = {}
            try:
                verify_listener(second_shell_pid, second_compositor_pidfd,
                                args.display_path, end_listener_evidence)
            finally:
                observed.update({f"end_{key}": value
                                 for key, value in end_listener_evidence.items()})
        finally:
            os.close(second_compositor_pidfd)
            os.close(second_service_pidfd)
        end_application_evidence = {}
        try:
            second = snapshot(
                args.app_pid, shell_pid, args.display_path, args.trace,
                end_application_evidence)
        finally:
            observed.update({f"end_{key}": value
                             for key, value in end_application_evidence.items()})
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
            "service_invocation_id=exact_systemd_property_and_environment\n"
            "service_control_group=exact_systemd_property\n"
            "pam_session_cgroup=exact_expected_user_session_scope\n"
            "compositor_in_pam_session_cgroup=yes\n"
            "compositor_descends_from_service_main_pid=yes\n",
            encoding="ascii")
        with args.output.open("a", encoding="ascii") as evidence_file:
            for key, value in sorted(observed.items()):
                evidence_file.write(f"observed_{key}={value}\n")
    except Exception as exc:
        detail = str(exc).replace("\n", " ")[:240]
        lines = [f"status=fail\nreason={type(exc).__name__}: {detail}\n"]
        lines.extend(f"observed_{key}={value}\n" for key, value in sorted(observed.items()))
        args.output.write_text("".join(lines), encoding="ascii")
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
