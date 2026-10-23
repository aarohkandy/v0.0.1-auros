#!/usr/bin/env python3
"""run.py -- boot the newest AurOS image under KVM and prove everyday tasks.

    sudo python3 tools/parity/run.py --image out/auros-desktop.img --out parity-out
    sudo python3 tools/parity/run.py --image IMG --checks call,film --without firefox

What it does (docs/handoff/streams/PARITY.md):
  1. lays tools/parity/guest/ into the image's root (in place: no copy) with a drop-in
     that runs the checks INSIDE the desktop's own login session (so polkit and
     the sound server see the same person they would see for her), then boots a
     qcow2 overlay of it, larger than the image so first boot grows the root;
  2. boots it under KVM (OVMF, virtio disk and net, a sound card, a USB tablet);
  3. reads "PARITY ..." lines from the serial port: results, and requests for
     a screenshot / a key press / typing, which it carries out over QMP;
  4. writes results.json, results.md and screenshots/ and exits non-zero when a
     check that is not a known gap failed, or a known gap started passing.

Needs root (loop mount) and qemu-system-x86_64, qemu-utils, OVMF, sgdisk. Standard library only.
"""
import argparse
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OVMF_CODE = os.environ.get("OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd")
OVMF_VARS = os.environ.get("OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd")
W, H = 1280, 800
ALL = ["desktop", "call", "film", "print", "office", "pdfform"]    # apps is a separate, long run

QCODE = {}
for c in "abcdefghijklmnopqrstuvwxyz0123456789":
    QCODE[c] = c
QCODE.update({" ": "spc", "\n": "ret", ".": "dot", ",": "comma", "-": "minus",
              "/": "slash", "=": "equal", "\t": "tab", ";": "semicolon",
              "'": "apostrophe", "[": "bracket_left", "]": "bracket_right",
              "\\": "backslash", "`": "grave_accent"})
SHIFTED = {"_": "-", "+": "=", ":": ";", "?": "/", "!": "1", "@": "2", "#": "3",
           "$": "4", "%": "5", "(": "9", ")": "0", "*": "8", "&": "7", "^": "6",
           '"': "'", "<": ",", ">": "."}


def say(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def sh(cmd, check=True, **kw):
    r = subprocess.run(cmd, shell=isinstance(cmd, str), text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)
    if check and r.returncode:
        raise RuntimeError("%s\n%s" % (cmd, r.stdout))
    return r.stdout


# ── the machine ─────────────────────────────────────────────────────

class Disk:
    """The image's root, mounted read-write only long enough to lay the guest side in.

    The image is never copied (a copy is 4.6 GB and homebase has a disk budget):
    the guest side goes in place, and the machine boots from a qcow2 overlay
    whose backing file is the image, so everything the run writes (first-boot
    growth, flatpak installs, red proofs) is thrown away with the overlay.
    """

    def __init__(self, image, work, grow_gb):
        if image.endswith(".zst"):
            raise SystemExit("--image must be the raw image: zstd -d it first")
        self.base = image
        self.path = os.path.join(work, "overlay.qcow2")
        self.mnt = os.path.join(work, "m")
        self.loop = None
        self.grow_gb = grow_gb
        p = sh(["sgdisk", "-p", self.base], check=False)
        m = re.search(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+\S+ \S+\s+\S+\s+AUROS-ROOT\s*$", p, re.M)
        if not m:
            raise RuntimeError("the image has no AUROS-ROOT partition:\n" + p)
        self.start, self.end = int(m.group(2)), int(m.group(3))

    def mount(self):
        os.makedirs(self.mnt, exist_ok=True)
        self.loop = sh(["losetup", "--find", "--show", "-o", str(self.start * 512),
                        "--sizelimit", str((self.end - self.start + 1) * 512),
                        self.base]).strip()
        sh(["mount", self.loop, self.mnt])
        return self.mnt

    def umount(self):
        if self.loop:
            sh(["sync"], check=False)
            sh(["umount", self.mnt], check=False)
            sh(["losetup", "-d", self.loop], check=False)
            self.loop = None

    def overlay(self):
        """The disk the machine boots: the image underneath, room above it."""
        if os.path.exists(self.path):
            os.remove(self.path)
        size = max(self.grow_gb, 5)
        sh(["qemu-img", "create", "-q", "-f", "qcow2", "-b", os.path.abspath(self.base),
            "-F", "raw", self.path, "%dG" % size])
        return self.path


def inject(mnt, opts):
    """Lay the guest side into the root. NOT PART OF AUROS: only ever the test's own image."""
    dst = os.path.join(mnt, "usr/lib/auros-parity")
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(os.path.join(HERE, "guest"), dst)
    assets = os.path.join(HERE, "assets")
    if os.path.isdir(assets):
        shutil.copytree(assets, os.path.join(dst, "www", "media"))
    if opts.script:
        shutil.copy(opts.script, os.path.join(dst, "checks", "adhoc.sh"))
    for f in ("session.sh", "root.sh", "run.sh"):
        os.chmod(os.path.join(dst, f), 0o755)
    with open(os.path.join(dst, "config"), "w") as f:
        f.write("CHECKS='%s'\nOPTS='%s'\nWITHOUT='%s'\nAPT='%s'\nAPPS='%s'\n" % (
            " ".join(opts.checks), " ".join(opts.opt), " ".join(opts.without), " ".join(opts.apt), " ".join(opts.apps)))
    d = os.path.join(mnt, "etc/systemd/system/aurshell.service.d")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "60-parity.conf"), "w") as f:
        f.write("# NOT PART OF AUROS: tools/parity/run.py puts this into the test's own image.\n"
                "[Service]\n"
                "ExecStartPre=+/usr/lib/auros-parity/root.sh\n"
                "ExecStart=\n"
                "ExecStart=/usr/bin/dbus-run-session -- /usr/lib/auros-parity/session.sh $AURSHELL_SAFE\n")
    # the shell itself is untouched; the test only wraps it (see guest/session.sh)


