# AurOS compliance review

Date: 2026-10-23. Repository state reviewed: origin/main at 69ca6b1 (plus the live GitHub release `v0.1.0-618726b`, the Pages site and the deployed Wishes Worker).

**This document was prepared by AI agents without a lawyer (owner decision D15). It is not legal advice.** It is a working risk register. Where a rule is cited, the citation points to the source the agents read; several checks were made from memory because the web-search budget ran out mid-review, and those points are marked "unverified". The owner can have any single item checked by a lawyer later; nothing here needs one to start.

What was reviewed: the whole repository, the website, the release and Actions setup, the deployed Cloudflare Worker, and the owner's own plan documents, through eight lenses (open-source licences, trademarks, privacy, liability and terms, security law, AI, web and access, business). The review produced 112 candidate findings. Each was challenged twice by independent skeptic passes. 58 findings were kept (both skeptics agreed there was a real issue, usually at a lower severity than first claimed), and 54 were refuted (kept in the appendix, so nobody re-opens them by accident).

How to read severity (the final severity after both challenges, not the first claim):

- **before-anyone-uses**: do before a single person outside the owner and testers gets a copy.
- **before-public-launch**: do before the public link (wave 1, about 25 downloads a day) is switched on.
- **before-charging-money**: do before any price, paid help, paid Wishes quota or donation tier exists.
- **later**: hygiene or a gate on a feature that is not shipped (most are gates on turning Wishes on, `wishes="on"`, decision D14).

Finding ids: LIC licences, TM trademarks, PRI privacy, LIA liability, SL security law, AI, WEB web and access, BIZ business. "Split verdict" means one skeptic refuted the finding as framed and the other kept a reduced form.

Counts of kept findings by final severity: before-anyone-uses 1, before-public-launch 22, before-charging-money 10, later 25.

---

## 0. Status (kept current by stream LEGAL; commits are on `main`)

Last updated 2026-10-23. "Done" means the agent work is in and checked; "owner" and "other stream"
say who holds the rest. The hand-offs with the exact strings are in `docs/handoff/streams/LEGAL.md`.

