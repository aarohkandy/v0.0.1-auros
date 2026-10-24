#!/usr/bin/env python3
"""releasegate -- may this build run be published as a release?

    python3 -I tools/releasegate.py JOBS ALLOW [TODAY] [--latest]   JOBS: lines "name|conclusion"
    python3 -I tools/releasegate.py harden ROWS RUN-ID   did the hardening boot of THIS build's image pass?
    python3 -I tools/releasegate.py e2e JOBS             did a real Windows install THIS AurOS-Setup.exe, base AND bitlocker?
    python3 -I tools/releasegate.py selftest             each rule refuses what it exists to refuse

JOBS is the build run's job list (release.yml's `which` job fetches it
from the Actions API); ALLOW is .github/ci-allow.txt; TODAY is a UTC date
(default: today). Exit 0 = publishable, 1 = refused, with the reasons.

The rules (docs/safety/FINDINGS.md S-11 is why the power-cut ones are here):
  1. `build` succeeded.
  2. `tests / unit` and `tests / wl (checks)` (the compositor's tests, WL2) succeeded,
     at least 5 `tests / e2e (...)` jobs ran, and EVERY `tests / ...` job succeeded.
  3. At least 4 `powercut / e2e (...)` jobs ran (the power cut in the
     mode that ships, tests.yml suite "nostick"), and every one of them
     succeeded -- or failed AND is on the allow-list with an expiry date
     that has not passed. The allow-list is the only way a failing
     power-cut job is let through: a named job, a reason, a date. A date
     in the past counts as no entry at all.
  Nothing else can be allow-listed: a failing build, unit or gating test
  always refuses. And with --latest (the release is to become the full
  release the website serves) an allow-list entry excuses nothing: a
  failure the allow-list tolerates lets a PRERELEASE out, never the
  download button.

  4. (--latest only, `harden`) the hardening boot test (harden.yml, job
     `boot`, tools/hardenboottest.sh) of THAT build run's image passed.
     ROWS are harden runs, "created_at|event|title|boot" (tools/hardengate.sh
     fetches them); a run counts only if its title is "harden of build run
     RUN-ID" (harden.yml's run-name) and its event is workflow_run or
     workflow_dispatch. The newest one with a verdict (boot success or
     failure) decides; skipped, cancelled, running, none or "partial" (the
     job passed but hardenboottest or timeboottest did not run: a dispatch
     with only=time/hardening; tools/hardengate.sh) is no verdict, and no
     verdict refuses. A red re-run to green is a pass.

  5. (--latest only, `e2e`) the real-Windows journey (e2e.yml, called by
     release.yml as job `e2e`) of the release's own AurOS-Setup.exe passed in
     the variants base AND bitlocker (D13): JOBS are the run's "name|conclusion"
     lines; `e2e / e2e (base)` and `e2e / e2e (bitlocker)` must both be
     success, and no other `e2e / e2e (...)` job may have failed. The fast
     path's jobs (`e2e-fastpath / ...`) never count for this.

ALLOW lines:  job name | expires YYYY-MM-DD (UTC, inclusive) | why
e.g.          powercut / e2e (powercut-nostick-early)|2026-10-30|S-03: ...
"""
import datetime, re, sys

MIN_E2E = 5
MIN_POWERCUT = 4


def load_allow(text):
    out = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 3 or not re.fullmatch(r"\d{4}-\d{2}-\d{2}", parts[1]):
            raise ValueError("bad allow-list line: %r (want: job name | YYYY-MM-DD | why)" % line)
        out[parts[0]] = (parts[1], parts[2])
    return out


