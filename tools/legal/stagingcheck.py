#!/usr/bin/env python3
"""stagingcheck -- are the programs inside the installer's small system all in its NOTICES?

    python3 -I tools/legal/stagingcheck.py --cpio auros-staging.img --info ROOTFS/var/lib/dpkg/info
                                           [--notices docs/legal/licenses/component-copyrights.txt]
    python3 -I tools/legal/stagingcheck.py selftest

`AurOS-Setup.exe` carries a small Linux system (build/staging: a gzip'd cpio). Every Ubuntu program
and library inside it is conveyed with the installer, so its license text and notice must travel
with it (docs/legal/COMPLIANCE.md LIC-3). docs/legal/licenses/component-copyrights.txt lists the
packages whose copyright files NOTICES.txt carries; this opens the real cpio, finds every ELF file
in it, asks the image's dpkg file lists which package owns each, and fails for any package that
is not listed. (Our own `init` and `aurscreen` are owned by no package and are skipped.)
"""
import gzip, io, os, re, sys


def cpio_entries(data):
    """(name, bytes) of a newc cpio, in order."""
    i, out = 0, []
    while i + 110 <= len(data):
        if data[i:i + 6] != b"070701":
            raise SystemExit("stagingcheck: not a newc cpio at byte %d" % i)
        f = [int(data[i + 6 + 8 * k:i + 14 + 8 * k], 16) for k in range(13)]
        size, namesize = f[6], f[11]
        name = data[i + 110:i + 110 + namesize - 1].decode("utf-8", "replace")
        i = (i + 110 + namesize + 3) & ~3
        body = data[i:i + size]
        i = (i + size + 3) & ~3
        if name == "TRAILER!!!":
            break
        out.append((name, body))
    return out


def owners(info_dir):
    """{path without leading slash: [package]} and {basename: [package]} from dpkg's *.list files."""
    by_path, by_base = {}, {}
    for fn in os.listdir(info_dir):
        if not fn.endswith(".list"):
            continue
        pkg = fn[:-5].split(":")[0]
        with open(os.path.join(info_dir, fn), encoding="utf-8", errors="replace") as fh:
            for l in fh:
                p = l.strip().lstrip("/")
                if not p or p.endswith("/"):
                    continue
                by_path.setdefault(p, []).append(pkg)
                by_base.setdefault(os.path.basename(p), []).append(pkg)
    return by_path, by_base


def elf_owner_packages(entries, by_path, by_base):
    pk, unowned = {}, []
    for name, body in entries:
        if body[:4] != b"\x7fELF":
            continue
        n = name.lstrip("./")
        cand = by_path.get(n) or by_path.get("usr/" + n)
        if not cand and n.split("/")[0] in ("sbin", "bin", "lib", "lib64", "usr"):
            cand = by_base.get(os.path.basename(n))   # copied under another directory (sbin/ntfsinfo)
        if cand:
            # a name owned by several packages (the same library twice): any of them is the owner
            pk.setdefault(sorted(cand)[0], []).append(n)
        else:
            unowned.append(n)
    return pk, unowned


def notice_packages(path):
    t = open(path, encoding="utf-8").read()
    return {l.strip() for l in t.split("\n") if re.fullmatch(r"[a-z0-9][a-z0-9+.:-]*(?: \(.*\))?", l.strip()) and not l.startswith(("(", "="))}


def check(cpio_path, info_dir, notices_path):
    raw = open(cpio_path, "rb").read()
    data = gzip.decompress(raw) if raw[:2] == b"\x1f\x8b" else raw
    pk, unowned = elf_owner_packages(cpio_entries(data), *owners(info_dir))
    listed = notice_packages(notices_path)
    # the kernel's package is named in the notices by a label
    bad = []
    for p, files in sorted(pk.items()):
        if p not in listed and not any(l.startswith(p + " ") for l in listed):
            bad.append("%s owns %s inside the installer's small system and is not in NOTICES (docs/legal/licenses/component-copyrights.txt; tools/legal/gather-copyrights.sh)" % (p, ", ".join(files[:3])))
    return bad, pk, unowned


def selftest():
    import tempfile, shutil
    def newc(entries):
        out = b""
        for ino, (name, body) in enumerate(entries, 1):
            nm = name.encode() + b"\0"
            h = b"070701" + b"".join(b"%08x" % v for v in (ino, 0o100644, 0, 0, 1, 0, len(body), 0, 0, 0, 0, len(nm), 0))
            out += h + nm + b"\0" * ((-(110 + len(nm))) % 4) + body + b"\0" * ((-len(body)) % 4)
        nm = b"TRAILER!!!\0"
        out += b"070701" + b"".join(b"%08x" % v for v in (0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(nm), 0)) + nm + b"\0" * ((-(110 + len(nm))) % 4)
        return out
    d = tempfile.mkdtemp(prefix="stagingcheck.")
    bad = 0
    try:
        info = os.path.join(d, "info"); os.makedirs(info)
        open(os.path.join(info, "libc6:amd64.list"), "w").write("/usr/lib/x86_64-linux-gnu/libc.so.6\n/usr/lib\n")
        open(os.path.join(info, "ntfs-3g.list"), "w").write("/usr/bin/ntfsinfo\n")
        open(os.path.join(info, "init.list"), "w").write("/usr/sbin/init\n")   # Ubuntu's own package called init must not claim ours
        open(os.path.join(info, "libnew1.list"), "w").write("/usr/lib/x86_64-linux-gnu/libnew.so.1\n")
        elf = b"\x7fELF" + b"\0" * 20
        cpio = os.path.join(d, "s.img")
        open(cpio, "wb").write(gzip.compress(newc([("init", elf), ("lib/x86_64-linux-gnu/libc.so.6", elf), ("sbin/ntfsinfo", elf),
                                                  ("etc/readme", b"text"), ("lib/x86_64-linux-gnu/libnew.so.1", elf)])))
        notes = os.path.join(d, "n.txt")
        open(notes, "w").write("libc6\nntfs-3g\n")
        b, pk, un = check(cpio, info, notes)
        ok = len(b) == 1 and "libnew1" in b[0] and un == ["init"] and sorted(pk) == ["libc6", "libnew1", "ntfs-3g"]
        print("%s  a program the notices lack is named; our own init and text files are not; a program copied under another name is found" % ("ok  " if ok else "FAIL")); bad += 0 if ok else 1
        open(notes, "w").write("libc6\nntfs-3g\nlibnew1\n")
        b, pk, un = check(cpio, info, notes)
        ok = not b
        print("%s  with every package listed it passes" % ("ok  " if ok else "FAIL")); bad += 0 if ok else 1
    finally:
        shutil.rmtree(d, ignore_errors=True)
    print("\n%s" % ("all passed" if not bad else "%d FAILED" % bad))
    return 1 if bad else 0


def main(argv):
    if argv[:1] == ["selftest"]:
        return selftest()
    a = {argv[i]: argv[i + 1] for i in range(0, len(argv) - 1, 2)}
    if "--cpio" not in a or "--info" not in a:
        print(__doc__); return 2
    bad, pk, unowned = check(a["--cpio"], a["--info"], a.get("--notices", os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "docs/legal/licenses/component-copyrights.txt")))
    print("stagingcheck: %d Ubuntu package(s) own programs in the small system: %s" % (len(pk), ", ".join(sorted(pk))))
    print("stagingcheck: not owned by any package (ours, or built here): %s" % ", ".join(sorted(unowned)))
    for b in bad:
        print("stagingcheck: " + b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
