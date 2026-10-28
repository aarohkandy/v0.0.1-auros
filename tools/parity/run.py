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
        if "capfix" in opts.opt:     # a candidate fix, to prove it before anybody ships it
            f.write("CapabilityBoundingSet=~CAP_WAKE_ALARM\n")
    # the shell itself is untouched; the test only wraps it (see guest/session.sh)


class Qemu:
    def __init__(self, disk, work, mem, smp, serial, accel):
        self.q = os.path.join(work, "qmp.sock")
        self.chan = os.path.join(work, "chan")           # shared with the guest over 9p: the host's acknowledgements
        os.makedirs(self.chan, exist_ok=True)
        open(os.path.join(self.chan, "acks"), "w").write("0\n")
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
                "-virtfs", "local,path=%s,mount_tag=parity,security_model=none,id=chan" % self.chan,
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


def hue_buckets(path, x0, y0, x1, y1):
    """(saturated pixels, distinct hue families among them) in a rectangle. A test card or a film is full of
    saturated colours of many hues; the shell's cards and a blank window have almost none (one green at most)."""
    import colorsys
    w, h, bpp, rows = png_pixels(path)
    n, fam = 0, set()
    for y in range(max(0, y0), min(h, y1), 2):
        r = rows[y]
        for x in range(max(0, x0), min(w, x1), 2):
            R, G, B = r[x * bpp], r[x * bpp + 1], r[x * bpp + 2]
            if max(R, G, B) - min(R, G, B) >= 90:
                n += 1
                fam.add(int(colorsys.rgb_to_hsv(R / 255, G / 255, B / 255)[0] * 6))
    return n, len(fam)


def near_count(path, x0, y0, x1, y1, rgb, tol):
    w, h, bpp, rows = png_pixels(path)
    n = 0
    for y in range(max(0, y0), min(h, y1), 2):
        r = rows[y]
        for x in range(max(0, x0), min(w, x1), 2):
            if all(abs(r[x * bpp + i] - rgb[i]) <= tol for i in range(3)):
                n += 1
    return n


