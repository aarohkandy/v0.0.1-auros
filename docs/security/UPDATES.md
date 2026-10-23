# Updates after the install: how a fix reaches her, and what she sees

The single source of truth for everything that updates on an installed
AurOS. Owner: stream S5. If code and this file disagree, one of them is a
bug: say which in the commit that fixes it.

## In one paragraph

Everything on her computer updates by itself, in the background, at low
priority, and **she sees nothing**: no question, no notice, no restart she
did not choose. Security fixes from Ubuntu, Ubuntu's bug fixes, AurOS's own
parts, Firefox and her programs from "Get more programs" are all installed
by timers. A program she has open is never replaced under her: it waits
until it is closed. A new kernel is installed at once and is used the next
time she switches the computer on, with the previous kernel kept as an
automatic fallback. Firmware is **not** updated from AurOS (decision and
evidence below). The move from Ubuntu 24.04 to 26.04 is planned, not
automatic yet. Ubuntu 24.04 is supported until April 2029.

## What updates, from where, and how

| What | From | Installed by | When | Proof |
|---|---|---|---|---|
| Ubuntu security fixes (`noble-security`) | archive/security.ubuntu.com, Ubuntu's key | unattended-upgrades (Ubuntu's own `50unattended-upgrades`) | daily, `apt-daily-upgrade.timer` | `updatepolicytest static` (on every build) |
| Ubuntu bug fixes (`noble-updates`) | same | unattended-upgrades, allowed in `rootfs/etc/apt/apt.conf.d/53auros-updates` | daily, **phased** (Ubuntu releases each to a few percent of computers first and stops on regressions) | same |
| AurOS's own parts (aurshell, aurfirst, boot chain, Ferry, themes, `/etc` settings) | AurOS's signed apt repository, `https://github.com/<owner>/<repo>/releases/download/apt/` | unattended-upgrades | daily | `tools/updatetest.sh` (25 checks, 4 mutants), `aptrepo.yml` publish |
| Firefox | `packages.mozilla.org` (or the mozillateam PPA), Mozilla's key | unattended-upgrades; **held while Firefox is open** | daily, on a run with Firefox closed | `updatepolicytest firefox` (real unattended-upgrade, real Mozilla repository, mutant) |
| Her programs from Flathub | Flathub, system installation | `auros-flatpak-update.timer` | 30 min after start, then every 12 h | static check of the timer only (see "Not proven") |
| Kernel, grub, shim | Ubuntu (`-security`, `-updates`) | unattended-upgrades, through `/usr/lib/auros/bootchain` | daily; the new kernel is used at the **next start** | `tools/bootchaintest.sh`, `tools/bootupdatetest.sh` (KVM, Secure Boot) |
| Firmware (UEFI, devices) | **not from AurOS** | Windows Update / the PC maker, while Windows is kept | — | `updatepolicytest static`: fwupd absent |
| The Ubuntu release (24.04 → 26.04) | — | **not automatic yet** | before April 2029 | plan below |

Every apt source in an image must be one of these. `updatepolicytest static`
fails a build that adds a source nothing updates from (this is how Firefox
went unpatched: its source was added, its origin was never allowed).

## Never interrupting her

- **Timing.** Ubuntu's timers: lists at 06:00 and 18:00 (randomized over
  12 h), installs at 06:00 (randomized over 1 h). A missed run happens
  shortly after the next start (`Persistent=true`). Never in the first hour
  of the computer's life, when she is installing programs and the store
  needs the package lock (`/usr/lib/auros/not-first-hour`, an
  `ExecCondition` on apt-daily, apt-daily-upgrade and the Flathub update).
- **Felt by nobody.** The install and the Flathub update run at Nice 15 and
  best-effort IO priority 7 (`apt-daily-upgrade.service.d/60-auros-quiet.conf`,
  `auros-flatpak-update.service`). Not "idle", which a busy computer could
  starve for hours while the store waits on the lock.
- **Nothing open is replaced under her.** A packaged Firefox replaced while
  it runs shows "Restart Firefox to keep going" in every new tab, so
  `/usr/lib/auros/hold-running-apps` (ExecStartPre of apt-daily-upgrade)
  holds `firefox` while it is open and clears the hold on the next run.
  Flatpak programs keep running the version they started with; the new one
  is what opens next time. AurOS's own programs are replaced file by file
  (dpkg renames into place), so a running aurshell keeps its old code until
  the next start.
- **No restart she did not choose.** `Unattended-Upgrade::Automatic-Reboot
  "false"` (written down so nothing changes it by accident). A kernel update
  is installed at once and used at the next start; `/run/reboot-required` is
  created and nothing reads it. `needrestart` (which restarts services, the
  network among them, after an update) must not be in the image.
