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
8. A job that calls e2e.yml (release.yml's e2e) journeys EVERY
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
13. make-os.yml (a custom OS for one person, docs/MAKE-AN-OS.md) can never touch
    the public release: no --latest, make_latest, --prerelease=false,
    releases/latest, `gh release edit` or call of release.yml; every
    `gh release create` says --prerelease; only the jobs that publish
    (publish; sources, which calls sources.yml for the release sources-os-...;
    and windows, which calls e2e.yml) have contents: write; and no script has
    ${{ }} in it (the profile is text a person typed: env only). The installer is
    never published without its source code: `sources` calls sources.yml with a
    profile, `publish` waits for it (needs, no `if:`), and nothing in the file
    says continue-on-error.
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
    if os.path.basename(path) == "make-os.yml":
        out += lint_make_os(path, doc, code)
    triggers = doc.get(True, doc.get("on"))
    if "workflow_run" in (triggers if isinstance(triggers, (dict, list)) else []) \
            and "AUROS_APT_SIGNING_KEY" in text and "tools/aptgate.sh decide" not in text:
        out.append("%s: started by workflow_run and names the update-signing key, but never calls tools/aptgate.sh decide" % path)
    return out


def lint_make_os(path, doc, code):
    out = []
    for bad in (r"--latest\b", r"make_latest", r"--prerelease=false", r"releases/latest",
                r"gh\s+release\s+edit", r"workflows/release\.yml"):
        if re.search(bad, code):
            out.append("%s: make-os.yml names %s: a custom OS must never be able to touch the public release" % (path, bad.replace("\\", "")))
    for cmd in re.findall(r"gh\s+release\s+create(?:\\\n|[^\n])*", code):
        if "--prerelease" not in cmd:
            out.append("%s: make-os.yml has a `gh release create` without --prerelease" % path)
    if re.search(r"continue-on-error", code):
        out.append("%s: make-os.yml says continue-on-error: a failure must stop the installer from being published" % path)
    jobs = doc["jobs"]
    pub, src = jobs.get("publish") or {}, jobs.get("sources")
    need = pub.get("needs") or []
    need = [need] if isinstance(need, str) else need
    if not isinstance(src, dict) or src.get("uses") != "./.github/workflows/sources.yml" \
            or not (src.get("with") or {}).get("profile"):
        out.append("%s: make-os.yml has no `sources` job calling sources.yml with a profile: no installer without its source code" % path)
    elif src.get("if") is not None:
        out.append("%s: make-os.yml's sources job has an if:" % path)
    if "sources" not in need or pub.get("if") is not None:
        out.append("%s: make-os.yml's publish must need sources and have no if: (an installer is never published without its source code)" % path)
    for name, job in doc["jobs"].items():
        perms = job.get("permissions")
        if isinstance(perms, dict) and perms.get("contents") == "write" and name not in ("publish", "windows", "sources"):
            out.append("%s: make-os.yml job %s has contents: write: only publish, sources (sources.yml's own release) and windows (e2e.yml's evidence) may" % (path, name))
        for st in job.get("steps") or []:
            if isinstance(st, dict) and "${{" in str(st.get("run", "")):
                out.append("%s: make-os.yml job %s has ${{ }} inside a script: the profile is typed text, pass it through env" % (path, name))
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
    mk = ("jobs:\n  sources:\n    needs: [build]\n    uses: ./.github/workflows/sources.yml\n    permissions:\n      contents: write\n"
          "    with:\n      profile: x\n"
          "  publish:\n    needs: [build, sources]\n    timeout-minutes: 5\n    permissions:\n      contents: write\n    steps:\n"
          "      - run: gh release create \"$TAG\" --prerelease --target main rel/*\n")
    cases += [
        ("make-os: a prerelease-only publish passes", mk, 0),
        ("make-os: --latest is refused", mk + "      - run: gh release create x --prerelease --latest\n", 1),
        ("make-os: a release without --prerelease is refused", mk + "      - run: gh release create \"$TAG\" rel/*\n", 1),
        ("make-os: a continued release create without --prerelease is refused", mk + "      - run: |\n          gh release create \"$TAG\" \\\n            rel/*\n", 1),
        ("make-os: releases/latest is refused", mk + "      - run: curl https://github.com/x/releases/latest/download/AurOS-Setup.exe\n", 1),
        ("make-os: gh release edit is refused", mk + "      - run: gh release edit v1 --prerelease=false\n", 1),
        ("make-os: contents: write on the build job is refused", mk + "  build:\n    timeout-minutes: 5\n    permissions:\n      contents: write\n    steps:\n      - run: x\n", 1),
        ("make-os: an input inside a script is refused", mk + "      - run: echo ${{ inputs.profile }}\n", 1),
        ("make-os: an input through env passes", mk + "      - env:\n          P: ${{ inputs.profile }}\n        run: echo \"$P\"\n", 0),
        ("make-os: publish that does not wait for sources is refused", mk.replace("needs: [build, sources]", "needs: [build]"), 1),
        ("make-os: publish with an if: (always() and the like) is refused", mk.replace("    needs: [build, sources]\n", "    needs: [build, sources]\n    if: ${{ always() }}\n"), 1),
        ("make-os: no sources job at all is refused", mk.replace("uses: ./.github/workflows/sources.yml", "uses: ./.github/workflows/e2e.yml"), 1),
        ("make-os: a sources job without a profile is refused", mk.replace("    with:\n      profile: x\n", ""), 1),
        ("make-os: a sources job with an if: is refused", mk.replace("    needs: [build]\n", "    needs: [build]\n    if: ${{ always() }}\n"), 1),
        ("make-os: continue-on-error is refused", mk + "      - continue-on-error: true\n        run: x\n", 1),
        ("make-os: contents: write on the sources job passes", mk, 0),
    ]
    bad = 0
    for what, text, want in cases:
        got = 1 if lint("make-os.yml" if what.startswith("make-os") else "t.yml", text) else 0
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
