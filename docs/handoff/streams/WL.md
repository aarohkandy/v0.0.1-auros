# Stream WL: the Wayland compositor draws every program correctly (session B)

Goal: every program a person runs on AurOS draws what it drew. Started as a P0 from
PARITY: **Firefox opened as a blank window** because `aurwl` never composited
`wl_subsurface`s (Firefox draws its page into one; Chromium/Electron video, GTK4 and Qt
popups/video, mpv/VLC do too).

Owns: `src/aurwl/**` except W1's `wishhook.*` and `textinput.*` (W1 adds hooks through
weak functions; coordinate through this file), `tools/wltest*`, and the minimal
compositing edits in `src/aurshell` (stream C owns the rest; every such edit is named in
its commit body).

## Status (update this section every push)

| piece | state |
|---|---|
| wl_subsurface tree: sync/desync with a cached snapshot, effective sync through parents, nested, position + z-order applied with the parent's commit, "the parent is in the stack" (below-parent), destroy/unmap at once | done, `tools/wltest-sub.c` (20 scenarios, byte-for-byte) |
| flattened window (`aurwl_win_content()` returns the tree composited, clipped to the toplevel's buffer / viewport size; damage-based; nothing changes for a window with no visible subsurface) | done |
| wp_viewport on subsurfaces and roots: destination scaling (nearest, pixel centres), source crop (`set_source`) | done |
| pointer hit-testing through the tree, input regions (`set_input_region`), implicit grab, per-surface enter/leave in the surface's own coordinates | done |
| keyboard focus (and with it W1's text-input-v3 focus) stays on the toplevel, as in every compositor; W1's weak hooks untouched | proven: `wltest-sub textinput`, and REAL Firefox (page in a subsurface): `sh tools/wltest-apps.sh firefox-pw` shows textinput_state 1 (plain) in a text field, **2 (sensitive; purpose 8, hint 0xc0)** in a password field, 0 outside any field |
| **zwp_idle_inhibit_v1** (a film must not go dark: the shell blanks after 10 idle minutes; mpv/Firefox/VLC ask for an inhibitor; `aurwl_idle_inhibited()` + 5 lines in main.c) | done; real mpv binds it and creates one (WAYLAND_DEBUG); `wltest-sub idle`. NOT covered: apps that inhibit through D-Bus org.freedesktop.ScreenSaver (the shell owns the bus: stream C) |
| **drag and drop** (wl_data_device.start_drag was `cancelled` for everyone: Thunar, the image's file manager, could not move a file by dragging): pointer belongs to the drag, enter/motion/leave/drop to the surface under it (subsurfaces in their own coordinates), accept/set_actions/action/target negotiation, finish/dnd_finished, Esc cancels, drag icon (`aurwl_drag_icon`, painted by session.c), release off every window still ends it (session.c edit) | done; `wltest-sub dnd` + 8 mutants; **real Thunar moved a.txt into a folder** (`DND_FROM=285,175 DND_TO=210,180 sh tools/wltest-apps.sh thunar-dnd 24`) |
| **pointer pictures** (the lead's "she never sees the hand / I-beam"): wl_pointer.set_cursor (surface, hotspot, attach offsets, animation, null = hide) and wp_cursor_shape_v1, honoured only from the program under the pointer quoting its latest enter, forgotten on enter/leave/quit; `aurwl_cursor()`; shell: `src/aurshell/cursor.{c,h}` (11 glyphs black+white outline, 34 shapes mapped, a program's own picture scaled so its canvas is 24 px x text step) + `session_paint_cursor` | done; `tools/cursortest.sh` (byte-exact vs a reference), wltest-sub `cursor`/`cursor-shape`, 20 mutants; **real Firefox on the real aurshell shows the hand over a link, the I-beam in a field, the arrow on the page, and larger at 200% text** (`MOUSE="300 261" SHELLF=workbench TEXT_SCALE=2.0 sh tools/wltest-apps.sh firefox 30`). Firefox gives 32x32 images (GTK3 ignores XCURSOR_SIZE and settings.ini's gtk-cursor-theme-size; it reads GSettings org.gnome.desktop.interface cursor-size): scaling 1.5x is soft; stream C could set that key beside text-scaling-factor in textsys.c for crisp 48 |
| other protocols (see "Protocol review") | in progress |
| real programs through the real shell (`sh tools/wltest-apps.sh firefox|gtk4|qt|mpv`, headless `aurshell --png --with-app`): Firefox 158 draws toolbar + page (+ its welcome dialog), GTK4 widget-factory, Qt5 terminal, mpv video (wlshm subsurface) | all draw; PNGs in the Progress log of SESSION-2026-10-23-B.md |
| PARITY `call.visible` flips to PASS on a booted image | **done**: PARITY confirmed on the real image (call.visible, webrtc, films PASS) |
| painting cannot be made expensive by a client (3000 stretched subsurfaces) | work budget in `compose_node`; `wltest-sub budget` |

## How it works (read this before touching `aurwl.c`)

- Every `wl_surface` is an `aurwl_win` with its own `store` (a copy of its last buffer).
  A subsurface's `parent` is the surface it is stacked on; a toplevel/popup is a *root*.
- **Stack**: each surface keeps `sub_pend` (what `place_above/below` edit) and `sub_cur`
  (what is drawn and hit-tested), bottom to top, and the surface ITSELF is a node in both
  (`self_pend`/`self_cur`), so a child "below the parent" is just an earlier node.
  `children_apply()` copies pending to current when the parent's state is applied.
  `children` / `child_link` are for xdg popups only.
- **Commit**: `surf_commit` folds `pending` into `cached` (`cache_pending`), then returns if the
  surface is effectively synchronized (`eff_sync`: own flag OR any ancestor's), else
  `apply_cached` -> `apply_state` -> `children_apply`, which also applies every child's cache.
  `set_desync` calls `flush_desync`.
- **Compositing**: `flat_get(root)` (called from `aurwl_win_content`, lazily) repaints the
  dirty rectangle of `root->flat` from the stack (`compose_node`, `blit_over`: premultiplied
  src-over; nearest scaling when a viewport differs). Content damage maps to a rectangle;
  anything structural (position, order, map/unmap, resize) repaints the window. No visible
  subsurface -> `flat` is freed and the shell is handed `store` exactly as before.
  Flat size = root's logical size (viewport destination if any, else buffer) x root buffer scale.
- **Input**: the shell still passes the ROOT window and a point in its surface coordinates;
  `aurwl_pointer_motion` resolves it with `pick()` to the topmost surface that takes input
  (`current.in_kind`: whole / none / box) and tracks `c->ptr_surf` (the surface that was sent
  `enter`) separately from `c->ptr_focus` (the root). `btns_down` is the implicit grab.
- **Touch**: the shell turns a touchscreen into pointer clicks (main.c DEV_DIRECT), so it goes
  through the same path. aurwl has no `wl_touch` event path at all (advertised, never sent).

## Protocol review (what the real programs bind, WAYLAND_DEBUG on Firefox 158, GTK4, mpv)

Bound and served: wl_shm, wl_compositor (v3-5), wl_subcompositor, wl_seat (v5-7), wl_output (v2-3), xdg_wm_base,
wl_data_device_manager, wp_viewporter, zxdg_output_manager_v1, (Qt) zxdg_decoration_manager_v1, zwp_idle_inhibit_manager_v1.
Firefox and GTK never bind xdg-decoration (GTK never implements it) -> they draw their own title bar. Not offered, and
every program works without: linux-dmabuf (shm path), presentation-time, fractional-scale, primary selection,
cursor-shape, xdg-activation. Programs fall back; what each costs: no middle-click paste, one arrow cursor for every
control, no raise-on-link-click.

New protocols are generated to `src/aurwl/gen/<name>-protocol.inc` and `#include`d INTO aurwl.c (forge's second
generator loop), so no list of source files elsewhere (forge, fuzz build.sh, wishtest.sh, shots.sh, ...) needs to learn them.
Add the XML to that loop + the README loop, `#include` the .inc in aurwl.c, add a row to fuzz_aurwl.c's `global_name` table
(AFTER the existing rows: seeds index into it).

## Not done, in order of value (each program works without them; what it costs her)

1. (done: cursor pictures, see the table.) Possible polish: a drag cursor (the arrow with a +/- during a drag), and
   crisp 48 px images (see the GSettings note).
2. **Primary selection** (middle-click paste), **xdg-activation** (raise the window a link opened), **presentation-time**,
   **fractional-scale**: not offered; Firefox, GTK4, Qt, mpv all work without. Add by the `.inc` recipe above.
3. **linux-dmabuf**: not needed for correctness (every program falls back to shm; GTK4's EGL fails and it uses cairo).
   It would let Firefox/GTK use the GPU on a machine that has one (LINEAR modifier only, CPU copy); a performance feature.
4. **Idle inhibit over D-Bus** (org.freedesktop.ScreenSaver): the shell owns the bus (stream C, on their list).
5. `wl_touch` events are never sent (the shell turns a touchscreen into pointer clicks, which goes through the same path).
6. Popups: `xdg_popup.grab` is accepted and ignored (no `popup_done` on an outside click, Esc is the client's own).
7. Qt6 was not run (no Qt6 GUI app in the container; Qt5 qterminal and the same QtWayland client draw).

## Tests

- `sh tools/wltest-sub.sh [-asan] [scenario]` (Linux: libwayland-dev, libxkbcommon-dev). 20
  scenarios; each is a client that builds a tree with known pixels and asserts
  `aurwl_win_content()` against a reference model written separately in the test.
  Shown RED on the pre-fix compositor: `git show 3a6d0cd:src/aurwl/aurwl.c > /tmp/old.c;
  AURWL_C=/tmp/old.c sh tools/wltest-sub.sh` -> 3 of 20 pass (plain, frame, wreck).
- **Mutation proofs**: `python3 tools/wltest-sub-mutants.py` breaks 25 things in a copy of aurwl.c (one exact replacement each, must match once) and requires the named scenario to fail; first run found 9 checks that could not fail (counted any release instead of that buffer's; never cleaned the window before testing partial damage; ...), fixed. Takes ~4 min on homebase; not in CI.
- Fuzz: `tools/fuzz/corpus/aurwl/subsurface*` seeds (get_subsurface, set_position, sync/desync, place_below, viewport src/dst, input region); 7 min libFuzzer on homebase, 851k runs, no finding.
- Runs in `.github/workflows/fuzz.yml` (S3's file; one added step) on every push touching src/aurwl.
- homebase: `docker run --rm -v "$PWD":/w -w /w auros-s3-fuzz:latest sh tools/wltest-sub.sh`
  (that image has clang, libwayland-dev, libxkbcommon-dev). rsync the worktree to
  `~/work/auros/wl/` first.

## Traps

- `wl_list_remove()` NULLs the node; always `unlink_init()` a node you may remove twice.
- The stack lists contain a sentinel for the surface itself: never `wl_container_of` a node
  without first comparing it with `&s->self_cur` / `&s->self_pend`.
- `aurwl_win_content()` may reallocate `flat`; the shell's `view` pointer is re-filled on every call.
- Position/z-order are the PARENT's state: moving a subsurface and committing only the
  subsurface moves nothing (the protocol; `wltest-sub pointer-moves` checks it).
- Premultiplied ARGB everywhere (wl_shm). The shell's `draw_scaled` treats window pixels as
  straight alpha; that was always so and is only visible for translucent windows.
- homebase disk is tight (94% used at the time of writing): do not build images there.