class Qemu:
    def __init__(self, disk, work, mem, smp, serial, accel):
        self.q = os.path.join(work, "qmp.sock")
        self.serial = serial
        vars_ = os.path.join(work, "vars.fd")
        shutil.copy(OVMF_VARS, vars_)
        argv = ["qemu-system-x86_64", "-machine", "q35,accel=%s" % accel, "-cpu", "max" if accel == "tcg" else "host",
                "-m", str(mem), "-smp", str(smp),
                "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=" + OVMF_CODE,
                "-drive", "if=pflash,format=raw,unit=1,file=" + vars_,
                "-drive", "file=%s,format=qcow2,if=none,id=d0" % disk,
                "-device", "virtio-blk-pci,drive=d0,bootindex=0",
                "-device", "VGA,edid=on,xres=%d,yres=%d" % (W, H), "-display", "none",
                "-device", "qemu-xhci,id=xhci", "-device", "usb-tablet,bus=xhci.0",
                "-device", "usb-kbd,bus=xhci.0",
                "-audiodev", "none,id=snd0", "-device", "intel-hda", "-device", "hda-duplex,audiodev=snd0",
                "-netdev", "user,id=n0", "-device", "virtio-net-pci,netdev=n0",
                "-serial", "file:" + serial, "-qmp", "unix:%s,server=on,wait=off" % self.q]
        open(serial, "w").close()
        self.p = subprocess.Popen(argv, stdout=open(os.path.join(work, "qemu.log"), "w"),
                                  stderr=subprocess.STDOUT)
        self.s = None
        for _ in range(100):
            if os.path.exists(self.q):
                break
            time.sleep(0.1)
        self.s = socket.socket(socket.AF_UNIX)
        self.s.settimeout(30)
        self.s.connect(self.q)
        self.f = self.s.makefile("rw")
        self.f.readline()
        self.cmd("qmp_capabilities")

    def cmd(self, name, **args):
        self.f.write(json.dumps({"execute": name, "arguments": args}) + "\n")
        self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if "return" in r or "error" in r:
                return r

    def alive(self):
        return self.p.poll() is None

    def screendump(self, path):
        r = self.cmd("screendump", filename=path, format="png")
        if "error" in r:                       # an older QEMU: PPM, converted
            ppm = path + ".ppm"
            self.cmd("screendump", filename=ppm)
            ppm_to_png(ppm, path)
            os.remove(ppm)
        return os.path.exists(path)

    def keys(self, *names):
        keys = [{"type": "qcode", "data": n} for n in names]
        self.cmd("send-key", keys=keys, **{"hold-time": 60})

    def type(self, text):
        for ch in text:
            if ch.isupper():
                self.keys("shift", ch.lower())
            elif ch in SHIFTED:
                self.keys("shift", QCODE[SHIFTED[ch]])
            elif ch in QCODE:
                self.keys(QCODE[ch])
            time.sleep(0.12)

    def click(self, x, y, button="left", double=False):
        ax, ay = int(x * 32767 / W), int(y * 32767 / H)
        ev = [{"type": "abs", "data": {"axis": "x", "value": ax}},
              {"type": "abs", "data": {"axis": "y", "value": ay}}]
        self.cmd("input-send-event", events=ev)
        time.sleep(0.15)
        for _ in range(2 if double else 1):
            self.cmd("input-send-event", events=[{"type": "btn", "data": {"down": True, "button": button}}])
            time.sleep(0.06)
            self.cmd("input-send-event", events=[{"type": "btn", "data": {"down": False, "button": button}}])
            time.sleep(0.08)

    def kill(self):
        if self.alive():
            self.p.kill()
            self.p.wait()


