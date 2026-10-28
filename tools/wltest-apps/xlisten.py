"""xlisten.py [wm] -- does Xwayland serve a client that connected BEFORE it started?
Runs inside tools/wltest-apps' container under a compositor (wltest), makes the
listening socket the way aurwl does, connects first, then starts Xwayland.
With the argument `wm`, also gives it a -wm connection (that nobody answers)."""
import os, socket, struct, subprocess, sys, time
n = 9
lock = "/tmp/.X%d-lock" % n
path = "/tmp/.X11-unix/X%d" % n
os.makedirs("/tmp/.X11-unix", exist_ok=True)
for f in (lock, path):
    try: os.unlink(f)
    except FileNotFoundError: pass
open(lock, "w").write("%10d\n" % os.getpid())
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.bind(path); s.listen(8)
early = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
early.connect(path)
early.sendall(struct.pack("<BxHHHHxx", 0x6c, 11, 0, 0, 0))
args = ["Xwayland", ":%d" % n, "-rootless", "-shm", "-terminate", "-nolisten", "tcp", "-listenfd", str(s.fileno())]
pass_fds = [s.fileno()]
wm = None
if "wm" in sys.argv[1:] or "wmfirst" in sys.argv[1:] or "wmredirect" in sys.argv[1:]:
    wm, b = socket.socketpair()
    if "wmfirst" in sys.argv[1:]: args[2:2] = ["-wm", str(b.fileno())]
    else: args += ["-wm", str(b.fileno())]
    pass_fds.append(b.fileno())
p = subprocess.Popen(args, pass_fds=pass_fds, stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
if wm and "wmredirect" in sys.argv[1:]:
    # become the window manager the plain way: setup, then take over the root
    wm.sendall(struct.pack("<BxHHHHxx", 0x6c, 11, 0, 0, 0))
    hdr = b""
    while len(hdr) < 8: hdr += wm.recv(8 - len(hdr))
    body = b""
    need = 4 * struct.unpack_from("<H", hdr, 6)[0]
    while len(body) < need: body += wm.recv(need - len(body))
    vendor = struct.unpack_from("<H", body, 16)[0]; fmts = body[21]
    off = 32 + ((vendor + 3) & ~3) + 8 * fmts
    root = struct.unpack_from("<I", body, off)[0]
    wm.sendall(struct.pack("<BBHIII I", 2, 0, 4, root, 0x800, 0x100000 | 0x80000 | 0x400000, 0)[:16])
    print("wm took over root", hex(root), flush=True)
early.settimeout(8)
try:
    d = early.recv(8)
    print("early client got a reply:", d[:1].hex(), flush=True)
except Exception as e:
    print("early client got NO reply:", e, flush=True)
p.terminate()
