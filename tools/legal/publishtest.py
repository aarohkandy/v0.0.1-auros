#!/usr/bin/env python3
"""publishtest -- tools/legal/publish.py against a pretend GitHub.

    python3 -I tools/legal/publishtest.py

The pretend `gh` keeps releases in a folder and can be told to fail an upload (FAKE_FAIL_UPLOAD=name-part)
or to corrupt one (FAKE_CORRUPT=name-part). The point of the file: a published sources release is the one
thing we may never lose (a run that was cancelled in the middle of the first version of this replaced a
part by nothing), so every case that can go wrong must leave the old release whole. Mutants: --mutants.
"""
import hashlib, json, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.environ.get("PUBLISH_TOOL") or os.path.join(ROOT, "tools", "legal", "publish.py")
FAILED = []

FAKE = r'''#!/usr/bin/env python3
import hashlib, json, os, shutil, sys
db = os.environ["FAKE_DB"]
a = sys.argv[1:]
def jload():
    return json.load(open(db + "/state.json")) if os.path.exists(db + "/state.json") else {"releases": [], "next": 1}
def jsave(s):
    json.dump(s, open(db + "/state.json", "w"))
def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()
def find(s, tag):
    return next((r for r in s["releases"] if r["tag_name"] == tag), None)
def opt(name):
    return a[a.index(name) + 1] if name in a else None
def out(x): print(json.dumps(x))
s = jload()
if a[:2] == ["release", "view"]:
    r = find(s, a[2])
    if os.environ.get("FAKE_VIEW_ERROR"):
        sys.stderr.write("HTTP 502\n"); sys.exit(1)
    if not r:
        sys.stderr.write("release not found\n"); sys.exit(1)
    print("true" if r["draft"] else "false")
elif a[:2] == ["release", "create"]:
    s["releases"].append({"id": s["next"], "tag_name": a[2], "draft": "--draft" in a, "body": open(opt("--notes-file")).read(), "assets": []}); s["next"] += 1; jsave(s)
elif a[:2] == ["release", "upload"]:
    r = find(s, a[2]); path = a[-1]; name = os.path.basename(path)
    if os.environ.get("FAKE_FAIL_UPLOAD") and os.environ["FAKE_FAIL_UPLOAD"] in name:
        if "--clobber" in a:
            r["assets"] = [x for x in r["assets"] if x["name"] != name]   # gh deletes first, then fails: the real behaviour
            jsave(s)
        sys.stderr.write("upload failed\n"); sys.exit(1)
    if any(x["name"] == name for x in r["assets"]) and "--clobber" not in a:
        sys.stderr.write("asset exists\n"); sys.exit(1)
    r["assets"] = [x for x in r["assets"] if x["name"] != name]
    d = sha(path)
    if os.environ.get("FAKE_CORRUPT") and os.environ["FAKE_CORRUPT"] in name:
        d = "0" * 64
    shutil.copyfile(path, "%s/f%d" % (db, s["next"]))
    r["assets"].append({"id": s["next"], "name": name, "digest": "sha256:" + d, "file": "f%d" % s["next"]}); s["next"] += 1; jsave(s)
elif a[:2] == ["release", "edit"]:
    r = find(s, a[2])
    if "--notes-file" in a: r["body"] = open(opt("--notes-file")).read()
    if "--draft=false" in a: r["draft"] = False
    jsave(s)
elif a[:2] == ["release", "download"]:
    r = find(s, a[2]); name = opt("-p"); d = opt("-D")
    x = next(x for x in r["assets"] if x["name"] == name)
    shutil.copyfile(db + "/" + x["file"], d + "/" + name)
elif a[0] == "api" and a[1] == "-X" and a[2] == "DELETE":
    aid = int(a[3].rsplit("/", 1)[1])
    for r in s["releases"]:
        r["assets"] = [x for x in r["assets"] if x["id"] != aid]
    jsave(s)
elif a[0] == "api" and a[1] == "-X" and a[2] == "PATCH":
    aid = int(a[3].rsplit("/", 1)[1]); new = a[a.index("-f") + 1].split("=", 1)[1]
    for r in s["releases"]:
        for x in r["assets"]:
            if x["id"] == aid: x["name"] = new
    jsave(s)
elif a[0] == "api" and "/releases?per_page" in a[1]:
    rl = [{"id": r["id"], "tag_name": r["tag_name"]} for r in s["releases"]]
    if os.environ.get("FAKE_PAGES"):          # gh api --paginate prints one JSON array per page, back to back
        for r in rl: print(json.dumps([r]))
    else:
        out(rl)
elif a[0] == "api" and "/releases/" in a[1]:
    rid = int(a[1].rsplit("/", 1)[1]); r = next(r for r in s["releases"] if r["id"] == rid)
    late = os.environ.get("FAKE_LATE_DIGEST") and not os.path.exists(db + "/late")   # the first look shows no digest yet
    if late and any(x["name"].startswith("new-") for x in r["assets"]): open(db + "/late", "w").write("1")
    out({"id": r["id"], "tag_name": r["tag_name"], "assets": [{"id": x["id"], "name": x["name"], "digest": (None if late and x["name"].startswith("new-") else x["digest"])} for x in r["assets"]]})
else:
    sys.stderr.write("fake gh: unknown %r\n" % a); sys.exit(2)
'''


