# ═══════════════════════════════════════════════════════════════════
#  AurOS Profile — Desktop (the default)
#
#  A profile is the whole customization surface in one file. Forge
#  reads it and produces a bootable image: branding, theme, language,
#  the app set, and what users are allowed to change.
#
#  This is the file a school or an organisation edits. Everything an
#  administrator could reasonably want to control is here, and nothing
#  here requires touching code.
# ═══════════════════════════════════════════════════════════════════

profile_id="desktop"
profile_name="AurOS Desktop"
profile_description="The standard AurOS experience for a personal computer."

# ── Base ───────────────────────────────────────────────────────────
# Ubuntu LTS. Chosen over Debian for one decisive reason: Canonical's
# shim-signed / grub-efi-amd64-signed / linux-image-generic chain is
# already Microsoft-signed, so Secure Boot works with no shim-review and
# no MOK prompt. See docs/research/signing-trust.md.
base_suite="noble"
base_mirror="http://archive.ubuntu.com/ubuntu"
base_components="main restricted universe multiverse"

# ── Identity ───────────────────────────────────────────────────────
brand_name="AurOS"
brand_short="auros"
brand_url="https://auros.example"
os_version="0.1.0"
# D14: Wishes (src/aurwish, the shell's "Make a wish") are NOT in the v0.1
# image. "on" puts them in; build/forge treats anything else as off, and
# tools/wishesoff.sh fails CI if an image that should not have them does.
wishes="off"
os_codename="Nocturne"

# ── Look ───────────────────────────────────────────────────────────
theme="sandstone"        # Light: the default on every surface (DESIGN-DIRECTION)
wallpaper_style=""        # empty = whatever the theme says

# ── How the computer works (docs/SHELLS.md) ────────────────────────
# The single most consequential line in this file. It decides how a
# person reaches a different thing, which is what actually makes a
# computer feel easy or hostile. Colours are cosmetic next to it.
#
#   rail       everything open sits in a row; nothing can hide
#   tiles      a page of big buttons; one thing fills the screen
#   locked     only the apps the owner chose; nothing else exists
#   taskbar    a bar of open windows along the bottom; they overlap
#   dock       favourites always in the same place along the edge
#   workbench  windows divide the screen; keyboard-driven
#
# All six are built into every image. This picks the default; a user
# can try another with `aurshell --shell`, and an administrator pins
# it by leaving allow_settings_change="no".
shell_archetype="rail"

# ── Locale ─────────────────────────────────────────────────────────
locale="en_US.UTF-8"
extra_locales=""          # e.g. "fr_FR.UTF-8 zh_CN.UTF-8"
timezone="UTC"
keyboard_layout="us"
keyboard_variant=""