def judge(jobs_text, allow, today, latest=False):
    """-> (refusals, notes)"""
    jobs = {}
    for line in jobs_text.splitlines():
        if "|" in line:
            name, conc = line.rsplit("|", 1)
            jobs[name.strip()] = conc.strip()
    why, notes = [], []
    if jobs.get("build") != "success":
        why.append("build: %s" % jobs.get("build", "did not run"))
    if jobs.get("tests / unit") != "success":
        why.append("tests / unit: %s" % jobs.get("tests / unit", "did not run"))
    if jobs.get("tests / wl (checks)") != "success":      # the compositor's tests are a gate (W16): they must have RUN
        why.append("tests / wl (checks): %s" % jobs.get("tests / wl (checks)", "did not run"))
    tests = {n: c for n, c in jobs.items() if n.startswith("tests / ")}
    e2e = [n for n in tests if re.match(r"tests / e2e \(", n)]
    if len([n for n in e2e if tests[n] == "success"]) < MIN_E2E:
        why.append("fewer than %d tests / e2e jobs succeeded" % MIN_E2E)
    for n, c in sorted(tests.items()):
        if c != "success":
            why.append("%s: %s" % (n, c))
    pcs = {n: c for n, c in jobs.items() if n.startswith("powercut / e2e (")}
    if len(pcs) < MIN_POWERCUT:
        why.append("the power cut in the shipped mode ran %d job(s), not %d (powercut / e2e (...))" % (len(pcs), MIN_POWERCUT))
    for n, c in sorted(pcs.items()):
        if c == "success":
            continue
        entry = allow.get(n)
        if c == "failure" and entry and latest:
            why.append("%s: failed; an allow-list entry (%s) lets a prerelease out, never the full release" % (n, entry[1]))
        elif c == "failure" and entry and entry[0] >= today:
            notes.append("ALLOWED TO FAIL until %s: %s (%s)" % (entry[0], n, entry[1]))
        elif c == "failure" and entry:
            why.append("%s: failed, and its allow-list entry expired on %s" % (n, entry[0]))
        else:
            why.append("%s: %s" % (n, c))
    return why, notes


E2E_VARIANTS = ("base", "bitlocker")


def judge_e2e(jobs_text):
    """-> refusals. Both variants of the legacy installer's journey, nothing else counts."""
    jobs = {}
    for line in jobs_text.splitlines():
        if "|" in line:
            name, conc = line.rsplit("|", 1)
            jobs[name.strip()] = conc.strip()
    why = []
    for v in E2E_VARIANTS:
        c = jobs.get("e2e / e2e (%s)" % v)
        if c != "success":
            why.append("no passing real-Windows journey of AurOS-Setup.exe in the %s variant (e2e / e2e (%s): %s)" % (v, v, c or "did not run"))
    for n, c in sorted(jobs.items()):
        if n.startswith("e2e / e2e (") and c != "success" and n[len("e2e / e2e ("):-1] not in E2E_VARIANTS:
            why.append("%s: %s" % (n, c))
    return why


def judge_harden(rows_text, rid):
    """-> refusals. The newest harden run of build run RID with a verdict decides."""
    title = "harden of build run %s" % rid
    mine = []
    for line in rows_text.splitlines():
        p = line.split("|")
        if len(p) == 4 and p[1] in ("workflow_run", "workflow_dispatch") and p[2].strip() == title:
            mine.append((p[0].strip(), p[3].strip()))
    if not mine:
        return ["no harden run of build run %s (harden.yml starts one when a build finishes; a [latest] waits for it: "
                "push [release], let harden finish, then [publish %s] [latest])" % (rid, rid)]
    for _, boot in sorted(mine, reverse=True):
        if boot == "success":
            return []
        if boot == "failure":
            return ["the hardening boot test of build run %s's image FAILED (harden.yml, job boot)" % rid]
    return ["the hardening boot test of build run %s's image has no verdict yet (%s; \"partial\" is a run that skipped "
            "hardenboottest or timeboottest: dispatch harden with only=all)" % (rid, ", ".join(b for _, b in mine))]


