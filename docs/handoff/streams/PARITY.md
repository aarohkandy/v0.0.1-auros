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
[-f checks=call,film,print,office,pdfform,apps] [-f apps=vlc,solitaire] [-f apt=libnss-mdns] [-f opt=perf,capfix]`). One run at a time
(concurrency on the job, queued; a second dispatch REPLACES a queued one). Evidence: prerelease `parity-<run>` (every screenshot, results.md/json,
serial log). Pick the image by build run id: the newest run with an `auros-pieces` artifact
(`gh api "repos/aarohkandy/v0.0.1-auros/actions/artifacts?name=auros-pieces"`; a build.yml run whose `build` job was skipped has none).
Results pages: `docs/results/{apps,sites,media-perf,pdf-forms}.md`. `sites.md` is the owner's 25-site campaign sheet (v0.2).

### State on build 40008758721 (2e04025: WL's subsurface fix 54b783b.., C's capability fix 6b6c438, WP-25 packages)

| check | result |
|---|---|
| desktop | pass; desktop.welcome: the first-run card ("Does everything work?") is found by colour and "Let me look first" pressed |
| (a) call | `call.webrtc` PASS (vivid camera + PipeWire tone mic; the far side gets video and sound). Picture (`call.visible`): see "Trust" below |
| (b) film | H.264/AAC MP4, DASH (Media Source) and 720p play with sound out of the decoder; dropped frames 9-164 of ~240 in the 4-vCPU software-rendered VM (media-perf.md) |
| (c) print | PASS since WP-25 (libnss-mdns): DNS-SD discovery, driverless add, a PDF arrives. Red proof: `--without avahi-daemon` fails print.discover |
| (d) office | Writer and Calc ship (304 MB installed) and open+save docx/xlsx (office.save PASS, office.opener PASS). `office.types` FAILS (gap): `xdg-mime query filetype` is empty for every local file, so programs that call `xdg-open <file>` open it in Firefox (fix: `file` package or an xdg-open that calls `gio open`; C) |
| (e) PDF form | `pdfform.saved` PASS (Evince: click box, type, Tab, Save a copy: both boxes in the file); `pdfform.last-box` FAILS (gap): saving straight after typing loses the last box (WP-56 decides the viewer) |
| (f) top ten apps | 8 of 10 start (WhatsApp wrapper, Teams, Spotify, Solitaire, Thunderbird, gThumb, LibreOffice, Chromium; window in 1-2 s); Zoom and VLC are X11-only Flatpaks and there is no Xwayland (WP-22; gaps `apps.zoom`, `apps.vlc`). Details: apps.md |
| Firefox | `firefox.firstrun` (gap): Firefox 158's first window asks her to accept Terms of Use and says it sends diagnostic data to Mozilla although the policy has SkipOnboarding+DisableTelemetry (S2 owns the policy). Seen once: a Firefox crash during the call page whose crash reporter has "Tell Mozilla about this crash" ticked by default (retried once, recorded as `firefox.crash`) |

Trust: the first picture checks were fooled twice (a colour count passed while the first-run card, then Firefox's Terms-of-Use modal, covered the
page). Both are now dismissed by colour, and `call.visible`/`film.*-visible` need >= 5 hues and >= 4000 saturated pixels. Runs 40015793857 (full)
and 40021364066 (desktop, call, pdfform, office) on build 2e04025 are the first with both fixes: `call.visible` PASS with 28,823 saturated pixels in
6 hues, `film.*-visible` 56,000 in 6 hues, apps 8/10; the job exits 0 with the known gaps listed in `tools/parity/known-gaps.txt`.

### Findings already fixed (the harness found them)
1. Firefox was a blank window: aurwl never composited `wl_subsurface`s. Fixed by WL (54b783b, 990fdae, 7799b6d); `call.visible` is the regression check.
2. NO Flathub program could start: the desktop session carried CAP_WAKE_ALARM in all four capability sets and bwrap refuses any. Fixed by C (6b6c438).
3. WP-25: libnss-mdns (printer names), LibreOffice, ipp-usb, heif, libmtp landed after the first reports.

### Things for other streams (exact asks)
- **C**: `xdg-open <file>` must work (office.types); Xwayland fallback for X11-only programs (WP-22: Zoom, VLC); aurshell prints nothing about windows, so tests read WAYLAND_DEBUG (a `--debug` listing would help).
- **S2**: Firefox's first-run Terms-of-Use window and the crash reporter's ticked "Tell Mozilla" boxes vs "no telemetry"; try `browser.preonboarding.enabled=false` and the crash reporter policy.
- **F (e2e)**: M3's "install VLC from the store and open it" installs but VLC opens NO window (X11-only), so pick Solitaire/Thunderbird/LibreOffice as the program to open, or wait for WP-22.
- **P/C**: none open from PARITY on packages (WP-25 verified).

### Next steps, in order
1. Read the last full run's table (40015793857); if a picture check is wrong again, fix the detector (region, thresholds) against its screenshots (`gh release download parity-<run>`).
2. Nightly: let `parity.yml` run (03:17 UTC) and read `parity-<run>`; a gap that starts passing fails the run until it is removed from `known-gaps.txt`.
3. v0.2 (WP-56): Widevine demo with the EME policy; the 10-form PDF test; scanning (simple-scan, sane-airscan); Flathub-down plain sentence; a 40-app table; media-perf on a CPU-limited VM (`--smp 2`); sites campaign by the owner (sites.md); USB printers (ipp-usb) once C adds it.
4. Red proofs (done, homebase or CI): `--without firefox` (call, film), `--without evince` (pdfform), `--without avahi-daemon --apt libnss-mdns` (print), `--without libreoffice-writer,libreoffice-calc` (office.save). Not yet: apps (`--without flatpak`; needs ~25 GB, run it in Actions).
5. homebase: the image was DELETED to stay under the 3 GB budget; re-fetch one when needed (the curl-redirect recipe under "Environment traps": artifact zip, python zipfile, `cat pieces | gzip -dc`), run `tools/parity/dev.sh`, delete it again.

## Environment traps

- The Mac has no `unzip`/KVM: heavy work on homebase (`~/work/auros/parity/`), image
  already assembled at `~/work/auros/parity/auros-desktop.img` (sha256 verified).
  Download an artifact on homebase: `curl -s -o /dev/null -w '%{redirect_url}' -H "Authorization: Bearer $(gh auth token)" https://api.github.com/repos/aarohkandy/v0.0.1-auros/actions/artifacts/<id>/zip`, then curl that URL on homebase.
- homebase has no `unzip`; use `python3 -c 'import zipfile...'`.
- homebase disk budget (FIELD-MANUAL §3): keep `~/work/auros/parity` under 3 GB. The harness mounts the image
  IN PLACE and boots a qcow2 overlay (no 4.6 GB copy); `dl/` (artifact pieces) is deleted after assembling.
- hb refuses jobs under disk pressure; `dev.sh` runs directly (niced) unless `PARITY_HB=1`.
- Commit trailer: use the harness-provided attribution line.
