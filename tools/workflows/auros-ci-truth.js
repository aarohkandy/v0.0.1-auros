export const meta = {
  name: 'auros-ci-truth',
  description: 'Find CI jobs whose skip filters hide a red, jobs never run, and gating tests missing from the release/promote gate; dig each and verify',
  phases: [
    { title: 'Map', detail: 'every job: triggers, skip filters, gate membership, last REAL verdict' },
    { title: 'Dig', detail: 'one digger per suspicious job: cause and owner' },
    { title: 'Critic', detail: 'what did the map miss' },
  ],
}

const R = 'aarohkandy/v0.0.1-auros'
const REPO = '/Users/aaroh/Desktop/auros v0.0.1/v0.0.1-auros'
const GIT = `git -C "${REPO}"`
const CTX = `AurOS CI lives in GitHub Actions on ${R}, branch main. Checkout ${REPO} (path has a space: quote it). READ-ONLY: \`${GIT} fetch -q origin\` then \`${GIT} show origin/main:.github/workflows/<f>.yml\`, \`${GIT} ls-tree --name-only origin/main .github/workflows/\`, \`${GIT} show origin/main:docs/RELEASE.md\`, \`${GIT} show origin/main:docs/handoff/FIELD-MANUAL.md\`. Runs: \`gh run list -R ${R} -w <workflow> -L 100 --json databaseId,headSha,conclusion,status,createdAt\`, \`gh run view <id> -R ${R} --json jobs\`, \`gh run view <id> -R ${R} --log-failed | tail -80\`. Never cancel, rerun, edit, commit or push. Context: a job that is SKIPPED by a path/diff filter shows green at the workflow level; E just found harden.yml's boot job had been red for 100 runs while every harden run read green, and that harden was not part of the release gate (releasegate reads build/unit/tests/powercut). Find every other case like it.`

const JOB = { type: 'object', properties: {
  workflow: { type: 'string' }, job: { type: 'string' },
  runs_when: { type: 'string' }, skip_can_hide_red: { type: 'boolean' },
  in_release_gate: { type: 'boolean' }, in_promote_gate: { type: 'boolean' },
  last_real_conclusion: { type: 'string' }, last_real_run: { type: 'string' }, last_real_sha: { type: 'string' },
  suspicious: { type: 'boolean' }, why: { type: 'string' } },
  required: ['workflow', 'job', 'runs_when', 'skip_can_hide_red', 'in_release_gate', 'last_real_conclusion', 'suspicious', 'why'] }
const MAP = { type: 'object', properties: { jobs: { type: 'array', items: JOB }, gate_notes: { type: 'string' } }, required: ['jobs', 'gate_notes'] }
const DIG = { type: 'object', properties: {
  workflow: { type: 'string' }, job: { type: 'string' }, real_problem: { type: 'boolean' },
  failing_check: { type: 'string' }, cause: { type: 'string' }, product_or_test: { type: 'string', enum: ['product', 'test', 'infra', 'none', 'unknown'] },
  owner_stream: { type: 'string' }, evidence: { type: 'string' }, recommended_fix: { type: 'string' }, should_gate_release: { type: 'boolean' } },
  required: ['workflow', 'job', 'real_problem', 'cause', 'product_or_test', 'evidence', 'recommended_fix', 'should_gate_release'] }

phase('Map')
const map = await agent(`${CTX}\n\nMap EVERY job in EVERY workflow file. For each: when it runs (triggers, if: conditions, path/diff filters), whether a skip can make a red invisible, whether the release gate (release.yml / releasegate) and the promote ([latest]) path require it, and its LAST REAL (not skipped/cancelled) conclusion with run id and sha (look back up to 100 runs). Mark suspicious = last real is failure, or it has never really run in the last 100, or it is a meaningful safety/security test that no gate requires. Stream ownership hints: docs/handoff/STREAMS.md and SESSION-2026-10-23-B.md.`, { label: 'map', phase: 'Map', schema: MAP })
if (!map) return { error: 'map failed' }
const sus = map.jobs.filter(j => j.suspicious)
const CAP = 7
if (sus.length > CAP) log(`digging ${CAP} of ${sus.length} suspicious jobs; not dug: ${sus.slice(CAP).map(j => j.workflow + '/' + j.job).join(', ')}`)

const digs = await parallel(sus.slice(0, CAP).map(j => () => agent(`${CTX}\n\nDig this suspicious job and say what is really wrong. Read its last real failing log, the test source, and the product code the failing check covers. Decide product bug vs test bug vs infra; name the owning stream; say whether it should gate a release. Be concrete with file:line and run/job ids.\n\nJob: ${JSON.stringify(j)}`, { label: `dig:${j.workflow}/${j.job}`, phase: 'Dig', schema: DIG })))

phase('Critic')
const critic = await agent(`${CTX}\n\nA mapper and diggers produced this. Find what they MISSED: workflows or jobs not mapped, gate paths misread (read release.yml and the promote path yourself), a skipped job whose last real verdict was misread, a continue-on-error or '|| true' that swallows a red, tests listed in docs as gating that no workflow runs. Return a short plain list with evidence.\n\nMAP:\n${JSON.stringify(map).slice(0, 60000)}\n\nDIGS:\n${JSON.stringify(digs.filter(Boolean)).slice(0, 40000)}`, { label: 'critic', phase: 'Critic' })
return { map, digs: digs.filter(Boolean), critic, not_dug: sus.slice(CAP).map(j => j.workflow + '/' + j.job) }