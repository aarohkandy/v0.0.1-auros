# SignPath Foundation application: the text to paste

> Prepared by the AurOS agents without a lawyer (decision D15). It is an
> application to a free programme, not a contract. **Everything marked
> `[OWNER: ...]` is for you to fill in or confirm; nothing else needs
> changing.** Their form (`https://signpath.org/apply`) may ask in a different
> order or with different words than the headings here: copy the matching
> block. Their rules are at `https://signpath.org/terms`; `docs/SIGNING.md`
> section 2 checks every one against this repository. Read their page again
> on the day you apply: if a rule has changed, tell an agent.

## Before you apply (2 minutes)

- [ ] Two-factor sign-in is on for your GitHub account (they require it of
      everyone on the team).
- [ ] `https://aarohkandy.github.io/v0.0.1-auros/code-signing.html` opens
      (the code signing policy page, linked from every page's footer). If you
      have moved to your own domain, use that address everywhere below.
- [ ] A release exists to point at: `https://github.com/aarohkandy/v0.0.1-auros/releases`
      (the newest prerelease; each has its source code beside it as `sources-<tag>`).
- [ ] The policy page says "We plan to apply" today. After you submit, tell an
      agent to change it to "We have applied" in the same commit as nothing
      else; do not let it say more than is true (`tools/legal/check.py` reads it).

## Project

- **Project name:** AurOS
- **One line:** AurOS is a free Linux desktop that a person installs on their
  Windows computer by downloading one file, `AurOS-Setup.exe`, and clicking
  through three screens. Windows and their files stay; "Put Windows back"
  removes AurOS.
- **Repository:** https://github.com/aarohkandy/v0.0.1-auros (public)
- **Home page:** https://aarohkandy.github.io/v0.0.1-auros/
- **Download page / releases:** https://github.com/aarohkandy/v0.0.1-auros/releases
- **Code signing policy:** https://aarohkandy.github.io/v0.0.1-auros/code-signing.html
- **Licence:** MIT (`LICENSE` in the repository). No dual licensing, no
  proprietary code in what is built or signed. Fonts are SIL OFL; the
  operating system image is Ubuntu packages under their own licences and is
  downloaded separately, not part of the file to sign.
- **Contact:** [OWNER: the email address SignPath should write to]
- **Applicant / maintainer:** [OWNER: your name as you want it shown], GitHub
  `@aarohkandy`, the owner of the repository and its only maintainer today.

## What the software does (their reviewers will read this)

AurOS Setup is a Windows program a person runs once. In order:

1. It looks at the computer (Windows version, firmware mode, free disk space,
   disk layout, Secure Boot and BitLocker state) and **stops, before changing
   anything, if the computer is one it cannot install on safely**. Nothing it
   learns leaves the computer.
2. It downloads the AurOS system image (about 1.5 GB, in pieces, each
   checked against a SHA-256 list that is built into the program) from the
   project's GitHub release, when the person presses the install button.
3. It prepares one restart on the Windows side (nothing destructive: a
   copy of the way back, the installation environment, a one-shot boot
   entry). After that restart a small installation environment shrinks the
   Windows filesystem into its unused space, writes AurOS into the freed
   space, reads it back and checks it, and only then changes the partition
   table, in a single-sector write. **Windows and your files are kept** (the installer's own pages say it may damage them if the power fails during the shrink, and tell her to copy what she cannot replace first); until
   the person says "it works" Windows is what starts by default, they can
   say "no, go back" at AurOS's first start, and "Put Windows back" later
   in AurOS's Settings removes AurOS and returns the disk to its previous
   layout.
4. Optional, and on the version being proven (the fast path, switched on by a
   build flag and not yet the default): if the person ticks a box that is
   unticked until they do, Setup reads the Wi-Fi profile of the network the
   computer is connected to and carries it to AurOS on the same computer, so it
   can get online at first start; it pauses BitLocker protection for one
   restart (Windows' own call; it never decrypts). Neither sends anything
   anywhere. [OWNER: say "today" or "being proven" as true on the day you apply.]
5. BitLocker, in the default version: today Setup stops on a computer with BitLocker or device
   encryption on, and says so before changing anything. [OWNER: delete this
   sentence's "today" version and use the next one once the BitLocker work
   ships (decision D13).] The finished design pauses BitLocker protection
   for one restart with Windows' own interface and never decrypts the drive.
6. Secure Boot stays on: AurOS starts through Canonical's signed `shim` and
   `grub`, the same files Ubuntu uses.

What it is not, said plainly because it touches disks and boot entries:
it is not a hacking tool and does not circumvent any protection of Windows;
it reads Windows through its documented interfaces, sets a one-shot EFI boot
entry, and (once supported) pauses BitLocker with Windows' own call; it
refuses on anything it is unsure of. It has no
network function except the download in step 2, no telemetry, no account, no
advertising, and installs nothing else.

**Why unsigned is a problem for these users:** the audience is
non-technical people, many of them elderly. Windows shows an unsigned
download a full-screen "Windows protected your PC" panel whose only visible
button is "Don't run", and some Windows 11 computers (Smart App Control)
block it outright. A person who gets past that has also learned to ignore
the warning, which is the wrong habit to teach before a program touches
their disk. A named publisher is the single biggest thing that would help.

## What we would sign

- **One file per release:** `AurOS-Setup.exe` (a 64-bit Windows PE, about
  35 MB), Authenticode, SHA-256.
- **Built from this repository only**, by `.github/workflows/build.yml` on
  GitHub-hosted runners (`ubuntu-24.04`, a MinGW cross-compiler); nothing is
  built on a maintainer's computer. The workflow files and build scripts are
  in the repository and reviewed like any other source.
- **Signed from the same run:** `release.yml` uploads the finished file as a
  workflow artifact and submits it with SignPath's GitHub action. It then
  verifies the signature, and that the signed file is the built file apart
  from the signature (`tools/signed-check.sh`), before anything is published,
  and runs it on a real Windows computer and in a full install test.
- **Release cadence:** a handful of releases in the first months; each one
  approved by hand before signing.
- **File metadata:** ProductName `AurOS`, ProductVersion one value per
  release (`src/aurbridge/aurbridge-info.rc`).

## Team roles

| Role | Who | Notes |
|---|---|---|
| Author (changes code without review) | [OWNER: name], `@aarohkandy` | the only committer today |
| Reviewer (reviews every change from anyone else) | [OWNER: name], `@aarohkandy` | |
| Approver (approves each signing request) | [OWNER: name], `@aarohkandy` | |

One person holds all three roles today; the policy page says so and will list
more people as they join. Everyone in these roles uses two-factor sign-in on
GitHub and on SignPath. [OWNER: confirm; if you add a second person before
applying, list them above and on `website/code-signing.html`.]

## Privacy

Nothing is sent to the AurOS project. The only network request AurOS Setup
makes is the download of AurOS itself from GitHub, which the person asked for
by pressing the install button; GitHub sees the usual connection information
(its own privacy statement applies). The Wi-Fi profile above, if the person
ticks the box, stays on the computer. The website uses no cookies and no
tracking and loads nothing from other servers. This is stated on the code
signing policy page and on `privacy.html`, which Setup's Ready page links. If
a later version adds an optional report, it will be off until the person ticks
a box, and the policy page will say so before it ships.

## Reputation (read this before you send it)

SignPath Foundation may refuse a brand-new project with one maintainer. What
exists that a reviewer can check: a public repository with an MIT licence
(`LICENSE`), a security policy (`SECURITY.md`), every release built and tested
on GitHub's runners in the open (the build and release workflows are in the
repository, and the tests include a power cut at each dangerous instant of an
install and of the way back), source code for each release published beside it
(`sources-<tag>`) with a written offer, a code signing policy page with named
roles, and an installer that refuses at the first doubt. What does not exist:
a history. The first public prerelease is dated 2026-10-23; there are no
outside contributors, no users yet, and the author's name carries no
reputation. Say so plainly if the form asks. A refusal is not the end:
the installer works unsigned (the website shows the extra clicks); the
decision to apply again later costs nothing.

## Questions their review is likely to ask, with the true answers

- *"It modifies partitions and boot entries; why is that not malicious?"*
  It is the whole purpose, it is announced screen by screen before it
  happens, it refuses at the first doubt, and the way back is tested:
  `tools/powercuttest.sh` cuts the power at each named dangerous instant
  of an install and of a restore and requires Windows back with every file
  unchanged; `tools/committest.sh` and `tools/gpttest.sh` prove the one-sector
  partition-table write is the only commit point. These run on every build
  (`.github/workflows/tests.yml`).
- *"Does it circumvent Secure Boot or BitLocker?"* No. Secure Boot stays on
  and AurOS boots through Canonical's Microsoft-signed `shim`. BitLocker, once
  supported, is paused with Windows' own call for one restart and never
  decrypted; today Setup stops on such a computer.
- *"What third-party code does the installer contain?"* The installer is our
  own C code, MIT, plus the SIL-OFL typeface it draws with. The payload it
  carries is the Ubuntu-signed boot files and a small Linux kernel and
  initramfs for the installation environment, which are Ubuntu's, unmodified,
  under their own licences.
- *"How can a user check a download?"* `SHA256SUMS` and `pieces.txt` are
  attached to each release; the installer checks every piece before it uses
  it; the signature is checkable with `Get-AuthenticodeSignature`.
- *"Where is the source of the signed file?"* The tag of each release names
  the build; the release notes name the commit the file was built from and
  the Actions run that tested it.

## After they answer

`docs/SIGNING.md` section 4 lists what you do in SignPath and in GitHub, in
order: the project, the GitHub trusted build system, the artifact
configuration, the `release-signing` policy with manual approval, an API
token, then one GitHub secret and one variable. The pipeline is already
written and stays off until those exist.
