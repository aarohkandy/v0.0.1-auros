# Stream E: build, test and publish AurOS on GitHub Actions

Owner of: `.github/workflows/{build,release,tests}.yml`, `build/sign`,
`tools/apt.sh`, `docs/{SIGNING,RELEASE,HANDOFF-RUNBOOK}.md`, this file.
Session B also: the release -> e2e -> promote -> Pages chain (with F for
`e2e.yml`) and `windows.yml`'s gate and pins (lead's orders).
Repository: https://github.com/aarohkandy/v0.0.1-auros, branch `main`.
Updated 2026-10-23 05:25 UTC.

## Goal

Every change is built on GitHub's `ubuntu-24.04` runners, tested under
KVM, and published as a GitHub Release that `AurOS-Setup.exe` downloads
its image from. One commit subject `[release] [latest]` takes it all the
way to the website with nobody touching it. The website's button is
`https://github.com/aarohkandy/v0.0.1-auros/releases/latest/download/AurOS-Setup.exe`,
which only a **full** (non-pre) release moves.

## The chain (release.yml), and the trigger words (commit SUBJECT only)

```
[release] [latest]:  build -> tests -> publish (prerelease) -> windows ┐
                                                             -> e2e    ├-> promote -> pages
                                                             -> site   ┘
[release]:           the same, stops before promote
[publish RUN-ID]:    publish -> windows + e2e + site (build RUN-ID, nothing rebuilt)
[publish RUN-ID] [latest]: ... -> promote -> pages (e2e runs again on that exe first)
```

- `e2e` = `e2e.yml` called with `installer_url` = this release's
  AurOS-Setup.exe and `installer_sha256` = the hash `publish` computed;
  the harness refuses a download that does not hash to it. Hours.
- `site` = website stamped + `tools/site/check.mjs` (as pages.yml), its
  github-pages artifact is what `pages` deploys.
- `promote` needs windows, e2e and site, and asks this run's jobs API
  that at least one `e2e / e2e (...)` job ran and all of them passed (a
  skipped e2e cannot count). No hand-pinned SHA anywhere.
- **Only the lead pushes `[latest]`.**

## Done (session B)

- `d782dca` signtest builds in a whole copy of build/ src/ tools/ (it
  broke when build/aurbridge started reading tools/repo.sh and
  src/aurstage); nmtest checks the Wi-Fi keyfile without Ferry's deleted
  emitter (stream R removed it in fb90bcd).
- `0de2e5f` `tools/apt.sh`: every apt-get under `timeout 300`, 3 tries,
  `dpkg --configure -a` between; used by build/tests/othersys(+its
  runner-setup). profiles.yml already bounded (P). Every job in those
  four files has `timeout-minutes`. Proven on homebase with PATH shims:
  a hung apt-get gives up after 3 bounded tries (exit 1); a flaky one
  succeeds on the 2nd.
- `e8f2744` windows.yml `pins` job: PUBLISHED_*/IMAGE_* come from this
  repo's newest FULL release at run time (SHA256SUMS, image.sha256,
  image.bytes); only on a 404 (no full release yet) the old repo's v4
  files. Any other API answer or a missing file fails. Checked on the
  Mac: this repo -> v4 fallback; cli/cli (release without SHA256SUMS) ->
  fails.
- `4506100` release.yml publishes `image.sha256` and `image.bytes` too.
- `29ea18c` the chain above (release.yml + one line in F's e2e.yml: the
  e2e job's concurrency group gains the installer SHA, so a release's
  e2e is never replaced by an `[e2e]` push while it waits). promote gate
  checked on fixtures + run 39901973428.
- `c336d84` windows.yml gated: runs only when src/aurbridge/,
  src/aurstage/{gpt,plan}.[ch], build/aurbridge, tools/{pe_payload.py,
  repo.sh,manifesttest.sh} or windows.yml change, `[windows]` in the
  subject, or by hand; per-job concurrency groups (a docs push replaces
  nothing; a started run finishes). It had never completed in this repo.
