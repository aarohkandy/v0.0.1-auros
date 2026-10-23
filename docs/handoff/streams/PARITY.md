# Stream PARITY: everyday tasks actually work (session B)

Goal: what she does on Windows or a Mac every day is proven on AurOS every
night in a KVM VM that boots the newest built image: (a) video call,
(b) a film, (c) printing, (d) .docx/.xlsx open and save, (e) PDF form fill and
save, (f) the top ten Flathub apps install from "Get more programs" and start.
Each check is shown red once by removing its package. Screenshots are published
as a prerelease asset `parity-<run>`.

Owns: `tools/parity/**`, `tools/apptest*`, `.github/workflows/parity.yml`
(nightly + `[parity]` in the subject, never on push), `docs/results/{apps,sites,media-perf,pdf-forms}.md`.

## Status (update this section every push)

Harness pushed (`c5badfa` and later): `tools/parity/run.py` + `guest/`. Develop on homebase with
`sh tools/parity/dev.sh --checks call` (rsyncs tools/parity, runs a privileged container, copies
results to ./parity-out/ incl. serial.txt and screenshots/). `PARITY_SCRIPT=file.sh sh tools/parity/dev.sh`
runs an ad-hoc script in the guest as the desktop user (notes appear in serial.txt as `PARITY note |`).

| check | state |
|---|---|
| desktop (boots, net, sound) | pass; desktop up ~10 s after power-on under KVM |
| (a) call | WebRTC passes (vivid camera + PipeWire tone mic, remote gets video and sound); `call.visible` FAILS: see P0 below. Listed in known-gaps.txt |
| (b) film, (c) print, (d) office, (e) PDF form, (f) apps | not written yet |

### P0 FOUND: Firefox opens as a BLANK window on AurOS

Proven on build 39906221767 (75e3321) in the nightly harness and by a control: Mousepad (GTK3) draws its
text in the same shell; Firefox 158 shows its title bar and a window-sized area filled with one colour
(252,250,245), for 60+ s, while its page demonstrably runs (the call page reports over HTTP).
Cause (WAYLAND_DEBUG trace): Firefox puts only its client-side frame in the toplevel surface (a 845x646
buffer) and draws the page into a `wl_subsurface` (793x594 at offset 26,23). `src/aurwl/aurwl.c` keeps each
surface's buffer in its own `store` and `aurwl_win_content()` returns only the toplevel's; no code
composites subsurfaces into the window. Same for anything else that renders through a subsurface
(Chromium/Electron apps use one for video, GTK4 and Qt apps for popups/video). Fix belongs in aurwl
(S3 owns it, C owns the shell): flatten a toplevel's subsurface tree (positions `sub_x/sub_y`, sync/desync) into
what `aurwl_win_content()` returns. A regression check for it is `call.visible` (and `film.visible`):
they flip to PASS the day it is fixed, and run.py then fails the job until the gap is removed from known-gaps.txt.

Other measured facts (image of 39906221767):

- The image is a raw disk: ESP (512 MiB) + AUROS-ROOT (3.8 GiB, 2.25 GB installed, 831 packages).
  First boot grows the root to the disk (`growpart` + `resize2fs`, ~16 GB in the harness), but it finishes
  a few seconds AFTER the desktop starts: the harness waits for `/var/lib/auros/firstboot.done`.
- Present: firefox 158 (Mozilla apt), mpv, evince, poppler-utils, cups + `cups-ipp-utils` (ippeveprinter),
  avahi-daemon, flatpak + gnome-software(-plugin-flatpak), pipewire/wireplumber/pipewire-pulse, libavcodec60,
  python3 (full stdlib), `pw-loopback`/`pw-play`/`wpctl`; user `auros` is in video, audio, lpadmin, input, bluetooth.
- The image blacklists the `vivid` test camera (`/etc/modprobe.d/auros-hardening.conf`); root.sh loads it with
  `modprobe --ignore-install`. v4l2loopback exists but needs a feeder (no ffmpeg/gstreamer CLI in the image).
- ABSENT: any LibreOffice package, `ffmpeg`, `unzip`, `pactl`. Check (d) is a real gap today (WP-25).
- `/etc/xdg/mimeapps.list` has no docx/xlsx/pptx/odt entries (WP-25).
- cups-browsed is deliberately NOT installed; a network printer is added with `lpadmin -m everywhere`.
- The welcome card ("AurOS is on this computer", Yes / Let me look first / No) covers the desktop at first boot;
  run.sh presses "Let me look first" (click 620,703 at 1280x800).
- aurshell runs as a unit with PAMName (a real logind session). The harness wraps it: drop-in
  `aurshell.service.d/60-parity.conf` runs `session.sh`, which starts the real, unmodified aurshell and the
  checks side by side in the SAME session cgroup, so polkit/sound/session-bus see the checks as the person at the keyboard.

## Plan, in order

1. `tools/parity/run.py`: host runner. Boots the image under KVM (OVMF, 4 GB, virtio-net
   user networking, QMP), injects a probe via a scratch copy of the root, reads results
   from the serial port, takes screenshots over QMP (`screendump`), writes
   `results.json` + a markdown table. Known gaps live in `tools/parity/known-gaps.txt`
   (a gap that fails is "GAP", a gap that passes is a failure: remove it from the list).
2. Checks (a) call, (b) film, (c) print, (d) office, (e) PDF form: `tools/parity/guest/`.
3. `.github/workflows/parity.yml`: nightly cron + `[parity]` subject; downloads the newest
   `auros-pieces` artifact (gh) or prerelease; job timeouts; bounded apt; concurrency queued;
   publishes `parity-<run>` prerelease with screenshots.
4. `tools/apptest.sh` (+ `tools/apptest/apps.list`): top ten Flathub apps.
5. `--without <pkg>` red proofs per check.
6. Results docs: `docs/results/{apps,sites,media-perf,pdf-forms}.md`.

## Environment traps

- The Mac has no `unzip`/KVM: heavy work on homebase (`~/work/auros/parity/`), image
  already assembled at `~/work/auros/parity/auros-desktop.img` (sha256 verified).
  Download an artifact on homebase: `curl -s -o /dev/null -w '%{redirect_url}' -H "Authorization: Bearer $(gh auth token)" https://api.github.com/repos/aarohkandy/v0.0.1-auros/actions/artifacts/<id>/zip`, then curl that URL on homebase.
- homebase has no `unzip`; use `python3 -c 'import zipfile...'`.
- homebase disk budget (FIELD-MANUAL §3): keep `~/work/auros/parity` under 3 GB. The harness mounts the image
  IN PLACE and boots a qcow2 overlay (no 4.6 GB copy); `dl/` (artifact pieces) is deleted after assembling.
- hb refuses jobs under disk pressure; `dev.sh` runs directly (niced) unless `PARITY_HB=1`.
- Commit trailer: use the harness-provided attribution line.
