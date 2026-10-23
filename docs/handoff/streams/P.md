# Stream P — every profile builds and boots

Owner of: `.github/workflows/profiles.yml`, `profiles/{office,revive,school-kiosk,multilingual}.profile`,
`tools/profiletest*`, this file. Session B: repository aarohkandy/v0.0.1-auros, branch `main`,
worktree `wt-p` (environment and push loop: `docs/handoff/FIELD-MANUAL.md`).

## Goal

The four non-desktop profiles (office, revive, school-kiosk, multilingual) must each build on
GitHub Actions (with Firefox, no browser fallback) and start in a KVM VM to their own desktop,
with a screenshot, an image size and a boot result for each.

## Status (update this table after every step)

Run 39903833034 (commit 3a5610f), Actions, KVM, 1280x800, no browser fallback (Firefox from
mozilla-apt). Every profile: build/all 4.5-6.5 min with the cached debootstrap base, then
`tools/profiletest.sh boot`: all checks green, no failed unit, zram swap + earlyoom active.

| profile | lint | built | boots to desktop | image (raw / gzip -9 download) | desktop up | RAM used | screenshot |
|---|---|---|---|---|---|---|---|
| office | pass | yes (308 s) | yes, 21/21 | 4.78 GiB / 1.53 GiB | 18 s | 415 MiB of 3.9 GiB | `docs/shots/profiles/office-first-start.png` |
| revive | pass | yes (267 s) | yes, 21/21 (2 GiB VM) | 4.24 GiB / 1.31 GiB | 14 s | 313 MiB of 1.96 GiB | `revive-first-start.png` |
| school-kiosk | pass | yes (328 s) | yes, 22/22 | 4.33 GiB / 1.37 GiB | 14 s | 1255 MiB of 3.9 GiB | `school-kiosk-first-start.png` |
| multilingual | pass | yes (384 s) | yes, 21/21 | 4.62 GiB / 1.39 GiB | 16 s | 432 MiB of 3.9 GiB | `multilingual-first-start.png` |

What I saw in every screenshot (read myself): the first thing on every profile is the full-screen
"AurOS is on this computer / Yes, it all works / Let me look first / No, go back to Windows" panel
(aurshell `welcome.c`), which hides the profile's own desktop. Right colours for each (slate
office, ember revive with the taskbar behind it, sandstone kiosk, nocturne multilingual), so the
shell really is the profile's. The kiosk shows ONLY the panel and a "Help" strip; whether its
Firefox is open behind it was not visible, so the boot test now checks it (below).

Open observations (not decided by me):
- multilingual: the system language is fr_FR (probe: `LANG=fr_FR.UTF-8`, all five locales
  generated) but aurshell's own text is English in the screenshot. aurshell has no translation
  yet; the profile promises "native French", so this is an aurshell gap, not a profile bug.
  Its theme is nocturne (inherited), not a French-specific one.
- revive: "Get more programs" is absent by design (`software_store=""`); owner may want it back.
- kiosk uses 1255 MiB (Firefox running) vs ~430 MiB for the others.

## Done and pushed

- `07ee591` (session B) selftest green again. lint_one now reads `packages_security` (stream S1
  moved zram-tools/earlyoom/nftables there; forge stage 2 installs it, lint did not read it, so
  every desktop-inheriting profile "asked for zram with no zram-tools"). The kiosk boot fixture
  now reports a hardened boot (`swap=/dev/zram0 partition 1.9G;`, `earlyoom=active`). New
  mutants prove the zram/swap/earlyoom checks fail when broken. apt in profiles.yml bounded.

- `47f6d90` office: `thunderbird` removed (noble's is a snap stub; forge pins stubs to -1, so the
  build died). revive: exclude list uses noble's real names (one unknown name made apt purge
  nothing). school-kiosk: neutral brand name, blank enrollment URL.
- `tools/profiletest.sh`:
  - `sh tools/profiletest.sh lint [profile...]` — seconds, no root; downloads noble's Packages
    indexes once (cache `$PROFILETEST_PKGS`, default `/tmp/profiletest-pkgs`).
  - `sh tools/profiletest.sh selftest` — proves every check fails on broken input (20/20 pass).
  - `sudo sh tools/profiletest.sh boot <profile> [evidence-dir]` — needs `out/auros-<profile>.img`;
    checks the image's packages/policy/browser, injects a probe unit (NOT part of AurOS) into a
    scratch copy, boots OVMF+QEMU (KVM if `/dev/kvm` writable) at 1280x800, reads the probe's
    `PROFILETEST key=value` lines from serial, screendumps via QMP, writes
    `<profile>.png`, `<profile>-report.txt`, `<profile>-serial.txt`, `<profile>-image.txt`.
    Locally verified only the QMP screendump + PNG writer (OVMF with no disk); the full boot
    has not yet run anywhere.

