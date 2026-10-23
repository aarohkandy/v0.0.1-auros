# AurOS plan: from today's repository to the owner's bar

Revised on 2026-10-22 against origin/main `75e3321`. The first draft merged 8 lens reports (about 230 raw gaps) into 72 work packages. Two critics then reviewed it: one for completeness (what the first week with 1,000 users needs) and one for priority and sequencing. Every point they got right is folded in below. The points that were rejected or changed are listed in the appendix, with reasons.

The result: **97 numbered work packages** (WP-01 to WP-97; WP-04 is split into 04a and 04b), and **5 new streams** (OPS, HW, PARITY, USABILITY, BACKUP). Boot safety goes to the existing **BT** stream; section 4 has a brief so BT can be respawned if it isn't live.

**WP** means work package. Stream IDs are the ones in `docs/handoff/STREAMS.md` and `SESSION-2026-10-23-B.md`. **NEW:X** is a stream to spawn; section 4 has its brief. **Status** marks a package checked against `75e3321`: *partial* means some of it has already landed and only the rest is open. Every other package must be re-checked before it is handed out (WP-93).

**Every stream, every package: push to GitHub after each green step, and never go more than 15 minutes of work without pushing.** That is the owner's rule ("push extremely frequently so I have a safe") and the rule in `docs/handoff/FIELD-MANUAL.md` §5. Before any step longer than 10 minutes, push and append one Progress-log line. Unpushed work is lost work. Nothing in this plan (branch protection included, see WP-03) may slow that down.

---

## 1. The finish line

### v0.1: "Safe to put the Get AurOS button in front of the public"

Every box below must be proven by the named test going green and by a mutant (a deliberately broken copy of the code) turning it red. **The boxes are enforced by a machine, not by a markdown list:** `docs/LAUNCH-READINESS.md` has one row per box, and the Get AurOS button and downloads stay off until every row is green (WP-85).

The public link opens in **waves** (WP-74): about 25 downloads a day, then 100, then 300, then open. Each wave widens only when the field numbers (WP-73) stay inside the stop rules. "The public link" below means wave 1.

**The download exists and is trustworthy**
- [ ] The site is live on the owner's domain. A test follows the Get AurOS button and checks that the file's sha256 matches the promoted release asset (WP-01, WP-15).
- [ ] Promote only happens after every `e2e (…)` job for that exact exe is green. A broken selector makes promote refuse (WP-01).
- [ ] VirusTotal has scanned the promoted exe and no more than 2 engines flag it (WP-05). The website has an "If your antivirus took the file" section and correct wording for S mode and Smart App Control (WP-40). The share of Smart App Control PCs that block the exe is measured (WP-09 `sac-on`).
- [ ] GPL source for every binary in the image and the exe is published as a release that is never deleted (WP-06).
- [ ] Terms, Privacy, a not-affiliated line and Licences are linked from every page footer and from the installer. The Terms and Privacy texts are approved by a lawyer (WP-37).
- [ ] The first promoted exe can already be told "this copy is out of date, get the new one" (WP-04a).
- [ ] Every address baked into the image, the staging initramfs and the exe is on the owner's domain (WP-15).
- [ ] An outside security review of the admin, root, signing and cloud paths is done, with no critical or high finding open (WP-84, decision 20).
- [ ] Every data-safety and security finding rated blocker or high either maps to a WP or is closed by a test (WP-86).

**Her data and her Windows are safe**
- [ ] The installer works in UTC−8, UTC+0 and UTC+13, and across a daylight-saving change with Windows still on the disk. After both systems have run, Windows' clock is within 2 minutes of correct (WP-10, WP-09).
- [ ] "Fix these for me" never touches `$Recycle.Bin` and never decrypts BitLocker; a lint keeps it that way (WP-11).
- [ ] A power cut at any instant, a retry after a failure, and Close or Cancel at any phase each leave a known state. A retry never shrinks Windows twice (WP-87).
- [ ] If the install is refused, fails, or BootNext is ignored, she gets one plain Windows page, "How it went", with a working clean-up button (WP-16). The red staging screen has no component names and restarts into Windows by itself (WP-13).
- [ ] Put Windows back copies her AurOS files into Windows first (WP-27). It restores the EFI start-up partition file by file, keeping any Microsoft file that changed after the capture (WP-14). The `decline` and `putback` e2e variants gate promote (WP-09).
- [ ] An update that stops AurOS from booting rolls itself back within 3 boots with no input from her. This includes Ubuntu kernel, shim and GRUB updates, which already arrive by themselves today (WP-30).
- [ ] If Windows (or a BIOS update) moves itself first in the start-up order or deletes AurOS's entry, AurOS is put back first and she is told in one sentence (WP-77).
- [ ] BitLocker PCs: either the pause path (WP-50) is proven, or the site and `--report` tell her before she downloads that AurOS can't go on this PC yet (decision 19).

**She is never stuck on day 1**
- [ ] Her Wi-Fi comes across with no typing (hwsim test). A Wi-Fi-only PC whose chip has no Linux driver is refused before anything changes (WP-12, WP-13, WP-32).
- [ ] No password prompt exists anywhere, and the account has no usable password (WP-26).
- [ ] Her chosen look survives 2 reboots. No sentence on screen contradicts what the computer does (WP-19, WP-41).
- [ ] The screen stays on during a 15-minute video. Bluetooth headphones play sound. A .docx, .xlsx, HEIC photo, PDF and mp3 each open. Email and mailto: work. Phone photos import. Printing to a driverless printer works (WP-19, WP-22, WP-25, WP-34).
- [ ] "Bring my files across" works on 5,000 photos plus OneDrive placeholders and shows plain progress. Until that is proven, the button is hidden (WP-27).
- [ ] Her Edge and Chrome favourites appear in Firefox on first start (WP-78).
- [ ] Every string she can see, including failure screens, staging text and the GRUB menu, passes plainwords in CI (WP-36).
- [ ] Before she commits, she has been told: which of her Windows programs won't come with her; which sites she will have to sign in to again; whether her printer and scanner will work; whether some of her email lives only on this PC; and she has opened her email in a web page and seen it work (WP-17, WP-32, WP-79, WP-80).
- [ ] The wizard offers only languages AurOS actually speaks. A non-English Windows gets one honest screen (WP-81).

**Safe after install**
- [ ] The signed AurOS channel is live. The signing key is an offline primary with a signing subkey, and every publishing secret sits behind an owner-approved environment (WP-88). Firefox, Ubuntu -security and -updates, and Flatpak apps all update by themselves, proven by a "day 30" test. A calm restart card appears when a restart is needed (WP-28, WP-19).
- [ ] Updates roll out in phases (1→10→50→100%), and `[apt-halt]` freezes a rollout (WP-29). Updates from Ubuntu, the Firefox source and Flathub reach the fleet only after the canary ring passes a day-1 smoke test, with a fast lane for urgent security fixes (WP-75).
- [ ] An installed prerelease N-1 upgrades to N and survives 2 reboots, before the first `[latest]` (WP-07).
- [ ] Every installed address is on the owner's domain. `repotest` fails if `github.com`, `github.io` or `workers.dev` appears anywhere she or her PC will use (WP-15).
- [ ] Supply chain: actions are SHA-pinned, secrets live in protected environments, publishing jobs refuse anything not on `green`, and every piece is attested and verified before it is signed (WP-03, WP-88).
- [ ] The ~12 orphan safety suites and the fuzzers run in CI. A `green` ref exists, and every release is cut from it (WP-02).
- [ ] The v0.1 image carries no Wishes code at all (WP-89).

**Someone is watching**
- [ ] Synthetic monitoring runs every 15 minutes and pages the owner's phone. There is a status page and `docs/ops/INCIDENTS.md` with 4 playbooks, rehearsed (WP-35).
- [ ] An opt-in outcome ping (off by default, fixed values only, no ID, no stored IP) shows each day how installs went (WP-73).
- [ ] The owner can put one signed, plain notice card on every installed PC (WP-76).
- [ ] There is a private support inbox with a stated reply time, a website card for every staging verdict, and a "Tell the AurOS team" button that previews exactly what it sends and works even when Wishes is switched off (WP-39). A security contact and security.txt exist, and private vulnerability reporting is on (WP-37).
- [ ] A same-day lane for suspected data loss, and an "AurOS Rescue" stick a helper can use to copy her files off and put Windows back (WP-82).
- [ ] A named second person can halt updates and pause downloads without the owner, and has done it in a drill (WP-83).
- [ ] Rollback works: `[rollback TAG]`, plus a signed withdrawn.txt that makes old installers say "get the new one" (WP-04a, WP-04b).

**Real people, real PCs**
- [ ] At least 5 physical PCs (different vendors, at least one HDD, one Wi-Fi-only) complete install → use → Put Windows back. One row per machine goes in `docs/results/hardware/` (WP-33).
- [ ] Three people aged 70+ have tried the wizard and the desktop early, and what confused them is fixed before the wizard is frozen (WP-92).
- [ ] At least 4 of 5 observed people aged 70+ finish, with no help, in under 60 minutes and with zero data loss (WP-43).

**Wishes**
Wishes is not in the v0.1 image (WP-89). It arrives through the update channel, per cohort, only when WP-44 to WP-49 are all green (decision 8).

### v0.2: "Every PC, under an hour"
- [ ] D11 BitLocker pause runs through the fast path. The bitlocker e2e variant shows no recovery screen anywhere, including "Start Windows this once". After the engine swap, the base e2e, 3 real PCs and a power pull are re-run on the new engine (WP-50, WP-23).
- [ ] e2e variants are green: slownet at 10 Mbit/s under 60 minutes, hdd, smalldisk, pending, win10, lang-de, ram4, dpi150, sac-on, avast. A monthly "Windows patched Secure Boot" run keeps AurOS booting. MATRIX.md is generated from CI (WP-51).
- [ ] At least 13 of 15 physical PCs reach "Yes", with no data change on any failure. A bare-metal runner is in the release gate. A field hardware report feeds `hw/compat.json` and the "Tested computers" page (WP-52).
- [ ] fwupd applies db/KEK/dbx updates, including the 2023 CA, with a BitLocker-aware deferral. staging.efi stays fresh (WP-53). The HWE kernel ships, with GA kept as the safe entry (WP-54).
- [ ] HiDPI, touch, rotation, Nvidia, suspend drain, the guided check before Yes, and screen share with a red Stop bar all work (WP-55).
- [ ] The full parity table is green: Widevine, the top-25 sites, the 40 apps, media performance and PDF forms. If Flathub is down she sees a plain sentence (WP-56).
- [ ] Backup to USB with restore, plus disk-health warnings (WP-57). "Repair AurOS, keep my files" and "give AurOS Windows' space" work (WP-58). The PIN is shipped only with a verified rescue code (WP-59).
- [ ] Downloads come from an R2 mirror. The site is behind a CDN with a page-weight budget. A hotfix lane takes a commit to the channel in 60 minutes or less. A nightly build and a CVE scan run. The repo lives in an org with 2 admins (WP-60).
- [ ] Wishes v1.1 adds Ask AurOS, local intents, voice, a "Call Sarah" button, a provider fallback and a capacity and cost model (WP-61).
- [ ] Builds are reproducible. The installer stays byte-stable across image releases (WP-62).
- [ ] The installer is code-signed, if decision 2 says so (WP-64).
- [ ] A grandchild can help from far away: one-time code, view-only until she grants control, a permanent banner (WP-72, minimal).
- [ ] Lookalike sites are watched, and the installer says "This copy isn't from AurOS" if its own hash doesn't match (WP-94).
- [ ] Put Windows back after months tells her how far behind Windows is (WP-95). Two people can share one PC (WP-96). Her saved passwords and her on-this-PC email can come across (WP-97).

