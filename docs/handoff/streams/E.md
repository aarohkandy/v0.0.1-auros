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
- **B / C / D**: the `site` job and pages.yml are red (run 39927312707, 09:00) on four
  promises the code no longer keeps: advanced.html quotes Setup saying "This computer starts
  up the older way (BIOS)" but wizard.c (7fb9b2d, B's restyle) says "This computer uses legacy
  BIOS boot" and preflight.c "This PC starts Windows the older way (BIOS)"; index.html quotes
  desktop-screen words "Switching on starts AurOS from now on.", "Nothing changes. This comes
  back next time.", "Switching on starts Windows again." that are not in src/aurshell any
  more. Reconcile one side each; until then `promote` is blocked by design (and the site
  does not deploy). My own page's one failure (a power-cut phrase) is fixed.
- **F**: e2e.yml has one line of mine (the e2e job's concurrency group gains the installer
  SHA) and a timeout on `gate`. Its env comment that "promote reads INSTALLER_SHA256 from this
  file" is stale.

## Session B, later (14:10 UTC): what changed since the first prerelease

- **First prerelease** `v0.1.0-618726b` (above) is old: it has the staging `cannot-measure` bug
  (fixed in 6917bde) and no fast-path asset. A run was cancelled (ae672fd) and a run (fb82014)
  cancelled for the same reason. **The release to use is the one built from `6af816a` or later**
  (run 39966214944); it is published by `[publish 39966214944]` at the commit that carries the
  allow-list (see "The release gate"). Its tag will be `v0.1.0-6af816a`, assets
  `AurOS-Setup.exe` (fast path off) and `AurOS-Setup-fastpath.exe`; the lead/F get the URLs and
  hashes in SESSION-2026-10-23-B.md the moment it exists.
- **The two installers.** build.yml builds the release installer twice: normal (fast path off,
  what the website serves) and with `AUROS_FASTPATH=1`; the second is published as the separate
  asset `AurOS-Setup-fastpath.exe` in the same prerelease, listed in `SHA256SUMS`, hash-checked and
  `--navtest`ed by the Windows job, and run through the real-Windows e2e by release.yml's
  `e2e-fastpath` job (informative, not in promote's needs; unsigned). By hand:
  `gh workflow run e2e.yml -f installer_url=<.../AurOS-Setup-fastpath.exe> -f installer_sha256=<hash> -f variants=base`.
  Do not put `[fastpath]` in a subject: A2's a5a3786 uses it to turn the fast path ON in the
  normal exe too.
- **The release gate (S-11, security review).** `build.yml`: `release` needs `[gate, build, tests,
  powercut]`; `powercut` is tests.yml's suite `nostick` (four KVM jobs of `tools/powercut-nostick.sh`
  by instants). release.yml's `which` job runs `tools/releasegate.py` (selftest 14 cases) on the
  run's jobs for BOTH `[release]` and `[publish]`: build, unit, every gating test green; every
  `powercut / e2e (...)` job green or named in `.github/ci-allow.txt` (name | expiry date | reason;
  only power-cut jobs; a past date is no entry; **never for `[latest]`**). Today's only entry:
  `powercut-nostick-restore`, 3 of 57 checks, S-01/S-02 (A2), until 2026-10-26. Numbers (run
  39962229132): early/write/commit slices green; restore slice 265 s of test plus ~640 s of
  setup under load. The suite adds ~10 min to the tests' critical path when runners are busy.
- **Security review items done on the workflow side.** SEC-004: aptrepo.yml's publish job has
  `environment: apt-signing` (owner's clicks: OWNER-SETUP 4a-lock). SEC-005: `contents: write`
  only on jobs that make or fill a release (profiles, othersys, e2e, drill, fleet moved off
  workflow level); all 90 `actions/*` uses pinned to commit SHAs (SignPath already was);
  `tools/workflowlint.py` fails a tag-pinned action, workflow-level `contents: write`, a job
  that names `AUROS_APT_SIGNING_KEY` outside `apt-signing`, an unbounded apt-get, a missing
  timeout, an unparsable file. Not done: release immutability (needs F/P/H to upload evidence to
  a draft and publish at the end; OWNER-SETUP 4d says so), moving the apt channel off Releases,
  the hourly external hash watchdog of `releases/latest/download/AurOS-Setup.exe` (OPS).
- **Other root-cause fixes this stretch.** tools/apt.sh drops a slow Azure mirror from the
  second try and CI installs only the 64-bit mingw compiler (278 MB -> ~60 MB): a [release] had
  died in "room and tools" at 40-200 KB/s. e2e.yml's harness checks install numpy first (F's
  selftest imported vm.py before the runner step installed it: every e2e died). Ferry's helpers are
  built with `-fstack-protector-all` (ffhash imported no `__stack_chk_fail`, and the hardening
  gate failed the build). forge's hardening gate checks only Ferry's ELF files.

- **What a release now has to be (release.yml `which`, for `[release]` and `[publish]`, prerelease or
  full).** (1) `tools/releasegate.py`: build, unit and every gating test green; every power-cut job
  green or on `.github/ci-allow.txt`, which excuses a PRERELEASE only. (2) `.github/release-requires.txt`:
  the built commit has each listed hash as an ancestor (956af4a TM-1, 6917bde cannot-measure,
  54b783b blank Firefox, e1c8015 lockdown); add a line when a fix lands that nothing may ship
  without. (3) the gating matrix is built in tests.yml's `which` (suite `gating`: 12 jobs incl.
  updateboottest, windowsboottest, rollbacktest, bootlocktest; six lanes) and stops on a duplicate
  label (two jobs of one name upload one artifact name and the second fails).
- **Candidate run 40001908921 (445de27) -> tag `v0.1.0-445de27`**, built after the first gated try
  (39975129610, ea34dc4: AurOS-Setup.exe fb60c603...13de, AurOS-Setup-fastpath.exe 39469c1f...51c1)
  found four reds, each with a cause and a fix: windowsboottest case C (test-only: the control's
  GRUB entry needed `--unrestricted` once the menu was locked; 6fb53d9), updatepolicytest (test-only:
  matched a comment; S5's 61fbe40), putbacktest (R's e55c41f), the power-cut restore slice (green
  there; A2's 50d623c). Do not use ea34dc4's build.

## Open / next

1. The first full chain has not run end to end: `[release] [latest]` has never been pushed
   (lead only). `promote` is checked on fixtures and `which`'s gate on fixtures and a fake `gh`.
2. Exercised for real so far: build, tests, publish, windows, site; e2e runs but the journey has
   not passed (F). `sign` (needs the owner's SignPath account) and `secrets: inherit` have not run.
3. Cuts not done: split nosticktest (16.4 min, the critical path of the gating tests); overlap
   the tests with publish/windows/e2e (publish a prerelease after build+unit; needs a decision).
4. **homebase's disk was 100% full at 08:30 UTC (94% at 13:15).** Tell the owner if it recurs.
5. The wrong-SHA e2e proof is not yet on CI: every e2e since 08:30 dies earlier (numpy; M1
   Windows install). e2e.py raises `Failure` on a mismatch (line ~440), unexercised.

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
