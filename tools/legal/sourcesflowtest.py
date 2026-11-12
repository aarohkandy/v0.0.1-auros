#!/usr/bin/env python3
"""sourcesflowtest -- the source offer of a custom OS, run as the workflows run it.

    python3 -I tools/legal/sourcesflowtest.py

sources.yml and make-os.yml are YAML with shell in them, and a shell step is not
checked by the YAML lint. This takes the `run:` text of the steps that decide
which source release is made, for what, and whether the installer may be
published, and RUNS it (bash -eo pipefail, as Actions does) with a fake `gh`
and `curl` that answer from a table and write down every call. Nothing is
reimplemented here: change a step and this runs the changed step.

Each "refuses" case breaks one thing and requires a non-zero exit AND the reason.
The desktop's own path is run through the same steps, and must behave as before.
SOURCES_YML / MAKEOS_YML name other copies of the two files (to show the cases red
against the old ones: git show agent/c-int:.github/workflows/sources.yml).
Needs PyYAML (apt: python3-yaml), bash, git, zstd, tar.
"""
import json, os, shutil, subprocess, sys, tempfile

try:
    import yaml
except ImportError:
    sys.exit("sourcesflowtest: needs PyYAML (apt: python3-yaml)")

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCES = os.environ.get("SOURCES_YML") or os.path.join(ROOT, ".github/workflows/sources.yml")
MAKEOS = os.environ.get("MAKEOS_YML") or os.path.join(ROOT, ".github/workflows/make-os.yml")
OSMETA = os.environ.get("OSMETA") or os.path.join(ROOT, "tools/legal/osmeta.sh")
FAILED = []
SHA = "0123456789abcdef0123456789abcdef01234567"
SHORT = SHA[:7]