# ── Packages ───────────────────────────────────────────────────────
# Hardware enablement. This is the line that makes WiFi and graphics
# work on a ten-year-old laptop, and it is the single strongest reason
# to sit on an existing base rather than rebuild one.
#
# polkitd decides whether the desktop may turn the computer off, open a
# USB stick, or change the network. network-manager already depends on
# it, so it arrives either way -- it is named here so that it is
# MANUALLY installed and survives the autoremove the build runs, and so
# that a profile which drops network-manager does not silently lose the
# other two as well. Without it those controls appear, press, and
# refuse: controls that look like controls and are not.
# bluez is the Bluetooth stack. It is here because a laptop's mouse,
# its headphones and its keyboard are increasingly all Bluetooth, and a
# machine that cannot pair one is a machine somebody has to find a
# cable for.
#
# cups and its discovery are printing. A person who has had a printer
# for fifteen years expects to press Print. avahi is what finds a
# network printer without being told its address, which is the only
# kind of printing setup she will complete unaided; printer-driver-*
# are the drivers for printers that do not speak a standard language,
# and they are the difference between "it printed" and "it printed
# nothing and said nothing".
#
# FIRMWARE, BY THE PIECE. `linux-firmware` in noble-updates is a
# metapackage over eighteen pieces, and four of them are for machines
# that are not in anybody's house: switch chips (mellanox-spectrum,
# marvell-prestera) and server network cards (netronome, qlogic) --
# about 170 MB of a download that is the floor of her ten minutes.
# linux-firmware-minimal is the empty package that stands in for the
# metapackage (it Provides linux-firmware, which linux-image-generic
# depends on), so the pieces a home PC uses are named one by one.
# build/forge removes the four if anything brought them back anyway,
# and refuses to remove them if that would take the kernel with it.
#
# firmware-sof-signed is the sound on most Intel laptops since about
# 2019 (Sound Open Firmware): without it the speakers and the built-in
# microphone simply do not exist. It is only a Recommends of the
# metapackage, and this build installs no Recommends -- so every image
# before this one shipped with silent laptops. 12 MB. alsa-ucm-conf and
# alsa-topology-conf are the other half: the per-machine descriptions
# that tell the sound stack which SOF output is the speakers and which
# input is the microphone. Under 1 MB together.
# ipp-usb: a printer bought since about 2015 speaks IPP over its USB
# cable (the same "driverless" printing a network printer does), and
# without this daemon the kernel sees a printer and CUPS has nothing to
# print through. It listens on the loopback address only and starts when
# such a printer is plugged in (a udev rule). CUPS finds the printer it
# serves through the local DNS-SD record ipp-usb registers with
# avahi-daemon -- which build/harden's disable-publishing=yes blocks
# (the first check on a booted image: plug an IPP-USB printer in and see
# it in Printers; docs/handoff/streams/C.md).
# libnss-mdns: a network printer is found by a name like printer.local, and
# only this package lets anything on the computer turn such a name into an
# address (the "mdns4_minimal [NOTFOUND=return]" word in the hosts line of
# /etc/nsswitch.conf, which build/forge checks). Without it the printer is
# listed and "Print" says nothing and prints nothing (PARITY, 17:50:
# printer.local unresolvable). 0.1 MB; avahi-daemon, above, answers for it.
packages_hardware="linux-firmware-minimal linux-image-generic
                   linux-firmware-amd-graphics linux-firmware-amd-misc
                   linux-firmware-intel-graphics linux-firmware-intel-misc
                   linux-firmware-intel-wireless
                   linux-firmware-nvidia-graphics
                   linux-firmware-qualcomm-graphics linux-firmware-qualcomm-misc
                   linux-firmware-qualcomm-wireless
                   linux-firmware-broadcom-wireless linux-firmware-marvell-wireless
                   linux-firmware-mediatek linux-firmware-realtek
                   linux-firmware-misc
                   firmware-sof-signed alsa-ucm-conf alsa-topology-conf
                   network-manager wireless-tools wpasupplicant
                   polkitd
                   bluez
                   cups cups-filters avahi-daemon avahi-utils libnss-mdns
                   ipp-usb
                   printer-driver-gutenprint printer-driver-hpcups
                   cups-pk-helper system-config-printer
                   pciutils usbutils"

# Boot chain. Signed, so Secure Boot does not have to be disabled.
# python3-cryptography: auros-bootchain checks a new shim's signatures
# the way firmware does before it installs one (sbverify trusts the
# certificates a signature carries, so it cannot be the check).
packages_boot="shim-signed grub-efi-amd64-signed grub-efi-amd64 python3-cryptography
               efibootmgr os-prober"

# Minimum userland for a system that can repair and explain itself.
#
# unattended-upgrades: THE UPDATE CHANNEL, for everything that comes
# from Ubuntu. Security updates from noble-security, installed by
# themselves once a day (rootfs/etc/apt/apt.conf.d/20auto-upgrades).
# The image already listed the security archive and nothing ever read
# it, so a machine installed today would have stayed exactly as it was
# built. AurOS's own programs are built into the image, not packaged,
# and are not covered: docs/issues/v3/07-images-and-build.md, 7.4.
#
# apparmor: named so it is MANUALLY installed. It used to arrive only
# as a dependency of snapd, which the build now keeps out -- and the
# autoremove in stage 3 would then take it too. It is Ubuntu's normal
# confinement (cupsd has a profile), and it is the configuration every
# Ubuntu 24.04 machine running Flathub programs and Firefox has: its
# sysctl restricts unprivileged user namespaces and its profiles for
# /usr/bin/flatpak and firefox are the exceptions their sandboxes use.
# Keeping it means this image is that tested configuration and not a
# third one nobody runs. 3 MB.
packages_base="systemd-sysv dbus udev sudo less nano apparmor
                   initramfs-tools
               ca-certificates curl wget
               e2fsprogs dosfstools ntfs-3g gdisk parted
                   exfatprogs
                   cloud-guest-utils util-linux
               zstd xz-utils bzip2
               iputils-ping openssh-client
               fonts-atkinson-hyperlegible fonts-jetbrains-mono
               fonts-paratype
               unattended-upgrades"

