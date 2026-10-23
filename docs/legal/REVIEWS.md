# The adversarial reviews (decision D15: no lawyer, so every public document is attacked before it is linked)

Prepared by the AurOS agents. Two independent skeptics read the public documents on 23 October 2026, each with a
different lens and with the code in front of them. They were told to find what is false, overclaimed, missing or
unclear, not to praise, and not to edit. This file keeps what they found and what was done, so nobody re-opens a
settled point by accident. A finding marked **NOT DONE** blocks `python3 -I tools/legal/check.py --launch` (the gate
before the public link); nothing is marked that way today except where the owner or another stream holds the work, and
those are marked **OTHER** or **OWNER** with who.

Documents reviewed: `TERMS.md`, `DISCLAIMER.md`, `PRIVACY.md`, `LICENSES.md`, the website footer and front-page
claims, `SOURCES.md`, `build/sources`, `sources.yml`, the written offer, the notices, the trademark use. Round 1 is
below; the rewritten texts (Terms v2, Disclaimer v2, Privacy v2, Licenses v2) are the answer to it. Round 2 (two more skeptics, C and D, on
the rewrites and the tooling) is at the end of this file.

## Skeptic A: honesty and consumer protection (21 findings)

| # | Finding | Answer | Status |
|---|---|---|---|
| A1 | Installer screens carry absolutes and no risk line (`wizard.c` Ready, Welcome, install-stopped; `screen.c` put-back) | Exact replacement strings and the facts behind them are in `docs/handoff/streams/LEGAL.md` (B list, items 1-5, 9). The Terms, Disclaimer and site now say what the Ready page must: the shrink is the one step that cannot be undone half way | **OTHER: stream B / A2** (dated allowance in `check.py` until 2026-11-30; `--launch` needs the risk sentence) |
| A2 | Assent does not exist yet; Terms 4 says "pressing Install means you agree" | True today. The Terms stay (the sentence is what the installer line will mean), and `check.py --launch` refuses a public link until `wizard.c` carries "By pressing Install AurOS you agree to the AurOS Terms" and `RCDATA 202`; the website is not deployed until a `[latest]`. LEGAL.md asks E to run `check.py --launch` in `promote` | **OTHER: B (line), E (gate)** |
| A3 | Licenses page says the texts are inside the exe; they are not | `--launch` now also needs `L"Licenses"` in `wizard.c`; NOTICES.txt travels in every sources release (the first one by `again`); the first prerelease's notes say plainly it predates the notices | **OTHER: B** (button), done (release) |
| A4 | Hero: "you can go back whenever you like" is false | Hero, meta description and closing line now say "made to keep Windows and your files, and to let you go back"; a risk line sits under the button; the FAQ says "That is what it is made for"; `check.py` bans "go back whenever you like" and "your files are safe" | Done (this round) |
| A5 | "Cutting the power at each step ... on pretend disks" overstates | Disclaimer: "just before and just after each step ... cannot test a power cut in the middle of making Windows smaller"; CLAIMS row 29 | Done |
| A6 | `CONTACT_EMAIL` is literal; the front page gives no contact | The footer of every page now says "Write to us: CONTACT_EMAIL"; `set-contact` fills it everywhere; `--launch` refuses until then | **OWNER: a mailbox** |
| A7 | The download starts before she presses Install, and a close leaves it on the drive | Verified in `wizard.c` (`check_passed`, `install_cancel`). Terms 4 and Privacy 3 now say exactly that. The Welcome page and S-22 (the room check ignores this download) are B/A2's | Done (text); **OTHER: B** (Welcome page) |
| A8 | "Empty space ... the screen shows how much" is wrong; Windows is made smaller and its files moved | Terms 4 and the FAQ say so, with 28 GB and 40 GB. `advanced.html` #room still says Setup "shows both numbers" (D's; `page_ready` shows no size): in LEGAL.md | Done; **OTHER: D/B** |
| A9 | Wi-Fi and drive-lock text describes a build we do not ship, and is wrong in detail | Removed from the published texts; `FASTPATH-TEXT.md` has the accurate sentences for the day the default flips | Done |
| A10 | "May cost money later" vs "never ask you for money" | Terms 9 is by channel and drops "money"; the site, help page and SUPPORT.md say "card number, password or gift cards" | Done |
| A11 | "The AurOS team" and "a small team" hide that it is one person | All four documents and SUPPORT.md say one person with AI help, no company | Done |
| A12 | Testing claims: the gate is the base variant only | Disclaimer and the front page: Windows 11, fast drive; Windows 10, slower, locked and small-drive computers are not tested before release; times are estimates | Done |
| A13 | "The answers are always Keep, Run anyway, Yes" trains click-through | Reworded: only if you got the file from this page just now; "if you are not sure where a file came from, stop and ask someone" (`check.mjs` B-03 still passes) | Done |
| A14 | "Is it safe?" implies files stop being kept after Yes; "empty room" wrong | Reworded with the risk sentence | Done |
| A15 | "Collects nothing" vs the emails; Safe Browsing cannot be switched off; "read 'mainly' as meant" is unparseable | Privacy 1, 4 reworded; footer says "this site collects nothing"; the table says the Firefox check is always on and that it comes from the build, not from watching a running computer | Done |
| A16 | Wishes is not in the image; "switched off" and "cannot delete files" are claims about nothing | Removed from Terms and Privacy; `WISHES-TERMS-DRAFT.md` keeps the paragraph for its gate | Done |
| A17 | Missing disclosures: Fast Startup, what stays after "No", unasked updates, second computer, maker's reset and warranty, no copy of the Terms on the computer | Terms 4 and 8 say each; `/usr/share/doc/auros/{TERMS,PRIVACY}` are generated into the image; updates for programs outside Ubuntu's main collection are slower | Done |
| A18 | Plain language: capitals, firmware, patents, export, a garbled carve-out | "Plain version" box at the top of the Terms; patents and export in plain words; the carve-out reordered; the capitals of 6 and 7 stay (the law asks for conspicuous disclaimers) | Done |
| A19 | Terms 10 lacks the Apple sentence | One trademark paragraph, `docs/legal/TRADEMARKS.txt`, copied verbatim into Terms, Disclaimer, Licenses and the footer; `check.py` fails if one differs | Done |
| A20 | "You have them" (UK/EU rights) has no mechanism; 12 months vs "as long as needed" | Privacy 7 and 5 reworded | Done |
| A21 | CLAIMS.md lacks rows | Rows 26-35 added (download, shrink, hazard, power-cut testing, release gate, "No, go back", Wishes absent, fast path absent, copies on the computer, update caveat) | Done; the front page's "if AurOS fails to start, the computer starts Windows" is D's and unproven on real firmware: **OTHER: D** |

