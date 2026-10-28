#!/usr/bin/env python3
"""Rehearse playbooks 1 and 3a of docs/ops/INCIDENTS.md for real, on a throwaway prerelease.

    python3 -I tools/ops/drill.py          (needs gh logged in, gpg; .github/workflows/drill.yml runs it)

Makes the prerelease `drill-<run>-<time>` with a fake installer, SHA256SUMS, pieces.txt and a signed
fake update index, then:
  A. swaps the installer for another file  -> the monitor must go red; an issue must open, assigned to
     the owner; the playbook's repair (`gh release upload --clobber`) must turn it green; the issue closes.
  B. deletes the signed index (the halt of playbook 3a) -> the monitor's channel check must go red; an
     issue opens; republishing turns it green; the issue closes.
Prints how long each took. The restore must take under 30 minutes (it takes about a minute: the
monitor's detection, which is every 15 minutes in real life, is the long part, and is reported
separately). Only touches the tag it made, and removes it (and the drill issues) at the end.
Real alerts are never touched: the drill's issues use the label and titles `drill: ...`.
"""
import json, os, re, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import monitor  # noqa: E402

R = os.environ.get("GITHUB_REPOSITORY") or subprocess.run(["sh", os.path.join(HERE, "..", "repo.sh")], capture_output=True, text=True, check=True).stdout.strip()
TAG = f"drill-{os.environ.get('GITHUB_RUN_ID', '0')}-{int(time.time())}"
assert re.fullmatch(r"drill-\d+-\d+", TAG)
os.environ.update(GITHUB_REPOSITORY=R, MON_ISSUE_PREFIX="drill", MON_FAST="1")


def sh(*a, check=True, **kw):
    return subprocess.run(a, check=check, capture_output=True, text=True, **kw)


def gh(*a, check=True):
    return sh("gh", *a, check=check).stdout


def until(pred, timeout=600, every=5):
    t = time.time()
    while time.time() - t < timeout:
        if pred():
            return time.time() - t
        time.sleep(every)
    raise SystemExit(f"drill: timed out after {timeout}s waiting for {pred.__doc__}")


