# Stream C — the AurOS desktop, first run and software (session B)

Owner's bar: "it should feel complete, like Apple ... my great-grandmother
should start using it without ever getting confused". Binding spec:
`docs/DESIGN-DIRECTION.md` §5 "C" (P0 first). Files: `src/aurshell/**`
(except S2 `guard*.c`, S4 `lock*.c`, S3's apps.c parsing), `src/aurscreen`,
`build/forge`, `profiles/desktop.profile`, `themes/`, `rootfs/**` not claimed
by S1/S2/S4/S5/K/BT. **BT now owns** `src/aurfirst/**`, `src/recovery/**` and
the boot files in `rootfs/usr/lib/auros` (bootchain, grub.cfg.in, answer.sh,
hold, putback).

## Done (newest last)

| Commit | What | Proof |
|---|---|---|
| `890dd0e` | Every word on the desktop was Inter **Black** (forge's font resolver globbed `Inter*`). Named weight = exact file; nocturne: InterDisplay-SemiBold / Inter-SemiBold / Inter-Regular; forge resolves every theme's faces | `tools/fonttest.sh` (fails with old glob or old theme) |
| `482dab2` | Her look reset on every restart (theme read before her copy was chosen) | `tools/lookboottest.sh` (fails reverted); in tests.yml |
| `77adbfe` | No shared password: account locked, not in sudo, root locked, getty tty1-6 masked + logind NAutoVTs=0 (`allow_tty="no"` on desktop) | `tools/permissions.sh` new section: fails on build 39901973428's rootfs, passes after forge stage 4 on it; in tests.yml |
| `9568d58` | gdebi pkexec grant (passwordless root) removed, gdebi gone; .deb/.exe/.msi open `rootfs/usr/lib/auros/program-file` (a card + the store) | permissions.sh; filetypes.sh |
| `e9d1ba0` | udisks filesystem-mount-system grant removed; udev rule hides internal NTFS/BitLocker from the file manager | permissions.sh (whole script exit 0 on the restaged rootfs) |

Lead item 5 (Help never opens the help pages) was already fixed by K in
`f3c42b0` (after the ATLAS base commit); tested by `tools/targets.c`.

## For other streams (exact asks)

- **BT**: `rootfs/usr/lib/auros/grub.cfg.in:91` still offers `single
  init=/bin/bash` (ATLAS image gap 1). With her account locked and no console
  login, that menu entry is the remaining keyboard-to-root path. Nothing of
  mine is in flight in aurfirst/answer.
- **A2** (`build/staging` is yours): the staging screen copies only DejaVu.
  After the `for f in DejaVuSans.ttf ...` loop (~line 360) add:
  ```sh
  mkdir -p usr/share/auros/fonts
  cp -L "$RFS/usr/share/auros/fonts/Inter-Regular.ttf"  usr/share/auros/fonts/ui.ttf
  cp -L "$RFS/usr/share/auros/fonts/Inter-SemiBold.ttf" usr/share/auros/fonts/ui-bold.ttf
  ```
  (`-L`: they are links to Ubuntu's .otf). When the UI face changes to
  Atkinson Hyperlegible Next (DESIGN-DIRECTION P1-10) only the two source
  names change. `src/aurscreen/screen.c` already tries `ui.ttf` first; C adds
  `ui-bold.ttf` to its bold list.
- **P**: `profiles/office.profile:72` says `allow_tty="yes"`, which keeps
  console login prompts that no password can answer now. Set `"no"`.

## How to work (environment)

- Worktree `$S/wt-c` (S = the session scratchpad). `git fetch origin main`
  does NOT update `origin/main` here (the shared config's refspec is the old
  branch): use `git fetch origin +refs/heads/main:refs/remotes/origin/main`.
- homebase dev container `auros-c:dev` (Ubuntu 24.04 + wayland/dbus dev +
  fonts-inter/paratype). Repo synced to `~/work/auros/c`, outputs in
  `~/work/auros/c-out`, `/usr/share/auros/fonts` emulated by mounting
  `~/work/auros/c-fonts` (links made by forge's own resolver).
- Image rootfs of build 39901973428: `~/work/auros/c-img/rootfs.tar.zst`;
  `forge stage desktop 4` can be re-run on it in a privileged ubuntu container
  to prove forge changes without a full build.
- Renders: `aurshell --conf X --shell shells/<look>.shell --png out --size W H`
  and `tools/journey.c` (first ten minutes, every screen; in progress).

## Next, in order

1. `tools/journey.c` + present() for tiles/taskbar (a program opened by the
   store or the "ready" card stayed hidden in tiles; the taskbar's program
   list stayed open over it) — stashed in wt-c, commit with a journeytest.
2. DESIGN-DIRECTION §5 C P0 1-8: open_font name lookup; Light default
   (sandstone tokens, profile, Settings Colors with two choices, delete
   synthwave, register mark and teal hardcodes); staging screen bold list;
   names (Wi-Fi, Internet, All my programs, look names); first run
   (welcome.c); notifications that stay; one press is one press; unique icons
   + the drawn mark.
3. Render every look at 1366x768 and 1024x600 after each change; replace
   `docs/shots/shells/*.png`.
4. Performance on a 4 GB 2013-class VM (boot time, idle RAM, frame time):
   not measured yet.