def ppm_to_png(src, dst):
    d = open(src, "rb").read()
    parts, i = [], 0
    while len(parts) < 4:
        while d[i:i + 1].isspace():
            i += 1
        j = i
        while not d[j:j + 1].isspace():
            j += 1
        parts.append(d[i:j])
        i = j
    i += 1
    w, h = int(parts[1]), int(parts[2])
    raw = b"".join(b"\0" + d[i + y * w * 3:i + (y + 1) * w * 3] for y in range(h))

    def chunk(t, b):
        c = struct.pack(">I", len(b)) + t + b
        return c + struct.pack(">I", zlib.crc32(t + b) & 0xffffffff)
    open(dst, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                          + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def png_pixels(path):
    """-> (w, h, bytes-per-pixel, rows of bytearray) of an 8-bit RGB/RGBA PNG."""
    d = open(path, "rb").read()
    i, idat, w = 8, b"", 0
    while i < len(d):
        n, t = struct.unpack(">I4s", d[i:i + 8])
        b = d[i + 8:i + 8 + n]
        if t == b"IHDR":
            w, h = struct.unpack(">II", b[:8])
            bpp = {2: 3, 6: 4}[b[9]]
        elif t == b"IDAT":
            idat += b
        i += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp + 1, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * stride], bytearray(raw[y * stride + 1:(y + 1) * stride])
        for x in range(len(line)):
            a = line[x - bpp] if x >= bpp else 0
            b_ = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b_) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b_) // 2) & 255
            elif f == 4:
                p = a + b_ - c
                pa, pb, pc = abs(p - a), abs(p - b_), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b_ if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def colours_in(path, x0, y0, x1, y1):
    """How many distinct (5-bit-per-channel) colours a rectangle of a screenshot has."""
    w, h, bpp, rows = png_pixels(path)
    seen = set()
    for y in range(max(0, y0), min(h, y1), 2):
        r = rows[y]
        for x in range(max(0, x0), min(w, x1), 2):
            seen.add((r[x * bpp] >> 3, r[x * bpp + 1] >> 3, r[x * bpp + 2] >> 3))
    return len(seen)


# ── the run ─────────────────────────────────────────────────────────

def read_known_gaps():
    gaps = {}
    p = os.path.join(HERE, "known-gaps.txt")
    if os.path.exists(p):
        for ln in open(p):
            ln = ln.split("#", 1)[0].strip()
            if ln:
                name, _, why = ln.partition(" ")
                gaps[name] = why.strip()
    return gaps


def parse_line(line):
    m = re.search(r"PARITY (.*)$", line)
    return m.group(1).strip() if m else None