# ── Security and reliability (docs/security/HARDENING.md) ─────────
#
#   nftables     the firewall: nothing on the network can start a
#                conversation with this computer (rootfs/etc/nftables.conf)
#   zram-tools   compressed swap in RAM, so 4 GB behaves like ~6
#   earlyoom     when memory runs out anyway, the biggest program is
#                stopped while the machine still answers, never the desktop
#   chrony       the clock. Without a time daemon the clock was the PC's
#                hardware clock read in the wrong zone (Windows keeps it
#                in local time): hours out, apt refusing every update as
#                "not valid yet". chrony, unlike systemd-timesyncd, can be
#                told never to write that hardware clock back (no rtcsync:
#                rootfs/etc/chrony/chrony.conf), so Windows' clock stays as
#                it was; it also speaks NTS, authenticated time.
#
# A few MB together. cups-browsed is NOT in this image (build/harden
# removes it whichever profile asks): it was the network listener of the
# CVE-2024-47176 printing attack, and printing works without it.
packages_security="nftables zram-tools earlyoom chrony"
zram_percent="50"
zram_algorithm="zstd"
earlyoom_min_free_percent="5"

# The desktop. Kept deliberately small; aurshell provides the shell.
# What an application needs before it can put a window on the screen.
# Every line here was added because something concrete failed without
# it, not because it seemed likely to be useful.
#
#   dbus-user-session   the first hard failure: a real browser will not
#                       start at all without a session bus, and says so
#                       as "Cannot autolaunch D-Bus without X11 $DISPLAY"
#                       -- an error about X11 on a machine that has none
#   fonts-*             an application draws its own text; the shell's
#                       fonts are the shell's
#   ca-certificates     otherwise every https page is a security error
#   libgl1-mesa-dri     llvmpipe. The compositor is CPU-only by design,
#                       but a toolkit that cannot create ANY GL context
#                       aborts rather than falling back
#   *-icon-theme,       GTK and Qt applications look broken, or refuse
#   gsettings-*,        to start, without their schemas, icons and mime
#   shared-mime-info    database. This is not decoration.
#   librsvg2-common     the gdk-pixbuf loader for SVG. Every symbolic icon in
#                       Adwaita is an SVG, and adwaita-icon-theme only
#                       RECOMMENDS this (the build installs without
#                       recommends), so without it every GTK program drew
#                       the "image missing" glyph where GNOME Software has its
#                       five stars and its Flathub source (F's real-Windows
#                       store run, 40038619603; reproduced in WL's harness:
#                       stars with it, the glyph without).
#
# mesa-vulkan-drivers was removed rather than lost: it is ~100 MB, and
# nothing in this product can use it. The compositor composites on the
# CPU by design, and the machines this exists to rescue have no Vulkan
# driver worth loading.
packages_desktop="xwayland libinput10 libdrm2 libgbm1
                  pipewire pipewire-pulse wireplumber
                  fontconfig fonts-dejavu-core fonts-liberation2
                  dbus-user-session dbus-daemon ca-certificates
                  libgl1-mesa-dri libegl-mesa0 libglx-mesa0
                  adwaita-icon-theme librsvg2-common hicolor-icon-theme
                  gsettings-desktop-schemas shared-mime-info xdg-utils
                  fonts-noto-color-emoji"

