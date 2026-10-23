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

Nothing pushed yet but this file. Read-only findings so far (all measured on the
image of build run 39906221767, commit 75e3321, `auros-pieces` artifact):

- The image is a raw disk: ESP (512 MiB) + AUROS-ROOT (3.8 GiB, 2.25 GB installed,
  831 packages). Firstboot grows the root to the disk (`growpart`/`resize2fs`),
  so a test disk must be enlarged (`truncate -s 24G`) or Flathub runtimes do not fit.
- Present: firefox 140 (tarball/apt), mpv, evince, poppler-utils, cups + `cups-ipp-utils`
  (has `ippeveprinter`), avahi-daemon, flatpak + gnome-software(-plugin-flatpak),
  pipewire/wireplumber/pipewire-pulse, libavcodec60 (H.264/AAC decode), xwayland pkg.
  Kernel modules present: `vivid` (virtual camera with test pattern), `v4l2loopback`, `snd-aloop`.
- ABSENT: any LibreOffice package, `ffmpeg` CLI, `unzip`. So check (d) is a real gap
  today (WP-25 adds Writer+Calc: stream C/P). Size cost of the debs: see docs/results/apps.md.
- Default apps in `/etc/xdg/mimeapps.list` (check (d) needs docx/xlsx/pptx/odt added: WP-25).
- cups-browsed is deliberately NOT installed; a network printer is added with
  `lpadmin -m everywhere` (driverless), not auto-discovered by a daemon.
- aurshell runs as a systemd unit with PAMName (a real logind session); polkit grants
  for flatpak install come from forge's rules for the active local user. Checks that
  must be "her" (flatpak install without a password) therefore have to run inside that
  session's cgroup: the plan is a drop-in whose ExecStart wraps aurshell and the probe
  in one session (`tools/parity/guest/session.sh`).

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
- Commit trailer: use the harness-provided attribution line.
