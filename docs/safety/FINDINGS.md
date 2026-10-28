# Safety findings: every way her disk or Windows could be hurt

**What was reviewed.** `main` at `df2b5d8` ("handoff: field manual for session B (new repository, Mac, homebase)", 2026-10-22 21:46 PDT), read-only copy.

**When.** 2026-10-23, written about 13:25 UTC. By then `origin/main` was `5c16a39`, 284 commits later. Every entry has a **Status on main** line saying what has changed since. That status was checked from the commit subjects plus a read of the cited code on `5c16a39`; it was not re-traced end to end, so confirm before closing a ticket on it.

**Method.** Reviewers swept nine areas: what the Windows side writes, refusals and preflight, the staging install, "Put Windows back", power cuts, firmware and boot entries, the unwired fast path, life inside AurOS, and Windows after the install. That gave 48 candidate defects. Each was handed to three independent skeptics whose job was to refute it by tracing the code on that commit (and, where the search budget allowed, checking the Windows/firmware facts it rests on). A finding is kept when at least 2 of 3 failed to refute it. **Severity is the majority of the skeptics who upheld it; a tie goes to the higher.** Two candidates came in as blocker and were both lowered. Result: **26 kept (0 blocker, 12 high, 14 medium), 22 refuted.**

**Limits.** Code reading plus small repros (a compiled harness, scratch copies of Ferry). Nothing was run on real Windows, real firmware or a real disk. Several skeptics hit the search budget; where an entry leans on a Windows, GRUB or kernel fact that was not re-checked, it says so.

**Pairs with one root cause.** These were reported twice from different angles and are kept as separate entries so neither is lost. Fix each pair once: S-01/S-02, S-05/S-06, S-07/S-18, S-14/S-15 (S-03 is a third face of the same restore ordering).

## Index

| ID | Sev | Owner | Title | Where | Status on main (5c16a39) |
|---|---|---|---|---|---|
| S-01 | high | A2 | Restore erases a partition added after the install and grows C: over it | `src/aurstage/rescue.c:879` | fixed in `c994042` (foreign_partition(): refused before any write; powercut-nostick "a partition that was not AurOS's") |
| S-02 | high | A2 | Same defect, second report | `src/aurstage/rescue.c:1200` | fixed in `c994042` (same) |
| S-03 | high | A2 | No-stick restore writes the table before the EFI partition | `src/aurstage/rescue.c:1297` | fixed in `c994042` (the EFI partition is restored first, only differing megabytes) |
| S-04 | high | A2 | Boot entry cannot be written (NVRAM full), install still "succeeds" | `src/aurstage/loader.c:187` | fixed in `0d0e047` (nvram_probe at the gate of both paths; nvram_boot_repurpose; nvramtest 27 checks) |
| S-05 | high | C | The hold re-arms AurOS before the desktop exists | `rootfs/etc/systemd/system/auros-hold.service:19` | fixed in 69ca6b1 (auros-hold only publishes; auros-keeper arms BootNext after the desktop has worked 30 s; firstboottest D red on the old image, green now) |
| S-06 | high | C | Same defect, second report | `rootfs/etc/systemd/system/auros-hold.service:19` | fixed in 69ca6b1 (same change as S-05) |
| S-07 | high | C | New kernel is the default before it has ever booted | `rootfs/usr/lib/auros/bootchain:257` | fixed in 69ca6b1 (WP-30 minimal: grub boot counting, auros-boot-ok, previous kernel after two unconfirmed starts; rollbacktest 16/16, counter mutants red) |
| S-08 | high | R | Ferry reports success when it copied nothing | `src/ferry/ferry:97` | fixed in Ferry and `answer.sh`; the panel must ask whose files (`result=choose`) and show `result=empty` (C) |
| S-09 | high | R | `/usr/bin/ferry` symlink breaks Ferry | `src/ferry/install.sh:41` | fixed |
| S-10 | high | R | `auros-answer.service` sandbox blocks the import | `rootfs/etc/systemd/system/auros-answer.service:22` | fixed |
| S-11 | high | E CI | powercuttest misses the shipped mode and runs in no CI job | `tools/powercuttest.sh:162` | addressed in CI (E, run 39962229132): the shipped no-stick mode runs in four jobs on every build and gates every release; one slice is red (S-01/S-02) |
| S-12 | high | A2 | Clock-behind refusal for every PC west of UTC | `src/aurstage/journal.c:404` | fixed |
| S-13 | medium | B | BitLocker C: passes preflight in every state | `src/aurbridge/preflight.c:1601` | fixed |
| S-14 | medium | A2 | A restore that stops after the table rewrite can never be finished | `src/aurstage/rescue.c:1475` | fixed in `c994042` + `0d0e047` (ESP first; growability asked before the first write) |
| S-15 | medium | A2 | Restore commits table and ESP before checking C: can be grown | `src/aurstage/rescue.c:1475` | fixed in `0d0e047` (growable() before the first write; ntfs_read_state unsure until looked at) |
| S-16 | medium | A2 | GPT commit relies on an unvalidated old backup | `src/aurstage/commit.c:90` | fixed |
| S-17 | medium | A2 | A retry captures the shrunk C: as the original | `src/aurstage/rescue.c:590` | fixed |
| S-18 | medium | A2 | A kernel that loads and then panics is chosen at every start | `rootfs/usr/lib/auros/grub.cfg.in:3` | fixed in 69ca6b1 (same change as S-07) |
| S-19 | medium | A2 | Two ESPs: boot entry names a partition the files were not copied to | `src/aurbridge/phases.c:239` | fixed |
| S-20 | medium | B | EFI partition checked with a 1 MiB margin, files stay for good | `src/aurbridge/preflight.c:1405` | partly fixed |
| S-21 | medium | B | RAID system volume passes preflight; staging cannot find it | `src/aurbridge/preflight.c:1757` | fixed |
| S-22 | medium | B | Early download is not counted by the check that passes | `src/aurbridge/preflight.c:1267` | open |
| S-23 | medium | A2 | Hibernation switched off for good, even after Stop | `src/aurbridge/plat_win.c:1391` | fixed: Stop/abort in `fe7702f`; the Put Windows back leg in `0d0e047` (the put-back task restores hibernation) |
| S-24 | medium | A2 + B | When Windows moves itself first in BootOrder, nothing brings AurOS back | `src/aurfirst/state.c:377` | fixed in 69ca6b1 (the keeper puts AurOS first again once she confirmed and the desktop works; never after she said no; firstboottest E, aurfirsttest). Windows-side keeper task is WP-77, not done |
| S-25 | medium | C + A2 | Deleting AUROS-ROOT in Disk Management leaves a `grub>` prompt | `build/mkimage:253` | fixed in 69ca6b1 (build/mkimage: the ESP grub.cfg exits to the firmware with a plain sentence; windowsboottest F). The encrypted layout writes its own cfg: rootfs/usr/lib/auros/lock/lockctl needs the same else/exit |
| S-26 | medium | B | Encryption fix decrypts every fixed drive and ignores failure | `src/aurbridge/fix_win.c:351` | fixed (code removed) |

---

# High

