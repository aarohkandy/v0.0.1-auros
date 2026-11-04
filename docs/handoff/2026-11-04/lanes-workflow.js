// REFERENCE ONLY: the script as it ran on 2026-11-04; nothing executes it.
// Before re-running it: check each report's branch against /^agent\/[A-Za-z0-9._-]+(-r[0-9]+)?$/ (or use
// the lane's own branch), pass agent reports as fenced untrusted data rather than instructions, and enforce
// no-push mechanically (e.g. git remote set-url --push origin no_push) instead of by prompt text.

export const meta = {
  name: 'auros-finish-lanes',
  description: 'Five AurOS lanes (put-back wording, retire fastpath lane, othersys powercut/budgie/cinnamon): implement in worktrees, adversarially review, fix',
  phases: [
    { title: 'Implement', detail: 'one agent per lane, own worktree, own agent/* branch' },
    { title: 'Attack', detail: 'a skeptic per lane tries to break the diff' },
    { title: 'Fix', detail: 'address surviving issues, re-attack once' },
  ],
}

const COMMON = `
You are working on AurOS, repo /Users/aaroh/auros (branch main of aarohkandy/v0.0.1-auros). You are in your own git worktree of it.
Read docs/handoff/SESSION-2026-11-03.md and docs/handoff/TRAPS.md before changing anything.
Hard rules:
- Commit your work on a branch named exactly as given below (git checkout -b <branch>). NEVER git push. NEVER trigger GitHub Actions (no gh workflow run, no gh run rerun, no gh release create/edit/delete). Reading runs/releases with gh is fine (repo aarohkandy/v0.0.1-auros).
- Every behaviour fix comes with a test that FAILS without the fix and passes with it; actually run it both ways and say so. Match surrounding code style, naming and comment density. Commit subjects look like the existing log ("area: what changes, in plain words"), no [release]/[build]/[othersys] trigger words. End each commit message with: Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
- Keep git light (pack.threads=1 is set). Put downloads in a scratch dir outside the repo (mktemp -d), never commit them.
- The remote Linux box "homebase" (ssh -i ~/.ssh/homebase_ed25519 azureuser@100.108.213.121) may be used for Linux/root-only tests in a privileged container (docker run --rm --privileged ubuntu:24.04 ...). Check free -h first, cap memory (--memory 8g), clean up after. Do NOT run full ISO VM installs there.
- The Mac is arm64 with no Docker.
- Smallest correct change at the root cause. Do not refactor unrelated code.
Your final answer is data for the lead: be exact about what changed, what you ran, and what you could NOT verify.
`