def selftest():
    h = {"r": "2026-10-24T01:00:00Z", "o": "2026-10-24T00:00:00Z"}
    def hc(what, rows, want_refused, rid="900"):
        nonlocal bad
        got = bool(judge_harden("\n".join(rows), rid))
        print("    %-62s %s" % (what, "ok" if got == want_refused else "FAIL"))
        bad += got != want_refused
    bad = 0
    def ec(what, jobs, want_refused):
        nonlocal bad
        got = bool(judge_e2e("\n".join(jobs)))
        print("    %-62s %s" % (what, "ok" if got == want_refused else "FAIL"))
        bad += got != want_refused
    ok2 = ["e2e / gate|success", "e2e / e2e (base)|success", "e2e / e2e (bitlocker)|success", "e2e-fastpath / e2e (base)|success"]
    ec("base and bitlocker both green pass", ok2, False)
    ec("base only is refused", [j for j in ok2 if "bitlocker" not in j], True)
    ec("bitlocker only is refused", [j for j in ok2 if "(base)|success" not in j or "fastpath" in j], True)
    ec("bitlocker red is refused", [j.replace("bitlocker)|success", "bitlocker)|failure") for j in ok2], True)
    ec("bitlocker skipped is refused", [j.replace("bitlocker)|success", "bitlocker)|skipped") for j in ok2], True)
    ec("base cancelled is refused", [j.replace("e2e / e2e (base)|success", "e2e / e2e (base)|cancelled") for j in ok2], True)
    ec("the fast path's two green jobs do not stand in", ["e2e-fastpath / e2e (base)|success", "e2e-fastpath / e2e (bitlocker)|success"], True)
    ec("a third variant failing is refused", ok2 + ["e2e / e2e (hdd)|failure"], True)
    ec("no e2e jobs at all is refused", [], True)
    t = "harden of build run 900"
    hc("a green hardening boot of this build passes", [h["r"] + "|workflow_run|" + t + "|success"], False)
    hc("a red hardening boot of this build is refused", [h["r"] + "|workflow_run|" + t + "|failure"], True)
    hc("no harden run of this build is refused", [], True)
    hc("a green boot of ANOTHER build is refused", [h["r"] + "|workflow_run|harden of build run 901|success"], True)
    hc("a green push-event run with this title is refused", [h["r"] + "|push|" + t + "|success"], True)
    hc("a skipped boot is no verdict: refused", [h["r"] + "|workflow_run|" + t + "|skipped"], True)
    hc("a cancelled boot is no verdict: refused", [h["r"] + "|workflow_run|" + t + "|cancelled"], True)
    hc("a boot still running is refused", [h["r"] + "|workflow_run|" + t + "|in_progress"], True)
    hc("red, then re-run green (newer) passes", [h["o"] + "|workflow_run|" + t + "|failure", h["r"] + "|workflow_dispatch|" + t + "|success"], False)
    hc("green, then red (newer) is refused", [h["o"] + "|workflow_run|" + t + "|success", h["r"] + "|workflow_dispatch|" + t + "|failure"], True)
    hc("a newer cancelled run does not hide an older red", [h["o"] + "|workflow_run|" + t + "|failure", h["r"] + "|workflow_run|" + t + "|cancelled"], True)
    hc("a partial run (one of the two tests skipped) is no verdict: refused", [h["r"] + "|workflow_dispatch|" + t + "|partial"], True)
    hc("a newer partial run does not override an older red", [h["o"] + "|workflow_run|" + t + "|failure", h["r"] + "|workflow_dispatch|" + t + "|partial"], True)
    hc("a newer partial run does not undo an older full green", [h["o"] + "|workflow_run|" + t + "|success", h["r"] + "|workflow_dispatch|" + t + "|partial"], False)
    hc("run 90 is not run 900", [h["r"] + "|workflow_run|harden of build run 9000|success"], True)
    green = "\n".join(
        ["build|success", "tests / unit|success"]
        + ["tests / e2e (t%d)|success" % i for i in range(7)]
        + ["tests / wl (checks)|success", "tests / wl (mutants-1)|success"]
        + ["powercut / e2e (p%d)|success" % i for i in range(4)])
    today = "2026-10-24"
    allow = {"powercut / e2e (p2)": ("2026-10-30", "S-03"), "powercut / e2e (p3)": ("2026-10-20", "old")}

    def case(what, jobs, allow_, want_refused, latest=False):
        nonlocal bad
        why, _ = judge(jobs, allow_, today, latest)
        got = bool(why)
        print("    %-62s %s" % (what, "ok" if got == want_refused else "FAIL"))
        bad += got != want_refused

    case("everything green passes", green, {}, False)
    case("a failing power-cut job is refused", green.replace("p1)|success", "p1)|failure"), {}, True)
    case("...also when it is cancelled", green.replace("p1)|success", "p1)|cancelled"), {}, True)
    case("a failing power-cut job on the allow-list passes", green.replace("p2)|success", "p2)|failure"), allow, False)
    case("...but never for the full release (--latest)", green.replace("p2)|success", "p2)|failure"), allow, True, latest=True)
    case("...and a green full release passes", green, allow, False, latest=True)
    case("...but not once its entry has expired", green.replace("p3)|success", "p3)|failure"), allow, True)
    case("...and an allow-list entry never excuses a cancelled job", green.replace("p2)|success", "p2)|cancelled"), allow, True)
    case("no power-cut jobs at all is refused", "\n".join(l for l in green.splitlines() if not l.startswith("powercut")), {}, True)
    case("only three power-cut jobs is refused", "\n".join(l for l in green.splitlines() if "(p3)" not in l), {}, True)
    case("a failing gating test is refused, allow-list or not",
         green.replace("t3)|success", "t3)|failure"), {"tests / e2e (t3)": ("2099-01-01", "x")}, True)
    case("a failing unit job is refused", green.replace("tests / unit|success", "tests / unit|failure"), {}, True)
    case("a compositor test that did not run is refused", "\n".join(l for l in green.splitlines() if "wl (checks)" not in l), {}, True)
    case("a failing compositor test is refused", green.replace("wl (checks)|success", "wl (checks)|failure"), {}, True)
    case("a compositor mutant that survived is refused", green.replace("wl (mutants-1)|success", "wl (mutants-1)|failure"), {}, True)
    case("a failing build is refused", green.replace("build|success", "build|failure"), {}, True)
    case("too few gating e2e jobs is refused", "\n".join(l for l in green.splitlines() if not l.endswith("(t0)|success") and not l.endswith("(t1)|success") and not l.endswith("(t2)|success")), {}, True)
    try:
        load_allow("powercut / e2e (p2)|soon|S-03")
        print("    %-62s FAIL" % "a malformed allow-list line is rejected"); bad += 1
    except ValueError:
        print("    %-62s ok" % "a malformed allow-list line is rejected")
    print("releasegate selftest: %s" % ("ok" if not bad else "%d wrong" % bad))
    return 1 if bad else 0


