# Stream BT — the boot chain, first boot, and every way back

Owns: `src/aurfirst/**`, `src/recovery/**`, `rootfs/usr/lib/auros/{bootchain,grub.cfg.in,
answer.sh,grubenv,start-windows}`, `rootfs/etc/systemd/system/auros-{answer,hold,boot-ok}.*`,
and `tools/{firstboottest,choicesboottest,bootchaintest,bootupdatetest,putbacktest,
windowsboottest,bootmenutest}.sh`. S1 edits only the kernel command line in `grub.cfg.in`;
R needs one sandbox change in `auros-answer.service`; C touches the menu only through us.

## Goal

The atlas findings that strand her or lose her Windows, each fixed with a test that fails
before and passes after:

1. HIGH  stale `next_entry` could run the restore weeks later
2. HIGH  "Windows" from AurOS's menu chainloaded bootmgfw (BitLocker recovery screen)
3. BLOCKER  the menu could be edited (`init=/bin/bash`), and had a root-shell entry
4. MEDIUM  a panicking kernel boot-looped (no boot counting)
5. MEDIUM  a dead AurOS entry stayed first in BootOrder after a restore
6. then `docs/safety/FINDINGS.md`'s boot / first-boot / restore items

## Done

- **1 stale restore** — `answer.sh` writes `auros_asked=<hardware clock, UTC s>` beside
  `next_entry`; `grub.cfg.in` honours `next_entry` only with it, clears both, and passes
  `aurstage.asked=` to the restore; `src/aurstage/main.c` `restore_must_be_fresh()` (A2's
  file, minimal edit) refuses a request > 1 h old or > 10 min in the future: says so, changes
  nothing, restarts. Hand-chosen restore (empty `aurstage.asked=`) is not checked.
  Proof: `putbacktest` boots 4-6 (press, then a start three weeks later via `-rtc base=`):
  unpatched image 3/24 FAIL (the restore ran), patched 24/24; `aurfirsttest` 68/68.

## How to verify locally (homebase, KVM)

The CI image predates a change, so tests run against a CI image with this tree's files laid
in (`patch.sh` in the stream's scratch tools: copies `rootfs/usr/lib/auros/*` and units,
compiles `aurfirst`, re-renders `grub.cfg` with `bootchain menu`, rebuilds the staging
initrd's static `init`). Container: `docker run --privileged --device /dev/kvm` with qemu,
ovmf, swtpm (FIELD-MANUAL §3); accel=tcg is rewritten to kvm as in `tests.yml`.

## Traps

- The image ships ONE fixed `/etc/machine-id` (e428a7e2…) on every machine — not ours
  (forge), reported to the lead; per-machine secrets are generated at first boot instead.
- grub has no clock (`date`/`datehook` are not in Canonical's signed grub; the module list is
  in this file's history): freshness can only be judged in Linux.
- grubenv written by grub (`save_env`) and by Linux (`grubenv` helper, temp + rename) —
  every Linux writer must keep the keys it is not about.