### S-01 | high | Restore erases a partition added after the install and grows C: over it
- **Owner:** A2
- **Where:** `src/aurstage/rescue.c:879` (`fde_guard`, 838-933; pre-write loop 1200-1243)
- **Status on main:** open (`rescue.c` has one change since, `88b928f`, unrelated).
- **Scenario:** The install fills the gap after C: with AurOS, so the only way to new space afterwards is to shrink C: again. In Windows she or a helper shrinks C: in Disk Management and makes a D: for photos in the hole. Later she presses Put Windows back. The checks pass: disk size, DiskGUID, BitLocker, and "NTFS larger than captured" (C: is now smaller). The captured table has no entry for D:, so it is written over the live one, then `ntfsresize` grows C: across D:'s extent. The screen says "Windows is back exactly as it was."
- **Evidence:** Nothing compares the live table with captured entries plus the three AurOS partitions. The comment at 879-881 says an unreproduced entry "must not be encrypted", so an unencrypted one is deleted unchecked. No test adds a partition after the install.
- **Fix:** Before the first write require every used live entry to be either (i) a captured entry with the same first LBA, type and unique GUID (C: may end earlier; compare extents and GUIDs only, not attributes or name, because Windows rewrites recovery-partition attributes), or (ii) one of AUROS-ROOT/SAVED/BOOT by type GUID plus GPT name. Otherwise refuse with nothing written: "Something new has been put on this disk since AurOS was installed; putting the old layout back would erase it."
- **Test (red before, green after):** after an install, `sgdisk` adds an NTFS partition inside C:'s freed space, then Put Windows back runs: today its entry vanishes and C: grows over it; after the fix the run refuses and the disk is byte-identical.
- **Skeptics (high, high, high):** No guard on the path. A flat byte-identical rule would itself strand restores after Windows rewrites recovery attributes, so match on extents and GUIDs.

### S-02 | high | Restore silently removes any partition Windows or she created after the install, then grows C: over it
- **Owner:** A2
- **Where:** `src/aurstage/rescue.c:1200`
- **Status on main:** open. Same root cause as S-01; fix once.
- **Scenario:** As S-01, with a second trigger: a Windows feature update that finds WinRE too small shrinks C: and creates a new recovery partition after it (the KB5034441 / 0x80070643 family). Restore deletes that entry, and C: grows over its extent. Reset this PC then points at a partition that no longer exists.
- **Evidence:** `rescue.c:1200-1243` compares only each captured NTFS volume's size; `fde_guard` looks only for `-FVE-FS-`. The grow at 1488 writes NTFS metadata over the new extent.
- **Fix:** As S-01. It must also tolerate any other AurOS-named partitions the installer can create on the system disk.
- **Test (red before, green after):** as S-01, with the added partition typed as a Windows recovery partition; restore must refuse with the disk untouched.
- **Skeptics (high, high, medium):** The Disk Management variant is real data loss. The Windows-update variant mostly loses a recovery partition (the old WinRE comes back with the captured table); that Setup creates a new partition this way was not confirmed (search budget).

### S-03 | high (entered as blocker) | Put Windows back, no-stick mode: the table is restored before the EFI partition, so a power cut during the EFI copy can leave no working Windows and no way back
- **Owner:** A2
- **Where:** `src/aurstage/rescue.c:1297` (table steps 1269-1322; EFI copy 1330-1365)
- **Status on main:** open. `437e947` added the restore instants to `tools/powercut-nostick.sh` and says they run "after the restore reorder that follows"; the reorder itself has not landed.
- **Scenario:** The wizard ships no-stick mode only, so the one saved copy is the AUROS-SAVED partition on her own disk. She uses Windows from the AurOS menu for weeks; Windows changes boot files or the BCD on the EFI partition. She presses Put Windows back. The restore writes the original table first, which removes AUROS-SAVED, AUROS-ROOT and AUROS-BOOT from the table, then streams the install-day EFI partition over the live one. The battery dies mid-stream. The EFI partition is now a mix of two moments and Windows Boot Manager fails. Running the restore again finds no copy (`rescue_find_n` locates it only through the live table), and AurOS's GRUB, which holds the restore menu, is gone too. A cut at `restore-sector` or `restore-backup` leaves Windows with a shrunk NTFS and no rerun.
- **Evidence:** `rescue_find_n` (65-93) finds copies only by `RESCUE_TYPE_GUID` in the live table. `powercuttest.sh:228-253` reruns only with a stick image attached, so "running it again finishes the job" is untested for the shipped mode.
- **Fix:** Restore the EFI partition before the table steps. The install never moves the machine's EFI partition, so its extent is identical in both layouts; a cut then leaves the AurOS table in force, AurOS still boots from AUROS-BOOT, and the copy can still be found. Keep the grow after the table. A cut after LBA 1 still cannot be rerun in no-stick mode, so the fuller fix is a copy that stays findable after the table reverts (for example its offset recorded on the EFI partition).
- **Test (red before, green after):** `tools/powercut-nostick.sh`: change a file on the ESP, cut power at `restore-esp-mid` with no stick attached, run the restore again: today "no saved copy" and a mixed ESP; after the reorder the rerun finishes and Windows starts.
- **Skeptics (high, high, high):** Lowered from blocker. It needs a Windows-modified EFI partition and a cut in a window of seconds (AURBRIDGE.md calls that window "what the stick is for"). If the EFI partition is unchanged since capture, a cut rewrites identical bytes and is harmless.

### S-04 | high | If the staging environment cannot write the "AurOS" boot entry (NVRAM full), the install still ends as a success and the next restart goes to Windows for good
- **Owner:** A2
- **Where:** `src/aurstage/loader.c:187` (and `install.c:794-799`, `src/aurfirst/state.c:272,329`)
- **Status on main:** open (`loader.c` and `nvram.c` unchanged; `loader_register` has no retry or reuse).
- **Scenario:** An old Lenovo or HP has a nearly full NVRAM store (AURBRIDGE.md:85-87 calls this "not rare"). The Windows half writes the "AurOS Installer" entry with `SetFirmwareEnvironmentVariable`; after the GPT commit, staging's write of the second entry fails. The installer entry is only deleted after the new one succeeds. `install.c` treats the failure as a warning on a console line that scrolls away, hands over to AurOS, and reports installed. In AurOS no entry names AUROS-BOOT, so the phase is "not-converted", the welcome question is never asked, `confirm` stamps nothing and Put Windows back fails. Next power-on goes to Windows. Windows was shrunk and AurOS is reachable only from the firmware boot menu.
- **Evidence:** `loader.c:187` then `203` (forget only on success); `nvram.c:392-396,411` fail without retry; `find_cb` reuses an entry only with the same description and partition GUID, so the installer entry is never reused.
- **Fix:** Prove the write can succeed before the commit: write and delete a probe Boot#### (or write the final "AurOS" entry first) and refuse while the machine is still Windows-only. On failure inside `loader_register`, reuse the installer's Boot#### number. From memory (not re-checked): the staging kernel refuses a non-volatile write that leaves under about 5 KB free, so "delete the installer entry and retry" gains only the difference between two near-equal entries; consider `efi_no_storage_paranoia`. In `aurfirst`, report a distinct phase when AUROS-BOOT exists and no entry names it, and say plainly that the next restart goes to Windows.
- **Test (red before, green after):** `tools/nvramtest.sh` with the fake efivarfs filled so only the installer's entry fits: `loader_register` must not end in `verdict=installed`; today it does, with no AurOS entry.
- **Skeptics (high, high, medium):** Every cited line matches. The medium vote: Windows still boots and nothing is lost. The 5 KB reserve is the premise that makes the trigger wide and was not checked online.

