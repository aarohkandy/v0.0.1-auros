# Current state (2026-10-23 ~05:00 UTC)

Full map: [docs/ATLAS.md](../ATLAS.md).

## Newest

- The project moved to `aarohkandy/v0.0.1-auros`, branch `main` (04:45). Workflows run there.
- No release exists in the new repository (`gh release list` is empty). The site has never been live.
- The build that should make the first prerelease (run 39901973428, `[release]`) is still **queued**.

## Works and is proven (how)

- **The image build and its gates** (hash = manifest, hardentest, signed shim chain): green on Actions in the old repository (`38e1959`). Not yet in the new one.
- **Install, no-stick install, restore, first boot, choices, Put Windows back:** installtest, nosticktest, putbacktest, firstboottest, choicesboottest. **KVM simulation only.** putbacktest runs under Secure Boot; the others with Secure Boot off.
- **Wizard on Windows Server, read-only** (preflight, navtest, selftest, screen reader): windows.yml, last green in the old repository (run 37224672361).
- **e2e M1** (Windows 11 in KVM boots and is driven): runs 39891036021 and 39891768538.
- **Download from the site in real Edge:** 34.9 MB fetched (run 39894079407).
- **Website checks** (`check.mjs`): green on the new repository at 04:50.
- **Unit suites** (aurfirst, nvram, bridge, choices, store, Ferry, othersys): green in the old repository.

## Broken, unproven or risky

- **Never done: an install on real Windows.** e2e M2 stalls at Edge's warning flyout; M3 has never been reached. aurstage has never run after a real Windows restart.
- **Promote can never succeed.** `release.yml:333` looks for a job named `e2e`, but the jobs are named `e2e (base)` and so on. `:330` only scans the newest 50 successful runs, and gate-only runs fill that window.
- **e2e.yml and windows.yml still test the old repository's installer** (`e2e.yml:35-36`, `windows.yml:29-37`).
- **windows.yml has not finished once in the new repository.** Each push cancels the previous run (11 in a row). So the gapselftest fix (`4ba4a5a`) is unproven.
- **The `tests` workflow has not run in the new repository** (15 of 15 skipped while the build is queued).
- **Data at risk on her PC:**
  - "Fix these for me" decrypts every BitLocker drive (`fix_win.c:329-353`) and empties the Recycle Bin (`:310`). Owner decision D11 says pause, never decrypt.
  - Staging probably refuses every install west of UTC (`clock-behind`, `journal.c:404` against `phases.c:1569`).
  - The no-stick mode that ships has no power-cut test. A retry shrinks Windows again.
- **Security:**
  - Every machine has the password `auros` and sudo rights (`forge:1358,1385`).
  - GRUB offers `init=/bin/bash`.
  - pkexec gdebi lets her account install anything as root without a password (`forge:1618`).
  - Firefox never updates.
- **Features that do not work:**
  - "Bring my files across" fails on every machine because of the answer service's sandbox (`auros-answer.service:21-31`).
  - Her chosen look resets on every restart (`main.c:744` runs before `:754`).
  - The Help button never opens the help pages.
- **Not in CI:** powercuttest, bootchaintest, bootupdatetest, gpttest, committest and the desktop's own tests. They exist only as old transcripts.

## In flight right now

- **E:** shepherd build 39901973428 to the first prerelease; chain release → e2e → promote → Pages; fix promote.
- **F:** warming the Windows cache (e2e run 39902908530 in progress); pin e2e to E's prerelease; M2, then M3.
- **P:** profiles lint fixed; building and booting all four profiles, one at a time (about 4-5 h).
- **windows.yml** run 39903139176 is pending. gapselftest now has its own queue (`b312624`).
- **Workflows:** `auros-safety-review` → `docs/safety/FINDINGS.md`; `auros-design-direction` → `docs/DESIGN-DIRECTION.md`.
- **Other streams** (A2, B, C, D, K, M, R, S1-S5): session-B orders in `SESSION-2026-10-23-B.md`. H paused.

## Next steps, in order

1. Get the first prerelease out of 39901973428. Point e2e.yml and windows.yml at it.
2. Fix promote (`release.yml:330,333`). Stop each push cancelling windows.yml.
3. Get M2 past the Edge flyout, then Smart App Control, then M3. That is the first real-Windows install.
4. Before anyone real runs it:
   - make BitLocker a pause, not a decrypt (D11)
   - stop emptying the Recycle Bin
   - make the journal clock check tolerate local time
   - add power-cut instants for no-stick mode
5. Security:
   - give each machine its own password, or no sudo password at all
   - remove `init=/bin/bash` and the gdebi rule
   - get Firefox and Ubuntu packages updating
6. Fix Ferry's sandbox and its privilege model together (ATLAS §8). Fix the look-reset bug.
7. Put powercut, bootchain, bootupdate, gpt and commit tests into CI.

## Needs the owner

- Turn on GitHub Pages: Settings → Pages → Source: GitHub Actions.
- Approve the BitLocker behaviour (D11 pause) and its visible consent wording.
- Decide what replaces the shared `auros` password and the sudo grant.
- Signing (SmartScreen and Smart App Control) and the AurOS apt archive key (S5).
- The security contact and the `[OWNER]` placeholders in `docs/legal/*`.