- **No questions.** A setting changed on this computer is kept and the rest
  of the package updated (`DPkg::Options --force-confdef --force-confold`).
  Without it, unattended-upgrades skips any package that would ask, silently
  and for good, and with it every package that must move at the same time:
  `tools/updatetest.sh` found that every AurOS fix would have stopped at the
  first edited `/etc` file.
- **The store never offers updates.** GNOME Software, once opened, would
  download updates through PackageKit and ask her to "Restart & Install"
  into an update screen. `rootfs/usr/share/glib-2.0/schemas/90_auros-updates.gschema.override`
  sets `allow-updates`, `download-updates` and `download-updates-notify` to
  false. No `update-notifier`, no `update-manager`.
- **An update cut off by a power cut** is finished at the next start, before
  anything else uses apt: `auros-dpkg-repair.service`.

What she could be shown, at most (not built; streams B/C own Settings): one
line in Settings, "Updates install by themselves. Last checked: today."

## AurOS's own update channel

### Packages

`build/aptchannel image` runs at the end of `build/forge` (stage 6, before
the rootfs is sealed). Every AurOS file in the image becomes part of a
package: `auros-desktop`, `auros-first`, `auros-bootchain`, `auros-ferry`,
`auros-config` (our `/etc`, as conffiles), `auros-archive-keyring`, and the
metapackage `auros`, which depends on each at exactly its own version so
they always move together. The files are registered with dpkg in the image
(byte-identical, so nothing changes on disk) and the `.deb`s are kept in
`/var/lib/auros/debs/` (about 4 MB; after an update, the set installed
now, with the one before it in `debs-previous`).

- Version: `<os_version>.<commit time, UTC, YYYYMMDDHHMMSS>`, which only
  ever rises.
- A file from `rootfs/`, `themes/` or `shells/` is packaged only if it is
  byte-identical to the repository's copy and owned by no other package
  (nor ucf): a profile that changed a file keeps it, and no package ever
  fights Ubuntu's.
- Only the **desktop** profile (the one published) points at the channel.
  Another profile's computers would otherwise take the desktop's files over
  their own. A profile that is released one day gets its own channel tag.
- The apt source (`/etc/apt/sources.list.d/auros.sources`) is itself a
  conffile of `auros-config`. If the repository moves, one update from the
  old address carries the new one (see "Moving").

### The repository

