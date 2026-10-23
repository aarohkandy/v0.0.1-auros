# Stream HW — will her PC work, and the real-PC lab (state, 2026-10-23)

**Goal.** Before anything changes, she knows whether her Wi-Fi, sound,
camera, printer and scanner will work (WP-32, inputs to WP-24), and the owner
has real-PC proof (WP-33, later WP-52). Brief: `docs/PLAN-END-TO-END.md` §4 "HW".

**Owns.** `hw/**`, `src/aurbridge/hwinv*.{c,h}`, `tools/hw/**`,
`docs/results/hardware/**`, this file. Smallest edits elsewhere, all announced
in their commit bodies: `src/aurbridge/main.c`, `wizard.c` (the `--report`
switch and the selftest sum), `build/aurbridge` (the matcher's tables and
include paths), `.github/workflows/windows.yml` (one step + the gate regex),
`tests.yml` (one step).

## Done (on main)

| What | Where | Commit |
|---|---|---|
| `AurOS-Setup.exe --report [DIR]` and `aurbridge.exe report [DIR]`: devices (PCI/USB/HDA ids, class codes), printers + their USB ids, maker/model, CPU, RAM, Windows build, UEFI/Secure Boot/BitLocker/drive type from the installer's own preflight; writes `AurOS-report.txt` + `.json` | `src/aurbridge/hwinv.{c,h}` (pure), `hwinv_win.c` | 7bb2e8b |
| `hw/compat.json` from the image (modules.alias, firmware via the dependency closure, CUPS model lists) + `hw/seeds.json` (hand-written rules + every sentence she reads) | `tools/hw/gen-compat.py`, `gen-header.py` | 94f64d9 |
| Matcher `hw/lib/hwcompat.{c,h}`: inventory → verdicts with her sentences | 36 cases | 94f64d9 |
| `hwinv_scan`, `hwinv_blocks`, `hwinv_journal` (the hooks below) | `hwinv_win.h` | 8959584 |
| Real-PC results table + campaign sheet | `tools/hw/results.py`, `docs/results/hardware/{CAMPAIGN.md,TEMPLATE.txt,README.md}` | (this push) |
| Tests | `sh tools/hw/hwtest.sh`: 28 checks incl. 17 mutants each shown red; run by `tests.yml` (unit job); the Windows build is checked with mingw `-Werror` | |

Proven where: the pure code and the matcher on the host and under mingw on
homebase; the `.exe` starts and writes both files under Wine
(`wine aurbridge-wizard.exe --report Z:/tmp/x`). **On real Windows: only by
windows.yml**, step "the PC report, from the console tool and from the
installer" (look at its job summary for the real-VM report). **Never yet on a
physical PC**: that is WP-33 (`CAMPAIGN.md`).

## Hooks for the other streams (exact)

**B (wizard).** `#include "hwinv_win.h"`; on the worker thread beside `pf_run`:
```c
hw_inv *inv = hwinv_new();
hwinv_scan(inv, &report);        /* report: the pf_report you already have; NULL is allowed */
for (int i = 0; i < inv->n_verdicts; i++)   /* in this order: Wi-Fi, cable, Bluetooth, sound, camera, screen, scanner, fingerprint, then one per printer */
    show(inv->verdicts[i].state /* HW_V_WORKS|PARTIAL|WONT|UNKNOWN */, inv->verdicts[i].says);
if (hwinv_blocks(inv)) refuse_before_anything_changes();   /* the Wi-Fi-only, no-driver case */
hwinv_free(inv);
```
`says` is a finished sentence (<= 320 bytes, lint-clean: `tools/wordlint.sh` runs
on the generated words header). Printer verdicts carry `printer` = index into
`inv->printers` (name in `inv->printers[i].name`) so the screen can say which.
`HW_V_NONE` is never stored: no device, no sentence. The sentences, all from
`hw/seeds.json` "words" (change them THERE, then `python3 -I tools/hw/gen-compat.py`):

- Your Wi-Fi will work. / …may be slower or drop out sometimes. / Your Wi-Fi won't work with AurOS yet. A network cable will, and so will a small USB Wi-Fi stick (about $15). / the same, no cable socket → **blocks=1**: "…so AurOS would have no internet. A small USB Wi-Fi stick (about $15) fixes this: plug one in and start again. Nothing has been changed."
- Your sound will work. · Your camera won't work yet. · Your Bluetooth will work. · Your screen will work, but games and 3D programs will be slow.
- Your printer will work. · …will print, but its scanner won't work yet. · …won't work yet. Connecting it to Wi-Fi, if it can, is the way to make it work. · …only works with Windows… · We can't tell if your printer will work. Most printers made in the last ten years do.
- Your fingerprint reader won't work. You will sign in with a short number instead.

Show the cable-socket sentence only when Wi-Fi is not "works" (the matcher already
leaves it out then). Suggested: tick for works, a plain note for the rest;
nothing is a stop except `blocks`.

**A2 (journal / staging).** Add one string field `hw` (<= 127 bytes; `JOURNAL_STR`
is 128) to `fmt_journal` + `fmt_journal_json` + `src/aurstage/journal.c`, filled
with `hwinv_journal(inv, j->hw, sizeof j->hw)`: `hw1;wifi=wont;eth=none;bt=works;
audio=works;cam=wont;gpu=partial;scan=none;fp=wont;prn=2;prnbad=1;blk=1`. Staging
may refuse on `blk=1` (nothing written yet). Also worth writing next to
`wifi.conf`: the full `hwinv_json()` text as `\EFI\AurOS\windows-hw.json`, for
WP-24's fixers ("driver missing compared with what Windows had").
Not done by HW: both are A2's files.

