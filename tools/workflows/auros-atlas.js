export const meta = {
  name: 'auros-atlas',
  description: 'Map every AurOS subsystem (expert brief per area), then write docs/ATLAS.md and docs/handoff/CURRENT-STATE.md',
  phases: [{ title: 'Read', detail: 'one expert reader per subsystem' }, { title: 'Write', detail: 'ATLAS + CURRENT-STATE, pushed' }],
}
const RO = '/private/tmp/claude-501/-Users-aaroh-Desktop-auros-v0-0-1/914a7092-8dd8-4369-bb66-bf5c4e2ea1f2/scratchpad/ro-main'
const M = '/Users/aaroh/Desktop/auros v0.0.1/v0.0.1-auros'
const S = '/private/tmp/claude-501/-Users-aaroh-Desktop-auros-v0-0-1/914a7092-8dd8-4369-bb66-bf5c4e2ea1f2/scratchpad'
const CTX = `You are mapping AurOS for the agents who will build and ship it. AurOS: a Linux desktop (Ubuntu 24.04 base, own shell aurshell) that a non-technical great-grandmother installs beside Windows from one downloaded .exe (AurBridge), one restart into a staging initramfs (aurstage) which writes AurOS, then AurOS's first boot (aurfirst) asks "does it work?"; "Put Windows back" restores. Read-only checkout of the newest main: ${RO} (read files there; NEVER edit, commit or check out anything). Start with ${RO}/START-HERE.md and ${RO}/docs/handoff/FIELD-MANUAL.md for context (5 min), then read YOUR area deeply: actual source, not just docs. Be precise: cite file:line. Distinguish proven on real Windows / proven in KVM simulation / unit-tested / untested. Output markdown, max 1300 words, with exactly these headings: "Purpose", "How it works (the real flow, with file:line)", "Invariants that protect her data and boot (file:line; how enforced)", "Tests that prove it (name, what it checks, where it runs)", "Gaps, risks and things that look wrong (evidence)", "Where to change what (index for a new engineer)".`
const AREAS = [
  ['aurbridge', 'The Windows installer: src/aurbridge/** (wizard.c, preflight.c, fix_win.c, phases.c/h, plat_win.c, plat_sim.c, main.c, download/getimage, inflate.c, format.c, sbdb), build/aurbridge, tools/pe_payload.py, tools/manifesttest.sh, tools/bridgetest.sh, tools/wizard/'],
  ['aurstage', 'The staging environment after the restart: src/aurstage/** (install.c, restore, commit, gpt.c, plan.c, shrink.c, probe.c, journal, screen), build/staging, src/aurscreen, tools/installtest.sh, nosticktest.sh, powercuttest.sh, putbacktest.sh, committest, gpttest'],
  ['boot', 'Boot chain and first boot: src/aurfirst/**, rootfs/usr/lib/auros/ (bootchain, grub.cfg.in, hold, putback), build/shimpick, src/recovery/**, BootNext/BootOrder writers, firstboottest/choicesboottest/bootchaintest/bootupdatetest, docs/STAGEC.md'],
  ['desktop', 'The desktop: src/aurshell/** (main.c, apps.c, store, settings, welcome), src/aurwl/**, src/common/** (font rasterizer, PNG, theme parser, wallpaper), src/aurora (theme engine), themes/**, shells/**, docs/SHELLS.md, docs/THEMING.md, docs/DESIGN.md, shell tests (storetest, choicestest, looktest, sessionenvtest)'],
  ['image', 'Building the OS image: build/forge, build/mkimage, build/all, build/harden, profiles/*.profile, rootfs/** (outside boot), pkg/**, what packages ship, Flathub setup, Firefox route, image size, hardening (S1), update channel (docs/security/UPDATES.md)'],
  ['ci', 'CI and test tooling: .github/workflows/*.yml (every job, triggers, gates, concurrency, timeouts, what each proves), tools/e2e/** (the real-Windows journey driver), tools/README.md, docs/results/, docs/HANDOFF-RUNBOOK.md, docs/RELEASE.md, release flow'],
  ['front', 'What she sees outside the OS: website/** (every page, door.js, how the download works on each browser/OS), tools/site/check.mjs, rootfs/usr/share/auros/help/**, docs/TRY-IT.md, docs/SIGNING.md (unsigned download warnings), docs/EASY.md, SECURITY.md, docs/SUPPORT.md, docs/legal/'],
  ['rest', 'Ferry and the rest: src/ferry/** (migration of her files), src/othersys/** + tools/othersys + docs/OTHER-SYSTEMS.md (phase 2), docs/security/** (PLAN, THEFT, UPDATES), docs/research/** (red-team, journey audit, every-machine), docs/PLAN.md, docs/RELEASE.md'],
]
phase('Read')
const briefs = await parallel(AREAS.map(([k, what]) => () =>
  agent(`${CTX}\n\nYOUR AREA (${k}): ${what}`, { label: `read:${k}`, phase: 'Read', effort: 'high' })
    .then(t => ({ k, t }))))
const ok = briefs.filter(Boolean)
log(`${ok.length}/${AREAS.length} area briefs done`)
phase('Write')
const body = ok.map(b => `\n\n=== AREA: ${b.k} ===\n${b.t}`).join('')
const res = await agent(`You are the technical writer for AurOS. Below are expert briefs of every subsystem. Produce TWO files and push them.

1. docs/ATLAS.md — "the expert's map of AurOS": one page of the whole system first (the journey from website to her desktop, as a numbered flow naming the component at each step), then one section per area (keep every file:line, every invariant, every test; cut repetition), then a combined "Top risks" table (risk, evidence, area, severity: blocker/high/medium). Max ~7000 words. Plain, exact, no padding.
2. docs/handoff/CURRENT-STATE.md — under 800 words, headings exactly: "Newest", "Works and is proven (how)", "Broken, unproven or risky", "In flight right now", "Next steps, in order", "Needs the owner". For in-flight state also read: \`gh run list -R aarohkandy/v0.0.1-auros -L 30\`, \`gh release list -R aarohkandy/v0.0.1-auros\`, ${RO}/docs/handoff/SESSION-2026-10-23-B.md and docs/handoff/streams/*.md (fetch the newest: \`git -C "${M}" fetch -q origin main && git -C "${M}" show origin/main:docs/handoff/SESSION-2026-10-23-B.md\`).

Steps: \`git -C "${M}" fetch -q origin main; git -C "${M}" worktree remove --force ${S}/wt-atlas 2>/dev/null; git -C "${M}" worktree add --detach ${S}/wt-atlas origin/main\`; write both files there; in START-HERE.md add one line right under the MOVED note: "> **Expert map: [docs/ATLAS.md](docs/ATLAS.md). State right now: [docs/handoff/CURRENT-STATE.md](docs/handoff/CURRENT-STATE.md).**"; append to docs/handoff/SESSION-2026-10-23-B.md's Progress log one line "- <HH:MM UTC> ATLAS.md + CURRENT-STATE.md written (workflow auros-atlas)."; commit (subject "handoff: the expert map (ATLAS) and the current state", short body, last line exactly "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"); push with \`git fetch -q origin main && git rebase origin/main && git push origin HEAD:main\` (retry up to 4 times, 2/4/8/16 s; on a conflict in the session log keep both lines). Then \`git -C "${M}" worktree remove --force ${S}/wt-atlas\`.

Return: the commit hash, then the "Top risks" table verbatim, then "Broken, unproven or risky" verbatim.

BRIEFS:${body}`, { label: 'write', phase: 'Write', effort: 'high' })
return res