def check(ok, what):
    print("%s  %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        FAILED.append(what)


def load(path):
    return yaml.safe_load(open(path, encoding="utf-8"))


SRC, MOS = load(SOURCES), load(MAKEOS)


def step(doc, job, key, val):
    """The step of `job` whose `key` (id or name) equals / starts with `val`."""
    for st in doc["jobs"][job]["steps"]:
        if str(st.get(key, "")).startswith(val):
            return st
    raise SystemExit("sourcesflowtest: no step %s=%r in job %s" % (key, val, job))


# ── fakes ───────────────────────────────────────────────────────────
FAKE_GH = r'''#!/usr/bin/env python3
import json, os, sys
a = sys.argv[1:]
open(os.environ["FAKE_LOG"], "a").write(" ".join(a) + "\n")
if not sys.stdin.isatty():
    sys.stdin.read()          # a real gh reads what is piped to it ("--notes-file -")
line = " ".join(a)
for r in json.load(open(os.environ["FAKE_GH"])):
    if all(m in line for m in r["match"]):
        sys.stdout.write(r.get("out", "")); sys.stderr.write(r.get("err", "")); sys.exit(r.get("rc", 0))
sys.stderr.write("fake gh: unexpected call: %s\n" % line); sys.exit(99)
'''
# curl -fsSL --retry 5 -o DEST URL : the file is FIXTURES/<basename of URL>, or a failure
FAKE_CURL = r'''#!/bin/sh
dest=""; url=""
while [ $# -gt 0 ]; do
    case "$1" in -o) dest=$2; shift ;; --retry) shift ;; -*) ;; *) url=$1 ;; esac
    shift
done
echo "curl $url" >> "$FAKE_LOG"
f="$FIXTURES/$(basename "$url")"
[ -f "$f" ] || { echo "curl: (22) 404 $url" >&2; exit 22; }
cp "$f" "$dest"
'''


class Box:
    """A scratch directory with the fake commands in front of PATH."""
    def __init__(self):
        self.d = tempfile.mkdtemp(prefix="sourcesflow.")
        self.bin = os.path.join(self.d, "bin"); os.makedirs(self.bin)
        for n, t in (("gh", FAKE_GH), ("curl", FAKE_CURL)):
            open(os.path.join(self.bin, n), "w").write(t); os.chmod(os.path.join(self.bin, n), 0o755)
        self.log = os.path.join(self.d, "calls.log"); open(self.log, "w").close()
        self.fix = os.path.join(self.d, "fixtures"); os.makedirs(self.fix)

    def gh(self, *rules):
        json.dump(list(rules), open(os.path.join(self.d, "gh.json"), "w"))

    def calls(self):
        return open(self.log).read()

    def run(self, script, env=None, cwd=None):
        cwd = cwd or self.d
        e = dict(os.environ, PATH=self.bin + os.pathsep + os.environ["PATH"], FAKE_GH=os.path.join(self.d, "gh.json"),
                 FAKE_LOG=self.log, FIXTURES=self.fix, GITHUB_REPOSITORY="o/r", GITHUB_OUTPUT=os.path.join(self.d, "out"),
                 GITHUB_ENV=os.path.join(self.d, "env"), GITHUB_STEP_SUMMARY=os.path.join(self.d, "summary"))
        for k in ("GH_REPO", "AUROS_REPO", "AUROS_DOMAIN", "AUROS_SITE"):
            e.pop(k, None)
        e.update(env or {})
        for f in ("out", "env", "summary"):
            open(os.path.join(self.d, f), "w").close()
        p = subprocess.run(["bash", "--noprofile", "--norc", "-eo", "pipefail", "-c", script], cwd=cwd, env=e,
                           capture_output=True, text=True, timeout=120, stdin=subprocess.DEVNULL)
        return p.returncode, p.stdout + p.stderr

    def file(self, name):
        return open(os.path.join(self.d, name)).read()

    def done(self):
        shutil.rmtree(self.d, ignore_errors=True)


# ── 1. sources.yml `which`: what a call may ask for ─────────────────
def test_which():
    run = step(SRC, "which", "id", "w")["run"]
    b = Box()
    try:
        def call(tag, profile, rid="123", event="push"):
            b.gh()
            rc, o = b.run(run, {"IN_RUN": rid, "IN_TAG": tag, "IN_PROFILE": profile, "EVENT": event, "MSG": "a commit"})
            return rc, o, b.file("out")
        rc, o, out = call("v0.1.0-" + SHORT, "")
        check(rc == 0 and '"tag":"v0.1.0-%s"' % SHORT in out and '"again":"false"' in out,
              "release.yml's call (no profile) asks for exactly its own tag, as before")
        rc, o, out = call("os-office-" + SHORT, "office")
        check(rc == 0 and '"tag":"os-office-%s","run":"123"' % SHORT in out,
              "make-os.yml's call (profile office, tag os-office-<sha7>) is accepted (%s)" % o.strip()[-60:])
        rc, o, out = call("os-school-kiosk-" + SHORT, "school-kiosk")
        check(rc == 0 and "os-school-kiosk-" + SHORT in out, "a profile with a dash in it is accepted")
        for what, tag, prof, why in (
                ("an os tag with no profile (the desktop path never makes a custom OS's)", "os-office-" + SHORT, "", "odd tag"),
                ("a v tag with a profile", "v0.1.0-" + SHORT, "office", "odd profile, tag"),
                ("a tag of another profile", "os-revive-" + SHORT, "office", "odd profile, tag"),
                ("a tag whose commit part is 6 hex", "os-office-" + SHORT[:6], "office", "odd profile, tag"),
                ("a tag whose commit part is 8 hex", "os-office-" + SHORT + "a", "office", "odd profile, tag"),
                ("a tag whose commit part is upper case", "os-office-0123ABC", "office", "odd profile, tag"),
                ("a tag with something after it", "os-office-%s;id" % SHORT, "office", "odd profile, tag"),
                ("a profile that is a shell command", "os-a;id-" + SHORT, "a;id", "odd profile, tag"),
                ("a profile that is a path", "os-../x-" + SHORT, "../x", "odd profile, tag"),
                ("a profile with capitals", "os-Office-" + SHORT, "Office", "odd profile, tag"),
                ("a profile that starts with a dash", "os--x-" + SHORT, "-x", "odd profile, tag"),
                ("a profile that is a regex", "os-.*-" + SHORT, ".*", "odd profile, tag"),
                ("a profile of 33 letters", "os-" + "a" * 33 + "-" + SHORT, "a" * 33, "odd profile, tag")):
            rc, o, out = call(tag, prof)
            check(rc != 0 and why in o and out == "", "refused: " + what)
        rc, o, out = call("os-office-" + SHORT, "office", rid="12 3")
        check(rc != 0 and out == "", "refused: a run id with a space")
        # the sweep (every 3 hours) is for v tags only: an os- prerelease with no sources release is not its business
        b.gh({"match": ["api", "--paginate"], "out":
              json.dumps(["os-office-" + SHORT, False, "tested by [Actions run 7]", "2099-01-01T00:00:00Z"]) + "\n" +
              json.dumps(["v0.1.0-abcdef0", False, "tested by [Actions run 8]", "2099-01-01T00:00:00Z"]) + "\n"})
        rc, o = b.run(run, {"IN_RUN": "", "IN_TAG": "", "IN_PROFILE": "", "EVENT": "schedule", "MSG": ""})
        out = b.file("out")
        check(rc == 0 and "v0.1.0-abcdef0" in out and "os-office" not in out and "mode=sweep" in out,
              "the 3-hourly sweep makes the sources of v releases and never reaches for an os- one (%s)" % o.strip()[-60:])
    finally:
        b.done()


# ── 2. sources.yml `make`, first step: tag, commit, does it exist ───
def test_make_first():
    run = step(SRC, "make", "name", "which commit the release was built from")["run"]
    b = Box()
    try:
        repo = os.path.join(b.d, "repo")
        subprocess.run("git init -q repo && cd repo && git -c user.email=a@b -c user.name=n commit -q --allow-empty -m x", shell=True, cwd=b.d, check=True)
        sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True, text=True).stdout.strip()
        short = sha[:7]

        def call(tag, profile, rid="99", sources="none", again="false", commit=None):
            rules = [{"match": ["api", "actions/runs/" + rid], "out": (commit or sha) + "\n"}]
            if sources == "none":
                rules.append({"match": ["release", "view", "sources-"], "rc": 1, "err": "release not found\n"})
            else:
                rules.append({"match": ["release", "view", "sources-"], "out": sources + "\n"})
            b.gh(*rules)
            rc, o = b.run(run, {"TAG": tag, "RID": rid, "AGAIN": again, "PROFILE": profile}, cwd=repo)
            return rc, o, b.file("env")
        rc, o, env = call("v0.1.0-" + short, "")
        check(rc == 0 and "EXISTS=false" in env and "COMMIT=" + sha in env, "desktop: a tag ending in the built commit is accepted, nothing exists yet")
        rc, o, env = call("os-office-" + short, "office")
        check(rc == 0 and "EXISTS=false" in env and "COMMIT=" + sha in env, "custom OS: os-office-<sha7> of the run's commit is accepted (%s)" % o.strip()[-60:])
        rc, o, env = call("os-office-" + short, "office", sources="false")
        check(rc == 0 and "EXISTS=true" in env, "a published sources release of that name is verified and not rewritten")
        rc, o, env = call("os-office-" + short, "")
        check(rc != 0 and "odd tag" in o, "refused: an os tag when no profile was given")
        rc, o, env = call("v0.1.0-" + short, "office")
        check(rc != 0 and "odd tag" in o, "refused: a v tag when a profile was given")
        rc, o, env = call("os-office-0000000", "office")
        check(rc != 0 and "does not end in the built commit" in o, "refused: a tag whose commit is not the run's")
        rc, o, env = call("os-office-" + short, "office", commit="f" * 40)
        check(rc != 0, "refused: a run whose commit is not in this repository")
    finally:
        b.done()


