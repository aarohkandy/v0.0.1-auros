# tools/parity: everyday tasks, proven on the built image

What she does on Windows or a Mac every day, done on AurOS in a KVM VM that boots the image build.yml made.
State, findings and next steps: `docs/handoff/streams/PARITY.md`. Results: `docs/results/{apps,sites,media-perf,pdf-forms}.md`.

| check | what is proven |
|---|---|
| `desktop` | desktop, network and sound come up |
| `call` | a real V4L2 camera (kernel `vivid`) and a PipeWire microphone (a tone) go through WebRTC in Firefox; the far side gets video and sound; the window shows the page (`call.visible`) |
| `film` | an H.264/AAC MP4 and a DASH stream (Media Source, separate audio and video) play with sound; the window shows the picture |
| `print` | a printer on "another machine" (a network namespace, ippeveprinter + mDNS) is found over DNS-SD, added driverless, a job arrives as a PDF |
| `office` | xdg-open can type a docx/xlsx/pdf; something sensible opens them; Writer and Calc open and save them |
| `pdfform` | double-click a form, click boxes, type, Save a copy: both boxes are in the file |
| `apps` | the top ten Flathub programs install with no password, get a launcher, open a window (separate, long run) |

## Run it

```sh
sudo python3 tools/parity/run.py --image auros-desktop.img --out parity-out [--checks call,film] [--apt libnss-mdns]
sudo sh tools/apptest.sh --image auros-desktop.img --out apptest-out [--apps vlc,solitaire]
```

Needs root (loop mount), KVM, `qemu-system-x86 qemu-utils ovmf gdisk`. The image is the RAW disk
(`auros-pieces` artifact, `cat auros-desktop.img.gz.* | gzip -dc`). It is mounted in place (no copy), the
guest side from `guest/` is laid into it, and the VM boots a qcow2 overlay: everything a run writes is thrown away.
In CI: `.github/workflows/parity.yml` (nightly, `[parity]`, or dispatch).
On homebase: `sh tools/parity/dev.sh --checks call` (rsync, privileged container, results in `./parity-out/`);
do not run the apps check or `--apt libreoffice-*` there (disk budget).

## How it works

`run.py` writes `guest/` into `/usr/lib/auros-parity` and a drop-in `aurshell.service.d/60-parity.conf` that runs
`guest/session.sh`: the real, unmodified `aurshell` and `guest/run.sh` side by side in the same login session. The
checks run as the desktop's own user with the environment the shell publishes. They talk on the serial port:

- `PARITY result NAME pass|fail detail` (a name `check.sub` is a sub-result; the check passes when all its parts pass)
- `PARITY do snap NAME` / `key ctrl+s` / `type TEXT` / `click X Y` / `pixels SHOT RESULT x0 y0 x1 y1 MIN`:
  carried out by the host over QMP (screenshots, keyboard, tablet, and a colour count to tell a drawn window from a blank one)

## Known gaps, and proving a check can fail

`known-gaps.txt`: a check that fails today for a named reason is shown as `GAP` and does not fail the job; a gap that
starts passing FAILS the job until its line is deleted. So a fix cannot go unnoticed and a gap cannot hide a regression.

Red proofs: `--without PKG` removes a package before the desktop starts (in the overlay) and `--expect-fail CHECKS` makes
the run demand that those checks fail. Used: `--without firefox` (call, film), `--without evince` (pdfform),
`--without cups-filters --apt libnss-mdns` (print). `--apt PKG` installs Ubuntu packages first, to show a check green
once the image carries them (`--apt libnss-mdns` for print; `--apt libreoffice-writer,libreoffice-calc,libreoffice-gtk3`
for office, which also prints the size cost).

## What is simulated, and what is not

- The camera is `vivid` (the kernel's test camera; the image blacklists it, root.sh loads it with `--ignore-install`),
  not a UVC webcam; the microphone is a virtual PipeWire source fed with a tone; sound goes to QEMU's HDA with no speaker.
- The printer is another network namespace on the same VM, not another machine; adding it uses `lpadmin` as the user,
  not the Printers window.
- Firefox gets a fresh profile with `media.navigator.permission.disabled` and autoplay allowed (nobody clicks a prompt
  in CI); the image's `/etc/firefox/policies` apply on top.
- Widevine/Netflix, real call services (Jitsi/Meet/Zoom), a real webcam and a real printer are v0.2/field work (WP-56, WP-33).