A flat apt repository whose files are the assets of the prerelease tag
`apt` of this GitHub repository: `InRelease`, `Release`, `Release.gpg`,
`Packages`, `Packages.gz` and the `.deb`s. Never "latest" (that is the
website's download). No Pages needed. The address is derived
(`tools/repo.sh`: `AUROS_REPO`, else `GITHUB_REPOSITORY`, else the
checkout's remote), never typed.

### What a computer accepts

- `Signed-By: /usr/share/keyrings/auros-archive-keyring.asc` in the source:
  only AurOS's key, even if some other key is trusted for something else.
- `rootfs/etc/apt/preferences.d/auros`: the repository may supply `auros`
  and `auros-*` and nothing else (pin -1). Even a signature by AurOS's key
  cannot make the computer take a libc or a browser from it.
- unattended-upgrades takes `origin=AurOS,label=AurOS,archive=stable`.
- apt never downgrades, and refuses any file whose hash does not match the
  signed index.

`tools/updatetest.sh` proves each of these on a real apt and the real
unattended-upgrade with a throwaway key: a signed update installs by itself,
every part together, with no restart. It refuses unsigned, signed by another
trusted key, a changed `.deb`, a changed `Packages`, an older version, and a
non-AurOS package sealed with AurOS's own key. Both halves of a key rotation
go through, and an edited setting neither stops the update nor is
overwritten. `--break=trusted|signedby|pin|origin` removes one protection at
a time, and each turns the test red.

### Publishing: `.github/workflows/aptrepo.yml`

- **By itself** after a green build or release run that promoted a release
  (`[latest]` in its subject): that release is published. A promoted fix
  reaches every AurOS computer with nobody remembering to.
- `[apt]` in a commit subject: the latest release. `[apt TAG]`: that release
  (an urgent fix that is still a prerelease). Also `workflow_dispatch`.
- The `.deb`s come **out of the released image** (debugfs, no root), so what
  the channel serves is what those computers have. The image must carry the
  committed key and point at this repository's channel, or nothing is
  published. Older than what is published: refused. Equal: nothing to do.
- Signs with every key in the secret `AUROS_APT_SIGNING_KEY` that is in the
  committed keyring (two during a rotation), and refuses a secret key that
  is not. Verifies against the committed file. Uploads the packages first
  and the signed index last. Reads every file back through GitHub's
  redirect and checks it again. A real apt on a fresh Ubuntu then sees the
  update and fetches it.
- Warns when a signing key expires within 180 days.

### The key

- `AUROS_APT_SIGNING_KEY` (secret, the owner's step: `docs/OWNER-SETUP.md`
  4a), public half committed at
  `rootfs/usr/share/keyrings/auros-archive-keyring.asc`.
- Current key: `706641325C24F885D351885290B0EA8D0A4C1F3B`, ed25519,
  "AurOS updates", made 2026-10-23, **expires 2029-10-22**. Offline backup:
  the owner's USB stick (until he has one: `~/AurOS-update-key` on his Mac).
- Two things guard the channel, not one: the key, and write access to this
  GitHub repository (the only place computers fetch from, over HTTPS).

### Key renewal and rotation

**Renew (same key, longer life). The easy path while the private key is
safe.** Before April 2029: `gpg --quick-set-expire <FPR> 3y` with the backup,
commit the re-exported public key (same fingerprint), put the re-exported
private key in the secret, then release and publish. Every computer takes the
renewed key through `auros-archive-keyring`, signed by the still-valid key.

**Rotate (new key)**, when the old one may be compromised or is to be retired:

1. Make the new key (OWNER-SETUP 4a, uid `AurOS updates <year>`), with an
   offline copy.
2. Commit the keyring with **both** keys, as one armored block
   (`gpg --armor --export OLD NEW`). Put **both** private keys in the secret,
   one block after the other. Release and publish. Every update is now
   signed by both. Computers take the two-key keyring through
   `auros-archive-keyring`, and old computers still verify with the old key
   (`updatetest`: "rotation: signed by both, a computer with the old key
   takes it").
3. Keep signing with both for as long as computers that have not updated may
   exist, ideally until the old key expires (`updatetest`: "a computer with
   both keys takes the new key's").
4. Then commit the keyring with the new key only, and the secret with the
   new key only.

**If the key leaked**, rotate at once, with step 2's keyring holding the
**new key only** (signed by both). Every computer that takes that update
trusts only the new key from then on. Whoever holds the old key can still
reach computers that have not updated yet, but only if they can also publish
to this repository or stand between the computer and GitHub's HTTPS.

### Moving to another repository

GitHub keeps redirecting a repository's old address after a **transfer or
rename**, so computers keep updating: that is the way to move. A fresh
repository (a push to a new one) strands every installed computer unless the
old repository publishes one last update whose `auros-config` carries the new
address (the source is a conffile for exactly this). Use the same key
(OWNER-SETUP 4a, step 3).

## Firmware: off, by decision

fwupd is not in the image, and `updatepolicytest static` refuses it. The
evidence:

- A UEFI firmware (capsule) update changes what the TPM measures (PCR 0, and
  PCR 7 when Secure Boot's databases change). Windows' BitLocker then asks
  for the 48-digit recovery key at the next Windows start unless BitLocker
  was suspended first (Microsoft's own advice for BIOS updates). AurOS
  cannot suspend Windows' BitLocker from Linux, and a recovery-key screen is
  the scariest thing she could see. D11 promises that BitLocker is paused
  for one restart at install, and never otherwise touched.
- The same is true of AurOS's own encrypted disk sealed to the TPM (D9,
  stream S4) until a re-seal-before-firmware-update exists.
- fwupd also delivers UEFI revocation (dbx) updates. A dbx that revokes the
  shim or grub AurOS starts with makes AurOS unbootable unless the boot chain
  moved first. The boot chain checks this firmware's dbx and SBAT before it
  installs a new shim or grub (`bootchaintest`), but it cannot stop a dbx
  write.
- Windows is kept, and Windows Update delivers firmware for most PC makers
  (capsules as drivers), with BitLocker handled.

Revisit when S4's TPM re-seal exists, and possibly then fwupd for dbx only.

## Ubuntu 24.04 → 26.04, and end of life

- AurOS 0.1 is Ubuntu 24.04 LTS ("noble"). Standard security updates for
  Ubuntu's `main` run until **April 2029**. Extended (ESM) needs Ubuntu Pro,
  an account with Canonical, which AurOS does not use
  (`ubuntu-advantage-tools` is excluded).
- Packages from `universe` (in the image: e.g. `earlyoom`, `zram-tools`) get
  security fixes only on a best-effort basis from the community. Keep the
  image's universe list short. Each one is a choice.
- The kernel is `linux-image-generic` (the 6.8 series), supported for 24.04's
  whole life.
- Ubuntu 26.04 LTS came out in April 2026. Ubuntu opens upgrades from 24.04
  with 26.04.1 (August 2026). 26.04's standard support runs to April 2031.
- **Plan** (not built; nobody is stranded before April 2029):
  1. forge an AurOS image on 26.04 and pass the whole e2e on it;
  2. new installs from 26.04 once green;
  3. in-place upgrade for installed computers, driven by AurOS (Ubuntu's
     release upgrader, non-interactive, through the boot chain), only on AC
     power and at a restart she chose, proven in KVM, power cut included,
     delivered through AurOS's channel;
  4. start by October 2027, finish by October 2028.

## When an update is broken

- **Kernel:** the boot chain writes the start-up menu with the new kernel
  first and the previous one as grub's automatic fallback. A kernel that
  does not start falls back by itself (`bootupdatetest`, boot 3: a damaged
  kernel, the desktop still comes up).
- **shim / grub:** installed only if this firmware will start them (dbx,
  SBAT, the Microsoft 2011 → 2023 CA change: `bootchaintest`).
- **An interrupted update:** finished at the next start (`dpkg-repair`).
- **A desktop that keeps failing because of her settings:** safe mode
  (`shell-guard`).
- **A broken AurOS update (aurshell itself crashes):** undone by itself.
  When apt moves AurOS forward, `/usr/lib/auros/keep-previous` (an apt
  `DPkg::Post-Invoke`, `53auros-updates`) keeps the set from before in
  `/var/lib/auros/debs-previous`. `/var/lib/auros/debs` always holds the set
  installed now. Safe mode alone cannot help here: it runs the same broken
  program. So when the desktop also fails three times in safe mode's last
  step, `shell-guard` calls `/usr/lib/auros/rollback`. That pins the broken
  version to -1 (`preferences.d/00auros-broken-<version>`, sorted before
  `preferences.d/auros` because apt takes the first matching record),
  reinstalls the kept set, and starts her own desktop again. The daily
  update does not bring the broken version back, and a newer fix still
  installs. Once per three failures, never in a loop. Log:
  `/var/lib/auros/rollback.log`. From the first crash to the rollback is
  about nine restarts of the desktop, with the apology screen between.
  Proven by `tools/updatetest.sh` (real apt, the real hook, the real
  shell-guard; `--break=keep` turns it red). Not yet proven in a VM.

## Proof, and what is not proven

| Test | Proves | Runs |
|---|---|---|
| `tools/updatetest.sh` (+5 mutants) | AurOS's channel: signed installs, every refusal, rotation, conffiles; a broken update rolled back | `aptrepo.yml` test job, every change; homebase |
| `tools/updatepolicytest.sh firefox` (+mutant) | Firefox updates by itself, not while open | `aptrepo.yml` test job |
| `tools/updatepolicytest.sh static` | the built image's policy, sources, timers, store, base up to date, the image's key | `tests.yml` unit job, every build |
| `aptrepo.yml` publish | the real repository, signed, read back, fetched by a real apt | every publish |
| `tools/bootchaintest.sh`, `tools/bootupdatetest.sh` | kernel/grub/shim updates under Secure Boot; previous-kernel fallback | **not in CI yet** (by hand, KVM) |

Not proven yet, said plainly:

- a real Flathub update (only the timer and the command are checked);
- an update arriving **while she uses the desktop** in a VM, the next start
  updated and healthy, and a broken AurOS update rolled back on a real boot
  (the KVM test; the mechanisms are proven in a container);
- phased `-updates` behaviour over days on a real computer;
- that Firefox's "restart" page never shows (the hold is tested; the
  Firefox UI is not).

Known limits:

- No `Valid-Until` in the channel's `Release`: someone who can stand between
  a computer and GitHub's HTTPS could replay an old index and withhold
  updates (never install an older one). Adding it means re-signing on a
  schedule. Add it if the channel ever moves off HTTPS-only hosting.
- The signing key is root on every AurOS computer, by nature. The pin limits
  which package names it can supply, not what they do.
- An image built with the Firefox tarball route (a build host that cannot
  reach Mozilla's repository) has a Firefox nothing updates.
  `updatepolicytest static` fails it.
- On battery: unattended-upgrades installs on battery too (no
  `powermgmt-base`). Never on a metered connection is not enforced.

## On a computer: how to check

```sh
apt-cache policy auros firefox            # versions, and where they come from
cat /etc/apt/sources.list.d/auros.sources
ls /var/lib/auros/debs                    # the parts this image shipped with
grep -h 'Packages that will be upgraded' /var/log/unattended-upgrades/unattended-upgrades.log | tail
systemctl list-timers apt-daily-upgrade.timer auros-flatpak-update.timer
```
