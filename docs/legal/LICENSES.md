# Licenses and source code

Version 2, 24 October 2026. Written by the person who makes AurOS, with the help of AI software, and without a lawyer. This page is not legal advice.

AurOS's own programs are free software under the MIT license. AurOS also includes programs made by other people, including the Linux system underneath, Firefox and many programs from Ubuntu. Each one has its own license. You are free to use and share almost all of them, and most can be changed too. Firmware from hardware makers is under its makers' own terms. This page says where the licenses are and how to get the source code.

## Get the source code

Every AurOS release has a second release beside it on [the releases page](RELEASES_URL), named "sources-" followed by the name of the AurOS release. For example, the source for the release v0.1.0-618726b is the release sources-v0.1.0-618726b. It holds the Ubuntu source packages, in the exact versions that are in AurOS, as files you can download. Its file SOURCES.txt lists every program and where its source is. Firefox is made by Mozilla; SOURCES.txt gives the address where Mozilla publishes its source. AurOS's own code is under the MIT license and is in the same release. The programs inside the Windows installer AurOS-Setup.exe (the Linux kernel, the boot loader and the small system that runs after the restart) are covered by the same release.

If this computer has the file /etc/auros/release, it names the release. If not, type "dpkg -l auros" in a terminal: the version shown is also written in the file PACKAGES.txt of the sources release that matches. If you are not sure, write to us and say what you can see.

We keep each sources release for at least three years after the last day we offer the matching AurOS release for download, and for as long as we support it. We do not delete them before then.

**If you cannot get the source that way,** write to CONTACT_EMAIL and say which release you have. We will send you the source on a USB stick or a disc, for no more than the cost of the medium and the postage. This offer is open to anyone, whether or not they have a copy of AurOS. Programs your computer later updates from Ubuntu or from Mozilla come with source from them, at their own addresses.

Nothing here limits the rights that the licenses of these programs give you. If these words and a license disagree, the license wins.

## Which licenses

| What | License (mostly) |
|---|---|
| AurOS's own programs: the desktop, AurOS Setup, the tools | MIT. The text is in the file LICENSE in the source code and in /usr/share/doc/auros/LICENSE |
| The Linux kernel | GNU GPL version 2, with the Linux syscall note |
| GRUB, the start-up menu | GNU GPL version 3 or later |
| shim, which lets Secure Boot start AurOS | BSD 2-clause |
| The C library (glibc), and many other libraries | GNU LGPL version 2.1 or later |
| bash | GNU GPL version 3 or later (other tools are GPL version 2 or 3, or LGPL) |
| systemd | GNU LGPL version 2.1 or later, some parts GPL version 2 |
| NTFS tools (ntfs-3g) | GNU GPL version 2 or later |
| File system tools (e2fsprogs) | GNU GPL version 2; its libraries are LGPL, one part is BSD |
| Firefox | Mozilla Public License 2.0 |
| The typeface, Atkinson Hyperlegible Next | SIL Open Font License 1.1 |
| The fallback typeface, DejaVu | Bitstream Vera license, with public-domain changes |
| Parts of the screen software (the Wayland protocol files) | MIT-style and similar permissive licenses |
| Hardware firmware (Wi-Fi, graphics, sound and the like) | Each maker's own license |

"Mostly" because a program can contain parts under other licenses. The exact licenses of each program are in its own copyright file.

## On your computer

- Each program's license is in /usr/share/doc/ in a folder named for the program, in the file named "copyright". The full texts of the common licenses are in /usr/share/common-licenses.
- AurOS's own notices are in /usr/share/doc/auros: LICENSE, SOURCE-OFFER and THIRD-PARTY-NOTICES.
- Firmware from hardware makers has its makers' licenses, in the copyright files of the programs whose names start with linux-firmware, under /usr/share/doc.
- Programs you add through **Get more programs** come from Flathub. Each carries its own license, and its page shows it.

## The Windows installer

AurOS-Setup.exe contains the Linux kernel, GRUB and shim, and a small system that runs after the restart, which includes programs under the GPL and LGPL. The license texts are inside AurOS-Setup.exe (the Licenses button) and in the file NOTICES.txt of every sources release, and the source is in the same sources release. The small system's own programs are linked to the C library in a way that lets you replace the library; the file docs/RELINK.md in the source code says how.

## Video, music and photos

AurOS includes programs that play video, music and photos. Some ways of doing that are covered by patents in some countries. The MIT license gives no patent license.

## Names

AurOS includes software from Ubuntu 24.04 LTS, put together and changed by its maker. It is not made, endorsed or supported by Canonical.

Windows, Microsoft, Edge, BitLocker, Defender and SmartScreen are trademarks of the Microsoft group of companies. Mac and macOS are trademarks of Apple Inc. Google Chrome and Safe Browsing are trademarks of Google LLC. Ubuntu is a registered trademark of Canonical Ltd. Firefox is a trademark of the Mozilla Foundation. Linux is the registered trademark of Linus Torvalds. Intel is a trademark of Intel Corporation. Other names are trademarks of their owners. AurOS is not made, approved or supported by any of them, and has not been authorized, sponsored, or otherwise approved by Apple Inc. The names are used only to say what something is made from, resembles or works with.
