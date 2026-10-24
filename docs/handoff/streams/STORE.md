# Stream STORE: "Get more programs" is never an empty page (session B)

Owns: `rootfs/usr/lib/auros/software-ready.sh` and its unit, forge `stage_flathub*`, `tools/store*`, `tools/softwarereadytest.sh`.

## The problem
RC1/RC6 on real Windows: GNOME Software opened a header bar over an empty page for 5 minutes ("failed to get featured apps: no apps to show"). Bare image under `[e2e:aur]`: catalogue in 42 s.

## Proven
- **The blank page IS an empty catalogue, not a compositor bug.** Real aurshell + real GNOME Software offline with no catalogue draws exactly the blank of shot 0041; search "vlc" says "No App Found" (`docs/shots/store/rc6-offline-*.png`).
- **ROOT CAUSE (F, run 40050260949, confirmed here by reproduction):** the first session after a real install is the staging kernel handing over in the same boot. The card is already UP (the kernel has given it a SLAAC IPv6 address), so NetworkManager "assumes" it (`connection-assumed`, sys-iface-state external), never runs DHCP, and sits in CONNECTED_LOCAL with no IPv4 default route. software-ready's catalogue step then fails in 0.1 s every minute (apt still works over the IPv6 it has); chrony never syncs. Reproduced under KVM on homebase with RC6 + a unit that does only `ip link set enp0s1 up` before NetworkManager: CONNECTED_LOCAL, "assumed", the same flatpak error. With `[device] keep-configuration=no` (`rootfs/etc/NetworkManager/conf.d/20-auros-fresh.conf`): DHCP in 1 s, CONNECTED_GLOBAL, catalogue fetched at +7 s. (Flushing and downing the card before NM also works; not shipped, one line is enough.) Not the clock (guest clock = host to the second), VM config (same `Machine`), or the compositor.
- Misleading error worth knowing: `flatpak update --appstream` with no route (or a clock >=30 min ahead: Flathub's summary expires) says "No such ref 'appstream2/x86_64' in remote flathub".

## Done
- build/forge `stage_flathub_catalogue`: Flathub's catalogue is in the image (4798 programs, icons 64x64 and 128x128). `REQUIRE_FLATHUB_CATALOGUE=1` (build.yml) makes a build without it fail. Size: **117 MB in /var/lib/flatpak (raw), 61 MB as tar.gz**; compare pieces.txt of the next build.
- software-ready.sh: one stamp per step (`catalogue-ready`, `apt-ready`; `software-ready` when both), reasons journaled, retry 10/20/40/60 s. `tools/softwarereadytest.sh` (16 checks, 2 mutants red).
- `tools/storecatalogue.sh` (static check of the rootfs, selftest with 4 broken trees), run in tests.yml `unit`.
- `tools/storeoffline.sh IMAGE.img`: the real image's rootfs, aurshell and store, no network. **Red on RC6** (`--expect=empty`: "no apps to show", 188 vivid pixels, search answers nothing); **green on RC6 + the forge catalogue call** (53197 vivid pixels, search answers `org.videolan.VLC`; `docs/shots/store/shipped-offline-*.png`). Needs Linux + docker + sfdisk; run on homebase.

- `tools/nmassumetest.sh IMAGE.img` (KVM): boots a copy of the image with the card pre-configured as staging leaves it, with and without the conf: CONNECTED_GLOBAL + lease + route + catalogue, vs CONNECTED_LOCAL (red without). `tools/nmtest.sh` also checks the merged NM config statically (red without the file).
- `31-auros-software` (NM dispatcher) wakes software-ready.sh from its pause when a network comes up (USR1); `softwarereadytest.sh` 18 checks, 3 mutants red.
- What keep-configuration=no does to what else: a Wi-Fi card has no address when NM starts, so the keyfile the installer writes (D10) is unaffected; a wired profile she has is a profile and is kept; only an address set outside NetworkManager (the staging leftover) is no longer honoured. On an NM restart the card briefly reconnects.

## Not built (deliberately)
Item 3, plain words while the store loads: with the catalogue shipped and a build gate (`REQUIRE_FLATHUB_CATALOGUE`), the blank page cannot occur on a release. GNOME Software shows its own white "..." placeholder for ~4 s on a fast machine (longer on an old one, while it reads the 49 MB listing the first time); changing the store's launch line to add a card risks the shell's store detection (C) and D-Bus activation, so it is left. If wanted: a wrapper posting a card through `busctl --user call org.freedesktop.Notifications` as `rootfs/usr/lib/auros/program-file` does.

## Next
After the CI image: run `tools/storeoffline.sh` and `tools/nmassumetest.sh` on it (homebase), compare pieces.txt for the real size cost, re-run `[e2e:base-fastpath]`.
