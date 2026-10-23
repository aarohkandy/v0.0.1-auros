# Stream LEGAL: licences, notices, terms, privacy, and the honesty of every claim (session B)

Repository `aarohkandy/v0.0.1-auros`, branch `main`. Rules: `docs/handoff/FIELD-MANUAL.md`.
Brief: `docs/legal/COMPLIANCE.md` sections 1, 2 and 4 (the LEGAL packages). Owner decision
D15: **no outside lawyer**; agents do the legal work and review each other adversarially, and
every document says plainly it was prepared without a lawyer. Nothing in `docs/legal/` is legal
advice.

**Owns.** `docs/legal/**`, `docs/PRIVACY.md` (does not exist; the policy lives in
`docs/legal/PRIVACY.md`), the new `website/{licences,terms,disclaimer,privacy}.html` pages,
`rootfs/usr/share/doc/auros/**`, `build/sources`, `tools/legal/**`.
Shared (smallest edit, said in the commit body): footers of the other website pages (D),
help pages (K), `build/aurbridge` resources and the Ready page (B/A2), `.github/workflows`
(E), `docs/ops/INCIDENTS.md` (OPS).

## Order of work (from the lead)

1. LIC-1  source + written offer for what we distribute (`build/sources`, `sources-<tag>` release).
2. LIC-2/3/5, TM-1/2/3  licences page + texts, non-affiliation line, mark check.
3. LIA-1/2, WEB-1/3, PRI-1/2/7, LIA-5  terms, disclaimer, Ready-page strings, honest wording, privacy.
4. TM-4  free knockout search for "AurOS" (`docs/legal/NAME-SEARCH.md`).
5. Keep `docs/legal/COMPLIANCE.md` status current; two skeptics review each public document.

## Status

(updated as each step lands; commit hashes below)

- Step 1 LIC-1: in progress.

## Hand-offs to other streams (via the lead)

(filled in as they arise: exact strings for B, wiring for E, edits for D/K/C)

## Traps

- The contact address is the literal placeholder `CONTACT_EMAIL` everywhere until the owner
  creates a mailbox. `tools/legal/check.py --launch` fails while any page, notice or release note
  still carries it; the ordinary check only counts them.
- This worktree's remote is `aarohkandy/v0.0.1-auros`. The tag a release carries points at the tip
  of `main` when it was published, not at the built commit (release.yml says so in the notes):
  the exact built commit is in the notes and in the sources release.
