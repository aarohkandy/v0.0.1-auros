export const meta = {
  name: 'auros-gate-status',
  description: 'Which v0.1 gate items (safety/security highs, launch blockers, design P0s) are really fixed with a failing-when-broken test on main; adversarially checked',
  phases: [
    { title: 'Audit', detail: 'three auditors: safety, security, design/EASY' },
    { title: 'Refute', detail: 'one skeptic per auditor attacks every fixed claim' },
  ],
}

const REPO = '/Users/aaroh/Desktop/auros v0.0.1/v0.0.1-auros'
const GIT = `git -C "${REPO}"`
const CTX = `You audit AurOS (an Ubuntu-based desktop installed beside Windows by a Windows exe; for non-technical people) for its v0.1 release gate. Repository checkout ${REPO} (path has a space; quote it). READ-ONLY: first run \`${GIT} fetch -q origin\`, then read with \`${GIT} show origin/main:<path>\`, \`${GIT} grep -n <pat> origin/main -- <paths>\`, \`${GIT} log origin/main --oneline -- <path>\`, \`${GIT} ls-tree -r --name-only origin/main\`. Never edit, checkout, commit or push. CI evidence: \`gh run list -R aarohkandy/v0.0.1-auros\`, \`gh run view <id> -R aarohkandy/v0.0.1-auros\` (read-only). A gate item counts as CLOSED only if (a) the code on origin/main fixes it (cite file:line) AND (b) a test exists that would go red if the fix were reverted (cite the test file and check name; say whether that test runs in a CI workflow, cite .github/workflows file) . Otherwise it is OPEN or PARTIAL. Also say where it is proven: real-Windows e2e, KVM/VM, unit, or never run. Be concrete; no padding.`

const ITEM = {
  type: 'object',
  properties: {
    id: { type: 'string' }, title: { type: 'string' },
    status: { type: 'string', enum: ['closed', 'partial', 'open', 'owner-decision'] },
    fix_evidence: { type: 'string' }, test_evidence: { type: 'string' },
    proven_where: { type: 'string' }, what_is_left: { type: 'string' }, owner: { type: 'string' },
  },
  required: ['id', 'title', 'status', 'fix_evidence', 'test_evidence', 'proven_where', 'what_is_left'],
}
const LIST = { type: 'object', properties: { items: { type: 'array', items: ITEM } }, required: ['items'] }
const VERD = {
  type: 'object',
  properties: { challenges: { type: 'array', items: { type: 'object', properties: {
    id: { type: 'string' }, claimed: { type: 'string' }, verdict: { type: 'string', enum: ['upheld', 'downgrade'] },
    corrected_status: { type: 'string' }, reason: { type: 'string' } }, required: ['id', 'verdict', 'reason'] } } },
  required: ['challenges'],
}

const AREAS = [
  { key: 'safety', q: 'Every finding in docs/safety/FINDINGS.md rated blocker or high (S-01..S-12), plus medium S-20 (partly fixed) and S-22 (open). Verify each status-table claim yourself.' },
  { key: 'security', q: 'docs/security/FINDINGS.md: every High (SEC-001..SEC-008) and every item listed as a launch blocker (SEC-001..008, 014, 029). Note SEC-008 (no disk encryption) and SEC-029 (no lock screen) depend on stream S4, whose 41 commits may be held unpushed (look at docs/handoff/SESSION-2026-10-23-B.md and docs/handoff/streams/S4.md); report what is on main only. Also the security doc candidate about Mozilla apt origin in unattended-upgrades (line ~158): check rootfs for 53auros-updates Origins-Pattern and packages.mozilla.org.' },
  { key: 'design', q: 'The design P0s in docs/DESIGN-DIRECTION.md (find the P0 list) and docs/EASY.md hard rules (text >=18px, targets >=44px, contrast >=4.5:1, no jargon, one next step) across website/, src/aurbridge (wizard), src/aurshell, themes/. Known open bug: themes on() in src/aurora/aurora picks dark text on the dark green accent in the light default theme (GNOME Software Install button unreadable) - confirm whether fixed on main yet. Check whether any test enforces contrast for third-party app theming.' },
]

const results = await pipeline(AREAS,
  a => agent(`${CTX}\n\nYour area: ${a.key}. ${a.q}\nReturn one item per gate item.`, { label: `audit:${a.key}`, phase: 'Audit', schema: LIST }),
  (audit, a) => {
    if (!audit) return null
    const claims = audit.items.filter(i => i.status === 'closed' || i.status === 'partial')
    if (!claims.length) return { area: a.key, items: audit.items, challenges: [] }
    return agent(`${CTX}\n\nYou are a SKEPTIC. An auditor claims these ${a.key} gate items are closed or partial. For EACH, try hard to REFUTE the claim: is the cited fix really on origin/main and does it cover the whole finding (not just one path)? Does the cited test really exist, really go red without the fix (read it: does it assert the behaviour or merely run), and does it run in CI? Default to downgrade if you cannot confirm.\n\nClaims:\n${JSON.stringify(claims, null, 1)}`, { label: `refute:${a.key}`, phase: 'Refute', schema: VERD })
      .then(v => ({ area: a.key, items: audit.items, challenges: v ? v.challenges : [] }))
  })
return results.filter(Boolean)