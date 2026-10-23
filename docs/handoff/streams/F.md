# Stream F: install AurOS from a real Windows, end to end (handoff)

Updated 2026-10-23 13:30 UTC (session B). Repository
https://github.com/aarohkandy/v0.0.1-auros, branch `main`.
Files: `.github/workflows/e2e.yml`, `tools/e2e/**` (nothing else).
Full user guide: `tools/e2e/README.md`. This page is the state of the work.

## Session B: now (updated 2026-10-23 13:30 UTC)

| What | State |
|---|---|
| Installer under test | this repo's first prerelease `v0.1.0-618726b` (E), pinned in e2e.yml (`7f52d4c`..), SHA-256 `5b875c0b...0b52`. It predates A2's raw-disk BitLocker read (`619d439`). |
| Journey so far (runs 39923720924, 39931612628; evidence `releases/download/e2e-<RUN>/e2e-evidence-base.zip`) | Windows to desktop (1:20) -> Edge: website, Get AurOS, the whole Keep/Keep anyway dance (0:17), SHA-256 matches -> Open file -> UAC (answered) -> wizard Next, Next, Install AurOS (2:18, image download+check on a runner) -> Restart now -> Windows restarts, **firmware starts the staging installer under Secure Boot** -> staging **refuses**. Machine time Get AurOS -> Restart pressed: **4:19**. |
| Refusal 1 (run 39923720924) | `verdict=bitlocker`: Windows 11 turned device encryption on by itself (`EncryptionInProgress 93%` at first logon, TPM+Secure Boot). Fixed on the harness side: the plain PC now has `PreventDeviceEncryption` (specialize.cmd) and the flow refuses a plain PC with BitLocker (`bitlocker_verdict`, selftested). Golden now says `FullyDecrypted` (run 39927613385 facts). |
| Refusal 2 (run 39931612628) | `verdict=cannot-measure` (`src/aurstage/install.c:804`: `ntfsresize --info --no-action` failed on C:). The screen shows only the verdict word. **Run 39959768382** (`2b0f...`) reads the disk with the machine off after a failed flow: `diag/disk-inspection.txt` in the evidence zip has ntfsresize's own words, `ntfsfix --no-action`, C:\AurOS and the ESP's small files. Read that first. Suspects: NTFS left unclean / hibernated by Fast Startup (this PC has it on), or a Windows 26H2 NTFS feature this ntfs-3g does not know. If it is Fast Startup: the installer's restart must be a full one (and the staging should say "hibernated" in words). |
| Cache | the golden is **18.7 GB** and this repo's whole cache limit is 10 GB (limit API: 402): it is evicted at once, every run installs Windows afresh (+13-17 min). homebase has only 22 GB free: not an option. Owner click if wanted: more cache room. |
| bitlocker variant (D13) | `[e2e:bitlocker]`: red at the plain stop with `variant bitlocker: STOPPED AT ITS STOP ... D13` (`close_verdict`, selftest-tied to both titles), today also needs a prerelease with `619d439` to even reach that stop. When A2's pause lands: the driver reads `Get-BitLockerVolume` before Restart now (must be FullyEncrypted). **Still to write, needs the real pause to test**: after M3, boot Windows directly (OVMF boot menu or AurOS's own) and via "Put Windows back"; each: no recovery-key screen (OCR "recovery key"), `helper-up`, `ProtectionStatus On`. The bitlocker golden itself (TPM + recovery password, protection on) is made by `firstlogon.ps1`; untested until its first run. |
| hdd variant | the guest now sees a 7200 rpm drive (`rotation_rate`), throttled to 100 MB/s, 120 IOPS; expectation: the Ready page says "older, slower kind of drive" (selftest ties it to wizard.c). `timings.md` ends with the journey legs. Not run yet. |
| Driver | installer buttons by name (MSAA via the elevated helper; primary = key Enter), colour fallback for teal and green; "none readable" in the first runs, so a once-only broad look is logged (`installer window as UI Automation sees it:`, in ui-elements.log) to find out why. UAC for an unsigned file ("from an unknown publisher") fixed. `selftest.py` now runs cmd_windows against a fake machine (a shadowed variable once cost a 13-minute Windows install). |
| `workflow_call`/`workflow_dispatch` | DONE: `installer_url`, `installer_sha256`, `variants`. E's release.yml calls it. |

