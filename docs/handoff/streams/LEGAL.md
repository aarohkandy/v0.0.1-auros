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
6. `/etc/auros/release`: the written offer says "if this computer has the file /etc/auros/release, it names the
   release". Make it true: `build.yml` passes `AUROS_BUILD_TAG="$TAG"` into `build/all`, and `build/forge`
   (stage_configure, next to os-release) writes `[ -n "${AUROS_BUILD_TAG:-}" ] && echo "$AUROS_BUILD_TAG" > "$RFS/etc/auros/release"`.
   Until then the offer's second route (the `auros` package version, which is in PACKAGES.txt) works.
7. `marks` (in sources.yml) runs `tools/legal/check.py image` on the build's rootfs sample: it fails on Ubuntu
   branding packages or an os-release/issue that says Ubuntu and only warns about `/etc/lsb-release`. Add it to
   `promote`'s `needs` once stream C has diverted lsb-release (TM-1 item 1).
8. `docs/legal/signpath-application.md` (yours): COMPLIANCE 4.9 lists its edits (say Setup reads the connected Wi-Fi
   profile only with the tick and the BitLocker pause; a reputation paragraph; "plans to apply" like the site; soften
   "The signature tells Windows who made it" to what a SignPath Foundation signature means; "Windows and every file stay"
   -> "kept"). `tools/legal/check.py` does not scan it.
9. `check.py --launch` (run it before any `[latest]`) lists what is still open for the public link: the owner's mailbox,
   the Ready-page lines and resources 200/202 in the installer, and the review log.

### B (installer: `src/aurbridge/wizard.c`, `build/aurbridge`), via the lead -- the exact strings
`tools/legal/check.py` has dated allowances (until 2026-11-30) for the old absolute words in `wizard.c`;
it goes red after that date, and green-without-allowance once these land.

1. `page_ready` first row (now "Windows and all your files stay, and there is enough room for AurOS."):
   `Windows and your files are kept, and there is enough room for AurOS.`
2. One more G_INFO line on the same page, not blocking (it may replace another row to keep the page near
   80 words and on 1366x768 without scrolling): `Any change to a drive carries some risk, so copy photos you can’t replace to a memory stick first.`
3. DIRECTLY above the Install AurOS button, normal body size and contrast (NOT `C_QUIET`):
   `By pressing Install AurOS you agree to the AurOS Terms.` and, beside or under it, a text button
   `Read the Terms` that opens an in-wizard read-only pane with resource 202 (works offline; no browser).
   Write `terms accepted: v1, <timestamp>` to the install journal (local only). "v1" is the Terms' version
   (`docs/legal/TERMS.md` line 3). Keep Ready's one primary button `Install AurOS`; update the navtest strings.
4. `page_welcome` subtitle (now "... Windows stays, with all your files. ..."): `AurOS is a free, simple new way to use this computer. Windows and your files are kept. If AurOS isn’t for you, you can go back to Windows.`
5. `SAY(...)` for PAGE_READY (now "Ready to install. Windows and all your files stay on this computer. ..."): say
   `Windows and your files are kept.` and then the risk sentence of item 2, then the rest.
6. A `Licenses` button in the wizard footer that opens a read-only pane with resource 200.
7. Resources (`src/aurbridge/aurbridge-info.rc` / `build/aurbridge`): RCDATA 200 = `docs/legal/notices/NOTICES.txt`
   (UTF-8), RCDATA 202 = `docs/legal/notices/TERMS.txt`. NOTICES.txt contains `@AUROS_RELEASES@`: stamp it with
   `sh tools/repo.sh releases` before embedding, as `build/forge` does for SOURCE-OFFER, and refuse a build
   if `@AUROS_` is left. `tools/manifesttest.sh` should require 200 and 202 present and non-empty.
   `python3 -I tools/legal/notices.py` regenerates both files; `tools/legal/check.py` fails if they are stale.
8. The pictures on the website that show these screens (`website/assets/shots/setup-*.jpg`) must be
   re-shot by D after 1-6 land; their alt text no longer repeats the old sentence.
9. Not B's, for A2: `src/aurscreen/screen.c` "Putting Windows back exactly as it was." -> "Putting Windows back." and
   "Windows is back, with every file in it." is only true after the engine's own verification: keep it only if
   it is shown after that check (it is a statement of what was just verified).

### D (website)
- Done by LEGAL (smallest edits, commit b1766ec): the legal footer block on index/guide/advanced/status/code-signing
  (generated: `python3 -I tools/legal/mkpages.py`; do not hand-edit between the `legal-footer` markers), the
  "Licenses and source code" link under both Get AurOS buttons (one `.under-get` rule in site.css), the softened claims,
  `code-signing.html` "plans to apply". `node tools/site/check.mjs`: 404 passed, 0 failed (homebase).
- For D: `check.mjs` STALE list still says "updates itself" is false; since S5 it is true (update channel proven):
  the Terms say "AurOS gets security fixes ... while your computer is online" to stay clear of that regex until you update it.
- A later `[latest]` publishes these pages: do it only after the two skeptic reviews in `docs/legal/REVIEWS.md` are marked done.

### K (help) and C (aurshell)
- Done by LEGAL: `rootfs/usr/share/auros/help/licenses.html` (+ the index entry, + `licenses.html` in `tools/helptest.py` TOPICS; 46 checks green).
- For C: aurshell Settings > About should link/open that help page (file `src/aurshell/*`, not touched);
  `build/forge` is to `dpkg-divert` /etc/lsb-release to say DISTRIB_ID=AurOS (TM-1 item 1; `check.py image` warns until then).

## Traps

- The contact address is the literal placeholder `CONTACT_EMAIL` everywhere until the owner
  creates a mailbox. `tools/legal/check.py --launch` fails while any page, notice or release note
  still carries it; the ordinary check only counts them.
- This worktree's remote is `aarohkandy/v0.0.1-auros`. The tag a release carries points at the tip
  of `main` when it was published, not at the built commit (release.yml says so in the notes):
  the exact built commit is in the notes and in the sources release.
