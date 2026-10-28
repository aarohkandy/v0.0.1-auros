# Relinking the installer's small system with another C library

Prepared by the AurOS team without a lawyer. This page is for the license of the GNU C library
(LGPL 2.1 or later), which asks that anyone can replace the library in a program that is linked
to it and link the program again (LIC-3 in `docs/legal/COMPLIANCE.md`).

**What is linked statically.** `AurOS-Setup.exe` carries a small Linux system that runs after the
restart (`build/staging`). Two of its programs, `init` (`src/aurstage/*.c`) and `aurscreen`
(`src/aurscreen/screen.c` and the files named below), are our own code (MIT) linked **statically**
to the GNU C library of the machine that builds them (`gcc -static-pie`, so `libc.a` of the Ubuntu
package `libc6-dev`). Everything else in that system (kmod, ntfs-3g, e2fsprogs and their libraries)
is Ubuntu's own dynamically linked build, copied unchanged from the image.

**What you need.** Ubuntu 24.04, this source code (the tag or commit named in the release notes,
and `auros-source-*.tar.gz` of the `sources-<tag>` release), and the C library you want to link:
for Ubuntu's, `apt-get source glibc` gives the source (the same version is in the sources release),
`apt-get install libc6-dev` gives the static library; for your own build, your own `libc.a`.

**The commands** (they are the two `gcc` lines of `build/staging`; `build/cflags` defines the flags):

```sh
. build/cflags
mkdir -p work/relink && cd work/relink
gcc $HARDEN_STATIC -std=gnu11 -Wall -Wextra -o init ../../src/aurstage/*.c -I ../../src/aurstage
gcc $HARDEN_STATIC -std=gnu11 -Wall -Wextra -o aurscreen \
    ../../src/aurscreen/screen.c ../../src/aurshell/kms.c ../../src/aurshell/draw.c \
    ../../src/common/font.c ../../src/common/png.c -lm
```

If your `libc.a` is not the system's, add `-nostdlib` and your own startup files and `-L` as your C
library needs. Then build the whole small system and the installer with the new programs:

```sh
sudo ./build/forge build desktop     # the image the small system takes its other programs from
sudo ./build/staging desktop         # the small system, with the two programs just built from this tree
sh build/aurbridge                   # AurOS-Setup.exe with that small system inside it
```

(`build/staging` compiles `init` and `aurscreen` itself, with whatever `libc.a` is installed, so
installing your C library's static archive before running it is all the relinking needs.)

The checks `build/staging` runs on the result (`file` says "static-pie", `tools/checksec.sh`)
still apply. Questions: the contact address on the Licenses page.