def drive(q, out, timeout, checks):
    """Follow the serial port until the guest says done (or the time is up)."""
    shots = os.path.join(out, "screenshots")
    os.makedirs(shots, exist_ok=True)
    results, notes, seen = {}, [], 0
    t0 = time.time()
    done = False
    buf = b""
    while time.time() - t0 < timeout and q.alive() and not done:
        time.sleep(0.25)
        with open(q.serial, "rb") as f:
            f.seek(seen)
            chunk = f.read()
        seen += len(chunk)
        buf += chunk
        *lines, buf = buf.split(b"\n")
        for raw in lines:
            line = raw.decode("utf-8", "replace").replace("\r", "")
            msg = parse_line(line)
            if not msg:
                continue
            tag, _, rest = msg.partition(" ")
            if tag == "result":
                name, status, *detail = rest.split(" ", 2)
                results[name] = {"status": status, "detail": " ".join(detail)}
                say("%-28s %-5s %s" % (name, status, " ".join(detail)[:120]))
            elif tag == "do":
                act, _, arg = rest.partition(" ")
                if act == "snap":
                    q.screendump(os.path.join(shots, arg + ".png"))
                    say("screenshot", arg)
                elif act == "pixels":        # SHOT RESULT x0 y0 x1 y1 MIN
                    shot, nm, x0, y0, x1, y1, mn = arg.split()
                    try:
                        n = colours_in(os.path.join(shots, shot + ".png"), int(x0), int(y0), int(x1), int(y1))
                    except Exception as e:               # no screenshot: a failure, said plainly
                        n = -1
                    ok = n >= int(mn)
                    results[nm] = {"status": "pass" if ok else "fail", "detail":
                        ("the window shows a picture (%d colours)" % n) if ok else
                        ("the window is blank on screen: %d colour(s) where a page has at least %s" % (n, mn))}
                    say("%-28s %-5s %s" % (nm, results[nm]["status"], results[nm]["detail"]))
                elif act == "key":
                    q.keys(*arg.split("+") if arg != "+" else ["equal"])
                elif act == "type":
                    q.type(arg)
                elif act == "click":
                    x, y = arg.split()[:2]
                    q.click(int(x), int(y), double="double" in arg)
            elif tag == "note":
                notes.append(rest)
                say("note:", rest[:160])
            elif tag == "done":
                done = True
    return results, notes, done, time.time() - t0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True, help="raw image (or .zst)")
    ap.add_argument("--out", default="parity-out")
    ap.add_argument("--checks", default=",".join(ALL))
    ap.add_argument("--without", default="", help="comma list of packages to remove first (red proof)")
    ap.add_argument("--apt", default="", help="comma list of Ubuntu packages to install first (what the image should have carried)")
    ap.add_argument("--apps", default="", help="restrict the apps check to these app ids or short names (comma list)")
    ap.add_argument("--opt", default="", help="comma list of guest options (e.g. libreoffice)")
    ap.add_argument("--mem", type=int, default=4096)
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--disk-gb", type=int, default=16)
    ap.add_argument("--timeout", type=int, default=1500)
    ap.add_argument("--work", default=None)
    ap.add_argument("--script", default="", help="development: a shell script run in the guest as check 'adhoc'")
    ap.add_argument("--expect-fail", default="", help="checks that MUST fail (a red proof run)")
    a = ap.parse_args()
    a.checks = [c for c in a.checks.split(",") if c]
    if a.script and "adhoc" not in a.checks:
        a.checks = ["adhoc"] if a.checks == ALL else a.checks + ["adhoc"]
    a.without = [c for c in a.without.split(",") if c]
    a.opt = [c for c in a.opt.split(",") if c]
    a.apt = [c for c in a.apt.split(",") if c]
    a.apps = [c for c in a.apps.split(",") if c]
    expect_fail = [c for c in a.expect_fail.split(",") if c]
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    work = a.work or tempfile.mkdtemp(prefix="parity.", dir=os.environ.get("TMPDIR", "/tmp"))
    os.makedirs(work, exist_ok=True)
    accel = "kvm" if os.access("/dev/kvm", os.W_OK) else "tcg"
    say("work", work, "accel", accel, "checks", ",".join(a.checks))

    disk = Disk(a.image, work, a.disk_gb)
    q = None
    try:
        disk.mount()
        inject(disk.mnt, a)
        disk.umount()
        disk.overlay()
        serial = os.path.join(out, "serial.txt")
        q = Qemu(disk.path, work, a.mem, a.smp, serial, accel)
        results, notes, done, secs = drive(q, out, a.timeout, a.checks)
        if not done:
            q.screendump(os.path.join(out, "screenshots", "timeout.png"))
    finally:
        if q:
            q.kill()
        disk.umount()
        if not a.work:
            shutil.rmtree(work, ignore_errors=True)

    gaps = read_known_gaps()
    rows, bad = [], 0
    for c in a.checks:
        sub = sorted(k for k in results if k.startswith(c + "."))
        parts = ([c] if c in results else []) + sub
        if not parts:
            r = {"status": "fail", "detail": "the guest never reported"}
        else:
            worst = next((k for k in parts if results[k]["status"] != "pass"), None)
            r = results[worst] if worst else {"status": "pass", "detail": results[parts[0]]["detail"]}
            if worst and worst != c:
                r = {"status": "fail", "detail": "%s: %s" % (worst, r["detail"])}
        st = r["status"]
        shown = st
        if c in expect_fail:
            shown = "red-ok" if st != "pass" else "NOT-RED"
            bad += st == "pass"
        elif c in gaps:
            if st == "pass":
                shown, bad = "PASS-REMOVE-GAP", bad + 1
            else:
                shown = "GAP"
        elif st != "pass":
            bad += 1
        rows.append((c, shown, r["detail"], gaps.get(c, "")))
        for k in sub:
            rows.append(("  " + k, results[k]["status"], results[k]["detail"], ""))
    json.dump({"checks": {n: {"status": s, "detail": d, "gap": g} for n, s, d, g in rows},
               "notes": notes, "guest_done": done, "seconds": int(secs),
               "without": a.without, "opt": a.opt, "apt": a.apt, "failures": bad},
              open(os.path.join(out, "results.json"), "w"), indent=1)
    with open(os.path.join(out, "results.md"), "w") as f:
        f.write("| check | result | detail |\n|---|---|---|\n")
        for n, s, d, g in rows:
            f.write("| %s | %s | %s |\n" % (n, s, (d + (" (known gap: %s)" % g if g else "")).replace("|", "/")))
        f.write("\nguest finished: %s in %d s\n" % (done, secs))
    print(open(os.path.join(out, "results.md")).read())
    sys.exit(1 if bad or not done else 0)


if __name__ == "__main__":
    main()
