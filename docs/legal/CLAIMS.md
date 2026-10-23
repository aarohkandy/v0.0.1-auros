# The claims ledger: what the legal pages say about the product, and what proves it

Prepared by the AurOS team without a lawyer. One row for every sentence in `TERMS.md`,
`DISCLAIMER.md`, `PRIVACY.md`, `LICENSES.md`, the website footer and the installer's Ready page that
says what AurOS does, sends, keeps or has been tested on. "Kind": **code** = a check fails if the
code stops doing it; **CI** = a gating job proves it on each release; **reading** = read from the
code on 2026-10-23, no test yet; **intent** = a promise about what we will do; **re-check** = true
today and expected to change, with what changes it. When a row stops being true, the sentence
changes before the code ships.

| # | Sentence (short) | Where | Proof | Kind |
|---|---|---|---|---|
| 1 | The website sets no cookies, uses no analytics, loads nothing from another server | PRIVACY 2; footer | `tools/site/check.mjs` "no page asks another server for anything"; `website/assets/door.js` has no cookie, storage or request code | code |
| 2 | GitHub hosts the site and keeps its own records of addresses | PRIVACY 2; footer | GitHub Pages docs (visitor addresses are logged for security) | external |
| 3 | AurOS Setup sends nothing about you or your computer to us | PRIVACY 1, 3; site "Your privacy" | `src/aurbridge/plat_win.c` WinHTTP is used only to fetch the image pieces from the baked-in release address (`phases.c` `out->base`); no other network call exists in `src/aurbridge` | reading |
| 4 | Setup downloads the image from GitHub Releases and checks it | TERMS 4; PRIVACY 3 | `aurbridge-sim getimage` in `build.yml` ("the pieces, put back together by the installer's own code"); every piece hash-checked | CI |
| 5 | Setup looks at the computer and sends none of it anywhere | PRIVACY 3 | as row 3 | reading |
| 6 | Setup writes notes on the computer to finish or undo the install | PRIVACY 3 | the install journal (`docs/AURBRIDGE.md`) | reading |
| 7 | The Wi-Fi password is copied on the same computer and never sent (versions that offer it) | PRIVACY 3; TERMS 4 | `tools/wifitest.sh`; the feature is behind `AUROS_FASTPATH` (off in the default build) | code (when on) |
| 8 | Windows' drive lock is paused while the install restarts the computer, comes back by itself, is never removed | TERMS 4 | `tools/fasttest.sh` bitlocker cases; behind `AUROS_FASTPATH` | code (when on) |
| 9 | AurOS sends no usage numbers, crash reports or program list | PRIVACY 4 | Firefox `DisableTelemetry` in `rootfs/etc/firefox/policies/policies.json`; `popularity-contest` excluded (`profiles/desktop.profile` `packages_exclude`); Ubuntu Pro units masked and its apt hook emptied (`tools/updatepolicytest.sh canonical`, run in `tests.yml`) | code |
| 10 | The host table: Ubuntu archive, Mozilla archive, Firefox services, uBlock lists, Safe Browsing, Flathub, GitHub for updates, time servers | PRIVACY 4 | `build/forge` (sources.list, Mozilla pin), `rootfs/etc/firefox/policies`, `rootfs/etc/apt/sources.list.d` via `build/aptchannel`, `rootfs/etc/chrony/{chrony.conf,sources.d}` | reading; **re-check** when the egress test (COMPLIANCE PRI-2, WP-38) exists |
| 11 | Wishes is off in this version and sends nothing | TERMS 5; PRIVACY 4 | `tools/wishesoff.sh` (`wishes="off"` in the profile, decision D14) | code |
| 12 | We keep emails and reports at most 12 months, then delete them | PRIVACY 5 | the owner's mailbox purge (COMPLIANCE checklist 15) | intent; **re-check** when the mailbox exists |
| 13 | Before a version goes on the website, the whole install runs on a virtual Windows 11 computer; power cuts and put-back are tested on pretend disks | DISCLAIMER; site "AurOS is new" | `release.yml` `promote` needs `e2e` (checked by `check.mjs` claim `release:tested`); `tools/powercuttest.sh`, `putbacktest.sh` | CI |
| 14 | AurOS has not yet been tried on a real, physical computer | DISCLAIMER; site "AurOS is new" | `docs/results/hardware/CAMPAIGN.md` (no row is done) | **re-check**: delete the sentence when the campaign has results, and say what ran |
| 15 | Windows and your files are kept; you can go back | site; Ready page; TERMS 4 | the engine's design and tests (`tools/fasttest.sh`, `installtest.sh`, `putbacktest.sh`); the sentence promises intent, not a guarantee, and sits beside the risk sentence | intent + CI |
| 16 | AurOS gets security fixes from Ubuntu, Mozilla and our update channel while online | TERMS 8 | `tools/updatetest.sh`, `updateboottest.sh` (gating in `tests.yml`), `updatepolicytest.sh` | CI |
| 17 | We will try to give 90 days' notice before stopping | TERMS 8 | -- | intent |
| 18 | The Terms' "free": AurOS itself is free | TERMS 1 | decision 12 (`docs/PLAN-END-TO-END.md`); no price exists anywhere | intent; **re-check** the day a price exists |
| 19 | Nobody from AurOS will ever phone you or ask to connect to your computer | TERMS 9; DISCLAIMER | `docs/SUPPORT.md`; `check.mjs` (no phone number or tel: link on any page) | code (no number) + intent |
| 20 | The source: a `sources-<tag>` release beside every release, kept three years | LICENSES; SOURCE-OFFER | `sources.yml` (make, verify, weekly re-check); `build/sources verify`; `tools/legal/sourcestest.py` | CI |
| 21 | The licenses listed on LICENSES.md | LICENSES | the programs' own copyright files (`tools/legal/gather-copyrights.sh`); read, not checked line by line | reading |
| 22 | The static parts can be relinked | LICENSES; RELINK.md | the two `gcc` lines of `build/staging` | reading |
| 23 | Built from Ubuntu 24.04 LTS software, not endorsed by Canonical | footer; TERMS 10 | `profiles/desktop.profile` (`base_suite="noble"`) | code |
| 24 | AurOS is made for people in the United States for now, in English | TERMS 1; PRIVACY 7 | owner's wave decision (COMPLIANCE checklist 16, recommended); the edge Worker's country check is not built | intent; **re-check** |
| 25 | The Ready page sentence "Windows and your files are kept" and the risk sentence | installer | `tools/legal/check.py` bans the old absolutes in `wizard.c`; B's `--navtest` string check (handed over in `docs/handoff/streams/LEGAL.md`) | pending B |