### S-05 | high | The "keep coming back to AurOS" hold runs before the desktop, so a desktop that never appears loops her back into AurOS before she has said yes
- **Owner:** C
- **Where:** `rootfs/etc/systemd/system/auros-hold.service:19` (`src/aurfirst/state.c:370-394`)
- **Status on main:** fixed in 69ca6b1 (auros-hold only publishes; auros-keeper arms BootNext after the desktop has worked 30 s; firstboottest D red on the old image, green now).
- **Scenario:** The install boot ends in AurOS with BootNext armed. On her PC the kernel boots but aurshell fails every time (graphics or KMS). `auros-hold` runs `Before=aurshell.service`, so `af_hold` re-arms BootNext on every boot while the question is unanswered. `aursorry` tells her to hold the power button and switch on again; that goes straight back to AurOS. Windows is reachable only by arrowing to "Windows" in GRUB's 3-second menu or the firmware boot key. The promise in AURBRIDGE.md ("the moment it does not work the firmware falls through to BootOrder") holds only for failures before the unit runs.
- **Evidence:** `af_hold` checks stamps, default and BootNext only. `aurshell.service` has `Restart=on-failure` and `StartLimitIntervalSec=0`, so nothing ever gives up or disarms. `shell-guard` safe mode changes arguments, not the hardware fault.
- **Fix:** Arm BootNext only after the desktop has proved itself: aurshell (or `welcome.c` on its first presented frame) touches a file; a path unit runs `aurfirst hold`. Do not drop the early unit outright: `welcome.c` reads `/run/auros-first.state`, which `hold` publishes, so keep an early `aurfirst state`. Do not use `RuntimeDirectory=` as the flag, it exists before anything is drawn. Also name the start-up menu's "Windows" line on the `aursorry` screen.
- **Test (red before, green after):** `tools/firstboottest.sh`: boot the installed image with aurshell forced to exit, power-cycle twice; today BootNext is re-armed each boot so the second restart is AurOS again; after the fix BootNext is consumed and the second restart is Windows.
- **Skeptics (high, medium, high):** Failures before the unit runs already self-heal. The medium vote: a persistent desktop failure after the hold is rare and one pass through the GRUB menu breaks the loop. A first-frame signal does not cover a desktop that draws but has dead input.

### S-06 | high | While AurOS is unconfirmed, the hold re-arms BootNext even when the desktop never starts, so a desktop crash loop keeps her in AurOS
- **Owner:** C
- **Where:** `rootfs/etc/systemd/system/auros-hold.service:19`
- **Status on main:** fixed in 69ca6b1 (same change as S-05).
- **Scenario:** As S-05, found from the "life inside AurOS" angle: the only exit is "No, go back to Windows" on the welcome panel, which lives in the desktop that never starts. The website promises "if AurOS ever fails to start, I start Windows by myself" (`website/index.html:287,361`, `how-it-works.html:175,314`).
- **Evidence:** As S-05. The staging probe checks Wi-Fi, backlight, sound and carrier only; it never starts a display.
- **Fix:** As S-05, plus a first-boot display probe in staging if cheap.
- **Test (red before, green after):** As S-05.
- **Skeptics (high, high, high):** Only a persistent desktop failure after the hold has run triggers it, and R13 rates old-PC hardware trouble as likely. A black screen where aurshell stays up is still stuck after the proposed fix.

### S-07 | high | A brand-new kernel becomes the default before it has ever booted, and nothing detects a failed boot
- **Owner:** C (S1 for the template)
- **Where:** `rootfs/usr/lib/auros/bootchain:257` (`grub.cfg.in:3`, `:39`)
- **Status on main:** fixed in 69ca6b1 (WP-30 minimal: grub boot counting, auros-boot-ok, previous kernel after two unconfirmed starts; rollbacktest 16/16, counter mutants red).
- **Scenario:** After she says yes, AurOS is first in BootOrder. Unattended upgrades install a new Ubuntu kernel; `update-grub` is diverted to `bootchain menu`, which writes the newest kernel as entry 0 with `set default=0`. On her hardware it hangs, black-screens or panics (`panic=10` reboots into the same entry). GRUB's `fallback` to "AurOS (previous kernel)" fires only when GRUB cannot load the entry, never once the kernel has started. Every power-on repeats; the previous kernel and Windows need arrow keys inside a 3-second menu. Choosing "previous" boots once; the next start is the broken kernel again.
- **Evidence:** `bootupdatetest.sh` boot 3 only damages a kernel's signature so shim refuses it. The docs claim a rollback that does not exist: `rootfs/etc/sysctl.d/61-auros-reliability.conf`, `README.md:57`, `website/how-it-works.html:239`, `docs/security/PLAN.md:36`.
- **Fix:** A boot-success flag in grubenv (the file `grub.cfg.in` already `load_env`/`save_env`s): at start, if `boot_ok` is not 1 and an `auros-previous` entry exists, default to it with the menu shown longer; set `boot_ok=0` and `save_env`; a unit after the desktop's first frame (same signal as S-05) writes `boot_ok=1` with `grubenv_put`. Only arm it when a second kernel exists. Correct the over-claiming sentences until it lands.
- **Test (red before, green after):** `tools/bootupdatetest.sh` boot 4: install a validly signed kernel that panics at init; today every restart picks it; after the fix the second start boots "AurOS (previous kernel)".
- **Skeptics (high, high, high):** Not refuted; the way back exists (a visible menu) but only for someone who knows to press a key. Corrections: Ubuntu's `recordfail` only lengthens the menu timeout (from memory); the truncated-initrd branch is weak because `kernels()` requires an initrd.

