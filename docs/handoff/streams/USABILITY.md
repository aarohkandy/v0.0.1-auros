# Stream USABILITY: is she ever confused? (measured, not assumed)

Brief: `docs/PLAN-END-TO-END.md` section 4 "USABILITY", WP-92 (early check) and WP-43
(the real test). Owns `docs/usability/**` and `docs/research/people-*.md`.
Worktree `wt-usability` (branch `wt-usability`, pushes to `main`).

## Done (all pushed to main)

| Commit | What |
|---|---|
| `d7bd1db`, `7f971f9`, `9e37cb1`, `f7902de` | `docs/research/people-walkthrough-2026-10.md`: the cognitive walkthrough, three personas (Edith 85, Ray 72, Margaret 78), nine stages, 59 findings with id, severity, who, evidence, fix, owner; top 15 in section 4; hypotheses for the sessions in section 5 |
| `1fd307a` | `docs/usability/`: `SCRIPT.md` (tasks in her words, pass rule, aid ladder, safety stops, data check), `SCORING-SHEET.md` (per-step stopwatch tables keyed to the findings), `CONSENT.md`, `RECRUITING-NOTE.md` (note, 10-question screening call, exclusions), `RUNNING-IT.md` (stations, equipment, what is proven), `wizard-sim/index.html` (click-through of the real light wizard shots, logs every hit and miss) |
| `9e37cb1`, `f7902de` | `docs/usability/renders/2026-10-23/`: real Light renders of the first-run question and the four desktops (`tools/journeytest.sh`) and of every Wishes screen (`tools/wishsheet.c`) |

## The fifteen that matter most (full text: walkthrough section 4)

1. D-03 staging prints "the Windows drive is now 8 MB" and device paths in 12 px (A2)
2. D-04 the longest staging step has no movement or estimate (A2)
3. D-01 one to two minutes of black after Restart now, unexplained (B, A2, K)
4. C-08 "Restart later" hands the install to an unsupervised start (B, A2)
5. D-05 the red failure screen: "AurBridge", "Start installing", "off and on again", no automatic way back (A2, B, K)
6. B-01 the website's Edge picture is not the Edge she has; the dark Delete is the loudest button (D, F)
7. A-01 the phone-to-PC link is `mailto:` with no recipient (D)
8. E-01 "Can you see your photos?" asks before the photos have come across (C, R; inferred)
9. E-02 "Does the internet work?" on Wi-Fi-only laptops, D10 off (A2, C)
10. F-01 "Bigger" enlarges only AurOS's own bars; no hook in GTK or Firefox (C, S2, P)
11. I-01 Put Windows back deletes what she saved in AurOS (R, WP-27)
12. B-02 antivirus quarantine, 25 to 40% of PCs, not on the site (D, K)
13. A-04/C-03 "10 to 25 minutes" is not measured (D, B, F)
14. C-01 "Fix these for me" restarts Windows without "save your work first" (B)
15. F-07 the lines under Yes / Let me look first / No are 12 px type (C)

## Half-done / next steps, in order

1. **Re-run after the staging restyle.** The wizard, website, first-run, desktop and
   Wishes are walked in Light. Staging is the one surface still dark
   (`src/aurscreen/screen.c` `PAPER 0B0E14`). When `docs/shots/staging-*.png` change,
   write `docs/research/people-walkthrough-2026-10-restyle.md` with the same ids and a
   "now" column (closed, smaller, same, new), and a new top fifteen.
2. **Walk the booted image** (nobody has): first start, GRUB, "Open Your files" (E-01),
   Bigger then Internet (F-01), the store at first open (G-01). PARITY has the image on
   homebase and a QMP screendump runner (`docs/handoff/streams/PARITY.md`); ask them or
   use `tools/firstboottest.sh`'s recipe.
3. **The sessions need the owner.** Mode E (WP-92, three people) can start now: the
   wizard sim plus an AurOS machine at the first-run question. What is missing is that
   machine: `tools/usability-vm.sh` (BT/A2) and `--report` (HW). See `RUNNING-IT.md`
   section 5 for who owes what.
4. **After the sessions:** `docs/research/people-<date>.md` per `SCRIPT.md` section 9,
   each finding routed to its stream.

## Traps

- **Citations drift.** The tree changes every few minutes; the walkthrough cites files,
  functions and quoted strings, not line numbers. `git grep` the string.
- `docs/shots/shells/*`, `docs/shots/staging-*.png`, `docs/shots/guard/*` are older and
  dark (shells). Use `docs/usability/renders/2026-10-23/` for the desktop.
- `docs/TRY-IT.md` still says "Drive encrypted: switches it off" (D11 says pause, never
  decrypt). K/B's to fix.
- The harness renders use the container's fonts (Inter), not the image's UI face, so
  widths (the "All my progra..." truncation, F-09) must be checked on the image.
- The click-through wizard's hot spots are measured pixels of the 1120x690 shots. When
  the shots change press `H` in the page and re-measure (RUNNING-IT.md section 2).
- Never leave a person with a half-done computer: `SCRIPT.md` section 4.5.

## How to re-render the desktop and Wishes (homebase; quick, under 10 minutes, 2 GB)

```sh
KEY=$HOME/.ssh/homebase_ed25519
rsync -az --delete --exclude .git --exclude docs/shots --exclude website -e "ssh -i $KEY" ./ azureuser@100.108.213.121:work/auros/usability/
ssh -i $KEY azureuser@100.108.213.121 'cd ~/work/auros/usability && mkdir -p out &&
  docker run --rm --memory 2g --cpus 2 -v "$PWD":/w -w /w \
    -v ~/work/auros/c-fonts:/usr/share/auros/fonts:ro auros-c:dev sh tools/journeytest.sh /w/out'
```
`tools/wishsheet.c` is built the way its header says (needs a `shell.conf` rendered with
`src/aurora/aurora render`, as `journeytest.sh` does). The container writes root-owned
files: remove them with `docker run --rm -v "$PWD":/w auros-c:dev rm -rf /w/out`, never
`sudo`. Keep `~/work/auros/usability/` under 3 GB (it is 15 MB).

## How to verify

Every finding cites a screenshot, a render or a source file and function. Quote the
string with `git grep`; if it is gone the finding is fixed: move it to "closed".
