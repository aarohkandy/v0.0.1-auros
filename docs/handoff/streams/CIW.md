# Stream CIW: CI wiring (every test the docs call proof must run in a workflow)

Brief: `docs/reviews/CI-TRUTH-2026-10-24.md` section D.12-D.13. Every test the
docs present as proof but no workflow ran becomes a gating step of
`.github/workflows/tests.yml` (`tests / unit`, a new `tests / ...` job, or an
e2e matrix entry if it needs KVM), or is retired with a reason.

Method for each test: run at origin/main on homebase (docker image `ciw-env`,
ubuntu:24.04 with the runner's packages), mutate the product code it guards,
watch it go red, restore, then wire. A red test is either a product bug in a
destructive path (stop the line, no red step wired) or a stale test (fixed
without loosening what it asserts).

Artifacts for the runs: build run 40046138543 (8dd0c05).

## Status

| test | status | evidence |
|---|---|---|
| modaltest | wired (`tools/ctests.sh`, unit) | green; mutant: drop `guard_open` from `SHELL_PANEL_OPEN` (shell.h) -> 2 of 57 wrong |
| hittest | wired | green; mutants: rail's Home tiles lose their click (rail.c:573) -> FAIL; rail card hit-tested at a hard-coded 1600x900 (rail.c:603) -> FAIL. (First try, dock `on_strip` swallow removed, SURVIVED: not a hittest guard, so not used as proof.) |
| kiosktest | wired | green; mutants: deny-list check removed (apps.c:527) -> 1 failed; user directory searched under an allow-list (apps.c:604) -> 2 failed |
| launchtest | wired | green; mutant: `shell_launch` never calls spawn (shellcommon.c:959) -> FAILED |
| nettest | wired | green; mutant: "psk: property is invalid" no longer recognised (net.c:360) -> 1 of 31 wrong |
| powertest | wired | green; mutant: AC `online` file misread (power.c:80) -> 1 of 31 wrong |
| stridetest | wired | green; mutant: row stride ignored in `surface_clear` (draw.c:62) -> FAILED |
| bttest | wired | green; mutant: "Paired: yes" not parsed (bt.c:174) -> 1 of 16 wrong |
| keytest | wired | green; mutants: evdev+8 offset wrong (aurwl.c:3737) -> 16 of 19 wrong; control characters let through (aurwl.c:3742) -> 4 of 19 wrong |
| padtest | wired | green; mutant: `PAD_TAP_MS` 0 (pad.c:54) -> FAIL |

| wrtest | wired (unit, sudo) | green 23/23; mutants: window bound loosened by 8 bytes (wr.c:114) -> 2 of 23 wrong; the build gate's `die` removed (build/staging:169) -> 2 of 23 wrong. Test fix: it rebuilt the staging image into out/ (changing the bytes exetest compares); builds now go to `$TMP/out`, and INT/TERM restore the sabotaged disks.c (tools/wrtest.sh) |
| healthtest | wired (unit) | green 14/14; mutants: pending sector not a refusal (health.c:262) -> 2 of 14; "plugged in" always true (health.c:91) -> 2 of 14 |
| termstest | wired (unit) | green 2/2; mutant: terms never written to the journal (format.c:468), sim rebuilt -> 1 of 2 wrong |
| ntfstest | wired (unit, sudo) | green 29/29; mutant: a "hibr" header read as not hibernated (ntfs.c:533) -> 3 of 29. Gap noted, not fixed: a live (nonzero, not "hibr") hiberfil header is untested (ntfs.c:536 mutant survives) |
| shrinktest | wired (unit, sudo) | green 10/10; mutant: achieved size not read back (shrink.c:358) -> 1 of 10 |
| handoverlinuxtest | wired (unit, sudo; needs exfatprogs) | green 19/19; mutants: disk read-back check off (handover.sh:138) -> 1 of 19; free-room check off (handover.sh:98) -> 2 of 19 |
| exetest | wired (unit, after Wine, before signtest) | green 17/17 with GITHUB_REPOSITORY set (CI sets it); mutant: a server that ignores Range no longer restarts at 0 (plat_sim.c:1254), sim rebuilt -> 1 of 17. ORDER MATTERS: it runs build/aurbridge, which rewrites out/aurbridge*.exe |
| matrixtest | wired (e2e matrix) | green 10/10 (3m38 under KVM); mutant: BitLocker signature check off (ntfs.c:54), staging rebuilt -> 1 of 10 wrong |
| loadertest | wired (e2e matrix) | green 18/18 (1m18 under KVM); mutant: BootNext never armed (loader.c:208), staging rebuilt -> 1 failed |
| failtest | wired (e2e matrix, image) | needs the full image: NOT run on homebase (disk 17 GB free). First proof is the CI run, plus a `*mut-*` branch |
| renametest | already wired by 229316c | green 21/21; mutant: the store renamed "Software" in build/forge's table (747) -> 1 of 21 |
| theftunit | see below | green 49/49 |
| apptest | retired | a launcher: `exec python3 tools/parity/run.py --checks apps`, which parity.yml runs itself (phase 2). Not a test with its own verdict |

## Traps

- tools/README.md's compile lines for keytest/wlhostile name `build/gen/`; the
  generated protocol code is committed at `src/aurwl/gen/`. `tools/ctests.sh`
  uses the committed path and the same source list as `tools/wltest-deco.sh`.
- Homebase runs: `rsync` the worktree to `~/work/auros/ciw/`, then
  `docker run --rm --memory 4g -v "$PWD":/w -w /w ciw-env sh tools/ctests.sh`.
