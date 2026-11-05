# Master plan: AurOS, good enough for the owner's grandmother

Written 2026-11-04. Done means: she types the address, presses Get AurOS, gets through Windows'
warnings, the three screens and the restart, and can then live on it without anyone beside her.

## What was found before starting

- `~/auros` `main` = `origin/main` = `8cf8a056`. No newer copy exists anywhere else: not upstream, not
  `auros_private` (September), not homebase.
- **The newer thing the owner remembered is the Sky look**: `claude/practical-leakey-1b740b`, two commits
  from 2026-11-01, never merged, newer than `main`'s night look. It goes in.
- **The Vercel code is in the repo and working**: `vercel.json` + `tools/site/vercel-build.sh`. Two Vercel
  projects build from `main`: `1os.vercel.app` and `v001-auros.vercel.app`, both public. Docs-only commits
  show as CANCELED there on purpose (`ignoreCommand`); that is not a failure.
- **Blocker 1: the Get AurOS button 404s.** No release is latest. Candidate `4e7957d5` failed its
  bitlocker e2e on a test that contradicted the design (see `SESSION-2026-11-04.md`); `e503f76c`
  replaced it and is green.
- **Blocker 2: the installer's help address 404s.** It bakes `aarohkandy.github.io/v0.0.1-auros`, and Pages is
  off. Fix: the workflows pass `AUROS_SITE` so builds bake the Vercel address (default `1os.vercel.app`,
  `vars.AUROS_SITE` overrides). `e503f76c` has it.
  Turning Pages on instead (`AUROS_PAGES=on`) was refused by the agent's permission check as a new public
  surface, so it stays the owner's choice.
- Legal launch gate: passes, 0 problems.
- homebase disk is 98% full (5.7 GB free). No VM installs there.

## The order

1. **Base.** `wave/base` = `main` + retire-fastpath + othersys-cinnamon + putback-wording, stale comment fixed.
2. **The wave** (workflow `auros-grandma-wave`, all Sonnet, each lane: build → adversarial attack → fix),
   each on its own `wave/<lane>` branch, nothing pushed:
   - `sky`, then `gm-site` and `gm-desktop` on top of it (they share its files);
   - `othersys-powercut`, `othersys-budgie`;
   - `gm-wizard`, `gm-boot`, `gm-help`, `gm-everyday`, `safety`;
   - a final critic: what would still stop her?
3. **Candidate.** `e503f76c` (build run 41939828813) and its harden are green. `[publish 41939828813] [latest]`
   was refused by the agent's permission check (public surface), so publishing is the owner's step.
4. **Integrate.** The lead reads every diff, merges into `wave/base`, runs every check.
5. **One push to main** with `[release] [othersys]`: one VM run proved all othersys lanes. That second
   release failed on tests that had not kept up, so a third `[release]` carries their fixes.
6. **Then** harden → `[publish N] [latest]`, the owner's step as above. Never `[release] [latest]` in one commit.
   Done: `v0.1.0-b18733c` is the latest release, published by the owner in `b3ea3c9a`.
7. **Walk it live** from `1os.vercel.app`, then update `SESSION-2026-11-04.md`.

## What no agent can do

A physical PC has never run this. A VM cannot prove a real one: drivers, odd firmware, a real grandmother.
The first real PC should be a spare, by someone who can put Windows back, before it reaches her.
