# LEAD RESUME — how a fresh agent takes over as lead, mid-flight

The owner (Aaroh) is asleep or away. His standing order: **"get it done end
to end ... think about everything and swarm it ... make sure you restart if
you hit session limits."** A scheduled task on his Mac (`auros-lead-watchdog`,
hourly) runs the check in §1 and, if the swarm is down, starts you with this
file. Follow it exactly; it is designed so a lead with zero context can
resume at full speed.

## 1. Is the swarm alive? (do this first, in 30 seconds)

```sh
M="/Users/aaroh/Desktop/auros v0.0.1/v0.0.1-auros"
git -C "$M" fetch -q origin && echo $(( ($(date +%s) - $(git -C "$M" log -1 --format=%ct origin/main)) / 60 )) minutes since the last push
```

- **Under 60 minutes: the swarm is alive. Do nothing and stop.** (Twenty-odd
  agents push every 15 minutes; a live lead is running them.)
- 60 minutes or more: the swarm is down (usage limit, crash or app restart).
  You are the lead now. Continue below.

## 2. Become the lead (15 minutes of reading, no shortcuts)

1. Push a marker at once so the next hourly check sees a live swarm:
   append `- <HH:MM UTC> Lead resumed by the watchdog (swarm was down N min).`
   to the Progress log in `docs/handoff/SESSION-2026-10-23-B.md`, commit, push.
