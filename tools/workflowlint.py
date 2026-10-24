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
7. actions/checkout comes before any download-artifact in a job: checkout
   cleans untracked files, so after a download it deletes what was
   downloaded (RC3's publish, run 40026374421: dist/installer was gone).
8. A job that calls e2e.yml (release.yml's e2e, e2e-fastpath) journeys EVERY
   prerelease on real Windows: it must not wait for [latest] or promote.
10. The update-signing key comes LAST. It is named by one step at most, not
   in the job's own `env`; that step has at least three steps before it and
   an `if:` on an earlier step's output (`steps.x.outputs...`), so it is
   loaded only after the job's checks and only when there is something to
   sign (a run once loaded it on a runner for a commit whose BODY merely
   mentioned [latest]).
11. A workflow started by `workflow_run` that names the key decides through
   `tools/aptgate.sh decide` (the commit SUBJECT says [latest] and the
   triggering run's promote job concluded success, asked of the API, failing
   closed), never from a substring of the whole commit message alone.
10. A mutant is applied without `|| true` (tools/fastpath-mutant.sh): one that
   fails to apply would run the clean test, and the run that "must go red"
   reads green.
9. No dynamic reads of secrets: `secrets[...]` (a name chosen at run time,
   e.g. `secrets[inputs.x]`) and `toJSON(secrets)` hand every secret of the
   repository to a run on any branch and slip past rule 6; each secret is
   named in the file that uses it (`secrets.NAME`).
12. The installer-signing token is never in workflow or job env. Only the
    configuration check and signing actions need it; unrelated actions must
    not inherit signing authority.
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
    code = "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))
    if re.search(r"secrets\s*\[|toJSON\(\s*secrets", code, re.I):
        out.append("%s: reads secrets dynamically (secrets[...] or toJSON(secrets)): name each secret" % path)
    perms = doc.get("permissions")
    if "SIGNPATH_API_TOKEN" in yaml.dump(doc.get("env", {})):
        out.append("%s: installer-signing token in workflow env: scope it to a signing step" % path)
    if perms == "write-all" or (isinstance(perms, dict) and perms.get("contents") == "write"):
        out.append("%s: workflow-level contents: write (give it to the job that needs it)" % path)
    for name, job in doc["jobs"].items():
        if not isinstance(job, dict):
            out.append("%s: job %s is not a mapping" % (path, name)); continue
        if "SIGNPATH_API_TOKEN" in yaml.dump(job.get("env", {})):
            out.append("%s: job %s has the installer-signing token in job env: scope it to a signing step" % (path, name))
        uses = [job.get("uses")] + [st.get("uses") for st in job.get("steps") or [] if isinstance(st, dict)]
        for u in uses:
            if u and not u.startswith("./") and not re.search(r"@[0-9a-f]{40}$", u):
                out.append("%s: job %s uses %s, not pinned to a full commit SHA" % (path, name, u))
        seen = ""
        for st in job.get("steps") or []:
            u = (st.get("uses") or "") if isinstance(st, dict) else ""
            if u.startswith("actions/download-artifact"): seen = u
            if u.startswith("actions/checkout") and seen:
                out.append("%s: job %s checks out after a download-artifact: checkout deletes the downloaded files" % (path, name))
        if job.get("uses") == "./.github/workflows/e2e.yml" \
                and re.search(r"latest|promote", str(job.get("if", "")) + str(job.get("needs", ""))):
            out.append("%s: job %s waits for [latest]/promote: every prerelease is journeyed" % (path, name))
        if "AUROS_APT_SIGNING_KEY" in yaml.dump(job):
            env = job.get("environment")
            env = env.get("name") if isinstance(env, dict) else env
            if env != "apt-signing":
                out.append("%s: job %s uses the update-signing key outside the apt-signing environment" % (path, name))
        if "AUROS_APT_SIGNING_KEY" in yaml.dump({k: v for k, v in job.items() if k != "steps"}):
            out.append("%s: job %s names the update-signing key outside a step: only one late, conditional step may" % (path, name))
        for i, st in enumerate(job.get("steps") or []):
            if isinstance(st, dict) and "AUROS_APT_SIGNING_KEY" in yaml.dump(st):
                if i < 3 or not re.search(r"steps\.[A-Za-z0-9_-]+\.outputs\.", str(st.get("if", ""))):
                    out.append("%s: job %s step %d loads the update-signing key before the job's checks or without an if: on an earlier step's output" % (path, name, i + 1))
        if "uses" not in job and "timeout-minutes" not in job:
            out.append("%s: job %s has no timeout-minutes" % (path, name))
        for step in job.get("steps") or []:
            run = step.get("run") if isinstance(step, dict) else None
            if run and re.search(r"-mutant\.sh[^\n]*\|\|\s*true", run):
                out.append("%s: job %s runs a mutant with || true: a mutant that fails to apply reads green" % (path, name))
            if run and re.search(r"\bapt-get\b", run) and "tools/apt.sh" not in run \
                    and not re.search(r"\btimeout\b[^\n]*apt-get", run):
                out.append("%s: job %s calls apt-get without tools/apt.sh or timeout" % (path, name))
    triggers = doc.get(True, doc.get("on"))
    if "workflow_run" in (triggers if isinstance(triggers, (dict, list)) else []) \
            and "AUROS_APT_SIGNING_KEY" in text and "tools/aptgate.sh decide" not in text:
        out.append("%s: started by workflow_run and names the update-signing key, but never calls tools/aptgate.sh decide" % path)
    return out


KEY = ("jobs:\n  a:\n    timeout-minutes: 5\n    environment: apt-signing\n    steps:\n"
       "      - run: one\n      - run: two\n      - run: three\n"
       "      - if: steps.n.outputs.go == 'true'\n        run: echo ${{ secrets.AUROS_APT_SIGNING_KEY }}\n")
WR = "on:\n  workflow_run:\n    workflows: [build]\n    types: [completed]\n"


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
        ("a checkout after a download is refused",
         good + "      - uses: actions/download-artifact@" + "a" * 40 + "\n      - uses: actions/checkout@" + "a" * 40 + "\n", 1),
        ("a checkout before a download passes",
         good + "      - uses: actions/checkout@" + "a" * 40 + "\n      - uses: actions/download-artifact@" + "a" * 40 + "\n", 0),
        ("release.yml's e2e waiting for latest is refused", "jobs:\n  e2e:\n    needs: promote\n    uses: ./.github/workflows/e2e.yml\n", 1),
        ("an e2e that needs only the publish passes", "jobs:\n  e2e:\n    needs: publish\n    uses: ./.github/workflows/e2e.yml\n", 0),
        ("a mutant run with || true is refused", good + "      - run: bash tools/fastpath-mutant.sh \"$M\" || true\n", 1),
        ("a mutant run without it passes", good + "      - run: bash tools/fastpath-mutant.sh \"$M\"\n", 0),
        ("secrets[inputs.x] is refused", good + "      - env:\n          V: ${{ secrets[inputs.name] }}\n        run: x\n", 1),
        ("secrets['NAME'] is refused", good + "      - env:\n          V: ${{ secrets['NAME'] }}\n        run: x\n", 1),
        ("toJSON(secrets) is refused", good + "      - env:\n          V: ${{ toJSON(secrets) }}\n        run: x\n", 1),
        ("a named secret passes", good + "      - env:\n          V: ${{ secrets.APT_ENV_CANARY }}\n        run: x\n", 0),
        ("secrets: inherit passes", "jobs:\n  a:\n    uses: ./.github/workflows/x.yml\n    secrets: inherit\n", 0),
        ("workflow-level contents: write is refused",
         "permissions:\n  contents: write\n" + good, 1),
        ("job-level contents: write passes",
         good.replace("    steps:", "    permissions:\n      contents: write\n    steps:"), 0),
        ("the signing key outside the apt-signing environment is refused",
         KEY.replace("    environment: apt-signing\n", ""), 1),
        ("the signing key inside the apt-signing environment passes", KEY, 0),
        ("a late, conditional key step passes", KEY, 0),
        ("a key step with no if: is refused", KEY.replace("      - if: steps.n.outputs.go == 'true'\n        run:", "      - run:"), 1),
        ("a key step that is the first step is refused",
         "jobs:\n  a:\n    timeout-minutes: 5\n    environment: apt-signing\n    steps:\n      - if: steps.n.outputs.go == 'true'\n        run: echo ${{ secrets.AUROS_APT_SIGNING_KEY }}\n      - run: one\n      - run: two\n      - run: three\n", 1),
        ("a key step that only tests something unrelated is refused", KEY.replace("steps.n.outputs.go == 'true'", "github.event_name == 'push'"), 1),
        ("the key in the job's own env is refused",
         KEY.replace("    environment: apt-signing\n", "    environment: apt-signing\n    env:\n      K: ${{ secrets.AUROS_APT_SIGNING_KEY }}\n"), 1),
        ("workflow_run + the key, no aptgate, is refused", WR + KEY, 1),
        ("workflow_run + the key, through aptgate, passes", WR + KEY.replace("      - run: one\n", "      - run: bash tools/aptgate.sh decide\n"), 0),
        ("push + the key needs no aptgate", "on:\n  push:\n" + KEY, 0),
        ("installer token in workflow env is refused",
         "env:\n  TOKEN: ${{ secrets.SIGNPATH_API_TOKEN }}\n" + good, 1),
        ("installer token in job env is refused",
         good.replace("    steps:", "    env:\n      TOKEN: ${{ secrets.SIGNPATH_API_TOKEN }}\n    steps:"), 1),
        ("installer token in one step passes",
         good + "      - run: test -n \"$TOKEN\"\n        env:\n          TOKEN: ${{ secrets.SIGNPATH_API_TOKEN }}\n", 0),
        ("explicit installer token handoff passes",
         "jobs:\n  sign:\n    uses: ./.github/workflows/sign-installer.yml\n    secrets:\n      SIGNPATH_API_TOKEN: ${{ secrets.SIGNPATH_API_TOKEN }}\n", 0),
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