- `e74c062` tests.yml: gpttest, committest, bootchaintest (unit);
  powercuttest (own fault-injection staging build) and bootupdatetest
  (e2e matrix); e2e lanes 2 -> 4, longest first.

## FIRST PRERELEASE (08:16 UTC) -- stream F, point e2e here

- Tag **`v0.1.0-618726b`** (prerelease, 21 assets: AurOS-Setup.exe, pieces.txt, SHA256SUMS,
  image.sha256, image.bytes, 16 pieces). Built and fully tested by run 39905858387 (build 9.7 min;
  unit + 7 KVM suites green); published by release run 39923341886.
- `AurOS-Setup.exe` 35,460,072 bytes, **SHA-256 `5b875c0bd3b007ad9caab67732fe13a4fab828e6ee550c5b881863a6f2480b52`**
  (re-downloaded and re-hashed on the Mac: equal to SHA256SUMS).
- URL: `https://github.com/aarohkandy/v0.0.1-auros/releases/download/v0.1.0-618726b/AurOS-Setup.exe`
- Image: 4,655,677,440 bytes, SHA-256 `7656a98de7fb9d0aa4f2980b59c888f52526a050d92a94e45b4d268bcc31c1c2`; pieces.txt base is this repository.
- release run 39923341886 also runs windows + e2e (this exe, hash handed over) + site; no promote.

## In flight (check first)

- Build run 39903639529 (`[release]` of 4506100): build 9 min green;
  tests running. When green, release.yml (as of 4506100: no e2e yet)
  publishes prerelease `v0.1.0-4506100`. Then post its tag + exe SHA-256
  here and in the session log (stream F needs it).
- Build of e74c062 (tests.yml change): first CI run of the new suites.

## Next steps

1. First prerelease: confirm 21 assets (exe, pieces.txt, SHA256SUMS,
   image.sha256, image.bytes, 16 pieces) and `release / windows` green.
2. Exercise the chain without rebuilding: `[publish 39903639529]`
   (windows + e2e + site; no promote). Prove the e2e SHA gate refuses:
   `gh workflow run e2e.yml -f installer_url=<that exe> -f
   installer_sha256=<wrong>` must fail at the download.
3. Wall clock push -> promoted: measure on the first full chain (below).

## Wall clock (measured)

| Stage | Run 39901973428 (old) | Notes |
|---|---|---|
| push -> build done | 8 m 40 s | tools 65 s, debootstrap 59 s (cache miss), forge+mkimage 213 s, staging 7 s, pieces 66 s, aurbridge 15 s, local getimage 52 s |

## Traps

- `git fetch origin main` in a worktree of the lead's checkout does not
  move `origin/main` (its refspec maps only the old branch): use
  `git fetch origin main:refs/remotes/origin/main`.
- A `[release]` re-run with `gh run rerun` uses the run's own commit:
  a fix to a test needs a new `[release]`, not a rerun.
- Unnamed `run:` shell is `bash -e` without pipefail; every workflow
  sets `defaults.run.shell: bash` (windows.yml's ubuntu jobs do not).
- Concurrency belongs on jobs, never the workflow: GitHub keeps one
  pending run per group, so a workflow-level group lets every docs push
  replace a waiting real run (windows.yml never finished because of it).
- A called workflow cannot have more permissions than its caller job;
  build.yml's `release` job grants what release.yml asks for.
- `[publish` anywhere in a commit message starts release.yml's `which`
  (seconds; it then reads the subject only).
- The free plan runs 20 jobs at once for the whole repository.

## How to verify

```sh
gh run list -R aarohkandy/v0.0.1-auros -w build -L 5
gh release list -R aarohkandy/v0.0.1-auros
gh release view <tag> -R aarohkandy/v0.0.1-auros --json assets --jq '.assets[].name'
curl -sSL -o x.exe https://github.com/aarohkandy/v0.0.1-auros/releases/download/<tag>/AurOS-Setup.exe && shasum -a 256 x.exe
```