### S-08 | high | Ferry reports success when it copied nothing: a stage that dies is downgraded to a warning
- **Owner:** R
- **Where:** `src/ferry/ferry:97`
- **Status on main:** fixed by `895297d` (exit codes 0 all came, 2 something did not, 3 refused, 1 nothing came; `answer.sh` maps 1 to `result=failed`) and by the commit that follows it (R, 2026-10-23). Now: (1) `ferry run` settles whose files BEFORE any stage (`choose_user`): `--user NAME`, else the only account, else the only one that has files of its own (said in her report: `From the Windows account "Bob"`), else it stops with exit 6 and the names in `/var/lib/ferry/users`; `answer.sh` answers `result=choose`, `users=Bob,Harold`, and accepts the reply `import:NAME` (passed only as `--user NAME`, never interpreted). (2) A folder she would expect that cannot come (Windows kept Videos on another drive, a network share, a path that does not resolve) makes the stage partial (exit 2), never ok. (3) A stage that was only partly done is recorded as failed, not done, so a retry redoes it instead of skipping it and saying all is well. (4) Exit 0 with nothing copied is `result=empty` (`files=0`), not ok; `result=ok` carries `files=N`. (5) Not enough room: exit 4 / `result=failed` / `code=4`. Tests: `src/ferry/tests/run.sh` ("whose files, and what counts as success"), `tests/answertest.sh`, each with a mutant in `tests/mutants.sh`. **Still open, not Ferry's:** `src/aurfirst/request.c` accepts only the four fixed words, so `import:NAME` cannot reach `answer.sh` yet, and `welcome.c` must show a "whose files?" choice for `result=choose` and a plain sentence for `result=empty`; until then two accounts that both have files end in the honest `chooseuser`/`result=choose` stop instead of a false success.
- **Scenario:** A Windows PC with two real accounts (hers and her husband's, or hers plus an admin a grandson made). `ferry-settings`, `ferry-files` and `ferry-firefox` each `die` with "could not identify which Windows profile to import". `stage_if_needed` swallowed each with `|| warn`, `ferry run` exited 0, `answer.sh` wrote `result=ok`, and the panel said "Your files are here" with an empty home folder and no retry. A Ferry that dies before any stage exited 1, which became `partial`, which showed the same headline.
- **Evidence:** Reproduced in a scratch copy with a fake volume (Margaret and Harold): rc 0, three "could not identify" errors, empty destination, stage status files left at `running`.
- **Fix:** Remaining: the panel asks whose files to bring and passes `--user`; the headline "Your files are here" only for `result=ok` with at least one copied file.
- **Test (red before, green after):** `src/ferry/tests/run.sh` with a fake volume holding two user profiles: today exit 0 and an empty home; after, a non-zero exit (or a prompt for the account) and no success headline.
- **Skeptics (high, high, high):** BitLocker as an example is blocked upstream by `ferry-detect`. The sandbox failure (S-10) was a stronger version of the same false success at the time. No Windows data is touched; the harm is a false success and a dead end.

### S-09 | high | "Bring my files across" cannot work in the shipped image: `/usr/bin/ferry` is a symlink, so Ferry sources its library from `/usr/bin` and dies before reading anything
- **Owner:** R
- **Where:** `src/ferry/install.sh:41` (`src/ferry/ferry:32`)
- **Status on main:** fixed in `895297d` (`SELF=$(dirname "$(readlink -f "$0")")`).
- **Scenario:** `answer.sh` runs `/usr/bin/ferry run --auto`; `SELF` resolves to `/usr/bin`, `. /usr/bin/ferry-common.sh` does not exist, dash exits 2, `answer.sh` maps that to `partial`, and the panel says "Your files are here". Nothing was copied.
- **Evidence:** Reproduced under dash through a symlink in a scratch root: "No such file", non-zero; the real path prints `ferry 0.1.0`. No test called `/usr/bin/ferry`.
- **Fix:** Call `/usr/lib/ferry/ferry` from `answer.sh` and resolve `SELF` with `readlink -f`; do the same for `ferry-detect`, `ferry-report` and `FERRY_BIN`.
- **Test (red before, green after):** `src/ferry/tests`: `install.sh scripts` into a scratch root and run `usr/bin/ferry version` through the symlink; today it fails with "ferry-common.sh: No such file".
- **Skeptics (high, high, high):** Reproduced independently. Fixing only the symlink would still have left S-10.

### S-10 | high | `auros-answer.service`'s sandbox blocks the import even with the symlink fixed
- **Owner:** R
- **Where:** `rootfs/etc/systemd/system/auros-answer.service:22` (`ProtectHome=yes`, `ProtectSystem=strict`, `ReadWritePaths` at 31)
- **Status on main:** fixed in `895297d` (`StateDirectory=ferry`, `RuntimeDirectory`, `ProtectHome=no` with `ReadWritePaths`, proof by `src/ferry/tests/sandbox.sh` under systemd). Caution: the unit still has `TimeoutStartSec=3600`, see Refuted R-19.
- **Scenario:** Inside the unit `ferry_state_init`'s `mkdir -p /var/lib/ferry` fails (read-only), Ferry exits 1, `answer.sh` writes `result=partial` and the panel says "Your files are here". Past that, `/run/ferry/windows` is read-only and `/home` is empty, so nothing could be copied.
- **Evidence:** `ferry-common.sh:26-27,97`; the unit's `ReadWritePaths` listed only `/var/lib/auros /var/log /run/auros-answer /run/auros -/boot/grub`; `docs/handoff/TRAPS.md:40-45` records the same class of bug for putback.
- **Fix:** Run Ferry in a unit with its own state and runtime directories and copy as her uid. Separate hardening item: `ferry-files` resolves the destination from her `~/.config/user-dirs.dirs` and `chown -R`s it as root, so a hostile `XDG_PICTURES_DIR=/etc` would chown `/etc`; copying as her uid closes that.
- **Test (red before, green after):** `src/ferry/tests/sandbox.sh`: `ferry run` under every `[Service]` property of the unit; it must copy a file into her home.
- **Skeptics (high, high, high):** The `ferry-wifi` sub-claim is stale (it only lists networks now). The chown/XDG point is a distinct hardening item.

### S-11 | high | powercuttest does not test the mode that ships, misses three dangerous instants, and runs in no CI job, yet the README and website say "138/138"
- **Owner:** E CI
- **Where:** `tools/powercuttest.sh:162`
- **Status on main:** addressed in CI by E, 2026-10-23. (1) `powercuttest` (the stick mode, 138 checks at 18 instants) is in the `tests.yml` gating matrix: `tests / e2e (powercuttest)` green in run 39905858387. (2) The shipped no-stick mode, `tools/powercut-nostick.sh`, runs on every build as `tests.yml` suite `nostick`, four jobs (`powercut / e2e (...)` in `build.yml`), and is a gate of every release: `release.yml`'s `which` job runs `tools/releasegate.py` for `[release]` and `[publish]` alike, and refuses unless every power-cut job is green or is named in `.github/ci-allow.txt` with a reason and an expiry date; an allow-list entry lets a prerelease out and never the full release (`[latest]`). Selftest 14 cases, and the real `which` script on fixtures. First full run, 39962229132 (fb82014): the `early`, `write` and `commit` slices (all 13 install instants) are green; the `restore` slice fails 3 of 57 checks, all in "a partition that was not AurOS's" (a recovery partition resized after the install: Put Windows back does not refuse). That is S-01/S-02 (A2's), allow-listed until 2026-10-26 for prereleases. (3) The docs no longer claim counts CI does not produce: 138 is the stick mode at 18 instants (not "seventeen"), README, tools/README, docs/PLAN and docs/results say "with a memory stick". Not done: `mirror-end` is in the no-stick loop but an instant inside `ntfsresize` and `settle-mid` still are not, and the website's wording is D's to check against these counts.
- **Scenario:** The wizard ships no-stick only (`wizard.c:1285`). powercuttest builds a stick and a journal without `image_on`, so every cut is checked with the stick's copy and record. Run against the shipped mode its own checks "Windows can be put back" (shrink-end through commit-array) and "running it again finishes the job" (restore-sector onward) would fail. `mirror-end` (`install.c:657`) is not in the loop.
- **Evidence:** 18 `fault_maybe` points in `src`; `grep -rn powercuttest .github` was empty at the reviewed commit; `website/how-it-works.html:212`, `docs/results/README.md:18`.
- **Fix:** Parameterise by mode (stick | none), add `mirror-end`, `shrink-mid`, `settle-mid`, run it in CI with an `AURSTAGE_FAULT=1` staging build, and qualify the numbers "(stick mode)".
- **Test (red before, green after):** powercuttest's instants run in no-stick mode (journal `image_on=windows`, no stick image) including `mirror-end`; today that run does not exist, and with it S-03 and S-14 go red.
- **Skeptics (medium, high; one refuted):** The refuter called it a coverage gap, not a path to harm (every pre-commit cut leaves Windows bootable; the website labels the row "with a stick"). The two who upheld it split on severity; the tie goes to high because the confirmed unrerunnable restore (S-03, S-14) is exactly what the gate hides.

### S-12 | high | Every install west of UTC is refused with "clock-behind", because the staging kernel reads Windows' local-time RTC as UTC
- **Owner:** A2
- **Where:** `src/aurstage/journal.c:404`
- **Status on main:** **fixed** in `a993bd0` (`journal_clock()`, 15 h slack, tested by `tools/bridgetest.sh`). The reviewed commit predates the fix; the third skeptic refuted it for that reason.
- **Scenario:** `phases.c:1569` stamps `written_unix = time(NULL)` (true UTC). Windows keeps the RTC in local time; staging's kernel takes the RTC as UTC, so `time()` is 4-10 h behind in the Americas, `now < written_unix` returns `JOURNAL_CLOCK`, and `install.c` refuses on every retry.
- **Evidence:** A compiled harness gave `verdict=clock-behind` for a journal 5 h ahead. Tests wrote the host's `date +%s` and QEMU's RTC defaults to UTC, so none could catch it.
- **Fix:** Allow for timezone skew in both comparisons: refuse as behind only when `now + 14 h < written_unix`, and widen the staleness test the same way (as landed).
- **Test (red before, green after):** `journal_check` with `written_unix = now + 5 h - 60 s` must return MATCH.
- **Skeptics (high, high; one refuted as already fixed):** Both who upheld it traced the full path; east-of-UTC PCs are unaffected.

---

# Medium

### S-13 | medium (entered as high) | BitLocker C: in every state passes preflight: the check reads the volume's decrypted view, and the raw-disk check assumes something already refused it
- **Owner:** B
- **Where:** `src/aurbridge/preflight.c:1601` (`volume_is_bitlocker`, 1097-1119)
- **Status on main:** fixed in `619d439` (`raw_bitlocker()` marks the volume and adds `bitlocker-system`; `windows.yml` step with `AURBRIDGE_TEST_FVE`). Per that commit's own text, not proven on a real BitLocker drive.
- **Scenario:** A Windows 11 laptop with Device Encryption (including "waiting for activation") reads as plain NTFS through `\\.\C:` because the filter decrypts it. `check_encryption` sees `-FVE-FS-` on the raw disk, then returns silently because "already refused by check_volumes". `pf_is_go` is true, the wizard says ready, the image downloads, and staging's `ntfs_read_state` refuses on a text screen needing a held power button. Retries repeat it. `phase_consent`'s recovery-key proof, keyed on `volumes[sys].bitlocker == 1`, never ran.
- **Evidence:** `preflight.c:572-576`, `1555-1563`, `1229-1231` all say the volume read is decrypted; `pf_sim.c:112` hard-codes `bitlocker = 0`; no CI step ran preflight on a BitLocker volume.
- **Fix:** Make the raw read at `system_offset` authoritative: set the system volume's `bitlocker = 1` and add `bitlocker-system`; add a CI step asserting it.
- **Test (red before, green after):** `aurbridge preflight --json` on a disk whose first sector reads `-FVE-FS-` must contain `bitlocker-system` and mark the volume.
- **Skeptics (high, medium, medium):** Staging refuses before any write, BootNext is one-shot and BitLocker is not suspended on this path, so the cost is a wasted download and a dead-end screen, not data loss. The high vote counted the wizard's whole BitLocker experience as dead. The volume-handle behaviour was not confirmed in a Microsoft document. It becomes a recovery-screen risk if D11's suspend path is wired with the old detection.

### S-14 | medium (entered as blocker) | Once "Put Windows back" rewrites the partition table, nothing can start the restore again, so a restore that stops partway can never be finished
- **Owner:** A2
- **Where:** `src/aurstage/rescue.c:1475`
- **Status on main:** open. Same ordering as S-15 and S-03.
- **Scenario:** C: is hibernated or log-unclean when she presses Put Windows back (she held the power button on a hung Windows, or an update turned Fast Startup on). The restore commits the GPT (removing AurOS) and rewrites the ESP, and only then reads the NTFS state at 1475. The grow is skipped and the screen says "run this again to finish", but the only `aurstage.restore` entry is AurOS's GRUB on the partition just deleted. C: stays tens of GB smaller than its partition until `diskpart extend filesystem`.
- **Evidence:** The only pre-commit volume check is `now > aux` (1200-1235). The test that claims a rerun works, `powercuttest.sh:240-252`, boots the restore with QEMU `-kernel`, a path no real machine has after step 1.
- **Fix:** Move the growability decision into the pre-write loop: for every captured NTFS volume smaller than captured run `ntfs_read_state` and apply the `only_dirty` rule; refuse the whole restore before step 1 ("start Windows, choose Restart, not Shut down, then try again"). Stop printing "run this again" when no boot path exists.
- **Test (red before, green after):** make C: log-unclean (or hibernated) after an install, run Put Windows back; today the table and ESP are rewritten and the grow skipped; after the fix it refuses before the first write and the disk is byte-identical.
- **Skeptics (medium, medium, medium):** Lowered from blocker: Windows boots from the restored table, nothing is lost, only 16-40 GB of capacity is stuck. The power-cut branch is the window AURBRIDGE.md calls unavoidable. **Also found, route to A2:** `ntfs_read_state` returns early on `NTFS_DIRTY`, leaving `hibernated` and `log_dirty` at `NTFS_NO` (0), so `only_dirty` (`rescue.c:1470`) can force-grow a volume that is dirty and also hibernated or log-unclean. Initialise them to `NTFS_UNSURE`.

### S-15 | medium (entered as high) | The restore deletes AurOS and rewrites the ESP before checking whether C: can be grown, then tells her to "run this again", which is impossible
- **Owner:** A2
- **Where:** `src/aurstage/rescue.c:1475`
- **Status on main:** open. Same defect as S-14; fix once.
- **Scenario:** As S-14, from the "Windows after" angle. `answer.sh` arms the restore after checking only that `staging.efi` exists; `install.c:1007-1024` reports `verdict=restored` and powers off, and the screen shows "Windows is back".
- **Evidence:** `rescue.c:1322` (`table_restored`), `1363` (`esp_restored`), `1468` (`ntfs_read_state`).
- **Fix:** As S-14. Optionally have `aurfirst putback` run the same read-only check from AurOS so she gets the sentence while AurOS still exists.
- **Test (red before, green after):** As S-14.
- **Skeptics (medium, medium, high):** Windows boots, no file lost. In the no-stick install there is no way to rerun. The high vote counted the permanent space loss as needing an expert.

### S-16 | medium | GPT commit writes the primary array first and relies on an old backup GPT at the last LBA that nothing ever validates
- **Owner:** A2
- **Where:** `src/aurstage/commit.c:90`
- **Status on main:** fixed in `ce5cead` (new backup first, then primary array, then LBA 1; `commit_build` refuses a table read from its spare copy; `committest` has 19 checks including a disk whose backup was zeroed).
- **Scenario:** A disk cloned to a larger SSD keeps its backup GPT at the old end. The gate (`rescue.c:503-516`) only checks the 8-byte magic at the primary's `alt_lba`. Step 1 invalidates the primary's array CRC; a cut before LBA 1 leaves firmware looking for a backup at the last LBA, finding none, and Windows' ESP is not found.
- **Evidence:** `gpt_read` returns as soon as the primary parses; AURBRIDGE.md's "backup header first" row said the backup "is already correct", which nothing verified.
- **Fix:** Reorder so a reader always finds the old table or the new one (landed), or refuse at the gate unless a CRC-valid backup at `disk_sectors-1` equals the primary.
- **Test (red before, green after):** `tools/committest.sh`: cut power after step 1 on a disk whose backup was zeroed; `fdisk` must still read a table.
- **Skeptics (medium, medium, medium):** Needs a misplaced or bad backup plus a cut inside a window of two flushes; only the no-stick install strands her (stick mode recovers). They warned the reorder reverses a documented design (backup-first changes the layout at the array write) and preferred the gate; `ce5cead` chose the reorder and added tests.

### S-17 | medium (entered as high) | A retry after an install cut short between shrink-end and the commit saves the shrunk C: as the "original", so Put Windows back gives her a permanently smaller C: and says "exactly as it was"
- **Owner:** A2
- **Where:** `src/aurstage/rescue.c:590`
- **Status on main:** fixed in `88b928f` (target picked from the partition entry; restore grows NTFS to at least the partition it was captured in; proof `tools/powercut-nostick.sh`).
- **Scenario:** No-stick install; a cut after `ntfsresize` leaves the filesystem 40 GiB smaller than its unchanged partition entry. Windows boots and clears the dirty bit; she retries. `rescue_capture` reads the shrunk `$Boot` total as the volume's original size. Later the restore grows only to that size and prints "exactly as it was".
- **Evidence:** `install.c:432-437` states the danger itself; no check compares NTFS size to partition size; `powercuttest.sh` never reruns an install after a cut.
- **Fix:** Record the partition-filling size as the captured size (landed), or refuse at the gate when the filesystem is smaller than its partition by more than a cluster.
- **Test (red before, green after):** cut at `write-mid`, install again, Put Windows back, assert the NTFS size equals the partition size (`powercut-nostick.sh`).
- **Skeptics (medium, medium, medium):** A retry only passes preflight while 36 GiB is free, so the permanent loss is at most about 40 GiB and she keeps room. Disk Management cannot extend a partition with no unallocated space; `diskpart extend filesystem` can. A refusal at the gate is the worse fix because it leaves her no AurOS to put back.

### S-18 | medium | A new kernel that loads but panics or hangs is chosen at every start after confirm; GRUB's fallback only covers kernels it cannot load
- **Owner:** A2 (C for the clear unit)
- **Where:** `rootfs/usr/lib/auros/grub.cfg.in:3`
- **Status on main:** fixed in 69ca6b1 (same change as S-07).
- **Scenario:** As S-07. `bootchain:199-209` says the previous kernel starts "on its own if the newest cannot be"; that holds for a load failure, not for a kernel that starts.
- **Evidence:** `bootupdatetest.sh` boot 3 tests only a signature failure; no recordfail or boot-success marker exists in `rootfs/` or `build/`.
- **Fix:** As S-07: a `boot_pending` counter in grubenv; if set, `default=auros-previous` with the menu shown; cleared after the desktop's first frame; add a hanging-kernel case.
- **Test (red before, green after):** As S-07.
- **Skeptics (medium, medium, medium):** Another panel rated the same root cause medium: triggered only by a regressed kernel on her hardware, and the menu offers a way out. Treat S-07 and S-18 as one item at the higher rating.

### S-19 | medium | The boot entry names a different EFI partition from the one the files were copied to when the disk has two ESPs, so the restart does nothing and no one is told
- **Owner:** A2
- **Where:** `src/aurbridge/phases.c:239`
- **Status on main:** fixed in `a8d8585` ("a computer with two EFI partitions is refused, not guessed", with `tools/esptest.sh`).
- **Scenario:** Two ESP-type partitions on the Windows disk. Preflight measures the first, `phase_inspect` keeps the last, and `mountvol /S` mounts whichever Windows booted from. The HD() node of the load option names `m->esp`; if that is not the mounted one the firmware finds no `shimx64.efi`, spends BootNext and starts Windows with no message.
- **Evidence:** `m->esp_offset` (set at `phases.c:170`) is never compared with `m->esp`. Staging refuses `esps != 1` (`rescue.c:181-188`) anyway, so such a PC could never finish.
- **Fix:** Refuse in `phase_inspect` when there is more than one ESP-type entry (landed); better, also check the mounted volume's extent equals `m->esp_offset` before writing.
- **Test (red before, green after):** `tools/esptest.sh`: a disk with two ESP-type entries must be refused by `phase_inspect`.
- **Skeptics (medium, medium, medium):** Windows boots and nothing is lost; the real gap is a silent late failure after a 5 GB download. Side effect: BitLocker stays suspended with `-RebootCount 0` after a silent no-op restart. Which partition `mountvol /S` picks on a two-ESP disk is undocumented (not found).

### S-20 | medium | Room on the EFI partition is checked against a 1 MiB margin, and the ~33 MB of AurOS files stay there for good, leaving Windows unable to update its start-up files
- **Owner:** B
- **Where:** `src/aurbridge/preflight.c:1405` (`phases.c:1491`)
- **Status on main:** partly fixed. `619d439` made preflight ask for carried + 21 MiB. The `esp_room` check in `phases.c` still adds only 1 MiB (the commit asks A2 to match it). I did not find `ab_abort` deleting the files.
- **Scenario:** An OEM PC with a 100 MB ESP and 34-48 MB free passes both checks, phase 3 copies about 33 MB, and 1-15 MB is left. Windows servicing wants about 13-15 MB (Microsoft KB3086249 as quoted on Microsoft Q&A); updates fail and roll back until the space is recovered. The files are kept for "Put Windows back", and an aborted attempt leaves them too.
- **Evidence:** `fix_win.c:132` marks `esp-low-space` as cannot-fix; `answer.sh` and `grub.cfg.in:128-138` depend on `staging.efi`/`staging.img` staying.
- **Fix:** Same headroom in `esp_room` (carried + 21 MiB). In `ab_abort`, delete the seven files for runs that never reached the restart. A cheaper first step for the permanent footprint: delete the ~4.5 MB of first-stage files (`shimx64.efi`, `grubx64.efi`, `mmx64.efi`, `grub.cfg`, `choices.conf`) once AurOS is installed.
- **Test (red before, green after):** an ESP with 40 MiB free: both `pf_run` and `esp_room` must refuse; today both accept.
- **Skeptics (medium, medium, medium):** Windows still boots and rolls the failed update back, so this is "updates stop", not "won't start". A flat +20 MiB turns the latent harm into a new refusal for PCs with 34-54 MB free. A secondary report that Microsoft's May 2026 cumulative update fails at 10 MB or less free was not checked at Microsoft.

### S-21 | medium | A Windows system disk that is a RAID volume passes preflight with an INFO, but staging has no RAID assembly and cannot find that disk
- **Owner:** B
- **Where:** `src/aurbridge/preflight.c:1757`
- **Status on main:** fixed in `619d439` (`raid-volume` is a stop, and so is not being able to tell how the drive is connected, `storage-controller-unknown`). Model names are what the drivers report; not proven on real hardware.
- **Scenario:** C: on an Intel RST RAID 0/1 volume. `rst_hides_disk` returns 0 for a model like "Intel Raid 0 Volume", so the result is the INFO "AurOS can see it". Staging loads only `nvme ahci mmc_block sdhci-pci vmd`, no `mdadm`, so `journal_disk` matches nothing and `install.c` refuses `wrong-disk` after the download and restart. `system_disk_rst() == -1` also passed silently.
- **Evidence:** `preflight.c:1757-1790`, `build/staging:562`.
- **Fix:** Block when the bus is RAID and the model is a RAID volume; do not key on `BusTypeRAID` alone (single SATA disks in "RAID On" report it and Linux sees them). Do not reuse the `intel-rst` text: switching a RAID volume to AHCI erases it.
- **Test (red before, green after):** preflight unit test: model "Intel Raid 1 Volume" with `iaStorAC` must give a stop; red on `df2b5d8` (INFO only).
- **Skeptics (medium, medium, medium):** The refusal comes before anything is written; the cost is a wasted download and restart. A worse path (a volume serial equal to a mirror member's) was not verifiable and was not counted.

### S-22 | medium | The early download is not counted by the check that passes, so the next check blocks her on space the installer itself used; the "fix" then empties her Recycle Bin and tells her to move her files
- **Owner:** B
- **Where:** `src/aurbridge/preflight.c:1267` (`wizard.c:3125`, `phases.c:305-330`)
- **Status on main:** open (no image term in `check_volumes`; `download_begin()` still starts on the first clean check).
- **Scenario:** C: can give up 28-33 GB, so the first check passes and `download_begin()` starts. 5-7 GB lands in `C:\AurOS`. At Start installing, preflight runs again with less giveable and blocks `insufficient-space`. "Fix these for me" cannot free the installer's own space and shows "Move big files off C:". If she presses Install while the download still runs, the re-check can pass and `phase_inspect` then uses a stale report.
- **Evidence:** `AURBRIDGE.md:746` promises 28 GB plus the image is checked before anything is downloaded.
- **Fix:** In `check_volumes` require `giveable >= PF_AUROS_NEED + max(0, AUROS_IMAGE_BYTES - bytes already in C:\AurOS\auros-desktop.img)` (same arithmetic as `phase_inspect`). The naive fix of adding the full image size double-counts after the download and gives a new permanent false block, so credit what is already there. Re-measure after the download wait, or hold the Install button until it finishes.
- **Test (red before, green after):** C: with 30 GB giveable and no image: `pf_run` must block `insufficient-space`; the same PC with the image already in `\AurOS` must not block.
- **Skeptics (medium, medium, medium):** The second check is correct; the first is too lenient. Nothing destructive ran, the Recycle Bin sweep is the standard disclosed fix, and the remedy says "move", not "delete". Cost: a wasted multi-GB download and a late block.

### S-23 | medium | Hibernation is switched off for good, even after she presses Stop, which is told "Windows ... not affected"
- **Owner:** A2
- **Where:** `src/aurbridge/plat_win.c:1391`
- **Status on main:** fixed for Stop and abort in `fe7702f` ("closing the installer takes back everything it did": `plat_fast_startup_restore()` from the abort path). Open: the Put Windows back leg, which runs under Linux and cannot call `powercfg`.
- **Scenario:** Phase 2 runs `powercfg /hibernate off` and sets `HiberbootEnabled=0` without recording the old state. She stops, or the download stalls. `ab_abort` restored BitLocker and BootNext but its comment said "nothing else was done". On a laptop that hibernated by default at critical battery, a machine left asleep with unsaved work runs flat and loses it.
- **Evidence:** `phases.h:26-33`, `wizard.c:4108` ("not affected either way"). The consent page that disclosed this was gone.
- **Fix:** Record whether `hiberfil.sys` existed, its type (full or reduced) and the old `HiberbootEnabled`; restore both in `ab_abort`. For Put Windows back, a Windows-side first-boot step or one sentence on the Ready page that hibernation stays off.
- **Test (red before, green after):** `tools/aborttest.sh` under Wine: after `phase_prepare` then `ab_abort`, `hiberfil.sys` is back and `HiberbootEnabled` has its original value.
- **Skeptics (medium, medium, medium):** Needs a laptop that had hibernation on, an aborted run, and days asleep with unsaved work; files on disk are not at risk. Restoring without the type would turn a reduced hiberfil into a full one (about 40% of RAM).

### S-24 | medium | When Windows moves itself to the front of BootOrder, AurOS becomes unreachable and nothing brings it back
- **Owner:** A2 (Windows side) + B (wizard writes the shortcut)
- **Where:** `src/aurfirst/state.c:377` (the finding cited `src/aurfirst/main.c:1`, a whole-file pointer)
- **Status on main:** fixed in 69ca6b1 (the keeper puts AurOS first again once she confirmed and the desktop works; never after she said no; firstboottest E, aurfirsttest). Windows-side keeper task is WP-77, not done.
- **Scenario:** After confirm, `af_hold` returns early on `confirmed || declined` and nothing else writes BootOrder. She picks Windows from AurOS's menu "for a moment" (what `help/windows.html` tells her to do); Windows re-registers its Boot Manager at the head of BootOrder, which forum reports show happening simply on booting Windows from GRUB. From then every power-on goes to Windows. The help page's "Next time you switch the computer on, AurOS starts again by itself" is false, and AurOS is reachable only via a firmware boot key.
- **Evidence:** Arch BBS id=236600 and Level1Techs 135245 (both fetched) report the reorder; nothing in `src/aurbridge` leaves a task or shortcut; the "put-back startup task" in START-HERE is about re-growing C:, not BootOrder.
- **Fix:** Leave a Windows-side route: a startup task or desktop shortcut running an elevated `bcdedit /set {fwbootmgr} bootsequence {AurOS entry}` plus restart, with the AurOS entry's GUID recorded by the installer (`bcdedit /enum firmware`). Gate it on a marker that `aurfirst confirm` leaves on the ESP and remove it in Put Windows back. Until then, stop `windows.html:21` promising AurOS returns by itself.
- **Test (red before, green after):** `tools/aurfirsttest.sh`: after confirm, rewrite BootOrder with Windows first; the Windows-side shortcut must exist and select the AurOS entry for one boot. Today none exists.
- **Skeptics (medium, medium, medium):** No data lost, Windows works; AurOS and what she saved there are intact but unreachable. The trigger is more likely than the claim said (any boot of Windows, not only an update). A firmware reset that drops the entry would defeat the shortcut; a "Reset this PC" trigger had no source.

### S-25 | medium | Deleting the "unknown" AurOS partition in Disk Management leaves the PC stuck at a `grub>` prompt on every start
- **Owner:** C (image stub) + A2 (`commit.c` attributes)
- **Where:** `build/mkimage:253`
- **Status on main:** fixed in 69ca6b1 (build/mkimage: the ESP grub.cfg exits to the firmware with a plain sentence; windowsboottest F). The encrypted layout writes its own cfg: rootfs/usr/lib/auros/lock/lockctl needs the same else/exit.
- **Scenario:** AUROS-ROOT is a large letterless "Primary Partition" right after C:; deleting it makes Extend Volume available. AUROS-BOOT is an ESP-typed partition Disk Management will not delete, so firmware still runs its shim and GRUB. The stub `grub.cfg` does `search --fs-uuid` then `configfile` with no failure branch, so GRUB stays at its prompt and the firmware never falls through to Windows. AurOS is first in BootOrder after confirm, so every power-on stops.
- **Evidence:** `mkimage:253-258`; `commit.c:35-61` gives all three partitions `attrs=0`; no other file rewrites the stub.
- **Fix:** Wrap the stub: `if search ...; then configfile ...; fi` then `exit`, so a failed search hands control back to the firmware. A non-zero exit status is what makes firmware try the next BootOrder entry; check that GRUB's `exit` takes a code. Optionally set GPT attribute bit 0 on the three partitions (whether Disk Management honours it was not verified).
- **Test (red before, green after):** `tools/loadertest.sh` under OVMF: boot the AurOS shim with AUROS-BOOT kept and the root UUID absent, Windows second in BootOrder; it must reach Windows Boot Manager. Today it stops at `grub>`.
- **Skeptics (medium, medium, medium):** Needs a deliberate manual delete outside the product's own removal path, plus a boot of Windows. Disk Management behaviour on an unknown-type GPT partition is from general knowledge (search budget).

### S-26 | medium | The encryption fix decrypts every fixed drive and reports success even when `manage-bde` refused, so she waits for ever
- **Owner:** B
- **Where:** `src/aurbridge/fix_win.c:351`
- **Status on main:** fixed by removal in `1bd6f6e` ("an encrypted drive is a stop; nothing decrypts it (D11)"): `encryption_off` no longer exists and `bitlocker-system` and `bitlocker-other` are cannot-fix.
- **Scenario:** A second fixed drive is BitLocker-locked (a USB backup disk that reports as fixed). "Fix these for me" ran `manage-bde -off X:` on every `DRIVE_FIXED` letter, threw the exit code away, returned 0, and the card promised "Encryption is being switched off... we carry on by ourselves" while the re-check kept blocking.
- **Evidence:** `fix_win.c:344-353`; `AB_CHOICE_BITLOCKER_PAUSE` was undefined, so the old decrypt path was the live one.
- **Fix:** If a fix ever returns, check `plat_run`'s exit only for the drives preflight flagged (a blanket non-zero check would false-fail on unencrypted drives, where `-off` is a no-op) and confirm with `manage-bde -status`.
- **Test (red before, green after):** `fix_run("bitlocker-other")` with a fake `plat_run` returning 1 for the flagged drive must return -1; today it returned 0.
- **Skeptics (medium, medium; one refuted):** The core mechanism holds but needs a locked drive that preflight flags; whether a locked volume's sector 0 reads `-FVE-FS-` through the volume handle was not confirmed. Decrypting every flagged drive was the intended pre-D11 design.

---

# Appendix: Refuted

These did not survive (at least 2 of 3 skeptics refuted them). Do not re-raise them as written. Where a skeptic left a latent condition, it is named so the right owner can re-check when that condition arrives.

- **R-01** "Fix these for me" can erase her user folder (TEMP fallback to `USERPROFILE`): the machine-level TEMP/TMP default to `%SystemRoot%\Temp`, so removing the per-user values lands on `C:\Windows\Temp`, which the sweep clears on purpose. Hardening only: require the last component to be `Temp` (the sweep has since been changed to named folders, `42541c5`).
- **R-02** Space fix empties the Recycle Bin on every drive: disclosed on the button, and `cleanmgr /sagerun` empties every drive's bin anyway, so scoping `SHEmptyRecycleBinW` changes nothing.
- **R-03** `bitlocker-system` auto-fix runs `manage-bde -off` on every fixed drive: reachable only after the detection bug (S-13) and now moot, since nothing decrypts (`1bd6f6e`).
- **R-04** A retry shrinks Windows again and can drain C: to ntfsresize's floor: preflight re-measures the shrunk volume and blocks below about 36 GiB free, so C: is never drained to zero (the 16-40 GiB cost per retry was still fixed in `88b928f`).
- **R-05** After the shrink every failure screen says "nothing has been changed": screens for the named failures carry the remedy, and a disk check after a resize is by design; a wording nit (since changed, `9dcecfa`).
- **R-06** The restore rolls the ESP back to a months-old snapshot and undoes boot updates: harm needs the PCA 2011 signer revoked in the firmware dbx, which is a manual opt-in with no enforcement date, and `fde_guard` refuses BitLocker before any write. Latent: re-check if Microsoft starts automatic revocation.
- **R-07** A stale capture on an old memory stick beats the on-disk capture: a stick capture cannot exist, the wizard only does no-stick mode. Latent: prefer the newest `captured_unix` if a stick page ever ships.
- **R-08** `restore_boot_sectors` misses ext4/LUKS/XFS on 512-byte disks: the backup-sector proof fails after any normal `mkfs` (it zeroes the tail), a reformat needs a manual terminal, and the 8 KiB overwritten is recoverable. Optional hardening: look for the ext4 magic at 1080 at any sector size.
- **R-09** BootNext is armed before `e2fsck`/`resize2fs` of AurOS's root: a cut leaves a bootable un-grown AurOS (`fsck.repair=yes`, `firstboot.sh` grows it), and moving the arming later would leave a longer window with no AurOS entry. Wording nit on the settle-failed screen only.
- **R-10** Chainloading Windows through shim and GRUB changes PCR7/PCR4 and triggers the BitLocker recovery screen: no BitLocker PC reaches the install on main (D11 undefined, encrypted drives are a stop). Latent gate for D11: before defining `AB_CHOICE_BITLOCKER_PAUSE`, give Windows a firmware-direct BootNext route and drop the chainload entry on such PCs. **Fixed in 69ca6b1**: the menu starts Windows through the firmware (BootNext at Windows Boot Manager), PCR 4 and 7 equal a normal morning (windowsboottest with swtpm; the old chainload differs).
- **R-11** After "yes" nothing keeps AurOS first (Windows update or BIOS NVRAM reset): lands her in her working Windows, AurOS stays in the firmware menu, and falling back to Windows is the design's safe direction. The part that stood is the missing route back, S-24.
- **R-12** An SBAT-revoked shim powers the PC off: no published SbatLevel revokes the shipped shim 15.8 (`shim,4`), so it needs a future revocation; same exposure as any Linux dual-boot. Watch for a `shim,5` entry.
- **R-13** Online shrink picks C: by GPT slot, not Windows' offset-ranked number: at the reviewed commit the only callers were the CI-only `gapselftest`; a skeptic found the engine on a newer main requires the offset to match the entry before it uses the number.
- **R-14** The gap fence does not protect the GPT itself: the only caller was `gapselftest`, whose window comes from `plan.c` clamped to the usable LBAs. Hardening: bound the fence by `first_usable`/`last_usable` before the fast path is selectable.
- **R-15** The fence checks partitions only when armed: same unwired path; add a live re-check when the fast path streams for minutes.
- **R-16** The BitLocker `gapselftest` step always passes as SKIPPED: disclosed, the fast path was unwired, and BitLocker PCs were still stopped. Make the BitLocker run a gate before D11.
- **R-17** After confirm, the GRUB "Windows" entry brings up the BitLocker recovery screen: same as R-10 (an encrypted PC cannot be installed on main). **Fixed in 69ca6b1**: the menu starts Windows through the firmware (BootNext at Windows Boot Manager), PCR 4 and 7 equal a normal morning (windowsboottest with swtpm; the old chainload differs).
- **R-18** Ferry copies as root with no free-space check and can fill AurOS's root: Ferry could not copy a byte in the sandbox (S-10), and since `a75e9a7` the copier runs as her. Re-raised and closed: the new copier had no free-space check on the way in either; it now refuses before the first byte (exit 4, nothing copied) unless the files fit with at least 1 GiB or 5 % of the disk left over, and skips that test on a resumed import. Test: `src/ferry/tests/run.sh` "R-18".
- **R-19** An import longer than an hour is killed by `TimeoutStartSec=3600`: unreachable at the reviewed commit because Ferry died on the sandbox first. **Re-checked and live, now fixed (R, 2026-10-23):** `auros-answer.service` has `TimeoutStartSec=infinity`; the two requests that can take hours (the import, and saving her files into Windows before Put Windows back) are bounded inside `answer.sh` by `timeout 43200` (12 h, `AUROS_IMPORT_LIMIT`) so a drive that has stopped answering cannot hold every later request for ever, and a stopped run says `result=failed code=124` with "press it again and it carries on". A killed copy is resumable without duplicates: hidden temporaries are removed on the next run and files already placed are recognised (same size and time). Tests: the kill test in `src/ferry/tests/run.sh` (a `ferry-copy` is SIGKILLed after 30 files are committed, then run again: 600 files exactly once, byte for byte, no "(from Windows)" twins, no temporaries), the unit check, and `answertest.sh` (124 maps to failed). At the reviewed commit `welcome.c` also offered no action on the "Bringing your files across" panel: still C's.
- **R-20** "Put Windows back" overwrites Windows' whole EFI partition with an install-day copy: same as R-06.
- **R-21** "Bring my files across" always fails because the installer's shrink left C: dirty: `ntfs-3g.probe --readonly` exits 0 on a dirty-bit-only volume and the `ntfsinfo` grep can never match, so the gate does not fire. The reverse is worth a ticket for R: Ferry's dirty gate is dead code for dirty volumes, so it would import from a volume Windows left unclean (read-only mount, no data loss).
- **R-22** The only everyday route to Windows chainloads `bootmgfw.efi` from shim and GRUB and shows a BitLocker recovery screen: same as R-10. **Fixed in 69ca6b1**: the menu starts Windows through the firmware (BootNext at Windows Boot Manager), PCR 4 and 7 equal a normal morning (windowsboottest with swtpm; the old chainload differs).

