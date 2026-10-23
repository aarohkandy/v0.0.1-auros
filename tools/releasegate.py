#!/usr/bin/env python3
"""releasegate -- may this build run be published as a release?

    python3 -I tools/releasegate.py JOBS ALLOW [TODAY] [--latest]   JOBS: lines "name|conclusion"
    python3 -I tools/releasegate.py selftest             each rule refuses what it exists to refuse

JOBS is the build run's job list (release.yml's `which` job fetches it
from the Actions API); ALLOW is .github/ci-allow.txt; TODAY is a UTC date
(default: today). Exit 0 = publishable, 1 = refused, with the reasons.

The rules (docs/safety/FINDINGS.md S-11 is why the power-cut ones are here):
  1. `build` succeeded.
  2. `tests / unit` succeeded, at least 5 `tests / e2e (...)` jobs ran, and
     EVERY `tests / ...` job succeeded.
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


def selftest():
    green = "\n".join(
        ["build|success", "tests / unit|success"]
        + ["tests / e2e (t%d)|success" % i for i in range(7)]
        + ["powercut / e2e (p%d)|success" % i for i in range(4)])
    today = "2026-10-24"
    allow = {"powercut / e2e (p2)": ("2026-10-30", "S-03"), "powercut / e2e (p3)": ("2026-10-20", "old")}
    bad = 0

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