## Next steps (in order)

1. Read run 39959768382 (`gh run view 39959768382 -R aarohkandy/v0.0.1-auros`; evidence `e2e-39959768382`): `diag/disk-inspection.txt` for refusal 2. Route what it says: the installer/staging is A2's (`src/aurstage/shrink.c`, install.c); the harness side (Fast Startup, restart kind) is mine if it is the PC's state.
2. When a run gets past the staging: M2's remaining steps (`watch_install`, `answer_yes`) and M3 (`store`) have never run; expect to fix OCR strings against the light theme (AurOS shell is light by default now).
3. A newer prerelease from E (with `619d439` and B's restyle): re-pin `INSTALLER_URL`/`INSTALLER_SHA256` in e2e.yml (or `gh workflow run e2e.yml -f installer_url=... -f installer_sha256=...`).
4. Variants two at a time: `hdd` (wall clock per phase: the bar is website to using it in under an hour on an ordinary PC), `smalldisk` + `pending`, `bitlocker` + `win10`. Dispatch: `gh workflow run e2e.yml --ref main -f variants=hdd,smalldisk -f installer_url=... -f installer_sha256=...`.
5. Evidence links into this file after each green run.

## What she sees (findings for the design owners; screenshot paths)

Evidence zips: `https://github.com/aarohkandy/v0.0.1-auros/releases/download/e2e-<RUN>/e2e-evidence-base.zip`.
Paths below are inside it (`flow/shots/...`), run 39906470700.

1. **Edge: "Keep anyway" is behind a small arrow on a black "Delete"
   button (worst so far; route to D, website; the page's own words
   `website/guide.html:44` and the index "Show more -> Keep anyway" are
   out of date).** Edge on Windows 11 26H2: the download's "..." shows only
   under the pointer (`0011-click-edge-download-item-more-actions.png`),
   its menu has Keep (`0013-click-edge-keep.png`), then a dialog "Make sure
   you trust ... before you open it" with Cancel and a solid black
   **Delete**; "Keep anyway" is in the menu of the 20-px arrow on Delete
   (`0014-click-edge-open-menu.png`, `0015-click-edge-keep-anyway.png`). She
   will press the black button, which deletes it. Three presses she cannot
   guess, the last one on a destructive-looking button.
2. **UAC says "from an unknown publisher" and focuses No**
   (`0018-auto-windows-open-the-installer-smartscreen-uac.png`). Normal for
   an unsigned file (D1), but the website's step ("Yes when it asks to make
   changes") should show this exact box and say the highlighted button is
   the wrong one: Yes is the white one on the left.
3. **The blue "Windows protected your PC" box never appeared** after
   Edge's Keep anyway: UAC came straight away. The site's step 3 ("Press
   More info, then Run anyway") then tells her about a screen she does not
   get. It may still appear on a PC where the file came from another
   browser or an older Windows; keep it, but say "if you see".
4. **The staging installer's "stopped" screen is full of words she has
   never seen (route to B/A2; `src/aurscreen/screen.c`)**
   (`0029-failure-auros-was-not-installed.png`, run 39923720924): a big pink
   "AurOS was not installed", then "To try again, open AurBridge in Windows
   and press Start installing" (there is no "AurBridge" and no "Start
   installing" anywhere she has been: the installer is "AurOS Setup", the
   button "Install AurOS"), no word on WHY (BitLocker), a footer line
   `aurstage-report v1 verdict=bitlocker record=refused`, and "Turn this
   computer off and on again": she must hold the power button; there is no
   Restart button and no automatic return to Windows. The calm sentence
   "nothing on it has been changed" is right but is the third line.
5. **A modern PC is a BitLocker PC (route to lead: D13 is the common case,
   not an edge).** A fresh Windows 11 with a TPM and Secure Boot starts
   device encryption by itself, before anyone logs in; the real
   Windows-side check in the first prerelease did not see it (fixed later in
   `619d439`). Until the pause lands, the PCs bought in the last two years
   hit the "can't go on yet" stop.
6. Website top (`0008-website.png`): clear, calm, one green button, two of
   them (header and hero) both work. No finding.

## Goal

Prove on a real Windows (in a KVM VM on GitHub Actions) what a person does:
Windows 11 from Microsoft's own ISO, unattended, to the desktop (M1) →
Edge opens our website, presses "Get AurOS", keeps the download, runs the
installer, goes through the wizard, Windows restarts into AurOS (M2) →
AurOS's store installs a program and opens it, and with the machine off the
program's files are on AurOS's disk (M3). Every step has a screenshot.
Then the same on the PCs people really have (variants, below).

## Status

| Milestone | State | Evidence |
|---|---|---|
| M1 Windows to desktop | **green** | runs 39891036021, 39891768538; prerelease `e2e-39891768538`. Windows 11 Enterprise Eval 10.0.26300 (26H2), Secure Boot 1, TPM 2.0 (swtpm), Fast Startup on, Smart App Control 2 (evaluation), BitLocker off. Setup from the ISO takes ~13 min on Actions (ISO download ~3 min). |
| M2 website → download → wizard → restart | **half**: download proven, wizard not yet reached | run 39894079407 (prerelease `e2e-39894079407`): Edge opened the runner-served website, the driver pressed the real "Get AurOS", Edge downloaded all 34.9 MB, then stalled at Edge's "isn't commonly downloaded" flyout (see Traps). Fix pushed in `208b3ed`; **re-run 39896941765 in progress** (doing Windows Setup again, see Cache). |
| M3 store install, program on disk | not reached | code is in `e2e.py` (store from the app list, search retried, exact "Install", then "Open" or the "Press here to open it" card; `inspect` checks the files with the machine off) |
| Variants | code pushed (`f70760f`); made safe in the 04:40 push (two at a time, base by default, only base cached); expectations per variant still to write (Next steps 4) | run 39897340342 cancelled by me: six jobs at once breaks the max-parallel ≤ 2 rule. Its smalldisk job showed first-logon variant setup works (leftover file made at 04:09). |

Unpushed work: none. The working tree was clean at 04:25.

## Open problem: no live view or evidence since 03:49

Every e2e run's `gh release create e2e-<run>` since 03:49 gets
`HTTP 403: Resource not accessible by integration`
(`.../releases`), at init and at final. othersys.yml does the same thing
with the same `permissions: contents: write` and worked at 03:58. I didn't find
the cause: there are no rulesets, all the commits are on the branch, and the
workflow headers are identical. `publish.sh` hides the failure (the step stays
green). The evidence zip still goes up as an Actions **artifact**
(`e2e-evidence` / `e2e-evidence-<variant>`), but artifacts can't be downloaded
from the agent container (blob host blocked). Job logs can be read with the
GitHub MCP `get_job_logs` once a job finishes.

## Point the harness at a new installer

In `.github/workflows/e2e.yml`, `env:`: set `INSTALLER_URL` to the
`AurOS-Setup.exe` URL (e.g. a prerelease asset) and `INSTALLER_SHA256` (empty
= accept and print the hash). Pushing that edit starts a run. Today it points
at `raw/image-desktop/AurOS-Installer-test.exe`. Stream E's log says to
re-point it at prerelease `v0.1.0-dd39389`'s `AurOS-Setup.exe` once that
is published. Details: README "Pointing it at a new build".

## Next steps

1. **Evidence visible again**: DONE in the 04:40 push, but not yet proven
   on Actions. `publish.sh init` tries e2e-<run> at the commit, then at the
   default branch, then the newest existing e2e-* prerelease with files named
   `r<RUN>-<file>`. It warns (`::warning::`) and puts the links in the job
   summary. Tested locally with a fake `gh` (all four paths). Check the first
   run after the push: its job summary names where the files went.
2. **Get M2 green, then M3**: read run 39896941765's result (job log via MCP;
   evidence wherever step 1 puts it). If the wizard is reached, follow the
   screenshots. The driver must press only Edge's own Keep / Show more /
   Keep anyway / Open file, never the page's.
