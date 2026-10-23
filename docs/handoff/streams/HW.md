# Stream HW — will her PC work, and the real-PC lab (state, 2026-10-23)

**Goal.** Before anything changes, she knows whether her Wi-Fi, sound,
camera, printer and scanner will work (WP-32, WP-24 inputs), and the owner
has real-PC proof (WP-33, WP-52). Brief: `docs/PLAN-END-TO-END.md` §4 "HW".

**Owns.** `hw/**`, new `src/aurbridge/hwinv.{c,h}`, `tools/hw/**`,
`docs/results/hardware/**`, this file. Smallest possible edits elsewhere
(`src/aurbridge/main.c` for `--report`, `.github/workflows/windows.yml` for
one step), said so in the commit body.

## Milestones (in order)

1. Windows-side inventory + `AurOS-Setup.exe --report` (plain text + JSON). **TODO**
2. `hw/compat.json` generated from the image + seeds, matcher
   `hw/lib/hwcompat.{c,h}` + unit tests. **TODO**
3. Plain sentences for B (screen) and A2 (staging refusal). **TODO**
4. `docs/results/hardware/CAMPAIGN.md` for the owner. **TODO**

## Facts found (so nobody re-reads)

- Image firmware/driver set is `profiles/desktop.profile` `packages_hardware`:
  linux-firmware-{amd-graphics,amd-misc,intel-graphics,intel-misc,
  intel-wireless,nvidia-graphics,qualcomm-*,broadcom-wireless,
  marvell-wireless,mediatek,realtek,misc}, firmware-sof-signed, bluez,
  cups + printer-driver-gutenprint + printer-driver-hpcups + cups-filters.
  Kernel: `linux-image-generic` on noble = 6.8.0-147 (as of today).
  No `ipp-usb`, no `sane`, no `broadcom-sta-dkms` (wl) in the image today.
- Where the build reads this: `build/forge` (no firmware list written out
  anywhere). hw/ generator therefore reads the image rootfs (modules.alias
  via `depmod`, `/usr/lib/firmware`, `/usr/share/cups/drv`, `ppd`) rather
  than parsing the profile.
- `--report` entry points: `src/aurbridge/main.c` (console `aurbridge.exe`,
  `cmd` dispatch) and `src/aurbridge/wizard.c` WinMain (`AurOS-Setup.exe`
  is the wizard build; flags `--shot --navtest --selftest --resume`).
  `--report` must work in the **wizard** exe (the file she downloads), which
  is `-mwindows`, so it must write to a file / attach to the parent console.
- preflight.c already has: `check_wifi` (WLAN API), `system_disk_rst`
  (SetupAPI + CM_*), BitLocker (`volume_is_bitlocker`, `-FVE-FS-`),
  Secure Boot (`plat_secure_boot`, `sbdb_judge`). Reuse via `pf_report`.
- build/aurbridge builds every `src/aurbridge/*.c` except `*_win.c`
  (Windows only), `*_sim.c`, `simmain.c`, `main.c`, `wizard.c`. A new
  `hwinv.c` therefore lands in the shared LIB automatically; Windows-only
  API code must be in `hwinv_win.c` or guarded by `_WIN32`, because
  `aurbridge-sim` builds only format/phases/inflate/sbdb + stage-shared.

## Hooks promised to other streams (exact, filled in as they land)

- B: `hwinv_run(hw_inv *)`, `hwinv_to_json`, `hwinv_to_text`, verdict
  sentences from `hwcompat_*` — TODO.
- A2: journal field name + format — TODO.

## Traps

- homebase exploration container `hw-noble` (ubuntu:24.04, image hardware
  packages installed) may still be running: `docker rm -f hw-noble` when done.
  Work dir `~/work/auros/hw/` on homebase.
