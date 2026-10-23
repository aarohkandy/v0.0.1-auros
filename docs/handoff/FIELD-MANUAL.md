# FIELD MANUAL — how every agent works in session B (read before anything else)

This replaces the environment parts of `docs/handoff/STREAMS.md` (its
"Common rules" 1, 2 and 6, which described a container that no longer
exists). Everything else in STREAMS.md — file ownership, culture, owner
decisions — still holds. If you are a fresh agent with only the
repository: `START-HERE.md`, then this file, then your stream's
`docs/handoff/streams/<ID>.md`.

## 0. The mission (owner's words, 2026-10-23)

"My great-grandmother should be able to download, install and start using
this software without ever getting confused ... end to end including the
website and the download ... in less than an hour ... it should feel like
something complete, not a side project ... beautiful in its clearly
intentional simplicity, like Apple ... people are actually going to use
this as their OS, so if something goes wrong it is TRAGIC ... speed and
width."

So, in priority order: **(1) never lose her data or her Windows; (2) she
never gets confused; (3) it works on her PC; (4) it is fast; (5) it is
beautiful.** When two conflict, the lower number wins.

## 1. Where the code lives

- Repository: **https://github.com/aarohkandy/v0.0.1-auros**, branch **`main`**
  (the only branch; no PRs). Public. The old repository
  (ComputerDude771/auros-from-scratch) is read-only for us: **never fetch it**
  (its `image-desktop` branch is 7 GB and ten parallel fetches of it filled
  the owner's disk once).
- The lead's checkout: `/Users/aaroh/Desktop/auros v0.0.1/v0.0.1-auros`
  (note the space in the path). **Never edit it**; it is shared.
- Your own worktree (no spaces in the path, so build scripts behave):
  ```sh
  M="/Users/aaroh/Desktop/auros v0.0.1/v0.0.1-auros"
  S=/private/tmp/claude-501/-Users-aaroh-Desktop-auros-v0-0-1/914a7092-8dd8-4369-bb66-bf5c4e2ea1f2/scratchpad
  git -C "$M" fetch -q origin main
  git -C "$M" worktree add -b wt-<id> "$S/wt-<id>" origin/main   # once
  cd "$S/wt-<id>"
  ```
- **Pushing to `main` is authorized by the owner and REQUIRED** ("push to GitHub
  extremely frequently so I have a safe"). The repository is public on purpose. Never
  hold a finished commit back on a local branch: an unpushed commit is lost work.
- **Push loop** (retry 2/4/8/16 s on a race):
  `git fetch -q origin main && git rebase origin/main && git push origin HEAD:main`
- Commits: plain subject; a body that says why; last line exactly
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. No model names in files.
- The Mac is macOS: BSD `sed` (`sed -i ''`), no `timeout`, no GNU coreutils by
  default. Linux-only work goes to homebase or Actions (below).

## 2. Where things run

| Need | Where | How |
|---|---|---|
| edit, git, gh, node, python | the Mac | your worktree |
| compile + unit tests, Wine (`--navtest`), mingw, shellcheck, quick QEMU | **homebase** (owner's Linux server: Ubuntu 24.04, 8 vCPU, KVM, wine, mingw-w64, qemu, debootstrap, docker) | see §3 |
| image builds, KVM simulation suite, real Windows | **GitHub Actions** | push; trigger words §4 |

`gh` on the Mac is logged in as the owner (aarohkandy, admin of the repo):
`gh run list -R aarohkandy/v0.0.1-auros`, `gh run view <id> --log-failed`,
`gh run download <id>` (artifacts DO download here), `gh release ...`,
`gh run rerun <id> --failed`, `gh workflow run <file> --ref main` (works now:
main is the default branch). Some repository-settings API calls (Pages,
secrets) are refused by the agent permission system: those are owner clicks,
listed in `docs/OWNER-SETUP.md`; tell the lead, don't work around it.

## 3. homebase rules (it runs other people's services: be a good guest)

```sh
H="ssh -o ConnectTimeout=10 -i $HOME/.ssh/homebase_ed25519 azureuser@100.108.213.121"
$H 'mkdir -p ~/work/auros ~/logs'
rsync -az --delete --exclude .git --exclude out --exclude work \
  -e "ssh -i $HOME/.ssh/homebase_ed25519" ./ azureuser@100.108.213.121:work/auros/<id>/
$H 'cd ~/work/auros/<id> && nice -n 10 sh tools/bridgetest.sh'
```
- Your directory is `~/work/auros/<id>/` and nothing else. Delete big
  temporaries when done.
- Quick commands (< 10 min, < 4 GB RAM) may run directly over ssh with
  `nice -n 10`. Anything heavier: `export PATH=$HOME/.local/bin:$PATH; hb plan 8 1`
  then `hb run <GB> <hours> '<command>'` (the broker; do not wrap it in tmux/nohup;
  `hb log <id> -f`). Read `~/az_serv/CLAUDE.md` on the Mac before your first `hb run`.
- **Never `sudo` a job** (it leaves root-owned files). Things that need root
  (forge, mkimage, loop devices) or KVM run in a container instead:
  `docker run --rm --privileged --device /dev/kvm -v "$PWD":/w -w /w ubuntu:24.04 ...`
  with `--memory 12g`.
- **Disk budget (homebase filled to 100% once, 2026-10-23 08:20 UTC, and nearly took down
  other people's databases):** run `df -h /` before anything that writes more than 1 GB and
  stop if less than 30 GB would be left; keep your `~/work/auros/<id>/` under 3 GB (delete
  images and rootfs copies you are not using right now; re-download artifacts with `gh`);
  every test that makes temp files must clean them on EXIT, INT and TERM (`trap 'rm -rf "$T"'
  EXIT INT TERM`), and a killed `timeout` run must not leave multi-GB images in /tmp.
- Never touch other containers/services, never `box down`, never open ports.
- RAM is the constraint (shared). Check `hb status` before anything big.

## 4. CI on GitHub Actions (public repo: free, KVM, max 20 jobs at once)

Trigger words are read from the commit **SUBJECT** only:

| Word | Effect | Who may use it |
|---|---|---|
| (push touching build/ src/ rootfs/ profiles/ themes/ shells/) | `build.yml` builds the image + installer | anyone |
| `[build]` | force a build | anyone |
| `[test]` | KVM simulation suite on the newest build | anyone |
| `[release]` | build + tests + **prerelease** | lead, stream E |
| `[publish]` / `[publish RUN-ID]` | prerelease of an already-green build | lead, stream E |
| `[latest]` | promote to the public release (website button) | **lead only** |
| `[e2e]`, `[e2e:all]`, `[e2e:a,b]`, `[e2e:none]` | real-Windows journey | stream F, lead |
| `[profiles ...]`, `[othersys]`, `[othersys-skip]` | aux workflows | their streams |

- `windows.yml` runs on every push (installer on a real Windows Server VM).
- Every job has `timeout-minutes`; every `apt-get` is bounded (`timeout 300`,
  3 tries). Add both to anything new.
- Concurrency: one run per workflow per branch, queued, newest pending wins,
  `cancel-in-progress: false`. A "cancelled" run is usually just superseded.
- **Mutant branches** (deliberately weakened code pushed only to prove a check goes red on a real runner) are named `*mut-*`, carry no trigger words, are never merged, and are deleted right after their run. Releases are built only from `main`.
- **Never cancel a CI run you did not start** (another stream's gating results die with it). If a run looks stuck, tell the lead with its id.
- Evidence a person must see goes into a (pre)release asset or the job summary.

## 5. Save constantly (the session can end at any minute)

- Push every green step, at least every 15 minutes of work (owner: "push extremely frequently so I have a safe"). Small commits beat big ones.
- Keep `docs/handoff/streams/<ID>.md` current: goal, done (with commit
  hashes), half-done, next steps in order, traps, how to verify. Written for
  an engineer with zero context.
- Before any step longer than 10 minutes, append one line to the Progress
  log in `docs/handoff/SESSION-2026-10-23-B.md` (append only; on a rebase
  conflict keep both lines).

## 6. Culture (non-negotiable)

1. **Refuse first.** Anything uncertain on her disk is a refusal, made before
   anything changes. The machine can always go back to Windows. Never ship
   an unproven destructive path. One atomic commit point per destructive phase.
2. **Prove every check fails when the thing it checks is broken** (mutate,
   watch it go red, restore). A test that cannot fail is not a test.
3. **She never sees jargon.** `docs/EASY.md` is law: no terminal ever, text
   ≥18 px, targets ≥44 px, contrast ≥4.5:1, one obvious next step per screen,
   nothing she must type, nothing that asks her to understand a computer.
4. **Design bar: Apple-grade intentional simplicity.** One visual language
   across website, installer, AurOS and help (`docs/DESIGN.md`). Remove before
   you add. Every pixel and word must earn its place.
5. **Security bar**: no passwordless root-equivalent permission, ever; no
   telemetry; least privilege; plain warnings for scams.
6. Owner decisions D1-D11 (`docs/handoff/SESSION-2026-10-23.md`) are fixed:
   no code signing and no Microsoft services ever; Secure Boot stays on via
   Ubuntu's signed shim; GitHub Releases + Pages; Wi-Fi comes across with a
   visible pre-ticked box (D10); BitLocker paused for one restart, never
   decrypted (D11); encrypted disk + PIN (D9); other OSes = official images.
7. Any agent that opens a page with audio/video in a browser mutes it at once.
8. Report faithfully: what is proven (where), what is only simulated, what is
   untested. Never say "works" for something you did not run.

## 7. Who owns what (session B)

`docs/handoff/STREAMS.md` "Stream table" is still the ownership map. Session
B additions and changes are recorded in `docs/handoff/SESSION-2026-10-23-B.md`
under "Streams". If you need a change in another stream's file: smallest
possible edit, say so in the commit body, tell the lead in your report.
