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

(Later rows are added as each test is run.)

## Traps

- tools/README.md's compile lines for keytest/wlhostile name `build/gen/`; the
  generated protocol code is committed at `src/aurwl/gen/`. `tools/ctests.sh`
  uses the committed path and the same source list as `tools/wltest-deco.sh`.
- Homebase runs: `rsync` the worktree to `~/work/auros/ciw/`, then
  `docker run --rm --memory 4g -v "$PWD":/w -w /w ciw-env sh tools/ctests.sh`.