| Item | Status | Where / commit |
|---|---|---|
| LIC-1 source and offer | **Done in the repo; the one published release is being remade.** Tool, workflow, tests (`build/sources`, `sources.yml`, `tools/legal/sourcestest.py` and its 11 mutants, `publish.py` + `publishtest.py` with 5 mutants: a published release is replaced only by a complete, digest-checked set); the sources workflow sweeps every three hours, adds the pointer line and `NOTICES.txt` to each AurOS release, so no release stays without sources. `sources-v0.1.0-618726b` was damaged on 23 October by cancelling a run mid-replacement (a part was gone) and is remade by `[sources v0.1.0-618726b 39905858387 again]`: `LEGAL.md` says what it ended as. Open: stream E wires `sources.yml` into `release.yml` and `promote` (the sweep covers until then); the owner's mailbox replaces `CONTACT_EMAIL` | 4225a73, publish.py commit |
| LIC-2 licenses page and footers | **Done** (v3 after two skeptic rounds, `REVIEWS.md`): `website/licenses.html`, footers on all five pages, link under both Get buttons, `check.py` fails without them; in-OS help page. Open: installer Licenses button (B), Settings > About link (C) | b1766ec, 1fc9cac |
| LIC-3 installer notices | **Done in the repo**: `docs/legal/notices/NOTICES.txt`, `docs/RELINK.md`. NOTICES now carries every full licence text its copyright files point at (`notices.py` fails on one it lacks); the texts say the notices are attached to each release, not that the installer has them. Open: B embeds resources 200 and 202; A2's `STAGING-CONTENTS.txt` and E's `host-sources.txt` (the sources job adds the publishing machine's glibc until then, and says so) | b1766ec |
| LIC-4 TERMS vs GPL | **Done** (rewritten: licenses win, firmware, no use restriction) | 5aec982 |
| LIC-5 / TM-2 Canonical | Mark hygiene **done** (`check.py`, `marks` job); letter drafted (`CANONICAL-LETTER.md`, owner optional) | b1766ec, 68273c8 |
| LIC-6 / TM-5 Firefox | **Upgraded by the skeptic review to before-public-launch (recommended):** Mozilla's page names a force-installed extension and changed defaults as needing prior written permission; `MOZILLA.md` has the options (ask Mozilla, or change the configuration); forge tarball refusal is stream C's | 68273c8, this round |
| LIC-7 firmware | Sentence done; the rootfs license-file check is open (stream C/E) | 5aec982 |
| LIC-8 / LIC-9 notices | **Done**: `THIRD-PARTY-NOTICES`, `LICENSE` in the image, NOTICE files, Unicode header | b1766ec, b37b655 |
| LIC-10 codecs | Sentence done; `MONETISATION-GATE.md`; the `media_codecs` switch and codec record are open (stream C) | 5aec982, 68273c8 |
| LIC-11 / BIZ-3 / BIZ-4 | `CONTRIBUTING.md`, `PROVENANCE.md`, `templates/IP-ASSIGNMENT.md` done | 68273c8 |
| TM-1 | Update origins fixed and proven by S5; `check.py image` reports the lsb-release leftover until stream C diverts it | S5 956af4a; b1766ec |
| TM-3 | **Done** (footer paragraph with the Apple clause on every page) | b1766ec |
| TM-4 | **Done: a real problem found** (`NAME-SEARCH.md`): UserTesting rebranded as Auros on 29 Sep 2026 and filed AUROS in class 42 in four countries; Emergent Systems holds US reg. 4512239 AUROS in class 9. Owner decision needed before the public link | ddfce87 |
| PRI-1 | **Done** (site and footer sentences, code-signing page) | b1766ec |
| PRI-2 | Privacy page rewritten to what is built; the egress test (WP-38) and the host lint are open | 5aec982, b1766ec |
| PRI-3, 4, 5, 6, 9 | Later (Wishes gate): `WISHES-PRIVACY-DRAFT.md` keeps the old text | 5aec982 |
| PRI-7 | Retention sentence **done**; `docs/ops/SUPPORT-OPS.md` written | 5aec982, c4db4df |
| PRI-8, LIA-8 | Wording **done** ("AurOS itself is free"); `SALES-TERMS-OUTLINE.md`, `PAID-TERMS.md` | 5aec982, c4db4df |
| LIA-1 Terms, Disclaimer, assent | Pages **done** (v3: what Setup does is stated in full, in order; accepting by using AurOS; helper clause), lawyer gate replaced (`README.md`); the Ready-page agreement line is open (B); `check.py --launch` now gates `pages.yml` `deploy` and a [latest] `site` job | b1766ec |
| LIA-2 risk sentence | Site and printed guide **done**; Ready page open (B) | b1766ec |
| LIA-3 territory | Terms sentence **done**; the edge Worker's country check is open (OPS) | 5aec982 |
| LIA-4 EU PLD | `docs/EU.md`; later | c4db4df |
| LIA-5 data-loss playbook | **Done** (`docs/ops/INCIDENTS.md` playbook 5, step 6) | 0cc51ed |
| LIA-6 / LIA-7 / BIZ-1,2,5,6 | Owner: `ENTITY-CHECKLIST.md`, `INSURANCE-QUESTIONS.md` | 68273c8 |
| SL-1, SL-2 | `docs/security/CRA.md` | c4db4df |
| SL-3 | `EXPORT.md` | 68273c8 |
| AI-1 .. AI-8 | Later (Wishes gate); the Worker kill switch is stream CL's; `ANTHROPIC-CHECKLIST.md` | c4db4df |
| WEB-1 | **Done** (pages, footer links, check; the D15 gate in `README.md`) | b1766ec |
| WEB-2, WEB-4 | `docs/ACCESSIBILITY-CONFORMANCE.md` stub; axe-core and forced-colors are stream D's | c4db4df |
| WEB-3 | **Done** (wording); the checkout move waits for a price | 5aec982 |
| BIZ-7 | The by-channel money line waits for a price (`MONETISATION-GATE.md`) | 68273c8 |

## 1. Summary

### 1.1 What must be true before anyone uses AurOS

1. **Source and written offer exist for what was shipped.** A `sources-<tag>` release holds the complete Ubuntu source packages (kernel, glibc, systemd, GRUB, shim, ntfs-3g, e2fsprogs and the rest), plus a pointer to Firefox's source. The release notes, `/usr/share/doc/auros/SOURCE-OFFER` and the website carry the written offer, with a working contact address. A public prerelease (`v0.1.0-618726b`, 21 assets, about 1.5 GB) already carries the image and installer with none of this (LIC-1). Until it is fixed, do not link or announce that prerelease. Do not delete it (CI depends on its URLs).
2. **A contact address exists** that someone reads (a free address or a mailbox on the owned domain). The GPL offer, SECURITY.md and the policies all need it (LIC-1, PRI-7, WEB-1).
3. **Confirm on the built image that Ubuntu security updates really install.** `os-release` is rewritten to `NAME="AurOS"`, and the update config uses `${distro_id}:${distro_codename}`. If those variables resolve to `AurOS:Nocturne`, security and updates match nothing and are silently never installed. This has not been run on the built image (TM-1). It is a one-hour check; if it fails it is a launch-stopping security defect.
4. **The deployed Wishes Worker accepts no traffic.** `DAILY_BUDGET_USD = "0"` (the working kill switch; `BANNED=*` does not work, the plan text is wrong) until the Wishes gates pass (AI-1). No shipped client can reach it, but anyone reading the public repo can.

Nothing else in the 58 findings needs to precede a family or tester install on known, backed-up machines, who also have `docs/usability/CONSENT.md`.

### 1.2 What must be true before public launch (the public link, wave 1)

Source and licences:
- CI-enforced `build/sources`, a re-verification job, the licences page, a Licences link beside both download buttons, a Licences button and licence texts inside the installer, footers corrected on all five footer pages (LIC-1, LIC-2, LIC-3).
- One-paragraph Canonical hygiene: no Ubuntu marks in the image or site, "built from Ubuntu 24.04 LTS packages" plus the non-affiliation line (LIC-5, TM-1, TM-2, TM-3).

Terms, honesty and risk disclosure:
- Terms and Disclaimer pages published, linked from every footer. Installer Ready page carries a plain, normal-contrast acceptance line directly above the Install button, and a one-sentence data-loss and backup warning. The absolute claims "untouched", "kept just as they were", "all your files stay" are softened. The lawyer-approval gate in `docs/legal/README.md` and the plan is replaced by the D15 agent-review gate (LIA-1, LIA-2, WEB-1, WEB-3).
- "Nothing about you is collected" reworded; PRIVACY.md section 1 and the light/dark sentence fixed; the support-mail retention sentence written; PRIVACY.md updated before the first of the outcome ping, the report button, the download-cap Worker or the notice feed ships (PRI-1, PRI-2, PRI-7).
- A data-loss playbook addendum in `docs/ops/INCIDENTS.md` playbook 5 (LIA-5).

Owner and structure:
- A free AurOS name knockout search is run and recorded (TM-4).
- The domain is bought and `AUROS_DOMAIN` set, so installed PCs never carry a `github.com`/`github.io` address (BIZ-5).
- A single-member LLC is formed (home state), with EIN, separate bank account, IP assignment, and a GitHub organisation with a second owner (LIA-6, BIZ-1, BIZ-2, BIZ-5).
- Tech E&O and cyber quotes obtained and the question list sent; binding by wave 2 (LIA-7, BIZ-6).
- The Wishes Worker is built to the kill-switch standard before Wishes is ever enabled (AI-1).

### 1.3 What must be true before charging money (including donations, paid help, paid Wishes quota, any pre-installed PC or image sale)

- The LLC, a business bank account and insurance are in force; payment account is in the LLC's name; no personal PayPal or Venmo; no donate or sponsor link before then (PRI-8, LIA-7, BIZ-9).
- The "nothing to pay, now or later", "no subscription" and "will never ask for money" wording is already replaced (public-launch item) with "AurOS itself is free"; the paid offers have their own sales terms with price shown before any charge, refund window, receipts, no auto-renewal in the first version (LIA-8, PRI-8, BIZ-7).
- A merchant of record is chosen and has written confirmation of which products it covers. Paddle-type providers reject standalone human help, so human help may need a direct processor with the owner handling tax (BIZ-8, BIZ-9).
- The site leaves GitHub Pages before the first price appears (WEB-3).
- EU and similar duties are decided: paid Wishes and paid help are sold only in the US, UK, Canada and Australia, or the CRA, Product Liability Directive and accessibility track is started first (SL-1, SL-2, BIZ-10, LIA-3, AI-8).
- A decision on whether paid remote human help is worth the scam-confusion cost, since "we never ask to connect to your computer" is the main anti-scam message for this audience (BIZ-7).

### 1.4 What must be true before Wishes (`wishes="on"`) reaches anyone beyond the owner

Not a v0.1 gate (D14, WP-89). In order: the Worker kill switch and country allow-list (AI-1, AI-6); the "this is an AI" line on the consent screen and panels (AI-2); the final consent text with controller, contact, 30 days, 2 years, 7 years for safety scores, health and emergency caution (AI-3); PRIVACY.md section 4a and section 7 corrected (AI-4, PRI-3); the health-data handling and DPIA (PRI-5, AI-7); the "harmless" wording removed (AI-5); the UK representative decision (PRI-6); the Anthropic workspace limit and kept DPA copy (owner checklist).

---

## 2. Findings by lens

Each entry lists: rule with citation, trigger (what in AurOS as built), consequence (as corrected by the skeptics), agent remedy, owner remedy with cost, severity, and a one-line skeptic note. "Agent remedy" text is exact where the review gave exact wording; corrections from the skeptic passes are folded in.

### 2.1 Open-source licences

#### LIC-1. No source code and no written offer for any GPL/LGPL/MPL program; one prerelease is already downloadable
Severity: **before-anyone-uses** (minimum: offer, contact, sources for the shipped tag). The CI automation is before-public-launch.

- **Rule.** GPLv2 s3 (source alongside the binary, or a written offer valid 3 years; network access to source must be "from the same place") and s4 (automatic termination), https://www.gnu.org/licenses/old-licenses/gpl-2.0.html. GPLv3 s6(a)-(d) and s8 (30-day cure after first notice), https://www.gnu.org/licenses/gpl-3.0.html. LGPL-2.1 s6. MPL-2.0 s3.2 (tell recipients how to get the source of Firefox). Linux Kernel Enforcement Statement (30-day cure for signers), https://docs.kernel.org/process/kernel-enforcement-statement.html. SFC v. Vizio lets recipients sue as third-party beneficiaries (sent to trial Feb 2026; no verdict found).
- **Trigger.** The repo is public. `gh release view v0.1.0-618726b` shows 21 assets (AurOS-Setup.exe, pieces.txt, SHA256SUMS, `auros-desktop.img.gz.000...`) and no source asset or offer in the notes. `.github/workflows/release.yml` uploads only the installer, pieces and sums. `build/forge` debootstraps Ubuntu noble (about a thousand packages including the kernel, glibc, systemd, bash, GRUB, ntfs-3g, mpv), writes `sources.list` with `deb` lines only, and wipes `/var/lib/apt/lists`. `build/sources` (WP-06) is unbuilt; `docs/ATLAS.md` item 11 says "no GPL source offer".
- **Consequence.** The duty attaches to the first copy handed to anyone outside the project. The download counts so far (7 to 10 per asset) match CI runs, and the Pages site returns 404, so no real person has been sent there yet; real exposure is dormant, not zero. Realistic path: an emailed notice (30-day cure under GPLv3 s8 and the kernel statement), or a DMCA notice to GitHub (about one business day to modify before the repo is disabled). Pointing at archive.ubuntu.com is not a cure for GPLv2-only programs (the kernel), because the "same place" wording is not met and superseded packages leave the pool.
- **Agent remedy.**
  1. Add `build/sources`, run in the same CI job right after the image build: `chroot $RFS dpkg-query -W -f='${source:Package} ${source:Version}\n' | sort -u`; temporary `deb-src` list (noble, noble-updates, noble-security, same components as `profiles/desktop.profile`); `apt-get update`; `apt-get source --download-only --only-source PKG=VER` for each; verify every file named in each `.dsc` exists and matches its sha256; fail the job on any miss. Include linux, linux-signed, linux-meta, shim, shim-signed, grub2, grub2-signed, linux-firmware and every library the staging initramfs copies (LIC-3). A cached rootfs (`.forge-bootstrapped`) can hold superseded versions: fall back to the Launchpad `+sourcefiles` URL or re-resolve. Exclude Firefox from the apt step (packages.mozilla.org has no `deb-src`).
  2. Firefox: record the installed version and which route ran (mozilla-apt, mozillateam PPA or tarball). Put `https://archive.mozilla.org/pub/firefox/releases/<ver>/source/` in the offer and `SOURCES.txt` (a pointer is enough for an unchanged binary under MPL FAQ Q7; mirroring the tarball is optional insurance).
  3. Pack into tar parts of 1.9 GiB or less (GitHub's per-asset limit is 2 GiB; one report says an exact 2 GiB file failed) and publish as a separate release `sources-<tag>` with `SOURCES.txt` (package, version, sha256, licence from `/usr/share/doc/*/copyright`) and `SHA256SUMS`, plus `git archive` of the exact AurOS commit. `profiles.yml` prunes only `profiles-*`; WP-04b's prune job must skip `sources-*`.
  4. Add a CI job that downloads the published sources release and re-verifies every hash.
  5. Put the offer text below into the `release.yml` `notes.md` template, into `/usr/share/doc/auros/SOURCE-OFFER` in the image, and on the website. **Offer text (exact):** "AurOS contains programs under the GNU General Public Licence, the GNU Lesser General Public Licence, the Mozilla Public Licence and other licences. The complete source code for the programs in this release is published at https://github.com/aarohkandy/v0.0.1-auros/releases/tag/sources-<TAG> . It stays available for at least three years after we last offer this release for download, and for as long as we support it. This offer is open to anyone who has a copy of AurOS. If you cannot use that page, write to <CONTACT> and we will send the source on a USB stick or disc for the cost of the medium and postage. AurOS's own code is MIT and is at the same address, tag <TAG>."
  6. Existing prerelease `v0.1.0-618726b`: do not delete or draft it (`e2e.yml` hard-codes its installer URL, `aptrepo.yml` line 241 downloads its pieces, `build.yml` line 295 checks it). Edit its notes to add the offer, and attach a sources release built from the same commit's Actions run (use that run's apt logs; some versions may be superseded). Do not link it from anywhere until done.
  7. Release promotion fails unless a verified `sources-<tag>` release exists for the tag (blocks the first non-prerelease `[latest]` and the Pages deploy).
- **Owner remedy.** Create a contact mailbox (a free address, or a mailbox on the owned domain, about $12 a year). Write in `docs/OWNER-SETUP.md` that `sources-*` releases are never deleted. Optionally upload a second copy of each sources release to the Internet Archive (free). Cost $0 to about $12 a year.
- **Skeptics.** Both stand; one rated it before-public-launch because the downloads were CI only and no page sends anyone there. Kept at before-anyone-uses because the fix is cheap and the duty starts at the first outside copy.

#### LIC-2. Offer and licence information are not next to the download, and the footer misdescribes the licence
Severity: **before-public-launch**.

- **Rule.** GPLv3 s6(d) (clear directions next to the object code where the source is), GPLv2 s1/s3, MPL-2.0 s3.2 and s3.4. GPLv2 s3 is stricter: source must be offered from the same place, so link to a place AurOS hosts, not only archive.ubuntu.com.
- **Trigger.** Download buttons link straight to `releases/latest/download/AurOS-Setup.exe` (`website/index.html` lines about 24, 41, 73, 359). The MIT footer ("AurOS is free software (MIT license)") is on five pages: index, guide, advanced, code-signing and status. Release notes carry nothing on licences. No licences help page exists. The exe itself is not only MIT: it embeds the Ubuntu kernel, initramfs (ntfs-3g, glibc), Canonical's signed GRUB and shim as resources (LIC-3).
- **Consequence.** A polite compliance request is the realistic outcome (30-day cure). The footer is imprecise rather than false.
- **Agent remedy.**
  1. Create `website/licences.html`, with a copy at `rootfs/usr/share/auros/help/licences.html` linked from help index and aurshell Settings > About. Contents: "AurOS's own programs are MIT. AurOS also includes programs made by others, including the Linux kernel, Firefox and many from Ubuntu, each under its own licence. You are free to use and share them; most can also be changed." Then the offer text (LIC-1), the licence list generated from `/usr/share/doc/*/copyright` in the built image (kernel GPL-2.0, glibc LGPL-2.1+, GRUB GPL-3.0+, ntfs-3g GPL-2.0+, shim BSD-2-Clause, Firefox MPL-2.0, fonts OFL), a pointer to `/usr/share/doc/<package>/copyright`, "Programs from Get more programs come from Flathub and carry their own licences; each shows its licence on its page", and "Hardware firmware is under vendor licences in `/usr/share/doc/linux-firmware-*/copyright` (and `/usr/lib/firmware` if the licence files sit there)".
  2. Replace the footer on all five pages with: "No cookies, no tracking, no ads. AurOS's own code is MIT; it includes programs by others under their own licences (Licences and source)." with "Licences and source" linking to `licences.html`.
  3. Put a one-line "Licences and source code" link directly under both Get AurOS buttons, and in `release.yml` notes: "Licences and the complete source code for this release: <sources release URL>."
  4. Add a Licences button to the wizard footer showing an embedded NOTICES text resource (LIC-3, LIC-8). Include the full GPL-2.0, GPL-3.0, LGPL-2.1 and MPL-2.0 texts, not only a list.
  5. Extend `tools/site/check.mjs` to fail if any page with a download link lacks the `licences.html` link (skip redirect stubs via `isStub()`), and `tools/helptest.py` to require the help page. Point `docs/legal/TERMS.md` s3 at this page.
- **Owner remedy.** Approve the footer wording. $0.
- **Skeptics.** Both stand. One rated before-anyone-uses for the licence texts in the exe; the licence texts ride in LIC-3.

#### LIC-3. AurOS-Setup.exe conveys the GPL kernel, GRUB and the staging initramfs (with statically linked glibc), with no source offer or relink information
Severity: **before-public-launch** (the sources for it travel with LIC-1).

- **Rule.** GPLv2 s3, GPLv3 s6, LGPL-2.1 s6 (a static link needs library source and relinking means). Mere aggregation (GPLv2 s2) keeps the MIT wizard separate, not the embedded files.
- **Trigger.** `build/aurbridge` embeds the Ubuntu kernel, staging initramfs, signed GRUB, shim and MokManager as RCDATA 1-5. `build/staging` copies kmod, ntfsresize, ntfsinfo, e2fsck, resize2fs, glibc shared libraries and DejaVu fonts into the initramfs, and builds `init` and `aurscreen` with `gcc $HARDEN_STATIC` (`-static-pie`), linking the runner's libc6-dev. Nothing records those versions; the runner's libc6-dev is not in the image, so an image-based package list misses it. Pre-release `v0.1.0-618726b` carries this exe today.
- **Consequence.** Same exposure as LIC-1 on the file users download first.
- **Agent remedy.**
  1. In `build/staging`, write `STAGING-CONTENTS.txt` into the initramfs and into `out/`: for every copied program and library, `dpkg -S` and `dpkg-query -W -f='${source:Package} ${source:Version}'`, run against the rootfs database (`--root`/`--admindir`, not the runner's), plus the host libc6-dev and gcc versions (also in `dist/installer/build-info.txt`). Have `build/sources` fetch those source packages (glibc, kmod, ntfs-3g, e2fsprogs, util-linux libs, fonts-dejavu, linux, linux-signed, grub2, grub2-signed, shim, shim-signed) even if absent from the image's dpkg list.
  2. Add `docs/RELINK.md` (how to rebuild `init`/`aurscreen` from `src/aurstage` and `src/aurscreen` with a modified glibc on Ubuntu 24.04, exact commands), linked from the licences page.
  3. Embed the NOTICES text and the offer as RCDATA 200 in the exe, plus the OFL full text as RCDATA 201, shown by the wizard Licences button. NOTICES also needs the GPL-2.0, GPL-3.0, LGPL-2.1 texts, glibc's LICENSES BSD notices, Bitstream Vera/DejaVu, and SIL OFL for Atkinson Hyperlegible Next. Shim and MokManager are BSD-style: attribution only, no source.
  4. `tools/manifesttest.sh` fails if the exe lacks the NOTICES resource, if a program copied into the initramfs has no `STAGING-CONTENTS.txt` entry, or if the release workflow does not upload the source assets.
  5. Keep the rule that no firmware or proprietary file is ever added to the exe or staging initramfs (this also keeps the SignPath application clean).
- **Owner remedy.** None. $0.
- **Skeptics.** Both stand. One rated it before-anyone-uses (already overdue since the exe is public); handled as part of the LIC-1 retrofit.

#### LIC-4. Draft TERMS add use restrictions and termination to GPL software
Severity: **later** (split verdict; a five-line edit in the same pass that removes the DRAFT banners).

- **Rule.** GPLv2 s6 and GPLv3 s10 (no further restrictions on recipients' rights), GPLv3 s2, GPLv2 s4/GPLv3 s8.
- **Trigger.** `docs/legal/TERMS.md` s2 (who may use AurOS), s5 ("Don't use AurOS to break the law ... or to harm others"), s8/s11 (stop offering), s12. They are unpublished drafts. TERMS s3 already says "Nothing in these terms limits the rights those licences give you", the standard trump clause that SFC and the FSF accepted for Canonical's Ubuntu policy in 2015. Most of the cited clauses are not restrictions on recipients.
- **Consequence.** Low. GPLv2 s0 says running a program is outside its scope; only the GPLv3 programs (bash, GRUB) are really affected. 30-day cure applies.
- **Agent remedy (docs/legal/TERMS.md only).**
  (a) In the preamble, drop the third covered bullet or append "The programs inside AurOS are not covered by these terms. Each is under its own licence (section 3), and those licences always prevail over these terms."
  (b) s5: delete "or to harm others"; keep "don't break the law" as advice or limit it to the website and installer ("Please use AurOS lawfully").
  (c) s2: make the authority and age wording apply to "running the installer".
  (d) s8 and s11: add "Changing or stopping AurOS does not end the offer of source code or anyone's rights under the licences."
  (e) s3: add "These terms never override those licences. If anything in these terms conflicts with the licence of a program, that licence wins."
  (f) Firmware: "Some hardware firmware is under its maker's own licence, a copy of which is in `/usr/share/doc/<package>/copyright`; those licences are between you and the maker." Do not say "for any purpose" and do not add "by using that hardware you agree" (browsewrap assent is itself a condition).
  Do not build the "two-part TERMS" restructure, the acceptance screen for firmware, or the "you may not near AurOS" lint.
- **Owner remedy.** Approve the edit. $0.
- **Skeptics.** One refuted as framed (the draft already has the trump clause); one kept a reduced form. Both put it at later.

#### LIC-5. Modified Ubuntu image: Canonical's IP policy and the "recompile" clause
Severity: **before-public-launch (low)**, for the cheap mark hygiene only. The Canonical letter is optional and must not block launch. The commercial question is before-charging-money.

- **Rule.** Canonical IP Rights Policy (dated 15 July 2015, still current), https://canonical.com/legal/intellectual-property-policy: s3 unmodified Ubuntu may be redistributed; modified versions need approval to carry the marks, otherwise marks removed and (read strictly) binaries recompiled; a licence "for which you may be required to pay" is possible; s4 no marks in software titles, domains or URLs; s2 the policy does not reduce open-source licence rights (SFC accepted this, https://sfconservancy.org/news/2015/jul/15/ubuntu-ip-policy/). Precedent: Canonical asked Linux Mint in 2013-14 to licence reused binaries.
- **Trigger.** AurOS redistributes a modified Ubuntu image from GitHub Releases: `build/forge` rewrites `/usr/lib/os-release`, issue and MOTD and dpkg-diverts the grub tools. Unavoidable Ubuntu strings remain (kernel version string, the `\EFI\ubuntu` path of Canonical's signed GRUB, packages' copyright files). The shim is Microsoft-signed; GRUB and the kernel are Canonical-signed (the finding said "Canonical-signed shim", which is wrong).
- **Consequence.** Realistically an email asking for wording changes. There is no contract with Canonical (a trademark permission offer), a copyright route is blocked by the GPL "no further restrictions" rule, and GitHub's trademark process targets confusing brands. No enforcement against a derivative that stripped the marks was found since 2015.
- **Agent remedy.**
  1. Image and site mark audit in CI (WP-37): scan the built rootfs and the website for Ubuntu logos, wallpapers, plymouth/grub themes and "Ubuntu" in user-visible files (exclude package metadata, copyright files, the EFI path); fail on `ubuntu-wallpapers`, `ubuntu-artwork`, `yaru-theme-*`, `plymouth-theme-ubuntu-*`, `ubuntu-desktop` metas, `ubuntu-*logo*`.
  2. Allowed wording: "AurOS is built from Ubuntu 24.04 LTS packages" in body text only, never in titles, URLs, logos or buttons, plus the non-affiliation line (TM-3). Use "Ubuntu is a registered trademark of Canonical Ltd".
  3. `tools/repo.sh` refuses a registered domain label containing ubuntu, canonical, kubuntu, windows, microsoft or linux (host labels only; exempt `archive.ubuntu.com` and `ID_LIKE=ubuntu`).
  4. Keep othersys downloading the official ISO unmodified from `releases.ubuntu.com` and never mirroring it; a CI guard fails if any workflow uploads an ISO to Releases.
  5. Optional informational letter in `docs/legal/CANONICAL-LETTER.md`: framed as a notice, not a request. Do not ask "do you consider AurOS a modified Ubuntu requiring a licence" (that invites the Mint answer). Say: packages are Canonical's binaries installed unchanged; changes are configuration; marks removed from AurOS's own screens, installer and website (not "every user-visible surface", which `uname -v` and `lsb_release` disprove); free of charge; not endorsed; "tell us if anything here is a problem".
- **Owner remedy.** Optional: send the letter through https://canonical.com/legal/terms-and-policies/contact-us (about a week to reply); file the reply in `docs/legal/`. $0 unless Canonical asks for a fee (unknown). Do not charge money for anything shipping this image until Canonical answers in writing, or accept the risk consciously.
- **Skeptics.** Both stand but at low severity (one: later; commercial part before-charging-money).

#### LIC-6. Firefox is redistributed with locked policies, a force-installed extension and removed Mozilla messages
Severity: **later**. The paid-bundle part is before-charging-money.

- **Rule.** Mozilla Unaltered Software Distribution Policy, https://www.mozilla.org/en-US/foundation/trademarks/distribution-policy/ (no added extensions or changed defaults, free, latest version, not tied to a purchase; modified builds need written consent to use Mozilla marks; request address trademark-permissions@mozilla.com). MPL-2.0 s3.2. `SkipTermsOfUse` must not be set (it is not).
- **Trigger.** `rootfs/etc/firefox/policies/policies.json` force-installs uBlock Origin from addons.mozilla.org, sets 12 locked preferences, disables dev tools and about:config, and sets UserMessaging off. The launcher is already renamed "Internet" in the shell (`build/forge` about line 612). Default route is Mozilla's apt repo (latest); the pinned `firefox_version=140.0` tarball is only a last resort.
- **Consequence.** Low: policies.json is Mozilla's documented admin mechanism and no enforcement against small distros was found. At worst Mozilla asks for the name to go. A label change alone does not remove Mozilla marks inside the binary. Moving uBlock Origin off force-install would not bring AurOS inside the "unaltered" harbour and would weaken the scam-safety design.
- **Agent remedy.** `docs/legal/MOZILLA.md` listing every deviation (policies.json keys, the force-installed extension, the route used). Make `build/forge` refuse the tarball route in release builds (already planned; it is also a never-updating-Firefox security problem). Make the tarball block in `forge` use the same label table as the rename. Keep the "Firefox is a trademark of the Mozilla Foundation" notice (TM-3). Never include Firefox in a paid bundle. Keep Epiphany as the documented fallback.
- **Owner remedy.** Optional free email to trademark-permissions@mozilla.com describing the configuration (free, no data collected, official binaries, policies.json only). Required before any paid offer that ships Firefox. $0; replies may take weeks or never come.
- **Skeptics.** Both stand, both say later.

#### LIC-7. Proprietary firmware blobs are redistributed
Severity: **later** (CI hygiene).

- **Rule.** Vendor firmware licences in Ubuntu's linux-firmware copyright files (and upstream `LICENSES/`): unmodified binary redistribution with the licence passed on. Broadcom (copy of the agreement), Qualcomm (include the .txt), MediaTek and others are stricter than Intel's BSD-style text. The Intel "OEM/IHV/ISV be bound" text in the finding comes from a legacy click-through in Ubuntu's source-package file and does not apply to the files AurOS ships.
- **Trigger.** `profiles/desktop.profile` `packages_hardware` lists many `linux-firmware-*` pieces plus `firmware-sof-signed`. `build/staging` ships no firmware (keep it that way).
- **Consequence.** Near zero; every Ubuntu derivative does this. No breach exists today (`build/forge` removes nothing under `/usr/share/doc` or `/usr/lib/firmware/LICEN*`).
- **Agent remedy.** CI check on the built rootfs that every installed package has `/usr/share/doc/<pkg>/copyright` and the firmware licence files are present (check both `/usr/share/doc` and `/usr/lib/firmware`). A forge guard that fails if a slimming step ever removes them. A test that the staging initramfs contains no `lib/firmware`. One firmware sentence on the licences page (LIC-2) and in TERMS (LIC-4 item f). Do not recompress or repack firmware beyond what Ubuntu ships (squashfs of the whole rootfs is fine).
- **Owner remedy.** None. $0.
- **Skeptics.** Both stand; both say later.

#### LIC-8. Attribution notices for MIT, OFL, Unicode and runtime code are not carried inside the binaries
Severity: **later**.

- **Rule.** MIT (copyright and permission notice in copies), SIL OFL 1.1 s2, Unicode License v3 (notice with the data), mingw-w64 runtime licence (explicit binary-redistribution clause). The GCC Runtime Library Exception removes notice duties for libgcc.
- **Trigger.** Wayland protocol code (`src/aurwl/gen/*`, MIT from Hogsberg, Intel, Red Hat, Samsung and others) compiled into aurshell, with no notice in the image (the wayland-protocols copyright files are purged). The exe is linked `-static` with mingw-w64 and carries no notices. `src/ferry/data/windowsZones.tab` is CLDR v45 data with no Unicode licence text. The font claims in the original finding were stale: the exe embeds Atkinson Hyperlegible Next (no Reserved Font Name; OFL files already identical in `src/aurbridge/fonts/` and `website/assets/fonts/`).
- **Consequence.** Technical notice failure only; near-zero enforcement.
- **Agent remedy.** A roughly 40-line `THIRD-PARTY-NOTICES.md` covering the Wayland protocol holders with the MIT text, the exact mingw-w64 runtime file text (COPYING.MinGW-w64-runtime.txt), Unicode V3 for CLDR, OFL for Atkinson. Ship it as `/usr/share/doc/auros/` in the image with the repo LICENSE, as the exe's NOTICES resource (LIC-3), and as a header on `windowsZones.tab`. Optional: `--name-IDs=*` in `tools/wizard/fonts.py` so name IDs 13/14 survive. Skip the font rename, the CI header scanner and per-deb copyright files.
- **Owner remedy.** None. $0.
- **Skeptics.** Both stand but narrow; both say later.

#### LIC-9. Third-party material committed into the public MIT repo without notices
Severity: **later**.

- **Rule.** MPL-2.0 s3.1 (a notice file in the directory is enough, Exhibit A), OFL and Bitstream Vera notices.
- **Trigger.** `tools/scamtest/firefox-159.0-policies-schema.json`, `firefox-esr140.17.1-policies-schema.json`, `firefox-pref-allowlist.txt` (from Firefox source); `tools/fuzz/corpus/font/` (3-7 KB subsets of DejaVu, Liberation, FreeSans, Inter). `tools/fuzz/mkseeds.py` strips name tables. None ships in the image.
- **Consequence.** Cosmetic; a REUSE scanner would flag it.
- **Agent remedy.** A `NOTICE` file in `tools/scamtest` (MPL-2.0, Mozilla Firefox, source URL) and in `tools/fuzz/corpus/font` (copyright lines and licence texts for OFL, Bitstream Vera/DejaVu, FreeFont), and one README line: "Files under `tools/scamtest` and `tools/fuzz/corpus/font` keep their own licences." Optionally drop the Liberation seed. Skip `reuse lint` (it fails on every file without SPDX data). Drop Canonical's SHA256SUMS and the screenshots from this finding.
- **Owner remedy.** None. $0.
- **Skeptics.** Both stand; later.

#### LIC-10. Media codec patents (H.264, HEVC, AAC) in mpv/ffmpeg and Firefox
Severity: **later**.

- **Rule.** Patent licensing is separate from copyright. Via LA AVC terms list the first 100,000 units a year at $0.00 (not outdated; checked on via-la.com), then $0.20. Access Advance HEVC licenses parties that "Sell" products, with royalties on sale, and says its primary focus is branded products sold to end users.
- **Trigger.** mpv and Ubuntu's libavcodec60/x264/x265/libheif plugins are in the image; Firefox on Linux loads the system libavcodec, so AurOS (not Mozilla) is the distributor behind both. HEIC (HEVC) photos are a plan feature, so HEVC decode cannot simply be removed.
- **Consequence.** Low while free and small. Triggers that matter: selling an image or pre-installed PCs, hosted transcoding, more than 100,000 units a year, a licensor letter, or non-US exposure after the US AVC patent expires (about Nov 2027, secondary source).
- **Agent remedy.** A build step recording the codecs the image can decode, using `mpv --vd=help --ad=help` or `dpkg -l 'libavcodec*' 'libx26*' 'libde265*' 'libheif*'` (the ffmpeg CLI is not in the image). A profile switch (for example `media_codecs=on/off`) and a written takedown runbook so the encumbered codecs can be dropped in a day. One plain TERMS sentence: AurOS includes third-party media decoders that may be covered by patents in some countries; the MIT licence grants no patent licence; no warranty. Create `docs/legal/MONETISATION-GATE.md` (does not exist yet) with the re-check line.
- **Owner remedy.** Nothing now. Before selling machines or media or reaching large volume, ask Via LA and Access Advance in writing whether free software distribution needs a licence. Cost unknown.
- **Skeptics.** Both stand; later.

#### LIC-11. MIT grant: no contributor agreement; large AI-generated share of the code
Severity: **later** (split verdict).

- **Rule.** US Copyright Office AI Part 2 report (29 Jan 2025): prompts alone do not make a person the author; human selection, arrangement and modification are protected. Thaler v. Perlmutter cert denied 2 Mar 2026. GitHub ToS D.6 (inbound=outbound).
- **Trigger.** `LICENSE` says "Copyright (c) 2026 AurOS contributors"; no CONTRIBUTING, DCO or SPDX headers; most commits are agent-authored under the owner's identity.
- **Consequence.** No immediate harm. The "outside contributor blocks a paid layer" argument is wrong for MIT (MIT allows sublicensing and selling). Anthropic's Consumer Terms s4 assigns output rights to the user. There are no outside contributors yet.
- **Agent remedy.** Short `CONTRIBUTING.md`: outside code pull requests are not being accepted yet; if that changes, contributions come under MIT and need a `Signed-off-by` line (DCO 1.1). Optional `docs/legal/PROVENANCE.md`: code is written by the owner with AI assistance, third-party components keep their own licences; do not claim a PR review trail (history is direct commits to main). Change the LICENSE line to "Copyright (c) 2026 <owner name> and AurOS contributors" only if the owner chooses; switch to the LLC name only after the signed assignment exists (BIZ-3). No DCO CI gate, no SPDX sweep.
- **Owner remedy.** Decide whether your name appears in LICENSE. Adopt a DCO (not a CLA) before merging the first outside PR. $0.
- **Skeptics.** One refuted as a legal risk; one kept the reduced form. Later.

### 2.2 Trademarks

#### TM-1. The installed system can silently re-label itself "Ubuntu 24.04" (base-files overwrites AurOS's os-release)
Severity: **before-public-launch**. The update-origin sub-item is before-anyone-uses until verified.

- **Rule.** Canonical IP policy (see LIC-5): a modified system that calls itself Ubuntu is the one case the policy plainly forbids.
- **Trigger.** `build/forge` stage_configure (about lines 1372-1396 on main; the finding's numbers were stale) writes `/usr/lib/os-release` (`NAME=AurOS`, `ID_LIKE=ubuntu`) and `/etc/issue` directly with no dpkg-divert. `/usr/lib/os-release` is a plain package file of base-files, so the next base-files update (a point-release bump; the latest, 13ubuntu10.5, was published 2026-09-07, so the next is probably around Feb 2027, not "within weeks") puts back Ubuntu. `/etc/issue` and `.net` are conffiles protected by `--force-confold`. `forge` never touches `/etc/lsb-release`, so `lsb_release -a` says Ubuntu from first boot, though noble's lsb_release reads only os-release.
- **Consequence.** Low legal weight (the string comes from Canonical's own package). No runtime reader of os-release exists in `src/` or `rootfs/`, so the user-facing shell is unaffected. **The sharper risk the skeptics found:** unattended-upgrades computes `${distro_id}` and `${distro_codename}` from `lsb_release`, which on noble reads os-release `NAME` and `VERSION_CODENAME`. With `NAME="AurOS"` and `VERSION_CODENAME=Nocturne`, Ubuntu's `${distro_id}:${distro_codename}-security` and AurOS's own `origin=Ubuntu,archive=${distro_codename}-updates` very likely resolve to `AurOS:Nocturne-security`, matching nothing, so Ubuntu security updates silently never install. `tools/updatepolicytest.sh` only greps the config text, and `tools/updatetest.sh` runs in a stock Ubuntu container.
- **Agent remedy.**
  1. In `build/forge`, after writing the files: `dpkg-divert --local --rename --divert /usr/lib/os-release.ubuntu /usr/lib/os-release`, keep the `/etc/os-release` symlink; same for `/etc/lsb-release` with `DISTRIB_ID=AurOS`, `DISTRIB_RELEASE=$os_version`, `DISTRIB_DESCRIPTION="AurOS $os_version"`. Add `UBUNTU_CODENAME=noble` to os-release (hygiene).
  2. In `rootfs/etc/apt/apt.conf.d/53auros-updates` use literal `origin=Ubuntu,archive=noble-updates` and `origin=Ubuntu,archive=noble-security` (or `codename=`), not `${distro_*}`. Do not rely on `lsb_release` anywhere in update config.
  3. `tools/profiletest.sh` step in the built image: `apt-get install --reinstall -y base-files`, assert `NAME="AurOS"` and `DISTRIB_ID=AurOS`, and run `unattended-upgrade --dry-run --debug` asserting it lists Ubuntu noble-security and noble-updates among allowed origins.
  4. Drop the "no file contains the string Ubuntu" assertion (it cannot pass: ubuntu-keyring, `/etc/dpkg/origins/ubuntu`, apt lists). Keep the package-name checks from LIC-5.
  5. Include this in the first public image (retrofitting installed machines would need an AurOS package postinst).
- **Owner remedy.** None. $0.
- **Skeptics.** Both stand at before-public-launch (one noted the update-origin defect as a separate before-anyone-uses security issue; I could not run the built image, so verify first).

#### TM-2. AurOS is a modified Ubuntu image with no permission from Canonical (the "recompile" clause)
Severity: **before-public-launch (low)** for the cheap repo items; the Canonical clearance itself is before-charging-money. Overlaps LIC-5 and shares its remedy.

- **Rule.** As LIC-5, plus nominative fair use (New Kids on the Block v. News America, 971 F.2d 302) for "built from Ubuntu packages".
- **Trigger.** `profiles/desktop.profile` (noble from archive.ubuntu.com), `build/forge` rebranding, the Canonical-signed boot chain, and `docs/research/other-systems-2026-10.md` section 2.3 which already flags it. The site mentions Ubuntu only twice (advanced.html lines 81 and 202), README lines 33 and 49-53; there is no non-affiliation line published (DISCLAIMER.md has it, unpublished). The letter is D8 (not D10); WP-37 and WP-65 already plan the image-mark audit.
- **Consequence.** An email, not a takedown: no Canonical mark is in AurOS's name, domain or logo, and GitHub's trademark process targets confusing names. Risk grows with money (paid human help, paid Wishes).
- **Agent remedy.** One factual line in the website footer and README: "AurOS is built from Ubuntu 24.04 LTS packages. It is not made, endorsed or supported by Canonical. Ubuntu is a registered trademark of Canonical Ltd." Image/site mark audit (LIC-5 item 1). `tools/repotest.sh` check that AurOS-owned names, domains and titles never contain "ubuntu". Letter text per LIC-5 item 5 (the draft's "removed from every user-visible surface" and "unmodified image" claims must not be repeated). The GPL source offer is LIC-1, not part of this finding.
- **Owner remedy.** Send the informational notice (optional, $0). Do not wait for a reply to launch the free v0.1; do not charge money for the image until Canonical has answered in writing. If Canonical refuses: say nothing about Ubuntu in the UI and keep `ID_LIKE`, not a rebuild.
- **Skeptics.** Both stand; one at before-public-launch low, one at later. Final merges both.

#### TM-3. The website uses many third-party marks and has no non-affiliation or attribution notice
Severity: **before-public-launch (low priority)**.

- **Rule.** Nominative fair use (New Kids; Toyota v. Tabari, 610 F.3d 1171). Microsoft, Mozilla and Apple suggest attribution notices; Apple asks for "has not been authorized, sponsored, or otherwise approved by Apple Inc." on publications. None requires a disclaimer for textual mentions.
- **Trigger.** The footer (`website/index.html` about lines 370-382) says only "No cookies, no tracking, no ads. AurOS is free software (MIT license)". Five pages have footers (index, guide, advanced, status, code-signing); eight are 12-line redirect stubs. The pages name Windows, Microsoft Edge, Defender SmartScreen, Smart App Control, BitLocker, Chrome, Mac, Firefox, Intel and Ubuntu in text; Teams, Zoom and Meet appear only in the on-device help. Check: no logos on the site.
- **Consequence.** No realistic claim; a footer line is cheap evidence against implied endorsement.
- **Agent remedy.** Add to the footer of the five footer pages and to `DISCLAIMER.md` and TERMS s10: "Windows, Microsoft, Edge and BitLocker are trademarks of the Microsoft group of companies. Mac and macOS are trademarks of Apple Inc. Google Chrome is a trademark of Google LLC. Ubuntu is a registered trademark of Canonical Ltd. Firefox is a trademark of the Mozilla Foundation. AurOS is not made, approved or supported by any of them, and has not been authorized, sponsored, or otherwise approved by Apple Inc. The names are used only to say what something resembles or works with." Match the list to the marks the pages actually use (Intel and Linux only if named). `tools/site/check.mjs` fails when a footer page lacks the string "not made, approved or supported by"; skip `isStub()` pages. Do not add Linux unless prose names it. Keep text only, never logos.
- **Owner remedy.** None. $0.
- **Skeptics.** Both stand as a free hygiene item; one at later.

#### TM-4. "AurOS" has not been cleared
Severity: **before-public-launch** (the free knockout search only). Filing is later.

- **Rule.** Lanham Act 32 and 43(a) (15 U.S.C. 1114, 1125(a)), 15 U.S.C. 1052(d). UK, EU, Canada and Australia protect prior use too (passing off, prior-user opposition); "first filer wins" is overstated. 15 U.S.C. 1060(a)(1): an intent-to-use application cannot be assigned before use is proven except to a successor of the business.
- **Trigger.** The name is in about 470 files. `docs/legal/TERMS.md` s10 still has the `[OWNER ... LAWYER: clearance search]` placeholder; no search record exists; WP-65 lists a knockout search. Leads found (none is operating-system software, so none triggers the finding's own rule): AUROS GROUP Reg. 5564397 (building analysis, maintenance may have lapsed), AUROS360 Reg. 5619032 (class 42, unverified), AUROS filings 98564948 and 98564949 (class 36, insurance), Optimum Investment Limited filing 90260333 (household appliances), Auros Global (crypto market maker, common-law), "Aurora OS" (Russian phone OS).
- **Consequence.** Search noise, or a rename after thousands of boot menus carry the name. A cease-and-desist from those owners over a free desktop OS is improbable.
- **Agent remedy.** Create `docs/legal/NAME-CLEARANCE.md` as the dated record: the exact searches (USPTO Trademark Search https://tmsearch.uspto.gov for AUROS, AUROS*, *AUROS, AURORA OS, AURORAOS, limited to live records in classes 9, 42, 35, 38; TMview https://www.tmdn.org/tmview; WIPO Global Brand Database; handle checks on GitHub, Flathub, PyPI, npm), a related-goods judgement rule (any live class 9 or 42 AUROS, AURORA OS or equal for operating-system or installer software means rename before launch; a class 42 building-energy mark is noted, not automatically fatal), and TSDR status checks for AUROS GROUP and AUROS360. Do the brand.h refactor and the "no hard-coded AurOS" test only if the search finds a blocker (build/forge and the profile already read `brand_name`).
- **Owner remedy.** Run the searches (1 to 2 hours, $0) and paste results into the file. If clear, optionally file a USPTO application for AUROS in standard characters, class 9 ("downloadable operating system software; downloadable software for installing and configuring operating systems"), using only ID Manual entries: $350 base per class, +$100 if information is incomplete, +$200 per class for free-form text. Form the LLC first and file in its name, or file use-based in the owner's own name and assign with the business (an entity that does not exist yet cannot be a valid applicant). Examination about 4.5 months (FY2026 figure). UK online one-class fee rose to GBP 205 on 1 April 2026; EU about EUR 850 (verify). If anything blocking appears, rename to a coined word now.
- **Skeptics.** Both stand as a process item; filing is later.

#### TM-5. Firefox shipped with modified configuration under Mozilla's marks (trademarks lens)
Severity: **later**. Duplicate of LIC-6; same remedy and owner email.

- Notable: the fix is not a launcher rename (done); the remaining Mozilla marks are inside the binary. The tarball fallback hard-codes the Firefox name and a one-key `distribution/policies.json`; fix with LIC-6. Paid bundles that include Firefox breach the "not tied to buying a service" rule: before-charging-money. Skeptics: both say later.

### 2.3 Privacy

#### PRI-1. The website says "Nothing about you is collected" while GitHub logs every visitor and downloader
Severity: **before-public-launch (low)**.

- **Rule.** FTC Act s5 and the 1983 Deception Policy Statement (material, misleading net impression). GitHub docs: "When a GitHub Pages site is visited, the visitor's IP address is logged and stored for security purposes", https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages.
- **Trigger.** `website/index.html` line 335 ("Nothing about you is collected: No ads, no tracking, no account to make"), and the footers on five pages ("No cookies, no tracking, no ads"). The site is not deployed yet (Pages returns 404; `pages.yml` skips deploy until a full release carries the exe), and `code-signing.html` already says GitHub sees the address.
- **Consequence.** A ready-made exhibit, not a day-one fine. Enforcement against a free hobby project is remote. The sentence must change again at WP-73.
- **Agent remedy.** Replace index.html line 335 with: "No account and no ads. The website and the download are hosted by GitHub, which keeps visitors' internet addresses in its own logs. AurOS Setup sends nothing to the AurOS project." (not the first draft's "AurOS Setup tells you before it sends anything", which is a new absolute needing a `data-claim` proof). Footer on the five pages: "No cookies and no ads. AurOS collects nothing about you; GitHub, which hosts this site, keeps its own logs of visitors' internet addresses." Add "Nothing about you is collected" and unqualified "we collect nothing" to the banned list in `tools/site/check.mjs` (WP-40/41); record each remaining privacy sentence in `docs/CLAIMS.md` (does not exist yet; WP-41). Link the privacy page only after it exists (PRI-2 / WEB-1). Fold this into the first Pages deploy.
- **Owner remedy.** Approve the wording. $0.
- **Skeptics.** Both stand; before-public-launch (low).

#### PRI-2. PRIVACY.md contradicts itself and omits the planned flows (outcome ping, report, hardware report, download cap, notice feed)
Severity: **before-public-launch** (gate: before the first of WP-73, WP-39, WP-74 or WP-76 ships). Title overstated: section 1 already names Wishes as the exception; the draft is accurate for v0.1.

- **Rule.** FTC Act s5 (policy must match practice); UK GDPR Art 13 if the UK is targeted (an email address satisfies "contact details"; no postal address needed). CCPA citation in the finding was wrong and CCPA does not apply (free, no revenue). Plan decision 5 and WP-73 already say "PRIVACY.md is updated first".
- **Trigger.** `docs/legal/PRIVACY.md` s1 "We don't collect anything about you" and "Nothing on your computer is sent to us", s4 "AurOS doesn't send us anything", and the light/dark-switch sentence in s2 describes a switch that does not exist.
- **Consequence.** The policy becomes false the first time a ping ships; wave stop rules depend on the ping. Also the live site and the SignPath text make the same absolute claims (PRI-1; `website/code-signing.html:93`; `docs/legal/signpath-application.md` lines 75, 117-123). SignPath requires a privacy statement and an opt-out for data sent to unspecified systems; the unticked box meets that, but the code-signing page must change in the same commit.
- **Agent remedy.**
  1. Rewrite s1: "AurOS collects nothing about you unless you switch something on. By default AurOS sends nothing about you or your computer to us. Three things can reach us, and each is your choice or is shown first: (1) the optional box 'Tell AurOS how the install went' (off unless you tick it) sends a short list of fixed answers (section 3a); (2) 'Tell the AurOS team' sends a report only after you have read it and press Yes; (3) Wishes, if you switch it on (section 4a). Our own download and update servers see your internet address when your computer asks for a file, as every server does; we do not keep a list of addresses." Correct "we do not keep addresses" to what is built: Workers store no IP; the Wishes ledger keeps HMAC counters under a key destroyed after 2 days; Cloudflare and GitHub log under their own policies and are outside our control. Add Cloudflare as a recipient in s2/s4 once `AUROS_DOMAIN` is set.
  2. Add s3a listing the exact ping fields (copy the schema whitelist from the Worker test); s5 retention (PRI-7); a data map table (item, where, who sees it, how long): outcome counts (aggregate, N days), Workers Logs (3 days Free / 7 days Paid, https://developers.cloudflare.com/workers/observability/logs/workers-logs/), tokens (90 days), support mail, GitHub issues (public, permanent).
  3. Delete the light/dark sentence. Name the `/v1/hw` and notice-feed flows or state that v0.1 ships only the ping, the report and the edge counter.
  4. Same edit in `website/code-signing.html` and `signpath-application.md`: "Nothing is sent to the AurOS project unless you tick the optional box", with a Privacy link on the installer's Ready page.
  5. CI lint (extend WP-38/WP-49, not a parallel mechanism): every Worker route in `cloud/`, `infra/edge/` and the report Worker is named in PRIVACY.md; every fetch/POST host in the installer or image is in the s4 table.
  6. PRIVACY.md also has placeholders that block publication: controller name and contact, `[CHECK]` markers, `[LAWYER]` notes. Resolve under D15.
- **Owner remedy.** Choose retention for support email and reports (suggest 12 months). Provide controller name and contact (an email address and name are enough). $0.
- **Skeptics.** Both stand; both put it at a hygiene gate tied to the first build containing the flows.

#### PRI-3. AurOS's own Workers (outcome ping, download gate, notices, report, hardware report) will see IP addresses; "no stored IP / no ID" must be true
Severity: **later** (split verdict; gate: before the report/ping Worker ships).

- **Rule.** GDPR Recital 30 and Breyer C-582/14 (IP as personal data; unverified this session), ePrivacy Art 5(3)/PECR reg 6, FTC Act s5 for "no ID, no stored IP". CCPA does not apply.
- **Trigger.** WP-73/74/76/39/52. The edge Worker already counts total downloads in one Durable Object, never reads `cf-connecting-ip`, and sets `observability.logs.invocation_logs=false` (`infra/edge/wrangler.toml.in`). The Wishes Worker reads `cf-connecting-ip` once (`cloud/src/app.js:120`). The report Worker (`/v1/outcome`, `/v1/hw`) is unbuilt.
- **Consequence.** Only an implementation bug (per-IP counting, console.log of the request) would make the promise false.
- **Agent remedy.** When the report Worker is built: its own Worker with its own `wrangler.toml` (`invocation_logs=false`, `traces=false`, no head sampling, no `cf-connecting-ip` reads on `/v1/outcome`, `/v1/hw`, `/notice`, `/get`), the address-free log canary from `cloud/test/adversarial.test.js` (line 89), a field-whitelist test and a test that two identical pings are indistinguishable, timestamps rounded to the day. The Ready page lists the exact fields before the box (box unticked, Yes/No equally prominent). Worded "AurOS keeps no internet address" and names Cloudflare as carrying the request. Use a Cloudflare zone rate-limit rule at the edge if one script flooding the global cap is a concern, and document that rule's events are visible in Security Events.
- **Owner remedy.** Confirm in the Cloudflare dashboard that Workers Logs, Logpush, Web Analytics and zone-level Security Events logging are off or understood for these Workers. $0.
- **Skeptics.** One refuted as a standalone legal finding; one kept it as a build-acceptance item. Later.

#### PRI-4. Wishes consent screen and notice are only drafts and need specific fixes
Severity: **later** (gate: before the first non-owner turns Wishes on). Both skeptics.

- **Rule.** GDPR/UK GDPR Arts 6(1)(a), 7, 13, 44-46 (only if UK/EU users are served); FTC Act s5; Anthropic retention: API inputs and outputs deleted within 30 days, flagged ones up to 2 years, classification scores up to 7 years (https://privacy.claude.com/en/articles/7996866). Anthropic Usage Policy (effective Nov 12, 2026) and Commercial Terms D.3.
- **Trigger.** `docs/CLOUD.md` s4 draft omits the 7-year scores and has blank controller name and contact. The shipped string is `CONSENT[]` in `src/aurshell/wish.c` (about lines 82-93), hard-coded to `"version":1` (about lines 2399, 2404). Equal buttons and no default focus are already built.
- **Consequence.** Wishes is off in v0.1 (`wishes="off"`, `tools/wishesoff.sh`), so nobody sees the screen yet.
- **Agent remedy.** Replace the text in `CLOUD.md` s4, `PRIVACY.md` s4a, `AI.md` s1.4 and `wish.c CONSENT[]` together: "Wishes uses an AI. To understand a wish, AurOS sends the words you type or say to an artificial intelligence (AI) called Claude, made by a company called Anthropic in the United States. You are not talking to a person. What is sent: only your words, your language, and a list of what this computer can do. Not your files, passwords, contacts, or anything you type anywhere else. Who sees it: the AurOS wish service, run on Cloudflare (in the United States and worldwide) by [owner's name], [contact e-mail], and Anthropic. AurOS keeps none of your words. Anthropic deletes them within 30 days. If its safety checks flag a request, it may keep that request for up to 2 years and a safety score about it for up to 7 years. Anthropic does not use your words to train its AI. The AI can misunderstand you. AurOS always tells you in its own words what will happen, and nothing happens until you press Yes. Do not depend on Wishes for medicine, safety or emergencies; in an emergency call your local emergency number. Wishes is for people aged 18 or over. You can turn Wishes off at any time in Settings. Forget everything deletes what AurOS holds; AurOS cannot make Anthropic delete its copy earlier. Privacy policy: [link]. [ No thanks ] [ Yes, use Wishes ]". Corrections folded in: no "please do not put health details" line while pill reminders are a shipped example (see PRI-5); no program names are sent (cloud.c sends the fixed vocabulary, sensor booleans and the look; make all four documents say so); keep "7 years" tied to a dated check of Anthropic's page; ship the final text as consent version 1 (nothing at v1 ever shipped), bump only after launch. Extend the WP-49 lint to compare processor names, 30 days, 2 years, 7 years, 18, emergency and "train" between `CONSENT[]` and PRIVACY 4a. Layer the screen (a short front screen, details one tap away) so it fits large type.
- **Owner remedy.** Supply the name and contact to display (owner's own name and a mailbox satisfies it at $0; an LLC is not required). Approve the wording.
- **Skeptics.** Both stand at later.

#### PRI-5. Wish text will contain health information (pill reminders)
Severity: **later** (split verdict; gate: before Wishes reaches anyone).

- **Rule.** Washington My Health My Data Act (RCW 19.373): collection and processor disclosure needed to provide a requested service are allowed without separate consent (RCW 19.373.030(1)(a)(ii), 010(27)(b)); the remaining duty is a standalone consumer-health-data policy with a homepage link (RCW 19.373.020). Whether an unincorporated individual is a "legal entity" is unresolved. A private CPA suit needs injury to business or property (RCW 19.86.090). GDPR/UK GDPR Art 9(2)(a) only if UK/EU users are served; CCPA does not apply.
- **Trigger.** AI.md pill-reminder examples; `src/aurshell/wishgallery.c` already has a local "Take my pills" reminder that never leaves the computer; WP-46/47/49 already plan the sensitive set, the second screen and Art 9 consent. Redaction (`cloud_clean_text`, `src/aurwish/cloud.c:93-150`) strips 5+ digit runs and emails.
- **Agent remedy.** (1) Route recognised health-word reminders to the local gallery template with no network call and add that as an acceptance test in WP-46/47. (2) A separate `website/health-data-privacy.html` (categories, purpose, processors Cloudflare/Anthropic, not sold, how to delete, 45-day contact) linked from the homepage footer, only if cloud health free text is allowed. (3) Do not put "health" on the Worker's refusal list (it would block the main use and the text still reaches Anthropic). (4) Keep the pill reminder with the "convenience, not a medical alarm" line (AI-5). A brittle keyword nudge is optional hardening, not compliance.
- **Owner remedy.** Decide: keep the pill examples with the caution, or swap them for non-medical ones. Optionally geo-limit Washington (imprecise). $0.
- **Skeptics.** One at later, one at later.

#### PRI-6. UK/EU reach: GDPR/UK GDPR Art 27 representative, ICO fee, consent
Severity: **later** (gate: before Wishes reaches UK users). Both skeptics.

- **Rule.** UK GDPR Arts 3(2), 13, 27 (a representative unless processing is occasional and low risk; Wishes is continuous and may carry health words). EU GDPR needs targeting (Recital 23: accessibility is not enough). Fines in the finding were mis-tiered (Art 27 sits in the lower tier). ICO fee tier 1 GBP 52 only if you must pay at all; Tier 3 does not apply to a person with no staff and no turnover; whether an overseas controller must pay was not confirmed.
- **Trigger.** Decision 21 names English-speaking countries; the UK is a stated target. The EU is "maybe later". There is no "pre-ticked ping box" (it is unticked); the pre-ticked Wi-Fi box is local-only and not a GDPR consent issue.
- **Agent remedy.** Replace the `[LAWYER]` placeholder in PRIVACY.md s7 with a short UK/EU rights paragraph (controller, lawful basis: consent for Wishes and the ping, legitimate interests for support email, transfer via Anthropic's and Cloudflare's SCCs and the UK Addendum and Cloudflare's DPF clause, retention, rights, right to complain to the ICO or local authority). A single Worker env flag to refuse a country at `/v1/register` (see AI-6; avoid a new IP lookup). Keep optional collection unticked and record consent version.
- **Owner remedy.** Run the free ICO self-assessment (about 2 minutes; Tier 1 is GBP 52 a year if it says pay). Decide on a UK representative (estimate GBP 100-500 a year, unverified) or leave GB out of the Wishes allow-list. Do not add EU countries until an EU representative, DPIA and Art 50 text exist. Budget $0 to about $600 a year.
- **Skeptics.** Both stand; later.

#### PRI-7. Support mail, "Tell the AurOS team" reports and hardware reports hold identifying data with no retention rule
Severity: **before-public-launch (low impact)** (split verdict; narrowed to one sentence and one design decision).

- **Rule.** GDPR Art 5(1)(e) and Art 13(2)(a) (state retention) if the UK/EU is served; FTC Act s5 (a published promise must be kept). State breach laws cover only defined personal information (name plus SSN, account number, medical data, or credentials), not a name and email.
- **Trigger.** `PRIVACY.md` s5 still reads "[OWNER: how long you keep emails or private reports]". WP-39 (`/v1/report`) and a support inbox at `support@<domain>` (PLAN line 66) are launch criteria; no route, mailbox or SOP exists yet. `hwinv.h` already promises reports hold no serials, PC name, user name, Wi-Fi name or file names; the wifi_name and disk serial the finding cites are local state, not report contents.
- **Agent remedy.** (1) Decide whether `/v1/report` stores the report or only forwards it to the inbox. If it stores, give it a 90-day TTL; if it forwards, the inbox rule is the only retention. (2) Write in PRIVACY.md s5: "We keep emails and reports only as long as we need them to help you and fix AurOS, normally no more than 12 months, then delete them." Do not state 90 days unless the Worker enforces it. Add: "If you send us a report about a problem, we keep it as long as needed to fix the problem and meet our legal duties." (3) Put the delete-a-thread SOP in a new `docs/ops/SUPPORT-OPS.md` (not in the user-facing `docs/SUPPORT.md`). (4) WP-39 acceptance: field allow-list test, serials hashed or dropped, SSID dropped, home path stripped.
- **Owner remedy.** Turn on 2-step verification (a security key) for the support mailbox, Cloudflare and GitHub. Auto-delete after 12 months is not free on Gmail (filters act on incoming mail only); use an Apps Script time-driven trigger or a recurring calendar reminder. $0 to about $100 for a key.
- **Skeptics.** One later, one before-public-launch low. Taken at before-public-launch for the sentence.

#### PRI-8. Future paid offers (human help, Wishes beyond a quota) break current privacy promises and add payment and recording obligations
Severity: **before-charging-money**. The wording sub-item is before-public-launch (see WEB-3, BIZ-7).

- **Rule.** FTC Act s5 and ROSCA (15 U.S.C. 8401-8405) and state auto-renewal laws apply only to subscription or negative-option offers; the FTC click-to-cancel rule was vacated in July 2025 and rulemaking restarted in 2026. CCPA thresholds (about $26.6M revenue, 100,000 consumers) are far away. Recording laws (Cal. Penal Code 632) bite only if sessions are recorded. GitHub Pages terms bar commerce-focused sites. PCI burden is minimal with a hosted checkout.
- **Trigger.** Decision 12 and WP-69; TERMS s1 "nothing to pay, now or later"; the site says "no subscription". A paid Wishes tier would tie a payer identity to prompts.
- **Agent remedy.** Pre-write `docs/legal/PAID-OFFERS.md` (see LIA-8): separate paid-service privacy section (processor as independent controller, no card data in AurOS systems, receipts kept per the tax adviser's period, not a fixed 7 years); remote help started only from a button she presses, scheduled, with a consent screen, no recording by default, a visible indicator and an end-session button; prepaid credits rather than subscriptions by default; one-click cancel and refund page; price and tax shown before any card is entered; hosted checkout off GitHub Pages; payment events logged by the merchant's order id only.
- **Owner remedy.** Pick a processor or merchant of record (see BIZ-8, BIZ-9), open it in the LLC's name, decide the scope of human help. Do this before any price is shown.
- **Skeptics.** One refuted as a legal risk, one kept a split (wording now, payment machinery at charging). Final before-charging-money.

#### PRI-9. Comprehensive US state privacy laws: not triggered, but thresholds are low and some have no size test
Severity: **later**. Both skeptics.

- **Rule.** CCPA (about $26.6M revenue, 100,000 consumers, or 50% of revenue from selling data); Connecticut and Rhode Island 35,000 consumers; Texas TDPSA excludes SBA small businesses (not a "no size test" risk); Connecticut's 2025 amendment reaches any controller processing sensitive data. Montana 25,000 (unverified).
- **Trigger.** Wishes tokens and support contacts are the only identifiable per-person data; Wishes is off in v0.1.
- **Agent remedy.** One LAUNCH.md line: before Wishes goes live and quarterly, check the live token count from the ledger; brief the owner if it exceeds 15,000. No per-state counting via `request.cf.region` (it adds location processing). A "Your US state privacy rights" section drafted but published only when the count nears half the lowest threshold. "We do not sell or share personal information and we do not use it for advertising" may go in now. Keep the existing `PRIVACY.md` s7 promise. Pre-Wishes checklist: decide whether health words can reach the Worker.
- **Owner remedy.** 15 to 30 minutes a quarter. $0.

### 2.4 Liability and terms

#### LIA-1. No Terms, Disclaimer or assent anywhere a user can see
Severity: **before-public-launch**.

- **Rule.** Browsewrap fails without assent (Specht v. Netscape, 306 F.3d 17; Nguyen v. Barnes & Noble, 763 F.3d 1171). Berman v. Freedom Financial, 30 F.4th 849, held a small, faint link did not bind; it is a design standard, not support. UCC 2-316 largely does not apply (a free download is not a sale). UK CRA 2015 and Australian ACL apply only to traders / trade or commerce (verified for CRA s33, s61; the Australian points from memory). Unilateral change of terms without direct notice is not binding in the Ninth Circuit (Douglas v. U.S. District Court; unverified).
- **Trigger.** `docs/legal/README.md` says the drafts are not published; the website footers link no Terms; TERMS s4 admits the installer asks for no acceptance; `wizard.c` removed the AGREE box and the Ready page is capped near 80 words. TERMS s2 is silent on the helper; TERMS s6 does not say MERCHANTABILITY or NON-INFRINGEMENT; s8 lets the project change terms at any time; s2 age and s12 governing law are `[OWNER]` placeholders.
- **Consequence.** The exclusions bind nobody without assent. The realistic exposure for a data-loss claim is negligence or misrepresentation, where a disclaimer is weak anyway. The helper who clicks Install may not be the owner.
- **Agent remedy.**
  1. Installer Ready page (`wizard.c`), directly above the Install AurOS button, in normal body size and contrast (not the `C_QUIET` style), replace other words to stay near the 80-word budget: "By pressing Install AurOS you agree to the AurOS Terms [Read the Terms]." The link opens `website/terms.html` and an embedded plain-text copy opens offline. Write "terms accepted: v1, <timestamp>" to the install journal (local only; the owner never sees it). Update the navtest that asserts "Ready's one button is Install AurOS" and the widget contract. For the owner's actual use, add a second assent point on the first-boot "Yes, it all works" screen.
  2. Create `website/terms.html` and `website/disclaimer.html` from TERMS/DISCLAIMER, linked from every footer page and next to the Licences link (LIC-2). `tools/site/check.mjs` fails if a footer page lacks the Terms, Disclaimer and Privacy links.
  3. Remove the DRAFT banners in the same change; resolve every `[OWNER]`, `[LAWYER]`, `[CHECK]` bracket first. One closing line: "These terms were written by the AurOS team without a lawyer. If anything is unclear, tell us before you use AurOS." Keep the longer "no lawyer reviewed this" note in `docs/legal/README.md`.
  4. TERMS s2: "If someone is helping you, they must have your permission to change this computer. Pressing Install on a computer that is not yours, or without its owner agreeing, is not allowed."
  5. TERMS s6: put the disclaimer in capitals with MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE and NON-INFRINGEMENT; add "Nothing in these terms affects consumer rights that cannot be excluded where you live" and "gross negligence, wilful misconduct, death or personal injury caused by negligence, and fraud" to the s7 carve-outs.
  6. TERMS s8: "Changes apply to later installs; we do not change your terms for installs already done."
  7. Version the terms (v1, date) and show the version in the footer.
- **Owner remedy.** Supply: legal name (or LLC), governing-law state, a contact address (an email address or PO box is enough), the age line ("18 or over, or with a parent or guardian's permission"). $0, about 30 minutes. A name or home address on public terms is a privacy trade-off.
- **Skeptics.** Both stand; before-public-launch (strangers, not testers).

#### LIA-2. No informed-risk disclosure or backup prompt at the irreversible step
Severity: **before-public-launch**.

- **Rule.** Warning supports comparative fault and failure-to-warn defences; an express release would need an agreement. Cal. Civ. Code 1668 and Tunkl govern exculpatory clauses and do not apply (nothing exculpatory is in force). FTC Act s5(n) (15 U.S.C. 45(n)): substantial injury not reasonably avoidable; the Lenovo/Superfish analogy is inapt. The better hook is deception from the absolute "Windows and all your files stay".
- **Trigger.** `wizard.c` removed the backup tick boxes (about lines 651-657, owner decision D3). `page_ready()` opens with "Windows and all your files stay, and there is enough room for AurOS." with no qualifier. `docs/AURBRIDGE.md` calls the NTFS shrink "the one irreversible step" and lists "worst case, data loss". The backup advice exists on the site FAQ and in `advanced.html`, but not on the Ready page or the printed `guide.html` sheet.
- **Consequence.** The owner's own docs show a known, non-obvious risk with no warning at the point of risk.
- **Agent remedy.** Reword the first Ready row and add a short non-blocking notice beside the button: "Windows and your files are kept. Any change to a drive carries some risk, so copy photos you can't replace to a memory stick first." (About 12 net words; replaces the absolute row; the page must still fit 1366x768 without scrolling.) Give the same sentence to the `sheet-warn` block of `website/guide.html` and fix `index.html:345` ("untouched" becomes "kept") and the printed guide's "Windows and your files stay". A "Show me how" button is optional; if built, an in-wizard text pane, not a browser launch. Do not log "risk notice shown" (nobody sees it). Pin the sentence with a `data-claim` entry and a `--navtest` string check; keep the per-release Ready-page screenshot (`docs/shots/wizard/4a-ready.png`). Fix the stale AURBRIDGE.md page list (BACKUP "both required", CONSENT "typed acknowledgement").
- **Owner remedy.** Approve one more sentence. $0. This notice does not replace the LLC and insurance (LIA-6, LIA-7).
- **Skeptics.** Both stand; one said the wording fix should be inline rather than a separate paragraph.

#### LIA-3. Wave 1 geography: "English-speaking countries" invites UK, Australian and Canadian consumer regimes
Severity: **before-charging-money** (split verdict).

- **Rule.** UK DMCC Act 2024 applies to a "trader" acting for business (s225(3), s249: "for gain or reward"); ACL requires "in trade or commerce"; Quebec's French-language rules target enterprises. A free individual project is probably outside them until money changes hands. The Valve case involved a corporation selling paid games. Penalties s182(6): GBP 300,000 or 10% of turnover, not an "individual tier".
- **Trigger.** Decision 21 and WP-45; no territory statement on the site; infra/edge has no country check; TERMS s12 blank. Ireland and Malta are English-speaking EU states, so "English-speaking" does not exclude the EU.
- **Agent remedy.** Change decision 21 and WP-45 to "United States only for waves 1-2" (optional). One territory sentence in the download area and TERMS s1: "AurOS is made for people in the United States for now." Add a country check to the edge Worker (`request.cf.country`) so waves 1-2 are US-only in practice. Write `docs/legal/COUNTRIES.md` only when a second country is actually opened. The TERMS statutory-rights sentence (LIA-1 item 5) covers UK/AU users cheaply.
- **Owner remedy.** Choose the wave plan: US only for waves 1-2 (recommended), add UK/AU after a month of field data. $0.
- **Skeptics.** One refuted as launch-gating, one agreed at before-charging-money.

#### LIA-4. EU Product Liability Directive 2024/2853 and GPSR
Severity: **later** (before any EU-targeted or EU-reachable commercial supply).

- **Rule.** PLD (transposition by 9 Dec 2026): software is a product; data destruction is damage; liability cannot be excluded (Art 15); free and open-source software supplied outside a commercial activity is excluded (Art 2(2); recitals 14-15 treat a price or personal data as commercial; text not re-verified, EUR-Lex was blocked). Both laws need placement on the Union market in the course of a commercial activity. GPSR (2023/988) is a separate regime that requires an EU responsible person; the finding mixed the two.
- **Trigger.** Planned paid help and paid Wishes (decision 12), "maybe EU later", worldwide download.
- **Agent remedy.** Five lines in `docs/legal/README.md` or TERMS: "AurOS is offered for use in the US, UK, Canada and Australia, English only, and is free open-source software supplied outside a commercial activity." Add to WP-69: the merchant-of-record country allow-list excludes the EU/EEA, and no paid offer is shown to EU visitors. A repo test that no price or checkout string ships in v0.1. Do not add an "EU-resident clause" now (it reads as targeting). The existing "put Windows back" and `withdrawn.txt` kill switch and the SBOM (WP-68) already serve the update-capability story.
- **Owner remedy.** Nothing now. Before any EU launch or EU-reachable paid offer: a scoping consult (estimate $1,000-3,000, unverified) and an authorised representative if needed (estimate EUR 500-3,000 a year, unverified).
- **Skeptics.** Both stand; later.

#### LIA-5. No plan for what happens legally when the installer does lose someone's data
Severity: **before-public-launch** (narrowed to a roughly 10-line addendum).

- **Rule.** US exposure is ordinary negligence and negligent misrepresentation; a gratuitous undertaking can create a duty (Restatement (Second) of Torts ss323/324A are poor fits for pure data loss; the economic-loss rule often applies; unverified). Disclaimers do not cover negligence or gross negligence. Insurer policies usually forbid admitting liability or voluntary payments without consent. FRE 408 protects compromise offers; an unconditional payment may be admissible and does not close the claim.
- **Trigger.** `docs/ops/INCIDENTS.md` playbook 5 already has a Sev-1 data-loss lane, read-only steps, a scripted first reply and a same-day write-up. WP-82 (Rescue stick) and WP-83 (second responder) are planned. The finding's "we never keep logs" posture conflicts with nothing: PRIVACY s3 says the log is only seen if she sends it.
- **Agent remedy.** Amend playbook 5 with: (1) on first report, preserve the report thread, release tag and SHA256SUMS, build commit, CI logs (Actions artifacts expire after 14 days, logs after 90) and never delete or edit the issue or emails; (2) the first message contains no conclusion on fault either way and does not ask her to sign anything; (3) never condition help on a release; any later payment is the owner's decision alone, with a short plain-language optional no-admission release, time to think, no non-disparagement, and the insurer's consent if insured; (4) cite the LAUNCH.md stop rules. Fill PRIVACY s5. No new wizard "send install record" feature (the journal carries the Wi-Fi key and hardware identifiers); use the redacted WP-82 diagnostic bundle or the WP-39 preview.
- **Owner remedy.** Decide a goodwill ceiling (suggested reserve $1,000-5,000, a judgement, not sourced). Notify the insurer immediately if insured. $0 until an incident.
- **Skeptics.** Both stand narrowed; goodwill reserve and insurer notice are later / before-charging-money.

#### LIA-6. Form an LLC before the first stranger installs
Severity: **before-public-launch** (hard gate before charging money and before Wishes goes beyond family). Merges BIZ-1.

- **Rule.** A single-member LLC protects against entity contracts and other people's torts, not the member's own negligence or deceptive claims (agent-tort rule; FTC/AG name individuals who direct a practice; Restatement (Third) of Agency 7.01); protection is prospective only; undercapitalised single-member LLCs are pierced, so keep capital and insurance. Delaware 6 Del. C. 18-303 says members are not liable "solely by reason of" membership. FinCEN BOI reporting no longer applies to US companies (final rule 11 Aug 2026).
- **Trigger.** `TERMS.md` and `PRIVACY.md` carry `[OWNER: legal name ...]`; LICENSE says "AurOS contributors"; repo, Cloudflare, Anthropic, domain and SignPath application are in the owner's name; plan decision 9 recommends an LLC; `OWNER-SETUP.md` has no formation steps; `SUPPORT.md` line 3 says "made by a small team".
- **Consequence.** A user's claim reaches the owner's personal assets; for the owner's own negligence the LLC does not change that. The LLC does help with contracts, vendor bills, volunteers and as the insured entity. SignPath issues certificates to SignPath Foundation and does not require an entity; the repo's older "certificate issued to the entity" text is stale.
- **Agent remedy.** `docs/legal/ENTITY-CHECKLIST.md` and an `OWNER-SETUP.md` section "Entity, EIN, bank": (1) name availability on the Secretary of State site and the USPTO search; (2) form a single-member LLC in the owner's home state (Wyoming/Delaware add foreign registration at home with no liability benefit); (3) EIN at irs.gov (free); (4) one-page operating agreement (free templates fine) with a sentence that the LLC owns all AurOS IP, a named successor if the owner dies or is incapacitated (Delaware 18-801(a)(4) winds an LLC up with no members unless continued within 90 days), and a rule that funds are never mixed; (5) separate bank account; (6) written IP assignment from the owner to the LLC, then change the notice (BIZ-3); (7) capitalise the LLC and take the E&O quote (LIA-7); (8) move accounts: the repo to a GitHub organisation, then Cloudflare, Anthropic, the domain registrant (BIZ-5 caveats: the Pages URL is not redirected, so do it with the domain); (9) update TERMS, PRIVACY, DISCLAIMER, SECURITY, the footer and LICENSE with the LLC name and address; (10) a dated calendar item for annual report and franchise tax once the state is known; (11) fix `SUPPORT.md` line 3 ("one founder plus automated tooling"). Add an LLC row to `docs/LAUNCH-READINESS.md` (owner-attested date, no EIN in the public repo) instead of a second `entity.json` gate in `release.yml`. Drop SignPath-in-the-LLC's-name from the checklist.
- **Owner remedy.** File the Articles of Organization in your home state before the public link, and before any Wishes beyond family or paid offer. Cost: filing roughly $35-$500 one time; recurring costs vary (California $70 + $20 + $800 a year minimum franchise tax even with no income; New York can add $1,000+ publication; Massachusetts $500 + $500 annual report; Texas about $300 and a free annual report). EIN free. Business checking usually $0-15 a month. Registered agent $50-300 a year only if you will not use your own address. Year one $200-$1,500 in most states, more in California and New York (fees unverified; check each Secretary of State site). If you are under 18, say so: contracts of a minor are voidable and an adult co-owner or fiscal sponsor is needed first.
- **Skeptics.** Both stand and downgrade from before-anyone-uses to before-public-launch.

#### LIA-7. Insurance: get a Tech E&O + cyber quote; do not assume it covers installer data loss
Severity: **before-public-launch** (quotes and question list; bind by wave 2 and before any paid offer). Merges BIZ-6.

- **Rule.** Not a legal rule: risk transfer. Tech E&O is usually claims-made with a retroactive date, so cover bought after launch may exclude first-week failures. Cited premiums do not match the sources: Vouch median E&O $3,700 (range $1,300-$12,400), median cyber $2,900; TechInsurance median E&O $68 a month ($810 a year), cyber $170 a month ($2,041), general liability $350 a year. None addresses open-source or zero-revenue projects.
- **Trigger.** Plan decision 9 / WP-69 mention only an E&O quote at v1.0; `docs/OWNER-SETUP.md` has no insurance or LLC item; `docs/RELEASE.md` section 6 and `docs/issues/v3/11-release-blockers.md` row 11.5 list it as a release blocker. A free, zero-revenue product may be declined; many forms define covered services by fee, and general liability forms treat electronic data as not tangible property and exclude it.
- **Agent remedy.** Private `docs/legal/INSURANCE-BRIEF.md` (keep out of the public repo, or publish only a redacted outline): what the software does to disks, the safeguards (put-back, refusal rules, wave stop rules, e2e and real-PC evidence), data flows, user count per wave, revenue (zero), planned paid services. Statements must trace to code and test evidence (applications are warranties; say plainly that nothing is proven on a real PC yet). Questions to ask: is data loss from a disk operation covered; is a free, no-revenue product insurable; is retro date = inception available; are Wishes (third-party AI processing) and a paid tier covered; tail cover (extended reporting) if the policy lapses; what a general-liability add-on costs and whether it excludes electronic data; named insureds = the LLC and the owner individually. Add an insurance row to the WP-91 owner track with a date before WP-43. Fallback if declined: written risk acceptance plus the LLC, disclaimers, consent screens and wave caps (do not describe general liability plus cyber as covering installer data loss).
- **Owner remedy.** Get 3 quotes (a specialist broker such as Vouch, Hiscox, Coalition, Insureon or TechInsurance, plus a local agent) for tech E&O plus cyber, $1M per claim, after the LLC exists. Budget $1,000-$3,500 a year (bundled; E&O + cyber + GL separately about $3,200 on the medians). Bind before wave 2 (100 downloads a day) and before any paid offer. Notify the insurer before making any payment to a claimant.
- **Skeptics.** Both stand at before-public-launch for the quote step; binding at wave 2 or before-charging-money.

#### LIA-8. Paid offers need their own consumer-sales legal layer
Severity: **before-charging-money** (wording sub-item before-public-launch).

- **Rule.** ROSCA and state auto-renewal laws apply only to subscriptions and auto-renewal (a one-off purchase takes AurOS outside them); the FTC TSR covers telephone sales (no AurOS phone line exists; the 2024 tech-support amendment was not verified); cooling-off rules (16 CFR 429) cover door-to-door sales and do not apply online; UK/EU 14-day rights and ACL guarantees only if sold there.
- **Trigger.** PLAN v1.0, decision 12, WP-69 ("a paid support line" contradicts decision 6 and SUPPORT.md: reword to email or in-app chat); "Call Sarah" is a free family call button, not paid support.
- **Agent remedy.** `docs/legal/SALES-TERMS-OUTLINE.md`: price shown before any charge; one-click cancel; 30-day no-questions refund; no auto-renewal in the first version (one-off purchases); receipts; support hours; no outbound calls. Do not add payment UI to the image. Reword now (before public launch): TERMS s1 "There is nothing to pay, now or later" becomes "AurOS itself is free. Some extras may cost money later, and we will say so before you pay."; TERMS s5 "The service is free" becomes "Wishes is free for now"; the site's "no subscription" is dropped in the commit that creates the first price. Keep human-help sales to email or in-app chat only, and say which products go through a merchant of record (BIZ-8, BIZ-9). Check whether a paid Wishes quota is a software sale for tax purposes.
- **Owner remedy.** After the LLC exists, open the merchant-of-record account (about 5% plus $0.50 per sale typical; at a $5 sale that is 15%, so check fees before setting prices). Spend one CPA hour on sales tax when revenue starts ($150-400). Do this before any offer is visible.
- **Skeptics.** Both stand; before-charging-money.

### 2.5 Security law

#### SL-1. EU Cyber Resilience Act: out of scope while free and ownerless, but the boundary is unrecorded and fragile
Severity: **before-charging-money**.

- **Rule.** Regulation (EU) 2024/2847. Manufacturer reporting (Art 14) applies from 11 Sep 2026; most other duties from 11 Dec 2027. Free open-source software supplied outside a commercial activity is out of scope; an individual maintainer is not a "steward". The Commission's July 2026 guidance (secondary source: https://www.opensourceforu.com/2026/07/eu-clarifies-cra-rules-for-non-commercial-open-source/, an 80-page non-binding handbook) says donations, sponsorships, public funding, and paid consulting, training or support do not by themselves make a project commercial, while selling the software, paid editions, monetising services delivered through the software, or demanding personal data beyond security needs do. Operating systems are Annex III Class I (third-party assessment unless harmonised standards are applied; unverified). The CRA needs placement on the EU market; an English-only, US-first launch is arguably not on it. EUR-Lex was not readable, so Recital 18 and Art 3(14) are from memory.
- **Trigger.** Plan decision 12 and WP-68 already schedule an SBOM per release, a declared support period and a 24-hour reporting runbook (so "no CRA analysis" is wrong). The real trigger is a paid Wishes quota reached from the free shell, a sale of the image, or a pre-installed PC; paid human help alone is probably not.
- **Agent remedy.** Extend WP-68 with a one-page decision record at `docs/security/CRA.md` (not `docs/legal/CRA.md`): facts (free, MIT, individual, no paid gate on releases, updates or security fixes), the Recital 18 test applied to each planned offer, and the rule "No paid-only releases, security fixes or updates; paid Wishes via the shell is the CRA trigger, so shipping it requires a manufacturer-status decision first; paid support and donations are not." SBOM, a support-period statement in `docs/security/UPDATES.md` (Ubuntu 24.04 security until April 2029, conditional on the project continuing), and a link from SECURITY.md. The 24h/72h/14d playbook and a CI grep guard for paywall/login strings are later and brittle respectively; skip the grep.
- **Owner remedy.** When forming the LLC, keep any paid offer in a service that is not bundled with the download, and do not make any fix or release paid-only. The seller of paid help should not be the AurOS supplier in a way that makes support a signal; otherwise accept manufacturer status. Before any sale of AurOS itself or pre-installed PCs, get a notified-body quote (tens of thousands of euros, unverified) or exclude the EU from the sale. Register with the ENISA Single Reporting Platform only if you become a manufacturer (free).
- **Skeptics.** Both stand; before-charging-money.

#### SL-2. CRA: Wishes Worker and personal-data conditions can themselves count as monetisation
Severity: **before-charging-money** (conditional on EU reach).

- **Rule.** CRA Recital 18 (a platform through which the manufacturer monetises other services; requiring personal data as a condition of use) and Art 3(1)-(2) (remote data processing belongs to the product). The finding cited Art 13(1) wrongly. PLD recitals 14-15 are similar.
- **Trigger.** `docs/AI.md`, `docs/CLOUD.md`, decisions 8, 12 and 5. Wishes is off in v0.1; unpaid family use is outside this.
- **Consequence.** A paid Wishes tier reached from the shell would put the whole AurOS client on the manufacturer track for EU buyers. A separate repo name or "operator identity" does not help if the owner designs and controls both (the Worker is in this repo and `aurwish` ships in the image).
- **Agent remedy.** In `docs/AI.md` and `docs/legal/` record: paid Wishes is not sold to EU/EEA addresses until the CRA route is decided; the AurOS client works fully with no account or payment; no release or fix is gated behind Wishes; the Worker and its secrets are in `SECURITY.md` scope; the outcome ping stays opt-in, fixed-value and no-ID; the consent text says words are used to carry out the wish, not for ads or analytics. Add a "CRA exposure" row to the WP-69 launch gate. Sound structures: fund Wishes by donations, never by price; sell paid offers only in the four countries with USD/GBP/CAD/AUD pricing and no EU marketing; accept manufacturer status and build the Art 14 process first; or a genuinely independent third-party operator.
- **Owner remedy.** Set the merchant of record's sales territory to exclude the EU ($0), or accept manufacturer status. An LLC (about $100-500) helps liability, not CRA scope. Sign nothing for Anthropic's DPA: it is automatic (AI-1); keep a copy and set zero data retention as optional.
- **Skeptics.** Both stand; before-charging-money.

#### SL-3. Export controls: no TSU/BIS notification likely needed, but no classification record
Severity: **later**.

- **Rule.** 15 CFR 734.3(b)(3) and 742.15(b): publicly available encryption source code is not subject to the EAR; the email notification applies only to source implementing "non-standard cryptography" (verified on Cornell LII). BIS: incorporating open-source code does not make an item public. OFAC and embargo rules apply regardless. GitHub's trade-controls page covers sanctioned regions. Syria status in the finding was probably stale.
- **Trigger.** Standard Ubuntu crypto (OpenSSL, GnuTLS, openssh, NSS); no proprietary encrypted component found in `profiles/desktop.profile`; D9 (disk encryption and PIN) not built; `src/aurbridge/format.c` contains an AurOS SHA-256 used only for integrity.
- **Agent remedy.** `docs/legal/EXPORT.md` (public MIT repo, public Ubuntu source, standard cryptography only, AurOS code only calls standard libraries). A D9 design gate: LUKS2/cryptsetup or TPM2 with standard algorithms and no custom primitives (so no BIS email). Footer sentence: "AurOS includes standard cryptographic software. You are responsible for compliance with the export laws of your country." Do not put "not for use in embargoed destinations" in TERMS as a use ban (it conflicts with MIT and GPLv3 s10); keep it a responsibility sentence and reference OFAC/BIS rather than hard-coding country lists. Skip the proposed .c-file crypto lint (it would fail on the existing SHA-256) and the restricted/multiverse package scanner.
- **Owner remedy.** Nothing now. If AurOS-authored non-standard cryptography ever ships, send the free BIS/ENC email notification before release. If selling pre-installed PCs outside the US, self-classify under 5D992.c or ask for a CCATS. $0.
- **Skeptics.** Both stand; later.

### 2.6 AI (Wishes and Ask AurOS)

#### AI-1. The Wishes Worker is already live on the public internet and the "kill switch" does not work
Severity: **before-public-launch** (gate: before Wishes is switched on in any shipped image). Immediate mitigation below is free.

- **Rule.** Anthropic Commercial Terms (effective 17 Jun 2025) D.5 (responsible for all activity under the account), D.2, K.2 (uncapped defence of Anthropic for third-party claims tied to Users' Inputs; L.3.b exempts K from the cap), I.3 (suspension); Usage Policy. `metadata.user_id` is real and optional.
- **Trigger.** `cloud/wrangler.toml` has `workers_dev = true` and `BANNED = ""`. `GET https://auros-wishes.aarohkandy.workers.dev/v1/health` returned 200 on 2026-10-23. `BANNED` is a set of per-token tags (`app.js:44,199`), so `*` matches nothing; the plan's "BANNED=* or equivalent" (WP-45) is wrong. `callClaude` sends no `metadata`.
- **Consequence.** Exposure is capped by the $5 daily budget, 3 registrations a day per /24, a 21-bit proof of work, 300-character inputs and a closed JSON envelope; no AurOS user can reach it in v0.1. The realistic worst case is a warned or suspended workspace (which also kills family-cohort Wishes), and the K.2 indemnity falls on the owner personally until an LLC exists. The K.2 claim over a 300-character input to a JSON pipeline is practically theoretical.
- **Agent remedy.** (1) Set `DAILY_BUDGET_USD = "0"` in `cloud/wrangler.toml` now with a comment and a test that every wish and ask returns 503 at that value (this works today: `ledger.admit` refuses). Correct the WP-45 and `CLOUD.md` s5 wording to name `DAILY_BUDGET_USD=0` and disabling workers.dev, not `BANNED=*`. (2) A `KILL` flag or per-route 503 is optional; if added, keep a CI-only bypass because `cloud.yml`'s live smoke test needs `/v1/register`. (3) In the change that enables Wishes: `callClaude` sends `metadata: { user_id: <HMAC(TOKEN_SECRET, "anthropic-uid:" + tid) as hex> }` with a test that it contains no token or request text; PRIVACY 4a gets "a scrambled code that stands for your computer is also sent to Anthropic so it can stop misuse; it is not your name" (only true once the code does it, and it is a persistent pseudonym, so word it for UK/EU readers). (4) A one-line test that a refusal `stop_reason` gives `cannot`/`other` with no retry (the code already parses only `end_turn`; `adversarial.test.js` lines 37-38 already feed `refusal`).
- **Owner remedy.** Install the workspace-scoped Anthropic key with its monthly limit (`OWNER-SETUP.md` 7a, about $50; the org-level key may be refused), and keep the Console organization on the Commercial Terms. Until the Wishes gates pass, leave the budget at 0 or disable workers.dev (free, 2 minutes). Do not enable Wishes beyond family until the LLC owns the Anthropic organization (LIA-6). $0.
- **Skeptics.** Both stand at before-public-launch of Wishes; before-anyone-uses was too high. Working switches existed; only the plan wording was wrong.

#### AI-2. Nothing on the Wishes screens tells her she is talking to an AI
Severity: **later** (gate on `wishes="on"`).

- **Rule.** Anthropic Usage Policy (effective Nov 12, 2026): consumer-facing chatbots and agents must disclose they are AI, at the start of a session or in the product interface; the disclosure need not name Anthropic or Claude (https://www.anthropic.com/legal/aup). EU AI Act Art 50(1) (applies from 2 Aug 2026; not postponed by Regulation (EU) 2026/1744; "obvious from context" exception is narrow for older users) matters only if the EU is served; Art 2(12) does not exempt open source from Art 50. Commercial Terms D.3 (tell Users output may be wrong). Utah Code 13-75 (disclose when asked) unverified.
- **Trigger.** `src/aurshell/wish.c` `CONSENT[]` says only "Anthropic, who make Claude" and the words "AI" and "artificial intelligence" appear nowhere in `wish.c`. The model's text never reaches her (the device writes every sentence; `/v1/ask` returns only a help page id), so "AurOS's AI helper says:" labelling every answer would itself be inaccurate.
- **Agent remedy.** Add as binding requirements to `docs/AI.md` section 4 and WP-47, each with a screenshot test: (a) the consent screen and the Wish and Ask panels carry a permanent line, 20 px or larger: "A computer program that uses artificial intelligence works out what you mean. It is not a person."; button text "Wish (uses AI)" or "Ask the AI" in the first-run state; (b) wait text "The AI is thinking about your wish..."; (c) the Yes-screen footer: "The AI worked this out from what you said. It can get things wrong, so read this carefully before you press Yes." (also satisfies D.3); (d) optional extra: the device answers "are you a person" style phrases with "No. You are talking to a computer program that uses artificial intelligence." (do not count it toward compliance; the label does that job; do not hard-code a vendor name in device copy); (e) the per-language AI term in every panel title that sends text to the cloud (not the literal string "AI"). The TTS reply carries the label once per session.
- **Owner remedy.** Approve the wording. $0.
- **Skeptics.** Both stand; both say later (gate).

#### AI-3. The Wishes consent screen draft is incomplete and in places inaccurate (retention, health, age, emergencies, errors)
Severity: **later** (gate on `wishes="on"`). Overlaps PRI-4; the exact text and changes are in PRI-4. Note: the withdrawn "Washington separate consent" and "every wish is processed without a valid basis" claims were dropped; the proposed text must not tell her "do not put health details" while pill reminders are a shipped example (decide in PRI-5), and the screen is about 230 words, so layer it.

- **Owner remedy.** Choose the name and contact to show (owner's own name and a mailbox); approve wording. $0.
- **Skeptics.** Both stand; later.

#### AI-4. PRIVACY.md contradicts itself about Wishes and omits facts a regulator will check
Severity: **later** (gate on `wishes="on"`).

- **Rule.** UK GDPR Arts 5(1)(a), 13, 44-46 (Anthropic's DPA is incorporated into the Commercial Terms and carries SCC Modules 2 and 3 and the UK Addendum; Cloudflare's DPA has SCCs, the UK Addendum and a DPF clause, https://www.anthropic.com/legal/data-processing-addendum, https://www.cloudflare.com/cloudflare-customer-dpa/); CalOPPA (Bus. & Prof. Code 22575, "commercial" operators collecting PII; weakly applicable); Canada/Australia items unverified or inapplicable.
- **Trigger.** PRIVACY s1 headline vs the carve-out in the next bullet (not a hidden contradiction), s4a "only counts" while `CLOUD.md` s4 shows a 90-day token record and day-scoped HMAC counters keyed to the /24; the Worker reads `cf-connecting-ip`; no 7-year score line; no transfer mechanism; rights not stated. The README, `OWNER-SETUP.md` 7b, `AI.md` change 12 and PRIVACY line 132 all tell the owner to "sign" a DPA that is automatic.
- **Agent remedy.** In 4a add, in order: who is responsible (owner name, contact); the AI and that it is Claude; what is sent, including "your internet address reaches Cloudflare when your computer connects, as with any website; we use a scrambled version of the first part of it only to limit how many wishes a day, and the scrambling key is destroyed after 2 days"; "a random code made on your computer, stored by our service for up to 90 days, so that we can limit wishes fairly"; legal basis (your consent, withdrawable); transfer ("to the United States under Anthropic's and Cloudflare's standard contractual clauses and, for Cloudflare, the Data Privacy Framework"); retention including safety scores up to 7 years; no training; no decisions with legal effect; adults only. Do not add the Anthropic scrambled-user-code sentence unless the code sends a user id (AI-1). In s7 list rights (access, deletion, correction, objection, withdrawal, complaint: ICO, local EU authority) and a 30-day reply. Fix the "sign Anthropic's DPA" lines in the four files to "the DPA is part of the Commercial Terms; keep a dated copy of the accepted version; zero data retention is optional and may be declined". Change `website/index.html:335` and "Nothing about you is collected" and the code-signing page in the same commit that enables Wishes or the ping (PRI-1, PRI-2). Extend the WP-49 lint to the consent string, 30 days, 2 years, 7 years, 18 and the processor names.
- **Owner remedy.** Confirm the Anthropic organization is on the Commercial Terms and save the DPA PDF; save Cloudflare's DPA ($0). Publish only when Wishes is about to be enabled.
- **Skeptics.** Both stand; later.

#### AI-5. TERMS and AI.md call wishes "harmless"; a pill reminder that silently fails is a foreseeable injury
Severity: **later** (gate on `wishes="on"`).

- **Rule.** Liability for AI output stays with the deployer (Moffatt v. Air Canada, 2024 BCCRT 149: small-claims, non-binding in the US); California Civil Code 1714.46 (AB 316, 2026) only removes the "AI acted autonomously" defence and preserves causation and comparative fault; UK CRA s65 needs a trader and a consumer contract; EU PLD only if EU and commercial.
- **Trigger.** `TERMS.md` s5 "can only do a fixed, limited set of harmless things"; `docs/AI.md` s1.3 "Nothing a wish does can hurt her". The design already mitigates: confirm-before-act, device re-renders the summary, quiet hours exempt `time`+`remind`, missed reminders kept 12 hours, never silently switched off.
- **Agent remedy.** TERMS s5: "Wishes uses an AI, which can misunderstand you. Wishes is built to do only a short list of things, each shown to you first, and it cannot delete files, send messages, buy anything or change security settings. It is not a safe way to remember medicine, appointments or anything where missing it could hurt you. Reminders can fail, for example when the computer is off, asleep, in a call or has no battery. Use a phone alarm or a person as well." Rename `AI.md` s1.3 to "Design goal" and delete "Nothing a wish does can hurt her". On the one-time Wishes consent screen, and on the Yes screen of a remind wish: "This is a convenience, not a medical alarm. Keep another way to remember." (not as a permanent banner on every card). Add the banned words safe, harmless, guaranteed, always, never fails for Wishes to `tools/site/check.mjs` (WP-40), a row per Wishes claim in `docs/CLAIMS.md` (WP-41), and keep red-team and eval reports dated under `docs/legal/evidence/`. One end-to-end test that "remind me about my pills at 9" fires at 9, survives sleep and reboot, and that an ambiguous "9" is confirmed on screen with an explicit am or pm before Yes. Repeat the statutory-rights sentence in TERMS s6.
- **Owner remedy.** Decide about the shipped "Take my pills" template: keep it with the caution, or swap for a non-medical example. Telling the family cohort in writing that Wishes is experimental. E&O quote and LLC are in LIA-6 and LIA-7. $0.
- **Skeptics.** Both stand; later.

#### AI-6. Wishes has no country gate
Severity: **later** (gate: before Wishes reaches anyone beyond the owner's family, and certainly any UK user).

- **Rule.** EU AI Act Art 2(1)(c) and Recital 22 (output "intended to be used in the Union"); GDPR Art 3(2) needs targeting (Recital 23); UK GDPR Art 27 is the live hook for a UK-targeted service. Fine tiers in the finding were wrong.
- **Trigger.** Nothing enforces decision 21: `cloud/src/app.js` `/v1/register` accepts any network.
- **Agent remedy.** `ALLOW_COUNTRIES = "US,CA,AU,GB"` (or US only for the first cohort) in `cloud/wrangler.toml`; in `/v1/register` compare `request.cf?.country` and return 403 `{error:'region',text:'Wishes is not available in your country yet.'}`, with a test; leave `/v1/forget` open; `aurwish` maps 403 to its existing "not available" state with the VPN/traveller caveat in the text. Terms and Privacy: "Wishes is offered to people in the United States, United Kingdom, Canada and Australia." Write `docs/legal/COUNTRIES.md` only for countries actually opened. Do not copy "UK: ICO registration is not needed for a non-UK controller" (unverified). Leave GB out of the list until PRI-6 is settled.
- **Owner remedy.** Decide the first-cohort countries (US only recommended). $0, or a UK representative (PRI-6).
- **Skeptics.** Both stand; later.

#### AI-7. The safety evidence the plan promises (400-utterance eval, sensitive set, DPIA) does not exist yet, and Ask AurOS must stay a non-chat feature
Severity: **later** (split verdict; gate: before Wishes is enabled for any UK/EU user, and as part of WP-46/WP-49).

- **Rule.** UK/EU GDPR Art 35 (ICO treats innovative technology plus elderly people as a DPIA trigger; required before processing). California SB 243 (companion chatbots) does not apply to a stateless closed-vocabulary classifier whose `/v1/ask` returns only `help_page_id`; the Utah/NY claims were unverified or inapplicable.
- **Trigger.** WP-46 (`tools/wisheval/`) and WP-49 (`docs/DPIA-WISHES.md`) are absent from main because they are planned gates. `cloud/test/adversarial.js` has scam, injection and a Hindi pill reminder but no loneliness or self-harm cases.
- **Agent remedy.** Create `docs/DPIA-WISHES.md` from the ICO template (purpose, data map from `CLOUD.md` s4, necessity, risks, mitigations, residual risk, review date). Build `tools/wisheval/` with at least 60 cases (scams, health, money, loneliness, self-harm). Add a distinct `distress` refusal value to the `missing` enum in `cloud/src/validate.js` (do not overload `private`) so the device shows a fixed, locally written line with a helpline by setup country (988 US, Samaritans 116 123 UK, 988 Canada, Lifeline 13 11 14 Australia; give them an owner for upkeep). A CI lock that no new displayed free-text field appears in `ASK_OUTPUT`/`WISH_OUTPUT`, redefined as "no new displayable-prose field" (`wish_json` already carries `show_message`/`remind` text under the `badText` filter). Record the date and the "not a product serving minors" reasoning when re-reading the Usage Policy. Save dated reports in `docs/legal/evidence/`.
- **Owner remedy.** Review and sign the DPIA (internal accountability, no filing). $0.
- **Skeptics.** One refuted (SB 243 and Anthropic-policy hooks misapplied), one kept the DPIA kernel. Later.

#### AI-8. Paid Wishes or paid human help changes the AI analysis
Severity: **before-charging-money**.

- **Rule.** EU PLD recital 14 (price or personal data makes software commercial), conditional on EU reach; UK CRA 2015 digital content and ACL guarantees once money is taken; Anthropic Commercial Terms D.3, D.4(a) (no reselling the Services: shape the paid quota as a product), L.2; ROSCA only if it auto-renews.
- **Trigger.** Decision 12 and v1.0; TERMS s1 "nothing to pay, now or later"; `website/index.html` "no subscription".
- **Agent remedy.** `docs/legal/PAID-TERMS.md` (separate from TERMS): what is sold (more wishes a day, not accuracy); "the AI can misunderstand; a wish that you did not approve is not charged" (quota counted only for wishes that reached her Yes screen); refund of the unused period on request; one-off or prepaid purchases only (no auto-renewal until a checkout flow with clear price, renewal date and one-click cancel exists); price in local currency including tax. A separate paid pool funded by prepaid credit (the $5 daily pools cap free spend and do not tie spend to revenue; paying users must not hit "resting"). Anonymous tokens plus purchases change the privacy model, so update PRIVACY first. Create (not "extend") `docs/legal/ANTHROPIC-CHECKLIST.md`: "pricing change reviewed against Commercial Terms D.3, D.4(a) and the Usage Policy". Reword TERMS s1 and the site first (LIA-8).
- **Owner remedy.** Use a merchant of record only after confirming it covers the products (BIZ-8); the LLC and insurance in force; a one-hour self-checklist plus the merchant's standard consumer terms (a lawyer is optional under D15).
- **Skeptics.** Both stand; before-charging-money.

### 2.7 Web and access

#### WEB-1. No Terms, Disclaimer or "not affiliated" line on the site, no assent in the installer, and the lawyer-approval gate has no way to close
Severity: **before-public-launch**. Overlaps LIA-1, TM-3 and LIC-2.

- **Rule.** As LIA-1 (Specht, Nguyen), New Kids / Tabari for marks. UK CRA s65 and UCTA do not reach a free hobby project. The EU PLD point is conditional (LIA-4).
- **Trigger.** `docs/legal/README.md` says the drafts "must not" be published until a lawyer approves; D15 says no lawyer. `PLAN-END-TO-END.md` row 26 and WP-37 still say "approved by a lawyer"; WP-85 keeps downloads off until every `LAUNCH-READINESS` row is green, so the gate can never go green. `tools/site/check.mjs` checks only that `advanced.html` is linked.
- **Agent remedy.** Replace the lawyer gate in `docs/legal/README.md`, the DRAFT banners, `PLAN-END-TO-END.md` row 26/WP-37 and `LAUNCH-READINESS.md` with: "Reviewed under decision D15 by an adversarial agent review on [date]; not reviewed by a lawyer." Publish `terms.html` and `disclaimer.html` (LIA-1) and the not-affiliated footer (TM-3). Terms fixes: s1 "AurOS itself is free. We may offer optional paid services later, and we will say so clearly before you pay for anything."; s6/7 keep the "nothing limits rights you have by law" sentence and name death/personal injury and fraud; s12 governing law of the owner's US state with "consumers keep the protection of the mandatory law of the country where they live" (drop for a US-only launch if confusing); s2 age: "18 or over, or with an adult's help"; fill the contact address. The installer checkbox is satisfied by the sign-in-wrap line in LIA-1 (an "I agree to the Terms" link; Privacy is a notice, not something consented to). Check.mjs failure when a footer page lacks the Terms, Privacy and not-affiliated links (skip stubs). The LLC does not need to exist for the notice, but the name is filled once it does.
- **Owner remedy.** Fill in legal name, governing-law state, contact address and age rule. Sign a one-line D15 note in `docs/handoff` that the documents were agent-reviewed only. $0. A one-hour fixed-fee review of TERMS s6-7 is optional and outside D15.
- **Skeptics.** Both stand; before-public-launch.

#### WEB-2. Web accessibility exposure (ADA Title III, UK Equality Act, Australian DDA)
Severity: **later** (re-rate to before-charging-money if money changes hands).

- **Rule.** ADA Title III (nexus to a physical place in the Ninth Circuit; Robles v. Domino's); Unruh Act requires a "business establishment"; Equality Act 2010 s29; DDA 1992. The DOJ Title II dates (26 Apr 2027 and 26 Apr 2028, interim final rule 20 Apr 2026) bind governments only. The 3,117-suit figure is a vendor blog count and covers e-commerce.
- **Trigger.** `website/**` is strong (Atkinson Hyperlegible Next, 21 px base, 4 px focus ring, skip link, reduced motion, `role="img"` labels, 4.5:1 contrast and 44 px target gates). Gaps: no axe/pa11y WCAG 2.2 test, no `forced-colors` styles in `site.css` (the primary `button.get` has `border: 0`), the six redirect stubs share one generic `<title>AurOS</title>` (they already have `content="0"` meta refresh and a visible "This page has moved" link), no accessibility statement. `windows.yml` already asserts the installer exposes UI Automation names to a screen reader.
- **Agent remedy.** (1) axe-core (pinned version) with WCAG 2.2 AA tags in the pages workflow; (2) `@media (forced-colors: active)` rules (a transparent border on `button.get`, focus ring, drawn dialogs); (3) distinct titles on the stubs; (4) `accessibility.html`: "We aim for WCAG 2.2 level AA. What AurOS cannot do yet: it cannot read the screen aloud yet, so a person who needs a screen reader should not install it yet. If you cannot use any part of this site or AurOS Setup, write to [contact] and we will reply as soon as we can, usually within a week." (not a "5 working days" promise one person cannot keep); date it and list the last test; (5) one manual NVDA/Narrator, 200% zoom and 400% reflow pass logged in `docs/research/`; (6) a footer link. The mailbox is tracked under PRI-7 / WEB-1.
- **Owner remedy.** A monitored contact mailbox (free with Cloudflare Email Routing; domain about $10-15 a year). Optional paid WCAG audit (roughly $1,500-5,000) only once money is taken. Nothing is legally filed.
- **Skeptics.** Both stand; one later, one before-charging-money.

#### WEB-3. "Free" promises conflict with the planned paid offers
Severity: **before-public-launch** for the wording only (split verdict); sales and checkout items are before-charging-money (LIA-8).

- **Rule.** 16 CFR 251 is an advisory FTC Guide about "free" items tied to a purchase; AurOS is unconditionally free, so the hook is FTC Act s5 and state UDAP on a forward-looking promise. GitHub Pages terms bar sites "primarily directed at facilitating commercial transactions" or SaaS; a link out to a hosted checkout is probably fine. Stripe Tax is not a merchant of record.
- **Trigger.** `TERMS.md` s1 "There is nothing to pay, now or later" (draft, unpublished), s5 "The service is free"; site "Is it free? Yes. No ads and no subscription." (true today); decision 12 recommends "AurOS itself stays free".
- **Agent remedy.** One edit, in the same change that removes the DRAFT banners: TERMS s1 "AurOS itself is free. Some extras may cost money later, and we will say so before you pay." and s5 "Wishes is free for now". Record in the claims table that "free" means the OS only. When the first price exists: separate pricing page and order flow off GitHub Pages (hosted checkout), terms-of-sale (`docs/legal/SALE.md`), unsubscribe and postal address in any promotional email, and remove "no subscription" in the same commit.
- **Owner remedy.** LLC and insurance before the first payment (LIA-6, LIA-7); a hosted checkout. $0 to open most accounts; fees about 2.9% + $0.30 (direct card) or about 5% + $0.50 (merchant of record), unverified.
- **Skeptics.** One refuted as a legal risk (later); one kept the one-line TERMS fix (before-public-launch).

#### WEB-4. Product-level accessibility and market access (EU Accessibility Act, CVAA, Canada, Australia)
Severity: **later** (split verdict).

- **Rule.** EAA, Directive (EU) 2019/882, Art 2(1)(a) covers consumer operating systems; Art 3(15) "making available" means in the course of a commercial activity, free or paid; the microenterprise exemption is for services only. CVAA (47 U.S.C. 617; 47 CFR 14) covers advanced communications services; AurOS provides none itself and 617(h) allows waivers for multipurpose equipment. Equality Act s29 is the UK analogue (the EAA does not apply in Great Britain).
- **Trigger.** English-first, no EU marketing; the Flathub Chromium call browser is installed on demand, not bundled; Orca/AT-SPI and an EN 301 549 statement are WP-66/WP-67. The site already says "AurOS can't read the screen aloud yet".
- **Agent remedy.** A three-line accessibility scope note in `docs/legal/README.md`: no EU-targeted marketing, EU sale or EU pre-install until WP-67 passes; US/UK paid offers re-checked against the CVAA multipurpose and achievability points at that time; a footer line "AurOS is offered in English-speaking countries for now". `docs/ACCESSIBILITY-CONFORMANCE.md` stub (EN 301 549 / WCAG 2.1 AA table). Same disclosure on the wizard's first page. Gate: "no EU-directed marketing, no sale to EU consumers (billing country, IP, currency)" not "no paid offers".
- **Owner remedy.** Decide the EU is out of scope until WP-67 is done ($0). If selling pre-installed computers in the EU, treat yourself as manufacturer/importer for EAA purposes; a scoping call about $300-600.
- **Skeptics.** One refuted the headline, one kept it conditional. Later.

### 2.8 Business

#### BIZ-1. No legal entity: the person shipping a disk-resizing installer is personally the defendant
Severity: **before-public-launch**. Same remedy as LIA-6 (the LLC steps, IP assignment, moving accounts, `SUPPORT.md` fix, LAUNCH-READINESS row). Corrections: the LLC does not cover the owner's own negligence (insurance, LIA-7, matters as much); SignPath needs no entity; `OWNER-SETUP.md` lacks an LLC entry because WP-91 has not landed (WP-91 is a dated owner track). Skeptics: both stand, downgraded from before-anyone-uses.

#### BIZ-2. State choice, EIN, bank account
Severity: **before-public-launch** (the LLC and EIN); the bank account and card separation bite at before-charging-money.

- **Rule.** Form in the state where you live; a foreign LLC registers and pays in both. EIN free, disregarded entity reported on Schedule C. FinCEN BOI is no longer required for US companies.
- **Trigger.** No state, entity or bank is named in `OWNER-SETUP.md` or `docs/legal`. Today's bills are near $0 (the $100-155 a month figure in `CLOUD.md` is the cap, not expected spend; Wishes is out of v0.1), so commingling cannot occur until the LLC exists.
- **Agent remedy.** An `OWNER-SETUP.md` section "Entity, EIN, bank" with the order of operations: check the name on the Secretary of State site and USPTO Trademark Search (not TESS, retired 2023); file the Articles; EIN at irs.gov; operating agreement; IP assignment; capital contribution; open the bank account; move every vendor bill onto an LLC card; accounts under the LLC. A dated item in `OWNER-SETUP.md` for annual report and franchise tax once the state is known (Texas still files a no-fee Public Information Report). The expense list: Anthropic, Cloudflare Workers and domain, SignPath (free). A "Who we are" page so a registered-agent address can be substituted for a home address. Per-state cost table, not one range.
- **Owner remedy.** Home-state LLC. Budget $100 to $500 first year plus $0 to $800 a year after (California about $890 in year one; New York can add $1,000+ publication; Massachusetts $500 + $500). EIN $0. Business checking $0-15 a month. Registered agent $50-300 a year optional. A fiscal sponsor is an alternative for donations only.
- **Skeptics.** Both stand at before-public-launch; before-anyone-uses was too high.

#### BIZ-3. Chain of title: "AurOS contributors" holds the copyright, and much of the code was written by AI agents
Severity: **later**. Overlaps LIC-11 (same remedy).

- **Rule.** 17 U.S.C. 204(a) (signed writing for a transfer); US Copyright Office AI Part 2 report; MIT lets anyone including the owner sell and sublicense, so copyright is largely irrelevant to a planned paid layer (services). Anthropic's Consumer Terms s4 assign output rights to the user.
- **Trigger.** `LICENSE` line 3; about 741 commits on main (369 under the owner's name, 242 Claude, 129 "AurOS install engine"); 736 carry a Co-Authored-By trailer; no outside contributors.
- **Agent remedy.** Template `docs/legal/templates/IP-ASSIGNMENT.md` (a one-page assignment from Aaroh to the LLC covering existing and future AurOS code, docs, brand, domain, keys), blank signature blocks. `docs/legal/PROVENANCE.md` (do not claim a PR review trail). `CONTRIBUTING.md` that outside code is not accepted yet (LIC-11). The LICENSE line changes only after the assignment is signed. The `.rc` `LegalCopyright` string and the CompanyName gate in `windows.yml` (it requires "AurOS") change together. No per-file licence-header CI.
- **Owner remedy.** After the LLC exists, sign and date the assignment ($0). Avoid "proprietary" or "exclusive" on the website; say "free software, MIT licence".
- **Skeptics.** Both stand narrowly; later.

#### BIZ-4. Contributor licensing: public repo with no DCO or CLA, and a paid tier planned
Severity: **later** (split verdict). Same remedy as LIC-11: a short `CONTRIBUTING.md` (outside code not accepted yet; if accepted, MIT and DCO sign-off). Keep any proprietary paid layer (billing, quota logic, support tooling) in a separate private repository owned by the LLC, never in the signed MIT tree (SignPath forbids proprietary components and commercial dual-licensing; this belongs at before-charging-money). No CLA, no DCO CI gate that would break the direct-push workflow, no branch protection requiring PR review (outsiders cannot push to main anyway; OWNER-SETUP step 5 leaves PRs unrequired). Skeptics: one refuted (MIT already permits sublicensing and sale), one kept a reduced form.

#### BIZ-5. GitHub is a single point of failure for code, releases, website, update channel, secrets and signing
Severity: **before-public-launch** (the owned-domain step must precede the first real install). Split verdict.

- **Rule.** GitHub ToS M.3 lets it suspend an account; a repository transfer redirects git and web links but does not redirect GitHub Pages sites, and creating a new repo at the old name deletes the redirects (https://docs.github.com/en/repositories/creating-and-managing-repositories/transferring-a-repository). This is a business-continuity risk, not a legal violation. A GitHub organisation needs a personal-account owner; it need not be owned by an LLC.
- **Trigger.** The repo already moved once (`docs/MOVING.md`). Most of the remedy exists: `docs/ops/DOMAIN.md` (WP-15) and the `AUROS_DOMAIN` variable (tools/repo.sh) make the image URL, apt source and website use `get.`/`updates.`/`www.<domain>`; `tools/ops/addrtest.py` fails the build if `github.com`, `github.io` or `workers.dev` is left in the image, staging initramfs or exe; `tools/repotest.sh` check 2b already enforces no `github.io` link when stamped; `INCIDENTS.md` 1c covers GitHub down; the apt private key backup is documented (`OWNER-SETUP.md` line 144, `UPDATES.md` line 160).
- **Agent remedy.** `docs/ops/CONTINUITY.md` (where each secret lives, how to rotate it, the "GitHub account gone" re-point procedure); the WP-60 scheduled mirror to a second host (R2 or a second git remote); a launch-readiness row "AUROS_DOMAIN set and `addrtest.py` green on the shipped build". A dead-man fallback in WP-75: if the signed snapshot file is older than N days, fall back to unpinned `noble-security`, so a dead channel does not stop all security fixes. Do not fail on `github.com/aarohkandy` in `website/` (the source link must stay). The second responder is halt-only by decision 18; either let them publish a sunset-only notice or state they cannot. A GitHub account successor and emergency access with a waiting period.
- **Owner remedy.** Buy the domain now in your own name (about $10-15 a year; change the registrant when the LLC exists), set `AUROS_DOMAIN`, rebuild and run the edge workflow before the first real install. Before public launch create a free GitHub organisation with a second owner (a 20-minute step), 2FA with two hardware keys or passkeys (about $50 each, $100), and keep a second physical copy of the APT private key separately. Complete `OWNER-SETUP.md` 4a-lock (below, owner checklist).
- **Skeptics.** One refuted as a legal finding, one kept it as an operational gate.

#### BIZ-6. Insurance (business lens)
Severity: **before-public-launch**. Duplicate of LIA-7; same remedy and owner steps. Skeptics: one before-public-launch, one before-charging-money.

#### BIZ-7. Shipped promises say "AurOS will never ask you for money", which collides with the planned paid help
Severity: **before-charging-money**; the TERMS "nothing to pay, now or later" and "no subscription" wording is before-public-launch (WEB-3).

- **Rule.** FTC Act s5 on a promise the plan intends to break. 16 CFR 251 and ROSCA are misapplied (no purchase condition, no subscription). The Telemarketing Sales Rule covers telephone sales; no AurOS phone line exists (decision 6, `SUPPORT.md`), and WP-69's "paid support line" must be reworded to email or in-app chat.
- **Trigger.** `website/index.html` lines 350/357, `advanced.html:262`, `code-signing.html:103`, `guide.html:65`, `docs/SUPPORT.md`, `rootfs/usr/share/auros/help/never-call.html`, TERMS s1 and s9, DISCLAIMER. These are also the project's main anti-scam message.
- **Agent remedy.** Keep the remote-access line absolute: "Nobody from AurOS will ever phone you, email or text you first, ask you to call a number, or connect to your computer." Restrict the money line by channel, not by "unless you asked": "AurOS never asks for payment by phone, email, text, gift card, card number over the phone, or in person. If a paid service ever exists, you can only buy it by pressing Buy inside AurOS, and the price is on screen first." (The proposed "unless YOU asked for help from inside AurOS" gives scammers a script.) Change TERMS s1/s5 per WEB-3. Update `tools/site/check.mjs` and `tools/helptest.py` and the repotest wording checks in the same change set. Design rules for paid offers: start only from a button she presses; no inbound sales calls or email upsells; prepaid credits by default; one-click cancel and refund; price and tax before any card. Any paid remote session is the in-app, red-Stop-bar flow she starts herself, with no AurOS staff connecting.
- **Owner remedy.** Decide whether paid human help is worth the scam-confusion risk; consider selling prepaid Wishes credits and keeping human help free or volunteer-run. No money to spend; decide before the first price is shown.
- **Skeptics.** Both stand at before-charging-money, with the wording at before-public-launch.

#### BIZ-8. Sales tax and VAT on digital services when charging
Severity: **before-charging-money**.

- **Rule.** UK: a non-UK business must register for VAT regardless of turnover when supplying taxable services to the UK (https://www.gov.uk/register-for-vat/non-uk-businesses). US: economic nexus about $100,000 or 200 transactions per state (200 transactions bites first at $5 credits); California and Colorado tax software and SaaS from 1 Jan 2027 (Cozen). EU non-Union OSS, Canada C$30,000 and Australia A$75,000 thresholds were not confirmed (official pages returned 403/404; check them). Remote human help is generally not an electronically supplied service; Wishes credits are.
- **Trigger.** Decision 12; no tax setup exists.
- **Agent remedy.** `docs/PAID.md`: a per-product table (Wishes credits through a merchant of record; human help only if the provider's written approval says so, otherwise a direct Stripe sale with a US-only geoblock; hardware out of scope); the Worker only checks a signed credit balance from the merchant's webhook; price page says "tax included/added at checkout"; no card data and no tax logic in the repo or Worker; a "Price and taxes" terms section saying the merchant is seller of record. A gate: no checkout code ships before the merchant is approved. Paddle's policy rejects "human services not related to a software offering" and lists "technical support for electronic devices" as restricted. Stripe Tax is not a merchant of record; Stripe Managed Payments is, for software and digital goods only and not "tech support" or human-intervention services.
- **Owner remedy.** Form the entity (or confirm the merchant accepts a sole proprietor), then apply to the merchant of record and get written confirmation of covered products. Keep Wishes free until approved. Plan a CPA hour (about $150-400) when revenue starts. Direct selling would add roughly $500-2,000 a year of bookkeeping and state registrations.
- **Skeptics.** Both stand; before-charging-money.

#### BIZ-9. Payment processor terms, 1099-K and donations
Severity: **before-charging-money**.

- **Rule.** Stripe's restricted list prohibits "Suspicious remote technical support" (https://stripe.com/legal/restricted-businesses); transparent, user-initiated paid help is not on it. Paddle restricts technical support. Card processors report on Form 1099-K from the first dollar; third-party payment apps use the $20,000 and 200-transaction test (https://www.irs.gov/businesses/understanding-your-form-1099-k). Under a merchant of record the card-processor 1099-K does not land on the owner.
- **Trigger.** No payment account exists; any donation or sponsor link before an LLC is personal income. TERMS s1's "nothing to pay, now or later" would look inconsistent to a processor reviewer (WEB-3).
- **Agent remedy.** `docs/PAID.md` (see BIZ-8) doubles as the processor application pack: business description, what is sold, refund policy, support contact, sample screens, and "no remote access sold; users start every session". Refund and chargeback handling designed in. No donate/sponsor link or `FUNDING.yml` until the LLC and bank account exist (a policy choice, not a legal requirement). Reconcile "paid support line" with the no-phone rule.
- **Owner remedy.** Apply in the LLC's name with the EIN and business bank account; fees about 3% to 6% per transaction. Keep a second processor in reserve in case of a freeze. Do not take money into a personal PayPal or Venmo.
- **Skeptics.** Both stand; before-charging-money.

#### BIZ-10. Going beyond the US: the EU CRA and Product Liability Directive turn on "commercial activity"
Severity: **before-charging-money**. Duplicate of SL-1, SL-2 and LIA-4.

- Corrections: the README has no EU-expansion text; "the private-repo rule above" does not exist (but see BIZ-4); a "first EU donation tier" is not a trigger (voluntary donations are not commercial); the real reopen triggers are a first paid Wishes quota, a first image or hardware sale, a donation required for updates, or Wishes collecting personal data beyond need. Separating paid features from the free image is infeasible while `aurwish` ships in the image; the fence is a Worker country check on paid Wishes and `/v1/register`, an "offered in US/UK/CA/AU" statement on the download page and TERMS, English only, USD/GBP pricing, and no EU-language pages. Ireland and Malta are English-speaking EU states, so "English-speaking" does not exclude the EU. `docs/EU.md` with the reopen triggers. Fill the SECURITY.md contact placeholder before any EU exposure (CRA coordinated disclosure needs a contact point). Owner: no spend now; a specialist consult (estimate $1,000-3,000, unverified) before opening the EU.

---

## 3. Owner's legal checklist (owner-only actions, in order)

Costs are estimates from the review; items marked (unverified) were not confirmed. "Gate" says what the action unblocks.

| # | Action | Cost | Time | Gate |
|---|--------|------|------|------|
| 1 | Do not link, announce or promote the prerelease `v0.1.0-618726b` until the sources release exists (leave it up; CI depends on its URLs). | $0 | 1 min | before anyone uses |
| 2 | Create a monitored contact mailbox (a free address now; a mailbox on the owned domain after step 6). Give it to the agents for the source offer, SECURITY.md and the policies. | $0 to about $12/yr | 30 min | before anyone uses |
| 3 | Write one line in `docs/OWNER-SETUP.md`: `sources-*` releases are never deleted (at least 3 years after the matching binary is withdrawn). Optionally upload a copy of each to the Internet Archive. | $0 | 5 min | before anyone uses |
| 4 | Install the workspace-scoped Anthropic key with a monthly limit (about $50, `OWNER-SETUP.md` 7a); until the Wishes gates pass, leave the Worker at `DAILY_BUDGET_USD = "0"` or disable workers.dev in Cloudflare. | $0 | 5 min | before anyone uses |
| 5 | In GitHub: switch on Settings, private vulnerability reporting (the toggle is under Advanced Security in current docs); enable Pages with Source: GitHub Actions; complete `OWNER-SETUP.md` 4a-lock (add `environment: apt-signing` with required reviewer to the aptrepo publish job; the claim that this is done is false on main). Security-stream item, not a legal gate. | $0 | 15 min | before public launch |
| 6 | Buy the project domain in your own name now (change the registrant when the LLC exists), set the repo variable `AUROS_DOMAIN`, rebuild, run the edge workflow. Do it before the first real install. | about $10-15/yr | 1 hour | before public launch |
| 7 | Approve the wording the agents propose: licences page and footer, Terms and Disclaimer pages, the Ready-page acceptance and risk sentences, the "AurOS itself is free" wording, the not-affiliated footer. | $0 | 1-2 hours | before public launch |
| 8 | Supply the facts for the terms and policies: legal name (or LLC name once formed), governing-law state, contact address (email or PO box; a home address is not required), the age line ("18 or over, or with a parent or guardian's permission"). | $0 | 30 min | before public launch |
| 9 | Sign a one-line note in `docs/handoff` that the legal documents were agent-reviewed only (decision D15). | $0 | 5 min | before public launch |
| 10 | Run the AurOS name knockout search (USPTO Trademark Search, TMview, WIPO Global Brand Database, handle checks; TSDR status for AUROS GROUP and AUROS360) and paste results into `docs/legal/NAME-CLEARANCE.md`. If anything blocks, rename now. | $0 | 1-2 hours | before public launch |
| 11 | Form a single-member LLC in your home state, get the EIN, sign the one-page operating agreement, open a business bank account, sign the IP assignment to the LLC, capitalise it. Then move accounts and fill the LLC name into the documents. If under 18, tell the agents first. | filing $35-$500 once; recurring varies (CA $800/yr, NY publication $1,000+, MA $500/yr); year one $200-$1,500 typical; EIN $0; bank $0-15/mo; registered agent optional $50-300/yr | 1-3 days plus a week of follow-up | before public launch (hard gate before charging money and before Wishes beyond family) |
| 12 | Create a free GitHub organisation, add a second owner (the second responder), transfer the repo with the domain, enable 2FA with two hardware keys or passkeys, keep a second physical copy of the APT private key. | free org; keys about $100 for two (estimate) | 1 hour | before public launch |
| 13 | Get 3 quotes for tech E&O plus cyber ($1M per claim, as the LLC, with an explicit statement that you ship a disk-repartitioning installer). Ask the question list in `docs/legal/INSURANCE-QUESTIONS.md`. Bind by wave 2 and before any paid offer. Notify the insurer before paying any claimant. | about $1,000-$3,500/yr (median-based estimate; may be declined) | 2-4 hours plus broker time | before public launch (quotes); wave 2 (binding) |
| 14 | Decide a goodwill ceiling for a data-loss incident (suggested reserve $1,000-5,000, a judgement). | $0 until used | 15 min | before public launch |
| 15 | Decide retention for support email and reports (12 months suggested); turn on 2-step verification (a security key) on the support mailbox, Cloudflare and GitHub; set the mailbox purge (Apps Script trigger or a calendar reminder). | $0 to about $100 | 1 hour | before public launch |
| 16 | Decide the wave plan: US only for waves 1-2 (recommended). | $0 | 10 min | before public launch |
| 17 | Optional courtesy emails (not launch gates): the informational note to Canonical through https://canonical.com/legal/terms-and-policies/contact-us (frame as notice, not a request); trademark-permissions@mozilla.com describing the Firefox policies. File replies in `docs/legal/`. Mozilla's agreement becomes mandatory before any paid bundle that ships Firefox. | $0 | 1 hour; replies may take weeks | later |
| 18 | Apply to SignPath Foundation at https://signpath.org/apply (MFA on GitHub and SignPath, reviewer roles named; a brand-new single-maintainer project may be refused for reputation). Not a legal gate; the unsigned path is documented. | $0 | 1 hour; weeks to decide | later |
| 19 | Before Wishes is enabled for anyone beyond you: confirm the Anthropic organization is on the Commercial Terms and save the DPA PDF; save Cloudflare's DPA; decide on the pill-reminder examples; decide the first-cohort countries (US only recommended); if the UK stays on the list, run the free ICO self-assessment (Tier 1 GBP 52 a year if it says pay) and decide on a UK Art 27 representative (estimate GBP 100-500 a year, unverified) or leave GB off; tell the family cohort in writing that Wishes is experimental. | $0 to about $600/yr | 1-2 hours | Wishes gate |
| 20 | Before charging money (including donations, paid help, paid Wishes quota): the LLC, bank account and insurance in force; choose a merchant of record (Paddle, Lemon Squeezy or Stripe Managed Payments) and get written confirmation of which products it covers; if human help is refused, use a direct processor and plan sales-tax handling; one CPA hour ($150-400); move the site off GitHub Pages; open the payment account in the LLC's name; no personal PayPal or Venmo; no donate link before then; decide whether paid remote human help is worth the scam-confusion cost. | merchant fees about 5% + $0.50 (15% at a $5 sale) or about 3% to 6%; setup low hundreds | days to weeks | before charging money |
| 21 | Before any EU launch or EU-reachable paid offer: a scoping consult and, if needed, an authorised representative; otherwise keep paid offers to the US, UK, Canada and Australia. | consult $1,000-3,000 and representative EUR 500-3,000/yr (unverified) | days | before charging money (EU) |
| 22 | If AurOS-authored non-standard cryptography ever ships (D9), send the free BIS/ENC email notification before release; if selling pre-installed PCs outside the US, self-classify under 5D992.c. | $0 | 1 hour | later |
| 23 | Before merging the first outside pull request: adopt a DCO (not a CLA) and add `CONTRIBUTING.md`. If a closed paid layer is ever built, keep it in a separate private repo owned by the LLC. | $0 | 30 min | later |
| 24 | Optional: a paid WCAG audit ($1,500-5,000) only once money is taken; a one-hour fixed-fee lawyer review of TERMS s6-7 (outside D15, optional). | optional | hours | later |

---

## 4. Agent work packages (grouped by stream)

### 4.1 LEGAL (docs/legal/**, notices, SBOM and source-offer tooling)

1. `docs/legal/README.md`: replace the lawyer gate with the D15 agent-review gate and update the plan rows (WEB-1); add the accessibility scope note (WEB-4); add the state AI-law one-line row and the monetisation gate pointer (AI notes; LIC-10).
2. `docs/legal/TERMS.md` edits (one pass, with the DRAFT banners removed, brackets resolved): LIC-4 items a-f (programs not covered, licences prevail, firmware sentence, stop does not end the source offer), LIA-1 items 4-6 (helper authority, capitalised disclaimer with consumer-rights and carve-outs, changes apply to later installs), WEB-3 and BIZ-7 wording ("AurOS itself is free", by-channel money line, absolute no-remote-access line), LIA-3 territory sentence, AI-5 Wishes paragraph, LIC-10 patents sentence, TM-3 non-affiliation list, SL-3 export sentence.
3. `docs/legal/DISCLAIMER.md`, `PRIVACY.md` (s1, s2 light/dark sentence, s3a, s4 table with Cloudflare, s4a, s5 retention, s6 "AurOS is for adults and is not aimed at children", s7 rights paragraph, DPA wording), kept in step with the website (PRI-1, PRI-2, PRI-7, AI-4, PRI-6).
4. New documents: `COMPLIANCE.md` (this file), `NAME-CLEARANCE.md` (TM-4), `ENTITY-CHECKLIST.md` (LIA-6), `INSURANCE-QUESTIONS.md` and a private `INSURANCE-BRIEF.md` (LIA-7), `EXPORT.md` (SL-3), `MOZILLA.md` (LIC-6), `CANONICAL-LETTER.md` as an informational notice (LIC-5), `PAID-OFFERS.md`, `PAID-TERMS.md`, `SALES-TERMS-OUTLINE.md`, `ANTHROPIC-CHECKLIST.md` (create, not extend), `MONETISATION-GATE.md` (LIA-8, AI-8, LIC-10), `COUNTRIES.md` (only when a second country opens), `docs/PAID.md` (BIZ-8, BIZ-9), `docs/security/CRA.md` decision record with the monetisation rule (SL-1, SL-2), `docs/EU.md` reopen triggers (BIZ-10), `templates/IP-ASSIGNMENT.md` and `PROVENANCE.md` (BIZ-3, LIC-11), `docs/DPIA-WISHES.md` and `docs/legal/evidence/` (AI-7), a health-data policy page text for `website/health-data-privacy.html` (PRI-5), `docs/ACCESSIBILITY-CONFORMANCE.md` (WEB-4), `docs/ops/CONTINUITY.md` and `docs/ops/SUPPORT-OPS.md` (BIZ-5, PRI-7), `CONTRIBUTING.md` (LIC-11, BIZ-4).
5. `build/sources` (LIC-1 items 1-4, 7) plus `STAGING-CONTENTS.txt`, `docs/RELINK.md`, the offer text template and `/usr/share/doc/auros/SOURCE-OFFER`, `THIRD-PARTY-NOTICES.md` / NOTICES (LIC-3, LIC-8: Wayland MIT holders, mingw-w64 runtime text, Unicode V3, OFL, DejaVu, GPL-2.0, GPL-3.0, LGPL-2.1, MPL-2.0, glibc BSD notices), the `NOTICE` files for `tools/scamtest` and `tools/fuzz/corpus/font` (LIC-9), the Unicode notice header on `windowsZones.tab` (LIC-8).
6. SBOM per release (WP-68, SL-1) and a build step recording codecs via `mpv --vd=help --ad=help` / `dpkg -l` (LIC-10).
7. `docs/ops/INCIDENTS.md` playbook 5 addendum (LIA-5) and the PRIVACY s5 sentence; a possible-personal-data branch and public-notice template in INCIDENTS (nothing is held today, so this is later).
8. Update `docs/CLOUD.md` s4 consent text, `docs/AI.md` (s1.3 renamed "Design goal", s4 AI-label requirements, s1.4/s12 DPA wording), and the WP-45 text (`DAILY_BUDGET_USD=0`, not `BANNED=*`).

### 4.2 D (website)

1. `website/licences.html` (LIC-2) and a Licences link under both Get AurOS buttons; footer replacement on index, guide, advanced, status, code-signing (LIC-2, PRI-1, TM-3 combined into one footer: licences sentence, no cookies/no ads/GitHub logs sentence, Terms, Disclaimer, Privacy, Code signing policy, Accessibility links, non-affiliation paragraph).
2. `website/terms.html`, `disclaimer.html`, `privacy.html` (from the reviewed drafts), `accessibility.html` (WEB-2), `health-data-privacy.html` only if cloud health text is allowed (PRI-5).
3. Copy edits: `index.html:335` ("No account and no ads. The website and the download are hosted by GitHub..."); `index.html:345` and the "AurOS is new" row ("untouched" to "kept"; "kept just as they were" to "stay on this computer"; add "no change to a drive is risk-free"); "Every version is installed on a virtual test computer running real Windows before it reaches you. It has not yet been tried on a real, physical computer, so keep a copy of the photos you treasure somewhere else too." (data-claim stays tied to `release:tested`); the by-channel never-ask-for-money line and the absolute no-remote-access line on index, guide, advanced, code-signing; the "free" FAQ answer kept present-tense with "no subscription" dropped only when a price exists; `guide.html` steps 1 and 2 add "only if you got this file from this page" and the printed risk sentence in `sheet-warn`; territory sentence ("made for people in the United States for now"); `code-signing.html` and `advanced.html` "is applying" changed to "plans to apply" until the application is filed, and the privacy sentence extended when the ping ships.
4. `tools/site/check.mjs`: fail when a download-link page lacks `licences.html`, or a footer page lacks Terms/Disclaimer/Privacy/not-affiliated links (skip `isStub()`); banned phrases ("Nothing about you is collected", "we collect nothing", "nothing to pay, now or later", Wishes words safe/harmless/guaranteed/always); `data-claim` entries for every remaining privacy and safety sentence and a `docs/CLAIMS.md` ledger (WP-41); forced-colors and axe-core WCAG 2.2 AA (pinned) in the pages workflow; distinct titles on the redirect stubs (WEB-2); a site-wide check that no page requests another server (already present).
5. `site.css`: `@media (forced-colors: active)` rules (WEB-2).
6. Moving off GitHub Pages is required only before the first price (WEB-3); the order flow and pricing page then live on a hosted checkout.

### 4.3 E (release pipeline)

1. `release.yml`: offer text and "Licences and the complete source code for this release: <sources release URL>" in `notes.md`; upload source assets; a `sources-<tag>` release (non-prerelease path; WP-04b prune skips `sources-*`); promote fails unless a verified `sources-<tag>` release exists; add the unsigned-case SignPath/policy link to the notes line; append the job list from `tools/releasegate.py` and the run id to the notes or a small asset (Actions logs expire) (LIC-1, LIC-2).
2. CI job re-downloading the sources release and re-verifying hashes (LIC-1 item 4); CI that no workflow uploads an ISO to Releases (LIC-5).
3. Image/site mark audit and the `profiletest.sh` step (`apt-get install --reinstall base-files`; assert `NAME="AurOS"`, `DISTRIB_ID=AurOS`; `unattended-upgrade --dry-run --debug` lists noble-security and noble-updates) (TM-1, LIC-5); package-name deny list; firmware licence-file presence check in the built rootfs (LIC-7).
4. `tools/manifesttest.sh`: exe NOTICES resource present, `STAGING-CONTENTS.txt` entries, source assets uploaded (LIC-3).
5. `tools/repo.sh` / `tools/repotest.sh`: banned domain labels (ubuntu, canonical, kubuntu, windows, microsoft, linux), `addrtest.py` green on the shipped build as a LAUNCH-READINESS row, an LLC row (owner-attested), a country check in the edge Worker for US-only waves (BIZ-5, LIA-3, LIC-5, LIA-6).
6. WP-60 mirror to a second host; WP-75 dead-man fallback to unpinned `noble-security` (BIZ-5); `aptrepo.yml` publish job with `environment: apt-signing` (owner click in checklist 5).
7. `tools/updatepolicytest.sh`/`updatetest.sh` also run inside the built image (TM-1).

### 4.4 K (help)

1. `rootfs/usr/share/auros/help/licences.html` linked from help index and aurshell Settings > About; `tools/helptest.py` requires it (LIC-2).
2. `rootfs/usr/share/auros/help/never-call.html` and `docs/SUPPORT.md` updated with the by-channel money line and the absolute no-remote-access line; maintainers' notes keep `never-call.html`, `advanced.html` and SUPPORT in step (BIZ-7). SUPPORT.md section 3: say issues are public and permanent, drop "a photo of the screen helps" from the public path, and point to `support@` once it exists; add `.github/ISSUE_TEMPLATE/config.yml` with a public-and-permanent warning and contact links (support-channel residue).
3. A short "how to copy photos to a memory stick" help page behind the Ready-page "Show me how" (LIA-2, optional).
4. `docs/ops/CONTINUITY.md` wording on who can publish a sunset notice (BIZ-5).

### 4.5 C (image)

1. `build/forge`: `dpkg-divert --local --rename` for `/usr/lib/os-release` and `/etc/lsb-release`; write AurOS versions; `UBUNTU_CODENAME=noble` (TM-1); refuse the Firefox tarball route in release builds, and make the tarball block use the rename table (LIC-6); `rootfs/etc/apt/apt.conf.d/53auros-updates` with literal `origin=Ubuntu,archive=noble-updates` / `noble-security` (TM-1); `/usr/share/doc/auros/SOURCE-OFFER`, `THIRD-PARTY-NOTICES.md` and the repo LICENSE in the image (LIC-1, LIC-8); no step may prune `/usr/share/doc` or `/usr/lib/firmware/LICEN*` (LIC-7); an optional `media_codecs` profile switch (LIC-10).
2. `build/staging`: `STAGING-CONTENTS.txt`, host libc6-dev/gcc versions in `build-info.txt`, no firmware (LIC-3).
3. Build step listing installed package versions as the input to `build/sources` (LIC-1).
4. Keep the image free of Ubuntu marks (LIC-5); optionally an Ubuntu-string grep of `/etc/lsb-release`, issue, GRUB menu and help pages.

### 4.6 CL (cloud)

1. `cloud/wrangler.toml`: `DAILY_BUDGET_USD = "0"` now, with a comment; a test that every wish and ask returns 503 at that value; later `ALLOW_COUNTRIES` and the `/v1/register` country check returning 403 `{error:'region',...}` with a test; `/v1/forget` stays open (AI-1, AI-6).
2. `cloud/src/app.js` `callClaude`: `metadata.user_id` (HMAC of `tid`) plus a test, added only in the change that enables Wishes; refusal `stop_reason` test (AI-1).
3. Report/ping Worker (when built): own Worker and `wrangler.toml` (`invocation_logs=false`, `traces=false`, no sampling), no `cf-connecting-ip` reads, address-free log canary, field-whitelist test, identical-pings-indistinguishable test, `/v1/report` storage decision and 90-day TTL if it stores (PRI-3, PRI-7).
4. A distinct `distress` value in the `missing` enum (`cloud/src/validate.js`) with tests, and `tools/wisheval/` (60 sensitive cases) (AI-7); a CI lock against new displayed free-text fields.
5. Add the Worker and its secrets to SECURITY.md scope (SL-2); a Worker-country allow-list as a single variable, not a new IP lookup (PRI-6).
6. A "live token count" line for LAUNCH.md (PRI-9).

### 4.7 W2 (Wishes consent screen and shell)

1. `src/aurshell/wish.c` `CONSENT[]`: the final text in PRI-4/AI-3 (AI line, controller and contact, 30 days, 2 years, 7 years, no training, emergencies, adults, privacy link, equal buttons, no default focus), consent version 1 at launch, layered so it fits large type (PRI-4, AI-2, AI-3).
2. Wish and Ask panel AI line (20 px or larger), "Wish (uses AI)" / "Ask the AI" first-run buttons, "The AI is thinking about your wish..." wait text, the Yes-screen footer, the "convenience, not a medical alarm" line on remind Yes screens, TTS label once per session, optional device answers to "are you a person"; screenshot tests per item (AI-2, AI-5).
3. Route recognised health-word reminders to the local gallery with a no-network-call test; fix the pill-example decision (PRI-5, AI-5).
4. Per-language AI term lint instead of the literal string (AI-2).
5. A "reminder with an ambiguous 9 is confirmed with am or pm" end-to-end test (AI-5).

### 4.8 Installer stream (A2/B; not in the requested list but needed by the findings)

1. `src/aurbridge/wizard.c` Ready page: the sign-in-wrap acceptance line (body size and contrast, not `C_QUIET`), the risk sentence replacing the absolute row, the embedded Terms text opening offline, a Licences button showing the NOTICES resource, journal entry "terms accepted: v1, <timestamp>" (LIA-1, LIA-2, LIC-2, LIC-3); update navtest and the widget contract.
2. `src/aurbridge/aurbridge-info.rc` / `build/aurbridge`: NOTICES (RCDATA 200) and OFL (RCDATA 201) resources; keep `CompanyName "AurOS"` (the `windows.yml` gate); no firmware in the exe (LIC-3).
3. Preflight: a managed-device block (domain-joined via `NetGetJoinInformation`, Entra-joined, or an active MDM enrolment with `EnrollmentState=1` and a non-empty `ProviderID`, not a stale BYOD registration), with a sim test (residue of the refuted ownership-gate finding).
4. Wi-Fi/BitLocker (before `AUROS_FASTPATH` is enabled in a public build): untick the Wi-Fi box by default or footnote the pre-ticked consent; subtitle "Copies this Wi-Fi's password to AurOS on this computer. Not sent anywhere."; delete `wifi.conf` at the "Restart later" path or move the write into `ab_arm()`; correct "paused for one restart" (code uses `-RebootCount 2`, which is two Windows restarts); add `wifitest.sh` cases; fix the `ferry-report` CANNOT row and `FERRY.md` line 343 to "except the network you were using when you installed, if you said Yes"; delete any stale `wifi.conf` at the next Setup start (residue of the refuted Wi-Fi findings).

### 4.9 Residue from refuted findings (small, agent-doable, $0)

- Add "superseded: Ferry never copies saved passwords (see `docs/FERRY.md`)" to `docs/research/migration.md` lines 27 and 78-79; optionally assert Chromium `Login Data` and `Cookies` never appear in the new home in `src/ferry/tests/run.sh`.
- Extend PRIVACY "Your files from Windows" to list Firefox history and sign-in cookies (not saved passwords) when WP-79 ships; resolve the PRIVACY s3 `[CHECK]` log sentence with a fact ("Setup keeps no log file on your computer", or name the file).
- Set `FirefoxHome` Pocket/Stories off in `policies.json` if it fetches recommended stories; decide ODRS in gnome-software; do not add time-server or connectivity-check rows to the host table until SEC-007 or the package ships. Finish WP-38 (`egresstest.sh`, `hosts.txt`) before PRIVACY.md is published; say "mainly connect to these services".
- SECURITY.md: replace the `[OWNER: ...]` placeholder with the real address (or delete the fallback line), add "this is our promise only; we cannot authorise testing of systems we do not own", turn the Worker into stated scope; `pages.yml`: confirm `/.well-known/security.txt` actually deploys (`upload-pages-artifact` excludes dot-directories unless `include-hidden-files: true`); a user-site repo named `aarohkandy.github.io` can serve it at the host root.
- `docs/usability/CONSENT.md`: fill "[six months]" and the contact; add a storage line to `SCRIPT.md` section 9 (encrypted external drive, never the repo, phone auto-upload to cloud off); add the second responder to "Who sees it"; close unrelated email and banking tabs in the pre-session checklist.
- `docs/legal/signpath-application.md` and `website/code-signing.html`: disclose that Setup reads the connected Wi-Fi profile and key (opt-in tick box, documented WLAN API, written only to the installed system, never transmitted) and the BitLocker pause, add a short reputation paragraph (public CI, tests, SHA256SUMS, documented install and undo) and name Put Windows back as the uninstall facility; add the first signed-release checklist line (rewrite the "Publisher: Unknown" pictures after a real-PC test, per `docs/SIGNING.md` section 1). Soften "The signature tells Windows who made it" to say the signer shown is SignPath Foundation and the signature means an automated build from public source, not that the program is safe.
- `docs/legal/README.md` line 4 and "If anything is ever sold" paragraph: before shipping or handing over hardware with AurOS pre-installed, resolve Mozilla's agreement or ship without Firefox, Canonical's licence, a GPLv3 s6 path (MOK enrolment and Secure Boot off), and Intel firmware OEM terms.
- `docs/ops/INCIDENTS.md`: a sunset section (best-effort notice sentence in TERMS s8: "We will try to give at least 90 days' notice, and if we can't, we will say so as soon as we can"); reword TERMS s8 "so your computer stays safe" to "while we keep AurOS going"; add `docs/ops/COSTS.md` note that wave 3-4 should wait for the R2 mirror (WP-60 is in the v0.1 checklist but milestone v0.2) and that GitHub's "no bandwidth limit" is "no published cap; AUP s9 applies" in `docs/RELEASE.md`.
- TERMS s4 "always leaves Windows at least 20 GB free" to "tries to leave Windows at least 20 GB free when there is room, and never less than 8 GB" (matches `PF_WINDOWS_KEEP_BYTES` and `advanced.html`), as a `data-fact`.
- In `docs/legal/DISCLAIMER.md`, drop "many times on test machines" until it is true of real machines.

---

## 5. Appendix: refuted findings (one line each)

Each was challenged twice; both challengers refuted, or the one that kept anything kept only a trivial residue (listed in section 4.9). Do not re-open without new facts.

1. Source retention and reproducibility (snapshot.ubuntu.com as the source): refuted, WP-06 already publishes a `sources-<tag>` release for 3 years or more and snapshot.ubuntu.com is only a rebuild pin; the 3-year term applies to the written-offer routes and GPLv3 6(d) for downloads has no fixed term; residue: the WP-04b prune must skip `sources-*`.
2. SignPath conditions (licences lens): refuted, `website/code-signing.html`, the application packet, `docs/SIGNING.md` and the signing jobs already exist; signing is optional and the embedded boot binaries are allowed upstream components.
3. Charging money flips four licence conditions: refuted, Canonical, Mozilla, Intel and OFL conditions are not price-dependent, GPLv3 s6 is triggered by handing over hardware not by money, and the planned paid offers are services; residue: one hardware-sale caution line in `docs/legal/README.md`.
4. "Like Windows" and "Like a Mac" as feature names: refuted, descriptive nominative use, Zorin ships the same; residue: the missing non-affiliation line is TM-3.
5. Drawn replicas of Windows, Edge, Chrome and SmartScreen dialogs: refuted, they are HTML/SVG drawings with generic icons and short UI strings, not screenshots or logos; Microsoft's screenshot terms do not apply; residue: the optional "these are drawings" caption.
6. Domain choice (Canonical and Microsoft bar their marks in domains): refuted, no domain with a third-party mark is proposed; residue: the banned-label check in `tools/repo.sh` (LIC-5).
7. Signed installer will show "SignPath Foundation" while the site says otherwise: refuted, the site correctly says unsigned today and `docs/SIGNING.md` already ties the rewrite to the first signed build; CompanyName is not the Authenticode publisher and must stay "AurOS".
8. No trademark policy for AurOS itself: refuted, a policy file creates no rights, takedowns of trojaned installers work on malware and phishing grounds, and TERMS s10, WP-65 and WP-94 exist; residue: one name-use rule in TERMS s10.
9. Future "official Ubuntu or Linux Mint beside Windows" advertised by name: refuted, one factual sentence is nominative use and the feature is unbuilt; residue: a pre-ship checklist (plain name as data, no logos, keep the not-affiliated string).
10. Wishes names Anthropic and Claude to users: refuted, a factual processor mention is nominative use and is required in the privacy notice; residue: no Anthropic logo or "Powered by Claude" without approval.
11. No privacy policy published or linked anywhere (privacy lens): refuted, `code-signing.html` already has a Privacy section and CalOPPA and Art 13 are not triggered by a no-collection v0.1; the stale lawyer gate and the placeholder-ridden draft are tracked in WEB-1 and PRI-2.
12. SignPath privacy and "no silent network transfer" conditions: refuted, the rule applies only to data sent to unspecified systems; Setup's only call is the image the user asked for; residue: update `code-signing.html` when the ping ships (PRI-2).
13. Wi-Fi password carry-over (pre-ticked consent, plaintext on the ESP): refuted as a legal risk, the feature is behind `AUROS_FASTPATH` and off in public builds, rollback already deletes `wifi.conf`; residue in 4.8 item 4.
14. Anthropic and Cloudflare processor paperwork (DPA automatic, ZDR not): refuted as framed, the drafts already assume no zero data retention; residue: fix the stale "sign the DPA" lines (AI-4).
15. Wishes `install_id` and token as persistent identifiers, rights handling undefined: refuted, GDPR Art 11 and the design cut the other way and offline Forget leaves only an unlinkable orphan row; residue: one sentence in PRIVACY s7 about offline forgetting.
16. Children and minors / COPPA / no age gate: refuted, AurOS is not child-directed, the Worker keeps no words and Anthropic's minors rules do not apply to a closed-vocabulary classifier; residue: the "adults" line in TERMS s2 and PRIVACY s6.
17. Public GitHub issues as the only support channel: refuted as a legal finding, pages already warn and no statute applies; residue: `ISSUE_TEMPLATE/config.yml`, SUPPORT.md wording and `support@` (4.4).
18. Responsible party is a private individual (home address, personal liability): refuted, CalOPPA and GDPR do not require a postal address; use an email contact and never print a company name that does not exist yet.
19. Local data touched by Setup and Ferry (logs, journal, ESP, bookmarks and history): refuted, no persistent installer log exists and Ferry never copies passwords; residue in 4.9.
20. Third-party "phone-home" table incomplete: refuted as a legal risk, PRIVACY s4 already lists the main hosts and WP-38 builds the test; residue: finish WP-38 before publishing, say "mainly".
21. Screen-recorded usability sessions without a consent form: refuted, `docs/usability/CONSENT.md` exists; residue: fill "[six months]" and the contact, add a storage line.
22. Absolute data-safety promises contradicted by engineering documents: refuted as written, most quoted text is already replaced on main; the surviving wording nits ("untouched", "kept just as they were") are in LIA-2 and 4.2.
23. "Every version is installed on a test computer" unsubstantiated: refuted, the claim is machine-checked against `release.yml`; residue: say "virtual test computer" (4.2).
24. "Nothing to pay, now or later" conflicts with paid offers (liability lens): refuted as a legal risk, the quote is in an unpublished draft; the one-line fix is in WEB-3 and LIA-8.
25. Draft warranty disclaimer over-broad, no savings clause: refuted, s7 already has a savings clause and the UK and Australian rules need a trader; residue: capitalisation and consumer-rights sentence (LIA-1).
26. Instructions that lower her protection (Keep anyway, Run anyway, Smart App Control): refuted, the site does not walk her through switching Smart App Control off and `advanced.html` already warns; residue: signing wording (4.9).
27. Other marketing claims need substantiation ("10 to 25 minutes", "Nothing about you is collected", "Is it safe?"): refuted, the time is now a code-checked 25 to 60 minutes and the claims carry hedges; residue: PRI-1 and the TERMS 20 GB sentence (4.9).
28. Open-ended right to stop AurOS and its updates: refuted as a legal finding, UK PSTI excludes desktop computers and CRA/PLD apply only if commercial; residue: soften TERMS s8 and add a best-effort notice sentence (4.9).
29. Wishes "harmless" creates personal-injury exposure: refuted as a liability finding, only the word "harmless" in an unpublished draft remains; residue folded into AI-5.
30. Update channel gives root to whoever controls the signing secret: refuted as a legal finding; it is the existing security work SEC-004, WP-88 and OWNER-SETUP 4a-lock (checklist item 5).
31. No ownership gate: the installer modifies a PC on the say-so of whoever is at the keyboard: refuted, CFAA and computer-misuse statutes need intent by the actor, the user is the actor and `phase_consent` refuses without the Install press; residue: the managed-device preflight block (4.8 item 3).
32. Wi-Fi key and BitLocker pause (consent text, exposure window): refuted, the fast path uses `-RebootCount 2` with automatic re-enable, the ESP is never BitLocker-encrypted and the feature is off; residue: wording ("one restart") fixed in 4.8 item 4.
33. SECURITY.md contact unfilled, private reporting off, safe-harbour overreach: refuted as a legal risk; residue is the toggle and placeholder in checklist item 5 and 4.9.
34. SignPath terms interaction (org move, publisher name, privacy): refuted, the policy page, roles, privacy sentence and application packet exist and no organisation is required; residue: the Wi-Fi disclosure and reputation paragraph in 4.9.
35. Website coaches users to click through warnings and switch off Smart App Control: refuted, it instructs one-off per-file overrides with conditions and keeps Smart App Control steps on the helper page with a warning box; residue: stronger address line on the guide.
36. Security-claim wording read as absolute promises: refuted, the quoted FAQ text has changed and the cited UK, EU and Australian regimes need a trader; residue: the "untouched" nit (LIA-2).
37. Reading Windows data offline (Ferry docs contradict code on saved passwords; multi-account PCs): refuted, FERRY.md, the code and the tests all say passwords never cross and multi-account selection already refuses to guess; residue in 4.9.
38. No incident-notification or breach plan: refuted, `docs/ops/INCIDENTS.md` exists and breach laws are not triggered by data AurOS does not hold; residue: a short personal-data branch before Wishes.
39. Health content in wishes (ai lens duplicate of PRI-5): refuted as a v0.1 risk, the MHMDA allows collection needed for a requested service; the surviving gate is PRI-5.
40. No age gate (Anthropic, COPPA): refuted, adult-targeted, Wishes is stateless and closed-vocabulary; residue: adults-only wording at the Wishes gate.
41. Owner as Anthropic Customer: Commercial Terms are business-only: refuted, an individual is a valid Customer and A.1 allows powering products for end users; residue: D.3 notice to Users in the Wishes consent text (AI-2).
42. US state AI laws (Colorado, California, Utah, Texas): refuted, none applies because the model's text never reaches her and no consequential decision is made; residue: a one-line summary row and the rule that a Yes press is not qualified human review.
43. Live site makes absolute safety and evidence claims: refuted, the quoted text no longer exists on main; residue in 4.2 (virtual computers, "untouched").
44. "Run anyway" coaching and stale signing text: refuted, `advanced.html` already says Setup is unsigned and applying; residue: guide.html condition phrase and "plans to apply" (4.2).
45. Code signing through SignPath imposes website, privacy and process duties: refuted, `code-signing.html`, the signgate jobs and `docs/SIGNING.md` already implement them; residue: add the policy link to the unsigned notes line.
46. Privacy and cookie statements on the site inaccurate: refuted, ePrivacy does not reach reads that never leave the device and the site sets no cookies; residue: delete the PRIVACY light/dark sentence (4.9).
47. Only support route is a public GitHub issue; no contact or imprint: refuted as a legal finding, UK E-Commerce Regs and German DDG require a remunerated service, CAN-SPAM needs commercial mail; residue: `support@` and the issue template (4.4).
48. GitHub hosting policy (bandwidth, malware heuristics, Pages commercial ban): refuted, GitHub documents no bandwidth limit for Releases and the site has no checkout; residue: move the site off Pages before the first price.
49. Windows, Edge and Chrome dialog replicas and "Like Windows / Like a Mac" without attribution (web lens): refuted for the same reasons as 4 and 5; residue: the TM-3 footer.
50. Key-person risk: refuted as a legal finding, rotation procedures and playbooks exist; residue: a GitHub successor setting, emergency access with a waiting period and a successor clause once the LLC exists.
51. Free hosting capacity and Pages commercial rule: refuted, Releases has no documented bandwidth cap and the site is not commercial; residue: open waves 3 and 4 only once the R2 mirror is first in `pieces.txt`.
52. SignPath eligibility and the paid model: refuted, the terms ban commercial dual-licensing and proprietary components but say nothing about paid services; the reputation check is the real refusal risk; residue: the Wi-Fi and BitLocker disclosure in the application.
53. Anthropic and Cloudflare accounts must be in the LLC's name: refuted, Anthropic does not require an entity and the budget caps limit exposure; residue: move the accounts when the LLC exists (LIA-6).
54. Records: nothing retained deliberately: refuted, git, release notes, hashes and screenshots already record what each release shipped; residue: attach the release gate output and run id to the release notes, a one-paragraph retention note in `docs/RELEASE.md`.
