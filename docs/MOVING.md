# Getting everything onto your computer, and moving to a new repository

## One command: download everything (or update it if you already have it)

Paste into Terminal on a Mac (or Linux). It puts the whole project in
`~/Desktop/auros`: every file and folder, at the newest version. Run the
same command again any time to update it.

```sh
D=~/Desktop/auros; R=https://github.com/aarohkandy/v0.0.1-auros.git; B=main; if [ -d "$D/.git" ]; then git -C "$D" pull --ff-only "$R" "$B"; else git clone --single-branch --branch "$B" "$R" "$D"; fi; cd "$D"
```

- `D` is the folder (change it if you like, e.g. `D=~/Desktop/"auros v0.0.1"`).
- `R` is the repository address, `B` the branch. **These two are the only
  things that change when you move repositories.**
- An existing folder updates from `R` and `B` by name, whatever its own
  `origin` says, so a folder cloned from the old repository
  (`ComputerDude771/auros-from-scratch`, branch
  `claude/brave-hypatia-x72g1m`) updates from the new one.
- `--single-branch` is deliberate: the old repository holds a branch
  (`image-desktop`) with about 7 GB of superseded image pieces; if one
  like it is ever pushed here, the clone stays small.

### What the folder contains, and what it doesn't

Contains: all source code, build scripts, tests, workflows, the website,
every design and handoff document (`docs/handoff/RESUME-NOW.md` is the
entry point), screenshots and research.

Does not contain, by design (each is rebuilt from the folder):
- **The built installer and OS image** (about 1.8 GB). They are published as
  GitHub Release files and rebuilt by `.github/workflows/build.yml` +
  `release.yml` from this folder.
- Test evidence from the Windows VM runs (published as `e2e-*` prereleases).
- GitHub settings and secrets (see `docs/OWNER-SETUP.md`).

## Moving to a new repository

1. Create the new, empty repository on GitHub (public, so Actions minutes stay
   free and runners have KVM).
2. From the folder above:
   ```sh
   git remote set-url origin https://github.com/NEWOWNER/NEWREPO.git
   git remote set-branches origin main
   git push -u origin HEAD:main
   ```
   (`main`, or keep the branch name; then use that as `B` in the one command.)
   `set-branches` matters: a folder cloned with `--single-branch` fetches
   only its old branch, so without it `git fetch origin main` leaves
   `origin/main` stale and a rebase onto it silently uses an old tip.
3. Do `docs/OWNER-SETUP.md` sections 1, 2 and 4 on the new repository
   (Pages, default branch, secrets).
4. Push a commit whose subject contains the build/publish words in
   `docs/HANDOFF-RUNBOOK.md` to rebuild and publish the installer there.

Nothing in the code should need editing for a move: workflows read the
repository from GitHub (`${{ github.repository }}`), scripts ask
`sh tools/repo.sh`, the website is stamped with it at deploy
(`tools/site/stamp.sh`) and the installer has it baked in at build time.
`sh tools/repotest.sh` (in the `tests` workflow) fails if the old
repository, a `claude/*` branch or a container path appears outside
`docs/`, and checks that a site built for another repository names only
that one.

## The 2026-10-23 move, and what still points back

`ComputerDude771/auros-from-scratch` (branch `claude/brave-hypatia-x72g1m`)
moved to **`aarohkandy/v0.0.1-auros`, branch `main`**; website
`https://aarohkandy.github.io/v0.0.1-auros/` once Pages is on
(`docs/OWNER-SETUP.md` 1). The old repository is read-only for us; never
fetch its `image-desktop` branch (7 GB). Still pointing at it, on purpose
and for now:
- `windows.yml` and `e2e.yml` `env:` pin the installer and image published
  only there (`PUBLISHED_URL`, `KNOWN_BAD_URL`, `IMAGE_PIECES_URL`,
  `INSTALLER_URL`). Stream E replaces them with this repository's releases
  once one is published; `tools/repotest.sh` allows exactly those keys.
- That published installer (`ef903efd...`) has the old image address baked
  in; a new release here replaces it.
- History in `docs/` and `HANDOFF.md`.
