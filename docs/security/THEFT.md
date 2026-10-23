# Theft: an encrypted disk and a short PIN (owner decision D9)

Status: **built; proven on a file against a software TPM
(`tools/theftunit.sh`) and on the real image in KVM with Secure Boot and a
TPM 2.0 (`tools/theftvm.sh`, `docs/results/theft/`); SWITCHED OFF in the
default install** until the owner says yes (the commits are held by the
permission system; it is on his morning list). Owner of this file and of
the code it names: stream S4 (`docs/handoff/STREAMS.md`). Implementation
status, measurements and next steps: `docs/handoff/streams/S4.md`. Where
the build differs from the first design, the text below was changed to
match and the change is marked "(measured)".

## Decisions (lead, 2026-10-23)

1. **No PCR binding.** The key is sealed in the TPM with her PIN as the
   TPM object's authorisation value and **no PCR policy**: the TPM's own
   dictionary-attack lockout is the protection. A firmware or dbx update,
   a new shim, a Secure Boot change therefore never asks her for the
   rescue code (D9: she is never locked out, never confused). Section 3
   says why this costs nothing against a thief. `lockctl prepare` writes
   `pcrs=` (empty); `THEFT_PCRS=7` / `AUROS_LOCK_PCRS=7` still seal to PCR 7
   and that path stays built and tested, for a reader who wants it.
2. **She chooses her PIN only after the encryption pass completes**
   (5 to 30 minutes), not before: LUKS2 refuses keyslot changes while the
   pass runs (section 1). Until then the drive opens without a PIN and
   nothing of hers is copied onto it (section 1).

## What it is for, in one sentence

If her laptop is stolen -- switched off, or asleep with the lid shut --
the thief gets a computer, not her photos, her letters, her bank
statements or her Wi-Fi password.

## What she sees

| When | What she sees | Words (EASY.md rules) |
|---|---|---|
| Install, first start | nothing new | -- |
| After "Yes, keep AurOS" | **Choose a PIN**: a big number pad, 4 to 6 numbers, typed twice | "Choose a PIN. You will type it when the computer starts and when it wakes up. Pick 4 to 6 numbers you will remember." |
| Straight after | **Your rescue code**: 8 groups of 8 letters, shown once, with *Print it* and *I have written it down* | "If you ever forget your PIN, this code opens your computer. Write it down or print it, and keep it at home, away from the computer. Nobody else has a copy -- not us either." |
| Every start | a full-screen AurOS page with the number pad (not a console, not grub) | "Type your PIN to start AurOS." Wrong: "That PIN didn't work. Try again." Many wrong: "Too many tries. Wait a few minutes, or use your rescue code." |
| Waking up, or after the screen went dark | the same number pad, over a blurred desktop | "Type your PIN to carry on." |
| Something about the computer changed (firmware, a security setting) | the rescue-code page | "This computer has changed since you last used it, so it needs your rescue code this once." then "Now type your PIN" -- the next start is back to the PIN |
| A PC with no security chip (no TPM 2.0) | the same PIN pages, plus one honest line at "Choose a PIN" | "This computer has no security chip, so choose 6 numbers. Your PIN keeps out a thief, but not an expert with days to spare." |

Nothing here is time-limited or a gesture (EASY rule 5); the number keys
work whatever the keyboard layout (they are read as physical keys), and
the on-screen pad is pressable.

## The design

### 1. The root is LUKS2, encrypted in place, in the background

The AurOS root partition (`AUROS-ROOT`) becomes LUKS2 **after** the
image is on the disk, by `cryptsetup reencrypt --encrypt
--reduce-device-size 32M` (LUKS2 online encryption with a data shift),
the way Windows' device encryption encrypts a working PC: she uses the
computer while it happens, power cuts are survived (LUKS2 reencryption
keeps a resilience journal), and an interrupted pass resumes at the next
start.