### v1.0: "Something people pay for"
- [ ] Translations in 5 languages with no English left over. A screen reader and an EN 301 549 statement (WP-66, WP-67).
- [ ] CRA: an SBOM per release, a declared support period, a 24-hour reporting runbook. A tested in-place upgrade from 24.04 to 26.04 (WP-68).
- [ ] A merchant of record, sales terms, refunds, paid human support with a runbook per verdict, a funded cost ledger (WP-69).
- [ ] Mini-programs ladder (declarative cards, then WASM), red-teamed (WP-70). Hearing-aid audio, extended desktop (WP-71). Family remote help grown beyond the v0.2 minimum (WP-72).

---

## 2. Work packages

Format: **ID title**, then owner, effort, depends. **Out** is the outcome. **Proof** is the test that shows it.

### v0.1: release and CI (E, F)

**WP-01 Promote gate and first public chain.** E · S · WP-08 · *Status: partial.* `release.yml` already refuses a run unless `build`, `tests / unit` and every `tests / …` job succeeded, with at least 5 `e2e (…)` jobs.
Out (what is left): the e2e count is checked against the variants that were asked for, not a floor of 5. The first prerelease is e2e-tested, promoted, and deployed to Pages. Proof: after deploy, a check fetches the live site, follows Get AurOS and compares sha256. A selector mutant makes promote refuse. A run with one variant missing makes promote refuse.

**WP-02 CI that tells the truth.** E · M · — · *Status: partial.* powercut, bootchain, bootupdate, gpt, shrinktarget and putback already run in tests.yml.
Out: `unit` is split into one transcript per suite, with a final verdict step listing every red suite. nmtest is fixed. A `safety` matrix runs whatever is still outside CI of commit, ntfs, shrink, fail, health, wr and scam, plus 5 minutes per fuzz harness with ASan and UBSan, **nightly, never on push** (CI capacity, section 3). A `green` ref fast-forwards only on all-green, and `[release]` builds from it. Proof: a commit that breaks two suites shows both names. One mutant per suite is recorded red.

**WP-03 Supply-chain lockdown.** E (with S5 and owner clicks) · M · WP-88
Out: every `uses:` is SHA-pinned, with dependabot. `contents:write` moves down to job level. build.yml attests provenance for the exe, the pieces and the debs, and aptrepo runs `gh attestation verify` before signing. The Flathub key fingerprint is pinned. Owner clicks: Actions allow-list plus required SHA pinning. **Protection sits at the point of use, not on pushes:** agents keep pushing straight to main; the `apt-signing` and `cloud-deploy` environments need the owner's approval, and every signing or deploy job refuses a commit that is not on `green`. (A path-based "PR for workflow files" rule was dropped; see the appendix.) Proof: an `@v4` mutant turns repotest red. A clobbered piece in a scratch repo fails verify. A workflow edit pushed to main cannot publish without the owner's click: the run sits at "waiting".

**WP-04a Withdrawn check in the first exe.** B (with E) · S · WP-15
Out: the installer fetches a signed `withdrawn.txt` from get.<domain> at start. If its own tag is listed it shows "This copy is out of date, get the new one" with the address. A 404 on a piece produces a plain "get a fresh copy" sentence. This ships in the first promoted exe; it does not wait for WP-29. Proof: a sim wizard run against a withdrawn.txt that lists its own tag. A forged signature is ignored. A 404 sim.

**WP-04b Rollback and retention.** E (with B) · M · WP-29, WP-04a
Out: `[rollback TAG]` re-marks latest and redeploys the site. A prune job keeps every promoted release for at least 12 months and deletes non-promoted ones after 14 days. Proof: a dry-run prune list. A rollback drill in a scratch repo.

**WP-05 Antivirus gate.** E · S · owner VT key
Out: each prerelease exe is checked on VirusTotal, the result is saved as `vt.json`, and promote is blocked above 2 detections. docs/RELEASE.md gets a per-release false-positive submission checklist for Avast/AVG, Norton and McAfee. Proof: the gate goes red on a fake detection count.

**WP-06 GPL sources.** E (with K and D) · M · —
Out: `build/sources` collects .dsc plus orig and debian tarballs for every image package and every staging initramfs binary, published as a `sources-<tag>` release kept for 3 years or more. A SOURCE-OFFER file, a Licences page in Help, and a corrected footer ("AurOS's own code is MIT; it includes…"). Proof: CI fails when one .dsc is removed.

**WP-07 Upgrade N-1 → N, before the first `[latest]`.** E with S5 · M · WP-28
Out: a tests.yml `upgrade` job boots a **prerelease** image as N-1 with Secure Boot, points it at a local channel signed with a throwaway key, runs unattended-upgrade, and reboots twice. firstboot, aurshell, bootchain and a user setting must survive. The first `[latest]` is refused until this is green: the first promoted image is every user's N-1 forever. Proof: a mutant whose postinst exits 1 turns the job red.

**WP-08 Real-Windows journey green.** F · L · E's first prerelease (it does **not** wait for the domain or the channel)
Out: M2 and then M3 are green on this repository's current prerelease. The ISO is pinned by URL and SHA-256 and mirrored in a private `ci-inputs` release. A journey clock covers website → Run anyway → wizard → restart → Yes → program open, with a budget per step; a step over budget fails the run. The SSD total must be 30 minutes or less at a throttled 50 Mbit/s. The numbers are printed in the job summary. Once WP-15 lands, one more base run proves the exe with owned addresses. Proof: one promote that passes on this e2e run.

**WP-09 v0.1 e2e variants.** F · M · WP-08, WP-10, WP-16, WP-27, WP-77
Out: `tz-west` (UTC−8, RTC in local time), `decline` (No → Windows, C: back to its original size, chkdsk clean), `putback` (the "From AurOS" folder is present and the boot list is clean), `nobootnext` (boot entry deleted after phase 3, so the How-it-went page must appear), `windows-once` (start Windows, apply a cumulative update and `bcdboot`, restart, AurOS must start), `sac-on` (Smart App Control on; **measurement only** in v0.1, the result feeds decision 2), and `bitlocker` if decision 19 picks the pause path. Proof: tz-west is red before WP-10 and green after; windows-once is red before WP-77.

### v0.1: install engine and wizard (A2, B)

**WP-10 Clock.** A2 · S · —
Out: the journal records ActiveTimeBias and RealTimeIsUniversal. Staging corrects for them, with ±15 hours of tolerance. AurOS writes `/etc/adjtime LOCAL` while Windows is on the disk, and puts it back to UTC when Windows is removed. Daylight-saving cases: Windows before the change and AurOS after it, and the reverse, with no network (EU clocks change 25 Oct 2026, US 1 Nov 2026). Proof: nostick and install tests under QEMU `-rtc base=localtime` at UTC−8, UTC+0 and UTC+13, plus both DST crossings; both systems agree within 2 minutes. A mutant with the correction or the adjtime handling removed goes red.

**WP-11 "Fix these for me" stays safe.** A2 · S · — · *Status: mostly done.* The Recycle Bin step is gone (`fix_win.c` freeing-space comment) and BitLocker is refused, never decrypted (`preflight.c`, D11).
Out: the decrypt page in the first draft is **dropped**; it would bring back what D11 removed. What is left: a lint in tests.yml that fails on `manage-bde -off`, `Disable-BitLocker` or `$Recycle.Bin` in `src/aurbridge`. Proof: one mutant of each shown red.

**WP-12 Wi-Fi comes across (D10).** A2 (F wires CI) · M · —
Out: after a visible tick box, the installer reads only the connected Wi-Fi profile through WlanGetProfile. The key travels in the journal, never in choices.conf, and lands in a 0600 NetworkManager keyfile before commit. Proof: a bridgetest round trip. `tools/wifitest.sh` uses mac80211_hwsim with hostapd (WPA2 and WPA3), has no cable, and must be online at the welcome screen. Unticking leaves no keyfile. A mutant with a wrong psk goes red.

**WP-13 Staging failures she can survive.** A2 · S · D (the help page), WP-15
Out: stop text in plain words, a QR code to `help/stopped.html#<verdict>` **on the owned domain** plus a 6-character code, and an automatic restart into Windows after 60 seconds. `probe_modalias` skips display-class devices (`bc03`). aurscreen re-opens the display on ENODEV. The probe counts only non-HDMI playback as speakers. If Windows had working Wi-Fi, AurOS finds 0 Wi-Fi radios and there is no cable, staging refuses before commit. Proof: screentest PNGs. A stagetest QMP screendump across phase 7, with a mutant. probe_judge tests on a synthetic /sys.

**WP-14 EFI partition restore by file.** A2 · M · —
Out: if the ESP mounts, the restore removes only `\EFI\AurOS` and AurOS's boot entry, and never rolls back Microsoft or OEM files. The raw image restore is kept only for a damaged ESP. Proof: a putbacktest case changes bootmgfw.efi after capture, and the newer file survives. A Secure Boot OVMF run with the old hash in dbx still reaches Windows.

**WP-15 Owned addresses, baked before the first real install.** NEW:OPS (with edits by E, A2, S5, M) · M · decision 1
Out: get., updates., wish. and www.<domain> sit behind Cloudflare, which redirects to GitHub for now. The image, pieces.txt, the staging initramfs, the exe and the cloud Worker carry only these names (`cloud/wrangler.toml` drops `workers_dev = true` for a route on the domain; `tools/repo.sh` stops building github.io help links). pieces.txt allows several `base` lines, tried in order (A2 changes the phases.c parser). auros.sources uses `mirror+file:` with GitHub as the fallback. Proof: `tools/repotest.sh` scans the image rootfs, the staging initramfs strings and the exe strings, and fails on `github.com`, `github.io` or `workers.dev` outside the listed fallbacks; a planted string in each of the three goes red. updatetest's "moved" case: the primary returns 404 and the update still arrives. A sim getimage where base #1 returns 503, 429 or a bad Content-Range completes from base #2.

