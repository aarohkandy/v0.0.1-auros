# The programs she installs, and what the image carries (stream PARITY, check (f) and (d))

Proof: `tools/parity/guest/checks/apps.sh` (the list is `tools/parity/guest/apps.tsv`; entry point `tools/apptest.sh`), run by
`.github/workflows/parity.yml` in a VM under KVM on the newest built image. Evidence screenshots: prerelease `parity-<run>`
(`apps-apps-<name>.png`, `apps-apps-<name>-ready.png`, the shell's "is ready" card).

For each program: install with the store's own backend as the desktop user, in the desktop's own login session
(`flatpak install --system`, no password: polkit allows the active local user), see its launcher appear, run it the way
the shell does (the Exec line, with `WAYLAND_DISPLAY` and the shell's environment) with `WAYLAND_DEBUG=1`, and wait up to
90 s for an `xdg_toplevel` (a window). A picture is taken and compared with the screen before.

## Result: build 40008758721 (commit 2e04025, has the capability fix 6b6c438), run 40012157679

| program (Flathub id) | installs | window | notes |
|---|---|---|---|
| WhatsApp, an unofficial web wrapper (com.ktechpit.whatsie) | 42 s, 329 MB | opens in 2 s | |
| Teams, Skype's successor (com.github.IsmaelMartinez.teams_for_linux) | 10 s, 360 MB | opens in 2 s | Electron, uses Wayland |
| Spotify (com.spotify.Client) | 17 s, 14 MB | opens in 1 s | |
| Solitaire (org.gnome.Aisleriot) | 26 s, 83 MB | opens in 1 s | |
| Thunderbird (org.mozilla.thunderbird) | 9 s, 343 MB | opens in 2 s | the old id org.mozilla.Thunderbird is end-of-life and installs the ESR under a different launcher name |
| Photos (org.gnome.gThumb) | 3 s, 26 MB | opens in 2 s | |
| LibreOffice (org.libreoffice.LibreOffice) | 21 s, 804 MB | opens in 1 s | also in the image as debs now |
| Chromium, the call browser (org.chromium.Chromium) | 13 s, 471 MB | opens in 1 s | shows its own "Additional Terms of Service" window first |
| **Zoom** (us.zoom.Zoom) | 63 s, 16 MB | **no window** | X11-only Flatpak, this desktop has no Xwayland: starts, logs nothing, shows nothing (WP-22) |
| **VLC** (org.videolan.VLC) | 37 s, 139 MB | **no window** | X11-only: `main interface error: no suitable interface module` (WP-22) |

Store catalogue: a search for "Zoom" answers 2 s after the check begins (plan: 30 s). All ten together take 7.3 GB of
runtimes and apps on disk (the runtimes are shared: freedesktop 25.08 with the codecs extension, GNOME 50, KDE 6.11 and
5.15). Install times are on a fast datacentre link; on her home connection the first program costs minutes, the next ones
seconds. A root partition under ~12 GB cannot hold all ten.

Before the capability fix (build 956af4a and earlier) none of these started: `bwrap: Unexpected capabilities but not setuid`.
The desktop session and everything it launched carried CAP_WAKE_ALARM; bubblewrap 0.9.0 is not setuid and refuses to run
with any capability. Fixed by stream C (`6b6c438`); the harness found it, measured it in `/proc/<pid>/status`, and proved the
one-line candidate (`CapabilityBoundingSet=~CAP_WAKE_ALARM`) end to end (`--opt capfix`, run 40003821764).

## What the image now carries, and what it cost (WP-25, measured on the same build)

| | before (39906221767) | now (2e04025) |
|---|---|---|
| packages | 831 | 897 |
| installed size (dpkg) | 2,250 MB | 2,577 MB (+327 MB) |
| of which LibreOffice (writer, calc, gtk3, core, uno, fonts-crosextra) | none | 304 MB (libreoffice-core 148 MB, common 47, writer 35, calc 29, style-colibre 12) |
| root filesystem in use after first boot | ~2.5 GB | 2.83 GB of a 4.1 GB partition (73 %) |

The other WP-25 additions measured: ipp-usb 6.1 MB, libmagic-mgc 8.3 MB (so the `file` data is there; the `file` program
itself is not), libheif 0.8 MB + heif-gdk-pixbuf, libmtp 0.5 MB, libimobiledevice 0.2 MB, libnss-mdns 0.14 MB, fonts-crosextra
Carlito and Caladea 3 MB. Decision on the owner's question 14 (Writer and Calc as debs from main): they ship, 304 MB installed.

## Known gaps (tools/parity/known-gaps.txt)

| gap | cost / fix |
|---|---|
| Zoom, VLC and every other X11-only program: no window | a rootful Xwayland fallback in aurshell (WP-22); the `xwayland` package is already in the image (2.3 MB installed); what is missing is the shell side. Until then hide such programs from the store |
| `xdg-open <file>` cannot tell what any local file is | `file` package (0.1 MB; libmagic data is already there) or an `xdg-open` that calls `gio open`; 0 MB |
| Firefox first-run Terms of Use window | a policy/pref change; 0 MB (stream S2) |
| Evince keeps a PDF box only after she leaves it | viewer decision (WP-56) |
