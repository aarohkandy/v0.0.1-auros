# Stream BT: the boot chain, first boot, and every way back

Owns: `src/aurfirst/**`, `src/recovery/**`, `rootfs/usr/lib/auros/{bootchain,bootchain-apt,
grub.cfg.in,answer.sh,grubenv,start-windows,boot-ok,desktop-up,keeper,rollback*}`,
`rootfs/etc/systemd/system/auros-{answer,hold,keeper,boot-ok}.*`, `rootfs/etc/fwupd/**`, and
`tools/{firstboottest,choicesboottest,bootchaintest,bootupdatetest,putbacktest,windowsboottest,
rollbacktest,fwupdtest}.sh`. S1 edits only the kernel command line in `grub.cfg.in`; R needs one
sandbox change in `auros-answer.service`; C touches the menu only through us.
(`rootfs/usr/lib/auros/rollback`, `keep-previous` are S5's package rollback, not ours.)

## Goal

Atlas / FINDINGS items that strand her or lose her Windows, each fixed with a test that fails
before and passes after:

1. HIGH stale `next_entry` could run the restore weeks later
2. HIGH "Windows" from the menu chainloaded bootmgfw (BitLocker recovery screen) (R-10/17/22)
3. BLOCKER the menu could be edited (`init=/bin/bash`), recovery shell entry
4. WP-30 boot counting and fallback (S-07, S-18)
5. dead AurOS entry first in BootOrder after a restore
6. S-05/S-06 hold before the desktop; S-24 Windows moves itself first; S-25 `grub>` prompt

## Done (all proven on homebase KVM, Secure Boot on, unless said)

- **1 stale restore** `0180b9e`. `answer.sh` writes `auros_asked=<RTC UTC s>` beside `next_entry`;
  grub honours it only with that, clears both, passes `aurstage.asked=`; `restore_must_be_fresh()`
  in `src/aurstage/main.c` (A2's file, one function and one call) refuses > 1 h old or > 10 min
  ahead, changes nothing, restarts. `putbacktest` boots 4-6 (three weeks later via `-rtc base=`):
  red on the unpatched image (3/24), green patched (24/24). `aurfirsttest` covers the clock.
- **2 Windows from the menu** (`start-windows`, `grubenv`, `aurfirst windows`/`af_windows`,
  grub entry = AurOS kernel with `init=/usr/lib/auros/start-windows`). It sets BootNext at the
  entry that starts `\EFI\Microsoft\Boot\bootmgfw.efi` (found by file, not name) and restarts: the
  firmware starts Windows. Fallback `auros_win=chain` (one grub chainload) only when there is no
  such entry AND start-windows found no `-FVE-FS-` volume in Linux. `tools/windowsboottest.sh`
  (swtpm + OVMF Microsoft keys; stand-in Windows prints PCR 4/7/BootCurrent): A normal morning;
  B menu = same PCR 4 and 7 as A; C control = old chainload, PCR 4 and 7 DIFFER (so the test can
  tell); D no entry + BitLocker volume: nothing chainloaded; E no entry + no BitLocker: chainloaded
  once, request used up; F AUROS-ROOT deleted: S-25 below. 22/23 once, the one miss was two
  containers racing for loop devices on homebase (re-run alone: see Next). Mutant: wrong path
  constant in `af_windows` turns `aurfirsttest` red (2 checks).
- **windows word** (K / lead): request word `windows` -> `answer.sh` -> `aurfirst windows` ->
  restart; Settings > "Put Windows back" page has a row "Start Windows this once" (one press, no
  removal; C's `settings.c`, minimal edit; welcome.c decline wording was already K's). Untimed.
  `aurfirsttest` 80 checks.
- **WP-30 minimal** (S-07, S-18). `grub.cfg.in`: `auros_try` counted by `auros_count` (last line
  of each AurOS entry body), 2 unconfirmed starts in a row -> `auros_bad=<kver>` and the kernel
  before it (`auros_prev` written by bootchain next to `fallback`); `auros-boot-ok.service` ->
  `boot-ok` -> `desktop-up` clears it 2 minutes after aurshell is up. A cut-short start is not a
  verdict. `tools/rollbacktest.sh`: broken kernel (real signed kernel + dead initramfs) panics
  twice, third start is the previous kernel, desktop up, confirmed, bad kernel set aside (run 1);
  next start straight to previous (run 2); a newer good kernel is tried despite the old "bad" (run
  3); a cut-short start does not send it back (run 4). 16/16. Mutants red: `nocount` (counting
  removed) and `noclear` (boot-ok never clears). `bootchaintest` 73/74 here (the one miss is
  `losetup -P` inside docker) and now checks that every counter variable is in `load_env`'s list
  (I once added a variable that was written and never read: counter stuck at 1).
- **S-25** `build/mkimage`: the ESP grub.cfg tries the root, else says so in words and `exit`s to the
  firmware (next BootOrder entry = Windows) instead of `grub>`. Proven in `windowsboottest` F.
  S4's `rootfs/usr/lib/auros/lock/lockctl` writes its own ESP cfg for the encrypted layout: it needs
  the same `else ... exit` (told lead).
- **S-05/S-06 and S-24** (`firstboottest` D and E green; red on the unpatched image, 5 of 9): `auros-hold.service`
  now only publishes state; `auros-keeper.service` -> `keeper` waits for `desktop-up 30` then
  `aurfirst hold`: unconfirmed -> BootNext (only after a desktop worked), confirmed + AurOS no
  longer first -> `promote()` puts it first again (declined: never). `aurfirsttest` S-24 cases.

- **3 menu lock (BLOCKER)**: `set superusers="auros"` + `password_pbkdf2` in the rendered menu; the
  password is 32 random bytes made by `bootchain` and thrown away, only the PBKDF2-SHA512 hash is kept
  (`/var/lib/auros/grub-lock`, 0600); `bootchain relock` (called by `firstboot.sh`) makes this machine's own.
  AurOS, verbose, the previous kernel, Windows (both entries) and Put Windows back are `--unrestricted`;
  aurinit asks; the `init=/bin/bash` recovery entry is gone; `render_menu` refuses a template that lost the
  lock or would ask for a password to start AurOS or has a shell line. `bootchaintest` 84/85 (the miss is
  `losetup -P` inside docker), `tools/bootlocktest.sh` 8/8 on KVM: `e` and `c` ask for a user name, Windows
  does not ask and starts, a hands-off start never asks and reaches the desktop; mutants `nolock` (edit
  opens) and `lockedboot` (ordinary start asks) go red. S1's `hardenboottest` rows for the lock should now be green.
  Note: a kernel line added anywhere (the Windows start line was missed once) needs S1's options; run
  `tools/hardentest.sh` (it checks every kernel line) before pushing a change to `grub.cfg.in` or `bootchain`.

- **5 dead entry after a restore**: `restore_run` (A2's install.c, one block) removes the entries described exactly
  "AurOS" and "AurOS Installer" once the restore's verdict is `restored` (BootOrder is not written: staging has
  no BootOrder writer by design and the firmware skips a number with no entry). `nvramtest` reads the code for
  the call (29 checks; red with the call removed). NOT proven end to end under UEFI NVRAM: nosticktest's
  Secure Boot section installs under OVMF but does not restore there; a restore under real NVRAM is unproven.

- **WP-31 residual**: `bootchain` records, in `var/lib/auros/bootchain.state` and in `status`, `fw_secureboot`,
  `fw_ca2011`, `fw_ca2023` (Microsoft's third-party UEFI CAs in db), `fw_sbat_level` and `fw_sbat_revokes_grub`
  (yes: component have<need), `unreadable` when the variables cannot be read. Measurement only; WP-53 is the
  fix, and the journal/outcome-ping side (WP-73) reads these. `bootchaintest` 88/89, red with the recording
  removed (OVMF 2011-only and 2023-only fixtures, an SBAT level above the grub).

## Next steps, in order

1. Watch the first CI run of `tests` with `windowsboottest` and `rollbacktest` (both gating now) and
   of the changed `firstboottest` (its stand-in waits for the keeper). Pushed as 69ca6b1; the
   keeper part was proven by `firstboottest` D and E only (not yet the full A, B, C with the new
   stand-in: run it on homebase if CI shows red).
2. Prove item 5 under UEFI: extend nosticktest's Secure Boot section with a restore on the same NVRAM and
   assert `efivarstore entry AurOS` is gone (A2's file).
3. WP-30 full: safe graphics (nomodeset) on boot 3, previous debs, boot-4 menu that waits for her;
   hang and crash-loop cases (needs a watchdog: `softlockup_panic`, systemd `RuntimeWatchdogSec`).
4. WP-77 Windows-side keeper task (with B) and the `windows-once` variant (F); WP-31 residual (CA and
   SBAT recorded in the journal); WP-53 fwupd.
5. FINDINGS.md statuses for S-05/06/07/18/24/25 and R-10/17/22 are marked (69ca6b1).

## How to verify (homebase, KVM)

CI images predate a change, so tests run against a CI image with this tree's boot files laid in.
`~/work/auros/bt-tools/{patch,run,kvm,sync}.sh` (not in the repo) do it: `sync.sh bt` copies the
tree, `BTD=bt kvm.sh start <log> patch <test> [args]` runs `docker run --privileged --device
/dev/kvm` (qemu, ovmf, swtpm in image `auros-bt:kvm`) with accel=tcg rewritten to kvm as in
`tests.yml`, `kvm.sh wait <log>` collects. `nopatch` runs the unpatched CI image (proves red).
Two runs at once clash on loop devices: run one at a time. Keep `~/work/auros/bt` under 3 GB.

## Traps

- The image ships ONE fixed `/etc/machine-id` on every machine (forge): not ours, reported.
- Canonical's signed grub has no `date`/`datehook`: freshness can only be judged in Linux.
- grubenv is written by grub (`save_env`, in place) and by Linux (`grubenv` helper, temp + rename):
  every Linux writer must keep the keys it is not about. `load_env` loads ONLY the variables in
  its list: add the new one there (guarded by `bootchaintest`).
- A menu entry body runs in its own context: variables set there vanish after it (so `save_env`
  inside the body, which `auros_count` does, is the only way to keep one).
- grub's own NTFS driver could not read a mkntfs/ntfs-3g test volume under Secure Boot ("unknown
  filesystem"): the BitLocker decision is therefore made in Linux (start-windows) and not
  re-made in grub.
- `docker ps --filter name=btrun-bt` also matches `btrun-bt2`: anchor with `^...$`.
- sed replace that matches nothing is silent: assert in the editing script (it cost a run).