# ── 3. sources.yml `make`, second step: the AurOS release is touched only for the desktop ──
def test_make_notices():
    run = step(SRC, "make", "name", "the AurOS release itself says")["run"]
    b = Box()
    try:
        for d in ("tools", "docs"):
            os.symlink(os.path.join(ROOT, d), os.path.join(b.d, d))
        os.makedirs(os.path.join(b.d, "rootfs/usr/share/doc/auros"))
        shutil.copy(os.path.join(ROOT, "rootfs/usr/share/doc/auros/SOURCE-OFFER"), os.path.join(b.d, "rootfs/usr/share/doc/auros/"))
        tag = "os-office-" + SHORT
        b.gh()
        rc, o = b.run(run, {"TAG": tag, "PROFILE": "office"})
        n = b.file("out-inc/NOTICES.txt") if os.path.exists(os.path.join(b.d, "out-inc/NOTICES.txt")) else ""
        check(rc == 0 and b.calls() == "" and "@AUROS_RELEASES@" not in n and "THE SOURCE CODE" in n,
              "custom OS: NOTICES.txt is made for the source release and no release is read, edited or uploaded to (%s)" % (b.calls().strip() or "no gh call"))
        shutil.rmtree(os.path.join(b.d, "out-inc"), ignore_errors=True)
        tag = "v0.1.0-" + SHORT
        b.gh({"match": ["release", "view", tag, "--json", "body"], "out": "the notes\n"},
             {"match": ["release", "edit", tag], "out": ""},
             {"match": ["release", "view", tag, "--json", "assets"], "out": "SHA256SUMS\n"},
             {"match": ["release", "upload", tag, "out-inc/NOTICES.txt"], "out": ""})
        open(b.log, "w").close()
        rc, o = b.run(run, {"TAG": tag, "PROFILE": ""})
        c = b.calls()
        check(rc == 0 and "release edit " + tag in c and "release upload " + tag in c and "now point to the source" in o,
              "desktop: the notes get the pointer and NOTICES.txt is attached, as before (%s)" % o.strip()[-60:])
    finally:
        b.done()