const LANES = [
  {
    key: 'putback-wording', branch: 'agent/putback-wording',
    prompt: `Lane: "Put Windows back" wording on a fast-path install.
Today the copy says "Anything you saved in AurOS goes with it" and tells her to copy files to a memory stick (src/aurshell/settings.c ~399 and ~789, website/guide.html ~62, website/install.html ~263 data-claim="putback:removes-saved", and the help page; tools/helptest and tools/site/check.mjs pin it on purpose as "the one truth"). But on a fast-path install (now the default installer) the handover carries her files back to Windows: rootfs/usr/... answer.sh + handover.sh save them to the handover volume, and Windows' put-back task (src/aurbridge/putback.ps1) copies them back and grows C:. The 11-03 session proved this on a BitLocker PC except the last step (fixed in 4e7957d5, being proven now).
Do this:
1. Establish exactly what is carried: which folders, any size limit, what happens when there is not enough room, what is NOT carried (programs, settings?), plain PC vs BitLocker PC, and whether the legacy non-fast-path install (AUROS_FASTPATH=0, still buildable) carries anything. Read the code, not the docs alone. Find out whether settings.c can tell at runtime which kind of install it is on (e.g. a file the installer leaves) - grep for it.
2. Change all of them TOGETHER so they say the same true thing, in the site's plain voice for a great-grandmother (short sentences, no jargon): settings.c (both strings; if it can detect the install kind, use the right wording for each, otherwise wording true for both), guide.html, install.html (rename the data-claim to match), the help page, and every check that pins the old wording (grep the old sentence and the claim id across tools/, website/, docs/, src/). If there is a limit or a failure case where files are NOT carried, say so honestly and keep the memory-stick advice for that case.
3. Run every check you touched: the helptest, settings/shell C tests that cover these strings (find them in tools/ and Makefile/build scripts), and node tools/site/check.mjs if node + playwright are available locally (if not, say so; also run any static site checks that work without a browser). The site has a 300 KB first-screen budget - do not add images.
Branch: agent/putback-wording`,
  },
  {
    key: 'retire-fastpath-lane', branch: 'agent/retire-fastpath-lane',
    prompt: `Lane: retire the redundant fast-path twin build and its e2e lane.
Facts: build/aurbridge defaults to AUROS_FASTPATH=1, and .github/workflows/build.yml's step "the same installer, built with AUROS_FASTPATH=1" rebuilds the installer with the SAME flags, compares the inner exe (tools/pe_payload.py), and when they differ publishes AurOS-Setup-fastpath.exe. They do differ every build (release v0.1.0-0e5cf01 has the asset), i.e. the build is simply not byte-reproducible, not a different configuration. release.yml then runs job e2e-fastpath (a whole Windows 11 journey, hours) on it plus a windows-job download check of it; promote does NOT need it. The 11-03 session notes its driver once read the website instead of Setup.
Do this:
1. Spend at most ~15 minutes finding WHY two identical builds differ (look at build/aurbridge's compiler/linker flags: PE timestamps, --no-insert-timestamp, __DATE__/__TIME__, random section ordering, embedded paths/temp names). Report the cause; if it is a one-flag fix, mention it but do not apply it in this lane unless trivial and clearly safe.
2. Delete the twin build step, the e2e-fastpath job, and every consumer of fastpath_sha256 / AurOS-Setup-fastpath.exe / present output (build.yml, release.yml publish/SHA256SUMS/notes/windows/upload/hardengate/promote, docs, tools, tests, website checks - grep the whole repo excluding .git). Keep the default-is-fastpath assertion if it guards something real (the grep that build/aurbridge defaults to fastpath) by moving it somewhere cheap, or drop it if another check already covers it - say which. Update the header comment block in release.yml. Keep accepting the historical [fastpath] subject marker only if removing it would break something; otherwise remove that dead case too.
3. Validate: YAML parses (python3 -c 'import yaml,sys; yaml.safe_load(open(f))' for each changed workflow; pip yaml may be needed - use a venv in a scratch dir), actionlint if available (brew list actionlint / or download nothing - just say if absent), and any repo tool that checks workflows (grep tools/ for workflow lint/tests e.g. tools/ci*). Make sure no job still "needs:" a deleted job and no expression references a removed output.
Branch: agent/retire-fastpath-lane`,
  },
  {
    key: 'othersys-powercut', branch: 'agent/othersys-powercut',
    prompt: `Lane: othersys (phase 2: official Ubuntu beside Windows) powercut variant fails.
Code: src/othersys/, tools/othersys/ (vmtest.py, selftest.py, checks.py), .github/workflows/othersys.yml, docs/OTHER-SYSTEMS.md (read "The flow" and "Next": "Power cut between curtin's NVRAM write and late").
Last real VM run: https://github.com/aarohkandy/v0.0.1-auros/actions/runs/41539330303 ; evidence in prerelease othersys-41539330303 (gh release download othersys-41539330303 -R aarohkandy/v0.0.1-auros -p 'powercut-*'). Failing checks:
 - "the cut came before the install finished": NO, "the install had already finished" (power on to the cut 475 s)
 - "at the cut: no BootNext": NO, BootNext 0004
 - "afterpowercut: the next start is Windows": NO, "WINDOWS-STAND-IN-BOOTED" never appeared on serial (power on to Windows Boot Manager 103.7 s)
 - passes: the installer put Ubuntu first and the guard put Windows back; BootOrder Windows first at the cut; Windows partitions byte-identical.
Read the serial logs, driver.log, steps.md and screenshots in the evidence to establish the real sequence. Determine for each failure whether it is a TEST bug (cut trigger timed wrong / wrong marker / wrong expectation) or a PRODUCT bug (a power cut at that point really leaves her PC not starting Windows next, e.g. a BootNext to a half-installed Ubuntu). A product bug where she is stuck in a half-installed Ubuntu is the important one: fix the product (the doc suggests the seed's grub.cfg or a guard), and make the test cut at the intended moment deterministically (on a serial marker, not a timer). Add selftest.py cases that fail without your fix. Run tools/othersys/selftest.py locally (python3; it needs no VM) both ways. You cannot run the KVM vmtest; the lead will run one othersys VM job for all lanes - so make your vmtest.py change as obviously right as you can and list exactly which VM checks you expect to flip.
Branch: agent/othersys-powercut`,
  },
  {
    key: 'othersys-budgie', branch: 'agent/othersys-budgie',
    prompt: `Lane: othersys (phase 2: official Ubuntu beside Windows) ubuntu-budgie variant fails.
Code: src/othersys/ (manifests, plan.py, othersys.py), tools/othersys/ (vmtest.py, selftest.py, checks.py), .github/workflows/othersys.yml (matrix), docs/OTHER-SYSTEMS.md.
Last real VM run: https://github.com/aarohkandy/v0.0.1-auros/actions/runs/41539330303 ; evidence in prerelease othersys-41539330303 (gh release download othersys-41539330303 -R aarohkandy/v0.0.1-auros -p 'ubuntu-budgie-*'). xubuntu, ntfs (Ubuntu), bitlocker pass in the same run. Budgie: the firmware started the ISO's shim from BootNext (pass), but "othersys: early: disk" never appeared on serial, the installer never finished (still running at the 4800 s deadline), no \\EFI\\ubuntu entry, "AurOS Installer" entry not removed. Screenshot ubuntu-budgie-install-241-4800s-end.png shows where it sat.
Establish why: does the Ubuntu Budgie 26.04 ISO boot casper with the same kernel cmdline/grub paths the seed's grub.cfg assumes, does it ship subiquity/autoinstall (or a different installer such as Calamares or the desktop bootstrap that ignores autoinstall), does cloud-init find the CIDATA seed, is the live session waiting at a GUI prompt? Read the serial log and look at the screenshot. Check facts against the real ISO where useful (you may read its grub.cfg / casper / manifest by downloading only what you need - the ISO's .manifest or list file from the mirror, not the whole 4+ GB ISO, unless genuinely necessary; scratch dir outside the repo).
Then fix the root cause in plan/seed/manifest if Budgie can be driven unattended the documented way; if Budgie 26.04 genuinely cannot be autoinstalled the way the others are, remove it from the offered systems and the matrix cleanly, record why in docs/OTHER-SYSTEMS.md (agent decision, reversible), and make sure nothing else lists it. Add selftest.py coverage that fails without the change. Run tools/othersys/selftest.py both ways. You cannot run the KVM vmtest; the lead runs one VM job for all lanes - say exactly which checks you expect to flip.
Branch: agent/othersys-budgie`,
  },
  {
    key: 'othersys-cinnamon', branch: 'agent/othersys-cinnamon',
    prompt: `Lane: othersys (phase 2: official Ubuntu beside Windows) ubuntucinnamon variant fails after a good install.
Code: src/othersys/ (othersys.py helper, the boot unit auros-othersys-boot.service, autologin setup, plan.py, manifests), tools/othersys/ (vmtest.py, selftest.py, checks.py), docs/OTHER-SYSTEMS.md ("The flow": first boot deletes AUROS-OS-ISO and grows the root, then the hold: BootNext = ubuntu; zenity question at login).
Last real VM run: https://github.com/aarohkandy/v0.0.1-auros/actions/runs/41539330303 ; evidence in prerelease othersys-41539330303 (gh release download othersys-41539330303 -R aarohkandy/v0.0.1-auros -p 'ubuntucinnamon-*'). Install phase passes entirely. firstboot: "OTHERSYS-TEST SESSION-UP" never on serial (1200 s), the ISO partition is still there, root not grown, BootNext None (expected the hold 0004). confirm: started and took the answer but BootOrder not Ubuntu-first (0002,0000,0001,0004). afteryes: no session. ntfs (plain Ubuntu) and xubuntu pass every phase in the same run.
Hypotheses to test, not assume: Ubuntu Cinnamon uses LightDM (not GDM) so autologin / the session-up marker / the zenity autostart are configured for the wrong display manager; the boot unit is ordered after/wanted by something Cinnamon lacks; the "yes" path writes BootOrder via a path that failed silently. Read the serial logs (firstboot, confirm, afteryes), the helper code and units, and compare with how xubuntu (also LightDM) passes - that comparison is the strongest clue. Fix the root cause in the product (not by loosening checks). Add selftest.py coverage that fails without the fix. Run tools/othersys/selftest.py both ways. You cannot run the KVM vmtest; the lead runs one VM job for all lanes - list exactly which checks you expect to flip.
Branch: agent/othersys-cinnamon`,
  },
]

