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
| Matcher `hw/lib/hwcompat.{c,h}`: inventory → verdicts with her sentences | cases for BCM4352 (blocks with no cable socket, warns with one), AX201/BE200/MT7922/QCA6174 fine, RTL8852BE/8723BE unsteady, RTL8922AE not yet, IPU6 camera not yet, Nvidia-only slow, USB sticks, printers | 94f64d9, 283c860 |
| `hwinv_scan`, `hwinv_blocks`, `hwinv_journal` (the hooks below) | `hwinv_win.h` | 8959584 |
| Real-PC results table + campaign sheet | `tools/hw/results.py`, `docs/results/hardware/{CAMPAIGN.md,TEMPLATE.txt,README.md}` | 9c30771 |
| Tests | `sh tools/hw/hwtest.sh` (about 50 s): ~34 checks, ~20 of them mutants shown red, a sanitized fuzz of random PCs, wordlint on every sentence, the sample report and results table kept in sync; run by `tests.yml` (unit job; that workflow runs on `[test]` or a release); mingw `-Werror` build of the Windows part | a4d17a4 |
| `tools/hw/regen.sh` remakes `hw/compat.json` (byte-identical on re-run) | | fd92118 |

Proven where: the pure code and the matcher on the host (ASan/UBSan clean) and
under mingw `-Werror` on homebase; the `.exe` starts and writes both files under
Wine. **On real Windows: windows.yml run 39959512211 (workflow_dispatch, all jobs
green)**: `aurbridge.exe report` and `AurOS-Setup.exe --report` both wrote the two
files on a Windows Server 2025 VM (UEFI, Secure Boot off, 1 PCI device kept, 60
skipped as plumbing, the Mellanox ConnectX-4 Lx VF read as "Network cable", driver
`mlx5_core`, "Your network cable will work."), the JSON parsed in PowerShell, the
installer's own check was in the report, `aurbridge selftest` ran all 40-odd
inventory and matcher cases. The report is in that run's job summary. (Push-triggered
windows runs were mostly cancelled or stalled in `apt` on the runners; a
`gh workflow run windows.yml --ref main` is the reliable way to get one.)
**Never yet on a physical PC**: that is WP-33 (`CAMPAIGN.md`). A VM has no Wi-Fi, no
printer, no camera: those paths (the "Windows uses this adapter now" flag, USB
printer pairing, class codes of real USB devices) are logic-tested only.

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

- Your Wi-Fi will work. / …may be slower or drop out sometimes. / Your Wi-Fi won't work with AurOS yet. A network cable will. So will a USB Wi-Fi stick with a MediaTek MT7612U, MT7921 or MT7601U chip: the list of sticks is on our website. / the same, no cable socket → **blocks=1**: "…so AurOS would have no internet. A USB Wi-Fi stick with a MediaTek MT7612U, MT7921 or MT7601U chip fixes this: the list is on our website. Plug one in and start again. Nothing has been changed."
- Your sound will work. · Your camera won't work yet. · Your Bluetooth will work. · Your screen will work, but games and 3D programs will be slow.
- Your printer will work. · …will print, but its scanner won't work yet. · …won't work yet. Connecting it to Wi-Fi, if it can, is the way to make it work. · …only works with Windows… · We can't tell if your printer will work. Most printers made in the last ten years do.
- Your fingerprint reader won't work. You will sign in with a short number instead.

The no-Wi-Fi verdicts also have `link` (the page naming the sticks); show it as the way to "see the list".
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

## The USB Wi-Fi stick promise (fixed 2026-10-23)

