# Stream WL2: the compositor review, the privacy/idle/text/test half (session B)

For an engineer with zero context. Environment: `docs/handoff/FIELD-MANUAL.md`. The review is
`docs/reviews/WL-2026-10-24.md` (W01-W32); stream WL (`WL.md`) owns the window/input/focus findings, this
stream owns the ones below. Both edit `src/aurwl/xwm.inc`, `aurwl.c`, `session.c`, `main.c`: rebase often, keep
edits there tiny and name them in the commit body.

## Goal

Her clipboard is never readable by a program she is not using; the screen does not go dark in a film or a call;
"Bigger text" reaches X programs; and the compositor's tests are release gates that can actually fail.

## Done (all on `main`)

| finding | what | where it is proved |
|---|---|---|
| W01 W09 W19 (privacy) | `xclip.inc`: CLIPBOARD is held on the X side for a Wayland copy only while an X window has the keyboard (taken on focus, released on leaving: `xc_focus_changed`, called from `xw_focus`); a request is served only if the requestor is a window of the focused X client (same resource-id base); a source offering `x-kde-passwordManagerHint` is never published | `wltest-x`: clip-wl-focused-refused, clip-bg-x-refused, clip-password-hint (+ the old scenarios, which now copy in Wayland then move focus to an X window). 14 mutants in `tools/wltest-sub-mutants.py` |
| W05 | a new X owner is accepted only from the focused X client; any other owner change is ignored, drops a stale X offer, and re-claims the Wayland copy | clip-hijack |
| W28 | an INCR stream past the 16 MiB cap is refused at the cap (before the timeout); the fuzz harness has a small cap (96 KiB), big replies and big INCR chunks, and aborts if any transfer holds more than the cap | clip-incr-cap + mutant (`p->n + bytes > XC_MAX`). The fuzz harness (`fuzz_xwm.c`) now has a 96 KiB cap, replies/INCR chunks up to 32 KiB, a step that gives an X program the keyboard and one that makes it copy, and aborts if a transfer ever holds more than the cap. **Two libFuzzer runs of 4-5 minutes (135k and 158k runs) with the cap check removed did not find it**, so the cap is proven by the wltest-x scenario, not by the fuzzer |
| W06 | `wishhook.c` `sensitive_now()` is true for a focused X window (`aurwl_win_is_x11`) | `wishtest-hook` + mutant; `wltest-x` checks `aurwl_win_is_x11` |
| W20 | the shell serves `org.freedesktop.ScreenSaver` (`notify.c`, paths `/org/freedesktop/ScreenSaver` and `/ScreenSaver`): Inhibit / UnInhibit / SimulateUserActivity / GetActive; a holder that leaves the bus releases everything; `main.c` ORs `notify_idle_inhibited()` into the idle timer. VLC and Zoom's Flathub manifests both `--talk-name=org.freedesktop.ScreenSaver` | `tools/notifytest.c` (real libdbus client, real dbus-daemon) + `tools/notifytest-mutants.py` (8 mutants) |
| W32 | `aurwl_set_text_scale()` (called from `main.c` beside `textsys_apply`); `xwm.inc` writes `Xft.dpi` into `RESOURCE_MANAGER` on the X root at WM start and on change | `wltest-x text-scale` + 3 mutants; **real Xwayland + real aurshell + real xterm**: `TEXT_SCALE=2.0 sh tools/wltest-apps.sh "custom:sh /w/tools/wltest-apps/xcheck.sh" 40` -> `xrdb -query` says `Xft.dpi: 192` |
| W16 | `tests.yml` job `wl` (checks + mutants-1..4) so every compositor test is a `tests / ...` job; `releasegate.py` requires `tests / wl (checks)` to have run | `releasegate.py selftest` (3 new cases) |
| W29 | the mutant runner counts a catch only if the named scenario printed its own FAIL line (exit 1); compile errors, a misspelt scenario, the alarm and crashes are not catches; `--shard I/N` | the runner itself |
| W15 | `tools/parity/guest/checks/apps.sh` sees X windows (`xprobe.py` reads `_NET_CLIENT_LIST` on the X root; DISPLAY is taken from the desktop's own published environment); the VLC and Zoom lines are gone from `known-gaps.txt` | `xprobe.py` against a real Xwayland + aurshell + xterm (same xcheck.sh run: `xwindows 1 0x40000c`, exit 0; it first failed on a wrong InternAtom length, found by that run). The booted-image parity run (`parity.yml`) was NOT run by this stream, see "Not proven" |

## Not proven / traps

- **Not proven on a booted image**: VLC and Zoom actually keep the screen on over D-Bus (only a libdbus client was
  tested), and parity's `apps.vlc`/`apps.zoom` now pass or fail for real. A red parity `apps.*` after this is the
  check doing its job, not a regression of the check.
- `org.freedesktop.portal.Inhibit` (Firefox/Chromium inside Flatpak may use it), `org.freedesktop.PowerManagement`
  (VLC's fallback) and `org.gnome.SessionManager` are NOT served. Firefox on this desktop uses `zwp_idle_inhibit_v1`
  (done earlier, `aurwl_idle_inhibited`).
- The clipboard bridge limit (written at the top of `xclip.inc`): X lets any client name another's window as the
  requestor and the focused X program can read what it is given. This stops background programs, not a hostile
  focused one; X programs were never isolated from each other.
- A copy made in an X program and switched away from within a few milliseconds can be missed (the XFixes event
  arrives after focus moved): the paste is then empty, never someone else's text.
- `SetSelectionOwner(None)` on release is sent with CurrentTime; a race with another program's copy in the same
  millisecond could clear that copy. Cost: one lost copy.
- Programs open before "Bigger text" changes keep their size (Xft.dpi is read at startup).
- The review's `release-requires.txt` additions are by hash of the pushed commits; rebases change hashes, so never
  add a hash of a local commit.
- Do not `rsync --delete` into `~/work/auros/wl2` while a `wltest-apps.sh` run is using it (`wltest-apps-out`
  is inside it).

## How to run

- Everything on homebase: `rsync` the worktree to `~/work/auros/wl2/`, then in the `auros-s3-fuzz:latest` image
  (clang, wayland, xkbcommon): `sh tools/wltest-x.sh -asan`, `sh tools/wltest-sub.sh -asan`, `sh tools/wltest-deco.sh`,
  `sh tools/fuzz/regress.sh /tmp/o`, `python3 tools/wltest-sub-mutants.py [--only X] [--shard 1/4]`. The D-Bus tests
  need libdbus-1-dev, dbus, gdbus: image `auros-wl2-dbus:dev` (`FROM ubuntu:24.04` + those packages + python3).
- CI: `tests / wl (checks)` and `tests / wl (mutants-N)` in `tests.yml`.

## Next

1. Read the first green/red `tests / wl (...)` run; fix any runner-only difference.
2. Portal Inhibit if a booted-image test shows a program that needs it (Chromium in a Flatpak).
3. PRIMARY selection and drag and drop across X and Wayland (WL.md "not done").
