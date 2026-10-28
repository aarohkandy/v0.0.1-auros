#!/usr/bin/env python3
"""publish.py -- put a made sources release on GitHub without ever losing a good one.

    python3 -I tools/legal/publish.py TAG OUTDIR NOTES_FILE [--again]

TAG is the sources release's name (sources-v0.1.0-618726b); OUTDIR is what `build/sources make` wrote;
NOTES_FILE is the release's text. Needs `gh`, GITHUB_REPOSITORY, and DEFAULT_BRANCH for a new release.
(GH names another program in the tests: tools/legal/publishtest.py plays GitHub.)

  new release   a DRAFT is made and filled, every file uploaded with retries (SHA256SUMS last); one file that
                will not go up fails the run and the draft stays a draft; only a complete draft is published
                (as a prerelease: every release is one until [latest]).
  --again       the release is PUBLISHED and is being replaced by a fuller set. Nothing of the old release is
                touched until the new set is complete and its digests are right:
                  1. refuse if the new SOURCES.txt lacks a source package the published one has (never fewer);
                  2. upload every file under the name new-<name> (no existing asset is replaced or deleted);
                  3. compare GitHub's own sha256 of each upload with SHA256SUMS; one wrong: delete the uploads,
                     leave the release as it was, fail;
                  4. only then, file by file (SHA256SUMS last): delete the old asset, rename new-<name> to <name>;
                  5. remove assets the new set does not have; refresh the notes.
                A crash in step 4 leaves new-* files beside the old ones, and the next run deletes the strays and starts again.
Exit status is not zero on anything wrong, with a sentence.
"""
import hashlib, json, os, re, shutil, subprocess, sys, tempfile, time

GH = os.environ.get("GH", "gh")
REPO = os.environ.get("GITHUB_REPOSITORY", "")
SLEEP = float(os.environ.get("PUBLISH_SLEEP", "15"))


def gh(*args, check=True):
    p = subprocess.run([GH] + list(args), capture_output=True, text=True)
    if check and p.returncode != 0:
        raise SystemExit("publish: gh %s: %s" % (" ".join(args[:3]), (p.stderr or p.stdout).strip()[:300]))
    return p


