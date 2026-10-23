# Stream E: build, test and publish AurOS on GitHub Actions

Owner of: `.github/workflows/{build,release,tests,workflowlint}.yml`, `build/sign`,
`tools/{apt.sh,signed-check.sh,authenticode.py,workflowlint.py,wishesoff.sh}`,
`docs/{SIGNING,RELEASE,HANDOFF-RUNBOOK}.md`, `docs/legal/signpath-application.md`,
this file. Session B also: the release -> e2e -> promote -> Pages chain (with F for
`e2e.yml`), `windows.yml`'s gate and pins, the D12 signing wiring and the D14 flag.
Repository: https://github.com/aarohkandy/v0.0.1-auros, branch `main`.
Updated 2026-10-23 09:00 UTC.

## Goal

Every change is built on GitHub's `ubuntu-24.04` runners, tested under KVM, and
published as a GitHub Release that `AurOS-Setup.exe` downloads its image from.
One commit subject `[release] [latest]` takes it all the way to the website with
nobody touching it. The website's button is
`https://github.com/aarohkandy/v0.0.1-auros/releases/latest/download/AurOS-Setup.exe`,
which only a **full** (non-pre) release moves. **Only the lead pushes `[latest]`.**

## The chain (release.yml), and the trigger words (commit SUBJECT only)

```
[release] [latest]:  build -> tests -> [sign] -> publish (prerelease) -> windows ┐
                                                                     -> e2e     ├-> promote -> pages
                                                                     -> site    ┘
[release]:           the same, stops before promote
[publish RUN-ID]:    publish -> windows + e2e + site (nothing rebuilt; NOT signable)
[publish RUN-ID] [latest]: ... -> promote -> pages (e2e runs again on that exe first)
```

- `sign` = SignPath (D12), skipped with no runner until the repository variable
  `SIGNPATH_ORGANIZATION_ID` exists (then the secret `SIGNPATH_API_TOKEN` must too).
  See `docs/SIGNING.md`. Waits for the owner's approval (up to 4 h), then
  `tools/signed-check.sh`. Only a `[release]` run can be signed; with signing on, a
  `[publish RUN-ID]` fails with a message rather than publishing unsigned.
- `e2e` = `e2e.yml` called with this release's AurOS-Setup.exe URL and the SHA-256
  `publish` computed; the harness refuses a download that does not hash to it.
- `site` = website stamped + `tools/site/check.mjs`; its github-pages artifact is
  what `pages` deploys. **It checks the website's promises against the code, so
  other streams can turn it red** (see "Open", below).
- `promote` needs windows, e2e and site, and asks this run's jobs API that at
  least one `e2e / e2e (...)` job ran and all passed. No hand-pinned SHA anywhere.
- The release tag is made on the **tip of main when published**: the workflow token
  may create a tag only at a branch head (probed: run 39923676690; every older commit
  is a 403 "Resource not accessible by integration", workflow files changed or not).
  The notes say which commit built the files and which run tested them.

## FIRST PRERELEASE (08:16 UTC) -- stream F, point e2e here

- Tag **`v0.1.0-618726b`** (prerelease, 21 assets). Built and fully tested by run
  39905858387 (build 9.8 min; unit + 7 KVM suites green); published by release run
  39923341886, whose windows job passed. Its e2e (base) was running at 09:00.
- `AurOS-Setup.exe` 35,460,072 bytes, **SHA-256
  `5b875c0bd3b007ad9caab67732fe13a4fab828e6ee550c5b881863a6f2480b52`**.
  URL `https://github.com/aarohkandy/v0.0.1-auros/releases/download/v0.1.0-618726b/AurOS-Setup.exe`.
- Image 4,655,677,440 bytes, SHA-256
  `7656a98de7fb9d0aa4f2980b59c888f52526a050d92a94e45b4d268bcc31c1c2`.

## Done (session B), by commit

- `d782dca` signtest builds in a whole copy of build/ src/ tools/; nmtest without
  Ferry's deleted emitter. `618726b` forge's hardening gate checks Ferry's ELF
  helpers, not its scripts. `8e3849a` the v4 fallback pins are env lines (repotest).
- `0de2e5f` + later: `tools/apt.sh` (every apt-get under `timeout 300`, stalled
  downloads cut after 30 s, 3 tries); every job in build/tests/profiles/othersys has
  `timeout-minutes`. `bc286b0` `tools/workflowlint.py` + `workflowlint.yml` enforce
  both rules, and that every workflow parses, on every workflow change.