## Skeptic B: licenses, source, notices, trademarks (17 findings)

| # | Finding | Answer | Status |
|---|---|---|---|
| B1 | The offer's only fallback is a placeholder; "anyone who has a copy" is narrower than GPLv2 3(b) | The offer now says "open to anyone, whether or not they have a copy of AurOS". The mailbox is the owner's; a mirror of each sources release to Software Heritage or the Internet Archive is listed in `docs/OWNER-SETUP.md` section 9. `check.py --launch` refuses until the mailbox exists | **OWNER: mailbox, optional mirror** |
| B2 | The one public installer and image predate every notice; the notes said otherwise | True. The notes of `v0.1.0-618726b` now say it predates the notices and must not be passed on; NOTICES.txt goes into its sources release; `check.py --launch` needs the exe resources; the next build carries the files (`rootfs/usr/share/doc/auros/`) | Done (notes, release); **OTHER: B** (exe resources) |
| B3 | Nothing enforces a sources release for the next release | `sources.yml` sweeps every three hours: every published AurOS release without sources gets them from the build run named in its notes, and the sweep fails when one older than a day has none. E's wiring into `promote` is still advised | Done; **OTHER: E** (promote) |
| B4 | The statically linked glibc version is not recorded | The sources job reads `host-sources.txt` from the installer artifact when the build wrote it, and says in `SOURCES.txt` when it did not; E's hand-off asks for the line; RELINK.md no longer claims more | Done (disclosure); **OTHER: E/A2** (the record) |
| B5 | Mozilla's policy does not cover the configuration | Confirmed against Mozilla's page. `MOZILLA.md` rewritten; the status upgraded to before-public-launch (recommended); options: ask, change the configuration, or rename | **OWNER: decide** |
| B6 | The published image carries `ubuntu-mono`; "built from" can read as "compiled from" | The sentence is now "AurOS includes software from Ubuntu 24.04 LTS, put together and changed by its maker"; the letter must not be sent until `check.py image` is green; stream C purges `ubuntu-mono` (LEGAL.md) | Done (words); **OTHER: C** |
| B7 | The published sources release misses `shim`, and a remake has gaps | Built-Using support (shim, unicode-data, ...); the `again` path replaces the assets and removes those the new set does not have; the remake was run | Done (see the run in `LEGAL.md`) |
| B8 | `build/sources` drops packages in states other than "installed" | Every state that leaves files counts (with a warning); tests and a mutant | Done |
| B9 | `sources.yml` can publish incomplete or overwrite a published release | "release not found" is the only "not there"; a failed upload fails the job; stale assets removed on `again`; notes refreshed | Done; the "tip commit only" limit of `[sources ...]` is documented |
| B10 | Verification is partly circular | Said plainly in `SOURCES.md` ("Not covered"); the staging check compares the real cpio with NOTICES; an unowned-file scan of the rootfs is open | Said; **OTHER: C/E** (unowned-file scan) |
| B11 | The NOTICES package list is typed by hand | `tools/legal/stagingcheck.py` reads the real staging cpio (12 packages found, all listed); the list is trimmed; `marks` runs it on every release | Done |
| B12 | SignPath is credited as if it signed | The note says "not in use yet ... SignPath is not connected to AurOS today" | Done |
| B13 | Nothing writes `/etc/auros/release` | `build/forge` writes it from `AUROS_BUILD_TAG`; E passes the tag | Done (forge); **OTHER: E** |
| B14 | Wrong license statements (e2fsprogs, bash, "free to use" for firmware); "only to say what something resembles" | Table corrected; firmware qualified; the trademark paragraph says "made from, resembles or works with" | Done |
| B15 | Marks named but not attributed (Linux, Intel, Safe Browsing, SmartScreen, SignPath) | The paragraph names Defender, SmartScreen, Safe Browsing, Linux, Intel and "other names ... their owners" | Done |
| B16 | The Firefox pointer cannot back the mail-order route if Mozilla's archive goes | Every sources release now carries a hash-checked copy of Mozilla's tarball | Done |
| B17 | The name "AurOS" | `NAME-SEARCH.md`: UserTesting rebranded as Auros; Emergent Systems holds US reg. 4512239 | **OWNER: decide before the public link** |

