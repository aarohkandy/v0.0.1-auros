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

## Status (2026-10-23; commits on `main`)

- **LIC-1 done and self-running.** `.github/workflows/sources.yml` **sweeps every three hours**: every published AurOS release
  without a sources release gets one from the build run named in its notes, so no one has to wire anything; it adds the
  source pointer line to that AurOS release's notes and attaches `NOTICES.txt` to it (add only); it re-checks every
  `sources-*` release against GitHub's digests and fails if an AurOS release older than a day has none. Publishing goes
  through `tools/legal/publish.py` (draft first; a published release is only replaced by a complete, digest-checked
  set, never emptied first). Tests: `build/sources` + `tools/legal/sourcestest.py` (11 mutants), `publishtest.py` (a pretend
  GitHub, 5 mutants), `stagingcheck.py` (the real staging cpio's 12 Ubuntu packages are all in NOTICES), all in `legal.yml`.
  **The published `sources-v0.1.0-618726b` was damaged on 2026-10-23 by cancelling a run that had already started replacing
  it with the old unsafe tool (a part was gone): it is being remade with the safe one** (`[sources v0.1.0-618726b
  39905858387 again]`); see the end of this file for what it ended as.
- **LIC-2/3/5, TM-1/2/3 done in the repo:** `website/{licenses,terms,disclaimer,privacy}.html` (generated from `docs/legal/*.md`
  by `tools/legal/mkpages.py`), one legal footer on every page, the one trademark paragraph (`docs/legal/TRADEMARKS.txt`), notices
  (`tools/legal/notices.py`), `docs/RELINK.md`, the in-OS help page, `tools/legal/check.py` (pages, claims, marks, image,
  `--launch`). Terms/Disclaimer/Privacy/Licenses are **v3** after two skeptic rounds.
- **LIA-1/2, WEB-1/3, PRI-1/2/7, LIA-5 done in the texts;** the installer half is stream B's (strings below).
- **TM-4 done, and it found a real problem** (`docs/legal/NAME-SEARCH.md`): UserTesting rebranded as Auros on 29 Sep 2026 and
  filed AUROS in four countries; Emergent Systems holds US reg. 4512239 AUROS in class 9. Owner decision before the public link.
- **Four skeptic reads in two rounds** (`docs/legal/REVIEWS.md`, 69 findings, each answered; open ones belong to B, A2, D, C, E, F or the owner).
- **Not linked publicly until:** `python3 -I tools/legal/check.py --launch` is clean (the owner's mailbox, the installer's agreement
  line / risk sentence / Licenses button / resources 200 and 202, no finding marked NOT DONE). It now runs as a step of `pages.yml`
  `deploy` and of `release.yml` `site` (only for a run that would become [latest]), so the public site and a [latest] release wait for it.
- **Owner:** a mailbox (`check.py set-contact`), the name decision, Mozilla (`MOZILLA.md`), optional mirror of each sources release.

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
   verifies. It needs the build run's `auros-rootfs` artifact (14 days). Until this is wired the 3-hourly sweep does the same
   job, so a release is never without sources for long; wiring it makes `promote` wait for them.
2. The notes in `publish` ("notes.md"): append `sh tools/legal/notes.sh line "$TAG"` (two lines: the sources release
   and the offer).
3. `tests.yml`: add `python3 -I tools/legal/sourcestest.py` and `python3 -I tools/legal/check.py` (seconds; legal.yml also runs them).
4. `build.yml` / `build-info.txt`: record the BUILD HOST's glibc, one line, so the sources job need not guess:
   `dpkg-query -W -f='${source:Package} ${source:Version}\n' libc6-dev >> dist/installer/host-sources.txt`
   (the installer artifact then carries it; `sources.yml` reads the publishing runner's until it does).
5. `profiles.yml` prune must skip `sources-*` (it prunes only `profiles-*`; keep it so).
6. `/etc/auros/release`: the written offer says "if this computer has the file /etc/auros/release, it names the
   release". `build/forge` now writes it from `AUROS_BUILD_TAG` (done by LEGAL, a no-op without it). **E: `build.yml` must pass
   `AUROS_BUILD_TAG="$TAG"` in the `sudo env ... ./build/all desktop` line.** Until then the offer's second route (the `auros`
   package version, which is in PACKAGES.txt) works.
7. `marks` (in sources.yml) runs `tools/legal/check.py image` on the build's rootfs sample: it fails on Ubuntu
   branding packages or an os-release/issue that says Ubuntu and only warns about `/etc/lsb-release`. Add it to
   `promote`'s `needs` once stream C has diverted lsb-release (TM-1 item 1).
8. `docs/legal/signpath-application.md` (yours): COMPLIANCE 4.9 lists its edits (say Setup reads the connected Wi-Fi
   profile only with the tick and the BitLocker pause; a reputation paragraph; "plans to apply" like the site; soften
   "The signature tells Windows who made it" to what a SignPath Foundation signature means; "Windows and every file stay"
   -> "kept"). `tools/legal/check.py` does not scan it.
9. `check.py --launch` is now a step of `release.yml` `site` (LEGAL added it, one step, for a run that would become [latest]) and of
   `pages.yml` `deploy`; it lists what is still open for the public link: the owner's mailbox, the Ready-page lines and
   resources 200/202 in the installer, and the review log. Do not remove it.
10. A tag must never be moved or deleted for a release whose `sources-<tag>` exists, and never cancel a `sources` run that may be
   uploading (the swap in `publish.py` takes minutes; a cancel in the middle needs the next `again` to clean up).

### B (installer: `src/aurbridge/wizard.c`, `build/aurbridge`), via the lead -- the exact strings
`tools/legal/check.py` has dated allowances (until 2026-11-30) for the old absolute words in `wizard.c`/`screen.c`; it goes red
after that date, and green-without-allowance once these land. The skeptic review (`docs/legal/REVIEWS.md`) rewrote item 1.

1. `page_ready` first row (now "Windows and all your files stay, and there is enough room for AurOS.") becomes a row that
   names the one real hazard, in her words (the text may be two rows; the page must still fit 1366x768 without scrolling):
   `Windows and your files are kept. Setup makes Windows smaller to fit AurOS. If the power fails or the computer crashes during that step, Windows or your files could be damaged and might not come back. Copy anything you can’t replace to a memory stick first.`
2. DIRECTLY above the Install AurOS button, normal body size and contrast (NOT `C_QUIET`):
   `By pressing Install AurOS you agree to the AurOS Terms.` and, beside or under it, a text button
   `Read the Terms` that opens an in-wizard read-only pane with resource 202 (works offline; no browser).
   Write `terms accepted: v3, <timestamp>` to the install journal (local only). "v3" is the Terms' version
   (`docs/legal/TERMS.md` line 3). Keep Ready's one primary button `Install AurOS`; update the navtest strings.
3. `page_welcome` subtitle (now "... Windows stays, with all your files. ..."): `AurOS is a free, simple new way to use this computer. AurOS is made to keep Windows and your files. If AurOS isn’t for you, you can go back to Windows.`
   and the line "This only looks. Nothing on this computer is changed." is true only until the check passes: from then on the
   download (about 2 GB; the manifest has the size) is running into `C:\AurOS\` (`check_passed`/`download_begin`); closing Setup
   before Install deletes it (`install_forget`), a power cut can leave part. Say so on the page the check ends on ("AurOS is
   downloading while you read, about 2 GB. If your internet has a data limit, close this window: closing deletes it.") -- the Terms and privacy page now say it, and S-22 (the room check ignores this download) is B/A2's.
4. `SAY(...)` for PAGE_READY: say the new first row, then the rest.
5. The install-stopped page (now "It stopped before the restart. Your files are safe. ..."): `Setup stopped. If Windows does not start, ask someone to help you.` plus the specific plain reason; never "safe".
6. A `Licenses` button in the wizard footer (the exact word `Licenses`; `check.py --launch` looks for `L"Licenses"`) that
   opens a read-only pane with resource 200.
7. Resources (`src/aurbridge/aurbridge-info.rc` / `build/aurbridge`): RCDATA 200 = `docs/legal/notices/NOTICES.txt`
   (UTF-8), RCDATA 202 = `docs/legal/notices/TERMS.txt`. NOTICES.txt contains `@AUROS_RELEASES@`: stamp it with
   `sh tools/repo.sh releases` before embedding, as `build/forge` does for SOURCE-OFFER, and refuse a build
   if `@AUROS_` is left. `tools/manifesttest.sh` should require 200 and 202 present and non-empty.
   `python3 -I tools/legal/notices.py` regenerates both files; `tools/legal/check.py` fails if they are stale.
8. The pictures on the website that show these screens (`website/assets/shots/setup-*.jpg`) must be
   re-shot by D after 1-6 land; their alt text no longer repeats the old sentence.
9. Not B's, for A2: `src/aurscreen/screen.c` "Putting Windows back exactly as it was." -> "Putting Windows back." and
   "Windows is back, with every file in it." is only true after the engine's own verification: keep it only if
   it is shown after that check. "Your Windows files are safe." (stopped screens) -> say what was verified.
10. Round 2 (skeptic C): the Ready page's "what happens" list should also say that Fast Startup and hibernation are turned off and stay
   off while AurOS is there, that AurOS's start-up files go on the start-up partition, and that Setup leaves a Windows task "AurOS put-back"
   (SYSTEM, at every start, files in `C:\ProgramData\AurOS`) that does nothing until AurOS is removed. The Terms (section 4) say all of it.
   If Setup is closed by the person who pressed Install for someone else, nothing special is needed; the Terms (section 2) cover it.
11. `docs/AURBRIDGE.md`-level facts the Terms now state and the installer should too: Fast Startup and hibernation are
   turned off first (`phase_prepare`); after "No, go back to Windows" AurOS (about 28 GB) stays until Put Windows back.

### D (website)
- Done by LEGAL (smallest edits, commit b1766ec): the legal footer block on index/guide/advanced/status/code-signing
  (generated: `python3 -I tools/legal/mkpages.py`; do not hand-edit between the `legal-footer` markers), the
  "Licenses and source code" link under both Get AurOS buttons (one `.under-get` rule in site.css), the softened claims,
  `code-signing.html` "plans to apply". `node tools/site/check.mjs`: 404 passed, 0 failed (homebase).
- For D: `advanced.html` #room says "Setup shows both numbers before it changes anything" but `page_ready` shows no size (the 28 GB appears only when it blocks): either B adds the size to Ready or the sentence goes. `check.mjs` STALE list still says "updates itself" is false; since S5 it is true (update channel proven):
  the Terms say "AurOS gets security fixes ... while your computer is online" to stay clear of that regex until you update it.
- Round 2 left for D: the h1 "Your computer, new again." (skeptic C 3: a promise as an adjective; the repo's own rule is "reassure with facts, never adjectives"; LEGAL added the facts to the hero and Step 3, it did not change the headline); the "25 to 60 minutes" assumes 24 Mbit/s and an SSD (`ETA_LINK_BPS`); `check.mjs` STALE list still has regexes for old claims ("updates by itself"), which is why LEGAL words some sentences around them; the screenshots of Welcome and Ready (`assets/shots/setup-*.jpg`) must be re-shot after B's strings land; LEGAL removed the Wishes line from `status.html` (Wishes is not in this version) and reworded two "Your computer is fine/safe" lines in help (K).
- A later `[latest]` publishes these pages: do it only after the two skeptic reviews in `docs/legal/REVIEWS.md` are marked done.

### K (help) and C (aurshell)
- Done by LEGAL: `rootfs/usr/share/auros/help/licenses.html` (+ the index entry, + `licenses.html` in `tools/helptest.py` TOPICS; 46 checks green).
- For C: `check.py image` (sources.yml `marks`) fails on `ubuntu-mono` in the published image: find what pulls it in and purge it (`packages_exclude`); `ubuntu-advantage-desktop-daemon` and `ubuntu-pro-client` are S5's (masked), the Canonical IP policy is about artwork and names, not these. aurshell Settings > About should link/open that help page (file `src/aurshell/*`, not touched);
  `build/forge` is to `dpkg-divert` /etc/lsb-release to say DISTRIB_ID=AurOS (TM-1 item 1; `check.py image` warns until then).

## Traps

- The contact address is the literal placeholder `CONTACT_EMAIL` everywhere until the owner
  creates a mailbox. `tools/legal/check.py --launch` fails while any page, notice or release note
  still carries it; the ordinary check only counts them.
- This worktree's remote is `aarohkandy/v0.0.1-auros`. The tag a release carries points at the tip
  of `main` when it was published, not at the built commit (release.yml says so in the notes):
  the exact built commit is in the notes and in the sources release.
- Adding a Wayland protocol file under `src/aurwl/gen/` (or changing LICENSE, TERMS.md, the website fonts' OFL.txt)
  makes `legal.yml` red until `python3 -I tools/legal/notices.py` (and `tools/legal/mkpages.py` for the Markdown
  texts) is run and the result committed: the notices are generated, never typed. Say so to the stream that hits it.
- The Python tools run on 3.9 (the Mac) and 3.12 (CI): no `match`, no `X | Y` types.
- `sources.yml` needs the build run's `auros-rootfs` artifact, which lives 14 days. Make a release's sources before then
  (`[sources TAG RUN-ID]`), or the offer for that release cannot be kept.
- The sources job takes about 30 minutes (2.7 GB from Launchpad) and the upload another 10; it is serialised per tag.

