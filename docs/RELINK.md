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

## Design note: AurOS missing from the start list (not built)

Written here because the lane brief named this file; it is not about relinking. Move it if a better home appears.

**The problem.** After a Windows or firmware update AurOS can fall out of the start list, or Windows Boot
Manager can go back to first place. She then lands in Windows and thinks AurOS is gone.

**What already repairs it (inside AurOS).** `aurfirst hold`, run by `auros-keeper` once the desktop has been up
30 s, re-promotes AurOS to first place when she has confirmed ("S-24", `af_hold` in `src/aurfirst/state.c`).
Unconfirmed, it re-arms the one-shot `BootNext` instead. Declined: never. This helps whenever AurOS is
reached at all (firmware menu, F12). It cannot help when AurOS never starts, and it does not re-create a
deleted `Boot####` entry (`promote` only reorders entries that exist).

**What nothing repairs.** The Windows side. The proposal was an `AurOS-Setup.exe` page on a PC that already has
AurOS ("AurOS is still on this computer. Start it again") that re-creates the entry and writes `BootOrder`.

**Why it is not built in this lane.**

1. *There is no Windows-side `BootOrder` writer to reuse.* The install writes `BootNext` only
   (`plat_boot_next`, `src/aurbridge/plat.h`; `ab_arm` in `phases.c`). `docs/AURBRIDGE.md` says the Windows half
   deliberately has no function that writes `BootOrder`, "the way a rule like this gets broken is somebody finding
   a function that already does the thing". The only `BootOrder` writer in the tree is `promote()` in
   `src/aurfirst/state.c`, which is Linux code (efivarfs), carries the ordering rules (keep every entry, hidden or
   non-boot ones behind, read back, roll back on mismatch), and has its own tests. A second writer in `plat_win.c`
   would have to repeat all of that against `SetFirmwareEnvironmentVariable`, and would break the rule above. (The
   "second gate" in `docs/AURBRIDGE.md` guards `src/aurstage/nvram.c` in the Linux staging build, not the Windows
   half; it is not what stops a Windows-side writer, the written rule and review are.)
2. *Nothing on the Windows side records "confirmed".* The answer is stamped in `/var/lib/auros` on the AurOS
   partition (`AF_CONFIRMED`), which Windows cannot read. Windows could see the partition by its GPT name and
   `\EFI\AurOS\shimx64.efi`, but not whether she said yes, nor a pending put-back. The brief asks to refuse on
   exactly those, so the check would be a guess.
3. *It is the risk the mission ranks first.* A wrong `BootOrder` or `Boot####` can leave a PC that starts nothing,
   on firmware whose setup screen she has never seen. This cannot be tried here: there is no physical PC, and the
   simulation (`plat_sim.c`) models our own reading of the spec, not a vendor's firmware.

**Smallest safe design, for whoever builds it.**

- Detect: exactly one GPT partition named `AUROS-BOOT` or `AUROS-ESP` on a disk, exactly one ESP holding
  `\EFI\AurOS\shimx64.efi` on that disk, BitLocker state as at install, no `putback` marker in the ESP's
  `\EFI\AurOS`. Anything else: change nothing, say nothing about repair, open the normal installer.
- Make the AurOS side leave the Windows side something to read: a small file on the ESP (`\EFI\AurOS\state`)
  written by `aurfirst confirm` and removed by `decline` and by Put Windows back. Without it, point 2 stands.
- Entry: reuse `plat_boot_find` / `plat_boot_make` (description `AurOS`, `shimx64.efi`, the ESP found above), and
  only when no existing entry already names that partition (the "which AurOS" rule in `docs/AURBRIDGE.md`).
- Order: do not write it from C on Windows. Prefer `BootNext` once (existing code, self-clearing) and let
  `aurfirst hold` do the permanent `promote()` after AurOS has run. If a Windows `BootOrder` write is ever
  chosen instead, it needs the same ordering rules and read-back as `promote()`, its own mutation-proved tests
  in `plat_sim`, and a run on at least one real PC.
- Tests: the four cases of the brief (entry missing, present but not first, ambiguous, no AurOS) in
  `pf_sim.c`/`simmain.c`, each proved red by mutation.

**What was done instead.** A paragraph on `website/guide.html` (the printed sheet) for "The computer starts
Windows instead of AurOS?": same menu key as for starting Windows, then choose the one that says AurOS; "Nothing is
lost." It makes no promise that AurOS puts itself first again, and gives no mailbox. It does not say "open AurOS Setup again", because that page does not exist.