const IMPL_SCHEMA = {
  type: 'object',
  properties: {
    branch: { type: 'string' },
    commits: { type: 'array', items: { type: 'string' } },
    root_cause: { type: 'string' },
    changed_files: { type: 'array', items: { type: 'string' } },
    tests_run: { type: 'array', items: { type: 'string' }, description: 'command -> result, incl. the run without the fix' },
    unverified: { type: 'array', items: { type: 'string' } },
    expected_ci_flips: { type: 'array', items: { type: 'string' } },
    notes: { type: 'string' },
  },
  required: ['branch', 'commits', 'root_cause', 'changed_files', 'tests_run', 'unverified'],
}
const ATTACK_SCHEMA = {
  type: 'object',
  properties: {
    verdict: { type: 'string', enum: ['sound', 'needs_fix'] },
    issues: { type: 'array', items: { type: 'object', properties: {
      severity: { type: 'string', enum: ['blocker', 'major', 'minor'] },
      file: { type: 'string' }, what: { type: 'string' }, evidence: { type: 'string' },
    }, required: ['severity', 'what', 'evidence'] } },
  },
  required: ['verdict', 'issues'],
}

function attackPrompt(lane, impl, round) {
  return `${COMMON}
You are an ADVERSARIAL reviewer (round ${round}). Another agent claims to have finished this lane:
---
${lane.prompt}
---
Their report: ${JSON.stringify(impl)}
Review branch ${impl.branch}: git log main..${impl.branch} and git diff main...${impl.branch} (run from /Users/aaroh/auros or your worktree; the branch is in the shared repo). Try hard to break it:
- Is the stated root cause actually supported by the evidence (re-read the logs/code yourself)? Or is it a plausible story?
- Does the new test really fail without the fix? Check it out without the fix (git stash / checkout main -- <file>) and run it.
- Does the change regress anything else (other variants, other callers, other checks that pin the same strings/outputs)? grep for every consumer.
- Is it true for her (the user) in every case the product supports, not just the tested one?
- Over-engineering or unrelated churn?
Only report issues you can back with concrete evidence (a command and its output, a file:line). Do not modify the branch. verdict=sound if nothing blocker/major survives your own scrutiny.`
}

