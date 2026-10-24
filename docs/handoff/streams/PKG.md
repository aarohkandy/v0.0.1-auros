# Stream PKG: what the no-Recommends image silently lost

**Goal.** Put back the packages docs/reviews/RECOMMENDS-2026-10-24.md proved she would notice
without, each with a check that is red without it.

**Done.** In `profiles/desktop.profile` (office and revive inherit it), one comment line each:
packages_hardware: system-config-printer-udev, dmsetup. packages_desktop: libspa-0.2-bluetooth,
rtkit. packages_files: tumbler (NOT tumbler-plugins-extra, which pulls WebKit), unzip, zip, eject,
webp-pixbuf-loader, libreoffice-math, hunspell-en-us, hyphen-en-us, mythes-en-us, libmtp-runtime.
None is excluded or pinned in build/forge, build/harden or tools/updatepolicytest.sh (checked).
`build/forge` (shared file, 8 lines after the rename table): hides the LibreOffice Math launcher
with a NoDisplay override in /usr/local/share/applications, so the program list does not grow.

**Checks** (`tools/everydaytest.sh`, run by tests.yml "everydaytest" against the built rootfs):
- profile half: the 14 names must be in the profile; tumbler-plugins-extra must not be.
- image half, files: libspa-bluez5.so, 70-printers.rules, eject, 55-dm and 95-dm-notify rules,
  rtkit D-Bus service and daemon, hunspell en_US .dic/.aff, hyphen, mythes, mtp-probe,
  LibreOffice Math, WebP in loaders.cache, Math launcher hidden.
- image half, by doing it (needs root/sudo for chroot, as the existing file-type checks do):
  session bus activates org.freedesktop.thumbnails.Thumbnailer1 and it lists image/jpeg and
  application/pdf; libspa-bluez5.so has no missing libraries (ldd); zip makes and unzip lists
  a zip; an .odt with a formula object saved as .docx contains Word's `<m:oMath>` equation.

**Not done / honest limits.** xarchiver itself is not run (GTK needs a display; `--info` and
`--extract-to` both exit "Cannot open display"); the check runs the programs it shells out to.
The formula check uses .docx output, not PDF: PDF export succeeds without Math, with a gap.
Bluetooth sound itself and the eject action are not exercised (need hardware).

**Proof.** homebase, docker: image A = ubuntu:24.04 + the profile's lists before the change
(840 packages, --no-install-recommends, plus store/flatpak/portal); image B = A + the new lists.
Same test, `RFS=<copy of the filesystem without /proc and /dev>` in a privileged container:
A fails every new check except the hunspell dictionary (present by accident; hyphen and mythes
fail; the profile half fails all 14); B passes all of them. The 6 other failures in both are
things only a forge-built image has (xdg-open divert, flatpak override, mimeapps) and the
2 macOS-only `stage_mdns` failures (BSD sed) are older than this change.
Cost, apt simulation on noble: 22 new packages, +28.9 MB installed, 5.1 MB download.
mythes-en-us alone is 21.5 MB of that (the thesaurus); drop it if the size matters more.