# ── 4. make-os.yml: the source release is checked before the notes promise it ──
def test_publish_check():
    run = step(MOS, "publish", "name", "the source release exists")["run"]
    tag = "os-office-" + SHORT
    b = Box()
    try:
        def fixtures(notices=b"licenses\n", packages=None, sources=None, sums_notices=None):
            sums_notices = hashlib_hex(notices) if sums_notices is None else sums_notices
            open(os.path.join(b.fix, "NOTICES.txt"), "wb").write(notices)
            open(os.path.join(b.fix, "PACKAGES.txt"), "w").write(packages or "# binary packages in AurOS %s, from the image's dpkg status\nbash\t5\tamd64\tbash\t5\tubuntu\n" % tag)
            open(os.path.join(b.fix, "SOURCES.txt"), "w").write(sources or "# AurOS %s: the corresponding source\n# commit %s, prepared x\nS\tbash\t5\t1\tabc\t3\t10\tbash\n" % (tag, SHA))
            open(os.path.join(b.fix, "SHA256SUMS"), "w").write("%s  NOTICES.txt\n%s  SOURCES.txt\n" % (sums_notices, "0" * 64))

        def call(state="false true", **kw):
            shutil.rmtree(os.path.join(b.d, "rel"), ignore_errors=True); os.makedirs(os.path.join(b.d, "rel"))
            shutil.rmtree(os.path.join(b.d, "src"), ignore_errors=True)
            fixtures(**kw)
            b.gh({"match": ["release", "view", "sources-" + tag, "--json", "isDraft,isPrerelease"], "out": state + "\n"})
            rc, o = b.run(run, {"TAG": tag, "GITHUB_SHA": SHA})
            return rc, o, os.path.exists(os.path.join(b.d, "rel/NOTICES.txt"))
        rc, o, has = call()
        check(rc == 0 and has, "a published, prerelease, matching source release passes and its NOTICES.txt joins the installer's release (%s)" % o.strip()[-70:])
        rc, o, has = call(state="true true")
        check(rc != 0 and not has and "not a published prerelease" in o, "refused: the source release is still a draft")
        rc, o, has = call(state="false false")
        check(rc != 0 and not has, "refused: the source release is not a prerelease")
        rc, o, has = call(notices=b"changed\n", sums_notices=hashlib_hex(b"licenses\n"))
        check(rc != 0 and not has and "NOTICES.txt of" in o, "refused: NOTICES.txt is not the file the release's own SHA256SUMS lists")
        rc, o, has = call(packages="# binary packages in AurOS os-office-0000000, from the image's dpkg status\nbash\n")
        check(rc != 0 and not has and "does not name" in o, "refused: PACKAGES.txt is another release's")
        rc, o, has = call(sources="# AurOS x: the corresponding source\n# commit %s, prepared x\nS\tbash\t5\t1\tabc\t3\t10\tbash\n" % ("e" * 40))
        check(rc != 0 and not has and "was not made from commit" in o, "refused: SOURCES.txt was made from another commit")
        rc, o, has = call(sources="# AurOS x: the corresponding source\n# commit %s, prepared x\n# no program\n" % SHA)
        check(rc != 0 and not has and "lists no source package" in o, "refused: SOURCES.txt lists no source package")
        os.remove(os.path.join(b.fix, "NOTICES.txt"))
        shutil.rmtree(os.path.join(b.d, "rel"), ignore_errors=True); os.makedirs(os.path.join(b.d, "rel")); shutil.rmtree(os.path.join(b.d, "src"), ignore_errors=True)
        b.gh({"match": ["release", "view"], "out": "false true\n"})
        rc, o = b.run(run, {"TAG": tag, "GITHUB_SHA": SHA})
        check(rc != 0, "refused: a file of the source release cannot be downloaded")
    finally:
        b.done()


