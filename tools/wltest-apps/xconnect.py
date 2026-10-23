import socket, struct, time, os
print("DISPLAY", os.environ.get("DISPLAY"), flush=True)
t0 = time.time()
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
try:
    s.connect("/tmp/.X11-unix/X0")
    print("connected after %.2fs" % (time.time()-t0), flush=True)
except Exception as e:
    print("connect failed:", e, flush=True)
    raise SystemExit
s.sendall(struct.pack("<BxHHHHxx", 0x6c, 11, 0, 0, 0))
s.settimeout(15)
try:
    d = s.recv(8)
    print("reply after %.2fs:" % (time.time()-t0), d[:8].hex(), "status", d[0] if d else None, flush=True)
except Exception as e:
    print("no reply:", e, flush=True)