3. **Variants, made safe for the shared account**: DONE in the 04:40 push.
   `max-parallel: 2`; a harness change runs `base` only; `[e2e:all]` runs
   all; `[e2e:a,b]` runs those; `[e2e:none]` skips. Only the base golden goes
   into the Actions cache; the others build Windows fresh (~15 min) and never
   save. Variants: `base`; `hdd` (base golden, QEMU throttle 100 MB/s,
   120 IOPS); `bitlocker` (first logon `Enable-BitLocker -TpmProtector
   -UsedSpaceOnly`, waits for FullyEncrypted); `smalldisk` (72 GB disk, 12 GB
   leftover in `C:\Windows\Temp`); `pending` (at the test's logon, a SYSTEM
   task runs `dism /enable-feature ... /norestart` → real `RebootPending`);
   `win10` (`WINDOWS_VERSION=10` in `get-windows.sh`).
4. **What each variant must see**: code DONE at 04:50 (`VARIANT_EXPECT` and
   `Flow.check_variant` in `e2e.py`; smalldisk's leftover is now sized from
   the free space). Not yet run: run `[e2e:smalldisk,pending]`, then
   `[e2e:bitlocker,win10]`, once base is green. Background
   (from `src/aurbridge/preflight.c` and
   `fix_win.c`): `pending-reboot` is PF_BLOCK with FIX_RESTART (the driver must
   take the restart, log on again, and run the installer again from Downloads).
   `insufficient-space` is FIX_AUTO (`free_space`), and the flow must then go
   on. `bitlocker-system` is FIX_AUTO (pause BitLocker, owner decision D11).
   Check that the 12 GB leftover file is what `free_space` actually cleans, and
   size it so the space check fails before the fix and passes after. Write each
   expectation as a check that fails when the fix is missing.