def find_boxes(path, x0, y0, x1, y1, rgb, tol, min_px=300):
    """Centres (x, y) of the horizontal bands of a colour in a rectangle, top to bottom: where the fill-in boxes of a
    form really are, whatever the shell's bar and the window frame do to the layout from one build to the next."""
    w, h, bpp, rows = png_pixels(path)
    bands, cur = [], []
    for y in range(max(0, y0), min(h, y1)):
        xs = [x for x in range(max(0, x0), min(w, x1), 2)
              if all(abs(rows[y][x * bpp + i] - rgb[i]) <= tol for i in range(3))]
        if len(xs) * 2 >= 40:
            cur.append((y, xs))
        elif cur:
            bands.append(cur)
            cur = []
    if cur:
        bands.append(cur)
    out = []
    for b in bands:
        n = sum(len(xs) for _, xs in b)
        if n * 2 >= min_px:
            allx = [x for _, xs in b for x in xs]
            out.append((sum(allx) // len(allx), (b[0][0] + b[-1][0]) // 2))
    return out


def changed_pct(a, b, x0, y0, x1, y1):
    """Percent of sampled pixels of a rectangle that differ clearly between two screenshots."""
    wa, ha, ba, ra = png_pixels(a)
    wb, hb, bb, rb = png_pixels(b)
    n = d = 0
    for y in range(max(0, y0), min(ha, hb, y1), 3):
        for x in range(max(0, x0), min(wa, wb, x1), 3):
            n += 1
            if any(abs(ra[y][x * ba + i] - rb[y][x * bb + i]) > 24 for i in range(3)):
                d += 1
    return 100.0 * d / max(n, 1)


# The first-run card ("Does everything work?") covers the whole desktop until she answers. Its layout has
# changed between builds, so: if a button's green is where a design puts "Yes, it all works", press the
# "Let me look first" that design has under it. (pixel of the green button, where to press)
GREEN = (11, 93, 79)


def card_button(path):
    """Where to press to get past the first-run card, or None when no card is on screen. The card's green 'Yes, it all
    works' button is found by colour (its position moves with the shell's bar from build to build); 'Let me look first'
    is 70 px under it in the design with the buttons stacked on the left, 356 px to its right in the first design."""
    bands = find_boxes(path, 90, 150, 450, 740, GREEN, 10, min_px=3000)
    if not bands:
        return None
    cx, cy = bands[0]
    return (cx + 356, cy) if cy > 600 else (cx, cy + 70)


def desktop_up(rows, bpp):
    """The shell is drawing: a dark bar along the bottom and a light, non-black page above it."""
    bar, body = rows[780][640 * bpp:640 * bpp + 3], rows[300][640 * bpp:640 * bpp + 3]
    return sum(bar) < 150 and sum(body) > 300


def welcome(q, shots):
    """Press 'Let me look first' (whichever design) once the card has appeared, and see it gone. Waits up to 90 s:
    the card is drawn some seconds after the desktop starts, and 'no card yet' must not be read as 'no card'."""
    t0, presses, calm = time.time(), 0, 0
    tmp = os.path.join(shots, "welcome-probe.png")
    while time.time() - t0 < 90:
        q.screendump(tmp)
        w, h, bpp, rows = png_pixels(tmp)
        press = card_button(tmp)
        if press:
            q.click(*press)
            presses += 1
            calm = 0
            time.sleep(4)
            continue
        calm = calm + 1 if desktop_up(rows, bpp) else 0
        if calm >= 3 and (presses or time.time() - t0 > 20):
            os.remove(tmp)
            return True, "the first-run card is gone (pressed Let me look first %d time(s))" % presses
        time.sleep(3)
    return False, "the first-run card could not be dismissed in 90 s (pressed %d time(s))" % presses


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


def ack(q, n):
    """Tell the guest that its n-th request has been carried out (the 9p share the guest has mounted)."""
    tmp = os.path.join(q.chan, ".acks")
    open(tmp, "w").write("%d\n" % n)
    os.replace(tmp, os.path.join(q.chan, "acks"))


def drive(q, out, timeout, checks):
    """Follow the serial port until the guest says done (or the time is up)."""
    shots = os.path.join(out, "screenshots")
    os.makedirs(shots, exist_ok=True)
    results, notes, seen, do_n = {}, [], 0, 0
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
                do_n += 1
                if act == "snap":
                    q.screendump(os.path.join(shots, arg + ".png"))
                    say("screenshot", arg)
                elif act == "clickbox":              # SHOT R G B TOL INDEX x0 y0 x1 y1
                    shot, R, G, B, tol, idx, x0, y0, x1, y1 = arg.split()
                    if shot == "now":                  # a fresh picture: the window may have re-fitted since the last one
                        shot_path = os.path.join(shots, "clickbox-probe.png")
                        q.screendump(shot_path)
                    else:
                        shot_path = os.path.join(shots, shot + ".png")
                    boxes = find_boxes(shot_path, int(x0), int(y0), int(x1), int(y1), (int(R), int(G), int(B)), int(tol))
                    if shot == "now" and os.path.exists(shot_path):
                        os.remove(shot_path)
                    if int(idx) < len(boxes):
                        q.click(*boxes[int(idx)])
                        say("clicked box %s at %s" % (idx, boxes[int(idx)]))
                    else:
                        say("no box %s in %s: boxes found %s" % (idx, shot, boxes))
                        notes.append("no box %s in %s: %s" % (idx, shot, boxes))
                elif act == "firstrun":
                    # Firefox 158's first window asks her to accept Terms of Use (purple Continue button) before her
                    # page; the test presses it (a person has to) and records that it was there.
                    tmp = os.path.join(shots, "firstrun-probe.png")
                    shown = None
                    for _ in range(12):                    # up to 36 s: Firefox is slow to draw its first window
                        q.screendump(tmp)
                        found = find_boxes(tmp, 200, 100, 1100, 700, (118, 78, 221), 12, min_px=1200)   # the purple Continue button
                        if found:
                            shown = found[0]
                            break
                        time.sleep(3)
                    if os.path.exists(tmp):
                        os.remove(tmp)
                    if shown:
                        q.click(*shown)
                        time.sleep(3)
                    old = results.get("firefox.firstrun")
                    if shown or not old:
                        results["firefox.firstrun"] = {"status": "fail" if shown else "pass", "detail":
                            "Firefox's first window asks her to accept its Terms of Use and says it sends diagnostic data to Mozilla (telemetry is off by policy) before her page: she must press Continue" if shown
                            else "Firefox shows her page straight away"}
                        say("%-28s %-5s %s" % ("firefox.firstrun", results["firefox.firstrun"]["status"], results["firefox.firstrun"]["detail"][:100]))
                elif act in ("hue", "near", "changed", "welcome"):
                    try:
                        if act == "welcome":
                            ok, det = welcome(q, shots)
                            nm = "desktop.welcome"
                        elif act == "hue":                # SHOT RESULT x0 y0 x1 y1 MINFAMILIES
                            shot, nm, x0, y0, x1, y1, mn = arg.split()
                            n, fam = hue_buckets(os.path.join(shots, shot + ".png"), int(x0), int(y0), int(x1), int(y1))
                            ok = fam >= int(mn) and n >= 4000
                            det = ("the window shows its picture (%d saturated pixels in %d hues)" % (n, fam)) if ok else \
                                  ("nothing like the page is on screen: %d saturated pixels in %d hue(s); a picture has at least %s hues" % (n, fam, mn))
                        elif act == "near":               # SHOT RESULT x0 y0 x1 y1 R G B TOL MINPIXELS
                            shot, nm, x0, y0, x1, y1, R, G, B, tol, mn = arg.split()
                            n = near_count(os.path.join(shots, shot + ".png"), int(x0), int(y0), int(x1), int(y1), (int(R), int(G), int(B)), int(tol))
                            ok = n >= int(mn)
                            det = ("the document is on screen (%d pixels of its box colour)" % n) if ok else \
                                  ("the document is not on screen: %d pixels of its box colour (need %s)" % (n, mn))
                        else:                             # changed: BEFORE AFTER RESULT x0 y0 x1 y1 MINPCT
                            ba, aa, nm, x0, y0, x1, y1, mn = arg.split()
                            pc = changed_pct(os.path.join(shots, ba + ".png"), os.path.join(shots, aa + ".png"), int(x0), int(y0), int(x1), int(y1))
                            ok = pc >= float(mn)
                            det = ("a window appeared (%.0f%% of its area changed)" % pc) if ok else \
                                  ("nothing appeared on screen: only %.1f%% of the area changed (need %s%%)" % (pc, mn))
                    except Exception as e:                    # no screenshot etc.: a failure, said plainly
                        nm, ok, det = (arg.split()[1] if act != "welcome" and len(arg.split()) > 1 else "desktop.welcome"), False, "could not look at the screen: %s" % e
                    results[nm] = {"status": "pass" if ok else "fail", "detail": det}
                    say("%-28s %-5s %s" % (nm, results[nm]["status"], det))
                elif act == "key":
                    q.keys(*arg.split("+") if arg != "+" else ["equal"])
                elif act == "type":
                    q.type(arg)
                elif act == "click":
                    x, y = arg.split()[:2]
                    q.click(int(x), int(y), double="double" in arg)
                ack(q, do_n)                  # the guest waits for this, so it never races the screen
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
    agg = a.checks + [x for x in ("desktop", "firefox") if x not in a.checks and any(k.startswith(x + ".") for k in results)]
    for c in agg:
        sub = sorted(k for k in results if k.startswith(c + "."))
        parts = ([c] if c in results else []) + sub
        # a sub-result listed in known-gaps.txt (e.g. apps.zoom) is a gap of its own: it fails without failing the check,
        # and passing without being removed from the list fails the run
        sub_rows, sub_bad = [], 0
        live = []
        for k in parts:
            if k != c and k in gaps:
                if results[k]["status"] == "pass":
                    sub_rows.append(("  " + k, "PASS-REMOVE-GAP", results[k]["detail"], gaps[k]))
                    sub_bad += 1
                else:
                    sub_rows.append(("  " + k, "GAP", results[k]["detail"], gaps[k]))
            else:
                live.append(k)
                sub_rows.append(("  " + k, results[k]["status"], results[k]["detail"], ""))
        if not parts:
            r = {"status": "fail", "detail": "the guest never reported"}
        else:
            worst = next((k for k in live if results[k]["status"] != "pass"), None)
            r = results[worst] if worst else {"status": "pass", "detail": results[(live or parts)[0]]["detail"]}
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
        bad += sub_bad
        rows.append((c, shown, r["detail"], gaps.get(c, "")))
        rows.extend(sub_rows)
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
