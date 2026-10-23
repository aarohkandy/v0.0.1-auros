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

Everything lives in `tools/parity/` (README.md there: how it works, how to run, what is simulated). CI: `.github/workflows/parity.yml`
(nightly 03:17 UTC, `[parity]` / `[parity RUN-ID]` in a commit subject, or `gh workflow run parity.yml --ref main -f run-id=<build run>
-f checks=call,film -f apps=vlc -f apt=... -f opt=capfix`). Evidence: prerelease `parity-<run>` (every screenshot, results.md/json, serial log).
The image under test is picked by build run id: the newest build with an `auros-pieces` artifact (NOTE: a `build.yml` run whose `build`
job is skipped has none: use `gh api repos/aarohkandy/v0.0.1-auros/actions/artifacts?name=auros-pieces`).

| check | state on build 39972421083 (956af4a, has WL's subsurface fix) |
|---|---|
| desktop (boots, net, sound, first-run card dismissed) | pass; desktop up ~10 s after power-on under KVM |
| (a) call | WebRTC PASS (vivid camera + PipeWire tone mic; far side gets video+sound). `call.visible` was reported PASS from run 39974574596 but its screenshots were covered by the new first-run card ("Does everything work?", buttons on the left): RETRACTED. The check now presses the right button for either card design and counts saturated hues; re-run 40003736776 decides. |
| (b) film | H.264/AAC MP4 and DASH (Media Source) play with sound out of the decoder; picture check as for call; 720p clip with `--opt perf`. Widevine = v0.2 |
| (c) print | `print.discover` PASS (DNS-SD finds the printer through the image's nftables rule). `print.pdf` FAILS: libnss-mdns is not in the image, so CUPS cannot resolve `parity-printer.local` ("Temporary failure in name resolution"). With `--apt libnss-mdns` the whole check is green (a PDF arrives). Red proof: `--without avahi-daemon` fails print.discover. |
| (d) office | RED, three separate causes: no LibreOffice in the image (WP-25 not landed); `.docx`/`.xlsx` double-click opens the ARCHIVE MANAGER (xarchiver claims zip); `xdg-mime query filetype` is EMPTY for every local file on this desktop, so any program that calls `xdg-open file` opens it in Firefox instead (cause: unknown desktop name, `file` package absent; `gio open` and the file manager work). Fix candidates: ship Writer+Calc, set docx/xlsx defaults, install `file` or wrap xdg-open as `gio open`. Size of the LibreOffice debs: see `docs/results/apps.md` once the `--apt libreoffice-*` run has finished. |
| (e) PDF form | `pdfform.saved` passes with Evince (click box, type, Tab, Save a copy: both boxes in the file). `pdfform.last-box` FAILS: saving straight after typing loses the box she is still in (Evince commits a box when she leaves it). Known gap; the viewer decision is WP-56. In CI the Ctrl+S path was MISSING once (card on top); re-run decides. |
| (f) top ten apps | all ten INSTALL with no password (4-67 s each, 7.3 GB of runtimes in total) but NONE STARTS: see P0 below. `apps.catalogue` (a search finds Zoom 2 s after online) passes. Thunderbird installed with rc=0 but no launcher appeared (investigate with the install output now in the notes). |

### P0 #1 (FIXED by WL, confirmed in principle): Firefox opened as a blank window
aurwl never composited `wl_subsurface`s (Firefox draws its page into one). Fixed by 54b783b + 990fdae + 7799b6d. The regression
check is `call.visible` / `film.*-visible`; see the retraction above: re-verify before believing "PASS".

### P0 #2 (open, C is fixing): NO Flathub program can start (blocks e2e M3 "install VLC from the store and open it": tell F)
`flatpak run` fails with `bwrap: Unexpected capabilities but not setuid, old file caps config?` then `error: ldconfig failed, exit status 256`.
Measured in the booted image (`/proc/<pid>/status`): aurshell and everything below it (the private session bus, every program it
launches, anything the user starts) have CAP_WAKE_ALARM (bit 35) in CapInh, CapPrm, CapEff and CapAmb; `systemd --user` has it in CapInh;
`aurshell.service` itself sets no AmbientCapabilities and our PAM stack has no pam_cap, so it comes from systemd's session setup.
bubblewrap 0.9.0 is not setuid and refuses to run with any capability. Candidate fix (tested): `CapabilityBoundingSet=~CAP_WAKE_ALARM` in
aurshell.service empties all four sets and bwrap gets past the check (`--opt capfix`; run 40003821764 proves flatpak run end to end).
C owns the fix (static + boot check). After it lands: run all ten apps on the first build that has it, report to the lead.

### Other measured facts (image of 39906221767 and 39972421083)

- Disk/first boot: ESP 512 MiB + AUROS-ROOT 3.8 GiB (2.25 GB installed, 831 packages). First boot grows the root to the disk, a few seconds AFTER
  the desktop starts (the harness waits for `/var/lib/auros/firstboot.done`, then `parity-grow` makes sure).
- Present: firefox 158 (Mozilla apt), mpv, evince, poppler-utils, cups + `cups-ipp-utils`, avahi-daemon (publishing disabled), flatpak + gnome-software,
  pipewire/wireplumber, libavcodec60, python3 with full stdlib; user `auros` is in video, audio, lpadmin, input, bluetooth, netdev.
- The image blacklists the `vivid` test camera (`/etc/modprobe.d/auros-hardening.conf`): root.sh loads it with `--ignore-install`.
- ABSENT: LibreOffice, `ffmpeg`, `unzip`, `pactl`, `file`, libnss-mdns.
- The first-run card covers the whole desktop until answered; its layout changed between builds (buttons were at the bottom, now on the left).
- Dropped frames in the VM (software rendering, 4 vCPU, no GPU): 25-125 of ~250 at 360p; this says nothing about a 2013 laptop (v0.2: media-perf.md).

### Things for other streams (exact asks)
- **C**: the CAP_WAKE_ALARM fix above (P0 #2); WP-25: libnss-mdns, Writer+Calc, docx/xlsx/pptx/odt defaults (and keep xarchiver off them); make
  `xdg-open <file>` work (install `file`, or ship a wrapper `xdg-open` -> `gio open`); aurshell prints nothing about windows, so tests read WAYLAND_DEBUG.
- **F (e2e)**: M3's "install VLC then open it" cannot pass until P0 #2 is fixed (the install itself works).
- **S3/WL**: none now.

## Environment traps

- The Mac has no `unzip`/KVM: heavy work on homebase (`~/work/auros/parity/`), image
  already assembled at `~/work/auros/parity/auros-desktop.img` (sha256 verified).
  Download an artifact on homebase: `curl -s -o /dev/null -w '%{redirect_url}' -H "Authorization: Bearer $(gh auth token)" https://api.github.com/repos/aarohkandy/v0.0.1-auros/actions/artifacts/<id>/zip`, then curl that URL on homebase.
- homebase has no `unzip`; use `python3 -c 'import zipfile...'`.
- homebase disk budget (FIELD-MANUAL §3): keep `~/work/auros/parity` under 3 GB. The harness mounts the image
  IN PLACE and boots a qcow2 overlay (no 4.6 GB copy); `dl/` (artifact pieces) is deleted after assembling.
- hb refuses jobs under disk pressure; `dev.sh` runs directly (niced) unless `PARITY_HB=1`.
- Commit trailer: use the harness-provided attribution line.
