#!/usr/bin/python3
# NOT PART OF AUROS. xprobe.py [SECONDS]: wait up to SECONDS for an X window to be managed by the desktop's own X
# window manager, and say so. A tiny raw X11 client (no xprop in the image): it reads _NET_CLIENT_LIST on the root,
# which aurwl's xwm.inc publishes for every X window it manages. Prints "xwindows N <ids>" and exits 0 when N > 0;
# exits 1 after the time with "xwindows 0", 2 when no X server can be reached (no DISPLAY, or Xwayland not started).
import os, socket, struct, sys, time

def rd(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            raise OSError("X closed")
        b += c
    return b

def client_list(path):
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(5)
    s.connect(path)
    s.sendall(struct.pack("<BxHHHHxx", 0x6C, 11, 0, 0, 0))           # no authorisation: Xwayland lets this user in
    h = rd(s, 8)
    body = rd(s, 4 * struct.unpack("<H", h[6:8])[0])
    if h[0] != 1:
        raise OSError("X refused the connection")
    vlen, nroots, nfmts = struct.unpack("<H", body[16:18])[0], body[20], body[21]
    root = struct.unpack("<I", body[32 + ((vlen + 3) & ~3) + 8 * nfmts:][:4])[0]
    s.sendall(struct.pack("<BBHHxx", 16, 1, 2 + 4, 16) + b"_NET_CLIENT_LIST")   # InternAtom only_if_exists (16 letters = 4 words)
    r = rd(s, 32); atom = struct.unpack("<I", r[8:12])[0]
    if not atom:
        return []
    s.sendall(struct.pack("<BBHIIIII", 20, 0, 6, root, atom, 0, 0, 1000))       # GetProperty, any type
    r = rd(s, 32)
    n = struct.unpack("<I", r[16:20])[0] if r[1] == 32 else 0
    data = rd(s, 4 * struct.unpack("<I", r[4:8])[0])
    return list(struct.unpack("<%dI" % n, data[:4 * n]))

def main():
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    disp = os.environ.get("DISPLAY", "")
    if not disp.startswith(":"):
        print("xwindows 0 (no DISPLAY)"); return 2
    path = "/tmp/.X11-unix/X" + disp[1:].split(".")[0]
    end = time.time() + secs
    reached = False
    while True:
        try:
            ids = client_list(path); reached = True
            if ids:
                print("xwindows %d %s" % (len(ids), " ".join("0x%x" % i for i in ids))); return 0
        except (OSError, struct.error):
            pass
        if time.time() >= end:
            print("xwindows 0"); return 1 if reached else 2
        time.sleep(1)

sys.exit(main())
