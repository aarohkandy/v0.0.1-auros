# Stream USABILITY: is she ever confused? (measured, not assumed)

Brief: `docs/PLAN-END-TO-END.md` section 4 "USABILITY", WP-92 (early check) and WP-43
(the real test). Owns `docs/usability/**` and `docs/research/people-*.md`.
Worktree `wt-usability` (branch `wt-usability`, pushes to `main`).

## State

- Started 2026-10-23 (session B). Read: START-HERE, FIELD-MANUAL, PLAN-END-TO-END,
  EASY, DESIGN-DIRECTION, journey-audit, every screenshot in docs/shots/ (website,
  wizard, shells, staging, guard).
- **Trap: the website shots in docs/shots/website/ were replaced by D at 54fd273 (design
  direction applied). The wizard and staging shots are still the DARK, pre-restyle
  ones (wizard 9b08c47/993ecf3, staging cded653, shells 6831fed). Re-read the website
  shots before you cite them, and say in the walkthrough which look each finding
  was seen on.**

## Next steps, in order

1. `docs/research/people-walkthrough-2026-10.md`: the cognitive walkthrough, three
   personas (85 / 72 / 78), screen by screen, findings table (id, persona, screen,
   what happens, severity, fix, owner stream), top 15 to the lead.
2. `docs/usability/SCRIPT.md` (+ `SCORING-SHEET.md`, `CONSENT.md`, `RECRUITING-NOTE.md`,
   `RUNNING-IT.md`) for WP-43: tasks in her words, per-step stopwatch table,
   pass = 4 of 5 unaided in under 60 minutes, zero data loss.
3. Re-run the walkthrough after C/B/D/K restyle lands (wizard + staging + desktop
   shots go light): `docs/research/people-walkthrough-2026-10-restyle.md`.
4. Real sessions need the owner (he recruits). Each recruit is screened with
   `AurOS-Setup.exe --report` (WP-32; not landed yet) so a refused PC wastes no session.

## How to verify

Every finding cites a screenshot path or a source file:line. `git grep` the quoted
string; if it is gone, the finding is fixed, move it to "closed".
