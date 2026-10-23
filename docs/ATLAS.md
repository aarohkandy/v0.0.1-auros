# ATLAS: the expert's map of AurOS

Written 2026-10-23 from eight subsystem briefs, read against `main` (`922bcba`).
Line numbers are from that commit. Claims marked **(checked)** were re-read in the code or
`gh` while writing this; the rest come from the briefs. "Her" is the person installing AurOS.

## The whole system on one page

From the website to her desktop, in order:

1. **Website** (`website/index.html`, `assets/door.js`). `door.js:29-41` builds the single
   download address, `github.com/<repo>/releases/latest/download/AurOS-Setup.exe`, and
   `door.js:46-127` shows only her browser's warning steps and refuses phones, Macs, ARM and
   32-bit Windows. Deployed by `pages.yml` (checked by `tools/site/check.mjs`) or by
   `release.yml`'s `pages` job (not checked).
2. **Release** (`release.yml`). Pieces of the image (90 MiB, pigz) and `AurOS-Setup.exe` are
   published as a prerelease; `promote` makes it "latest" only after a passing `e2e.yml` run
   that pins this exe's SHA-256.
3. **AurBridge wizard** on Windows (`src/aurbridge/wizard.c`, WinMain `:4725`). Preflight
   (`preflight.c`) on a thread; if it passes, the image download starts at once
   (`download_begin`, `wizard.c:3125`) into `C:\AurOS\` only.
4. **Stop page / Fix** (`fix_win.c`). "Fix these for me" runs fixes, some of which need a
   restart and arm a resume task.
5. **Install AurOS** (`phases.c`). Phase 0 inspect (GPT, NTFS serial, room), phase 1 consent,
   phase 2 prepare (Fast Startup off, image verified against the baked hash, manifest),
   phase 3 handoff (staging kernel/initrd, journal appended to the initrd, Canonical shim/grub
   into `\EFI\AurOS`, `Boot####` entry, one-shot BootNext). No raw write to her disk.
6. **Restart** (60 s countdown, `ExitWindowsEx` without force-close, `plat_win.c:1442`).
7. **Staging** (`src/aurstage`, PID 1 of the initramfs). Reads the journal, re-checks the
   machine, shrinks NTFS with `ntfsresize`, writes and verifies the image into the gap,
   commits the new GPT with one write at LBA 1, sets BootNext only, `switch_root`.
8. **Every AurOS boot: the hold** (`aurfirst hold`, `auros-hold.service`). Re-arms BootNext
   so AurOS keeps starting while BootOrder still says Windows.
9. **First boot** (`firstboot.sh`): grow root, apply `choices.conf`, look for Windows for Ferry.
10. **Desktop** (`aurshell` + `aurwl`). The welcome panel asks "does it work?". Her one-word
    answer goes to `/run/auros/answer`; root's `answer.sh` → `aurfirst request`.
11. **Her answer.** Confirm puts AurOS first in BootOrder; decline returns to Windows;
    "Bring my files across" runs Ferry; "Put Windows back" sets grubenv `next_entry` +
    BootNext and boots staging with `aurstage.restore`.
12. **Updates.** `bootchain` replaces `update-grub`/`grub-install`; unattended-upgrades for
    Ubuntu and the (not yet live) AurOS apt channel.