**WP-16 "How it went" in Windows.** B · M · A2 (the record format on the ESP)
Out: phase 3 registers a one-shot logon task. On her next Windows sign-in it reads the staging record and shows one of: refused (nothing changed), failed after the shrink, never started (BootNext ignored, with a boot-menu picture for her PC's maker), or installed (removes the task). Buttons: "Try once more", "Remove what AurOS left", "Get help" (shows the code). Proof: windows.yml plants each verdict. An e2e run with `die_at` injected.

**WP-17 Before you switch.** B (with K for wording; uses WP-32) · M · WP-32
Out: three screens. (a) Programs that won't come with her: registry Uninstall DisplayName matched against a vetted table of about 60 entries, each with what to use instead. (b) Sites she is signed into, read from Edge/Chrome history and cookie hosts only, never secrets, with "Print this list". (c) The hardware verdict, including her printers and scanners (WP-32), and the "email only on this PC" warning (WP-80). Proof: bridgetest fixtures with fake hives and profiles. A grep test asserts the code never opens `Login Data` or `Local State`. Screenshots.

**WP-18 Ready page and consent record.** B · S · WP-37 wording
Out: a short disclaimer of 25 words or fewer, an optional "Copy my pictures to a memory stick first", a Terms link on the first screen, the outcome-ping tick box, unticked (WP-73), and a consent record in the journal (wording hash, time, D10/D11 choices, ping choice, Smart App Control state). Proof: navtest asserts the string ids. e2e reads the record from the guest.

### v0.1: desktop, packages, hardening, Ferry (C, P, S1, R)

**WP-19 Day-1 correctness.** C · M · WP-28
Out: the saved look is loaded before the profile default (main.c:744/754). The welcome line is made true. Screen stays on during video and calls: `zwp_idle_inhibit_manager_v1` in aurwl, plus `org.freedesktop.ScreenSaver.Inhibit`. A restart card reads `/run/reboot-required` and offers "Restart now / Tonight"; after 7 days it restarts by itself at 03:00, only when idle, on AC power, with no unsaved work. An update-health card appears when there has been no successful upgrade for 14 days. Proof: choicesboottest picks a look and checks it across 2 reboots. A wltest inhibitor case. A KVM kernel update with the clock moved forward 7 days. A fake-/run unit test.

**WP-20 Display robustness.** C · L · —
Out: kms_open waits up to 10 seconds for a GPU card that isn't simpledrm, and udev re-opens the display when the card changes. The desktop is cloned onto every connected output, with hotplug and lid-closed-with-external-screen handling. The preferred resolution is used at 60 Hz. After resume, the mode is set again (logind PrepareForSleep). Proof: QEMU with virtio-gpu blacklisted and loaded later. 2-head QEMU with QMP hot-add. A QEMU S3 suspend and wake with a screendump diff.

**WP-21 Touchpads.** C · M · —
Out: pointer input moves to libinput (already in the image): disable-while-typing, palm rejection, tap-to-click, clickpad button areas. Proof: libinput recordings (Synaptics, Elantech, ALPS, an I2C-HID precision pad) replayed through uinput. A resting palm while typing produces no motion, and a bottom-right press is BTN_RIGHT.

**WP-22 Apps actually open.** C (with S2) · L · WP-34
Out: a rootful Xwayland fallback for X11-only apps; any app it can't save is hidden from the store. A `.exe`/`.msi`/`.deb` card says "This is a program for Windows; Zoom is in Get more programs [Open it there]", using a name-to-Flathub map that S2 reviews; the gdebi rule is removed. Email: a launcher that opens her webmail, picked once, plus a Firefox `Handlers` policy for mailto:. A Chromium "call browser" handles facetime.apple.com/join and web.whatsapp.com and gets S2's policies. Proof: the apptest table, storetest, the parity mailto check, and scamtest run against both browsers.

**WP-23 "Start Windows this once".** C (with K) · M · —
Out: a new answer word. aurfirst sets BootNext to the Windows boot entry and restarts, so firmware starts Windows directly and there is no GRUB chainload. Before it does, it warns when Windows has an update half-installed ("Windows may spend up to an hour finishing an update"). help/windows.html is rewritten. Expert GRUB entries move into a "For someone who knows computers" submenu, and `init=/bin/bash` is dropped. Proof: aurfirsttest. An OCR check in the e2e bitlocker run finds no "BitLocker recovery".

**WP-24 Fixers and choosing where sound goes.** C (with K for pages) · M · WP-32
Out: Help → "Something isn't working" → Internet / Sound / Printer / Screen. Each runs local checks with a one-press fix: rfkill, driver missing compared with the Windows inventory, muted, wrong output, a stuck print queue. A Settings row lets her choose "Sound comes from…" and the microphone, shown only when there is more than one. Proof: unit tests over fake sysfs, rfkill and wpctl output. A KVM test mutes sound and the fixer restores it.

**WP-25 Everyday packages.** P · S · —
Out: add libspa-0.2-bluetooth, libreoffice-writer, libreoffice-calc, libreoffice-gtk3, fonts-crosextra-carlito, caladea, heif-gdk-pixbuf, tumbler, ffmpegthumbnailer, usbmuxd, libimobiledevice-utils, libmtp-runtime, ipp-usb, libnss-mdns, aisleriot and gnome-mahjongg. MIME entries for docx, xlsx, pptx, odt, heic and mailto. filetypes.sh fails on any of 15 everyday types left unmapped. Image growth and idle RAM are measured against the 4 GB budget. Proof: imagetest asserts each package file, with one mutant per package.

**WP-26 No shared password.** S1 · M · — · *Status: partial.* forge already locks the account (`usermod -p '!'`, `build/forge:1434`) and root.
Out: **the test comes first**, against today's image: drive every Settings row and a store install, and list every action that would raise a polkit agent dialog. Then a polkit rule gives the active local user each of those actions: NetworkManager, cups-pk-helper, power profiles, flatpak system install. Proof: a firstboot test asserts the shadow entry is locked, drives every row and a store install, and fails if any polkit agent dialog appears.

**WP-27 Ferry works, both directions.** R · M · —
Out: ferry-copy drops to her uid and the answer-service sandbox is fixed. Progress is plain, weighted by bytes, with "about N minutes left". The button stays hidden until the test passes. Reverse Ferry: before Put Windows back is armed, her Desktop, Documents, Pictures, Music, Videos and Downloads are copied into a "From AurOS" folder in Windows. It checks free space first, refuses a dirty or hibernated volume, and lists anything that did not fit. Proof: firstboottest on 5,000 photos plus OneDrive placeholders, red with the old unit file. putbacktest asserts the files are on the restored NTFS and `ntfsfix -n` is clean.

### v0.1: updates and boot safety (S5, BT)

**WP-28 Signed channel live, and everything updates.** S5 · M · WP-88 · *Status: partial.* aptrepo.yml exists and is green; a single-key AUROS_APT_SIGNING_KEY is set.
Out: updatetest and UPDATES.md land. The key is re-cut first as an offline primary with a signing subkey (WP-88). `[latest]` is refused if the image lacks auros.sources or the keyring. Origins added: Firefox from LP-PPA-mozillateam (snapshot-able, WP-75) and `${distro_codename}-updates`. forge fails if Firefox would come from the tarball. A daily `auros-flatpak-update.timer` runs only on AC power and not on metered networks. Release files carry Valid-Until, a re-sign job refreshes them, and an alarm fires 90 days before the key expires. Proof: updatetest cases for firefox (N→N+1 from a local PPA-shaped repo), -updates, flatpak (local OSTree v1→v2) and a revoked subkey. Each origin mutant goes red. A "day 30" KVM run.

**WP-29 Phased rollout, halt and revert.** S5 · M · WP-28
Out: aptchannel writes `Phased-Update-Percentage`. `[apt-phase TAG PCT]` uses a default ramp of 1→10→50→100% over about 4 days, done by scheduled re-signs. `[apt-halt]` sets the percentage to 0. The previous debs are kept, and `aptchannel revert` republishes the last good version with a higher version number. The owner's PCs and the CI VMs are a canary ring that always takes phased updates. Proof: 200 containers with different machine-ids against a 10% index; 15 to 25 of them upgrade. A mutant with the field removed goes red. A revert test. If apt ignores phasing for a non-Ubuntu origin, a 20-line pre-invoke hook passes the same test.

**WP-30 Boot counting and rollback.** BT · L (minimal version M, first) · —
Out, minimal version first (a hard dependency of WP-43 and of any install outside the owner's PCs, because `51auros-unattended` already lets kernel, shim and GRUB update with nothing held back): GRUB increments `boot_try` in grubenv; `auros-boot-ok.service` clears it once aurshell has drawn the home screen for 2 minutes; boot 2 starts the previous kernel. Then the full version: boot 3 adds safe graphics (nomodeset) and reinstalls the auros-* debs kept in `/var/lib/auros/debs/prev`, then holds them until a fix arrives. Boot 4 shows a big menu that waits for her: AurOS / Start Windows / Put Windows back. Proof: `rollbacktest.sh` in tests.yml with a panicking kernel (minimal version), then a hanging kernel and a crashing auros-desktop; each returns to the desktop with no input. A counter mutant goes red.

**WP-31 Record Secure Boot certificates.** BT (with B for sbdb) · S · — · *Status: partial.* bootchain already refuses, at update time, a shim or GRUB that the 2011→2023 CA change or SBAT would stop.
Out (what is left): the journal and an AurOS boot record note whether the firmware db holds the 2023 Microsoft CA and the SBAT level against the installed shim, and the outcome ping (WP-73) carries a yes/no for it. This is measurement now; WP-53 is the fix. Proof: an OVMF VARS fixture with only the 2011 CA is reported as such.

### v0.1: hardware, parity, ops

**WP-32 Will this PC work?** NEW:HW (B shows the screen, A2 adds the journal field) · L · —
Out: AurBridge lists every present device with `SetupDiGetClassDevs(ALLCLASSES|PRESENT)`, plus DMI vendor and model, plus her installed printers and scanners with their USB IDs (`EnumPrinters` and the imaging class), into the journal. `hw/compat.json` is generated at build time from the image's modules.alias, firmware and printer drivers (gutenprint, hpcups, IPP Everywhere), with hand-written seeds: Broadcom wl-only IDs, Intel IPU6/IPU7 cameras, Nvidia-only desktops. If the network device Windows is using now has no driver, she gets one plain screen before the download ("a USB Wi-Fi plug costing about $15 fixes it"), or a refusal when there is no Ethernet port. Printers get "prints but won't scan yet" or "won't print, here's what to do". `AurOS-Setup.exe --report` is a read-only mode; milestone 1 lands it early so the BitLocker scope (decision 19) and WP-33 can use it. Proof: matcher unit tests. A plat_sim fixture with BCM4352 (14e4:43b1) gets the screen and AX201 is fine. Printer fixtures: one that works, one that doesn't. windows.yml shows the IDs in the report.

**WP-33 Real-PC round 1.** NEW:HW with the owner's hands · M · WP-08 M3, WP-10, WP-11, WP-13, WP-14, WP-16, WP-27, WP-87, WP-86
Out: runs on the **prerelease** exe as soon as WP-08 M3 is green and the safety set above is met; it does not wait for promote. At least 5 physical PCs (different vendors, at least one HDD, one Broadcom Wi-Fi-only, one BitLocker) run `--report`, a full install, use, and Put Windows back. A power pull at each phase is tried on at least 2 of them, only after WP-87 proved it in simulation. The share of machines refused, and why, is recorded. Proof: `docs/results/hardware/<vendor-model>.txt` per machine and a generated README table. The public link is withheld until this is green.

**WP-34 Everyday-task harness.** NEW:PARITY · M · E (a CI slot)
Out: `tools/parity/` runs a nightly KVM job, also part of the promote gate. (a) A Jitsi call with a v4l2loopback camera and a PipeWire null-sink microphone, where the remote peer receives video. (c) Print to ippeveprinter over DNS-SD. (d) xdg-open on docx, xlsx, heic, pdf, mp3 and mailto. (e) A 15-minute idle video that never goes black. apptest installs the top 10 apps (Chrome, Zoom, Firefox, LibreOffice, Spotify, VLC, Solitaire, Teams-for-Linux, Thunderbird, a printer app): no password, it appears on Home, opens a window within 60 seconds, and the store catalogue is ready within 30 seconds. Proof: each check is shown red once by removing its package.

**WP-35 Ops baseline.** NEW:OPS · M · WP-15
Out: `monitor.yml` runs every 15 minutes, plus a free external uptime monitor. It checks: the site returns 200 and the exe's sha matches SHA256SUMS; each piece answers a Range request with 206; InRelease verifies and Valid-Until is more than 3 days away; wish health; Anthropic spend; Flathub; security.txt; the WP-74 stop rules. A failure opens an issue and sends a phone push to the owner and the second responder (WP-83). A static status page at status.<domain> on Cloudflare, not GitHub. `docs/ops/INCIDENTS.md` has 4 playbooks (bad installer, bad update, key leak, cloud outage or spend), each with exact commands and target minutes, and `drill.yml` rehearses them in a scratch repo. `fleet.csv` records daily download counts. Proof: each check is mutated (bad hash, expired Release, 503) and pages within one run. The drill restores in under 30 minutes.

### v0.1: words, legal, support (K, D, lead)

**WP-36 Plain words everywhere, and help she needs.** K · S · E (a tests.yml step)
Out: plainwords.sh covers stage_say and stage_warn, aurscreen, fix_win.c, Ferry's progress line and the GRUB entries, and runs in tests.yml. New help pages: sound, phone photos, something-wrong, and a rewritten windows page. Proof: plant "partition" in a stage_say string and CI goes red. helptest.

**WP-37 Publishable legally.** NEW:owner-legal (the owner) with K, D and C · M · decision 9
Out: an entity and a contact mailbox. A lawyer reviews TERMS, PRIVACY and DISCLAIMER. K fills the placeholders. D adds terms.html, privacy.html and a not-affiliated line (Microsoft, Canonical, Mozilla, Apple) to every footer. SECURITY.md gets security@<domain>, `/.well-known/security.txt` is published, and private vulnerability reporting is on (OWNER-SETUP §4b). A letter goes to Canonical. C adds an image-mark audit that fails on Ubuntu logos or names anywhere she can see. Proof: check.mjs fails on a missing footer link. A planted ubuntu-logo turns the audit red. The lawyer's sign-off is recorded in docs/legal/README.md.

**WP-38 The privacy host list is proven.** K with E · M · —
Out: `tools/egresstest.sh` boots the image with tcpdump and DNS logging through first start, 30 idle minutes, the store and one Firefox launch. CI fails on any domain not listed in `docs/legal/hosts.txt`, and the privacy table is generated from that file. Proof: an unlisted curl in a test image turns it red.

**WP-39 Support she can reach.** K (inbox and wording), D (pages), CL (endpoint) · M · decision 6, WP-15
Out: support@<domain> with a ticket-ID auto-reply and a stated reply time. A website "Something went wrong" page with one card per verdict, which the QR code from WP-13 opens. "Tell the AurOS team" in Help and on the How-it-went page: it scrubs user names, network names and serials, shows the exact text first, and goes to `/v1/report` only after she says yes. `/v1/report` is its own small Worker route with its own limits, **not** behind the Wishes budget or kill switch. Proof: helptest checks every verdict id in install.c has a card. The scrubber is run against mutants (home path, network name, email) that must never appear. With the Wishes kill switch on, a report still arrives.

**WP-40 Truthful website.** D · S · WP-08 numbers
Out: the time she is promised is stamped from the e2e journey-clock evidence, both the SSD and HDD medians. Sections for antivirus, S mode, and Smart App Control (can be switched back on since KB5083769, with how). "No, you won't lose files" becomes "designed to keep…; copy treasured photos first". "Nothing to pay, ever" becomes "AurOS itself stays free". "AurOS speaks English for now" (WP-81), and "AurOS only comes from <domain>". Proof: check.mjs fails if the time shown is below the measured median, or if a banned phrase or a section is missing.

**WP-41 Claims table.** W (the lead), with B, C, D and K co-owning their strings · S · —
Out: `docs/CLAIMS.md` maps every promise sentence to its proof test. A test fails if a listed string changes without its proof id, or if the proof goes red. Proof: a welcome-line mutant.

**WP-42 Handoff hygiene.** W (the lead) · S · —
Out: `tools/handofftest.sh` runs in CI. It checks that every stream has a file (BT, W1 and CL are missing from `docs/handoff/streams/`), every referenced doc exists, there are no old-repo literals, and the workflow scripts are committed. It checks that every v0.1 WP id appears in exactly one stream file, and that WP dependency lists respect the hard ordering rules in section 3. It fixes the FIELD-MANUAL §3 `hb run` line so it matches what is really installed on homebase. Proof: deleting a stream file turns CI red. A WP-33 dependency list without WP-87 turns CI red.

**WP-43 People test.** NEW:USABILITY with the owner · M · WP-33, WP-30 (minimal), WP-12, WP-17, WP-26, the rule-2 set
Out: 5 sessions with people aged 70+ on their own PCs, from "here is a link" to "video call your grandchild". Recruits are screened with `--report` first, so a refused PC doesn't waste a session. The observer stays silent, records the screen, times each step, and logs every hesitation over 10 seconds and every question. Can run on lab PCs alongside WP-33 where a recruit's PC is refused. Pass: 4 of 5 finish unaided in under 60 minutes with zero data loss. Proof: `docs/research/people-<date>.md`, with each finding routed to a stream. Repeated after fixes.

### Wishes gate (built now, shipped later)

Wishes is **not in the v0.1 image** (WP-89). These packages are built and tested in parallel and run their CI jobs nightly; the lead reviews v0.1 blockers first.

**WP-44 Wish engine core.** W1 · L · —
Out: `src/aurwish` validates against the v1 schema with the caps. A filter refuses phone numbers, URLs, email addresses, money amounts and a scam word list in any free-text field unless the words came from her. `caps` carries ids only. Undo for every one-off change, plus "Put my computer back how it was". Cycles in the action→event graph are refused, and there are cooldowns and quiet hours (22:00–07:00). It runs as a socketpair child of aurshell with no named socket. wishes.json is HMAC-signed, and a foreign edit puts the wish back to "needs her Yes". A missed reminder is shown on wake. A profile key `wishes` **defaults to off in every profile**; she turns it on through WP-47's consent screen. The volume floor is 10%. Every action shows a "Your wish did this [Undo] [Stop]" card. Proof: one unit test per AI.md §2.2 row. 200+ injection cases refused, and the job goes red if the filter is stubbed. A same-uid attacker script fires nothing. Crafted cycles are refused. profiletest.

**WP-45 Wishes cloud.** CL · L · W1 schema, WP-88
Out: `cloud/` and `cloud.yml`. **Until every proof below passes, the deployed Worker stays with its kill switch on** (`BANNED=*` or equivalent) even though the owner's keys exist. Registration needs a device key plus a small proof-of-work, with per-IP and per-/24 limits. Counters live in Durable Objects. Fair share: an established-token pool gets 80% of the daily spend, and no single token can use more than 0.01% of the cap. Hard caps: 500 characters in, 1024 tokens out. `DELETE /v1/register`. A log canary fails if a request body reaches any log. A request-field whitelist. Deploys roll out gradually, roll back automatically, and need owner approval (`cloud-deploy`). A spend kill switch, and `/v1/status`. Proof: k6 with 10k fresh tokens from 50 IPs plus 1k established; established tokens are answered more than 99% of the time and spend stays under the cap. The canary test. A deliberately bad deploy rolls itself back.

**WP-46 Wish eval and safety policy.** CL · M · WP-44
Out: `tools/wisheval/` holds 400+ utterances: her phrasing, typos, transcription errors, 5 languages, out-of-scope, harmful and injection cases. The gate is at least 95% correct and 0 unsafe. A 60-prompt sensitive set covers scams (never remote access, phone numbers or payments), health, money and loneliness (a local helpline). The model id is pinned in server config, with a retirement plan. It runs on every prompt or model change with a $5 budget. Proof: the eval report in the repo.

**WP-47 Wish UI.** W2 · M · WP-44
Out: a consent screen that names Claude and Anthropic, defaults to No, and uses the same words as the privacy policy. The panel and My wishes. A welcome on-ramp with 3 tappable examples. A second, differently worded screen for risky classes. Proof: screenshots for each class, and a VM e2e where "remind me about my pills at 9" fires at 9.

**WP-48 Wish capabilities are real.** C (P adds packages) · M · WP-44
Out: night light through GAMMA_LUT or a CPU tint, an overlay layer, and a TTS package; anything not built is dropped from the schema. `caps` is generated from runtime probes. `typed_word` uses an in-memory ring buffer of 32 characters or fewer, wiped on every focus change and on the lock screen, and is turned off for non-Latin input methods. Proof: `wishcaps.sh` asserts each action listed in caps changes observable state. A strace check shows no keystroke byte reaches a file or socket.

**WP-49 Wishes privacy.** K with CL · S · decision 8
Out: PRIVACY.md gets a Wishes section naming Anthropic and Cloudflare, what is sent, how long it is kept, and Art. 9 consent. `DPIA-WISHES.md`. A zero-data-retention request to Anthropic. Proof: a lint that the consent text and PRIVACY.md name the same processors.

### v0.1: added after review

**WP-73 Field outcome ping.** CL (endpoint), B and A2 (senders), K (wording) · M · WP-15, WP-37, decision 5
Out: an opt-in tick box on the Ready page, off by default (WP-18). When ticked, AurBridge and aurfirst send `POST /v1/outcome` with fixed values only: version, phase reached, ok or refusal code, a put-back reason picked from a list, SSD or HDD, 2023-CA yes/no (WP-31). No ID; the Worker never stores the IP. It runs on the WP-39 report Worker, not the Wishes one. Daily counts go to a dashboard the owner and the second responder can see. PRIVACY.md is updated first. Proof: a request-field whitelist test (any extra field is dropped). The WP-45 log canary. An e2e run with `die_at` appears as a failure row within 5 minutes.

**WP-74 Launch waves and stop rules.** NEW:OPS with D · M · WP-73, WP-15, WP-35
Out: a Worker on get.<domain> with a daily download cap per wave (about 25, then 100, then 300, then open; decision 21). Each wave widens only after the owner reviews the WP-73 numbers. `docs/ops/LAUNCH.md` holds the stop rules: any suspected data loss pauses downloads automatically; so does a failure-after-shrink rate above 2% or a put-back rate above 15%. When paused or full, a plain "full today, come back tomorrow" page. The owner reviews the numbers every day in week 1. Proof: a Worker test where the cap is reached and the next request gets the wait page. A planted data-loss outcome flips the site to paused within one monitor run.

**WP-75 Upstream updates go through the canary.** S5 · M · WP-28, WP-29
Out: the Ubuntu archive origins and the Firefox PPA are pinned with the deb822 `Snapshot:` field to a snapshot ID read from a signed file on the AurOS channel. The ID moves forward only after the canary ring passes a day-1 smoke test: desktop up, Firefox opens a page, sound plays, printing works. Flathub apps are held to the canary-approved commit (`flatpak update --commit`, or a mirrored remote). A fast lane moves the snapshot the same hour for an urgent security fix. Proof: an updatetest case where a broken Firefox lands in a newer snapshot; fleet VMs stay on the old snapshot until the canary passes. A canary-fail mutant blocks the move. The time for a security fix to reach the fleet is measured and written in UPDATES.md.

**WP-76 Notices to installed PCs.** S5 (feed) and C (card) · S · WP-15, WP-28
Out: a signed `notice.json` on updates.<domain>, signed with the channel key and checked daily. It holds at most one card, with an expiry date, a target version range and plain words that pass plainwords. It shows as a calm card with "OK" and an optional "Show me" that opens a local help page. A notice never carries a link to an outside site or a phone number. Proof: a forged signature is ignored. An expired card is never shown. A planted phone number or URL fails lint. A KVM run shows the card.

**WP-77 Start-up keeper.** BT with B (Windows task), F (variant) · M · WP-23, WP-08
Out: a small Windows startup task, "AurOS start-up keeper", left behind on purpose and removed by Put Windows back. Unless she chose to stay on Windows, it puts AurOS back first in BootOrder, re-creates the AurOS boot entry if it is missing, and shows "Your computer started Windows. [Go back to AurOS now]". AurOS checks the same at its own start-up. Proof: the `windows-once` e2e variant (WP-09). An OVMF case with the NVRAM wiped and the Windows task present recovers. putbacktest asserts the task is gone after Put Windows back.

**WP-78 Favourites come across.** R · S · —
Out: `ferry-chromium` reads `User Data/*/Bookmarks` (plain JSON), the start pages and pinned sites from Edge and Chrome, writes a `bookmarks.html`, and sets `browser.places.importBookmarksHTML` so Firefox imports it on first launch. It never opens `Login Data`, `Cookies` or `Local State`. Proof: a `src/ferry/tests` fixture with Edge and Chrome profiles; the bookmarks appear in `places.sqlite` after first launch. A grep test that the importer never opens a secrets file. A mutant goes red.

**WP-79 Can she open her email?** B with K · S · WP-17
Out: before she commits, a wizard step: "Let's check you can open your email in a web page." It opens her webmail (detected from Edge history) in a private window and asks "Did your emails appear?". If not, she is told to get her password sorted first and given a printable sheet. Proof: navtest for the step. A bridgetest fixture with webmail in the history.

**WP-80 Email kept only on this PC: warn.** B · S · WP-17
Out: AurBridge detects `*.pst` and `*.ost` files, POP accounts in the Outlook profile registry, and the Windows Mail store. WP-17 shows "Some of your emails are kept only on this computer. They won't appear in AurOS yet." Proof: bridgetest fixtures with a fake PST and a POP hive.

**WP-81 English only, honestly.** B and D · S · WP-41
Out: the wizard (`wizard.c` language list) offers only the English locales until WP-66. A non-English Windows gets one honest screen: "AurOS speaks English for now." The website says the same, and the launch waves are limited to English-speaking countries. Proof: navtest checks the language list. A WP-41 claims entry. A lang-de fixture gets the honest screen.

**WP-82 The worst case.** NEW:OPS with A2 (rescue image) and K (wording) · M · WP-39
Out: a Sev-1 lane: an email subject keyword or the verdict code puts the case in front of the owner the same day. We never phone her and we publish no phone number (the never-call rule stands). A downloadable "AurOS Rescue" USB image for her helper: it copies her files off any partition, runs Put Windows back from the `mkrecovery` capture, and writes a diagnostic bundle. `docs/ops/SEV1.md` with steps for photorec and ntfsundelete, and an honest script for what to tell her. Proof: a KVM drill: corrupt the GPT after install, boot the rescue image, recover her files with matching hashes. The drill is timed.

**WP-83 A second responder.** K and the owner, with NEW:OPS · M · WP-35, WP-39, WP-74
Out: a ticket tool with ID auto-reply and labels per verdict id. A library of canned answers keyed to every `install.c` verdict and help page. A named second person who also holds `[apt-halt]` and site-pause rights (halting and pausing only, never publishing). A coverage calendar for week 1, and a written handover: "who does what if the owner is unreachable for 24 hours". Proof: helptest checks that every verdict id has a canned answer. A drill where the second person runs `[apt-halt]` and pauses downloads in under 15 minutes with no help from the owner.

**WP-84 Outside security review.** NEW:owner-legal (budget) with S1 · M · WP-03, WP-28, WP-88, decision 20
Out: a scoped review of about 1–2 weeks by an independent firm (or OSTIF / OTF Red Team Lab funding if eligible). Scope: AurBridge preflight, shrink, GPT and BCD paths; aptchannel signing and WP-03; the update path from snapshot to client; the cloud's abuse and token handling. Each finding becomes a ticket owned by a stream. Proof: `docs/security/REVIEW-<date>.md` with findings and fixes. Wave 2 (anyone beyond the owner's circle) is blocked while a critical or high finding is open.

**WP-85 Launch gate enforced.** W (the lead) with E and D · S · WP-41, WP-42
Out: `docs/LAUNCH-READINESS.md` (named in ROADMAP but missing from the tree) is created: one row per v0.1 box, with the CI job or results file that proves it. `tools/launchgate.sh` parses it. The Pages deploy shows the public Get AurOS button, and the WP-74 Worker allows downloads, only when every row is green. Proof: marking one row red hides the button on the deployed site.

**WP-86 Findings gate.** W (the lead) · S · —
Out: re-run the `auros-safety-review` and `security-audit` workflows against origin/main. `docs/safety/FINDINGS.md` and `docs/security/FINDINGS.md` exist (neither does today). Every blocker or high finding maps to a WP id or is closed by a test. handofftest fails on any finding with neither. This is an entry gate for WP-33. Proof: an unmapped high finding turns handofftest red.

**WP-87 Power cuts, retries, Close and Cancel.** A2 (B for Close and Cancel) · M · —
Out: powercuttest instants for the no-stick mode that ships. A retry after a failure re-reads the existing shrink instead of shrinking again. Close or Cancel at every phase leaves a known, listed state, and the failure messages say what really happened. Proof: a tests.yml powercut matrix entry per instant, plus a "retry after cut" case with a re-shrink mutant shown red. A close-at-each-phase navtest. Hard prerequisite for WP-33.

**WP-88 Keys and secrets, now.** S5 with owner clicks · S · —
Out, before the keyring is committed and before any image carries auros.sources: the update key is re-cut as an offline primary plus a signing subkey (the current single key 7066…1F3B is retired before any PC trusts it). AUROS_APT_SIGNING_KEY, ANTHROPIC_API and CLOUDFLARE_API_TOKEN move from plain repository secrets into the `apt-signing` and `cloud-deploy` environments with the owner as required reviewer. The key backup moves off the Mac to a USB stick (OWNER-SETUP §4a). Proof: a workflow outside the environment that prints the secret's length gets 0. An unapproved run waits.

**WP-89 Wishes left out of the v0.1 image.** W1, W2 and E · S · —
Out: v0.1 images build with Wishes compiled out: no aurwish binary and no hooks in aurshell. imagetest asserts it is absent. Wishes arrives later through the update channel once WP-44 to WP-49 are green. Proof: an imagetest mutant (binary present) goes red.

**WP-90 Wizard contract freeze.** B, F and W · S · —
Out: the wizard's accessible names and string ids are frozen as a contract until WP-08 M3 has been green once. Design changes must keep the contract or announce the change to F in the commit body. Proof: a navtest asserts the contract list; renaming one button turns it red.

**WP-91 Owner track.** NEW:owner-legal (the owner) · S · —
Out: `docs/OWNER-SETUP.md` gets a dated owner track with ETAs: domain and Cloudflare, key re-cut and environment clicks (WP-88), VirusTotal key, LLC, lawyer, Canonical letter, outside review (WP-84), buying the 5 WP-33 PCs (one HDD, one Broadcom Wi-Fi-only, one BitLocker), recruiting for WP-92 and WP-43, the second responder (WP-83), and a SignPath application if decision 2 says so. The lead checks it every session. Proof: handofftest fails on an overdue item with no new date.

**WP-92 Early people check.** NEW:USABILITY with the owner · S · —
Out: now, not at the end: 3 people aged 70+ use the owner's pre-installed AurOS laptop and the wizard (sim or screenshots). Findings go to B, C, D and K before the WP-90 freeze ends. WP-43 then confirms. Proof: `docs/research/people-<date>.md`.

**WP-93 Re-baseline and rewrite the guides.** W (the lead) · S · —
Out: before any package is handed out, mark it done, partial or open against origin/main HEAD (section 2 already does this for WP-01, 02, 11, 26, 28, 31). Rewrite `docs/ROADMAP.md` (the signed channel is v0.1 here, not v0.3), `START-HERE.md` §6 and each `docs/handoff/streams/*.md` "Next steps" in terms of WP ids, so a respawned stream works in this order. Add a dated chart of the three chains in section 3 to ROADMAP.md. Proof: handofftest (WP-42).

### v0.2

**WP-50 D11 BitLocker pause plus fast path steps 1-5.** A2 · L · WP-11. Start now (decision 19). Out: suspend protectors with `-RebootCount 2`. Every change that alters PCR7 is deferred, or preceded by a recovery-key page and her Yes. **Entry gate for switching the wizard to the new engine:** WP-08 base, WP-33 on at least 3 machines, and a power pull, all re-run on the new engine. Proof: the e2e bitlocker variant, including applying a db update, plus the re-run results.

**WP-51 e2e matrix.** F · M · WP-08. Out: slownet at 10 and 4 Mbit/s, avast, lang-de, ram4, dpi150, hdd budgets, sac-on as a gate (it is measurement-only in v0.1), and a monthly run after Patch Tuesday that applies Windows' Secure Boot servicing and asserts AurOS still starts. `MATRIX.md` is generated on promote. Proof: the timing table, with the 60-minute budget enforced at 10 Mbit/s.

**WP-52 Real-PC round 2.** NEW:HW · L · WP-33. Out: 15–20 machine classes, a PiKVM bare-metal runner in the release gate, an opt-in hardware report to `/v1/hw` aggregated nightly into compat.json, and a "Tested computers" page. Proof: at least 13/15 rows green. A schema allow-list test.

**WP-53 Firmware lifecycle.** BT · L · WP-31, WP-50. Out: fwupd 2.0.13 or newer, LVFS stable only. db/KEK/dbx updates apply only after bootchain proves the new state still starts shim, GRUB, every kernel and staging.efi. A BitLocker-aware deferral. BIOS capsules apply only on AC power with a plain notice. staging.efi is refreshed whenever the shim moves. AurOS-Setup gets a "Repair start-up" mode. Proof: OVMF with only the 2011 CA → update → `dpkg` upgrade of shim-signed exits 0 → Secure Boot boot → Put Windows back. A raised SbatLevelRT test.

**WP-54 HWE kernel.** P (with E and A2) · M · WP-30. Out: linux-generic-hwe-24.04 with GA kept as "AurOS (safe)". build/staging takes the kernel version from the profile and fails if more than one matches. Proof: every boot test green, plus 2023+ lab laptops.

**WP-55 Desktop grows up.** C · L · WP-20. Out: integer HiDPI scale taken from the panel's millimetre size; touch forwarding plus iio-sensor-proxy rotation; the Canonical-signed Nvidia driver installed by GPU ID, with RTD3 on Optimus laptops; a 60-second guided check before Yes; a ScreenCast portal with an un-hideable red Stop bar (rules from S2); a "Your phone is plugged in" import card; a 2 GB low-space warning. Proof: a QEMU test at 2880x1800 checking text cap height; a wl_touch client; lab Nvidia rows; welcometest; the parity screen-share check.

**WP-56 Full parity.** NEW:PARITY (with S2 and C) · M · WP-34. Out: the Widevine demo plays with an `EncryptedMediaExtensions` policy; notifications allowed for a vetted list of sites; a 10-form PDF test that settles the default viewer; `sites.md` (the top 25 sites for over-65s); `media-perf.txt` (fewer than 5% dropped frames and under 80% CPU on a 2013 laptop); a 40-app table; a Flathub-down test with the plain sentence; scanning (simple-scan, sane-airscan).

**WP-57 Keep a copy of my things.** NEW:BACKUP · L · —. Out: plug in a USB drive and accept one offer, and incremental, encrypted copies run from then on. A card after 30 days with no copy. "Get my things back" on a fresh AurOS. A smartmontools daily check whose "drive wearing out" notice opens the backup. Proof: a KVM backup, wipe, restore with matching hashes. A power cut mid-backup leaves the previous set usable. smartctl JSON fixtures.

**WP-58 Repair and space.** A2 · L · WP-57. Out: a "Repair AurOS, keep my files" GRUB entry and exe mode that rewrites root from a verified image and keeps /home. A "give AurOS the space Windows uses" flow. Ferry checks space first. Proof: corrupt /usr, repair, /home hashes unchanged. A shrinktest.

**WP-59 PIN that can never lock her out.** S4 · M · WP-57, decision 16. Out: the flow cannot finish until she proves she kept the rescue code. Optional family QR escrow. "Forgot PIN?" Encryption is offered only once a backup exists. Proof: theft.yml cases.

**WP-60 Scale the plumbing.** NEW:OPS with E · M · WP-15. Out: pieces mirrored to R2 and read back by release.yml; the site behind Cloudflare with a 350 KB first-load budget in check.mjs; `COSTS.md` at 100, 10k and 1M users; a `[hotfix]` lane taking a commit to the channel in 60 minutes or less, rehearsed 3 times; nightly build, weekly refresh, daily debsecan/OSV scan; the repo moved to an org with 2 owners on hardware-key 2FA. Proof: an e2e run with github.com blackholed still completes. k6 against the mirror. Drill timings recorded.

**WP-61 Wishes v1.1.** CL, W1 and W2 · L · WP-44–49. Out: Ask AurOS grounded in the help pages, with an offline search fallback. Local matching for the top 30 intents, with 100% precision. Vocabulary v2 (call_contact link button, read_aloud, cursor size, magnifier, keep_awake). Push-to-talk voice with whisper tiny (3 seconds or less on the 2013 laptop). Version negotiation. A Bedrock or Vertex fallback with circuit breaker. A remote pause list signed with S5's key. A cost model with daily quotas. Proof: the eval, an offline VM test, a 429-storm load test, `docs/results/voice.txt`.

**WP-62 Reproducible builds and a stable installer.** E (with A2) · L · —. Out: a snapshot.ubuntu.com timestamp per release, SOURCE_DATE_EPOCH carried through the whole build, and a manifest signed with an ed25519 key so the exe only changes when installer code changes. Proof: two builds produce the same image sha. Two releases produce a byte-identical exe.

**WP-63 Security coverage.** S1 and S5 · M · —. Out: a universe-package audit with a budget; parsers she touches moved to main or to auto-updated Flathub apps; an allow-list of vendor .deb repos; a declared support period. Proof: the audit runs in tests.yml.

**WP-64 Code signing.** E · M · decision 2. Out: build/sign uses SignPath, and release asserts `Get-AuthenticodeSignature == Valid`. Proof: the sac-on variant is green.

**WP-65 Names.** NEW:owner-legal · S · WP-37. Out: AurOS trademark knockout search; Mozilla's written OK to keep the Firefox name, or the tile is labelled "Internet"; Apple and Mac added to the notices.

**WP-72 (minimal) Let someone I trust help.** S2 with C · M · WP-55 (red Stop bar). Moved from v1.0. Out: a one-time code, view-only by default, a permanent banner, control only when she grants it, everything logged, S2's rules. Proof: a KVM test where the helper can see but not click until she presses "Let them help"; the banner cannot be hidden; the log lists the session.

**WP-94 Lookalikes and self-check.** NEW:OPS with B and S2 · S · WP-15. Out: a weekly lookalike-domain and name check (certstream or a dnstwist report). The wizard checks its own sha256 against the signed SHA256SUMS on the owned domain and says "This copy isn't from AurOS" on a mismatch. Proof: a mismatching-exe fixture shows the warning.

**WP-95 Put Windows back after months.** B with K · S · WP-16, WP-27. Out: the put-back page says how many months Windows hasn't been updated and whether it is Windows 10 (past end of support), then offers "Keep AurOS" or "Continue". The How-it-went page in Windows says "Updates may take an hour; leave it plugged in." Proof: a putbacktest wording case with the clock moved forward 180 days.

**WP-96 Two people, one PC.** C with R and S1 · M · WP-26. Out: a "Someone else uses this computer" setting with two named homes and a picker at start-up. Ferry offers to bring each Windows user across. Proof: a KVM test with two homes, a ferry run with two users, and WP-26's polkit rights for both.

**WP-97 Passwords and old mail come across.** R with S1 (passwords), R (mail) · M · WP-79, WP-80. Out: a guided Edge or Chrome "Export passwords" (Windows Hello prompt) into an encrypted journal entry that Firefox imports on first boot, then shreds; ships only after an S1 threat note. `readpst` converts PST to mbox for Thunderbird. Proof: a test that the plaintext never touches the disk. A PST fixture becomes a Thunderbird folder with matching message count.

### v1.0
**WP-66** NEW:i18n: gettext for aurshell, aurscreen, aurstage, the wizard string table, help and the site in 5 languages, plus right-to-left layout; a screentest per language that finds no English and no overflow at 1024x600. Lifts WP-81's English-only limit (L).
**WP-67** NEW:a11y with C: AT-SPI, Orca with a single-key toggle, a high-contrast theme, keyboard-only use, a11ytest, an EN 301 549 statement (L).
**WP-68** S5 (later NEW:BASE): per-series channel suites, an unattended 24.04→26.04 upgrade proven in KVM with Secure Boot, an SPDX SBOM per release, and `docs/security/CRA.md` (L).
**WP-69** NEW:business with K: merchant of record, sales terms, refunds, a paid support line, a runbook per verdict enforced in CI, an E&O insurance quote (M).
**WP-70** NEW:W3: `docs/MINIPROGRAMS.md` with a declarative-cards → WASM ladder, red-teamed before WASM ships (L).
**WP-71** NEW:audio and C: hearing-aid ASHA/LE Audio through a PipeWire 1.4+ backport; extended desktop with a choice of main screen (L).
**WP-72** S2: family remote help grows beyond the v0.2 minimum (unattended help with her standing permission, several helpers) (L).

---

## 3. Critical path to v0.1, and what starts now

**Critical path: three chains that run side by side and meet at promote.** The first draft put the real-Windows e2e (WP-08, the longest pole) behind the domain and the update channel. It doesn't need them: `e2e.yml` takes the installer address as an input, and the release chain needs no channel. So:

- **Chain A, install proven (F, the longest pole):** WP-90 contract freeze → **WP-08** M2 then M3 green on today's prerelease (Edge flyout stalls, cache warm-up and runner contention) → one more base run on the exe with owned addresses.
- **Chain B, addresses and updates (owner, OPS, S5):** owner buys the domain and does the WP-88 key re-cut and environment clicks (day 0) → **WP-15** owned addresses baked everywhere → **WP-28** channel live with auros.sources and the keyring in the image → **WP-07** upgrade rehearsal on a prerelease → **WP-04a** withdrawn check in the exe.
- **Chain C, safety set (A2, B, BT):** **WP-10**, **WP-11**, **WP-13**, **WP-14**, **WP-16**, **WP-27**, **WP-87**, **WP-30 minimal**, **WP-86** findings gate.

A + B + C → **WP-01** promote and Pages. A + C → **WP-33** five real PCs on the prerelease (alongside WP-01, not after it). WP-33 + WP-30 minimal + WP-12, WP-17, WP-26 → **WP-43** five people. Then **WP-85** launch gate all green, **WP-84** review done, **WP-74** wave 1 opens.

**The owner track runs alongside from day 0** and is the most likely thing to slip, because each item takes weeks of someone else's time: domain, key re-cut and clicks, VirusTotal key, LLC, lawyer (nothing goes public before the review), Canonical letter, outside security review, buying 5 PCs, recruiting people, naming the second responder, and the BitLocker scope (decision 19). All of it is dated in `docs/OWNER-SETUP.md` (WP-91).

What can land late without moving the date: anything not on those chains, as long as it lands before WP-33 starts. The next-longest chains are WP-30 in full (L), WP-20 display robustness (L) and WP-32 (L). Start them today.

**Hard ordering rules** (handofftest checks the WP dependency lists against them, WP-42)
1. Do not promote an installer, and do not let any real person install, before WP-15. A machine installed earlier stays pointed at GitHub for good.
2. Do not install on any machine that isn't the owner's before WP-10, WP-11, WP-13, WP-14, WP-16, WP-27 (reverse Ferry), WP-87 and WP-30 minimal are green.
3. Do not let anyone outside the owner's own PCs and the observed sessions take updates until WP-29, WP-30 (full) and WP-75 are green. Phasing alone cannot hold back Ubuntu's own kernels; boot counting and the snapshot canary can.
4. No image carries auros.sources or the keyring until WP-88 is done. A key structure that PCs already trust can only be changed by rotating a keyring on every one of them.
5. No `[latest]` until WP-07 is green on a prerelease N-1.
6. The Wishes Worker stays with its kill switch on until WP-45's proofs pass; the v0.1 image carries no Wishes code (WP-89).

**Start immediately (no unmet dependencies):**

| Stream | Start now |
|---|---|
| Owner | WP-88 key re-cut and environment clicks, domain (decision 1), WP-91 owner track, decision 19 |
| W (lead) | WP-93 re-baseline and guide rewrite, WP-86 findings gate, WP-41, WP-42, WP-85, WP-90 with B and F |
| E | WP-01 residual, WP-02, WP-03 (SHA pins, attest, point-of-use protection), WP-06, WP-89 with W1/W2, WP-05 once the key exists |
| F | WP-08 M2 on the current prerelease, ISO pin, journey clock; the `sac-on` measurement as soon as base is green |
| A2 | WP-10 (with the DST cases), WP-11 lint, WP-87, WP-13, WP-14, WP-12; WP-50 in parallel (decision 19) |
| B | WP-16, WP-18, WP-04a (the check, ready for the domain), WP-79, WP-80, WP-81; WP-17 (a) and (b) |
| C | WP-19 (look bug and idle inhibit first), WP-20, WP-21, WP-23 |
| P | WP-25 |
| S1 | WP-26 polkit test first, against today's image |
| S5 | WP-88 with the owner, then WP-28 origins, flatpak timer, -updates; WP-29; WP-75 |
| R | WP-27 sandbox fix, WP-78 |
| K | WP-36, WP-38, WP-37 placeholders, WP-49, WP-83 canned answers |
| D | WP-40 wording fixes (time stays a placeholder until F has numbers) |
| CL | WP-45 kill switch on now; WP-39 report Worker route; WP-73 endpoint skeleton with the field whitelist |
| W1 | WP-44 filter, undo and IPC |
| W2 | WP-47 consent screen |
| BT (respawn if not live) | WP-30 minimal first, then WP-31 residual, WP-77 |
| Spawn | NEW:OPS (WP-15 prep, WP-35 against the current URLs, WP-82, WP-74 skeleton), NEW:HW (WP-32 `--report` first), NEW:PARITY (WP-34), NEW:USABILITY (WP-92 now; script and recruiting sheet for WP-43) |

**Waits:** WP-07 (needs WP-28), WP-09 (needs WP-08), WP-33 (needs WP-08 M3 and chain C), WP-43 (needs WP-33), WP-04b (needs WP-29), WP-24 and WP-17 (c) (need WP-32), WP-73 and WP-74 (need WP-15 and WP-37), WP-76 (needs WP-28), WP-84 (needs WP-03, WP-28, WP-88).

**CI capacity:** 20 jobs, shared, and GitHub has no priority queue. So priority is made by trigger, not by hope:
- On push: build, unit, the v0.1 safety suites and e2e base only.
- Nightly, never on push: profiles other than desktop (the other editions are v1.0 work), othersys, fuzz, PARITY, the e2e variants, the Wishes jobs.
- OPS's monitor uses one tiny job.
- H stays paused.
- One extra e2e lane on homebase is allowed only as an ephemeral, label-gated runner that runs `e2e` against an already-built exe, never builds or signs, holds no secrets, and never runs code from a fork. Container capped with `mem_limit`, with the RUNBOOK checks (`free -h`, `tmux ls`, `docker ps`) done first.
- Proof: the queue wait per e2e job, printed in the job summary, stays under 10 minutes for a week.

---

## 4. New streams to spawn (paste-ready)

Every brief inherits the common rules in `docs/handoff/STREAMS.md`, with `FIELD-MANUAL.md` replacing its environment rules.

**Save rule for all of them: commit and push to `main` after every green step, and at least every 15 minutes of work (the owner's rule: "push extremely frequently so I have a safe"). Before any step longer than 10 minutes, push and append one line to the Progress log in `docs/handoff/SESSION-2026-10-23-B.md` (append only; on a rebase conflict keep both lines). Keep `docs/handoff/streams/<ID>.md` current. Unpushed work is lost work.** Refuse first. Prove every check fails when broken. Plain words. Budget: a 2013 laptop with 4 GB. Before starting a package, check it against origin/main HEAD; some are already partly done.

**OPS: domain, monitoring, incidents, launch waves, distribution.**
Goal: no installed PC depends on a GitHub address; the owner learns about a broken download, update or cloud within 15 minutes and has a rehearsed fix; the launch opens in waves that stop by themselves when something goes wrong.
Owns: `infra/**` (Cloudflare redirects and Workers for get/updates/www/status, DNS as code, the wave cap), `.github/workflows/{monitor,drill,fleet}.yml`, `docs/ops/**` (INCIDENTS, LAUNCH, SEV1, drills), `tools/ops/**`. Smallest possible edits in E, A2, S5, M and CL files for WP-15, announced in the commit body.
Milestones: (1) WP-15 redirects and the repotest scan of rootfs, initramfs and exe. (2) WP-35 monitor, push alert to the owner and the second responder, status page. (3) INCIDENTS.md and a drill under 30 minutes. (4) WP-74 wave cap and stop rules; WP-82 Sev-1 lane and rescue stick (with A2); WP-83 drill with the second responder. (5) fleet.csv. (6) v0.2: WP-60 R2 mirror, CDN, COSTS.md, org move checklist; WP-94 lookalike watch.
Proofs: every monitor check mutated and paging; a planted github.io string in each of the three places goes red; updatetest "moved" case; a planted data-loss outcome pauses the site within one run; rescue drill recovers files with matching hashes; drill timings in `docs/ops/drills.md`.

**HW: will her PC work, and the lab.**
Goal: before anything changes, she knows whether her Wi-Fi, sound, camera, printer and scanner will work, and the owner has real-PC proof.
Owns: `hw/**` (compat.json, generator, matcher `hw/lib/hwcompat.{c,h}`), new `src/aurbridge/hwinv.{c,h}` (B calls it, A2 adds the journal field), `tools/hw/**`, `docs/results/hardware/**`.
Milestones: (1) device and printer inventory plus `--report`, landed first, so the owner can screen WP-33 machines and WP-43 recruits and measure the BitLocker share. (2) compat.json from modules.alias, firmware and printer drivers, plus seeds (Broadcom wl, IPU6/7, Nvidia). (3) matcher wired to B's screen and A2's staging refusal. (4) WP-33 with the owner, on the prerelease exe once WP-08 M3 and the safety set are green: at least 5 PCs. (5) v0.2: WP-52 with 15–20 machines, PiKVM runner, opt-in field report; the 3-machine re-run after the WP-50 engine swap.
Proofs: matcher unit tests; BCM4352 fixture refused; a printer that won't work gets the plain sentence; one results file per machine, generated into a table.

**PARITY: everyday tasks actually work.**
Goal: a call, a film, a print job, a Word file and her top apps are proven every night, not assumed.
Owns: `tools/parity/**`, `tools/apptest*`, `.github/workflows/parity.yml` (nightly, `concurrency`, max-parallel 1, never on push), `docs/results/{apps,sites,media-perf,pdf-forms}.md`.
Milestones: (1) WP-34 checks (a), (c), (d), (e). (2) apptest top 10. (3) v0.2: Widevine, 40 apps, sites campaign sheet for the owner, media-perf, Flathub-outage, scanning.
Proofs: each check shown red once by removing its package; screenshots in the evidence zip, published as a prerelease asset.

**BT: AurOS always starts.** (Existing stream from the 05:40 routing; respawn with this brief if it isn't live. The first draft's NEW:BOOT is folded in here so two streams never own the same files.)
Goal: no update, firmware change, Windows patch or BIOS update leaves her with a black screen or lands her in Windows with AurOS seemingly gone.
Owns: `src/aurfirst/**`, `src/recovery/**`, `rootfs/usr/lib/auros/{bootchain,bootchain-apt,grub.cfg.in,answer.sh,hold*,putback*}` (S1 keeps the cmdline line), the auros-answer units, `rootfs/etc/systemd/system/auros-boot-ok.service`, `rootfs/usr/lib/auros/rollback*`, `rootfs/etc/fwupd/**`, `tools/{bootchaintest,bootupdatetest,rollbacktest,fwupdtest}.sh`.
Milestones: (1) WP-30 minimal: boot_try, boot-ok, fall back to the previous kernel (this blocks every install outside the owner's PCs). (2) WP-30 full: safe graphics, previous debs, the boot-4 menu. (3) WP-77 start-up keeper, with B for the Windows task and F for `windows-once`. (4) WP-31 residual: CA and SBAT recorded. (5) v0.2: WP-53 fwupd and staging.efi refresh.
Proofs: panic (first), hang and crash-loop cases recover with no input; counter mutant goes red; `windows-once` red before WP-77 and green after; OVMF 2011-CA fixture.

**USABILITY: real people.**
Goal: "never confused" is measured on people aged 70+, early and again at the end, not inferred from scripts.
Owns: `docs/usability/**` (script, scoring sheet, consent form, recruiting note), `docs/research/people-*.md`.
Milestones: (1) WP-92 now: 3 people on the owner's pre-installed laptop and the wizard sim; findings to B, C, D and K before the WP-90 freeze ends. (2) script and sheet for WP-43. (3) the owner recruits 5 people, each screened with `--report`. (4) sessions after WP-33. (5) findings routed to streams. (6) a re-run after fixes.
Proofs: per-step stopwatch table; pass is 4/5 unaided in under 60 minutes with zero data loss.

**BACKUP: keep a copy of her things.** (Spawn after v0.1 is on track.)
Goal: a dead disk, a theft or a forgotten PIN never takes her photos.
Owns: `src/keepsafe/**`, `rootfs/usr/lib/auros/keepsafe*`, `rootfs/etc/systemd/system/auros-keepsafe*`, `auros-diskhealth*`, `tools/keepsafetest.sh`. C adds a one-line hook for the cards.
Milestones: (1) USB offer and incremental copies. (2) Restore on a fresh AurOS. (3) SMART notice. (4) S4 hand-off: a backup exists before encryption is offered.
Proofs: backup, wipe, restore with matching hashes; power cut mid-backup leaves the previous set intact; smartctl fixtures.

Later, not spawned now: i18n, a11y, BASE, W3, business, audio (v1.0).
Ask AurOS stays inside W2 (panel) and CL (endpoint). Support stays inside K, with D and CL; the second responder is a person, not a stream (WP-83).

---

## 5. Decisions only the owner can make

Each has a recommendation. Decisions 1, 19 and the WP-88 clicks are day 0.

1. **Buy a short domain and a Cloudflare account now.** Recommend yes, today, before any promote. It is the root of WP-15, 35, 37, 39, 60, 73, 74 and 76.
2. **D7, no code signing.** Smart App Control makes an unsigned exe impossible to run on new Windows 11 PCs, and our site currently coaches her through the same "switch off protection" steps a scammer would. Recommend applying to SignPath Foundation now, because an application takes weeks; it is free, the key stays in an HSM, it signs from Actions, and it uses no Microsoft service. Keep the honest Smart App Control path until it lands. The v0.1 `sac-on` measurement (WP-09) gives the share of target PCs it blocks, so the decision can be revisited on data.
3. **D7 scope.** Recommend that VirusTotal and the antivirus vendors' false-positive portals are allowed. Microsoft's false-positive submission portal is also allowed, because it is a form we fill in, not a dependency on her PC.
4. **S mode.** Recommend not guiding her out of S mode in v0.1 (it is one-way and goes through the Microsoft Store). The site says so honestly. Revisit in v0.2.
5. **Field signal.** Recommend yes to WP-73 before wave 1: an opt-in outcome ping, unticked by default, carrying fixed values only (version, phase, ok or refusal code, put-back reason, SSD/HDD, 2023-CA yes/no), with no ID and no stored IP. Plus download counts. PRIVACY.md is changed before it ships. Without it, the wave stop rules (decision 21) have nothing to read.
6. **Support.** Recommend support@<domain>, a 2-business-day reply time, same day for anything marked as possible data loss (WP-82), the owner plus one named second responder (decision 22). No AurOS phone number is ever published (it is exactly what scammers would copy; the never-call page stays true). Public GitHub issues stay for developers only.
7. **The shared "auros" password.** Already gone: the account is locked in forge. Recommend no password at all until she sets a PIN (S4), with polkit giving the active local user her everyday actions, after WP-26's test lists every action that would prompt.
8. **Wishes at launch.** Recommend v0.1 ships without Wishes in the image (WP-89). It arrives through the update channel for the owner's family cohort once WP-44 to WP-49 are green, then widens. The model is the cheapest one that passes the eval (likely Haiku-class). Ask Anthropic for zero data retention. Set a hard monthly spend cap (for example $200) until the cost model exists. Until WP-45's proofs pass, the deployed Worker keeps its kill switch on.
9. **Legal.** Recommend forming an LLC now, buying one lawyer review (Terms, Privacy, Disclaimer, EU product liability, imprint), and getting an E&O insurance quote. Nothing goes public until the review is done.
10. **Canonical.** Recommend sending the "based on Ubuntu" letter now, without waiting for the reply.
11. **Real-PC gates and budget.** Recommend at least 5 machines for v0.1 (one HDD, one Broadcom Wi-Fi-only, one BitLocker) and at least 13 of 15 for v0.2. About $2–3k for used PCs plus a PiKVM.
12. **The "free forever" promise.** Recommend rewording it to "AurOS itself stays free". Paid offers would be human help and Wishes beyond a free daily quota.
13. **Account risk.** Recommend moving the repo to a GitHub org, with a transfer so redirects are kept, with 2 owners on hardware keys. Agents get a fine-grained token, not the owner's session.
14. **Office and calls.** Recommend LibreOffice Writer and Calc as debs from main, so they get Ubuntu's security fixes. The call browser would be Flathub Chromium, installed the first time she needs it.
15. **Automatic restarts.** Recommend asking her once. After that, restart at 03:00 on AC power after 7 days pending, with no unsaved work.
16. **Rescue code (S4).** Recommend a printed sheet plus an optional family QR code, never stored online. Encryption is offered only when a backup exists.
17. **HWE kernel.** Recommend switching in v0.2, with GA kept as the safe entry.
18. **Who approves fleet publishes.** Recommend the owner clicks approve for every `apt-signing` and `cloud-deploy` run. One click per publish is the price of root access to every PC. The second responder can halt and pause, never publish.
19. **BitLocker PCs in v0.1 (day 0).** Most new Windows 11 laptops set up with a Microsoft account have device encryption on, and today every BitLocker PC is refused. Two choices: (a) pull WP-50 onto the v0.1 critical path; (b) scope wave 1 as "not on BitLocker PCs yet", said on the site and by `--report` before she downloads, with WP-50 worked on in parallel. Recommend (b) for waves 1 and 2, with WP-50 started today so it can join before the link opens fully. WP-33 records the share of machines refused, so the choice can be revisited on data.
20. **Outside security review.** Recommend paying for a scoped 1–2 week review (or applying for OSTIF / OTF Red Team Lab funding) now, so it is done before wave 2. Agents wrote the code that shrinks her disk and signs root updates for every PC; fuzzers find crashes, not design flaws.
21. **Launch waves.** Recommend about 25 downloads a day, then 100, then 300, then open, English-speaking countries only, each step only after a daily review of the WP-73 numbers. Stop rules: any suspected data loss, failure-after-shrink above 2%, or put-back above 15% pauses downloads automatically.
22. **A second responder.** Recommend naming one trusted person before wave 1, with halt and pause rights only, a week-1 coverage calendar and a 24-hour "owner unreachable" handover.

---

## 6. Risks that could sink the product

1. **The VMs can't see real-hardware failures.** OEM firmware ignoring BootNext, RST, eMMC and odd ESPs could cost someone her Windows or her data on PC #3. Mitigation: WP-87 in simulation first, WP-33 before the public link (on the prerelease, as early as possible), WP-52, bare metal in the gate.
2. **A bad update hits the whole fleet.** Daily unattended upgrades with no phasing and no boot counting turn one bad deb, one bad Ubuntu kernel or one bad Firefox into every grandmother's black screen on the same morning. Kernels already update this way today. WP-30 (minimal first), WP-29 and WP-75 are non-negotiable before strangers install.
3. **Supply chain through our own process.** 15+ agents push straight to main, actions are tag-pinned, and the signing key is today a plain repository secret any workflow can read. One poisoned commit or action gives root on every PC. WP-88 now, WP-03, owner approval per publish, WP-84.
4. **Single points: GitHub, one account, one repo name.** A rename, suspension or throttle cuts off downloads and security fixes for everyone already installed. WP-15 must land before the first install. WP-60 adds the mirror and the org.
5. **She never gets started.** Smart App Control, antivirus quarantine, SmartScreen, S mode, BitLocker refusal, and a "10 minutes" promise that turns out to be 45 could stop most of the target users before the wizard opens. WP-05, WP-40, decisions 2 and 19, WP-09 sac-on measurement, WP-51 avast.
6. **The Secure Boot ecosystem is changing.** The 2011 CA expiry, SBAT revocations and dbx updates arriving from Windows can stop AurOS starting months after install, or trigger BitLocker recovery. Windows and BIOS updates can also move Windows first in the boot order. WP-31, WP-53, WP-23, WP-14, WP-77.
7. **A Wishes incident.** Prompt injection makes her own computer show a scam number, her health words leak, or one abuser drains the budget so Wishes dies for everyone. Wishes is out of the v0.1 image (WP-89), the Worker stays killed until WP-45 passes, and support reports never depend on it (WP-39).
8. **Legal exposure.** GPL non-compliance, Canonical's IP policy, trademarks, and absolute "you won't lose files" claims under the EU product liability rules from Dec 2026. All of it lands on the owner personally until an entity exists. WP-06, WP-37, WP-40.
9. **Nobody answers when it goes wrong.** A tragic failure with no reachable human ends trust permanently. WP-39, WP-35, WP-82, WP-83, decisions 6 and 22.
10. **We don't know it's going wrong.** Without field signal, one failure in 20 shows up as tickets days later. WP-73, WP-74 waves with automatic stop rules, WP-76 notices.
11. **Execution drift.** 17+ parallel streams, a saturated 20-job CI, stale handoff files, guides that still give the old order, and the owner as the bottleneck for clicks and decisions. Mitigations: WP-93 re-baseline, WP-02 green ref, WP-42 handofftest, WP-85 machine-enforced launch gate, WP-91 owner track, CI by trigger (section 3). Pushing to GitHub at least every 15 minutes keeps progress safe across sessions.
12. **Money.** Free software plus per-wish API costs plus support hours plus a review and a lawyer, with no revenue until v1.0. WP-60 COSTS.md, decision 8 cap, decision 12.

---

## Appendix: critic points rejected or changed

Every other point from both critics was accepted as written and is folded in above. Completeness points are C-G*n*; priority points are P-G*n*.

- **C-G10, "she or her helper calls a published number".** Rejected. A published AurOS phone number is exactly what scammers would copy, and `never-call.html` and SUPPORT.md tell her we never phone. Kept: the same-day lane through the email subject or verdict code, the rescue stick and SEV1.md (WP-82).
- **C-G3, packages.mozilla.org through snapshot.ubuntu.com.** Changed. The snapshot service covers the Ubuntu archive and Launchpad PPAs, not Mozilla's own repo. Firefox comes from the mozillateam PPA instead, so the same `Snapshot:` gate applies (WP-28, WP-75). Flathub is held by commit, as the critic proposed.
- **C-G12, "promote is blocked while any critical or high finding is open".** Changed. The review needs the owner's money and an outside firm's calendar, and promote is also how the owner's own machines and WP-33 get tested. The block is on wave 2 (anyone beyond the owner's circle), not on promote (WP-84, decision 20).
- **P-G18, "one self-hosted e2e lane on homebase".** Changed, not rejected. The repo is public, and a general self-hosted runner on a public repo can run untrusted code next to the owner's other services. Allowed only as an ephemeral, label-gated lane that tests an already-built exe, holds no secrets, never builds or signs, and never runs fork code (section 3). The `hb run` line is fixed to match what is really installed rather than picking a side here, because the global notes and the session log disagree on whether `hb` exists (WP-42).
- **P-G19, the claim that path-based push rules exist only for private and internal repos.** Not verified here, and not needed: the remedy (protect at the point of use with environments and a `green` check, keep pushing straight to main) is right either way and is adopted in WP-03.
- **P-G23, "release.yml:393 already gates on every e2e job".** Partly wrong on `75e3321`: the check is at line 117 and requires at least 5 `e2e (…)` jobs plus every `tests / …` job green. WP-01 is marked partial, and its residual is to compare the count with the variants asked for.
- **P-G24, "forge:1358 (`chpasswd auros`) is still there".** Out of date: `build/forge:1434` now locks the account and root. The useful part, build the polkit test first against today's image, is adopted in WP-26.
- **P-G5 "WP-13b" and P-G16 "WP-43a" names.** Renamed to WP-87 and WP-92 so every ID stays a plain number; the content is unchanged.
- **C-G7 v0.2 password import.** Kept, but only behind an S1 threat note and a test that plaintext never reaches the disk (WP-97). It is the one step in this plan that handles her secrets.
