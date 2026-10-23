#!/usr/bin/env python3
"""workflowlint -- the rules every workflow in this repository keeps.

    python3 -I tools/workflowlint.py            check .github/workflows/*.yml
    python3 -I tools/workflowlint.py selftest   each rule refuses what it exists to refuse

1. Every file parses. One invalid file does not fail alone: GitHub also
   refuses every workflow that calls it (2026-10-23: a ": " in one step
   name in tests.yml stopped build.yml, tests.yml and so every release).
2. Every job that runs steps has timeout-minutes (a hang holds one of the
   repository's 20 runners until someone cancels it).
3. No unbounded apt-get: a workflow `run:` that calls apt-get must go
   through tools/apt.sh or bound the call with `timeout` itself.
4. Every action is pinned to a full commit SHA (`uses: owner/repo@<40 hex>
   # v4.1.0`), never a tag that can move (SEC-005). Local workflows
   (`./.github/...`) are the repository's own.
5. No workflow-level `contents: write`: it belongs to the few jobs that make
   or fill a release (SEC-005).
6. The update-signing key (secrets.AUROS_APT_SIGNING_KEY) is named only in
   a job that runs in the environment `apt-signing`, whose deployment
   branches and reviewer the owner sets (SEC-004, docs/OWNER-SETUP.md).
"""
import glob, os, re, sys

try:
    import yaml
except ImportError:
    sys.exit("workflowlint: needs PyYAML (apt: python3-yaml)")


def lint(path, text):
    out = []
    try:
        doc = yaml.safe_load(text)
    except yaml.YAMLError as e:
        return ["%s: does not parse: %s" % (path, str(e).replace("\n", " ")[:160])]
    if not isinstance(doc, dict) or not isinstance(doc.get("jobs"), dict):
        return ["%s: no jobs" % path]
    perms = doc.get("permissions")
    if perms == "write-all" or (isinstance(perms, dict) and perms.get("contents") == "write"):
        out.append("%s: workflow-level contents: write (give it to the job that needs it)" % path)
    for name, job in doc["jobs"].items():
        if not isinstance(job, dict):
            out.append("%s: job %s is not a mapping" % (path, name)); continue
        uses = [job.get("uses")] + [st.get("uses") for st in job.get("steps") or [] if isinstance(st, dict)]
        for u in uses:
            if u and not u.startswith("./") and not re.search(r"@[0-9a-f]{40}$", u):
                out.append("%s: job %s uses %s, not pinned to a full commit SHA" % (path, name, u))
        if "AUROS_APT_SIGNING_KEY" in yaml.dump(job):
            env = job.get("environment")
            env = env.get("name") if isinstance(env, dict) else env
            if env != "apt-signing":
                out.append("%s: job %s uses the update-signing key outside the apt-signing environment" % (path, name))
        if "uses" not in job and "timeout-minutes" not in job:
            out.append("%s: job %s has no timeout-minutes" % (path, name))
        for step in job.get("steps") or []:
            run = step.get("run") if isinstance(step, dict) else None
            if run and re.search(r"\bapt-get\b", run) and "tools/apt.sh" not in run \
                    and not re.search(r"\btimeout\b[^\n]*apt-get", run):
                out.append("%s: job %s calls apt-get without tools/apt.sh or timeout" % (path, name))
    return out


def selftest():
    good = "jobs:\n  a:\n    timeout-minutes: 5\n    steps:\n      - run: sh tools/apt.sh gcc\n"
    cases = [
        ("a good workflow passes", good, 0),
        ("a ': ' in a plain step name is refused", good + "      - name: voice (aurvoice: her voice)\n        run: x\n", 1),
        ("a job without timeout-minutes is refused", good.replace("    timeout-minutes: 5\n", ""), 1),
        ("a bare apt-get is refused", good.replace("sh tools/apt.sh gcc", "sudo apt-get install -y gcc"), 1),
        ("a bounded apt-get passes", good.replace("sh tools/apt.sh gcc", "sudo timeout 300 apt-get install -y gcc"), 0),
        ("a job that only calls a workflow needs no timeout",
         "jobs:\n  a:\n    uses: ./.github/workflows/x.yml\n", 0),
        ("an action pinned to a tag is refused",
         good + "      - uses: actions/checkout@v4\n", 1),
        ("an action pinned to a full SHA passes",
         good + "      - uses: actions/checkout@" + "a" * 40 + " # v4\n", 0),
        ("workflow-level contents: write is refused",
         "permissions:\n  contents: write\n" + good, 1),
        ("job-level contents: write passes",
         good.replace("    steps:", "    permissions:\n      contents: write\n    steps:"), 0),
        ("the signing key outside the apt-signing environment is refused",
         good + "      - run: echo ${{ secrets.AUROS_APT_SIGNING_KEY }}\n", 1),
        ("the signing key inside the apt-signing environment passes",
         good.replace("    steps:", "    environment: apt-signing\n    steps:") + "      - run: echo ${{ secrets.AUROS_APT_SIGNING_KEY }}\n", 0),
    ]
    bad = 0
    for what, text, want in cases:
        got = 1 if lint("t.yml", text) else 0
        print("    %-52s %s" % (what, "ok" if got == want else "FAIL"))
        bad += got != want
    print("workflowlint selftest: %s" % ("ok" if not bad else "%d wrong" % bad))
    return 1 if bad else 0


def main(argv):
    if len(argv) > 1 and argv[1] == "selftest":
        return selftest()
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".github", "workflows")
    problems = []
    files = sorted(glob.glob(os.path.join(here, "*.yml")))
    for f in files:
        problems += lint(os.path.relpath(f), open(f, encoding="utf-8").read())
    for p in problems:
        print("workflowlint:", p, file=sys.stderr)
    print("workflowlint: %d workflow files, %s" % (len(files), "ok" if not problems else "%d problem(s)" % len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