# ── Letters this computer can draw ─────────────────────────────────
#
# A font is not decoration. A page in a language the machine has no
# font for is a page of empty boxes, and this product's own README
# promises that a school can ship it in its own language.
#
# fonts-noto-color-emoji is in packages_desktop above and not optional:
# every web page, every message and half the application menus on the
# internet now contain emoji, and without it they are boxes.
#
# The CJK set is 91 MB, which is a real decision rather than an
# oversight, so it is a knob. Chinese, Japanese and Korean are a
# quarter of the people alive; an image built for a school that does
# not need them can say so, and an image built without thinking about
# it gets them.
#   yes    fonts-noto-cjk        91 MB, covers CJK at one weight set
#   extra  + fonts-noto-cjk-extra  a further 214 MB, all weights
#   no     boxes, in those languages
fonts_cjk="yes"

# What the user actually opens.
#
# Ubuntu ships Firefox and Chromium ONLY as snap transitional packages,
# so purging snapd (below) removes the browser with them. A desktop
# without a browser is not a product, so the browser comes from a real
# apt repository instead.
#
#   mozilla-apt  Mozilla's official .deb repo (packages.mozilla.org)
#   archive      a real .deb from Ubuntu's own archive
#   snap         Ubuntu's default; requires snapd, slow on old hardware
#   none         headless / appliance builds
#
# browser_fallback is used when the preferred source cannot be reached
# from the build host -- a corporate proxy, an air-gapped builder, a
# blocked domain. Shipping a desktop with no browser because of the
# network the BUILDER was on is not a trade-off anyone chose, and the
# fallback is a real browser rather than an apology.
browser="firefox"
# IN ORDER, AND EVERY ONE OF THEM IS A DIFFERENT HOST TO BLOCK.
# packages.mozilla.org first; the mozillateam PPA next, which is
# the same binaries built for Ubuntu and is reachable on networks
# that block the first; then the release tarball, checked against
# the SHA256SUMS published beside it, which needs no repository at
# all. Ubuntu's own archive is deliberately NOT in this list: its
# `firefox` is a 120 KB package whose job is to run snap.
browser_source="mozilla-apt mozilla-ppa mozilla-tarball"
# Pinned, because the tarball route has no repository to ask what
# the current release is and an unpinned download is not a
# reproducible build.
firefox_version="140.0"
browser_fallback="epiphany-browser"

# ── Opening things, and getting more things ────────────────────────
#
# Software arrives here the way it arrives on any other Linux machine:
# from the archive, or as a .deb downloaded from a website. There is no
# AurOS-only walled garden, and there is no attempt to run .exe files --
# a .exe is a Windows program and nothing on Linux runs one. What IS
# here is the normal path, made pressable:
#
#   thunar     her files, in a window. Double-clicking something opens
#              it with whatever claims that kind of file, which is what
#              makes "make files openable" true rather than a plan.
#   (no gdebi: a .deb she downloads opens "Get more programs" instead,
#              with a card saying programs come from there. Installing
#              one as root needed a grant that made any program she ran
#              root; it waits for the PIN, docs/security/THEFT.md.)
#   ristretto  pictures      evince    PDFs
#
#              evince and not atril, which was here: atril draws EPUB
#              through its own copy of WebKit, and that copy (WebKit 4.1
#              and its JavaScriptCore) was 144 MB of the image -- a
#              second browser engine, for a book format she is unlikely
#              ever to open, on a machine whose browser has its own.
#              evince reads PDF, PostScript, DjVu, XPS and comics with
#              the same poppler; it adds 4.5 MB.
#   mousepad   text          xarchiver zip files
#   mpv        music and video. One binary, every format anyone sends
#              her, and it plays a CD-quality file on a 2013 laptop
#              without dropping frames. A computer that cannot play the
#              song her granddaughter emailed her is not "usable for
#              anything"; it was not usable for that.
#   mate-calc  a calculator. 488 KB, and its absence is the kind of
#              thing nobody lists as a requirement and everybody
#              notices on the first afternoon.
#
# The viewers are the light ones (XFCE and MATE) rather than the GNOME
# ones: ristretto is 8 packages where eog is 14, and this is a machine
# from 2013.
#
#   libreoffice-writer, -calc   Word and Excel files, and her own letters
#              and tables (decision 14: Writer and Calc only; no Impress,
#              Draw or Base, no Java). Debs from Ubuntu's main archive, so
#              they get its security fixes. -gtk3 is the plug-in that makes
#              them draw like every other program here (without it they
#              fall back to a bare toolkit); carlito and caladea are the
#              fonts a Word document says it was written in (metric
#              copies of Calibri and Cambria), so a letter from a Windows
#              friend keeps its line breaks. The cost on top of what is
#              already here: about 75 packages, 344 MB installed, 103 MB
#              to download (apt simulation on noble, Installed-Size).
#   heif-gdk-pixbuf, libheif-plugin-libde265   a photo from an iPhone
#              (.heic) opens in Photos instead of being a blank file.
#   libglib2.0-bin   `gio`, which our xdg-open hands every "open this" to (so
#              a program's request goes where a double-click would; named
#              here so that it is installed on purpose, not by luck).
packages_files="thunar thunar-volman gvfs gvfs-backends udisks2
                ristretto evince mousepad xarchiver mpv mate-calc
                libreoffice-writer libreoffice-calc libreoffice-gtk3
                fonts-crosextra-carlito fonts-crosextra-caladea
                heif-gdk-pixbuf libheif-plugin-libde265 libglib2.0-bin
                shared-mime-info desktop-file-utils xdg-user-dirs"