def main(argv):
    if len(argv) > 1 and argv[1] == "selftest":
        return selftest()
    if len(argv) > 1 and argv[1] == "e2e":
        if len(argv) != 3:
            print(__doc__, file=sys.stderr)
            return 2
        why = judge_e2e(open(argv[2], encoding="utf-8").read())
        for w in why:
            print("releasegate: REFUSED:", w, file=sys.stderr)
        if not why:
            print("releasegate: ok, a real Windows installed this AurOS-Setup.exe in both variants (base, bitlocker)")
        return 1 if why else 0
    if len(argv) > 1 and argv[1] == "harden":
        if len(argv) != 4:
            print(__doc__, file=sys.stderr)
            return 2
        why = judge_harden(open(argv[2], encoding="utf-8").read(), argv[3])
        for w in why:
            print("releasegate: REFUSED:", w, file=sys.stderr)
        if not why:
            print("releasegate: ok, the hardening boot of build run %s's image passed" % argv[3])
        return 1 if why else 0
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    latest = "--latest" in argv
    rest = [a for a in argv[1:] if a != "--latest"]
    today = rest[2] if len(rest) > 2 else datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")
    allow = load_allow(open(rest[1], encoding="utf-8").read())
    why, notes = judge(open(rest[0], encoding="utf-8").read(), allow, today, latest)
    for n in notes:
        print("releasegate:", n)
    for w in why:
        print("releasegate: REFUSED:", w, file=sys.stderr)
    if not why:
        print("releasegate: ok, publishable")
    return 1 if why else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