- `a7f0af8` a ": " in a step name in tests.yml (from W2's 09e927e) made tests.yml
  and build.yml invalid and stopped every build for ~5 minutes: fixed. The lint
  catches this class now.
- `e8f2744` windows.yml `pins` job: PUBLISHED_*/IMAGE_* come from this repo's newest
  FULL release at run time (SHA256SUMS, image.sha256, image.bytes); only on a 404 the
  old repo's v4 files. `c336d84` windows.yml is gated on what it tests
  (`src/aurbridge/`, `src/aurstage/{gpt,plan}`, `build/aurbridge`, `tools/{pe_payload.py,
  repo.sh,manifesttest.sh}`, itself) or `[windows]`; per-job concurrency groups.
- `29ea18c` the chain (release -> e2e -> promote -> pages); `263fe7d` the tag on the tip.
- `e74c062` tests.yml: gpttest, committest, bootchaintest (unit); powercuttest (own
  fault-injection staging build) and bootupdatetest (KVM); e2e lanes 2 -> 4.
- `9a19e78` D12: `sign` job, `tools/signed-check.sh`, `tools/authenticode.py`, 19-check
  signtest (mutants proven on CI), `docs/SIGNING.md`, `docs/legal/signpath-application.md`,
  `website/code-signing.html` + footer links, OWNER-SETUP section 5.
- `b8b0824` D14: profile variable `wishes` (desktop: off), `-DAUROS_WISHES`
  (`src/aurshell/features.h`), `/etc/auros/features`, `tools/wishesoff.sh` in the unit job.

## For other streams

- **W1**: put your forge lines that build/install/enable aurwish under `if wishes_on; then`
  (defined at the top of build/forge). No second switch. `tools/wishesoff.sh` fails the unit
  job if an image whose profile says off contains an aurwish file/unit or the shell tile.
- **W2**: any other door into Wishes (voice, a key, a notification) belongs behind
  `AUROS_WISHES` (`#include "features.h"`). Done: home tile (apps.c), preview list entry
  (shellcommon.c), Settings row (settings.c `row_applies`).
- **D**: `website/code-signing.html` is new (policy page SignPath requires) and linked in
  four footers; restyle freely but keep: the term "Code signing policy" on the home page
  footer, the line "Free code signing provided by SignPath.io, certificate by SignPath
  Foundation", the roles, the privacy statement. When a signed release exists, rewrite the
  "no signature" sentences (advanced.html, the front door's click strip) and teach
  check.mjs the claim. Today the page says "applying" because the owner has not applied.
- **B / D**: the `site` job (and pages.yml) was red at c604d41 on `advanced.html`: it quotes
  Setup saying "This computer starts up the older way (BIOS)" but wizard.c (7fb9b2d, B's
  restyle) now says "This computer uses legacy BIOS boot" (and fix_win.c something else).
  Reconcile one side; until then `promote` is blocked by design.
- **F**: e2e.yml has one line of mine (the e2e job's concurrency group gains the installer
  SHA) and a timeout on `gate`. Its env comment that "promote reads INSTALLER_SHA256 from this
  file" is stale.

## Open / next

1. The first full chain has not run end to end: `[release] [latest]` has never been pushed
   (lead only). Exercised so far: build, tests, publish, windows (green), e2e (running),
   site (red for the BIOS wording above), promote (gate logic checked on fixtures only).
2. The build of the tip with D14 + wishesoff + signing wiring (run 39927700494) tests that
   the shell edits did not break storetest/looktest/choicesboottest.
3. Not yet run for real: the `sign` job (needs the owner's SignPath account), `secrets: inherit`.
4. Pipeline cuts not done: split nosticktest (16.4 min, the critical path of the tests, into
   two); overlap the tests with publish/windows/e2e (publish a prerelease after build+unit,
   KVM suite concurrent; needs a decision, see numbers below).
5. **homebase's disk was 100% full (122 MB free of 247 GB) at 08:30 UTC.** Not from E
   (my scratch was 50 MB, removed). Others' services may break; tell the lead/owner.

## Wall clock (measured, run 39905858387, the [release] of 618726b)

| Stage | Minutes from push | Note |
|---|---|---|
| build | 0 -> 10.0 | tools 91 s, forge+mkimage 263 s, pieces (pigz -9) 78 s, installer 17 s, getimage-local 66 s |
| unit | 10.1 -> 14.9 | |
| KVM suite (7 tests, 4 lanes) | 10.1 -> 26.5 | critical path nosticktest 16.4; firstboot 10.7; powercut 9.4; others 2.6-5.8 |
| publish + read-back | +1.1 | run 39923341886 |
| windows | +1.6 | parallel with e2e and site |
| e2e (base) | cache miss: runner 2.9 + Windows install 28.0 + journey (see below) | the long pole; the Windows golden image is cached once saved |

Before this session (run 39901973428): tests ended at 27.1 min with 5 tests on 2 lanes;
now 7 tests (adding powercut and bootupdate) end at 26.5 on 4 lanes. Push -> prerelease is
~28 min; push -> promoted is that plus the e2e journey (hours when the Windows cache misses).

## Traps

- `git fetch origin main` in a worktree of the lead's checkout does not move `origin/main`:
  `git fetch origin main:refs/remotes/origin/main`.
- A `[release]` re-run with `gh run rerun` uses the run's own commit: a fix needs a new commit.
- Unnamed `run:` shell is `bash -e` without pipefail; every workflow sets `defaults.run.shell: bash`.
- Concurrency belongs on jobs, never the workflow (GitHub keeps one pending run per group).
- A called workflow cannot have more permissions than its caller job, and sees no secret that is
  not passed: build.yml's `release` job has `permissions` and `secrets: inherit`.
- `secrets` cannot be used in a job-level `if`; `vars` can. The signing switch is the variable.
- The azure apt mirror stalls mid-download sometimes (10 minutes seen): `tools/apt.sh` cuts a
  stalled download after 30 s.
- A plain YAML scalar with ": " in it is invalid; GitHub then also refuses every workflow that
  calls the file. `python3 tools/workflowlint.py` before pushing a workflow edit.
- `[publish` anywhere in a commit message starts release.yml's `which` (seconds; subject only after).
- The workflow token tags only a branch head (see above).

## How to verify

```sh
gh run list -R aarohkandy/v0.0.1-auros -w build -L 5
gh release list -R aarohkandy/v0.0.1-auros
gh release view <tag> -R aarohkandy/v0.0.1-auros --json assets --jq '.assets[].name'
curl -sSL -o x.exe https://github.com/aarohkandy/v0.0.1-auros/releases/download/<tag>/AurOS-Setup.exe && shasum -a 256 x.exe
python3 tools/workflowlint.py selftest && python3 tools/workflowlint.py
sh tools/wishesoff.sh selftest
```
