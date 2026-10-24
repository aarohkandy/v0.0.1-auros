#!/usr/bin/env python3
"""Bounded, dependency-free X11/XRes inventory for the IBus panel probe.

This is test tooling.  It asks the X server, rather than trusting _NET_WM_PID,
which local process owns each top-level X window, and records mapped state and
root-relative geometry.  It neither changes a window nor selects events.
"""

import argparse
import json
import os
import socket
import struct
import sys


MAX_REPLY = 1024 * 1024
MAX_WINDOWS = 512
LOCAL_CLIENT_PID = 0x02


class X11Error(RuntimeError):
    pass


def _pad4(n):
    return (n + 3) & ~3


def setup_request():
    # byte-order, pad, protocol 11.0, no auth name/data, final pad16.
    return struct.pack("<BBHHHHH", ord("l"), 0, 11, 0, 0, 0, 0)


def parse_setup_root(body):
    """Return the first root XID from a successful setup body."""
    if len(body) < 32:
        raise X11Error("short X11 setup")
    vendor_len = struct.unpack_from("<H", body, 16)[0]
    formats = body[21]
    off = 32 + _pad4(vendor_len) + formats * 8
    if off + 40 > len(body):
        raise X11Error("X11 setup has no complete root")
    return struct.unpack_from("<I", body, off)[0]


def parse_xres_ids(body, count):
    """Parse XRes 1.2 ClientIdValue records into (resource_base, pid)."""
    out = []
    off = 0
    if count > MAX_WINDOWS:
        raise X11Error("too many XRes client-id records")
    for _ in range(count):
        if off + 12 > len(body):
            raise X11Error("short XRes client-id record")
        client, mask, size = struct.unpack_from("<III", body, off)
        off += 12
        padded = _pad4(size)
        if size > 64 or off + padded > len(body):
            raise X11Error("invalid XRes client-id value length")
        if mask & LOCAL_CLIENT_PID and size >= 4:
            out.append((client, struct.unpack_from("<I", body, off)[0]))
        off += padded
    if off != len(body):
        raise X11Error("trailing XRes client-id bytes")
    return out