def hashlib_hex(data):
    import hashlib
    return hashlib.sha256(data).hexdigest()


# ── 5. make-os.yml: the notes ──────────────────────────────────────
def test_notes():
    run = step(MOS, "publish", "name", "the notes, in plain words")["run"]
    tag = "os-office-" + SHORT
    b = Box()
    try:
        for d in ("tools", "docs", "rootfs", "profiles"):
            os.symlink(os.path.join(ROOT, d), os.path.join(b.d, d))
        os.makedirs(os.path.join(b.d, "rel")); open(os.path.join(b.d, "rel/AurOS-Setup-office.exe"), "wb").write(b"x" * 10)
        env = {"PROFILE": "office", "TAG": tag, "IMAGE_SHA256": "a" * 64, "IMAGE_BYTES": "1", "SETUP_SHA256": "b" * 64, "PIECES": "2",
               "DOWNLOAD_BYTES": "3", "WINDOWS": "true", "GITHUB_SHA": SHA, "GITHUB_RUN_ID": "5", "GITHUB_SERVER_URL": "https://github.com",
               "GITHUB_REPOSITORY": "o/r"}
        rc, o = b.run(run, env)
        notes = b.file("notes.md") if os.path.exists(os.path.join(b.d, "notes.md")) else ""
        check(rc == 0 and "https://github.com/o/r/releases/tag/sources-" + tag in notes and "complete source code for this release" in notes,
              "the notes link the source release of this very tag and carry the sentence every AurOS release carries (%s)" % o.strip()[-60:])
        check("custom builds do not get one" not in notes and "do not get one" not in notes, "the notes no longer say a custom build has no source release")
        check("NOTICES.txt" in notes and "SOURCE-OFFER" in notes, "the notes say where NOTICES.txt and the written offer are")
    finally:
        b.done()


# ── 6. make-os.yml: the cross-check between the build tree and the image's disk ──
def test_crosscheck():
    run = step(MOS, "build", "name", "the package database handed to the source release")["run"]
    b = Box()
    try:
        os.makedirs(os.path.join(b.d, "srcin")); os.makedirs(os.path.join(b.d, "ev"))

        def call(want, got):
            open(os.path.join(b.d, "srcin/packages.count"), "w").write("%s\n" % want)
            if got is None:
                if os.path.exists(os.path.join(b.d, "ev/office-image.txt")):
                    os.remove(os.path.join(b.d, "ev/office-image.txt"))
            else:
                open(os.path.join(b.d, "ev/office-image.txt"), "w").write("profile=office\nimage_bytes=1\npackages=%s\nbuild-warning: x\n" % got)
            return b.run(run, {"PROFILE": "office"})
        rc, o = call(912, 912)
        check(rc == 0, "the database taken from the build tree has as many packages as proof 2 read off the image's disk")
        rc, o = call(912, 911)
        check(rc != 0 and "is not the image's" in o, "refused: one package more or fewer than the image's own disk")
        rc, o = call(912, None)
        check(rc != 0, "refused: proof 2 left no count to compare with")
    finally:
        b.done()