## What was verified by the skeptics (so it is not re-checked)

The website loads nothing external and sets no cookies; Setup's only network call is the image download; the release gate
needs a passing e2e in the same run; Firefox telemetry and studies are off; snapd, ubuntu-advantage-tools and
popularity-contest are excluded and Ubuntu Pro's units are masked; the time servers match chrony's files; Firefox DoH is
not forced on; Windows browser passwords are never copied; epoch'd versions resolve on Launchpad; the shim-signed split
and binNMUs are handled; the published SOURCES.txt had linux, linux-signed, grub2, grub2-signed, ntfs-3g, e2fsprogs, kmod,
glibc, openssl, fonts-dejavu and all linux-firmware splits; the notices texts for MIT, OFL, Apache, ZPL, Unicode and the
Wayland headers are present; the three-year term and cost wording are at least as generous as GPLv2 3(b) and GPLv3 6(b).

## Round 2

Two more skeptics, told not to repeat what round 1 got fixed, read the v2 texts (C: honesty, with the code open) and the
sources tooling, notices and trademark use (D). Their findings and the answers; the texts are now v3.

### Skeptic C: honesty of the rewritten texts (14 findings, 6 nits)

| # | Finding | Answer | Status |
|---|---|---|---|
| C1 | `screen.c` / `wizard.c` stopped and put-back screens say "files are safe / not touched / every file in it / nothing has been lost" at the point where the Terms admit loss | `check.py` BANNED now also refuses "files are not touched", "with every file in it", "nothing on this computer has been lost", "stays, with all your files"; each one in someone else's file is in PENDING until 2026-11-30, after which it fails. Replacement strings are in `LEGAL.md` (B and A2 lists) | **OTHER: A2, B** |
| C2 | No assent, no risk line, no licence notice where the decision is made; nothing enforces it; a helper pressing Install | Terms 4 now says "using AurOS, including pressing Install, means you accept" (true whatever the installer shows); Terms 2: whoever presses Install for someone else says they have read and agreed; the "Licenses button" sentence is gone from LICENSES and NOTICES until it exists; `check.py --launch` is now a step of `pages.yml` `deploy` and of `release.yml` `site` (only for a run that would promote to [latest]) | Done (text, gate); **OTHER: B** (the line, the button, RCDATA 200/202) |
| C3 | Hero and Ready page hide the most material facts (only a pretend Windows 11, Windows made smaller) | Hero: "Tried so far only on a pretend computer, not a real one" (the first screen on a phone has room for one more line only: `check.mjs` A-02); Step 3 says Windows is made smaller and Good to know says Windows 11 on a fast drive; Terms 1 says it. The h1 "Your computer, new again." stays: **OTHER: D** decides; the Ready page strings are in `LEGAL.md` | Done (site); **OTHER: D, B** |
| C4 | "What was downloaded stays there" is false: closing Setup deletes it | Verified (`WM_DESTROY` -> `install_forget` -> `ab_forget_image`). Terms 4, Privacy 3 and CLAIMS row 26 corrected; a power cut can leave part | Done |
| C5 | A multi-gigabyte download starts before consent and its size and data cost are never said; `status.html` "Setup never changes anything until you say so" | The site has "The download" (about 2 GB, close Setup if your internet has a limit); Terms 4 and Privacy 3 say it. (Skeptic C's "5 GB" came from a stale comment: `DL_BYTES_FALLBACK` is 1.8 GB and the manifest gives the real size.) Status page reworded. The Welcome page line is **OTHER: B** | Done; **OTHER: B** |
| C6 | Terms 4's list is out of order and incomplete (shrink after the restart on the default path; the put-back task as SYSTEM at every start, `ProgramData` files, EFI files; Fast Startup only restored on abort) | Terms 4 rewritten from `phases.c`/`plat_win.c`: Fast Startup and hibernation stay off while AurOS is there, shrink "while Setup runs or just after the restart", EFI files and entry, the task and its folder | Done |
| C7 | Put Windows back is presented as safe | Terms 4, FAQ, Disclaimer say it changes the drive again and carries risk | Done |
| C8 | `guide.html`: "Not now" does not exist on Welcome/Ready; "Windows and your files are kept" unhedged | Lede: closing before Install changes nothing; "Not now" only where offered; sheet: "made to keep" | Done |
| C9 | Step 3: "If AurOS fails to start, the computer starts Windows instead" is unproven (`check.mjs` proves a unit exists) | "should start Windows instead; if it does not, ask someone to help"; "is set to start"; "are meant to stay" | Done; the real proof is **OTHER: F** (a boot-failure e2e) |
| C10 | Privacy 3/4 omit Wi-Fi name, printers/devices, language; "Save a report" file; search box row wrong; Safe Browsing detail; Firefox route | All added or corrected (CLAIMS 36-38); Firefox's source row names the Launchpad route | Done |
| C11 | Terms 8 understates updates; Privacy 1 "your choice" cannot hold | Terms 8 lists what is installed on its own (Ubuntu, Mozilla, Flathub programs, AurOS's own packages including start-up files) and that it does not restart; "it will be your choice" removed | Done |
| C12 | "put together and changed by its maker" reads as Canonical; "we" is unnamed | "AurOS is built from Ubuntu 24.04 LTS software that we have put together and changed"; "we" is the owner of the project on GitHub, named on the project page. Naming the person is the owner's call | Done; **OWNER: name or handle in the Terms** |
| C13 | Absolutes elsewhere: "Your computer is safe/fine"; Wishes listed live on status | Reworded in `status.html`, `something-wrong.html`, `never-call.html`; the Wishes line is removed from status; BANNED `computer is (safe\|fine)` | Done |
| C14 | "25 to 60 minutes" assumes 24 Mbit/s and an SSD; the hero omits the speed dependency | The hero already says older computers take longer; the internet-speed dependency is D's data-fact and B's estimate | **OTHER: D** |
| nits | Terms 1 "extras may cost money" vs "no subscription"; dates one day ahead; GiB vs GB; "no advert" wording; use restrictions rest on the "licenses win" clause; CONTACT_EMAIL | Reworded; documents dated 23 October 2026 and now Version 3; GiB/GB left (Windows shows GiB as GB); Privacy: "AurOS shows no adverts of its own"; the rest as before | Done / **OWNER: mailbox** |

### Skeptic D: sources, notices, tooling, trademarks (17 findings)

| # | Finding | Answer | Status |
|---|---|---|---|
| D1 | The `again` path could destroy the only good source release (`--clobber` on a published release) | `tools/legal/publish.py`: nothing of the old release is touched until the new set is complete and GitHub's digests match; "never fewer" is enforced; strays are cleaned; tested against a pretend GitHub with 5 mutants (`publishtest.py`, in `legal.yml`). The release broken by the cancelled run is remade by it | Done (the remake is run after this commit) |
| D2 | `release.yml` neither waits for sources nor points to them | The sweep adds the pointer line to the AurOS release's notes and attaches `NOTICES.txt` (add only) before it builds; `promote` needing `sources` is E's | Done (pointer); **OTHER: E** |
| D3 | The installer carries no licence texts or offer; NOTICES and LICENSES say it does | The sentences are removed until it is true; the texts are attached to the release; `--launch` needs `L"Licenses"` and RCDATA 200/202; a `manifesttest` check for left-over `@AUROS_` / `CONTACT_EMAIL` in resource 200 is B's | Done (text); **OTHER: B** |
| D4 | The mail route has a placeholder; `--launch` runs nowhere | `--launch` is now a step of `pages.yml` and of `release.yml` `site` (for [latest] runs); the mailbox is the owner's | Done (gate); **OWNER: a mailbox** |
| D5 | The offer does not help an `AurOS-Setup.exe` holder identify the release; "three years" not on the mail route | SOURCE-OFFER: "the release is the one on whose page you found it", and the mail route now carries its own term; showing the tag in Setup is B's; `AUROS_BUILD_TAG` is E's | Done (text); **OTHER: B, E** |
| D6 | The statically linked glibc source can be a later version than the one linked; failure is hidden | The sources job fails when the installer artifact cannot be downloaded; `host-sources.txt` is E's hand-off; until then SOURCES.txt says the glibc of the publishing machine was used **and** lists the image's glibc | Partly; **OTHER: E** (record the host's glibc in `build.yml`) |
| D7 | One bad matrix leg skips verification for every other | The public re-verification is the last step of the `make` leg; releases older than 13 days leave the matrix (the weekly check says so) | Done |
| D8 | Trigger regex unanchored; the run need not belong to a release | The subject must start with `[sources TAG RUN-ID]`; the release must exist, be published and name that run (tested with a fake gh) | Done |
| D9 | Unvalidated names reach paths and URLs | `parse_status`, `read_extra` validate names and versions; a name that walks out of the work directory is refused (test + mutant) | Done |
| D10 | A folded `Built-Using` loses its tail | Continuation lines of `Built-Using` are folded into it (the other fields we read never fold; their continuations are ignored); a malformed `Built-Using` is an error | Done |
| D11 | NOTICES section 4 lacks licences that section 3 names | GPL-1, LGPL-2, LGPL-3, GFDL-1.3, Artistic added; `notices.py` reads the references and fails on a text it lacks | Done |
| D12 | Trademark statements overreach (Firefox changed defaults/extension; Canonical letter volunteers "we do not recompile") | Last sentence now "what a program is, what AurOS is made from, or what it works with"; attributions completed ("in the U.S. and other countries", "or its subsidiaries"); letter rewritten around what is true (binary packages as published, branding left out); `MOZILLA.md` lists the open question and the version/route issue | Done (text); **OWNER: ask Mozilla or change the configuration** |
| D13 | Future-dated attestations | All dated 23 October 2026 | Done |
| D14 | `SOURCES.md` contradicts `again` and "weekly-style" | Rewritten | Done |
| D15 | Mozilla pointer checks are thin | `verify` compares the pointer with the firefox row and the mirror hash with the pointer, every `P` row | Done |
| D16 | A persistent 5xx from Launchpad skips the archive fallback | Falls through to the next base on server errors too, retries, and fails closed if all fail | Done |
| D17 | GitHub's automatic "Source code" link is the wrong commit | Every sources release's notes say so; SOURCES.md says so | Done |