The no-Wi-Fi sentence used to say "a small USB Wi-Fi stick (about $15) fixes this", true only
for sticks whose chip the kernel drives. Now: the sentence names the chips (MediaTek MT7612U,
MT7921, MT7601U) and every `wifi.wont` / `wifi.wont_alone` verdict carries
`link = "advanced.html#wifi-sticks"` (a page on the site, from `hwinv_win.h`'s verdict struct:
B turns it into a button/address with the site host it already bakes in).
- The list: `hw/sticks.json` (4 chips, 12 example products from public Linux-friendly-stick lists,
  each chip's USB ids), rendered into `website/advanced.html` between `sticks:begin/end` by
  `python3 -I tools/hw/sticks.py --write` (never edit the block by hand: the check compares).
- The check (`tools/hw/sticks.py`, run by `hwtest.sh`, so by the unit job): every listed id is
  claimed by the named Wi-Fi driver with its firmware in the image, in the image's kernel
  (`hw/compat.json`) AND the HWE kernel (`hw/compat-hwe-wifi.json`, 7.0.0-38, made by
  `gen-compat.py --wifi-only` on a tree that has it); 2 to 4 products per chip; no Realtek
  RTL8811AU/8812AU/8821AU/8814AU anywhere except under "Do not buy"; and every place a person
  can read (website, help pages, installer and staging strings, the sentences in seeds.json,
  `docs/results/hardware/*.md`) that mentions a USB Wi-Fi stick/adapter/dongle names a listed
  chip or links `#wifi-sticks`. Eight mutants are shown red.
- A stick Windows has no program for is an "unknown device" to Windows; the matcher now still
  sees its chip (`wifi-stick-unknown-to-windows`), so "plug one in and start again" works even
  when Windows cannot use the stick.
- The check found two more places that said "a small USB Wi-Fi stick (about $15)" and they are fixed: stream B's new section `#wifi-stick` in `website/advanced.html` ("What fixes it", with its own chip list that included Realtek RTL8188EUS/8821CU...; now it names the three MediaTek chips and links `#wifi-sticks`, which is the one list) and `fix_win.c`'s `no-internet` line ("Plug in a USB Wi-Fi stick (MT7612U, MT7921 or MT7601U chip).", 86 characters: navtest limits a fix to 90). B's pictures `2h-blocked-no-internet` show the old words until B re-shoots them.
- Verified from: morrownr's "Plug and Play" list (MT7612U, MT7610U, MT7921AU products and the
  MT7601U ones from hardware databases), NETGEAR A6210 (0846:9053, mt76x2u). NOT verified on
  hardware: that these products still carry these chips in their current revisions, and that the
  sticks work in AurOS: `CAMPAIGN.md` buys three and tries them.
- With the HWE kernel the matcher will call RTL8812AU/8821AU sticks "works" (drivers exist in
  7.0); the list still does not recommend them until AurOS itself connects with one (CAMPAIGN stick C).
- D owns the website: `tools/site/check.mjs` was NOT run on the new section (no browser here);
  `pages.yml` will. If it objects to markup, fix `render()` in `tools/hw/sticks.py`.

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

1. (done once: run 39959512211, see above.) After any change to `src/aurbridge/hwinv*`,
   `hw/`, `build/aurbridge`: `gh workflow run windows.yml --ref main`, read the job.
2. When B/A2 have wired the hooks: add a plat_sim-style fixture test for the
   whole path (BCM4352 → screen / staging refusal; AX201 → fine).
3. First physical PC: `CAMPAIGN.md` step 1 only, then diff the report against
   what Device Manager shows; fix classification (`hwinv_kind`) from real names.
4. After the image changes (kernel/firmware/printer packages): `sh tools/hw/regen.sh`
   on homebase (fresh container, 2 GB for a few minutes, refuses under 33 GB free;
   `--reuse NAME` for a container that already has the profile's `packages_hardware`),
   or on CI `python3 -I tools/hw/gen-compat.py work/forge/desktop/rootfs`. Output is
   deterministic (a re-run is byte-identical), so a diff of `hw/compat.json` is a diff
   of what the image can drive. Commit it.

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
- No homebase container is left behind by HW (`regen.sh` makes and removes its own).