# ── 7. tools/legal/osmeta.sh: what the build hands to the source job ──
def test_osmeta():
    if not os.path.exists(OSMETA):
        check(False, "tools/legal/osmeta.sh exists"); return
    b = Box()
    try:
        # a dpkg-query that answers what a runner's would
        open(os.path.join(b.bin, "dpkg-query"), "w").write("#!/bin/sh\n[ -n \"$FAKE_HOST_EMPTY\" ] || echo 'glibc 2.39-0ubuntu8'\n")
        os.chmod(os.path.join(b.bin, "dpkg-query"), 0o755)

        def tree(name, status=True, n_installed=2, lists=True, extras=True):
            w = os.path.join(b.d, name); r = w + "/rootfs"
            os.makedirs(r + "/var/lib/dpkg/info"); os.makedirs(r + "/etc/auros")
            if status:
                open(r + "/var/lib/dpkg/status", "w").write("".join("Package: p%d\nStatus: install ok installed\n\n" % i for i in range(n_installed)) +
                                                              "Package: gone\nStatus: deinstall ok config-files\n\n")
            if lists:
                open(r + "/var/lib/dpkg/info/p0.list", "w").write("/usr/bin/p0\n")
                open(r + "/var/lib/dpkg/info/p1:amd64.list", "w").write("/usr/bin/p1\n")
            if extras:
                open(r + "/etc/auros/build-warnings", "w").write("browser=x\n")
                open(r + "/etc/auros/not-wanted", "w").write("secret\n")
            return w
        stg = os.path.join(b.d, "staging.img"); open(stg, "wb").write(b"cpio")

        def call(w, s=stg, out="o", env=None):
            return b.run('sh "$OSMETA" "%s" "%s" "%s"' % (w, s, os.path.join(b.d, out)), dict(env or {}, OSMETA=OSMETA))
        rc, o = call(tree("w1"))
        names = sorted(os.listdir(os.path.join(b.d, "o"))) if os.path.isdir(os.path.join(b.d, "o")) else []
        check(rc == 0 and names == ["auros-staging.img", "host-sources.txt", "packages.count", "rootfs.tar.zst"],
              "a good tree gives the four files the source job reads (%s)" % names)
        members = subprocess.run("zstd -dc o/rootfs.tar.zst | tar -t", shell=True, cwd=b.d, capture_output=True, text=True).stdout.split()
        check(sorted(members) == ["rootfs/etc/auros/build-warnings", "rootfs/var/lib/dpkg/info/p0.list",
                                  "rootfs/var/lib/dpkg/info/p1:amd64.list", "rootfs/var/lib/dpkg/status"],
              "the tar holds the dpkg database, its file lists and the image's records, in rootfs/..., and nothing else (%s)" % members)
        check(b.file("o/packages.count").strip() == "2" and b.file("o/host-sources.txt").strip() == "glibc 2.39-0ubuntu8",
              "installed packages are counted by dpkg's own state (a removed package is not one); the build host's glibc is recorded")
        # the source job reads it with exactly the lines of its own step
        ex = os.path.join(b.d, "ex"); os.makedirs(ex)
        rc2 = subprocess.run("zstd -dc o/rootfs.tar.zst | tar -x -C ex --strip-components=1", shell=True, cwd=b.d, capture_output=True, text=True)
        check(rc2.returncode == 0 and os.path.exists(ex + "/var/lib/dpkg/status") and os.path.exists(ex + "/var/lib/dpkg/info/p1:amd64.list"),
              "...and unpacks with --strip-components=1 to var/lib/dpkg/... as sources.yml unpacks it")
        for what, w, kw, why in (
                ("no dpkg database", tree("w2", status=False), {}, "no dpkg database"),
                ("a database with no installed package", tree("w3", n_installed=0), {}, "no installed package"),
                ("no package file lists", tree("w4", lists=False), {}, "no package file lists"),
                ("no staging image", tree("w5"), {"s": os.path.join(b.d, "nope.img")}, "no staging image"),
                ("no record of the build host's glibc", tree("w6"), {"env": {"FAKE_HOST_EMPTY": "1"}}, "glibc")):
            rc, o = call(w, **kw, out="o_" + os.path.basename(w))
            check(rc != 0 and why in o, "refused (nothing is made from nothing): " + what)
    finally:
        b.done()