class XConnection:
    def __init__(self, display):
        number = display.split(":", 1)[-1].split(".", 1)[0]
        if not number.isdigit():
            raise X11Error("DISPLAY must name a local numeric display")
        path = "/tmp/.X11-unix/X" + number
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect(path)
        self.seq = 0
        # AurWL starts Xwayland without -auth on a mode-0700 local socket.
        self.sock.sendall(setup_request())
        head = self._read(8)
        status, reason_len, major, minor, words = struct.unpack("<BBHHH", head)
        body = self._read(words * 4)
        if status != 1:
            reason = body[:reason_len].decode("utf-8", "replace")
            raise X11Error("X11 setup refused: " + reason)
        if major != 11:
            raise X11Error("unexpected X11 protocol version")
        self.root = parse_setup_root(body)

    def close(self):
        self.sock.close()

    def _read(self, n):
        chunks = []
        left = n
        while left:
            data = self.sock.recv(left)
            if not data:
                raise X11Error("X11 connection closed")
            chunks.append(data)
            left -= len(data)
        return b"".join(chunks)

    def request(self, packet):
        if len(packet) % 4:
            raise X11Error("unaligned X11 request")
        self.seq = (self.seq + 1) & 0xFFFF
        want = self.seq
        self.sock.sendall(packet)
        while True:
            head = self._read(32)
            kind = head[0] & 0x7F
            seq = struct.unpack_from("<H", head, 2)[0]
            if kind == 0:
                raise X11Error("X11 error code %d on request %d" % (head[1], want))
            if kind != 1:
                continue
            words = struct.unpack_from("<I", head, 4)[0]
            if words * 4 > MAX_REPLY:
                raise X11Error("oversized X11 reply")
            body = self._read(words * 4)
            if seq != want:
                raise X11Error("out-of-sequence X11 reply")
            return head, body

    def query_extension(self, name):
        raw = name.encode("ascii")
        packet = struct.pack("<BBHHH", 98, 0, (8 + _pad4(len(raw))) // 4,
                             len(raw), 0) + raw + b"\0" * (_pad4(len(raw)) - len(raw))
        head, _ = self.request(packet)
        if not head[1]:
            raise X11Error(name + " extension unavailable")
        return head[8]

    def query_tree(self, window):
        head, body = self.request(struct.pack("<BBHI", 15, 0, 2, window))
        parent = struct.unpack_from("<I", head, 12)[0]
        count = struct.unpack_from("<H", head, 16)[0]
        if count > MAX_WINDOWS or len(body) != count * 4:
            raise X11Error("invalid QueryTree reply")
        return parent, list(struct.unpack("<%dI" % count, body)) if count else []

    def attributes(self, window):
        head, body = self.request(struct.pack("<BBHI", 3, 0, 2, window))
        whole = head + body
        if len(whole) < 44:
            raise X11Error("short GetWindowAttributes reply")
        return {"map_state": whole[26], "override_redirect": bool(whole[27])}

    def geometry(self, window):
        head, _ = self.request(struct.pack("<BBHI", 14, 0, 2, window))
        x, y, width, height, border = struct.unpack_from("<hhHHH", head, 12)
        return x, y, width, height, border

    def absolute(self, window):
        packet = struct.pack("<BBHIIhh", 40, 0, 4, window, self.root, 0, 0)
        head, _ = self.request(packet)
        if not head[1]:
            raise X11Error("window is on another X screen")
        return struct.unpack_from("<hh", head, 12)

    def xres_version(self, opcode):
        head, _ = self.request(struct.pack("<BBHBBH", opcode, 0, 2, 1, 2, 0))
        return struct.unpack_from("<HH", head, 8)

    def xres_pid(self, opcode, window):
        packet = struct.pack("<BBHIII", opcode, 4, 4, 1, window,
                             LOCAL_CLIENT_PID)
        head, body = self.request(packet)
        count = struct.unpack_from("<I", head, 8)[0]
        values = parse_xres_ids(body, count)
        return values[0] if values else (None, None)


def inventory(display, wanted_pid=None):
    x = XConnection(display)
    try:
        opcode = x.query_extension("X-Resource")
        version = x.xres_version(opcode)
        if version < (1, 2):
            raise X11Error("XRes 1.2 required, server has %d.%d" % version)
        _, children = x.query_tree(x.root)
        if len(children) > MAX_WINDOWS:
            raise X11Error("X top-level window inventory exceeds limit")
        records = []
        for window in children:
            attrs = x.attributes(window)
            _, pid = x.xres_pid(opcode, window)
            gx, gy, width, height, border = x.geometry(window)
            ax, ay = x.absolute(window)
            rec = {
                "xid": "0x%08x" % window,
                "parent": "0x%08x" % x.root,
                "pid": pid,
                "mapped": attrs["map_state"] == 2,
                "map_state": attrs["map_state"],
                "override_redirect": attrs["override_redirect"],
                "absolute": {"x": ax, "y": ay, "width": width,
                             "height": height, "border": border},
                "parent_relative": {"x": gx, "y": gy},
            }
            if wanted_pid is None or pid == wanted_pid:
                records.append(rec)
        return {"display": display, "root": "0x%08x" % x.root,
                "xres_version": "%d.%d" % version,
                "wanted_pid": wanted_pid, "windows": records}
    finally:
        x.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--pid", type=int)
    args = parser.parse_args()
    if not args.display:
        parser.error("--display or DISPLAY is required")
    try:
        print(json.dumps(inventory(args.display, args.pid), sort_keys=True,
                         indent=2))
    except (OSError, X11Error) as exc:
        print("x11-xres-probe: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