# The browse-and-install store. Heavier than everything above put
# together -- 35 packages and a background service -- so it is a knob
# rather than an assumption, and a build for a slow machine can drop it
# and still install software by downloading it.
#   gnome-software  the usual Ubuntu store, backed by packagekit
#   none            the archive is still there; .deb files still install
software_store="gnome-software"

packages_apps=""

packages_extra=""
packages_exclude="snapd ubuntu-advantage-tools popularity-contest"

# ── Policy / lockdown (the schools + organisations story) ──────────
kiosk_mode="no"           # yes = single-app, no desktop, no shell access
allowed_apps=""           # empty = everything installed is allowed
blocked_apps=""
# May users install software?
#
# "yes" is what makes "Get more programs" work at all: every install,
# remove and update the store performs demands an administrator password
# by default, and this product has no password prompt to show her and
# no password to give her.
#
# Saying yes means the person at this computer can install and remove
# software without being asked for anything. On a personal machine that
# person is the owner, and it is the trust Windows already extends to a
# sole administrator. On a shared or managed machine, say no -- and
# then nothing is granted and both paths correctly refuse.
allow_user_install="yes"
allow_settings_change="yes"
allow_theme_change="yes"
# May the person using this machine choose its wifi?
#
# "yes" puts an Internet button in the band at the bottom of every
# screen, which lists the wifi in range and joins one, and grants the
# desktop permission to change the network. With "no", neither exists
# and the network is whatever the image or the cable says it is.
#
# Anything that is not a plain no -- "false", "0", "off" -- is read as
# no; anything else is read as yes. The build normalises it once and
# both the button and the permission come from that one reading, so the
# two can no longer disagree.
#
# On a personal computer this is obviously yes -- it is her house and
# her router, and the alternative is a machine that cannot get online
# without somebody who knows what a terminal is. On a school laptop or
# a machine on a counter it is the owner's decision and not the user's.
# Kiosk builds never get the button regardless.
allow_network_change="yes"
allow_tty="no"            # no console login: nobody here has a password to type

# After this many minutes with nobody touching it, the screen goes
# dark. 0 means never; anything above 120 is clamped to it.
#
# The panel is most of what a 2013 laptop spends its charge on, so
# this is the single biggest thing between this product and a machine
# that lasts an afternoon. It is also the whole of privacy on a
# computer with no lock screen: one left on a kitchen table should not
# go on showing her bank statement to the room.
#
# Any key, any click, any touch brings it back, and that first press
# does nothing else -- it must not type, and it must not press
# whatever happens to be under a pointer she cannot see.
screen_off_minutes="10"
auto_login="no"
default_user="auros"

# ── Fleet ──────────────────────────────────────────────────────────
enrollment_url=""         # optional MDM / fleet check-in endpoint
update_channel="stable"
telemetry="off"           # off | anonymous — never on by default