# ── 7b. make-os.yml: what the run's summary says is ready to send ──
def test_summary():
    run = step(MOS, "summary", "name", "what to tell the person")["run"]
    tag = "os-office-" + SHORT
    b = Box()
    try:
        def call(build, sources, publish, windows="success"):
            b.gh()
            rc, o = b.run(run, {"PROFILE": "office", "TAG": tag, "REPO": "o/r", "COMMIT": SHA, "RUN_URL": "https://x/run", "SETUP_SHA256": "c" * 64,
                                "R_BUILD": build, "R_SOURCES": sources, "R_PUBLISH": publish, "R_WINDOWS": windows, "WINDOWS_ASKED": "true"})
            return rc, o
        rc, o = call("success", "success", "success")
        check(rc == 0 and "To paste to the person" in o and "releases/tag/sources-" + tag in o and "Nothing was published" not in o,
              "all green: the summary has the text to paste and names the source release")
        rc, o = call("success", "failure", "skipped", "skipped")
        check(rc == 0 and "Nothing was published" in o and "could not be made or checked" in o and "To paste to the person" not in o,
              "sources failed: nothing is ready to send, and it says the source is why")
        rc, o = call("success", "success", "failure", "skipped")
        check("Nothing was published" in o and "To paste to the person" not in o, "publish failed: nothing is ready to send")
        rc, o = call("failure", "skipped", "skipped", "skipped")
        check("Nothing was published" in o and "could not be made or checked" not in o and "To paste to the person" not in o,
              "build failed: nothing is ready to send, and the source is not blamed")
        rc, o = call("success", "failure", "success")
        check("Nothing was published" in o and "To paste to the person" not in o, "a publish that somehow succeeded without the source is still not ready to send")
    finally:
        b.done()


# ── 8. the shape of the two files: what must stay true ─────────────
def test_shape():
    ins = SRC[True]["workflow_call"]["inputs"] if True in SRC else SRC["on"]["workflow_call"]["inputs"]
    check(ins.get("profile", {}).get("default") == "" and ins.get("rootfs-artifact", {}).get("default") == "auros-rootfs"
          and ins.get("installer-artifact", {}).get("default") == "auros-installer",
          "sources.yml's new inputs default to today's behaviour (no profile, build.yml's artifact names)")
    calls = [j for j in MOS["jobs"].values() if j.get("uses") == "./.github/workflows/sources.yml"]
    check(len(calls) == 1 and calls[0]["with"].get("profile") == "${{ needs.check.outputs.profile }}", "make-os.yml calls sources.yml once, with the profile")
    if calls:
        c = calls[0]["with"]
        check(c["rootfs-artifact"] not in ("auros-rootfs", "auros-installer") and c["installer-artifact"] not in ("auros-rootfs", "auros-installer"),
              "...and under artifact names of its own: stagekvm.yml and tests.yml pick auros-rootfs / auros-installer from the newest run of any workflow")
        ups = [s["with"]["name"] for s in MOS["jobs"]["build"]["steps"] if str(s.get("uses", "")).startswith("actions/upload-artifact")]
        check(c["rootfs-artifact"] in ups and c["installer-artifact"] in ups, "...which the build job uploads (%s)" % ups)
        check("auros-rootfs" not in ups and "auros-installer" not in ups, "make-os.yml uploads nothing under build.yml's artifact names")
    check("sources" in MOS["jobs"]["publish"]["needs"] and "if" not in MOS["jobs"]["publish"], "publish waits for sources, with no if: to skip the wait")
    check("publish" in MOS["jobs"]["windows"]["needs"], "windows waits for publish, so for sources: no hours of Windows for a file that cannot be sent")
    rel = load(os.path.join(os.path.dirname(MAKEOS), "release.yml"))["jobs"]["sources"]
    check(set(rel.get("with", {})) == {"run-id", "tag"}, "release.yml still calls sources.yml with run-id and tag only (the desktop is unchanged)")
    text = open(SOURCES, encoding="utf-8").read()
    check("-n auros-installer" not in text and "name: auros-rootfs" not in text,
          "sources.yml names its input artifacts through the inputs, with build.yml's names as the defaults")


def main():
    for t in (test_which, test_make_first, test_make_notices, test_publish_check, test_notes, test_crosscheck, test_osmeta, test_summary, test_shape):
        try:
            t()
        except (Exception, SystemExit) as e:
            check(False, "%s could not run: %r" % (t.__name__, e))
    print("\n%s" % ("all passed" if not FAILED else "%d FAILED: %s" % (len(FAILED), "; ".join(FAILED))))
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