function fixPrompt(lane, impl, attack, round) {
  return `${COMMON}
${lane.prompt}
---
A previous agent did this lane on branch ${impl.branch} (report: ${JSON.stringify(impl)}).
An adversarial reviewer found: ${JSON.stringify(attack.issues)}
In your worktree: git checkout -B ${lane.branch}-r${round} ${impl.branch} . Address every blocker and major issue that is real (verify each first; if you disagree with one, say why with evidence instead of changing code). Run the tests again both ways. Commit on ${lane.branch}-r${round}. Report with branch = ${lane.branch}-r${round}.`
}

const results = await pipeline(
  LANES,
  lane => agent(`${COMMON}\n${lane.prompt}`, { label: `impl:${lane.key}`, phase: 'Implement', schema: IMPL_SCHEMA, isolation: 'worktree' }),
  async (impl, lane) => {
    if (!impl) return { lane: lane.key, impl: null, rounds: [] }
    let cur = impl
    const rounds = []
    for (let round = 1; round <= 2; round++) {
      const atk = await agent(attackPrompt(lane, cur, round), { label: `attack:${lane.key}:${round}`, phase: 'Attack', schema: ATTACK_SCHEMA })
      rounds.push({ round, attack: atk })
      if (!atk || atk.verdict === 'sound' || !atk.issues.some(i => i.severity !== 'minor')) break
      const fixed = await agent(fixPrompt(lane, cur, atk, round), { label: `fix:${lane.key}:${round}`, phase: 'Fix', schema: IMPL_SCHEMA, isolation: 'worktree' })
      if (fixed) { cur = fixed; rounds[rounds.length - 1].fix = fixed }
    }
    return { lane: lane.key, impl: cur, rounds }
  },
)
return results