## Half-done / not pushed

- `93f5c8a` COMMITTED LOCALLY IN wt-p, NOT PUSHED (the agent permission system refused the push
  at that moment): boot test presses Escape over QMP and takes `<profile>-desktop.png` (the
  profile's own desktop under the welcome panel), and a kiosk must have a browser process running
  (probe `running=`; selftest mutant). profiles.yml uploads `-desktop.png`. Selftest 25/25.
  Push it (`git push origin HEAD:main` from wt-p after `git fetch -q origin +main:refs/remotes/origin/main
  && git rebase origin/main`), then the next `profiles` run (any push touching tools/profiletest*
  triggers all four, ~1 h one at a time) gives the desktop shots; commit them to docs/shots/profiles/.

## Next 5 steps

1. Write `.github/workflows/profiles.yml`: push on `claude/**`; a `gate` job (like build.yml's)
   runs when the subject has `[profiles]` (optionally `[profiles office revive]` for a subset) or
   the push changed `profiles/`, `tools/profiletest*` or `profiles.yml`. Then a matrix job over the
   four profiles with `max-parallel: 1`, `fail-fast: false`, and job-level
   `concurrency: profiles-<ref>-<profile>` (`cancel-in-progress: false`; NOT workflow-level:
   docs-only pushes would replace a pending real run). Each leg: free disk, apt tools
   (debootstrap squashfs-tools zstd gdisk dosfstools mtools e2fsprogs sbsigntool python3 cpio
   file binutils qemu-system-x86 qemu-utils ovmf pigz), KVM udev rule (copy tests.yml), restore
   the debootstrap cache with build.yml's exact key formula (key `debootstrap-desktop-<k>`) and
   untar into `work/forge/<profile>/`, `sudo env -u ALLOW_BROWSER_FALLBACK ./build/all <profile> --keep=raw`,
   `pigz -9` size, `sudo sh tools/profiletest.sh boot <profile> ev/`, publish `ev/*` to prerelease
   `profiles-<run_id>` with `gh release create/upload` (needs `permissions: contents: write`).
2. Push; watch `gh run list -w profiles.yml`; read failures with `gh run view --log-failed`.
3. Fix profile-specific breakage in the four profile files only (anything in `build/forge` is
   shared: smallest edit, say so in the commit body).
4. Download the PNGs (`curl -sSLO https://github.com/ComputerDude771/auros-from-scratch/releases/download/profiles-<RUN>/<profile>.png`),
   LOOK at each, commit them to `docs/shots/profiles/`, fill the table above.
5. Report sizes and boot results in this file and in the Progress log of
   `docs/handoff/SESSION-2026-10-23.md` (append one line).

## Traps

- Any push touching `profiles/` also triggers the desktop `build.yml` (its gate greps `^profiles/`).
  Batch profile edits.
- Free plan: 20 concurrent jobs shared by ~15 agents. Keep max-parallel 1.
- This checkout's `remote.origin.fetch` maps only the old branch, so `git fetch origin main`
  does NOT move `origin/main` and the field manual's push loop rebases onto a stale tip. Use
  `git fetch -q origin +main:refs/remotes/origin/main` in the loop.
- lint must read every list forge's stage 2 installs (build/forge `local pkgs=`); when someone
  adds a new packages_* key there, add it to lint_one and the selftest index loop too.
- `school-kiosk`'s work dir is `work/forge/school-kiosk/` (forge uses `profile_id`).
- forge refuses a build whose browser fell back to Epiphany unless `ALLOW_BROWSER_FALLBACK=1`;
  on Actions Mozilla is reachable, so do not set it.
- revive has `software_store=""`: no "Get more programs" on its desktop at all (a profile design
  choice for 2 GB RAM, not a bug in the shell). Owner may want it back; ask, don't decide.
- S1 (hardening) may add zram/oomd to every image; revive already installs zram-tools + earlyoom.
  Two zram setups would fight: check `swap=` in revive's report.
- Office has no mail program now (see the comment in office.profile).

## How to verify

`sh tools/profiletest.sh selftest && sh tools/profiletest.sh lint` locally (both exit 0). On
Actions: the `profiles` run is green, and each profile's PNG shows its desktop (office: rail in
slate; revive: taskbar in ember; school-kiosk: locked, only the browser; multilingual: rail,
French).