**E.** `windows.yml` step is in; `tests.yml` runs `tools/hw/hwtest.sh`. To also
check `compat.json` against the real image set `HW_ROOTFS=work/forge/desktop/rootfs`
in that step (prints a summary; exits 1 only with `HW_STRICT=1`). Left off so a
kernel update in the archive cannot stop a release; turn it on after reading
what it prints once.

**P (profiles).** What changes the verdicts (the generator reads the image, so a
rebuild + `gen-compat.py` flips them): `ipp-usb` (USB printers without a model
driver), `sane-airscan`/`sane-utils` (scanners), `printer-driver-foo2zjs`,
`fprintd`, `linux-image-generic-hwe-24.04` (see "Findings").

## Findings the lead should hear

1. **The image ships the 6.8 GA kernel; the HWE kernel is 7.0 today**
   (`linux-image-generic-hwe-24.04`, one 166 MB modules package). Measured with
   the same generator on both (not installed, only read): 7.0 gains PCI ids
   for 12 Wi-Fi chips (Realtek RTL8922AE Wi-Fi 7, RTL8852BTE, MediaTek
   MT7920/7996, newer Intel and Broadcom), **144 USB Wi-Fi ids** (and loses 77
   of old staging drivers), 38 Intel graphics ids (`xe`: Lunar/Panther Lake and
   Arc), 13 sound ids (SOF for the newest Intel/AMD), 5 Bluetooth, and the
   **Intel IPU6/IPU7 camera drivers** (8 ids; the userspace camera stack is
   still missing, so the camera verdict would stay "won't work yet"). It loses
   three PCI Wi-Fi ids (07aa:0044/0047, 8086:272f: worth a look before
   switching). Anything newer than ~2024 is better served. Not my call:
   P/BT/S5 own the kernel; to see the delta again, `gen-compat.py` takes any
   tree that has `lib/modules/KVER`. **Also found:** Ubuntu ships signed,
   prebuilt Nvidia kernel modules per kernel (`linux-modules-nvidia-580-7.0.0-38-generic`
   + `nvidia-driver-580`), the way to give Nvidia machines their real driver
   with Secure Boot on; today they get nouveau ("slow games").
2. USB printers: with no `ipp-usb` in the image a USB printer not in the HP/
   Gutenprint lists is "won't work yet" (over Wi-Fi it is fine). WP-25 already
   lists `ipp-usb`; the verdict flips by itself when it lands.
3. No scanner software (SANE) in the image: every scanner is "won't work yet".
4. Broadcom BCM4352/4360/4331/43142 etc. (14e4) are claimed only by the `bcma`
   door in 6.8 (no `brcmsmac` module built, `b43` needs firmware we do not
   ship): all "won't work yet". The one case that **blocks**.

## Half-done / not done

- `--report` has not run on a physical PC yet; USB-printer pairing and the
  "Windows uses this adapter now" flag are logic-tested only (a real printer
  and a real Wi-Fi card will be the first test: `CAMPAIGN.md`).
- IPP-over-USB capability is not read from Windows (would turn "unknown" into
  "works" once `ipp-usb` ships); needs a physical IPP-USB printer to see how
  Windows shows its interface.
- Per-chip firmware is checked per driver (is the driver's firmware set in the
  image), not per exact chip (iwlwifi: 40 of 67 files present). A chip whose
  specific `.ucode` is missing would be reported "works".
- WP-52 (v0.2): 15–20 machines, PiKVM runner, opt-in field report to `/v1/hw`.
  Nothing started; `results.py` and the JSON schema are the base.
- The staging-side refusal (A2) and the wizard screen (B) are not wired by HW.

## Next steps, in order

1. Read the windows.yml job summary of the newest run on `main` (the real-VM
   report) and fix whatever real Windows shows that Wine did not.
2. When B/A2 have wired the hooks: add a plat_sim-style fixture test for the
   whole path (BCM4352 → screen / staging refusal; AX201 → fine).
3. First physical PC: `CAMPAIGN.md` step 1 only, then diff the report against
   what Device Manager shows; fix classification (`hwinv_kind`) from real names.
4. After the image changes (kernel/firmware/printer packages): rebuild an image
   tree and run `python3 -I tools/hw/gen-compat.py ROOTFS` (on CI the unit
   job has `work/forge/desktop/rootfs`; locally a container with the profile's
   `packages_hardware` installed works: `docker run ubuntu:24.04`, install
   `$packages_hardware kmod python3-minimal`, run it with ROOTFS `/`), commit
   `hw/compat.json`.

## How to verify

`sh tools/hw/hwtest.sh` (host + mingw if installed). Windows part: `sh
build/aurbridge` (homebase has mingw: see FIELD-MANUAL §3), then
`wine out/aurbridge.exe selftest` and `wine out/aurbridge-wizard.exe --report
Z:/tmp/x`.

## Traps

- `hw/compat.json` and `hw/lib/hwcompat_*.h`: edit `hw/seeds.json` and the
  generators, never the JSON; the headers are NOT committed (build/aurbridge and
  hwtest.sh make them into a scratch dir).
- `hwinv_add_dev` keeps every PCI device and every whole USB device on purpose:
  an Intel IPU camera is a "System" device to Windows.
- PowerShell: `"$w: x"` is a drive reference. Write `"${w}: x"` (E fixed my step).
- homebase container `hw-noble` (ubuntu:24.04 + the image's hardware packages,
  ~2 GB) is only needed to regenerate: `docker rm -f hw-noble` frees it.
