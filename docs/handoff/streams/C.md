# Stream C — the AurOS desktop, first run and software (session B)

Owner's bar: "it should feel complete, like Apple ... my great-grandmother
should start using it without ever getting confused". Binding spec:
`docs/DESIGN-DIRECTION.md` §5 "C" (P0 first). Files: `src/aurshell/**`
(except S2 `guard*.c`, S4 `lock*.c`, S3's apps.c parsing), `build/forge`,
`profiles/desktop.profile`, `themes/`, `rootfs/**` not claimed by others.
BT owns `src/aurfirst/**`, `src/recovery/**` and the boot files in
`rootfs/usr/lib/auros`. Notifications (`notify.c`, `osd.c`) were done by a
helper of C (C6 below), staging screens (`src/aurscreen`) by another.

## Done (newest last; all on main, every one with a test)

| Commit | What | Proof |
|---|---|---|
| `890dd0e` | Every word was Inter **Black** (forge globbed `Inter*`); named weights now exact files | `tools/fonttest.sh` |
| `482dab2` | Her look reset on every restart | `tools/lookboottest.sh` |
| `77adbfe` `9568d58` `e9d1ba0` | No shared password / sudo / console login; gdebi pkexec grant gone (a `.deb` opens "Get more programs" with a card); Windows disk not mountable by her programs | `tools/permissions.sh` (section "what it must NOT do", exit 0 on the re-staged image rootfs) |
| `b1366ba` | A program opened from the store or the ready card comes into view (tiles, taskbar `present()`) | `tools/journeytest.sh` |
| `7b800d3` | The shell opens a theme's font by its name | `fonttest.sh` |
| `75e3321` | Light is the default (sandstone = "Light", nocturne = dark), synthwave deleted, Settings "Colors" has two | contrast `--strict`, looktest |
| `429fd33` | One name per thing: Wi-Fi, Internet, All my programs, the four looks | journeytest |
| `5ae7a0d` `5aeb3c7` `a3d5efa` | First run: one check, answers with their sentences beside them in 18 px, Yes/No ask twice, chip "Back to the question", offline opens Wi-Fi | welcometest 34, journeytest holds 18 px |
| `e8d7185` | One press is one press (700/500 ms) | `tools/presstest.c` |
| `08d4a45` | `draw_poly`, the drawn mark, every icon its own, wordmark | `tools/icontest.c` 1621 |
| `53bcd8a` | `features.h` -> `aurfeatures.h` (it shadowed glibc's, broke every harness) | all harnesses build |
| `349f96a` | "Bigger" reaches GTK/Firefox (gsettings + gtk-xft-dpi) | `tools/textsystest.c` — **Firefox following it is not proven** |
| `0d2d9c6` `a7a1bb2` | The spec's type scale (20/18/18), the footer, taskbar, dock, rail, tiles | journeytest 148, targets 20644, storetest, icontest, modaltest |
| `0d2a2cc` | `ipp-usb` in the desktop profile | not provable in a VM (see S1 ask) |

Lead item "Help button never opens the pages" was already fixed by K (`f3c42b0`).
C6 (notifications that stay) by a helper: `1c430eb`, `cb52b2c`, `tools/notifytest.c` 81.

## Not done / decisions needed

- **HWE kernel (HW decision): NOT shipped.** Needs, in order: (1) `build/staging:62`
  picks `ls $RFS/lib/modules | head -1` (alphabetical: 6.14 before 6.8 by luck) and
  `build/mkimage:146` takes `sort -V | tail -1` — make both explicit (A2's file);
  (2) BT's `bootchain` must write the GA kernel as the automatic fallback entry
  (`grub.cfg.in` has one `@KVER@`); (3) profile: add `linux-generic-hwe-24.04`
  beside `linux-image-generic`; (4) the whole KVM suite (installtest, nosticktest,
  firstboottest, choicesboottest, putbacktest, bootchain/bootupdate) on that build,
  with image size and boot time before/after. Not flipped on main because every
  stream's CI and the e2e run on the newest build, and I cannot verify (4) locally.
- **S1/PARITY: printers.** ipp-usb registers its printers with avahi-daemon over
  DNS-SD; `build/harden`'s `disable-publishing=yes` refuses that. If a plugged-in
  IPP-USB printer is not in Printers on a booted image, that is why (publish for
  loopback only). PARITY's print check must cover USB.
- **A2** (`build/staging`): copy the UI face for the staging screens:
  `mkdir -p usr/share/auros/fonts; cp -L "$RFS/usr/share/auros/fonts/Inter-Regular.ttf" .../ui.ttf; cp -L .../Inter-SemiBold.ttf .../ui-bold.ttf`.
- **P**: `profiles/office.profile` says `allow_tty="yes"`: console prompts nobody can answer.
- **BT**: `grub.cfg.in:91` still has `single init=/bin/bash` (keyboard-to-root now that
  console login is off).
- Staging helper's commits (`wt-c-stage`, 9c08822 + 0b67a82) were NOT pushed (its push
  was denied by the permission system); they need the lead.
- The UI face is Atkinson Hyperlegible 1.0 from noble's `fonts-atkinson-hyperlegible`
  (Regular and Bold; headings in Bold), not "Next" (no 800; needs a file the archive does
  not have). Its zero is slashed (visible in the clock): the plain-zero script is P2.
  The per-glyph DejaVu fallback (`font.c`) is done; `cursor_size` 32 is not.
- "Like Windows": the footer is a second bar under the taskbar, not folded into it
  (ten controls and the window buttons do not fit one 56 px bar at 1024).
- Window drag slop (8 px) not done (`taskbar.c`/`dock.c` drag code).
- F-03 (the X in three looks): "Close this" in the footer is the answer; not decided.
- G-01 the store itself (GNOME Software) is not ours: text size now follows Bigger via GTK.
- Performance on a 4 GB 2013 laptop (boot time, idle RAM, frame time): not measured.
- `docs/shots/shells/*.png` are the old set; the new renders are in the scratchpad
  (`c-shots/jq`) and must replace them (task 16).

## How to work (environment)

- Worktree `$S/wt-c`. `git fetch origin main` does NOT update `origin/main` (shared
  config's refspec is the old branch): `git fetch origin +refs/heads/main:refs/remotes/origin/main`.
  `git rebase --autostash` (never `git stash`: the stash is shared between worktrees).
- homebase dev image `auros-c:dev`; repo synced to `~/work/auros/c`, outputs `~/work/auros/c-out`.
  `sh /t/mk.sh` builds aurshell + resolves the Light conf; `tools/journeytest.sh <dir>`
  renders every screen of the first ten minutes for four looks x two sizes;
  `tools/icontest.c <png>` writes the icon sheet. Keep `~/work/auros/c*` under 3 GB.
- Pitfall: a header named like a system one (`features.h`, `time.h`) in `src/aurshell`
  breaks every harness built with `-I src/aurshell`.

## Next, in order

1. Replace `docs/shots/shells/*.png` with the new Light renders (taskbar/dock/rail/tiles at
   1366x768 and 1024x600; welcome, Settings, a card, the store) — and tell D.
2. Measure and record: boot time, idle RAM, shell frame time (`aurshell --png --frames`),
   in a VM; cut.
3. Trace the everyday tasks (volume, brightness, printing, camera/mic, updates) through
   the code and fix or file each gap.
4. The remaining §5 C items listed above.