def check(ok, what):
    print("%s  %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        FAILED.append(what)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def make_out(d, extra=None, files=None):
    os.makedirs(d, exist_ok=True)
    files = files or {"sources-v1-1of2.tar": b"part one", "sources-v1-2of2.tar": b"part two", "PACKAGES.txt": b"p\n",
                      "SOURCES.txt": b"S\tfoo\t1.0\t1\tx\t1\t1\tfoo\nS\tbar\t2.0\t2\ty\t1\t1\tbar\n", "NOTICES.txt": b"licenses"}
    files = dict(files)
    if extra:
        files.update(extra)
    for n, b in files.items():
        open(os.path.join(d, n), "wb").write(b)
    open(os.path.join(d, "SHA256SUMS"), "w").write("".join("%s  %s\n" % (sha(b), n) for n, b in sorted(files.items())))


def run(db, out, notes, *args, env=None):
    e = dict(os.environ, GH=os.path.join(db, "gh"), FAKE_DB=db, GITHUB_REPOSITORY="o/r", PUBLISH_SLEEP="0", DEFAULT_BRANCH="main")
    e.update(env or {})
    p = subprocess.run([sys.executable, "-I", TOOL, "sources-v1", out, notes] + list(args), capture_output=True, text=True, env=e)
    return p.returncode, p.stdout + p.stderr


def state(db):
    return json.load(open(db + "/state.json"))["releases"]


def assets(db):
    return {x["name"]: x["digest"] for x in state(db)[0]["assets"]}


def main():
    tmp = tempfile.mkdtemp(prefix="publishtest.")
    try:
        db = os.path.join(tmp, "db"); os.makedirs(db)
        open(os.path.join(db, "gh"), "w").write(FAKE); os.chmod(os.path.join(db, "gh"), 0o755)
        notes = os.path.join(tmp, "notes.md"); open(notes, "w").write("the offer\n")

        # a new release: draft, filled, then published
        out1 = os.path.join(tmp, "out1"); make_out(out1)
        rc, o = run(db, out1, notes)
        r = state(db)[0]
        check(rc == 0 and not r["draft"] and len(r["assets"]) == 6, "a new release is made, filled and published (%s)" % o.strip()[-50:])

        # a new release whose upload fails stays a draft
        db2 = os.path.join(tmp, "db2"); os.makedirs(db2); shutil.copy(os.path.join(db, "gh"), db2); os.chmod(os.path.join(db2, "gh"), 0o755)
        rc, o = run(db2, out1, notes, env={"FAKE_FAIL_UPLOAD": "2of2"})
        check(rc != 0 and state(db2)[0]["draft"] and "stays a draft" in o, "one file that will not upload fails the run and the release stays a draft")

        # a state check that cannot tell is not 'not there'
        db3 = os.path.join(tmp, "db3"); os.makedirs(db3); shutil.copy(os.path.join(db, "gh"), db3); os.chmod(os.path.join(db3, "gh"), 0o755)
        rc, o = run(db3, out1, notes, env={"FAKE_VIEW_ERROR": "1"})
        check(rc != 0 and not os.path.exists(db3 + "/state.json"), "a release lookup that fails for another reason stops the run instead of building a new release")
        db3b = os.path.join(tmp, "db3b"); os.makedirs(db3b); shutil.copy(os.path.join(db, "gh"), db3b); os.chmod(os.path.join(db3b, "gh"), 0o755)
        rc, o = run(db3b, out1, notes)
        # make the lookup fail after the release exists
        rc, o = run(db3b, out1, notes, "--again", env={"FAKE_VIEW_ERROR": "1"})
        check(rc != 0 and "cannot tell" in o, "again: a lookup that fails is not 'published'")

        before = dict(assets(db))

        # again: a fuller set replaces the published one
        out2 = os.path.join(tmp, "out2")
        make_out(out2, files={"sources-v1-1of3.tar": b"p1", "sources-v1-2of3.tar": b"p2", "sources-v1-3of3.tar": b"p3", "PACKAGES.txt": b"p2\n",
                              "SOURCES.txt": b"S\tfoo\t1.0\t1\tx\t1\t1\tfoo\nS\tbar\t2.0\t2\ty\t1\t1\tbar\nS\tshim\t15\t3\tz\t1\t1\tshim\n",
                              "NOTICES.txt": b"licenses v2", "firefox-1.source.tar.xz": b"ff"})
        rc, o = run(db, out2, notes, "--again")
        a = assets(db)
        want = {n: "sha256:" + sha(open(os.path.join(out2, n), "rb").read()) for n in os.listdir(out2) if n != "SHA256SUMS"}
        want["SHA256SUMS"] = "sha256:" + sha(open(os.path.join(out2, "SHA256SUMS"), "rb").read())
        check(rc == 0 and a == want, "again: the complete new set replaces the old one, and the files the new set does not have are removed (%s)" % o.strip()[-60:])
        check(not [n for n in a if n.startswith("new-")], "again leaves no temporary file behind")

        # again refused: fewer sources
        out3 = os.path.join(tmp, "out3")
        make_out(out3, files={"sources-v1-1of1.tar": b"x", "PACKAGES.txt": b"p\n", "SOURCES.txt": b"S\tfoo\t1.0\t1\tx\t1\t1\tfoo\n"})
        mid = dict(assets(db))
        rc, o = run(db, out3, notes, "--again")
        check(rc != 0 and "lacks" in o and assets(db) == mid, "again: a set with fewer source packages than the published one is refused and nothing is touched")

        # again: an upload fails (gh deletes the old asset first when clobbering: the new files go up under new-*, so the old ones are never at risk)
        out4 = os.path.join(tmp, "out4")
        make_out(out4, files={"sources-v1-1of3.tar": b"p1b", "sources-v1-2of3.tar": b"p2b", "sources-v1-3of3.tar": b"p3b", "PACKAGES.txt": b"p3\n",
                              "SOURCES.txt": b"S\tfoo\t1.0\t1\tx\t1\t1\tfoo\nS\tbar\t2.0\t2\ty\t1\t1\tbar\nS\tshim\t15\t3\tz\t1\t1\tshim\nS\tmore\t1\t3\tz\t1\t1\tmore\n",
                              "NOTICES.txt": b"licenses v3", "firefox-1.source.tar.xz": b"ff"})
        mid = dict(assets(db))
        rc, o = run(db, out4, notes, "--again", env={"FAKE_FAIL_UPLOAD": "2of3"})
        check(rc != 0 and assets(db) == mid, "again: a part that will not upload leaves the published release exactly as it was")
        rc, o = run(db, out4, notes, "--again", env={"FAKE_CORRUPT": "3of3"})
        check(rc != 0 and "digest" in o and assets(db) == mid, "again: an upload whose digest is not the file's is refused and nothing is touched")
        rc, o = run(db, out4, notes, "--again")
        check(rc == 0 and assets(db)["sources-v1-1of3.tar"] == "sha256:" + sha(b"p1b"), "again: and the next try, with nothing wrong, succeeds")

        # gh api --paginate prints one JSON array per page: a list of many releases is several documents
        st = json.load(open(db + "/state.json")); st["releases"].append({"id": 99, "tag_name": "v9", "draft": False, "body": "", "assets": []})
        json.dump(st, open(db + "/state.json", "w"))
        out5 = os.path.join(tmp, "out5")
        make_out(out5, files={"sources-v1-1of3.tar": b"p1c", "sources-v1-2of3.tar": b"p2c", "sources-v1-3of3.tar": b"p3c", "PACKAGES.txt": b"p4\n",
                              "SOURCES.txt": b"S\tfoo\t1.0\t1\tx\t1\t1\tfoo\nS\tbar\t2.0\t2\ty\t1\t1\tbar\nS\tshim\t15\t3\tz\t1\t1\tshim\nS\tmore\t1\t3\tz\t1\t1\tmore\n",
                              "NOTICES.txt": b"licenses v4", "firefox-1.source.tar.xz": b"ff"})
        rc, o = run(db, out5, notes, "--again", env={"FAKE_PAGES": "1"})
        check(rc == 0 and assets(db)["sources-v1-1of3.tar"] == "sha256:" + sha(b"p1c"), "again: a release list that comes in several pages is read whole (%s)" % o.strip()[-60:])

        out6 = os.path.join(tmp, "out6")
        make_out(out6, files={"sources-v1-1of3.tar": b"p1d", "sources-v1-2of3.tar": b"p2d", "sources-v1-3of3.tar": b"p3d", "PACKAGES.txt": b"p5\n",
                              "SOURCES.txt": b"S\tfoo\t1.0\t1\tx\t1\t1\tfoo\nS\tbar\t2.0\t2\ty\t1\t1\tbar\nS\tshim\t15\t3\tz\t1\t1\tshim\nS\tmore\t1\t3\tz\t1\t1\tmore\n",
                              "NOTICES.txt": b"licenses v5", "firefox-1.source.tar.xz": b"ff"})
        rc, o = run(db, out6, notes, "--again", env={"FAKE_LATE_DIGEST": "1"})
        check(rc == 0 and assets(db)["sources-v1-1of3.tar"] == "sha256:" + sha(b"p1d"), "again: a digest GitHub has not shown yet is waited for (%s)" % o.strip()[-60:])

        # again on something that is not published
        db5 = os.path.join(tmp, "db5"); os.makedirs(db5); shutil.copy(os.path.join(db, "gh"), db5); os.chmod(os.path.join(db5, "gh"), 0o755)
        run(db5, out1, notes, env={"FAKE_FAIL_UPLOAD": "2of2"})
        rc, o = run(db5, out1, notes, "--again")
        check(rc != 0 and "not" in o, "again refuses a release that is only a draft")
        rc, o = subprocess.run([sys.executable, "-I", TOOL, "evil name", out1, notes], capture_output=True, text=True,
                               env=dict(os.environ, GITHUB_REPOSITORY="o/r", GH=os.path.join(db, "gh"), FAKE_DB=db)).returncode, ""
        check(rc != 0, "an odd release name is refused")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("\n%s" % ("all passed" if not FAILED else "%d FAILED: %s" % (len(FAILED), "; ".join(FAILED))))
    return 1 if FAILED else 0


MUTANTS = [
    ("the new set is swapped in without checking the digests", "if wrong:", "if False:"),
    ("an upload goes up over the old name (the old part is deleted first)", "os.path.join(stage, \"new-\" + n), clobber=False)", "os.path.join(outdir, n), clobber=True)"),
    ("a set with fewer sources is accepted", "if lost:", "if False:"),
    ("a lookup error means 'not there'", 'raise SystemExit("publish: cannot tell whether %s exists: %s" % (tag, (p.stderr or p.stdout).strip()[:200]))', 'return "none"'),
    ("a failed upload still publishes the draft", "if bad:", "if False:"),
    ("a digest GitHub has not shown yet is taken as wrong", "for attempt in range(6):", "for attempt in range(1):"),
    ("the release list is read as one JSON document", "obj, i = dec.raw_decode(t, i)", "obj, i = json.loads(t), len(t)"),
]


def mutants():
    bad = 0
    src = open(TOOL).read()
    for what, old, new in MUTANTS:
        if old not in src:
            print("FAIL  mutant text not found: %s" % what); bad += 1; continue
        d = tempfile.mkdtemp(prefix="publishmutant.")
        try:
            t = os.path.join(d, "publish.py"); open(t, "w").write(src.replace(old, new, 1))
            p = subprocess.run([sys.executable, "-I", os.path.abspath(__file__)], capture_output=True, text=True, env=dict(os.environ, PUBLISH_TOOL=t))
            red = p.returncode != 0
            print("%s  mutant: %s -> suite %s" % ("ok  " if red else "FAIL", what, "red" if red else "STILL GREEN"))
            bad += 0 if red else 1
        finally:
            shutil.rmtree(d, ignore_errors=True)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(mutants() if "--mutants" in sys.argv else main())