5. **Timings for the owner**: the hdd variant's wall clock for each phase
   (`timings.md` in each evidence zip); a 2013-laptop budget is the bar.

## Traps (each cost a run)

- **Edge's download flyout is a web page** (Edge WebUI, so a "document" to UI
  Automation). A filter that ignores web documents, meant to stop the
  website's own "Keep anyway" text from being clicked, also hid Edge's
  flyout. Fixed: only a document that fills the window counts as the website;
  the flyout is reopened from the toolbar if it closes; the step fails after
  8 minutes with a screenshot.
- **Display timeout**: Windows blanks the screen and screenshots go black.
  The driver now moves the pointer 1 px every 2 minutes without input.
- **Cache eviction**: key `e2e-windows-v1-48c56e1d96769621` hit at 03:19
  and was gone by 03:49 (other workflows' caches pushed it out). Don't
  count on the cache. It saves ~10 min at best.
- **Release 403** (above): a green publish step doesn't mean anything was
  published. Check `releases/download/e2e-<run>/...` with curl.
- **Smart App Control is in evaluation (2)** on a fresh Windows 11. It can
  switch itself on and block an unsigned installer (no code signing, owner
  D1). Watch for its block dialog in M2 screenshots.
- `[e2e:...]` is read from the commit **subject** only; the README alone
  doesn't start a run; the `concurrency` group is on the e2e job, so a
  push doesn't cancel a queued run.

## How to verify

- The Actions run: `https://github.com/ComputerDude771/auros-from-scratch/actions/workflows/e2e.yml`.
  The step names are the milestones: "M1 - ...", "M2/M3 - ...", "M3 - the
  program is on AurOS's disk".
- Evidence (when publishing works): `releases/download/e2e-<RUN>/live[-<variant>].png`
  and `.txt` while it runs, `e2e-evidence[-<variant>].zip` after (screenshots
  `shots/`, `driver.log`, `timings.md`, serial logs, `windows-facts.json`).
- Locally without KVM: `python3 tools/e2e/selftest.py` (36 checks: variant
  words against the wizard's titles, publish.sh's four ways out with a fake
  `gh`; also the job's first step), `python3 -m pyflakes tools/e2e/*.py`,
  `sh -n tools/e2e/*.sh`, `sh tools/e2e/mkanswer.sh /tmp/a.iso` (then check
  `autounattend.xml` in it with `xmllint`), and `python3 tools/e2e/vision.py shot.png`
  on any screenshot to see what the driver reads.
