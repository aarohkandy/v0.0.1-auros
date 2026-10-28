#!/usr/bin/env python3
"""No installed address may be GitHub's once the owner has a domain (WP-15).

    python3 -I tools/ops/addrtest.py [--strict] [--rootfs DIR] [--initramfs FILE] [--exe FILE]
    python3 -I tools/ops/addrtest.py --selftest

Scans three places an installed PC keeps for good:
  --rootfs     AurOS's own files in the built image: everything the auros*
               packages list in var/lib/dpkg/info/*.list, plus the AurOS
               directories (etc/auros, usr/lib/auros, usr/share/auros,
               etc/apt/sources.list.d/auros.sources, ...). Ubuntu's own
               files name GitHub in their docs and are not ours to judge.
  --initramfs  the staging environment (gzip'd cpio, out/auros-staging.img):
               every byte of it.
  --exe        the installer: ASCII and UTF-16 (the wizard names its website
               as a wide string).
Fails (exit 1) on github.com, github.io, githubusercontent.com or
workers.dev, except what tools/ops/addr-allow.txt lists. Until AUROS_DOMAIN
is set those addresses are the expected ones: it then only reports, unless
--strict. docs/ops/DOMAIN.md.
"""
import gzip, os, re, sys, tempfile

PAT = re.compile(rb"(github\.com|github\.io|githubusercontent\.com|workers\.dev)", re.I)


def wide(s):
    return b"".join(bytes([c]) + b"\x00" for c in s)


# UTF-16LE: every ASCII character followed by a NUL
PAT16 = re.compile(b"(" + b"|".join(wide(w) for w in
                   (b"github.com", b"github.io", b"githubusercontent.com", b"workers.dev")) + b")", re.I)

HERE = os.path.dirname(os.path.abspath(__file__))
OWNED_DIRS = ("etc/auros", "etc/apt/sources.list.d/auros.sources", "etc/apt/preferences.d/auros",
              "etc/skel", "usr/lib/auros", "usr/share/auros", "usr/libexec/auros", "var/lib/auros/debs",
              "etc/systemd/system/auros", "usr/lib/systemd/system/auros")


def allowed():
    """(where-substring, reason) pairs from addr-allow.txt: 'substring # reason'."""
    out = []
    p = os.path.join(HERE, "addr-allow.txt")
    if os.path.exists(p):
        for line in open(p):
            line = line.split("#", 1)
            if line[0].strip():
                out.append(line[0].strip())
    return out


URLCH = re.compile(rb"[A-Za-z0-9./:_~%?&=#@+-]")


def hits_in(data, where, allow):
    found = []
    for wide_ in (False, True):
        step = 2 if wide_ else 1
        src = data[::1]
        for m in (PAT16 if wide_ else PAT).finditer(src):
            # the address it sits in: the run of URL characters around the match
            a, b = m.start(), m.end()
            while a - step >= 0 and URLCH.fullmatch(src[a - step:a - step + 1]) and (not wide_ or src[a - 1:a] == b"\0"):
                a -= step
            while b < len(src) and URLCH.fullmatch(src[b:b + 1]) and (not wide_ or src[b + 1:b + 2] == b"\0"):
                b += step
            token = src[a:b].replace(b"\0", b"").decode("ascii", "replace")
            if not any(x in where or x in token for x in allow):
                found.append(f"{where}: {token}")
    return found


def scan_file(path, where, allow):
    try:
        if os.path.islink(path) or not os.path.isfile(path):
            return []
        with open(path, "rb") as f:
            return hits_in(f.read(), where, allow)
    except OSError:
        return []


def scan_rootfs(root, allow):
    files = set()
    info = os.path.join(root, "var/lib/dpkg/info")
    if os.path.isdir(info):
        for n in os.listdir(info):
            if n.startswith("auros") and n.endswith(".list"):
                for line in open(os.path.join(info, n), errors="replace"):
                    line = line.strip().lstrip("/")
                    if line:
                        files.add(line)
    for d in OWNED_DIRS:
        base = os.path.join(root, d)
        if os.path.isfile(base):
            files.add(d)
        for dp, _, fn in os.walk(base):
            for f in fn:
                files.add(os.path.relpath(os.path.join(dp, f), root))
    found = []
    for rel in sorted(files):
        found += scan_file(os.path.join(root, rel), "rootfs:/" + rel, allow)
    return found, len(files)


def scan_initramfs(path, allow):
    with gzip.open(path, "rb") as f:
        data = f.read()
    return hits_in(data, "initramfs:" + os.path.basename(path), allow), len(data)


def scan(rootfs=None, initramfs=None, exe=None):
    allow = allowed()
    found, notes = [], []
    if rootfs:
        h, n = scan_rootfs(rootfs, allow); found += h; notes.append(f"rootfs {n} AurOS files")
    if initramfs:
        h, n = scan_initramfs(initramfs, allow); found += h; notes.append(f"initramfs {n} bytes")
    if exe:
        found += scan_file(exe, "exe:" + os.path.basename(exe), allow)
        notes.append(f"exe {os.path.getsize(exe)} bytes")
    return found, notes