Why after, not while copying: the install has two paths. The legacy one
copies the image in staging; the fast path (D1) has *Windows* write the
image raw into the gap. Writing ciphertext from Windows would mean a
LUKS2 + AES-XTS implementation in the Windows installer. Encrypting in
place afterwards works identically for both paths and for an image
written straight to a disk, and changes nothing in the engine.

Where it starts:

1. **AurOS's own initramfs** (`rootfs/etc/initramfs-tools/`, our hook +
   `local-top` script), at any start where the root is still a plain
   ext4 and `/etc/auros/lock` does not say "off": `e2fsck -p`,
   `resize2fs` to 32 MiB below the partition's end, `reencrypt --encrypt
   --init-only` with a temporary keyslot, activate `auros-root`, boot.
   `auros-lock-encrypt.service` then runs `cryptsetup reencrypt
   --resume-only` at idle I/O priority until the whole partition is
   ciphertext.
2. **Staging, later (stream A2's hook)**: `src/aurstage/luks.c` does the
   same right after `commit_settle` and before `switch_root`, so
   encryption has begun before AurOS's first start. Until then the
   first start is plain and encryption begins at the second.

**Her files wait for it.** Ferry (her Windows files) only comes after
encryption has begun, so her data is never written to the disk in
plaintext (on an SSD, overwriting in place does not erase the old
cells); writes made while the pass runs go through the encryption.

**The PIN comes after the pass, not before (measured).** LUKS2 refuses
every keyslot and token change while an online encryption pass runs
("Operation incompatible with device marked for LUKS2 reencryption";
cryptsetup 2.7.0), so the PIN cannot be added until the whole partition
is ciphertext: about 5 minutes (SSD) to 30 (HDD) for 40 GB. `lockctl
set-pin` exits 3 until then. The order after "Yes, keep AurOS" is:
(restart once if encryption has not begun: "AurOS will restart once to
lock the drive") -> Ferry may run -> a plain "Locking your drive: 40%"
page she can ignore while she works -> when it is done: Choose a PIN ->
rescue code. The honest cost is a longer window with the temporary key
(next paragraph).

The temporary keyslot is an **empty passphrase** (pbkdf2, minimum
cost): the disk opens with no input until she chooses a PIN, exactly
like Windows' "waiting for activation" state. It is wiped the moment
the PIN is enrolled, and the state is visible (`lockctl status`:
`state=open-no-pin`). Honest cost: a thief who takes the PC *before*
she chooses a PIN gets what is on it (the OS and nothing of hers, since
Ferry waits); one who copies the LUKS header in that window could later
use the volume key. Re-keying needs a second full pass and is not done.

Free space is encrypted too (the whole partition is rewritten), which
also makes the old Windows deleted files that sat in the gap unreadable.

### 2. Boot files live on AurOS's own EFI partition

grub cannot unlock LUKS2 with a TPM, and Canonical's signed grub cannot
read argon2 keyslots. So the kernel, the initramfs, `grub.cfg` and
`grubenv` live on `AUROS-BOOT` (the copy of the image's own ESP, 512
MiB, ~10 MB used today), under `/auros/`. This is the same layout as
systemd-boot's `$BOOT=ESP`. Concretely:

- `build/mkimage` puts the boot set on the image's ESP and the ESP's
  stub `grub.cfg` loads `/auros/grub.cfg` from the ESP itself when it
  exists (falls back to the root's `/boot/grub/grub.cfg` otherwise, so
  an old image or a plain root still starts).
- `/etc/kernel/postinst.d/zz-auros-lock` and
  `/etc/initramfs/post-update.d/zz-auros-lock` copy each new kernel and
  initramfs there (current + previous, ~180 MB worst case), and
  `bootchain menu` writes the menu to both places.
- `answer.sh` (Put Windows back) already takes `AUROS_GRUBENV`; it
  points at the ESP's grubenv.

The initramfs on the ESP is **not signed** (no keys of our own: D7).
That is the evil-maid gap; see "What it does not protect against".

### 3. Unlock: TPM 2.0 + PIN, no PCR (decision 1)

`systemd-cryptenroll --tpm2-device=auto --tpm2-pcrs= --tpm2-with-pin=yes`
(noble's systemd 255). `lockctl prepare` writes `pcrs=` (empty) to
`/etc/auros/lock`; `pcrs=7` would seal to PCR 7 instead. **Decided: no
PCR.** PCR 7 changes whenever the firmware's db/dbx changes or the
shim's signing certificate does, and each time she would need the
rescue code ("this computer has changed"); a signed stick produces the
same PCR 7 anyway (below), so the PIN, counted by the chip, is what
protects her, and PCR 7 would add only "Secure Boot must still be on
with Microsoft's keys". Both settings are proven in the VM
(`THEFT_PCRS=7|none`: 29 and 25 checks, `docs/results/theft/`). The key is sealed inside the
TPM; the PIN is the TPM object's authorisation value, so **every wrong
PIN is counted by the TPM itself** (dictionary-attack lockout), not by
our software. systemd 255 talks to the TPM over an encrypted session,
so sniffing the bus of a discrete TPM does not reveal the key.

Why the PIN carries the weight: PCR 7 records the Secure Boot policy
and *which keys* approved the boot, not *what* booted. Any
Microsoft-and-Canonical-signed system (an Ubuntu live stick) produces
the same PCR 7 and could ask the TPM -- so TPM-only (no PIN) with PCR 7
would be defeated by a stick. With the PIN, the thief must guess it, at
the TPM's pace, on this one machine:

| PIN | TPM lockout as Windows leaves it (32 tries, then 1 per 10 min) | 1 per 2 hours |
|---|---|---|
| 4 numbers | about 35 days on average | about 1.1 years |
| 6 numbers | about 9.5 years on average | about 110 years |

(We cannot change those numbers: Windows owns the TPM's lockout
authorisation. `lockctl status` reads and prints the real ones from the
TPM.) The disk taken out and put in another PC is useless without the
rescue code: the sealed key only exists inside this TPM.

The initramfs prompt is ours: `aurlock-pin` (`src/aurshell/lockpin.c`),
a full-screen KMS painter built like `aurscreen` in her theme's colours
and faces, reading digit keys from evdev (physical keys: any layout)
and unlocking through libcryptsetup's token API
(`crypt_activate_by_token_pin`, dlopened). It has an on-screen number
pad (keys 84 px at 720 lines, 71 px at 1024x600, all over EASY.md's 44)
that works with a pointer or a touch screen, fills the dots from the PIN
length recorded in the drive's `auros-hint` token and submits by itself
at that length, and says the right sentence for each case.
**(measured)** systemd 255's token answers -131 to a wrong PIN, to a
chip that is locked out, and to a chip that is not the one the key was
sealed in, so the page cannot tell them apart from the answer: it reads
the chip's own `TPM2_PT_LOCKOUT_COUNTER` and lockout state before and
after each try (the count rose: wrong PIN; locked out: "Too many tries",
with the chip's own wait; neither: this computer has changed). A PCR
mismatch (PCR 7 bound) is -ENOENT and falls into the same "changed"
case. Without a display it falls back to a console prompt in the same
words.

### 4. PCs without TPM 2.0

Common in the target market (pre-2016 PCs, TPM 1.2, firmware TPM off).
The fallback is honest: the PIN becomes a LUKS2 passphrase (argon2id,
1 GiB, about 2 s per try on her machine), minimum **6** numbers there.
Strength: a casual thief is stopped; an expert copying the disk and
guessing offline on a GPU rig needs hours for 6 numbers (minutes for
4), because nothing counts the tries. The words say so (table above).
Better options later: a longer passphrase for people who want one, and
turning on a firmware TPM that is present but off (many 2014-2017 Intel
PCs have PTT in the firmware menu; Settings could explain it).

### 5. When PCR 7 changes (dbx updates, SBAT, firmware, Secure Boot)

**With the decided default (no PCR) none of this ever happens to her:**
the PIN simply works after any of these (proven: the dbx-update case of
`THEFT_PCRS=none`). This section is the path for a key sealed to PCR 7
(`THEFT_PCRS=7`) and for a cleared or replaced chip, which still needs it.

PCR 7 changes when the firmware's `db`/`dbx` changes (a dbx update via
fwupd or Windows Update on a dual-boot PC), when shim raises the SBAT
level, when Secure Boot is switched, or after some firmware updates.

- **Planned** (an AurOS update that will change it -- shim-signed,
  fwupd's dbx plugin): `lockctl reseal-begin` runs first and adds a
  second TPM+PIN keyslot bound to **no** PCR. The next start unlocks
  with that (same PIN, still counted by the TPM), and
  `lockctl reseal-finish` (run at every start) re-enrols to the new
  PCR 7 and wipes the unbound slot. Cost: for one start, a signed live
  stick plus her PIN could also unlock -- which PCR 7 never stopped.
- **Unplanned** (Windows Update applied a dbx on the Windows side, a
  firmware update; only when the key is bound to PCR 7): the TPM
  refuses, `aurlock-pin` shows "This computer has changed" with the
  rescue-code pad, then asks her to choose her PIN (it may be the same
  one) and hands it to `/run/auros-lock/repin`; `auros-lock-repin.service`
  (`lockctl repin`) seals it to the new PCR 7 before her desktop
  starts. One rescue-code entry, then normal. The same path serves a
  forgotten PIN and a cleared or replaced chip. **Built and proven
  (theftunit, theftvm).** The planned two-phase `reseal-begin` /
  `reseal-finish` (above) is NOT built: it is only needed if the lead
  wants PCR 7 and zero rescue codes on AurOS's own updates.

### 6. The rescue code

**(measured, changed from the first design)** 24 numbers in 6 groups of
4 (about 79 bits, every number equally likely: `lockctl` draws bytes
from `/dev/urandom` and discards 250-255 so `% 10` has no bias), in an
argon2id keyslot. Numbers, not modhex letters, because she types it on
the same number pad as her PIN, and because a number is the one thing
every keyboard and every paper sheet agrees on. Shown once, in the
"Choose a PIN" flow, with *Print it* (when a printer is set up) and *I
have written it down*. **Stored nowhere on the
computer** -- a code kept on the laptop is a code the thief has -- and
nowhere online (no accounts, no Microsoft: D7). A paper sheet at home is
the design; a memory stick is offered as a second copy with the warning
"keep it away from the computer". Settings has *Make a new rescue code*
(needs the PIN; the old code stops working).

### 7. CPUs without AES-NI: Adiantum

`/proc/cpuinfo` without the `aes` flag (Atom, Celeron N, Pentium
before 2010, some VIA) -> `xchacha12,aes-adiantum-plain64` with 4096-
byte sectors (Google's design for exactly these CPUs; the noble kernel
has the modules). With AES-NI -> `aes-xts-plain64`, 512-bit key, 4096-
byte sectors. Chosen at encryption time and recorded in the header.

### 8. The same PIN on wake

**Not built yet.** Her account's password becomes the PIN (set by
`lockctl` as root when she chooses it). aurshell's lock screen (`src/aurshell/lockscreen.c`)
covers the screen on resume and when the screen goes dark after idle,
and checks the PIN through PAM (`/etc/pam.d/aurlock`: `pam_faillock`
deny=5 unlock_time=60, then `pam_unix` via `unix_chkpwd`). The PIN is
also what she types for anything that needs her permission (the lead's
rule: a downloaded installer asks for the PIN and warns).

### 9. Privilege boundary

aurshell runs as her and never runs anything as root. As with
`answer.sh`, it writes a request file in its own 0700 runtime directory
and a root path unit runs `rootfs/usr/lib/auros/lockctl`, which accepts
only `set-pin` (when no PIN exists), `change-pin` (old PIN checked by
PAM first), `new-rescue-code` (PIN checked) -- digits only, 4-6 (6 with
no TPM) -- and deletes the request after reading it. The PIN never goes
on a command line (systemd-cryptenroll reads `$NEWPIN`), never to the
journal, never to the EFI partition.

### 10. How the install writes into it (legacy and fast path)

It does not, and that is the design: **neither install path ever
writes into a LUKS device.** The legacy path (aurstage copies the image
onto AUROS-ROOT from the staging environment) and the fast path
(Windows streams the image raw into the gap, D1) both leave a plain
ext4 on a GPT partition named AUROS-ROOT, exactly as today. Encryption
happens afterwards, in place, by AurOS's own initramfs (or, later, by a
staging hook that runs the same `encrypt-init` after `commit_settle`),
so the engine, the planner, the journal and the power-cut tests do not
change, and "Put Windows back" still needs no key: it deletes the
partition, it never reads it.

What A2 (who owns `src/aurstage`, `build/staging`, the planner) keeps
true, none of it new work today:

- **AUROS-ROOT and AUROS-BOOT are 4 KiB-aligned** (start and size). Then
  the LUKS sector size is 4096 and the 512 branch of `encrypt-init` is
  unreachable on AurOS installs. (Measured: an image partition ending
  off a 4096 boundary made cryptsetup refuse 4096-byte sectors; the
  start-up code now falls back to the disk's logical block size, but the
  fast cipher path wants the aligned layout.)
- **The filesystem stops 32 MiB short of the partition's end** when the
  lock is enabled. `firstboot.sh` grows it to fill the partition today
  (`resize2fs "$root_src"`); with the lock enabled the one-line change is
  `resize2fs "$root_src" "$((partition_kib - 32768))K"`. Then
  `encrypt-init` never has to shrink a filesystem, which is the one step
  of the whole design that a power cut could hurt.
- **The boot set is on AUROS-BOOT** (kernel, initramfs, menu under
  `/auros/`), written by `lockctl prepare` / `boot-sync`; `build/mkimage`
  and `bootchain` keep writing `/boot/grub/grub.cfg` as today and the
  copy follows (`auros-lock-boot.path`).
- **aurfirst's `root_disk()`** looks for the root's partition through
  `/sys/dev/block`; on a device-mapper root it falls back to "the first
  entry called AurOS" (it says so in its state file). It should follow
  the mapper's `slaves/` to the partition.
- **`answer.sh`** (Put Windows back, from inside AurOS) writes
  `next_entry` to `/boot/grub/grubenv`; once the menu is read from the
  EFI partition the file that matters is `/boot/efi/auros/grubenv`
  (`AUROS_GRUBENV` already exists for this).

## What it does not protect against (said plainly, here and in Help)

- **Her Windows.** Until Windows is removed, the Windows drive is as
  protected as it was: BitLocker PCs stay encrypted (D11 pauses, never
  decrypts); others keep her Windows files readable to a thief. Ferry
  copies them into the encrypted AurOS; it does not delete them.
- **The two-visit attack (evil maid).** The initramfs on the EFI
  partition is not signed (D7: no keys). Someone who gets the laptop,
  plants a fake PIN screen, gives it back, and steals it after she has
  typed her PIN, wins. Also: PIN watched over her shoulder.
- **A running, unlocked computer**, and memory attacks on a sleeping
  one (cold boot, DMA from a malicious device): the key is in memory
  while the disk is open. The lock screen protects her session, not
  the RAM.
- **Before she chooses a PIN** (section 1).

## Performance (measured; read the caveat)

Measured in the KVM VM on homebase (2 vCPU, 2 GB, the disk a file in
RAM, a busy shared host), `docs/results/theft/`. **The VM's disk is RAM,
so disk numbers here are upper bounds that mostly measure the CPU cost
of the cipher; a 2013 laptop's SATA SSD or hard disk, and a real
Celeron's no-AES speed, are real-PC measurements still to be made.**

| Cost | Expected | Measured (VM) |
|---|---|---|
| Cipher speed, AES-XTS (CPU with AES instructions), `cryptsetup benchmark` | thousands of MB/s | 2.3-4.3 GiB/s |
| Cipher speed, Adiantum (CPU without AES instructions) | 300-600 MB/s, above a 2013 SSD | 240-560 MiB/s on a host kept busy by others |
| Reading through the encryption vs the raw disk (1 GiB, direct) | under 5 % on a SATA SSD | 0.5-2.8 GB/s vs 3.4-9 GB/s raw: **the VM is CPU-bound here, not disk-bound; not a 2013 number** |
| Writing through the encryption (512 MiB) | | 0.4-2.7 GB/s |
| The unlock itself, TPM + PIN | 0.3-1.5 s | **137-203 ms** (KVM, swtpm) |
| The unlock, no TPM (argon2id, 6 numbers) | about 2 s | **3.8-4.4 s** with 2 GB of RAM (it takes the memory it can) |
| Power on to the PIN page | | 13-17 s |
| The one-time in-place pass | about 5 min (SSD) to 30 min (HDD) for 40 GB, idle priority | 4 GB in 4-5 s on a RAM disk: **only a real disk can say** |
| Start with no PIN yet | silent | no page, no delay |

## How it is proven

Two layers, both repeatable:

**`tools/theftunit.sh`** (50+ checks, runs anywhere with Docker and
privileges: swtpm, loop devices, dm-crypt) and its **8+ mutants**, each
of which must make it fail: in-place encryption with her files intact
and no plaintext left; a pass stopped half-way resumes; PIN, wrong PIN
(counted by the chip itself), lockout recognised, rescue code, the
forgot-PIN flow through the page's own logic, repin, change-pin, PCR 7
change, a different chip, no chip, 44 px targets, no jargon,
`encrypt-init` with only an initramfs's tools, 512 / 4096 / 4Kn sector
sizes and the header check, Adiantum.

**`tools/theftvm.sh`** on the real image: OVMF Secure Boot with
Microsoft's keys, swtpm as a TPM 2.0, her PIN typed as key presses into
the VM, screens taken. Five runs, all green (summaries and pictures in
`docs/results/theft/`):

| Run | Checks | What it adds |
|---|---|---|
| AES-NI, key sealed to PCR 7 | 29 | in-place encryption on the booted image, power pulled mid-pass and resumed, no plaintext on the raw disk, set-pin, the PIN page on screen, wrong PIN counted by the TPM, **a dbx update -> "this computer has changed" -> rescue code -> PIN again -> re-sealed -> same PIN next start**, a different chip -> same path |
| no AES (Adiantum), PCR 7 | 29 | the same, on the Adiantum drive |
| AES-NI, no PCR | 25 | a dbx update changes nothing: the PIN just works |
| no TPM (6-number passphrase) | see S4.md | the passphrase path, Esc on the keyboard for "I forgot my PIN" |

What this found in the real image that the unit tests could not (all
fixed, all in the history): the initramfs script was installed as a
link that dangled; `cryptsetup` needs `/run/cryptsetup` and, without
udev's dm rules, `DM_DISABLE_UDEV` (the pass waited for ever in a
semaphore); real partitions are not 4096-aligned (cryptsetup refuses
4096-byte sectors); the initramfs has no `head`; Adiantum needs
`af_alg`/`algif_skcipher` in the initramfs; `prepare` tested a command
name as a path.

### The "Put Windows back" handover (from A2)

Putting Windows back may copy her AurOS files into an exFAT volume
inside AurOS's own space so Windows can read them. With an encrypted
root that volume would be ciphertext, so `handover.sh` refuses (exit 5)
and she gets the USB-stick path. When this is switched on, the handover
volume has to live **outside** the LUKS container: a small plaintext
exFAT made only at put-back time, after she consents (she is giving the
files away on purpose), or another way Windows can read them. Until
then the USB-stick path is the answer, and "Put Windows back" itself
never needs a key (it deletes partitions, it never reads them).
