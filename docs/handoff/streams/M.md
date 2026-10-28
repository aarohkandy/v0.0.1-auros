# Stream M — portability: no hard-coded repository

## Goal
The project moves between GitHub repositories. After a move nothing but
the clone address and branch (docs/MOVING.md) may need changing. Outside
`docs/`, nothing may name the repository by hand:
- workflows use `${{ github.repository }}` / `$GITHUB_REPOSITORY`;
- scripts ask `sh tools/repo.sh [repo|owner|name|site|host|releases]`
  (AUROS_REPO, else GITHUB_REPOSITORY, else the `origin` remote);
- the website is stamped at deploy (`tools/site/stamp.sh`, placeholders
  `@AUROS_REPO@` / `@AUROS_ADDRESS@`); door.js also derives the download
  address from its own `<owner>.github.io/<name>/`;
- the wizard's website address (`AUROS_SITE_W`) is baked by build/aurbridge
  from tools/repo.sh.

## State (2026-10-23, session B)
The move happened: ComputerDude771/auros-from-scratch (branch
claude/brave-hypatia-x72g1m, worked on in a container under /home/user)
→ **aarohkandy/v0.0.1-auros, branch `main`**, worked on from the owner's
Mac. `sh tools/repotest.sh` passes on the Mac and on homebase (dash).

### Done and pushed
- Session A: tools/repo.sh; workflows on `${{ github.repository }}`;
  build/aurbridge `AUROS_SITE_W`; website stamping + door.js; check.mjs,
  tools/e2e/e2e.py, tools/{e2e,othersys}/publish.sh derive the repository.
- `96d10bd`, `f25d67e` tools/workflows/*.js (the lead's Workflow scripts)
  run on the Mac: constants at the top of each (REPO = the lead's
  checkout, quoted because of the space; BR origin/main; SCRATCH; LOG =
  SESSION-2026-10-23-B.md; PUSH to main; TRAILER = the Co-Authored-By
  line only); fetches name their refspec (trap below). Every prompt was
  rendered with stub agents to check the templates evaluate.
- `45dbc93` README.md + MOVING.md one command → new repo + main (its
  update half pulls `"$R" "$B"` by name, tested from a clone whose origin
  is wrong); SECURITY.md links; shellpreview.c font path relative to the
  checkout. (tools/site/stamp.sh's `sed -i` and tools/e2e/README.md's
  `claude/**` were fixed by their streams at the same time.)
- `bdebb33` tools/repotest.sh + one step in tests.yml's unit job.
- `9c6c67e` MOVING.md (set-branches step, "what still points back"),
  OWNER-SETUP.md (new repo; main is already default), START-HERE §4.
- `2a0ac90` RELEASE.md, HANDOFF-RUNBOOK.md, SUPPORT.md, legal drafts.

### Deliberately still naming the old repository
- `windows.yml` / `e2e.yml` env: `PUBLISHED_URL`, `KNOWN_BAD_URL`,
  `IMAGE_PIECES_URL`, `INSTALLER_URL` — files published only there.
  Stream E replaces them; then delete the allow-grep in repotest.sh
  (marked `ponytail:`).
- The published installer ef903efd has the old image address baked in.
- History: docs/handoff, docs/issues, docs/results, docs/research,
  HANDOFF.md, the image-desktop links in TRY-IT.md / HANDOFF-RUNBOOK.md.

## Next steps
1. When stream E's pins land: remove the `grep -v` allow line in
   tools/repotest.sh, run it.
2. build/aptchannel derives the repository itself (GITHUB_REPOSITORY,
   else origin) and ignores AUROS_REPO; switching it to tools/repo.sh
   would make `AUROS_REPO=x/y` builds consistent. Low priority.
3. repotest runs in CI only when the unit job runs (`[test]`, or a
   release). It has not yet run on Actions; it ran on macOS and on
   Ubuntu 24.04 (dash, GNU grep) on homebase.

## Traps
- **The shared checkout's `remote.origin.fetch` still maps only
  `claude/brave-hypatia-x72g1m`**, so `git fetch origin main` does not
  move `origin/main` and the field manual's push loop rebases onto a
  stale tip (pushes then fail non-fast-forward, forever). Use
  `git fetch -q origin +main:refs/remotes/origin/main`. Root fix, once, by
  the lead: `git -C "<checkout>" remote set-branches origin main`.
- repotest's patterns: `/home/user/` (with the slash, so Firefox's
  `/home/username/` schema example is not a hit); `claude/<word>-<word>-<6>`
  for harness branch names (`.gitignore`'s `/.claude/` is not a hit).

## Verify
`sh tools/repotest.sh` (each of its checks was mutated red and restored:
old name in a script / workflow / C / website, another `_URL` key in
windows.yml, the current repository written into index.html, a stamp that
fills nothing, repo.sh without lower-casing, repo.sh without its name
check). `sh tools/repo.sh site` → https://aarohkandy.github.io/v0.0.1-auros/.
