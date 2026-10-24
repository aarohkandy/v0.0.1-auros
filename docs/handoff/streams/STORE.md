# Stream STORE: "Get more programs" is never an empty page (session B)

Owns: `rootfs/usr/lib/auros/software-ready.sh` and its unit, forge `stage_flathub*`, `tools/store*`, `tools/softwarereadytest.sh`.

## The problem
RC1/RC6 on real Windows: GNOME Software opened a header bar over an empty page for 5 minutes ("failed to get featured apps: no apps to show"). Bare image under `[e2e:aur]`: catalogue in 42 s.

## Proven
- **The blank page IS an empty catalogue, not a compositor bug.** Real aurshell + real GNOME Software offline with no catalogue draws exactly the blank of shot 0041; search "vlc" says "No App Found" (`docs/shots/store/rc6-offline-*.png`).
- software-ready.sh's catalogue step failed in 0.1 s every minute on every installed run (RC1, RC6; 5 runs) while apt succeeded 7 s later, and on 0 of 3 bare runs. Not the clock (guest clock = host to the second), not VM config (same `Machine`), not the compositor. Its stderr went to /dev/null; it is now journaled. F's journal (run 40050260949): NetworkManager "connection-assumed" after the staging kernel handed over; no DHCP, CONNECTED_LOCAL.
- Misleading error worth knowing: `flatpak update --appstream` with no route (or a clock >=30 min ahead: Flathub's summary expires) says "No such ref 'appstream2/x86_64' in remote flathub".

## Done
- build/forge `stage_flathub_catalogue`: Flathub's catalogue is in the image (4798 programs, icons 64x64 and 128x128). `REQUIRE_FLATHUB_CATALOGUE=1` (build.yml) makes a build without it fail. Size: **117 MB in /var/lib/flatpak (raw), 61 MB as tar.gz**; compare pieces.txt of the next build.
- software-ready.sh: one stamp per step (`catalogue-ready`, `apt-ready`; `software-ready` when both), reasons journaled, retry 10/20/40/60 s. `tools/softwarereadytest.sh` (16 checks, 2 mutants red).
- `tools/storecatalogue.sh` (static check of the rootfs, selftest with 4 broken trees), run in tests.yml `unit`.
- `tools/storeoffline.sh IMAGE.img`: the real image's rootfs, aurshell and store, no network. **Red on RC6** (`--expect=empty`: "no apps to show", 188 vivid pixels, search answers nothing); **green on RC6 + the forge catalogue call** (53197 vivid pixels, search answers `org.videolan.VLC`; `docs/shots/store/shipped-offline-*.png`). Needs Linux + docker + sfdisk; run on homebase.

## Next
See the bottom of this file as it is updated.