Proof today: steps 3-11 are proven **in KVM simulation only**. On real Windows only the
download in step 1-2 has been seen (e2e M2 stalled at Edge's flyout). No release exists in the
new repository and the site has never been live.

---

## 1. AurBridge (Windows installer)

**Purpose.** `AurOS-Setup.exe` is `aurbridge-wizard.exe` (copied at `build.yml:270`). The
shipped wizard always uses no-stick mode (`wizard.c:1285`); the stick path in `phases.c`
remains but is unreachable. The fast-path pieces (`plat_gap_*`, `plat_shrink_system`) are
compiled in and called by nothing in the product.

**Flow.**
1. WinMain `wizard.c:4725`: single-instance mutex (~4752), removes the resume task
   (`fix_resume_done`, ~4761), `start_check` runs `pf_run` on a thread (`:766`).
2. `check_advance` (`:3091`) → stop page if anything blocks; else `download_begin()`
   (`:3125`, `:1352`) starts before consent. Writes only `C:\AurOS\*`, keeps the PC awake.
3. Stop page: "Fix these for me" → `fix_worker` (`:851`) → `fix_run` (`fix_win.c:357`).
   FIX_RESTART fixes arm `schtasks /rl highest` (`fix_win.c:376-391`).
4. Ready page "Install AurOS": preflight again, `install_begin` (`:1369`) sets
   `consent_given=1` (`:1389`); `ab_worker` waits for the download, then
   `ab_run(AB_HANDOFF)` (`wizard.c:1209` → `phases.c:1749`).
   - Phase 0 inspect (`phases.c:146`): refuses unless preflight is go; reads the GPT for
     Windows' start/length in 512-byte units (`:226-246`), NTFS serial (`:257-266`), GPT
     SHA-256 (`:286`); room check (`:302-330`); `prepare_possible` (`:333`).
   - Phase 1 consent (`:344`); BitLocker branch `:366-391`.
   - Phase 2 prepare (`:1109`): Fast Startup and hibernation off (`:1119`,
     `plat_win.c:1378`); `ensure_image` (`:985`) resumes pieces, checks each SHA-256,
     inflates (`inflate.c`), checks the result against the baked hash (`:848-981`); writes
     and re-reads `<img>.manifest` with root/ESP extents (`:1324-1396`).
   - Phase 3 handoff (`:1503`): ESP room (`:1453`), copies staging.efi/img, appends a
     gzip+cpio journal to the initrd (`:1544-1615`); writes shim, grub, mmx64, grub.cfg into
     `\EFI\AurOS` only (`:1641-1711`) and `choices.conf` (`:1418`); creates `Boot####` by
     description (`plat_win.c:1048`); sets BootNext (`:1739`).
5. Restart: 60 s countdown (`wizard.c:3869`), `ExitWindowsEx(EWX_REBOOT|EWX_FORCEIFHUNG)`.
6. Abort: `ab_abort` (`phases.c:1780`) frees the payload, releases the stick, re-enables
   BitLocker, clears BootNext.

**Invariants.**
- No raw writes to the system disk before the restart: `plat_write` refuses any disk except
  `g_allowed` (−1) (`plat_win.c:149, 339-346`); the gap writer refuses outside its armed window
  or overlapping a partition (`:511-540`).
- Refuse before changing anything: `prepare_possible` in phase 0 (`phases.c:333, 509`);
  preflight re-run before install (`wizard.c:3091-3110`).
- One-shot boot only, BootOrder never touched (`phases.c:1733-1745`); a free boot slot must
  read as "not found", never "unreadable" (`plat_win.c:1062-1106`).
- Image is the one the build expected: baked SHA-256 and size (`build/aurbridge` refuses
  pieces without them), `hex_ok` (`phases.c:604`), output capped at `image_expect` (`:819`).
- Same-machine check: journal carries GPT hash, Windows start/length, NTFS serial, profile
  (`phases.c:1548-1567`); staging re-verifies.
- Secure Boot: shim chain mandatory when SB is on (`phases.c:1712-1718`); a release build
  refuses without a Microsoft-signed shim.
- One process (mutex `wizard.c:4752`), one disk handle (`take_stick`, `plat_win.c:164`).

**Tests.**
- `windows.yml` (Windows Server runner): selftest, read-only preflight, fast-startup-off,
  `fix insufficient-space` + symlink safety, resume-arm/done, ESP room, `--navtest`,
  `--selftest` (window + screen reader), VERSIONINFO, icon. Last green: old repo run
  37224672361. **New repo: no completed run; every run so far cancelled by the next push
  (checked: 11 cancelled, 1 pending).**
- `gapselftest` (`windows.yml:360-453`, now its own queue per `b312624`): never proven. Its one
  run (39900258215) stopped at step 1: the refusals were correct but `$LASTEXITCODE=1` leaked
  out of pwsh. Workflow bug, fixed in `4ba4a5a`, not yet re-run.
- `bridgetest.sh`: journal round trip writer→reader, fast-path plan agrees both sides, GPT
  hash vs Python, gzip/cpio, stick GPT via sgdisk (unit). `gapfencetest.sh`: fence vs
  `plat_sim` (unit). `manifesttest.sh` / `pe_payload.py`: manifest XML, requireAdministrator.
- `nosticktest` / `installtest`: aurbridge-sim → staging under Secure Boot in QEMU (KVM only).
- `getimage` against local pieces (`build.yml:307`), on Linux.
- `e2e.yml` (Windows 11 VM): never passed an install.
- `ab_selftest` (`phases.c:1837`) is never called by `selftest` (`main.c:395`) or any script.

**Gaps.**
1. **"Fix these for me" decrypts every fixed drive (checked).** BitLocker is FIX_AUTO
   (`fix_win.c:21-30`); `manage-bde -off` on C: to Z: (`:329-353`), including
   "bitlocker-other" drives that block nothing. Contradicts D11 and D9. No prompt to save the
   recovery key, though preflight's own remedy says to (`preflight.c:1129`). The page then
   still says "Nothing on this computer has been changed" (`wizard.c:2076`).
2. **The same button empties the Recycle Bin permanently (checked)** (`fix_win.c:310`) and
   clears Update Cleanup and driver packages (`:280`), removing rollbacks.
3. **The D11 switch is a trap.** `AB_CHOICE_BITLOCKER_PAUSE` makes BitLocker INFO
   (`preflight.c:1178`), but `phase_consent` still wants 8 typed key digits
   (`phases.c:366-374`) that the wizard cannot collect, and suspends with `-RebootCount 0`
   (indefinitely) (`:381`). Unreachable today.
4. **Closing mid-phase skips undo.** `install_cancel` returns while `g_ab_state==1`
   (`wizard.c:1452`), but the dialog promises "Windows and your files are not affected"
   (`:4108`); the process exits mid-step.
5. Fast Startup and hibernation stay off even on abort; the Ready page does not say so
   (`phases.c:1116`, `wizard.c:2596-2670`).
6. Leftovers after cancel: 5-10 GB in `C:\AurOS`, `\EFI\AurOS`, the `AurOS Installer`
   `Boot####` (`phases.c:1830`). Picking it later from the firmware menu boots a stale but
   armed journal.
7. Shipped binary honours the test env var `AURBRIDGE_STAGING_KARGS` (`phases.c:1641`).
8. Elevation trick: the resume task runs the exe from Downloads with `/rl highest` at logon
   (`fix_win.c:385`); anything that can write Downloads gets admin without UAC.
9. Stale comment `phases.h:17-28` ("nothing here writes the computer's own disk").
10. Minor: `plat_release` leaves `g_allowed` set (`plat_win.c:258`); stick read-back checks
    the root extent only, not the ESP (`phases.c:1261-1283`).

**Where to change what.** Pages/copy/flow/countdown/close: `wizard.c` (engine wiring
1120-1460, Ready 2596, stop page 2062, close 4106). What blocks and its words: `preflight.c`
(every BLOCK id needs a `fix_win.c` entry or `build/aurbridge` fails). Fixes: `fix_win.c`
(table `:14`, `fix_run` `:357`). Phases/download/journal/boot entry: `phases.c/h`
(`AB_CHOICE_*`). Windows APIs: `plat_win.c`; simulated twin `plat_sim.c` (`plat.h`). Shared
formats: `format.c/h`; planner shared from `src/aurstage/plan.c`, `gpt.c`. Build, baked
URL/hash, payload, manifest gate: `build/aurbridge`, `tools/pe_payload.py`. CI subcommands:
`main.c` (gapselftest `:152`). Tests: `tools/bridgetest.sh`, `gapfencetest.sh`,
`manifesttest.sh`, `tools/wizard/check.sh`, `.github/workflows/windows.yml`.

---

## 2. aurstage (staging installer and restore)

**Purpose.** PID 1 of the staging initramfs after AurBridge's restart. Same binary runs
"Put Windows back" (`aurstage.restore`). `aurscreen` draws the screen and only listens.
**aurstage has never run after a real Windows restart.**

**Flow.**
1. Dispatch `main.c:608` (restore wins if both set), install `:613`, else dry run/handover
   (`:619-645`).
2. Journal read before any write (`install.c:245-263`; damaged ≠ missing). `image_on`:
   `windows` = no-stick, `stick` allowed, anything else (incl. fast path `gap`) refused (`:267`).
3. Resume ladder (stick mode only, `:285`): at/after COMMIT_SECTOR → "already installed"
   (`:308`); last record SHRINK_BEGIN → give up (`:316`).
4. The gate (`:328-593`, read-only): `journal_check` (`journal.c:288-431`: serial, Windows
   start/size, GPT SHA-256, sector size, clock behind journal, age > 3 days `:210`); NTFS
   dirty/hibernated/unsure refused (`:363-375`), third-party FDE (`:383`), SMART failing
   (`:398`), battery (`:405`); image found and hashed (`:424-444`); boot-partition size from
   the image (`:459`); rescue copy sized, in RAM in `/run/aurstage` for no-stick
   (`:478-508`); GPT read, `shrink_ask` (`:529`), `shrink_pick_target` (`plan.c:268`),
   surface test of the freed region only (`:553`), `plan_compute` (`:569`), rescue capture
   (`:587`).
5. Destructive: `shrink_do` (`:607`); re-plan from the size reached (`:624`); no-stick rescue
   mirror into the gap first (`:642-663`); root write with the first MiB last
   (`image.c:566-665`); boot partition (`:690`).
6. Probe new root (`:709`), `commit_build` (`:731`), `plan_check` again (`:748`),
   `commit_table` (primary array, flush, LBA 1, flush, backup, read back; `commit.c:86-132`),
   `loader_register` BootNext only (`:782`, `nvram.c:432`), stick mirror (`:820`), e2fsck +
   resize2fs (`commit.c:175`), handover (`:860`).
7. Restore (`install.c:903-1025`): every saved copy verified (`:944`), off-disk copies
   preferred (`:987`), `rescue_restore` puts back table and ESP and grows NTFS.

**Invariants.**
- Only `wr.c` opens a disk for writing: `build/staging:111-168` refuses any other
  `O_RDWR`/`O_CREAT` open; only `nvram.c` writes EFI variables (`:233`).
- Every write inside a declared, non-overlapping window; out-of-window refused, not clamped
  (`wr.c:68-121`).
- Layout changes at one sector (LBA 1); primary array first so readers fall back to the old
  backup (`commit.c:86-107`); protective MBR never written.
- Nothing laid over an existing partition: `plan_check` (`plan.c:162-261`) in the plan and
  before commit (`install.c:748`); `gpt_add` refuses overlaps (`gpt.c:295`); Windows' entry
  can only shrink (`gpt.c:245`).
- Windows' entry never ends below its filesystem: rounded up (`plan.c:53`), re-planned from
  ntfsresize's real result (`install.c:624`).
- A half-written root never mounts: first MiB zero until read-back and hash (`image.c:566, 647`).
- No rescue copy, no install (`:587`); an already-damaged primary GPT is refused
  (`rescue.c:160`).
- Windows stays default: BootNext only (`:772`).
- NTFS mounted read-only only (ntfs3 `MS_RDONLY`, statvfs check, `winvol.c`).
- A fault-injection build cannot ship (`build/staging:315-327`).

**Tests.**
- `installtest`: full install on synthetic NTFS with a stick; Windows files byte-identical;
  sgdisk accepts the table; every refusal leaves the disk unchanged. KVM CI
  (`tests.yml:239`), green in old repo, 399 s.
- `nosticktest`: no-stick with real aurbridge-sim; rescue lands before the image; restore
  gives back the machine (45/45). KVM CI (`:240`).
- `putbacktest`: Settings → grub → `aurstage.restore` under SB; disk holds nothing to restore
  (`:243`).
- Unit in CI: `probetest`, `shrinktargettest`, `gapfencetest`, `bridgetest`.
- **Not in CI:** `powercuttest` (13 install + 5 restore instants, 138 checks;
  `powercuttest.sh:163-164, 228-229`; transcript 2026-09-22, a month older than
  `install.c`), `committest`, `gpttest` (22), `stagetest`, `matrixtest`.
- Real Windows: none.

**Gaps.**
1. **Probably refuses every install west of UTC.** Windows keeps the RTC in local time;
   AurBridge writes `written_unix = time(NULL)` (UTC) (`phases.c:1569`, checked); the staging
   kernel reads the RTC as UTC, so the clock looks behind the journal and `journal.c:404`
   refuses `clock-behind`. KVM uses `-rtc base=utc` (`tools/e2e/vm.py:348`), so tests cannot
   see it. Fix: ±14 h tolerance, or record the UTC bias.
2. **Wrong words after a failure.** Every `give_up` passes `windows_still_boots=1`, so
   "nothing on it has been changed" (`install.c:80`, checked) follows "The Windows drive is
   smaller" (`:652`, `:672`). `settle-failed` (`:848`) says Windows will start, but BootNext
   already points at AurOS. The `0` branch (`:119-123`) is unreachable.
3. **The shipped no-stick mode has no power-cut test.** `mirror-end` is not in the list. A
   retry re-shrinks the already-shrunk NTFS; Windows loses another 16-40 GiB each time
   (`:537`).
4. A cut inside ntfsresize is untested (`shrink-begin` fires before `shrink_do`, `:604`);
   no-stick relies on ntfsresize leaving the volume dirty (`:283`), unchecked.
5. Cut at commit-array then retry → "already damaged ... broken before it arrived"
   (`rescue.c:160`), blaming her machine. No resume case.
6. Restore never touches BootOrder: after confirm, a dead AurOS entry stays first.
7. Smaller: surface test over its 4 h deadline reported as "The drive is failing"
   (`shrink.c:447`, `install.c:556`); `shrink_do` has no timeout; `mkguid` fallback skips
   version/variant bits (`gpt.c:268-272`); `probe.h` claims Wi-Fi key carry-over that does not
   exist (`AB_CHOICE_BRING_WIFI` undefined).
8. Fast path not built in staging (`image_on=gap` refused, `:267`).
9. KVM uses `aurstage.min_gb=1`; the real 24 GB floor and big disks are unit-tested only.

**Where to change what.** Steps and wording: `install.c` (`install_run`, `restore_run`,
`refuse`/`give_up`). Boot/dispatch: `main.c`, `boot.c`; screen `src/aurscreen/screen.c`.
Journal: `journal.c` (writer: `src/aurbridge/format.c`, `phases.c`). Layout/shrink: `plan.c`,
`shrink.c`. GPT: `gpt.c`; commit/grow `commit.c`; fence `wr.c`. Image: `image.c`; boot
partition/BootNext `loader.c`, `nvram.c`. Rescue `rescue.c`; record `record.c`; power-cut
points `fault.c/h`. Gate: `ntfs.c`, `fde.c`, `health.c`, `probe.c`, `winvol.c`. Build:
`build/staging`. Tests: `tools/{installtest,nosticktest,powercuttest,putbacktest,committest,gpttest,stagetest}.sh`.

---

## 3. Boot (after the handover)

**Purpose.** AurOS keeps starting while she decides; her answer sets the default. Updates to
kernel, grub, shim must never stop the PC starting. Until she confirms, BootOrder says Windows.

**Flow.**
1. Staging writes the "AurOS" `Boot####` and reads it back (`src/aurstage/nvram.c:364-427`),
   arms BootNext and reads it back (`nvram.c:429-441`, from `loader.c:187-203`). Windows side
   arms its own BootNext (`plat_win.c:1128-1145`) with no read-back.
2. Every boot: `auros-hold.service` runs `aurfirst hold` before the desktop (`:18-19`).
   `af_look` finds our entry by its Hard Drive node's partition (`state.c:242-276`), falling
   back to the description "AurOS". `af_hold` re-arms BootNext with read-back
   (`state.c:370-394`); does nothing if answer files are unreadable, answered, or AurOS is
   already default (`:376-383`). State published atomically to `/run/auros-first.state`
   (`:335-368`).
3. First boot: `firstboot.sh` grows root (`:25-38`), applies `\EFI\AurOS\choices.conf`
   through allowlists (`choices.sh`), scans for Windows for Ferry (`:58-63`).
4. Her answer: desktop writes one word to `/run/auros/answer`; `auros-answer.path` starts
   `answer.sh` as root; `aurfirst request` opens with `O_NOFOLLOW|O_NONBLOCK`, rejects >1 hard
   link (`request.c:112,129`); words at `request.c:80-83`.
   - confirm: our entry first, all others kept; builds an order if none; read back, roll back
     on mismatch; clear BootNext; stamp (`state.c:435-575`).
   - decline: clear BootNext first, then stamp; if AurOS is first, move it down (`:577-636`).
   - import: Ferry, only after confirm (`main.c:144-155`; `answer.sh:130-171`).
5. Put Windows back: `answer.sh` checks `staging.efi`/`staging.img`, writes
   `next_entry=put-windows-back>put-windows-back-yes` to grubenv, `af_putback` arms BootNext
   even after decline (`state.c:410-432`), reboots (`answer.sh:172-222`). grub loads and clears
   `next_entry`, shows the menu 10 s (`grub.cfg.in:35-55`), boots staging with
   `aurstage.restore` (`:128-140`).
6. Updates: forge diverts `update-grub`/`grub-install` to `bootchain` (`build/forge:1084-1101`);
   dpkg Post-Invoke hook as second net (`bootchain-apt`). `bootchain menu` renders with a
   previous-kernel fallback (`bootchain:212-290`). `bootchain sync` checks the new shim against
   firmware db/dbx/SBAT and against grub, every menu kernel, MokManager and the restore kernel
   on every ESP; refuses if it cannot look (`:797-878, 885-935`); power-cut-safe atomic writes
   (`:111-147, 1006-1027, 1042-1185`); never writes NVRAM.
7. Build: `build/shimpick:31-62` requires a 2011-CA signature.

**Invariants.**
- Before confirm, BootOrder untouched: only `af_confirm`, `af_decline` (when AurOS first),
  `af_putback` write boot variables (`state.c:385,423,535,594,631`); staging has no BootOrder
  writer (gated by `nvramtest`); diverted `grub-install` cannot write NVRAM (`:16-18`).
- No entry dropped from BootOrder; inactive/hidden go to the back (`state.c:486-527`);
  confirm reads back and rolls back (`:540-563`).
- Oversized read = refusal, not truncation (`efivar.c:74`, `state.c:282-293`).
- Decline clears BootNext before the stamp, else refuses (`state.c:589-594`).
- "Cannot tell" never re-arms (`stamped()` −1, `state.c:33-38,376`).
- The menu keeps Put Windows back: `render_menu` refuses a template missing `auros` or
  `put-windows-back-yes` (`bootchain:271-274`); failed render keeps old grub.cfg (`:279-284`).
- Root never follows a path she controls (`request.c:112-129`; root-owned output dir).

**Tests.**

| Test | Checks | Where |
|---|---|---|
| `aurfirsttest.sh` | hold/confirm/decline/putback on fake efivarfs; ownership; request escalation shapes; description matches `loader.h` | unit, `tests.yml:128` |
| `nvramtest.sh` | staging `Boot####` bytes decoded by a 2nd implementation; write gate fires | unit, `:161` |
| `firstboottest.sh` | real image, persistent OVMF NVRAM: hold, yes, no, two AurOS entries (32) | KVM `:241`, **SB off** (`:53`) |
| `putbacktest.sh` | button → grubenv + BootNext → restore kernel under SB → `next_entry` used up | KVM, SB on, `:243` |
| `choicesboottest.sh` | `choices.conf` applied on first boot | KVM `:242`, SB off |
| `bootchaintest.sh` | menu + every sync refusal, 71/71, mutation-proven | **not in CI**; transcript 2026-10-04 |
| `bootupdatetest.sh` | real dpkg/apt shim+grub+kernel update under SB; previous-kernel fallback | **not in CI** |

Real Windows/firmware: nothing. New repo: `tests` has never completed (15/15 skipped, checked).

**Gaps.**
1. Windows started from the AurOS menu after confirm (grub → shim → bootmgfw,
   `grub.cfg.in:105-113`) changes PCR 7; BitLocker PCs may hit the recovery screen. Untested.
2. Stale `next_entry`: if firmware ignores BootNext after putback, Windows starts, grubenv
   keeps `next_entry`, and the next AurOS start auto-runs the restore after 10 s
   (`grub.cfg.in:49-53`; `answer.sh:184-201` only checks the BootNext read-back).
3. Nothing re-promotes AurOS after confirm if Windows/firmware puts Boot Manager first
   again (hold stops, `state.c:377`).
4. Restore leaves a dead "AurOS" entry first in BootOrder after confirm (no
   `nvram_boot_forget`).
5. `bootchaintest` and `bootupdatetest` are not in CI; a bootchain regression ships silently.
6. confirm/decline/hold unproven under Secure Boot.
7. A panicking kernel boot-loops: grub `fallback` only fires on load failure
   (`bootchain:212-224`); `panic=10` reboots into the same default; no boot counting.
8. Planned root encryption (D9) breaks grubenv on `($root)/boot/grub/grubenv`
   (`grub.cfg.in:35-48`), so `next_entry` stops working.
9. The website says "until Yes, switching on starts Windows", but the hold re-arms AurOS
   every boot (`website/index.html:287,360`).
10. Smaller: decline's BootOrder rewrite not read back (`state.c:631`);
    `plat_win.c:1139` no BootNext read-back; `aurfirst.h:157` omits `putback`; nothing gates
    "aurfirst is the only BootOrder writer" (`aurfirst.h:36-45`); `choices.sh` trusts lsblk
    PARTTYPE, which can be empty without udev's database (`bootchain:813-818`); `shimpick`
    matches the issuer by string (`:28`).
11. Dead code: `src/recovery/mkrecovery`, `test-recovery.sh`.
12. Not built: Put Windows back from inside Windows after a decline.

**Where to change what.** Our entry and stamps: `src/aurfirst/state.c:59-276`. NVRAM per
answer: `state.c:370-636`; raw efivarfs `efivar.c`. Words: `request.c:80-83`,
`answer.sh:111-208`. Hold timing: `auros-hold.service`; sandbox `auros-answer.service:20-31`.
Menu: `grub.cfg.in`; fallback `bootchain:153-290`; update gates `bootchain:885-1185`;
`build/shimpick`. Staging NVRAM: `nvram.c`, `loader.c:170-205`; Windows `plat_win.c:1128-1160`.
First boot: `firstboot.sh`, `choices.sh`. Diversions: `build/forge:1084-1126`. CI matrix:
`tests.yml:236-243`.

---

## 4. Desktop (aurshell, aurwl, common, aurora, themes, shells)

**Purpose.** `aurshell` is the whole desktop: draws to KMS, reads input directly, embeds the
`aurwl` Wayland server. Six archetypes (`shells/*.shell`, `src/aurshell/layouts/`): rail
(default), tiles, locked, taskbar, dock, workbench. The band (Help / Internet / Smaller /
Bigger / Turn off) is in all of them and cannot be covered. Panels: Settings, Wi-Fi,
Bluetooth, welcome. `src/common`: theme parser, font rasteriser, PNG, wallpaper.
`src/aurora/aurora`: `.theme` → `shell.conf`, `gtk.css`, etc.

**Flow.**
1. `aurshell.service`: `dbus-run-session -- aurshell $AURSHELL_SAFE`, `PAMName=aurshell`;
   failure shows `aursorry` and restarts without limit (`StartLimitIntervalSec=0`).
   `shell-guard:44-56`: 3 failures in 10 min → safe mode.
2. `main()` `main.c:667`: `flatpak_data_dirs()` (`:668`); theme load (`:744`); `PICK_HERS`
   (`:754-755`); `shell_theme_load` (`:759`); `load_policy` (`:763`);
   `shell_archetype_load` (`:765`, fallback locked on kiosk, else rail);
   `shell_seed_apps` + `shell_scan_apps` (`:788`, `apps.c:551`); `appwatch_open` (`:800`).
3. Headless `--png` (`:822-951`), same paint path; `--with-app` runs a real program in aurwl.
4. `kms_open` (`:1039`); `console_take(...,!allow_tty)` (`:1047`); body height = screen −
   `foot_height` (`:1050`); `aurwl_create` (`:1079`); `input_open_all` (`:1101`, exclusive
   on kiosk).
5. Main loop (`:1179`): reload on SIGHUP/`want_reload` (`:1180-1270`); panels (`:1311`);
   blanking; appwatch rescans (`:1378`); `settings_step`, `watch_tick`, `welcome_step`;
   paint and input (`:1950-2200`).
6. Launch: `shell_launch` (`shellcommon.c:666`) → `session_spawn` (`session.c:347`) →
   `aurwl_spawn`. `Exec=` split without a shell (`apps.c:309`); PATH resolve (`:370`); policy
   allow/deny (`:511`).
7. Settings: layout replaces symlink `~/.config/auros/active.shell` (`settings.c:1062-1078`);
   look runs `aurora set` with `AURORA_OUTDIR` = her dir (`:1081-1135`); both reload.
8. Welcome: opens only when `/run/auros-first.state` says `phase=asking`
   (`welcome.c:145-156`); answer written `O_EXCL` to `/run/auros/answer`; reply read from
   root-owned `/run/auros-answer/result` (`:166-200`).

**Invariants.**
- Put Windows back takes two presses ≥1.5 s apart; first only warns (`settings.c:992-1031`);
  shell writes only `putback`; root refusals shown (`:1324-1347`).
- Desktop never acts as root: bare word, `O_CREAT|O_EXCL`, 0600 (`settings.c:401-409`).
- Bad policy file locks everything incl. programs (`main.c:504-532`).
- No shell interprets `Exec=` (`apps.c:296-336`); locked machines ignore her `.desktop`
  files (`:593-598`).
- Band unreachable by layouts (`main.c:1050, 1269`); fixed colours and text floor
  (`foot.c:198-206`).
- Never drops to a login prompt (`Conflicts=getty@tty1`, unlimited restarts).
- Installer theme names validated `[a-z0-9-]{1,32}` and must exist (`choices.sh:223`).
- Theme numbers sanitised (`common/theme.c:96-114`), geometry clamped
  (`shellcommon.c:476-485`), font sizes clamped (`:612-616`).
- aurwl: shm only, `MAX_DIM`, stride checks (`aurwl.c:480-497`); per-client object caps
  (`:60-62, 1209-1240`); clipboard only from the focused program (`:1408-1431`).

**Tests.** In `tests.yml` unit (`:131-177`): `choicestest.sh` (42), `storetest.c` (108:
"Get more programs" in ≤2 presses on every home, 4 sizes × 2 text sizes; new program
appears), `looktest.c` (GTK follows a look change), `sessionenvtest.c` (env to user bus).
KVM: `firstboottest` reaches `aurshell.service` active (32/32 old repo) but writes the
answer file directly (`firstboottest.sh:129`), never drawing the panel. Real hardware:
nothing. **Not in CI:** `welcometest`, `targets.c` (44 px tap targets), `wltest`,
`wlstress.sh`, `wlhostile`, `font_test`, `tools/fuzz/regress.sh`, `kiosktest`, `launchtest`,
`modaltest`, `hittest`, `notifytest`, `nettest`, `padtest`, `powertest`, `keytest`, `bttest`,
`theme-test.sh`.

**Gaps.**
1. **Bug: her look resets on every restart (checked).** `main.c:744` loads the theme from
   `conf` while it is still `/etc/auros/shell.conf`; `PICK_HERS` switches to hers only at
   `:754`. Layout survives, look does not; Settings still shows her choice (`settings.c:258`).
   Fix: move `PICK_HERS` above `:744`. `looktest` does not link `main.c`.
2. Policy booleans only match exact lowercase `yes`/`no` (`main.c:534-539`).
3. Policy values cut at 191 chars (`theme.h:15`), lines at 511 (`theme.c:67`); deny-list
   matches file name only (`apps.c:493-510`).
4. Her `shell.conf` (writable by her programs) can name any font, parsed in the shell
   (`shellcommon.c:553`); font fuzzers not in CI; `tools/fuzz/crash-9339…` is loose.
5. `foot_load_text_scale` accepts "nan" (`foot.c:228-230`); `foot.c:99` `(int)(96*NaN)` is UB
   in the band.
6. Welcome reply check has no timestamp (`welcome.c:180-195` vs `settings.c:1329`).
7. A second Put Windows back press under 1.5 s does nothing silently (`settings.c:1016`).
8. Stale `main.c:4-5`; Settings lists 4 of 6 themes (`settings.c:361`).
9. A downloaded `.deb` does not install (gdebi via pkexec has no screen); never run on a PC.

**Where to change what.** Startup/policy/input/loop/reload `main.c`; contract `shell.h`;
helpers/theme/fonts/launch `shellcommon.c`; archetype `layouts/<id>.c` + `shells/<id>.shell`;
apps `apps.c`, `appwatch.c`; spawning `session.c` → `src/aurwl/aurwl.c`; band `foot.c`;
`net.c`, `bt.c`, `power.c`, `notify.c`, `osd.c`, `watch.c`; `settings.c`, `welcome.c`,
`sorry.c`; `run.c`, `kms.c`, `pad.c`; themes `common/theme.c`, `aurora/aurora`,
`themes/*.theme`, `themes/templates/`, `common/wall.c`, `common/font.c`; service
`aurshell.service`, `shell-guard`; build `build/forge:921-1005`.

---

## 5. Image (forge, mkimage, all, harden, profiles, rootfs, update channel)

**Purpose.** Builds the raw GPT image (FAT32 ESP + ext4 root, Ubuntu 24.04 noble) that is
copied byte for byte. Profiles: desktop, office, revive, school-kiosk, multilingual.

**Flow.**
1. `build/all desktop --keep=raw` (`build.yml:196`); disk-space refusal
   (`build/all:111-123, 262-297`); `forge build` + `mkimage` (`:301-302`); hash + manifest
   (`:173-188`).
2. Stage 1: debootstrap minbase noble (`forge:180-197`), cached in CI (`build.yml:149-182`).
3. Stage 2 (`:200-275`): snap pinned out (`:291-307`); one `apt-get install` (`:220-240`);
   excludes purged (`:252-255`); browser via mozilla-apt (fingerprint `:695-730`) → PPA →
   hash-checked tarball (`:768-804`), accepted only if a real program exists (`:665-675`,
   `:829-870`); store: gnome-software, flatpak, portals (`:382-411`); Flathub now or at first
   online (`:427-459`, `software-ready.sh`); snap leftovers and DC firmware removed
   (`:271-272`).
4. Stage 3: C programs compiled in chroot (`:900-1006`); `rootfs/` overlay (`:1058`);
   diversions (`:1095-1101`); grub debconf preseed (`:1113-1120`); units (`:1122-1135`).
5. Stage 4: os-release, locale, user, polkit (`:1725-1765`), zram, earlyoom;
   `stage_harden` (`build/harden:32-66`): nftables, cups-browsed purged, avahi quiet.
6. Stage 5: theme (`:1871-1931`).
7. Stage 6: `aptchannel image` packs `auros-*` debs (`aptchannel:380-429`); seal
   (`forge:2014-2037`); preconditions (`:1949-2000`); `harden_verify` (`harden:68-76`);
   mksquashfs (`:2052`); browser refusals (`:2095-2140`).
8. mkimage: sparse image, sgdisk, mkfs, tar copy (`mkimage:44-94`); fstab and
   `/etc/default/grub` (`:102-126`); grub.cfg via `bootchain menu` (`:162-168`); `shimpick`
   (`:206`); signed shim+grub to ESP (`:238-259`); ESP chain check (`:279-301`).

Size: rootfs 2.8-3.3 GB (Sept log), ~5.3 GB raw; the later ~650 MB cut is unmeasured.

**Invariants.**
- Signed chain only: no grub-install; shim without MS 2011 signature refused
  (`mkimage:191-206`); `core.efi` marker (`:219-220`); `BOOTX64.CSV` (`:250`); build stops if
  grub.cfg lacks root UUID or kernel (`:165-168`) or ESP incomplete (`:301`).
- Ubuntu's update-grub never replaces the menu; grub never enumerates Windows' ESP
  (diversion verified at seal `forge:1987-1991`; preseed read back `:1118-1120`).
- Required units really enabled (link checked, `forge:166-177, 1976-1984`).
- No snap, real browser (pin, purge, seal refuses `:2018-2023`; CI without fallback
  `build.yml:191-196`).
- Kernel metapackage survives firmware trim (`:360`).
- Build host safe: overlay refuses `dev/ proc/ sys/` (`:1067-1077`); `forge clean`, never
  bare `rm -rf` (`build/all:274-282, 374-382`); decompress + re-hash before delete
  (`:328-341`).
- ext4 journal on (`mkimage:80-86`). CI image hash = manifest (`build.yml:229-232`).
- Update channel: dpkg verifies packed files (`aptchannel:400-402`); no source without a key
  (`:408-412`); AurOS repo may only supply `auros*` (`preferences.d/auros`).

**Tests.** `hardentest.sh` (build gate, every image build); `hardentest-mutants.sh` (not in
CI); `nmtest`, `storetest` (`tests.yml:145,170`); `firstboottest`, `choicesboottest`,
`putbacktest` (KVM, `:99-104`); `profiletest` (`profiles.yml`). Not in CI:
`bootupdatetest`, `bootchaintest`, `theme-test`, `filetypes`, `scamtest`. Referenced but
missing: `tools/updatetest.sh`, `hardenboottest.sh`. Proof: build green on Actions in the
old repo (`38e1959`); new repo's only real build (39901973428) still **queued** at 04:58 UTC;
boot KVM only; Firefox only via mozilla-apt; update channel never end to end.

**Gaps.**
1. **Every machine has the same password with sudo (checked).** Groups include `sudo`
   (`forge:1358`); `chpasswd` `auros`, merely expired, `|| true` (`:1385`). Nothing changes
   it later. Any program running as her can become root; console login on by default; GRUB
   offers `single init=/bin/bash` (`grub.cfg.in:91`, checked); disk unencrypted. Kiosk
   `student` same.
2. **Passwordless root grant:** `com.ubuntu.pkexec.gdebi.gtk` (`forge:1618`, checked) lets
   her install any .deb as root; lead decided it must go; `.deb` still opens in gdebi.
3. **Firefox never updates:** unattended-upgrades lists only `origin=AurOS`; Firefox updater
   off (`policies.json:3`); tarball frozen at 140.0 (`forge:788`); Flathub apps no background
   updates (`software-ready.sh:56`).
4. Base packages ship unpatched: no `apt-get upgrade` ("36 not upgraded",
   `build-all.txt:899`).
5. Update channel off in every image: no `auros-archive-keyring.asc`; `aptrepo.yml`,
   `docs/security/UPDATES.md`, `HARDENING.md`, `FINDINGS.md` missing.
6. mkimage copies the build host's `/etc/resolv.conf` (`mkimage:113`), undoing
   `forge:121-130`.
7. Shipped ≠ checked: `tar` without `--xattrs/--acls` (`mkimage:94`) drops file caps and
   ACLs; mkimage rewrites `/etc/default/grub` after the gate (`:115-126`); squashfs built then
   deleted unread (`build/all:360,383`).
8. Office lockdown does nothing (negation-only sudoers `forge:1810-1813`, user in sudo).
9. Any program running as her can mount Windows read-write without a password (udisks
   `filesystem-mount-system`, `forge:1748`).
10. Mozilla pin is `Package: *` at 1000 (`forge:723-727`).
11. Silent failures: build-dep purge `|| true` (`:1141-1145`), exclude purge `|| true` (`:254`).
12. Dead: `build/config.sh`, `build/aurb`, `pkg/core/*`.

**Where to change what.** Packages/policy knobs `profiles/desktop.profile`; install/snap/
browser/Flathub `forge:200-870`; compile/overlay/diversions/units `:873-1259`;
account/polkit/sudoers/zram `:1262-1868`; seal `:1949-2055`; firewall `build/harden`,
`tools/hardentest.sh`, `rootfs/etc/{sysctl.d,modprobe.d,nftables.conf}`; partitions/ESP
`build/mkimage`, `build/shimpick`, `grub.cfg.in`; profiles/disk `build/all`; channel
`build/aptchannel`, `rootfs/etc/apt/...`, `docs/OWNER-SETUP.md` §4a; auto-updates
`apt.conf.d/{20auto-upgrades,51auros-unattended}`; Firefox `policies.json`; CI
`.github/workflows/{build,tests,profiles}.yml`.

---

## 6. CI (build, tests, release, windows, e2e)

**Flow.** Trigger words from the commit subject only (`build.yml:72`, `release.yml:93`,
`tests.yml:69`, `othersys.yml:40`); all on `push: [main]`.
- `build.yml`: `gate` (`:69-88`); `build` restores debootstrap cache (`:157-189`), `build/all
  desktop` (`:196`), `build/staging` (`:218`), hash vs manifest (`:231`), 90 MiB pieces
  (`:237-251`), `AUROS_RELEASE=1 build/aurbridge` (`:269`); asks the .exe what it carries
  (`:283-296`); `getimage` over local Range HTTP (`:304-309`); `tests` reusable (`:362`);
  `release` only on `[release]` (`:368-381`).
- `tests.yml`: `unit` 16 suites (`:128-210`, each step runs, job fails overall); `e2e` 5-test
  KVM matrix, max-parallel 2 (`:236-243`); qemu wrapper tcg→kvm (`:263-275`).
- `release.yml`: `which` (`:69-121`; `[publish]` needs build, unit and ≥5 e2e jobs green,
  `:100-110`); `publish` (`:123-261`; pieces vs `pieces.txt` `:169`; always prerelease `:229`;
  read back through 302, Range 206 `:234-261`); `windows` (`:263-310`, navtest + WinHTTP
  getimage); `promote` (`:312-349`); `pages` (`:357-391`).
- `windows.yml`: `windows-source` (preflight, RST walk, Fast Startup, junction-safe cleanup,
  resume, ESP, navtest/selftest, VERSIONINFO/icon); `windows-published`, `windows-known-bad`,
  `windows-download` against old-repo pins; `gapselftest` (`:360-478`, VHDX shrink + gap
  write, verified, undone).
- `e2e.yml`: gate (`:47-83`), variant matrix (`:85-205`), M1/M2/M3 via `tools/e2e/e2e.py`
  (`cmd_flow` `:820-856`, `cmd_inspect` `:860-921`); evidence to prerelease `e2e-<run>`.

**Invariants.** Published = tested (`[publish]` reuses artifacts `release.yml:145-156`; tags
from baked `pieces.txt` `:162`; existing tag's exe must match `:212-214`). Nothing public
unproven (prerelease first `:229`; promote needs pinned-hash e2e `:325-341`; `e2e.py:440`
rejects wrong hash). Image intact end to end (`build.yml:231`, `release.yml:169, 246-249,
307-309`, `tests.yml:318-320`). Release installer carries staging/shim/address
(`build.yml:283-293`). Gap writer refuses system disk and GUID mismatch, fences checked,
chkdsk compared (`windows.yml:403-459`). Fix cleanup never deletes through a link
(`:164-181`). Known-bad must still fail (`:311-330`). Pages never deploys a 404 button
(`pages.yml:67-87`, `release.yml:371-376`).

**Proof levels.** Real Windows Server (read-only): windows.yml. Windows 11 in KVM: **M1 green**
(39891036021, 39891768538), **M2 never past the download**, M3 never reached. KVM sim:
installtest, nosticktest, firstboottest, choicesboottest, putbacktest. Unit/Wine: aurfirst,
choices, nm, sessionenv, look, nvram, bridge, help, store, shrinktarget, probe, gapfence,
manifest, Ferry, sim selftests, `aurbridge.exe selftest`, signtest. Not in any workflow:
powercut, loader, matrix, bootupdate, bootchain, stage, exe, gpt, ntfs, fail, commit, screen,
health.

**Gaps.**
1. **Promote can never succeed (checked).** `release.yml:333` selects `.name == "e2e"`; jobs
   are `e2e (base)` etc. since `f70760f`. `:330` scans only the newest 50 successful e2e runs,
   and every push makes a gate-only success, so a real pass ages out in hours. The site never
   deploys from release.yml.
2. **windows.yml never finishes in the new repo (checked).** Each push cancels the previous
   run (11 cancelled in a row). `875fd9f`/`4ba4a5a` fixes are therefore unproven.
3. **e2e.yml tests an old installer** (`INSTALLER_URL`/`SHA256` at `e2e.yml:35-36` point at
   the old repo's `AurOS-Installer-test.exe`); `windows-published`/`windows-download` likewise
   (`windows.yml:29-37`).
4. Data-safety suites (powercut, loader, matrix) last ran on the v4 image.
5. `e2e.yml:60` matches `[e2e]` anywhere in the message; no `timeout-minutes` on e2e `gate`
   (`:47`), othersys `gate` (`:25`) and `prep` (`:68`); unbounded `apt-get`
   (`build.yml:140`, `tests.yml:90,254`, `runner-setup.sh:37`).
6. Windows ISO not hash-pinned (`e2e.yml:41`, `get-windows.sh:53`); M1's Windows has Smart App
   Control in evaluation, a likely next blocker for the unsigned exe; gapselftest BitLocker
   step passes when BitLocker is absent (`windows.yml:464-467`).
7. Stale docs: `docs/RELEASE.md`, `tools/e2e/README.md:41,86` (`claude/**`), `RESUME-NOW.md:143`.

**Where to change what.** Triggers `build.yml:69-88`; tag/URL `:115-125`; exe gates
`:279-296`; tests `tests.yml:128-210, 239-243`; publish/promote/pages `release.yml:157-232,
325-349, 357-391`; installer under e2e `e2e.yml:35-37`; variants `:44, 116-125`,
`e2e.py:85-89`; Windows checks `windows.yml`; driver `tools/e2e/{e2e,vm,vision}.py`,
`guest/` (editing anything but `helper.ps1` invalidates the cached Windows), `publish.sh`;
profiles `profiles.yml:95-105` → `tools/profiletest.sh`.

---

## 7. Front (website, download, help, trust docs)

**Flow.** `tools/site/stamp.sh:30-35` fills `@AUROS_REPO@`/`@AUROS_ADDRESS@` from
`tools/repo.sh` and refuses leftovers (`pages.yml:62,97`, `release.yml:386`). One download
address: `#get` href (`index.html:39`), rebuilt by `door.js:29-37`, copied to every
`[data-download]` (`:41`); no `download` attribute, no SHA check on the page. Browser steps
`door.js:46-57`, `index.html:100-173`; Windows warnings `:186-219`; Smart App Control
`:224-265`. Computer detection `door.js:61-127` (phone/Mac/Chromebook/Linux → "open on the
Windows computer" `:75-100`; ARM/32-bit refused via `getHighEntropyValues` `:121-126` or UA
`:114-117`; NT 5/6 warned `:106`; unsure keeps the button). Installer links to
`advanced.html` (`fix_win.c:168`, `build/aurbridge:192`). Deploy gated on the release URL
answering 200/206 (`pages.yml:67-87`). Six static offline help pages.

**Invariants.** Only download is the latest full release (`check.mjs:261,268, 413-434`;
release SHA compare). No external requests (`check.mjs:217-220,407`). Never offered a file
that cannot run (`door.js:102-126`, `check.mjs:276-304`, 9 UAs; preflight is the backstop).
Readable: ≥18 px, contrast ≥4.5:1, button above the fold, no sideways scroll
(`check.mjs:227-236, 249-317`, only `index.html` and `advanced.html`). Help offline, her words
(`helptest.py`, `tests.yml:167-169`).

**Tests.** `check.mjs` green on the new repo (04:50). `helptest.py` 33 checks (static).
e2e M2: real Edge on Windows 11 KVM downloaded 34.9 MB (run 39894079407) then stalled at
Edge's flyout. Chrome, Firefox, SAC steps, Windows 10, real PC: untested.

**Gaps.**
1. BitLocker decryption contradicted across `fix_win.c`, D11, `SECURITY.md:58`,
   `how-it-works.html:123`, `TRY-IT.md:32`; "About 10 minutes" (`index.html:43,338`) false on
   an encrypted PC.
2. Help button never opens the help pages; it shows a fixed panel (`foot.c:268-300`);
   `SUPPORT.md:11-13` promises otherwise.
3. `help/windows.html:18` ("a few seconds", "straight away") breaks EASY rule 5.
4. Smart App Control advice disagrees (`index.html:264`, `SIGNING.md:30-33`,
   `TRY-IT.md:93-94`).
5. Stale public facts: "No Firefox in this build" (`how-it-works.html:233-235`); "12 October
   2027" for Windows 10 (`how-it-works.html:35`, `drive.js:259`) needs verifying; "18 power
   cuts" is from v3 with a stick (`:212`); test table hard-coded.
6. Old-repo URLs in `SECURITY.md:12,86`, `SUPPORT.md:25-35`, `TRY-IT.md:88`; `SECURITY.md:23`
   no contact.
7. `release.yml:357-391` deploys without `check.mjs`.
8. `stamp.sh` was GNU-only `sed -i`; fixed in `eb28a52` ("stamp.sh works on a Mac too").
9. `check.mjs` floors cover 2 of 6 pages; Chromium only.
10. Size claims disagree: `TRY-IT.md:12-14` (8 GB kept, 50 GB free) vs 20 GB
    (`index.html:340`, `wizard.c:2622-2624`) vs 8 GB floor (`preflight.h:163`).
11. Legal drafts unpublished with `[OWNER]` placeholders; no GPL source offer; footer's
    "doesn't send anything anywhere" vs GitHub logging IPs; support needs a GitHub account.
12. Dead `site.js:88-155`; internal `website/IDEAS.md` is published.

**Where to change what.** `website/assets/door.js` (`SIGNED` `:15`); `index.html`,
`door.css`; `advanced.html`, `guide.html`; `how-it-works.html` + `drive.js/css`;
`desktop.html`, `wallpaper.html`; redirect stubs; `tools/site/stamp.sh`, `tools/repo.sh`;
`tools/site/check.mjs`; `pages.yml`, `release.yml` `pages`; e2e driving `e2e.py:214-250,
361-531`; help `rootfs/usr/share/auros/help/`, `tools/helptest.py`, entry point
`foot.c:268`; words `SIGNING.md`, `SUPPORT.md`, `SECURITY.md`, `docs/legal/*`, `TRY-IT.md`.

---

## 8. Rest (Ferry, other systems, security and planning docs)

**Ferry flow.** `firstboot.sh:58-62` → `ferry-detect` → `/var/lib/auros/windows-found.json`;
`welcome.c:70-83` shows "Bring my files across" if it has `"device"`; press writes `import`
(`:543-549`); `answer.sh` asks `aurfirst ferry` (`:136`), runs `ferry run --auto` (`:145`),
maps exit 0 ok / 3 refused / else partial (`:160-169`). `ferry:100-119`: read-only mount,
volume check, stages wifi → settings → files → firefox → report. Shared rules
(`ferry-common.sh`): `mount_ro` `:369-385`; `vol_state`/`require_clean_volume` `:293-363`;
`safe_copy` `:478-500` (`.ferrypart` then `mv`); `resolve_known_folder` `:632-671`
(OneDrive redirects). `ferry-files:93-142` classifies with `ferry-winattr`; `ferry-wifi` names
only (`:59-62`); `ferry-firefox` skips password/card files; `ferry-settings` leaves installer
choices (`:85`). Install list `src/ferry/install.sh` (`forge:916-918`).

**Other systems.** Windows side not built (no partitioner, no `layout.json`, no OpenPGP
verifier, no `Boot####`). Built: `ubuntu/plan.py:61-73` (refuses partitions without role or
type), `verify.py:25-57` (Canonical signature + ISO hashes), `othersys.py` (`cmd_early` `:320`
compares layout `:285-316`; `cmd_late` `:564` Windows first, cancels MOK; `remove_iso` `:599`
only on GUID+name+UUID match; `hold` `:655`, `confirm` `:691`, `decline` `:701`).

**Invariants.** Ferry: Windows never written (ro mount + `/proc/mounts` read-back
`:374-381`; tree hashed before/after `tests/run.sh:225-229`); no import before confirm
(`answer.sh:136`, `aurfirsttest.sh:146-165`); stale hibernated volume → exit 3
(`:336-363`); cloud-only OneDrive never copied as stubs (`ferry-files:112-117`); nothing
overwritten (`safe_copy` `:480-487`); no passwords. Othersys: curtin cannot delete/retype
Windows partitions (`preserve` + type); stops if the disk differs (`othersys.py:320-326`);
Windows first until she says yes (`:529-547`).

**Tests.** `src/ferry/tests/run.sh` (~49 checks, fake tree with `user.ntfs_attrib` xattrs,
`tests.yml:190-193`); `aurfirsttest.sh` import gate; `firstboottest.sh` does not exercise
import; e2e presses "Not now" (`e2e.py:729-733`); `tools/othersys/selftest.py` (81,
`othersys.yml`); `vmtest.py` last green on old repo run 39897837863 (`475c8bf`). New-repo
othersys runs are 5-12 s gate-only. Nothing on real Windows or real NTFS.

**Gaps.**
1. **"Bring my files across" fails on every machine (checked).** `auros-answer.service:21-22,31`
   sets `ProtectSystem=strict`, `ProtectHome=yes`, writes only to `/var/lib/auros /var/log
   /run/auros-answer /run/auros /boot/grub`. Ferry needs `/var/lib/ferry`, `/run/ferry/windows`
   and her home, so `ferry_state_init` (`:97`) exits 1 → "partial" → "Some things could not be
   brought across" with nothing copied. No test runs import in this sandbox.
2. **Latent root escalation once #1 is "fixed" by opening paths:** `xdg_dir`
   (`common:424-437`) trusts `~/.config/user-dirs.dirs`; `XDG_DOCUMENTS_DIR="/etc"` → root
   copies into `/etc` then `chown -R her /etc` (`ferry-files:201-204`); `safe_copy` follows
   symlinks as root (`:490-491`); dangling-symlink write (`ferry-files:233-240`).
3. `collision_name` splits on the full path (`common:464-469`); a second collision overwrites
   the first copy (`:482-491`).
4. BitLocker PCs get nothing; JSON mode skips silently (`ferry-detect:340-345`).
5. Possible false "hibernated" refusal on any non-zero `hiberfil.sys` signature
   (`common:298-303`); no retry path (`welcome.c:156, 551`).
6. D9 ignored: THEFT.md says Ferry waits for encryption; `answer.sh` imports at once.
7. OneDrive classifier only tested on `user.*` xattrs; ntfs3 may not expose reparse data.
8. `hivepeek.c:74,85` negates `INT32_MIN`; no Ferry fuzz harness despite `FERRY.md:149`.
9. Docs vs code: `FERRY.md:90-94` (passwords migrate), `PLAN.md:118` (musl/busybox),
   `PLAN.md:195` (Ferry "done"), `RELEASE.md:12-15`, `research/README.md`, journey audit at
   `424ed96`.
10. Minor: `ferry-files:224` `grep -c ... || echo 0` prints "0 0"; `--all` unused
    (`ferry:101-102`); ~26 ms per file.

**Where to change what.** Privilege model (fix #1 and #2 together): run Ferry as her uid, or
`StateDirectory=ferry` + scoped `ReadWritePaths` + validated `xdg_dir`; files
`auros-answer.service`, `answer.sh:130-170`, `ferry-common.sh:408-443, 478-500`; stream R's
planned `copy.c` (her uid, `RENAME_NOREPLACE`). Stages `src/ferry/ferry-*`; gate
`ferry-common.sh:293-363`; `winattr.c`; `hivepeek.c`; `install.sh`; welcome wording
`welcome.c:413-445, 520-560`. Othersys `src/othersys/ubuntu/{plan,othersys}.py`,
`systems/*.json`, `docs/OTHER-SYSTEMS.md`, `tools/othersys/*`, `othersys.yml:92`. Security:
`docs/security/PLAN.md`, `THEFT.md`; S4.md lists files to create.

---

## Top risks

| # | Risk | Evidence | Area | Severity |
|---|---|---|---|---|
| 1 | "Fix these for me" runs `manage-bde -off` on every fixed drive, no recovery-key step, page still says nothing changed | `fix_win.c:21-30, 329-353`; `wizard.c:2076`; contradicts D11 | aurbridge | blocker |
| 2 | Staging likely refuses every install west of UTC (RTC local time vs UTC journal → `clock-behind`) | `phases.c:1569`; `journal.c:404`; `vm.py:348` hides it | aurstage | blocker |
| 3 | No install has ever run on real Windows; e2e M2 stalls at Edge's flyout, M3 never reached | `F.md:22`; `SESSION-2026-10-23-B.md:12` | ci | blocker |
| 4 | Promote can never succeed (job-name mismatch; 50-run window), so no release goes "latest" and the site never deploys from release.yml | `release.yml:330, 333` | ci | blocker |
| 5 | Same password `auros` with sudo on every machine; `init=/bin/bash` in GRUB; console login on | `forge:1358, 1385`; `grub.cfg.in:91` | image | blocker |
| 6 | "Bring my files across" fails on every machine (systemd sandbox) and reports "partial" | `auros-answer.service:21-22,31`; `ferry-common.sh:26-27,97` | rest | high |
| 7 | Passwordless root via pkexec gdebi rule | `forge:1618` | image | high |
| 8 | Firefox and Flathub apps never update; base packages unpatched | `policies.json:3`; `forge:788`; `build-all.txt:899` | image | high |
| 9 | Fix empties Recycle Bin and update rollbacks permanently | `fix_win.c:280, 310` | aurbridge | high |
| 10 | No power-cut test of the shipped no-stick mode; retry re-shrinks Windows 16-40 GiB each time | `install.c:537`; powercuttest list | aurstage | high |
| 11 | Failure messages say "nothing has been changed" after a shrink; `settle-failed` says Windows will start while BootNext is armed | `install.c:80, 652, 672, 848` | aurstage | high |
| 12 | Closing the wizard mid-phase skips undo but promises Windows is unaffected | `wizard.c:1452, 4108` | aurbridge | high |
| 13 | Resume task runs an exe from Downloads elevated at logon (UAC bypass) | `fix_win.c:385` | aurbridge | high |
| 14 | Her chosen look resets on every restart | `main.c:744` vs `:754` | desktop | high |
| 15 | Windows via AurOS menu may trigger BitLocker recovery (PCR 7) | `grub.cfg.in:105-113` | boot | high |
| 16 | Stale `next_entry` can auto-run the restore weeks later | `grub.cfg.in:49-53`; `answer.sh:184-201` | boot | high |
| 17 | windows.yml never completes in the new repo (each push cancels it); e2e and windows checks pin the old repo's installer | `gh run list -w windows`; `e2e.yml:35-36`; `windows.yml:29-37` | ci | high |
| 18 | Data-safety and boot-update suites (powercut, bootchain, bootupdate, gpt, commit) not in CI | `docs/results/*` transcripts only | ci | high |
| 19 | Latent root escalation in Ferry (`xdg_dir`, symlinks) once its sandbox is opened | `ferry-common.sh:424-437, 490-491`; `ferry-files:201-204` | rest | high |
| 20 | Kernel panic boot-loops; no boot counting | `bootchain:212-224`; `panic=10` | boot | medium |
| 21 | Restore leaves a dead AurOS entry first in BootOrder | `state.c:65-67` | boot | medium |
| 22 | Fast Startup/hibernation left off; cancel leaves 5-10 GB + armed `Boot####` | `phases.c:1116, 1830` | aurbridge | medium |
| 23 | Any program as her mounts Windows read-write without a password | `forge:1748` | image | medium |
| 24 | Update channel off in every image (no keyring) | `aptchannel:408-412` | image | medium |
| 25 | Public pages contradict code (BitLocker, Firefox, power cuts, sizes, SAC, hold) | `how-it-works.html:123, 233`; `index.html:264, 287` | front | medium |
| 26 | Help button never opens the help pages | `foot.c:268-300` | front | medium |
| 27 | `release.yml` deploys the site without `check.mjs` | `release.yml:357-391` | front | medium |