def main():
    d = tempfile.mkdtemp()
    key = os.path.join(d, "k"); os.mkdir(key, 0o700)
    sh("gpg", "--batch", "--homedir", key, "--passphrase", "", "--quick-gen-key", "AurOS drill", "ed25519", "sign", "1d")
    asc = os.path.join(d, "keyring.asc")
    open(asc, "wb").write(sh("gpg", "--batch", "--homedir", key, "--armor", "--export").stdout.encode())

    def sign():
        open(os.path.join(d, "Release"), "w").write("Origin: AurOS drill\nSuite: stable\nDate: Thu, 22 Oct 2026 00:00:00 UTC\nValid-Until: Tue, 01 Dec 2099 00:00:00 UTC\n")
        sh("gpg", "--batch", "--homedir", key, "--yes", "--clearsign", "-o", os.path.join(d, "InRelease"), os.path.join(d, "Release"))
        sh("gpg", "--batch", "--homedir", key, "--yes", "--detach-sign", "-o", os.path.join(d, "Release.gpg"), os.path.join(d, "Release"))

    good, other = os.path.join(d, "AurOS-Setup.exe"), os.path.join(d, "other", "AurOS-Setup.exe")
    os.mkdir(os.path.dirname(other))
    open(good, "wb").write(os.urandom(1_300_000)); open(other, "wb").write(os.urandom(1_300_000))
    piece = os.path.join(d, "auros-desktop.img.gz.000"); open(piece, "wb").write(os.urandom(200_000))
    import hashlib
    h = lambda p: hashlib.sha256(open(p, "rb").read()).hexdigest()
    open(os.path.join(d, "SHA256SUMS"), "w").write(f"{h(good)}  AurOS-Setup.exe\n{h(piece)}  auros-desktop.img.gz.000\n")
    base = f"https://github.com/{R}/releases/download/{TAG}/"
    open(os.path.join(d, "pieces.txt"), "w").write(f"# drill\nbase {base}\npiece auros-desktop.img.gz.000 {os.path.getsize(piece)} {h(piece)}\n")
    sign()
    live = os.path.join(d, "live.txt"); open(live, "w").write("download pieces channel\n")
    env = dict(os.environ, MON_GET=base + "AurOS-Setup.exe", MON_APT=base, MON_KEYRING=asc, MON_LIVE_FILE=live,
               MON_SITE="http://127.0.0.1:9/", MON_WISHES="", AUROS_DOMAIN="", MON_GATE="")
    files = [os.path.join(d, n) for n in ("AurOS-Setup.exe", "SHA256SUMS", "pieces.txt", "auros-desktop.img.gz.000", "InRelease", "Release", "Release.gpg")]
    rows = []

    def states():
        return {r["check"]: r["state"] for r in monitor.run(True, env)}

    def red(check):
        """a live check is red"""
        return states().get(check) == "fail"
    def green_all():
        """every live check is green"""
        return all(states()[c] == "ok" for c in ("download", "pieces", "channel"))

    def issues_of(check):
        # newest first. GitHub's issue lists lag a few seconds behind a creation or a close (both the
        # search and the label filter): never read a list right after a write, poll it.
        j = json.loads(gh("issue", "list", "-R", R, "--label", "drill", "--state", "all", "--limit", "50", "--json", "number,title,state,assignees"))
        return sorted((i for i in j if i["title"] == f"drill: {check} is down"), key=lambda i: -i["number"])

    def open_assigned(check):
        """the drill's issue for this check is open and assigned"""
        i = issues_of(check)
        return i[0] if i and i[0]["state"] == "OPEN" and i[0]["assignees"] else None

    def closed(check):
        """the drill's issue for this check is closed"""
        i = issues_of(check)
        return bool(i) and i[0]["state"] == "CLOSED"

    def until_value(fn, timeout=180, every=3):
        t = time.time()
        while time.time() - t < timeout:
            v = fn()
            if v:
                return v
            time.sleep(every)
        raise SystemExit(f"drill: timed out after {timeout}s waiting for {fn.__doc__}")

    try:
        gh("release", "create", TAG, "-R", R, "--prerelease", "--title", f"drill {TAG} (throwaway, deleted by the drill)",
           "--notes", "A rehearsal of docs/ops/INCIDENTS.md. Not an AurOS release. Deleted when the drill ends.", *files)
        print(f"drill: made {base}")
        until(green_all, 300); print("drill: baseline green (download with the hash compared, pieces, the signed index)")

        # A. a bad installer
        t_break = time.time(); gh("release", "upload", TAG, other, "-R", R, "--clobber")
        detect = until(lambda: red("download"), 600, 5)
        monitor.issues(monitor.run(True, env))
        i = until_value(lambda: [open_assigned("download")] if open_assigned("download") else None)
        paged = time.time() - t_break
        t_fix = time.time(); gh("release", "upload", TAG, good, "-R", R, "--clobber")
        until(green_all, 900, 5); restore = time.time() - t_fix
        monitor.issues(monitor.run(True, env)); until(lambda: closed("download"), 180, 3)
        rows.append(("A. the installer is swapped (playbook 1/2)", detect, paged, restore, f"assigned to {i[0]['assignees'][0]['login']}"))

        # B. the signed index is taken away (playbook 3a halt), then put back
        t_break = time.time()
        for n in ("InRelease", "Release", "Release.gpg"):
            gh("release", "delete-asset", TAG, n, "-R", R, "-y")
        detect = until(lambda: red("channel"), 600, 5)
        monitor.issues(monitor.run(True, env))
        i = until_value(lambda: [open_assigned("channel")] if open_assigned("channel") else None)
        paged = time.time() - t_break
        t_fix = time.time(); gh("release", "upload", TAG, *[os.path.join(d, n) for n in ("InRelease", "Release", "Release.gpg")], "-R", R)
        until(green_all, 900, 5); restore = time.time() - t_fix
        monitor.issues(monitor.run(True, env)); until(lambda: closed("channel"), 180, 3)
        rows.append(("B. the signed index is removed, then put back (playbook 3a)", detect, paged, restore, f"assigned to {i[0]['assignees'][0]['login']}"))
    finally:
        # only the tag this run made
        if re.fullmatch(r"drill-\d+-\d+", TAG):
            gh("release", "delete", TAG, "-R", R, "--cleanup-tag", "-y", check=False)
        for _ in range(8):  # an issue made seconds ago may not be listed yet
            for i in json.loads(gh("issue", "list", "-R", R, "--label", "drill", "--state", "open", "--json", "number", check=False) or "[]"):
                gh("issue", "close", str(i["number"]), "-R", R, "--comment", "the drill ended", check=False)
            time.sleep(4)

    out = ["| what was broken | monitor went red after (s) | the issue was open and assigned after (s) | repaired and green after (s) | who it was assigned to |", "|---|---|---|---|---|"]
    out += [f"| {a} | {b:.0f} | {c:.0f} | {d_:.0f} | {e} |" for a, b, c, d_, e in rows]
    worst = max(r[3] for r in rows)
    out += ["", f"Slowest repair: {worst:.0f} s. Target: under 1800 s ({'met' if worst < 1800 else 'MISSED'}).",
            "In real life detection waits for the monitor's 15-minute cron, so add up to 15 minutes to the first column.",
            f"Throwaway tag `{TAG}` was removed."]
    print("\n".join(out))
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        open(os.environ["GITHUB_STEP_SUMMARY"], "a").write("### Drill\n" + "\n".join(out) + "\n")
    return 0 if worst < 1800 else 1


if __name__ == "__main__":
    sys.exit(main())