def selftest():
    """Clean passes; one planted string in each of the three places goes red."""
    import io, stat, struct
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("ok   " if cond else "FAIL ") + what)
        ok = ok and bool(cond)

    def cpio_newc(files):
        out = b""
        for i, (name, body) in enumerate(files):
            nm = name.encode() + b"\0"
            hdr = "070701" + "".join("%08X" % v for v in
                  (i + 1, 0o100644, 0, 0, 1, 0, len(body), 0, 0, 0, 0, len(nm), 0)).encode().decode()
            blob = hdr.encode() + nm
            blob += b"\0" * ((4 - len(blob) % 4) % 4) + body
            blob += b"\0" * ((4 - len(blob) % 4) % 4)
            out += blob
        return out

    def build(d, rootfs_extra=b"", init_extra=b"", exe_extra=b""):
        r = os.path.join(d, "rootfs")
        for p, body in (("etc/auros/version", b"0.1\n"),
                        ("etc/apt/sources.list.d/auros.sources", b"URIs: https://updates.auros.example/apt/\n"),
                        ("usr/lib/auros/answer.sh", b"#!/bin/sh\n" + rootfs_extra),
                        ("usr/share/doc/ubuntu/readme", b"see https://github.com/ubuntu/x\n")):
            os.makedirs(os.path.dirname(os.path.join(r, p)), exist_ok=True)
            open(os.path.join(r, p), "wb").write(body)
        init = os.path.join(d, "auros-staging.img")
        with gzip.open(init, "wb") as g:
            g.write(cpio_newc([("init", b"#!/bin/sh\necho hi\n" + init_extra), ("TRAILER!!!", b"")]))
        exe = os.path.join(d, "AurOS-Setup.exe")
        open(exe, "wb").write(b"MZ\x90\0" + wide(b"get.auros.example/") + b"\0\0" + exe_extra)
        return r, init, exe

    cases = [
        ("clean (a GitHub string in Ubuntu's own docs is not ours)", {}, False),
        ("rootfs: github.com in an AurOS script", {"rootfs_extra": b"curl https://github.com/o/r/x\n"}, True),
        ("rootfs: workers.dev in an AurOS script", {"rootfs_extra": b"u=https://x.workers.dev\n"}, True),
        ("initramfs: github.io in the staging init", {"init_extra": b"echo https://o.github.io/r/help\n"}, True),
        ("exe: github.com as a plain string", {"exe_extra": b"https://github.com/o/r"}, True),
        ("exe: github.io as a wide string (the wizard's kind)", {"exe_extra": wide(b"o.github.io/r/")}, True),
        ("exe: githubusercontent.com", {"exe_extra": b"raw.githubusercontent.com"}, True),
    ]
    for what, kw, red in cases:
        with tempfile.TemporaryDirectory() as d:
            r, i, e = build(d, **kw)
            found, _ = scan(r, i, e)
            check(bool(found) == red, what + (" -> red" if red else " -> green"))
    # a dpkg-listed file outside the AurOS directories is scanned too
    with tempfile.TemporaryDirectory() as d:
        r, i, e = build(d)
        os.makedirs(os.path.join(r, "usr/bin"), exist_ok=True)
        open(os.path.join(r, "usr/bin/aurthing"), "wb").write(b"https://github.com/o/r")
        os.makedirs(os.path.join(r, "var/lib/dpkg/info"), exist_ok=True)
        open(os.path.join(r, "var/lib/dpkg/info/auros-desktop.list"), "w").write("/usr/bin/aurthing\n")
        found, _ = scan(r, i, e)
        check(len(found) == 1, "rootfs: a file the auros-* package list names, outside the AurOS directories")
    return 0 if ok else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    args = dict(zip(argv[::2], argv[1::2])) if "--strict" not in argv else \
        dict(zip([a for a in argv if a != "--strict"][::2], [a for a in argv if a != "--strict"][1::2]))
    unknown = set(args) - {"--rootfs", "--initramfs", "--exe"}
    if unknown or not args:
        print(__doc__); return 2
    found, notes = scan(args.get("--rootfs"), args.get("--initramfs"), args.get("--exe"))
    print("addrtest: scanned " + "; ".join(notes))
    strict = "--strict" in argv or bool(os.environ.get("AUROS_DOMAIN", "").strip())
    for h in found:
        print("  " + h)
    if found and strict:
        print(f"addrtest: FAIL {len(found)} GitHub/workers.dev address(es) where the owner's domain belongs "
              f"(AUROS_DOMAIN={os.environ.get('AUROS_DOMAIN', '') or 'unset, --strict'})")
        return 1
    if found:
        print(f"addrtest: {len(found)} GitHub address(es), expected until AUROS_DOMAIN is set (docs/ops/DOMAIN.md); not failing")
    else:
        print("addrtest: ok, no GitHub or workers.dev address in what an installed PC keeps")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
