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
| `8b0e4ab`..`8be02cb` | Atkinson Hyperlegible (+ DejaVu fallback per glyph), the 32 px pointer, big-text fixes, the footer's icon never touches its label | fonttest, fallbacktest, cursortest, targets |
| `c7e0b6d` `f0c4b7a` | **One set of window buttons**: a program that draws its own title bar (Firefox, GTK) gets none of ours in any look; its bar's drag / minimise / maximise / restore are done by the look; the buttons it SHOWS are its look's (textsys_buttons); the X on our own windows asks the program (it used to drop the slot and the program stayed); the dock honours a program's "fill the space". Shots: `docs/shots/csd/` | `tools/csdtest.c` 44 (13 mutants), `tools/wltest-deco.sh` 27 (7 mutants) |
| `3fedc30` `1636550` | **Full screen lasts** (a film's own button was one frame long); the band stays so "Close this" is there. (1636550: 3fedc30 shipped a call to a function I had not defined; aurshell did not link for a few minutes) | wltest-deco runs the real `session_sync` |
| `6b6c438` | **No capability for the session** (`CapabilityBoundingSet=~CAP_WAKE_ALARM`, `AmbientCapabilities=`): bubblewrap refused every Flathub program | `tools/permissions.sh` (red x2), `hardenboottest` reads CapInh/Prm/Eff/Amb |
| `a430c5f` | **Everyday basics**: printer.local resolves (libnss-mdns + the hosts line), LibreOffice Writer + Calc (+gtk3, Carlito, Caladea; 75 packages, 344 MB installed, 103 MB download), .docx/.xlsx/.odt/.ods/.csv in Writer and Calc, films in mpv, .heic in Photos, presentations open an honest card, never the archive manager; `xdg-open` hands to `gio open` (dpkg-divert) | `tools/everydaytest.sh` 48 (6 mutants), `filetypes.sh` |
| `2ff8442` | **Each computer its own machine id** (empty `/etc/machine-id`, systemd-firstboot masked, first-boot preset frozen to what the build enabled) | `hardentest.sh` "Machine identity" + 5 mutants; **boot check is BT's** |
| `5a6cfea` | Flathub programs use Wayland (global flatpak override) | everydaytest |
| `fa11087` | "Bigger" also sets GTK's `cursor-size` | textsystest 27 |
| `03ce09a` | **Ferry in the shell**: Whose files?, No files to bring, the USB stick (plug in / which / saving / done), and why Put Windows back could not save her files (`why=`) with "Save my files to a USB stick first". `stick:/dev/NAME` word in `src/aurfirst/request.c` + `answer.sh`. Shots: `docs/shots/ferry/` | welcometest 67 (9 mutants), windowspagetest 16, aurfirsttest +22 (4 mutants), journeytest 308 |

Lead item "Help button never opens the pages" was already fixed by K (`f3c42b0`).
C6 (notifications that stay) by a helper: `1c430eb`, `cb52b2c`, `tools/notifytest.c` 81.

## Not done / decisions needed

- **Saving her files to a USB stick does not unlock Put Windows back.** Lead decision
  (16:00): no "lose my files anyway". The screens say so ("Windows is still not put back.
  AurOS checks again ... whenever you press it in Settings"). If the lead wants a stick save
  to count, the hook is `answer.sh` `putback`: a marker written after a successful `stick:`
  and `find -cnewer` over her home (dot-directories pruned). Not built: it changes what is
  deleted, so it is A2's/BT's call.
- **HWE kernel (HW decision): NOT shipped.** Needs, in order: (1) `build/staging:62`
  picks `ls $RFS/lib/modules | head -1` (alphabetical: 6.14 before 6.8 by luck) and
  `build/mkimage:146` takes `sort -V | tail -1` -- make both explicit (A2's file);
  (2) BT's `bootchain` must write the GA kernel as the automatic fallback entry
  (`grub.cfg.in` has one `@KVER@`); (3) profile: add `linux-generic-hwe-24.04`
  beside `linux-image-generic`; (4) the whole KVM suite on that build, with image size
  and boot time before/after. Not flipped on main: every stream's CI and the e2e run on
  the newest build, and I cannot verify (4) locally.
- **First start changes nothing but the id** is held by `etc/systemd/system-preset/00-auros.preset`
  (what the build enabled, then `disable *`) and a masked `systemd-firstboot`. I could not boot to
  see Ubuntu's preset policy run; if a first start shows an extra unit, that file is where to look.
- **Firefox following "Bigger" is not proven** (gsettings + GTK settings are written; no booted
  Firefox read them back). Same for the buttons a program shows (`gtk-decoration-layout`): proven
  with real Firefox/GTK 4 through the harness (settings.ini), not through gsettings.
- **S1/PARITY: printers.** ipp-usb registers its printers with avahi-daemon over DNS-SD;
  `build/harden`'s `disable-publishing=yes` refuses that. If a plugged-in IPP-USB printer is
  not in Printers on a booted image, that is why (publish for loopback only).
- **Wishes hooks (W2), low priority, not started**: Help-panel wish buttons, forge `voice.c` /
  `-DAUROS_WISHES=1` only under `wishes="on"`; shipped profiles stay `wishes="off"`.
- **Idle inhibit over D-Bus** (`org.freedesktop.ScreenSaver`): WL did the Wayland protocol; the
  shell owns the bus and does not answer it yet (a film in a program that only uses D-Bus still
  lets the screen go dark after 10 minutes).
- **P**: `profiles/office.profile` says `allow_tty="yes"`: console prompts nobody can answer.
- **BT**: `grub.cfg.in:91` still has `single init=/bin/bash` (keyboard-to-root now that
  console login is off).
- Staging screens (DESIGN-DIRECTION §5 C item 3) are DONE on main by `c801ca3` (rewrote the same
  `src/aurscreen/screen.c`: Light tokens, the drawn mark, ui.ttf, no log lines, helper code only
  on failure, step/minutes). The staging helper's branch `wt-c-stage` (9c08822 + 0b67a82, never
  pushed) conflicts in 21 places with that rewrite and is superseded: delete it.
- "Like Windows": the footer is a second bar under the taskbar, not folded into it (ten controls
  and the window buttons do not fit one 56 px bar at 1024). Window drag slop (8 px) not done.
- F-03 (the X in three looks): the footer's "Close this" is the answer, and the X on Like Windows
  and Like a Mac windows now really asks the program. Not decided: whether Big and simple / One
  thing at a time should grow an X.
- G-01 the store itself (GNOME Software) is not ours: text size follows Bigger through GTK.
- Workbench draws a title bar over a program's own (not one of the four looks; `csd` unused there).

## Performance on a 4 GB 2013 laptop (measured in a VM on homebase EPYC, so ratios, not seconds)

Shell frame (`aurshell --png --frames`): 1.3 to 3.8 ms a paint at 1366x768 and 1024x600 in all four
looks; start 50 to 86 ms to first frame; resident about 36 MB. Boot time and idle RAM of the whole
image are S1's `hardenboottest` medians (see docs/security/HARDENING.md); nothing in the shell
was found worth cutting. The new office suite is 344 MB installed and does not run until opened.

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

1. Read the first real `[test]` run: the new unit steps (everydaytest on the built rootfs, welcometest,
   windowspagetest, targets, csdtest, permissions, hardentest "Machine identity") were proven in a
   container and against fake roots, not against a built image.
2. The two boot checks nobody can run in a container: two VMs, different machine ids (BT);
   `flatpak run` of solitaire and VLC on the capability fix (PARITY did it on 40003821764).
3. The remaining items above (HWE, W2 hooks, D-Bus idle inhibit, F-03).
