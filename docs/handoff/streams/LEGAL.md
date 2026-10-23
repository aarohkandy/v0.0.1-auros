# Stream LEGAL: licenses, notices, terms, privacy, and the honesty of every claim (session B)

Repository `aarohkandy/v0.0.1-auros`, branch `main`. Rules: `docs/handoff/FIELD-MANUAL.md`.
Brief: `docs/legal/COMPLIANCE.md` sections 1, 2 and 4 (the LEGAL packages). Owner decision
D15: **no outside lawyer**; agents do the legal work and review each other adversarially, and
every document says plainly it was prepared without a lawyer. Nothing in `docs/legal/` is legal
advice.

**Owns.** `docs/legal/**`, `docs/PRIVACY.md` (does not exist; the policy lives in
`docs/legal/PRIVACY.md`), the new `website/{licenses,terms,disclaimer,privacy}.html` pages,
`rootfs/usr/share/doc/auros/**`, `build/sources`, `tools/legal/**`.
Shared (smallest edit, said in the commit body): footers of the other website pages (D),
help pages (K), `build/aurbridge` resources and the Ready page (B/A2), `.github/workflows`
(E), `docs/ops/INCIDENTS.md` (OPS).

## Order of work (from the lead)

1. LIC-1  source + written offer for what we distribute (`build/sources`, `sources-<tag>` release).
2. LIC-2/3/5, TM-1/2/3  licenses page + texts, non-affiliation line, mark check.
3. LIA-1/2, WEB-1/3, PRI-1/2/7, LIA-5  terms, disclaimer, Ready-page strings, honest wording, privacy.
4. TM-4  free knockout search for "AurOS" (`docs/legal/NAME-SEARCH.md`).
5. Keep `docs/legal/COMPLIANCE.md` status current; two skeptics review each public document.

## Status

(updated as each step lands; commit hashes below)

- Step 1 LIC-1 **DONE for the first prerelease** (4225a73, run 39975787998): `sources-v0.1.0-618726b` is
  published (prerelease; 539 source packages, 2 parts, 2.7 GB, `build/sources verify` run on the public
  copy; assets: 2 tar parts, SOURCES.txt, PACKAGES.txt, SHA256SUMS, auros-source-618726bd8436.tar.gz) and the
  notes of `v0.1.0-618726b` carry the line that points at it. `build/sources` + `tools/legal/sourcestest.py`
  (34 cases + 7 mutants), `.github/workflows/sources.yml`, `legal.yml`, `tools/legal/{check,notes}.py/sh`,
  `rootfs/usr/share/doc/auros/SOURCE-OFFER` (forge stamps the releases page into it).
  Still to do for LIC-1: E wires `sources.yml` into release.yml (below); CONTACT_EMAIL stays until the owner has a mailbox.
- Step 2 (licenses page, notices, marks check): in progress. Texts written in `docs/legal/{TERMS,DISCLAIMER,PRIVACY,LICENCES}.md`
  (v1, not linked from anywhere until two skeptic reviews have passed).

## Hand-offs to other streams (via the lead)

### E (release pipeline), via the lead
1. `release.yml`: after `publish`, add
   ```yaml
   sources:
     needs: [which, publish]
     uses: ./.github/workflows/sources.yml
     secrets: inherit
     permissions: { contents: write, actions: read }
     with: { run-id: ${{ needs.which.outputs.run-id }}, tag: ${{ needs.publish.outputs.tag }} }
   ```
   and add `sources` to `promote`'s `needs` (and its `if` must require `needs.sources.result == 'success'`).
   `sources.yml` makes the draft, uploads, publishes the prerelease `sources-<tag>`, downloads it again and
   verifies. It needs the build run's `auros-rootfs` artifact (14 days).
2. The notes in `publish` ("notes.md"): append `sh tools/legal/notes.sh line "$TAG"` (two lines: the sources release
   and the offer).
3. `tests.yml`: add `python3 -I tools/legal/sourcestest.py` and `python3 -I tools/legal/check.py` (seconds; legal.yml also runs them).
4. `build.yml` / `build-info.txt`: record the BUILD HOST's glibc, one line, so the sources job need not guess:
   `dpkg-query -W -f='${source:Package} ${source:Version}\n' libc6-dev >> dist/installer/host-sources.txt`
   (the installer artifact then carries it; `sources.yml` reads the publishing runner's until it does).
5. `profiles.yml` prune must skip `sources-*` (it prunes only `profiles-*`; keep it so).

## Traps

- The contact address is the literal placeholder `CONTACT_EMAIL` everywhere until the owner
  creates a mailbox. `tools/legal/check.py --launch` fails while any page, notice or release note
  still carries it; the ordinary check only counts them.
- This worktree's remote is `aarohkandy/v0.0.1-auros`. The tag a release carries points at the tip
  of `main` when it was published, not at the built commit (release.yml says so in the notes):
  the exact built commit is in the notes and in the sources release.