def sha256_file(p):
    h = hashlib.sha256()
    with open(p, "rb") as fh:
        for b in iter(lambda: fh.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def state(tag):
    """none | draft | published. Only 'release not found' means none; any other error stops the run:
    rebuilding into a release that exists but could not be read would overwrite it."""
    p = gh("release", "view", tag, "--repo", REPO, "--json", "isDraft", "--jq", ".isDraft", check=False)
    if p.returncode == 0:
        return "draft" if p.stdout.strip() == "true" else "published"
    if "release not found" in (p.stderr + p.stdout).lower():
        return "none"
    raise SystemExit("publish: cannot tell whether %s exists: %s" % (tag, (p.stderr or p.stdout).strip()[:200]))


def release_json(tag):
    """The release's REST object (assets with id, name, digest); drafts are found in the list."""
    p = gh("api", "repos/%s/releases?per_page=100" % REPO, "--paginate")
    # --paginate prints one JSON array per page, one after the other: read them one at a time
    rels, dec, i, t = [], json.JSONDecoder(), 0, p.stdout
    while True:
        while i < len(t) and t[i].isspace():
            i += 1
        if i >= len(t):
            break
        obj, i = dec.raw_decode(t, i)
        rels += obj if isinstance(obj, list) else [obj]
    for r in rels:
        if r.get("tag_name") == tag:
            return gh_json("repos/%s/releases/%d" % (REPO, r["id"]))
    raise SystemExit("publish: release %s not in the list" % tag)


def gh_json(path):
    return json.loads(gh("api", path).stdout)


def upload(tag, path, clobber=True):
    for i in range(1, 5):
        p = gh("release", "upload", tag, "--repo", REPO, *(["--clobber"] if clobber else []), path, check=False)
        if p.returncode == 0:
            return True
        time.sleep(i * SLEEP)
    return False


def sums_of(outdir):
    out = {}
    for l in open(os.path.join(outdir, "SHA256SUMS")):
        if l.strip():
            h, _, n = l.rstrip("\n").partition("  ")
            out[n] = h
    return out


def srows(path):
    rows = set()
    for l in open(path, encoding="utf-8"):
        if l.startswith("S\t"):
            f = l.split("\t")
            rows.add((f[1], f[2]))
    return rows


def publish_new(tag, outdir, notes):
    st = state(tag)
    if st == "published":
        raise SystemExit("publish: %s is published; a first publish never writes into a published release (use --again)" % tag)
    if st == "none":
        gh("release", "create", tag, "--repo", REPO, "--target", os.environ.get("DEFAULT_BRANCH", "main"),
           "--draft", "--prerelease", "--title", "AurOS %s: source code" % tag.replace("sources-", "", 1), "--notes-file", notes)
    names = sorted(f for f in os.listdir(outdir) if os.path.isfile(os.path.join(outdir, f)) and f != "SHA256SUMS") + ["SHA256SUMS"]
    bad = [n for n in names if not upload(tag, os.path.join(outdir, n))]
    if bad:
        raise SystemExit("publish: could not upload %s after four tries: %s stays a draft" % (", ".join(bad), tag))
    gh("release", "edit", tag, "--repo", REPO, "--notes-file", notes, "--draft=false", "--prerelease")
    print("published %s: %d files" % (tag, len(names)))


def publish_again(tag, outdir, notes):
    if state(tag) != "published":
        raise SystemExit("publish: --again is for a published release; %s is not" % tag)
    sums = sums_of(outdir)
    # 1. never fewer
    tmp = tempfile.mkdtemp(prefix="publish.")
    try:
        gh("release", "download", tag, "--repo", REPO, "-p", "SOURCES.txt", "-D", tmp, "--clobber")
        old = srows(os.path.join(tmp, "SOURCES.txt"))
        new = srows(os.path.join(outdir, "SOURCES.txt"))
        lost = sorted(old - new)
        if lost:
            raise SystemExit("publish: the new set lacks %d source package(s) the published release has (%s): refused"
                             % (len(lost), ", ".join("%s %s" % x for x in lost[:5])))
        # 2. strays of a crashed run go; the new set goes up under temporary names
        rel = release_json(tag)
        for a in rel["assets"]:
            if a["name"].startswith("new-"):
                gh("api", "-X", "DELETE", "repos/%s/releases/assets/%d" % (REPO, a["id"]))
        stage = os.path.join(tmp, "stage"); os.makedirs(stage)
        names = sorted(f for f in os.listdir(outdir) if os.path.isfile(os.path.join(outdir, f)) and f != "SHA256SUMS") + ["SHA256SUMS"]
        for n in names:
            try:
                os.link(os.path.join(outdir, n), os.path.join(stage, "new-" + n))
            except OSError:
                shutil.copyfile(os.path.join(outdir, n), os.path.join(stage, "new-" + n))
        def cleanup():
            for a in release_json(tag)["assets"]:
                if a["name"].startswith("new-"):
                    gh("api", "-X", "DELETE", "repos/%s/releases/assets/%d" % (REPO, a["id"]), check=False)
        failed = [n for n in names if not upload(tag, os.path.join(stage, "new-" + n), clobber=False)]
        if failed:
            cleanup()
            raise SystemExit("publish: could not upload %s: the published release is as it was" % ", ".join(failed))
        # 3. GitHub's own digests against ours
        want = dict(sums); want["SHA256SUMS"] = sha256_file(os.path.join(outdir, "SHA256SUMS"))
        for attempt in range(6):          # GitHub may take a moment to show a digest it has just computed
            by = {a["name"]: a for a in release_json(tag)["assets"]}
            if all((by.get("new-" + n) or {}).get("digest") for n in names):
                break
            time.sleep(SLEEP)
        wrong = [n for n in names if (by.get("new-" + n) or {}).get("digest") != "sha256:" + want[n]]
        if wrong:
            cleanup()
            raise SystemExit("publish: GitHub's digest of %s is not the file's: the published release is as it was" % ", ".join(wrong))
        # 4. swap, SHA256SUMS last
        for n in [x for x in names if x != "SHA256SUMS"] + ["SHA256SUMS"]:
            by = {a["name"]: a for a in release_json(tag)["assets"]}
            if n in by:
                gh("api", "-X", "DELETE", "repos/%s/releases/assets/%d" % (REPO, by[n]["id"]))
            gh("api", "-X", "PATCH", "repos/%s/releases/assets/%d" % (REPO, by["new-" + n]["id"]), "-f", "name=" + n)
        # 5. what the new set does not have
        for a in release_json(tag)["assets"]:
            if a["name"] not in names:
                print("removing %s, which the new set does not have" % a["name"])
                gh("api", "-X", "DELETE", "repos/%s/releases/assets/%d" % (REPO, a["id"]))
        gh("release", "edit", tag, "--repo", REPO, "--notes-file", notes, "--prerelease")
        print("made %s again: %d files" % (tag, len(names)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv):
    if len(argv) < 3 or not REPO:
        print(__doc__); return 2
    tag, outdir, notes = argv[:3]
    if not re.fullmatch(r"sources-v[0-9][0-9A-Za-z.+-]*", tag):
        raise SystemExit("publish: odd release name %r" % tag)
    if "--again" in argv:
        publish_again(tag, outdir, notes)
    else:
        publish_new(tag, outdir, notes)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