2. Read: `START-HERE.md`, `docs/handoff/FIELD-MANUAL.md` (environment and rules:
   Mac + homebase + Actions, worktrees, push loop, culture),
   `docs/handoff/METHODOLOGY.md` (the playbook), this file, the
   **whole** `docs/handoff/SESSION-2026-10-23-B.md` (decisions D12-D15, every
   routing, every stream's orders), `docs/handoff/CURRENT-STATE.md`,
   `docs/PLAN-END-TO-END.md` §3 (critical path + hard ordering rules) and §5,
   `docs/ROADMAP.md`, `docs/AI.md` §7, `docs/DESIGN-DIRECTION.md` §1-§3.
3. Fact-check the state yourself (never trust a note): `gh run list -R
   aarohkandy/v0.0.1-auros -L 40`, `gh release list -R aarohkandy/v0.0.1-auros`,
   `git -C "$M" log --oneline -40 origin/main`, each `docs/handoff/streams/*.md`.

## 3. Salvage unpushed work, then respawn every stream

Each stream worked in `$S/wt-<id>` (S = the scratchpad path in FIELD-MANUAL
§1; if your session has a different scratchpad, the old one is still on
disk). For each `wt-*`:

```sh
git -C "$S/wt-<id>" status --short | head      # uncommitted work?
git -C "$S/wt-<id>" log --oneline origin/main..HEAD   # committed, unpushed?
```

Do not push someone else's half-work blindly: the respawned stream inspects
its own worktree first (its prompt says so). Then respawn **one background
agent per stream not marked DONE**, all in one message, with:

> You are stream `<ID>` of AurOS (session B, resumed by a new lead). Read
> START-HERE.md, docs/handoff/FIELD-MANUAL.md, docs/handoff/STREAMS.md,
> docs/handoff/streams/`<ID>`.md, your orders in
> docs/handoff/SESSION-2026-10-23-B.md (search for your ID) and your brief in
> docs/PLAN-END-TO-END.md §4 if you are listed there. Your previous worktree
> `<S>/wt-<id>` may hold unpushed commits or uncommitted work: inspect it
> first and salvage what is good (rebase onto origin/main, re-run its tests,
> push). Then continue your stream at full speed. Push every green step, at
> least every 15 minutes. Keep docs/handoff/streams/`<ID>`.md current. Final
> report ≤250 words.

| Stream | Status at 07:30 UTC 2026-10-23 |
|---|---|
| E pipeline (+ SignPath wiring, D12) | running |
| F real-Windows e2e (bitlocker variant is a v0.1 gate, D13) | running |
| A2 fast path (+ BitLocker pause, D13; UTC clock fix first) | running |
| B wizard (data-safety fixes, design P0s, wordlint) | running |
| D website (design P0s, true promises, wordlint) | running |
| C desktop (shared password, look reset, design P0s) | running |
| BT boot chain (WP-30 boot fallback first) | running |
| W1 Wishes engine / W2 Wishes UI+voice / CL cloud | running |
| S1 hardening / S3 fuzzing / S4 encryption+PIN / S5 updates / R Ferry | running |
| P profiles / OPS / HW / PARITY / USABILITY | running |
| K help, S2 scams, M portability | **DONE** (respawn only for new findings) |
| H other OSes | **paused** (phase 2) |
| LEGAL | spawn after `docs/legal/COMPLIANCE.md` lands (its §4 LEGAL package) |

## 4. Workflows (adversarial review), scripts in `tools/workflows/`

Run with the Workflow tool (`scriptPath`). If an output doc is missing, its
run died: re-run it.

| Script | Output |
|---|---|
| `auros-safety-review.js` | `docs/safety/FINDINGS.md` |
| `security-audit.js` | `docs/security/FINDINGS.md` |
| `auros-legal-review.js` | `docs/legal/COMPLIANCE.md` |
| `auros-atlas.js` | `docs/ATLAS.md` + `docs/handoff/CURRENT-STATE.md` (re-run when stale) |
| `auros-design-direction.js`, `auros-master-plan.js`, `auros-wishes-redteam.js` | done; re-run only to refresh |
| `review-changes.js` | per-area review of a commit range: run after every big landing |
| `launch-readiness.js` | `docs/LAUNCH-READINESS.md`: run before any public promote |

Workflow writers have twice held a finished doc on a local branch "because the
repo is public": every writer prompt must say pushing to main is authorized
and required; if one still holds it, cherry-pick and push it yourself.

## 5. The finish line and the rules you may not break

v0.1 done = `docs/PLAN-END-TO-END.md` §1 v0.1 checklist, in short: a
prerelease from a green build → the real-Windows e2e passes website →
download → wizard → restart → AurOS → "it works" → a program installed and
opened, on THAT exe, for the base variant AND the bitlocker variant (D13) →
every blocker/high in `docs/safety/FINDINGS.md` and `docs/security/FINDINGS.md`
fixed with a test → design P0s applied → then promote.

- **Never `[latest]` (public promote) unless all of these hold**: the e2e
  above is green on that exact exe; no open blocker in the safety/security
  findings; the update keyring is in the image (it is: afa7755); and the owner
  has either bought the domain (decision 1, WP-15) or said in chat to promote
  without it. Until then, prereleases only.
- **Never promote (or hand anyone) an image built before `956af4a`**: those images never install Ubuntu security
  updates (TM-1). The promoted build must also contain `6917bde` (cannot-measure), WL's subsurface fix
  (`54b783b`..`7799b6d`) and pass `tools/updatepolicytest.sh dryrun` in its own unit job.
- Wishes are not in the v0.1 image (D14). Code signing via SignPath (D12)
  needs the owner to submit the application; prepare, don't block on it.
- Never ship an unproven destructive path. Prove every check fails when broken.
- The owner wants **speed, width, Apple-grade design, data safety first**,
  and **pushes to GitHub constantly**. Report to him in plain words: what
  works (proven where), what doesn't, what he must click.

### Switching the shipped install route to the fast path (one decision, two lines)

When the fast-path installer passes the real-Windows e2e (base AND bitlocker) and the
safety/security highs on it are closed: set the default in `build/aurbridge`
(`${AUROS_FASTPATH:-0}` → 1) AND `<html data-build="fastpath">` in `website/index.html`
in the SAME commit; `tools/site/check.mjs` is red if only one changes. Then a normal
`[release]` → e2e → promote.

## 6. When he wakes up

Write `docs/handoff/MORNING-REPORT.md` (one page: what landed overnight with
proof links, what is red, the decisions waiting for him with a
recommendation each) and keep it current; it is the first thing he reads.
