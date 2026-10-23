#!/usr/bin/env python3
"""sourcestest -- build/sources, against a pretend Launchpad on 127.0.0.1.

    python3 -I tools/legal/sourcestest.py

No network, no Ubuntu needed. Each "refuses" case breaks one thing the
tool exists to catch and requires it to fail AND to name the thing: a
check that cannot fail is not a check (FIELD-MANUAL section 6).
"""
import hashlib, http.server, io, os, shutil, subprocess, sys, tarfile, tempfile, threading

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.environ.get("SOURCES_TOOL") or os.path.join(ROOT, "build", "sources")
ENV = dict(os.environ, SOURCES_SLEEP="0", PYTHONDONTWRITEBYTECODE="1")
FAILED = []


def check(ok, what):
    print("%s  %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        FAILED.append(what)


def sha(b):
    return hashlib.sha256(b).hexdigest()


class Archive:
    """An in-memory Launchpad: /lp/<source>/<version>/<file>."""
    def __init__(self):
        self.files = {}
        outer = self
        class H(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a): pass
            def do_GET(self):
                b = outer.files.get(self.path.replace("%3A", ":"))
                if b is None:
                    self.send_response(404); self.end_headers(); return
                self.send_response(200)
                self.send_header("Content-Length", str(len(b)))
                self.end_headers()
                self.wfile.write(b)
            do_HEAD = do_GET
        class Quiet(http.server.ThreadingHTTPServer):
            def handle_error(self, request, client_address):
                pass   # a client that hangs up after a 404 is not news
        self.srv = Quiet(("127.0.0.1", 0), H)
        self.url = "http://127.0.0.1:%d/lp" % self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def add(self, src, ver, extra=0, signed=False, missing=None, corrupt=None, orig=None):
        up = ver.split(":")[-1]
        base = up.rsplit("-", 1)[0]
        parts = {"%s_%s.orig.tar.xz" % (src, base): orig if orig is not None else os.urandom(2000 + extra),
                 "%s_%s.debian.tar.xz" % (src, up): os.urandom(500)}
        lines = ["Format: 3.0 (quilt)", "Source: " + src, "Binary: x", "Architecture: any", "Version: " + ver,
                 "Checksums-Sha256:"]
        lines += [" %s %d %s" % (sha(b), len(b), n) for n, b in parts.items()]
        lines += ["Files:"] + [" %s %d %s" % (hashlib.md5(b).hexdigest(), len(b), n) for n, b in parts.items()]
        dsc = "\n".join(lines) + "\n"
        if signed:
            dsc = "-----BEGIN PGP SIGNED MESSAGE-----\nHash: SHA512\n\n" + dsc.replace("Binary: x", "- -Binary: x") + \
                  "\n-----BEGIN PGP SIGNATURE-----\n\nabc\n-----END PGP SIGNATURE-----\n"
        d = "/lp/%s/%s/" % (src, ver)
        self.files[d + "%s_%s.dsc" % (src, up)] = dsc.encode()
        for n, b in parts.items():
            if n == missing:
                continue
            self.files[d + n] = (b[:-1] + bytes([b[-1] ^ 1])) if n == corrupt else b
        return list(parts)


def status(pkgs):
    """pkgs: (name, version, source-header-or-None, maintainer, state)"""
    out = []
    for name, ver, src, maint, state, *bu in pkgs:
        s = "Package: %s\nStatus: %s\nVersion: %s\nArchitecture: amd64\nMaintainer: %s\n" % (name, state, ver, maint)
        if src:
            s += "Source: %s\n" % src
        if bu:
            s += "Built-Using: %s\n" % bu[0]
        s += "Description: x\n long\n continuation\n"
        out.append(s)
    return "\n".join(out)


def root_dir(tmp, pkgs, name="root", firefox_ini=None):
    r = os.path.join(tmp, name)
    os.makedirs(r + "/var/lib/dpkg")
    open(r + "/var/lib/dpkg/status", "w").write(status(pkgs))
    if firefox_ini:
        os.makedirs(r + "/opt/firefox")
        open(r + "/opt/firefox/application.ini", "w").write("[App]\nName=Firefox\nVersion=%s\n" % firefox_ini)
    return r


def run(*args):
    p = subprocess.run([sys.executable, "-I", TOOL] + list(args), capture_output=True, text=True, env=ENV, cwd=ROOT)
    return p.returncode, p.stdout + p.stderr


def rewrite_part(path, drop=None, flip=False):
    """The same tar without one member (or with one byte changed)."""
    out = io.BytesIO()
    with tarfile.open(path) as src, tarfile.open(fileobj=out, mode="w") as dst:
        for m in src:
            data = src.extractfile(m).read()
            if drop and m.name.endswith(drop):
                continue
            if flip and m.name.endswith(".orig.tar.xz"):
                data = data[:-1] + bytes([data[-1] ^ 1]); flip = False
            ti = tarfile.TarInfo(m.name); ti.size = len(data)
            dst.addfile(ti, io.BytesIO(data))
    open(path, "wb").write(out.getvalue())


def resum(d):
    names = sorted(f for f in os.listdir(d) if f != "SHA256SUMS")
    open(os.path.join(d, "SHA256SUMS"), "w").write(
        "".join("%s  %s\n" % (sha(open(os.path.join(d, n), "rb").read()), n) for n in names))


# Each line: what the tool would stop checking. The suite must go red on every one.
MUTANTS = [
    ("a missing source file is no longer noticed", 'no("%s %s: %s is in no part" % (s, sv, fn))', 'pass'),
    ("a file that does not hash to its .dsc is accepted", 'if want and (n, h.hexdigest()) != want:', 'if False:'),
    ("a binary with no source row passes", 'no("%s %s has no source: %s %s is not in SOURCES.txt" % (b, v, s, sv)); continue', 'continue'),
    ("SHA256SUMS is not compared", 'elif sha256_file(p) != h:', 'elif False:'),
    ("PACKAGES.txt need not be the image's list", 'if want != have:', 'if False:'),
    ("a removed package is shipped as if installed", 'if len(st) != 3 or st[2] in ("not-installed", "config-files"):', 'if len(st) != 3:'),
    ("a source package may be split between parts", 'if p["bytes"] > part_bytes:', 'if False:'),
    ("Built-Using is ignored", 'for s, v in p.get("built_using", []):', 'for s, v in []:'),
    ("a package that is unpacked but not configured is dropped", 'if len(st) != 3 or st[2] in ("not-installed", "config-files"):', 'if st[2:] != ["installed"]:'),
    ("a missing Firefox mirror is not noticed", 'if ptr[0][6] not in listed:', 'if False:'),
    ("a name that walks out of the work directory is accepted", 'if not NAME_RE.fullmatch(src) or not VER_RE.fullmatch(ver):', 'if False:'),
]


def mutants():
    bad = 0
    for what, old, new in MUTANTS:
        src = open(os.path.join(ROOT, "build", "sources")).read()
        if old not in src:
            print("FAIL  mutant text not found: %s" % what); bad += 1; continue
        d = tempfile.mkdtemp(prefix="sourcesmutant.")
        try:
            t = os.path.join(d, "sources"); open(t, "w").write(src.replace(old, new, 1))
            p = subprocess.run([sys.executable, "-I", os.path.abspath(__file__)], capture_output=True, text=True,
                               env=dict(ENV, SOURCES_TOOL=t))
            red = p.returncode != 0
            print("%s  mutant: %s -> suite %s" % ("ok  " if red else "FAIL", what, "red" if red else "STILL GREEN"))
            bad += 0 if red else 1
        finally:
            shutil.rmtree(d, ignore_errors=True)
    return 1 if bad else 0


def main():
    if "--mutants" in sys.argv:
        return mutants()
    sys.path.insert(0, os.path.dirname(TOOL))
    import importlib.machinery, importlib.util
    spec = importlib.util.spec_from_loader("sources", importlib.machinery.SourceFileLoader("sources", TOOL))
    S = importlib.util.module_from_spec(spec); spec.loader.exec_module(S)

    # ── the package list ──────────────────────────────────────────
    st = status([("libfoo1", "1.2-3build1", "foo (1:1.2-3)", "Ubuntu Developers <x@y>", "install ok installed"),
                 ("bar", "2.0-1", None, "Ubuntu Developers <x@y>", "install ok installed"),
                 ("gone", "1-1", None, "x", "deinstall ok config-files"),
                 ("held", "3-1", "heldsrc", "x", "hold ok installed"),
                 ("half", "4-1", None, "x", "install ok half-configured"), ("unp", "5-1", None, "x", "install ok unpacked"),
                 ("trig", "6-1", None, "x", "install ok triggers-pending"), ("never", "7-1", None, "x", "purge ok not-installed")])
    pk = S.parse_status(st)
    by = {p["bin"]: p for p in pk}
    check(set(by) == {"libfoo1", "bar", "held", "half", "unp", "trig"},
          "a removed package (config files only, not-installed) is not shipped; held, unpacked, half-configured and triggers-pending ones are (their files are on disk)")
    check((by["libfoo1"]["src"], by["libfoo1"]["srcver"]) == ("foo", "1:1.2-3"),
          "Source: foo (1:1.2-3) gives dpkg's ${source:Package} and ${source:Version}")
    check((by["bar"]["src"], by["bar"]["srcver"]) == ("bar", "2.0-1"), "no Source header: the binary is its own source")
    check(S.moz_version("1:158.0~build2") == "158.0" and S.moz_version("156.0.1~build1") == "156.0.1",
          "Mozilla's 158.0~build2 is upstream 158.0")
    folded = S.parse_status("Package: x\nStatus: install ok installed\nVersion: 1\nBuilt-Using: aa (= 1-1),\n bb (= 2-2)\n\n")
    check(folded[0]["built_using"] == [("aa", "1-1"), ("bb", "2-2")], "a folded Built-Using keeps its tail")
    for label, bad in (("a Source header that walks out of the work directory", "Package: x\nStatus: install ok installed\nVersion: 1\nSource: ../../tmp/evil (1.0)\n\n"),
                       ("a Built-Using that names a path", "Package: x\nStatus: install ok installed\nVersion: 1\nBuilt-Using: /etc/passwd (= 1)\n\n"),
                       ("a version with a slash", "Package: x\nStatus: install ok installed\nVersion: 1\nSource: ok (1/../2)\n\n")):
        try:
            S.parse_status(bad); ok = False
        except S.Fail:
            ok = True
        check(ok, "%s is refused" % label)
    d = S.parse_dsc("-----BEGIN PGP SIGNED MESSAGE-----\nHash: SHA512\n\nSource: a\nVersion: 1\nChecksums-Sha256:\n"
                    " aa 1 a_1.tar.xz\n- -----not armour\n-----BEGIN PGP SIGNATURE-----\n\nzz\n-----END PGP SIGNATURE-----\n")
    check(d["Source"] == "a" and d["Version"] == "1" and "a_1.tar.xz" in d["Checksums-Sha256"], "a clearsigned .dsc is read")

    tmp = tempfile.mkdtemp(prefix="sourcestest.")
    try:
        arch = Archive()
        arch.add("foo", "1:1.2-3", signed=True)       # epoch, clearsigned
        arch.add("bar", "2.0-1")
        arch.add("heldsrc", "3-1", extra=1000)
        arch.add("hostglibc", "2.39-0ubuntu8.9")
        arch.add("bu1", "1.0-1"); arch.add("bu2", "2:3.4-5")
        shared = os.urandom(3000); arch.add("dup", "1.0-1", orig=shared); arch.add("dup", "1.0-2", orig=shared)
        maint = "Ubuntu Developers <x@y>"
        pkgs = [("libfoo1", "1.2-3build1", "foo (1:1.2-3)", maint, "install ok installed"),
                ("foo-tools", "1.2-3", "foo (1:1.2-3)", maint, "install ok installed"),
                ("bar", "2.0-1", None, maint, "install ok installed"),
                ("held", "3-1", "heldsrc", maint, "hold ok installed"),
                ("auros-desktop", "0.1.0.2026", None, "AurOS <noreply@auros.invalid>", "install ok installed"),
                ("firefox", "158.0~build2", None, "Mozilla <release@mozilla.com>", "install ok installed"),
                ("bar-signed", "2.0-1+sig", "bar-signed", maint, "install ok installed", "bu1 (= 1.0-1), bu2 (= 2:3.4-5)"),
                ("dup-a", "1.0-1", "dup", maint, "install ok installed"), ("dup-b", "1.0-2", "dup", maint, "install ok installed")]
        arch.add("bar-signed", "2.0-1+sig")
        root = root_dir(tmp, pkgs)
        extra = os.path.join(tmp, "host.txt")
        open(extra, "w").write("# the runner's libc6-dev\nhostglibc 2.39-0ubuntu8.9\n")
        own = os.path.join(tmp, "auros-source-abc.tar.gz"); open(own, "wb").write(b"own source")
        base = ["--base", "lp:" + arch.url, "--no-online"]

        rc, o = run("plan", "--root", root, "--extra", extra)
        check(rc == 0 and "9 Ubuntu source packages" in o and "foo\t1:1.2-3\tfoo-tools,libfoo1" in o,
              "plan lists one source per (name, version), with its binaries")
        check("bu1\t1.0-1\t(Built-Using of bar-signed)" in o and "bu2\t2:3.4-5" in o,
              "plan adds every Built-Using source of an installed package (a signed copy, a static link)")

        rc, o = run("plan", "--root", root, "--extra", extra, "--resolve", "--base", "lp:" + arch.url)
        check(rc == 0 and "9 of 9 source packages resolve" in o,
              "plan --resolve finds every .dsc on a complete archive")
        a0 = Archive(); a0.add("foo", "1:1.2-3"); a0.add("bar", "2.0-1"); a0.add("hostglibc", "2.39-0ubuntu8.9")
        rc, o = run("plan", "--root", root, "--extra", extra, "--resolve", "--base", "lp:" + a0.url)
        check(rc != 0 and "MISSING heldsrc 3-1" in o, "plan --resolve names the source package that is not published")

        # ── the good release ──────────────────────────────────────
        out = os.path.join(tmp, "rel")
        rc, o = run("make", "--root", root, "--out", out, "--tag", "v9", "--commit", "c0ffee", "--own-archive", own,
                    "--extra", extra, "--part-bytes", "7500", *base)
        check(rc == 0, "make succeeds on a complete archive (%s)" % o.strip().splitlines()[-1][:90])
        files = sorted(os.listdir(out))
        parts = [f for f in files if f.startswith("sources-v9-")]
        check(len(parts) >= 2 and all(os.path.getsize(os.path.join(out, f)) < 80000 for f in parts[:1]) or len(parts) >= 2,
              "parts are split at the limit (%d parts)" % len(parts))
        check({"SOURCES.txt", "PACKAGES.txt", "SHA256SUMS", "auros-source-abc.tar.gz"} <= set(files), "the release has its lists, sums and own source")
        so = open(os.path.join(out, "SOURCES.txt")).read()
        check("S\tfoo\t1:1.2-3\t" in so and "P\tfirefox\t158.0\t" in so and "O\tauros\tv9\t" in so, "SOURCES.txt names source, pointer and own archive")
        rc, o = run("verify", "--dir", out, "--root", root, "--no-online")
        check(rc == 0, "verify accepts it, against the image's own package list")

        # ── include, host note, Firefox mirror (online, against a pretend Mozilla) ──
        tarball = os.urandom(4000)
        arch.files["/moz/158.0/SHA256SUMS"] = ("%s  source/firefox-158.0.source.tar.xz\n" % sha(tarball)).encode()
        arch.files["/moz/158.0/source/firefox-158.0.source.tar.xz"] = tarball
        notices = os.path.join(tmp, "NOTICES.txt"); open(notices, "w").write("the license texts")
        out2 = os.path.join(tmp, "rel2")
        rc, o = run("make", "--root", root, "--out", out2, "--tag", "v9", "--own-archive", own, "--extra", extra,
                    "--include", notices, "--host-note", "the build host's glibc was read on the publishing machine",
                    "--mirror-firefox", "--moz-base", arch.url.replace("/lp", "/moz"), "--base", "lp:" + arch.url, "--jobs", "2")
        check(rc == 0, "make with --include, --host-note and a Firefox mirror succeeds (%s)" % o.strip().splitlines()[-1][:80])
        so2 = open(os.path.join(out2, "SOURCES.txt")).read() if os.path.isfile(os.path.join(out2, "SOURCES.txt")) else ""
        check(os.path.isfile(os.path.join(out2, "NOTICES.txt")) and "NOTICES.txt" in open(os.path.join(out2, "SHA256SUMS")).read(),
              "--include puts the file in the release and in SHA256SUMS")
        check("# the build host's glibc was read on the publishing machine" in so2, "--host-note is in SOURCES.txt's header")
        check(os.path.isfile(os.path.join(out2, "firefox-158.0.source.tar.xz")) and "\tfirefox-158.0.source.tar.xz\n" in so2,
              "--mirror-firefox puts Mozilla's tarball in the release and names it in the pointer row")
        rc, o = run("verify", "--dir", out2, "--root", root)
        check(rc == 0, "verify accepts the release with the mirror (online pointer check against the pretend Mozilla: %s)" % o.strip()[-60:])
        w = os.path.join(tmp, "m2"); shutil.copytree(out2, w); os.remove(os.path.join(w, "firefox-158.0.source.tar.xz")); resum(w)
        rc, o = run("verify", "--dir", w, "--root", root, "--no-online")
        check(rc != 0 and "Firefox source mirror" in o, "verify refuses a release whose Firefox mirror has gone")
        arch.files["/moz/158.0/SHA256SUMS"] = ("%s  source/firefox-158.0.source.tar.xz\n" % ("0" * 64)).encode()
        rc, o = run("make", "--root", root, "--out", os.path.join(tmp, "rel3"), "--tag", "v9", "--own-archive", own, "--extra", extra,
                    "--mirror-firefox", "--moz-base", arch.url.replace("/lp", "/moz"), "--base", "lp:" + arch.url)
        check(rc != 0 and "Mozilla's SHA256SUMS says" in o, "a Firefox tarball that does not hash to Mozilla's sum is refused")

        # ── make refuses ──────────────────────────────────────────
        def refuses(label, args, needle, mutate=None):
            rc, o = run(*args)
            check(rc != 0 and needle in o, "%s -> refused, and says %r" % (label, needle))

        a2 = Archive(); a2.add("foo", "1:1.2-3"); a2.add("bar", "2.0-1", missing="bar_2.0.orig.tar.xz"); a2.add("heldsrc", "3-1")
        refuses("a file the .dsc names is not on the server",
                ["make", "--root", root, "--out", os.path.join(tmp, "r2"), "--tag", "v9", "--own-archive", own,
                 "--base", "lp:" + a2.url, "--no-online"], "bar 2.0-1")
        a3 = Archive(); a3.add("foo", "1:1.2-3"); a3.add("bar", "2.0-1", corrupt="bar_2.0.orig.tar.xz"); a3.add("heldsrc", "3-1")
        refuses("a file does not hash to what the .dsc says",
                ["make", "--root", root, "--out", os.path.join(tmp, "r3"), "--tag", "v9", "--own-archive", own,
                 "--base", "lp:" + a3.url, "--no-online"], "its .dsc says")
        a4 = Archive(); a4.add("foo", "1:1.2-3"); a4.add("bar", "2.0-1")   # heldsrc never published
        refuses("a binary package whose source package is nowhere",
                ["make", "--root", root, "--out", os.path.join(tmp, "r4"), "--tag", "v9", "--own-archive", own,
                 "--base", "lp:" + a4.url, "--no-online"], "heldsrc 3-1")
        refuses("AurOS's own package and no archive of AurOS's source",
                ["make", "--root", root, "--out", os.path.join(tmp, "r5"), "--tag", "v9"] + base, "--own-archive")
        a5 = Archive(); a5.add("foo", "1:1.2-3", extra=900); a5.add("bar", "2.0-1"); a5.add("heldsrc", "3-1"); a5.add("bar-signed", "2.0-1+sig"); a5.add("bu1", "1.0-1"); a5.add("bu2", "2:3.4-5"); a5.add("dup", "1.0-1"); a5.add("dup", "1.0-2")
        refuses("one source package larger than a part",
                ["make", "--root", root, "--out", os.path.join(tmp, "r6"), "--tag", "v9", "--own-archive", own,
                 "--part-bytes", "1000", "--base", "lp:" + a5.url, "--no-online"], "more than one part")
        a6 = Archive(); a6.add("foo", "1:1.2-3"); a6.add("bar", "2.0-1"); a6.add("heldsrc", "3-1"); a6.add("bar-signed", "2.0-1+sig"); a6.add("bu1", "1.0-1"); a6.add("bu2", "2:3.4-5"); a6.add("dup", "1.0-1"); a6.add("dup", "1.0-2")
        d = "/lp/bar/2.0-1/bar_2.0-1.dsc"
        a6.files[d] = a6.files[d].replace(b"Version: 2.0-1", b"Version: 2.0-2")
        refuses("a .dsc that is another version than the one asked for",
                ["make", "--root", root, "--out", os.path.join(tmp, "r7"), "--tag", "v9", "--own-archive", own,
                 "--base", "lp:" + a6.url, "--no-online"], "not bar 2.0-1")
        a7 = Archive(); [a7.add(*x) for x in (("foo", "1:1.2-3"), ("bar", "2.0-1"), ("heldsrc", "3-1"), ("bar-signed", "2.0-1+sig"), ("bu2", "2:3.4-5"), ("dup", "1.0-1"), ("dup", "1.0-2"))]
        refuses("a Built-Using source that is nowhere (a signed copy's origin)",
                ["make", "--root", root, "--out", os.path.join(tmp, "r9"), "--tag", "v9", "--own-archive", own,
                 "--base", "lp:" + a7.url, "--no-online"], "bu1 1.0-1")
        os.makedirs(os.path.join(tmp, "r8")); open(os.path.join(tmp, "r8", "x"), "w").write("x")
        refuses("a release directory that is not empty", ["make", "--root", root, "--out", os.path.join(tmp, "r8"),
                "--tag", "v9", "--own-archive", own] + base, "not empty")

        # the Firefox tarball route has no dpkg entry: the version comes from the install itself
        root2 = root_dir(tmp, [p for p in pkgs if p[0] != "firefox"], "root2", firefox_ini="140.0.4")
        rc, o = run("plan", "--root", root2, "--extra", extra)
        check(rc == 0 and "firefox\t140.0.4\t(pointer)" in o, "Firefox from the tarball route (no dpkg entry) still gets its pointer")

        # ── verify refuses ────────────────────────────────────────
        def mutated(label, needle, fn, status_root=root):
            w = os.path.join(tmp, "m")
            shutil.rmtree(w, ignore_errors=True); shutil.copytree(out, w)
            fn(w)
            rc, o = run("verify", "--dir", w, "--root", status_root, "--no-online")
            check(rc != 0 and needle in o, "verify refuses: %s (says %r)" % (label, needle))

        mutated("a part deleted", "missing", lambda w: os.remove(os.path.join(w, parts[0])))
        def flip_byte(w):
            p = os.path.join(w, parts[0]); b = bytearray(open(p, "rb").read()); b[700] ^= 1; open(p, "wb").write(bytes(b))
        mutated("a byte of a part changed", "does not match its SHA256SUMS", flip_byte)
        def drop_member(w):
            for p in parts:
                rewrite_part(os.path.join(w, p), drop="bar_2.0.orig.tar.xz")
            resum(w)
        mutated("a source file missing from the parts (sums redone)", "bar_2.0.orig.tar.xz is in no part", drop_member)
        def flip_member(w):
            for p in parts:
                rewrite_part(os.path.join(w, p), flip=True)
            resum(w)
        mutated("a source file altered inside a part (sums redone)", "is not the file its .dsc names", flip_member)
        def drop_srow(w):
            p = os.path.join(w, "SOURCES.txt")
            open(p, "w").write("".join(l for l in open(p) if not l.startswith("S\tbar\t")))
            resum(w)
        mutated("a source package missing from SOURCES.txt", "bar 2.0-1 has no source", drop_srow)
        def drop_brow(w):
            p = os.path.join(w, "PACKAGES.txt")
            open(p, "w").write("".join(l for l in open(p) if not l.startswith("foo-tools\t")))
            resum(w)
        mutated("a binary package missing from PACKAGES.txt", "is not the image's package list", drop_brow)
        def drop_own(w):
            os.remove(os.path.join(w, "auros-source-abc.tar.gz")); resum(w)
        mutated("AurOS's own source archive removed", "no archive of AurOS's own source", drop_own)
        def drop_ptr(w):
            p = os.path.join(w, "SOURCES.txt")
            open(p, "w").write("".join(l for l in open(p) if not l.startswith("P\t")))
            resum(w)
        mutated("the pointer to Firefox's source removed", "no pointer to Firefox", drop_ptr)
        mutated("no SHA256SUMS", "no SHA256SUMS", lambda w: os.remove(os.path.join(w, "SHA256SUMS")))

        # ── the weekly re-check: no parts on disk, only the API's digests ──
        import json
        def assets_of(d):
            return [{"name": n, "digest": "sha256:" + sha(open(os.path.join(d, n), "rb").read())}
                    for n in sorted(os.listdir(d)) if n != "SHA256SUMS"] + [{"name": "SHA256SUMS", "digest": "sha256:x"}]
        def light(label, ok, needle, tweak_assets=None, drop_part_row=False):
            w = os.path.join(tmp, "L"); shutil.rmtree(w, ignore_errors=True); os.makedirs(w)
            for n in ("SHA256SUMS", "SOURCES.txt", "PACKAGES.txt"):
                shutil.copy(os.path.join(out, n), w)
            a = assets_of(out)
            if tweak_assets: a = tweak_assets(a)
            aj = os.path.join(tmp, "assets.json"); json.dump(a, open(aj, "w"))
            rc, o = run("verify", "--dir", w, "--api-json", aj, "--no-online")
            check((rc == 0) == ok and (ok or needle in o), "light verify %s" % label)
        light("accepts the release as the API lists it", True, "")
        light("refuses a part whose digest changed", False, "does not match its SHA256SUMS line",
              lambda a: [dict(x, digest="sha256:" + "0" * 64) if x["name"] == parts[0] else x for x in a])
        light("refuses a part that has gone from the release", False, "not a file of the release",
              lambda a: [x for x in a if x["name"] != parts[0]])
        light("refuses a file on the release that SHA256SUMS does not know", False, "not in SHA256SUMS",
              lambda a: a + [{"name": "extra.bin", "digest": "sha256:1"}])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("\n%s" % ("all passed" if not FAILED else "%d FAILED: %s" % (len(FAILED), "; ".join(FAILED))))
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
