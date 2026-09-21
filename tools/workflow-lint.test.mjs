#!/usr/bin/env node --test
// workflow-lint — the three GitHub Actions mistakes whose symptom is silence, tested both ways.
//
//   node --test tools/workflow-lint.test.mjs
//
// EACH RULE HERE EXISTS BECAUSE IT ALREADY COST US SOMETHING:
//
//   1. `${{ }}` unquoted inside a YAML FLOW MAPPING. The braces are structural, the document is
//      invalid, GitHub rejects the whole workflow — and the symptom is not an error. It is a run
//      that appears named by its FILE PATH instead of by its `name:`, with no jobs and no log.
//   2. No pipefail. A step that pipes through `tail` takes the LAST command's exit status, so a
//      FAILING bootc-image-builder scored as a PASSING step and the workflow reported success
//      while producing no image (DECISIONS.md D19). The general rule D19 draws from it is the one
//      governing this whole file: a step that cannot fail is not a check.
//   3. No top-level `name:`. Which is also what a rejected workflow looks like, so a workflow with
//      no name is a workflow whose parse failures are indistinguishable from its normal appearance.
//   4. `ubuntu-latest` (D23). It migrates to 26.04 from 2026-10-19; SYSTEM-REVIEW H21 found
//      propagate.yml on it with nothing enforcing the decision.
//
// THE DISCIPLINE. For every "must fire" there is a near-identical "must not fire" that differs by
// one character — a quote, a brace, a line. A linter that flagged every `${{ }}` would satisfy every
// positive test in this file and be useless, because `${{ }}` in block style is how workflows are
// written. The negative half is what makes the positive half mean something.

import { test, describe } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, writeFileSync, rmSync, readdirSync, readFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'
import { execFileSync } from 'node:child_process'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = join(HERE, '..')
const LINT = join(HERE, 'workflow-lint.mjs')

let ROOT
const root = () => (ROOT ??= mkdtempSync(join(tmpdir(), 'auros-wflint-')))
let seq = 0

/** Run the real linter over a tree containing the given workflow files. */
function lint (files) {
  const dir = join(root(), `t-${seq++}`)
  const wf = join(dir, '.github', 'workflows')
  mkdirSync(wf, { recursive: true })
  for (const [name, text] of Object.entries(files)) writeFileSync(join(wf, name), text)
  try {
    const out = execFileSync(process.execPath, [LINT, dir], { encoding: 'utf8', stdio: 'pipe' })
    return { exit: 0, out, problems: [] }
  } catch (e) {
    const out = String(e.stdout ?? '') + String(e.stderr ?? '')
    return { exit: e.status ?? -1, out }
  }
}
const one = (text, name = 'w.yml') => lint({ [name]: text })

const clean = (res, why) =>
  (assert.equal(res.exit, 0, `expected NO findings (${why}) but workflow-lint exited ${res.exit}:\n${res.out}`), res)
const flags = (res, why) => {
  assert.notEqual(res.exit, 0, `expected a FINDING (${why}) but workflow-lint exited 0:\n${res.out}`)
  return res
}

// A minimal workflow that is correct in every respect this linter checks. Every positive test below
// introduces exactly one defect into this text, so a finding is attributable to that defect alone.
const OK = `name: build-base
on:
  workflow_dispatch:

defaults:
  run:
    shell: bash -euo pipefail {0}

jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - name: build
        run: |
          echo building
`

// ─────────────────────────────────────────────────────────────────────────────────────────────────
describe('the control — a correct workflow produces no findings', () => {
  test('the baseline workflow is clean', () => {
    const r = clean(one(OK), 'a workflow with a name, pipefail defaults and no flow-mapping expressions')
    assert.match(r.out, /1 workflow\(s\), no problems/)
  })

  test('a block-style ${{ }} is clean — the overwhelmingly common, correct shape', () => {
    // If this ever fires, the linter has become a blanket ban on expressions and everybody will
    // switch it off, which costs more than the bug it was written for.
    clean(one(OK.replace('echo building', 'echo "${{ github.sha }}"')), 'block-style expression in a run')
    clean(one(OK.replace('      - uses: actions/checkout@v4',
      '      - uses: actions/checkout@v4\n        with:\n          ref: ${{ github.sha }}')),
    'block-style expression under with:')
  })
})

// ─────────────────────────────────────────────────────────────────────────────────────────────────
describe('rule 1 — ${{ }} unquoted inside a YAML flow mapping', () => {
  const FLOW_BAD = [
    ['with, one key',        '        with: { name: art-${{ matrix.profile }} }'],
    ['with, two keys',       '        with: { name: art-${{ matrix.profile }}, path: out/ }'],
    ['env flow mapping',     '        env: { SHA: ${{ github.sha }} }'],
    ['expression in the value position of an inputs map',
      '        with: { recipe: ${{ inputs.recipe }}, base_digest: abc }'],
    ['an unterminated flow mapping continued on the next line',
      '        with: { name: art-${{ matrix.profile }},'],
  ]

  for (const [label, line] of FLOW_BAD) {
    test(`FIRES on ${label}`, () => {
      const text = OK.replace('        run: |\n          echo building\n', `${line}\n`)
      const r = flags(one(text), label)
      assert.match(r.out, /flow mapping/, `the message must explain the mechanism. Got:\n${r.out}`)
      assert.match(r.out, /named by its file path/,
        'the message must describe the SYMPTOM, because the symptom is what somebody will be staring at')
    })
  }

  const FLOW_OK = [
    ['double-quoted expression in a flow mapping',
      '        with: { name: "art-${{ matrix.profile }}" }'],
    ['single-quoted expression in a flow mapping',
      "        with: { name: 'art-${{ matrix.profile }}' }"],
    ['a flow mapping with no expression in it at all',
      '        with: { name: artifact, path: out/ }'],
    ['quoted expression alongside an unquoted literal',
      '        with: { name: "art-${{ matrix.profile }}", path: out/ }'],
    ['an expression in block style under with:',
      '        with:\n          name: art-${{ matrix.profile }}'],
  ]

  for (const [label, line] of FLOW_OK) {
    test(`QUIET on ${label}`, () => {
      const text = OK.replace('        run: |\n          echo building\n', `${line}\n`)
      clean(one(text), label)
    })
  }

  test('the quoted and unquoted forms differ by exactly two characters, and the verdicts differ', () => {
    // The pair, stated as one test, because this is the entire rule. If somebody ever "simplifies"
    // the quote-stripping, one of these two assertions goes red immediately.
    const bad = OK.replace('        run: |\n          echo building\n',
      '        with: { name: art-${{ matrix.profile }} }\n')
    const good = OK.replace('        run: |\n          echo building\n',
      '        with: { name: "art-${{ matrix.profile }}" }\n')
    flags(one(bad), 'unquoted')
    clean(one(good), 'quoted')
    assert.equal(good.length - bad.length, 2, 'the two fixtures must differ by exactly the two quote characters')
  })
})

// ─────────────────────────────────────────────────────────────────────────────────────────────────
describe('rule 2 — pipefail (D19)', () => {
  test('FIRES on a workflow with no pipefail anywhere', () => {
    const noDefaults = OK.replace(/defaults:\n  run:\n    shell: bash -euo pipefail \{0\}\n\n/, '')
    const r = flags(one(noDefaults), 'no pipefail')
    assert.match(r.out, /no pipefail default/)
    assert.match(r.out, /tail|head|tee/, 'the message must name the shape that caused the original bug')
  })

  test('QUIET when the shell is declared per-step instead of in defaults', () => {
    const perStep = OK
      .replace(/defaults:\n  run:\n    shell: bash -euo pipefail \{0\}\n\n/, '')
      .replace('        run: |', '        shell: bash -euo pipefail {0}\n        run: |')
    clean(one(perStep), 'per-step pipefail shell')
  })

  test('QUIET for a pwsh workflow — Windows runners are not bash (D27, Gate 3)', () => {
    const pwsh = `name: gate3
on: { workflow_dispatch: null }
jobs:
  run:
    runs-on: windows-latest
    steps:
      - shell: pwsh
        run: Write-Host "hi"
`
    clean(one(pwsh), 'a pwsh workflow')
  })

  test('FIRES on a `defaults:` block that sets no pipefail — the key existing is not enough', () => {
    // This was a false negative until 2026-09-20: the check short-circuited on the mere presence of
    // a top-level `defaults:` key, so a block that only set a working-directory exempted the whole
    // file from the D19 rule. The linter now asks whether a pipefail shell is actually declared.
    const hollow = OK.replace('defaults:\n  run:\n    shell: bash -euo pipefail {0}',
      'defaults:\n  run:\n    working-directory: ./src')
    const r = flags(one(hollow), 'a defaults block with no shell at all')
    assert.match(r.out, /no pipefail default/)
  })

  test('FIRES on a `defaults:` block whose shell has no pipefail in it', () => {
    flags(one(OK.replace('shell: bash -euo pipefail {0}', 'shell: sh {0}')), 'shell: sh')
    flags(one(OK.replace('shell: bash -euo pipefail {0}', 'shell: bash -e {0}')), 'errexit without pipefail')
  })

  test('QUIET on the control once more, so the two tests above are not a linter that now fires on every defaults block', () => {
    clean(one(OK), 'the baseline, whose defaults block DOES set pipefail')
  })
})

// ─────────────────────────────────────────────────────────────────────────────────────────────────
describe('rule 3 — a top-level name', () => {
  test('FIRES on a workflow with no top-level name:', () => {
    const noName = OK.replace(/^name: build-base\n/, '')
    const r = flags(one(noName), 'no name')
    assert.match(r.out, /no top-level `name:`/)
    assert.match(r.out, /list this workflow by its file path/,
      'the message must connect the missing name to the parse-failure symptom, which is the reason the rule exists')
  })

  test('QUIET when a JOB is named but the workflow is too — the name that matters is the top-level one', () => {
    clean(one(OK.replace('  build:\n', '  build:\n    name: Build the base\n')), 'a named job in a named workflow')
  })

  test('FIRES when ONLY a job is named — an indented name: does not count', () => {
    const jobNameOnly = OK.replace(/^name: build-base\n/, '').replace('  build:\n', '  build:\n    name: Build the base\n')
    flags(one(jobNameOnly), 'only a job name')
  })
})

// ─────────────────────────────────────────────────────────────────────────────────────────────────
describe('rule 4 — no ubuntu-latest (D23)', () => {
  test('FIRES on runs-on: ubuntu-latest, and in quoted, list and matrix forms', () => {
    const r = flags(one(OK.replace('runs-on: ubuntu-24.04', 'runs-on: ubuntu-latest')), 'runs-on: ubuntu-latest')
    assert.match(r.out, /D23/)
    assert.match(r.out, /w\.yml:11/, 'the finding must point at the runs-on line')
    flags(one(OK.replace('runs-on: ubuntu-24.04', "runs-on: 'ubuntu-latest'")), 'quoted')
    flags(one(OK.replace('runs-on: ubuntu-24.04', 'runs-on: [ubuntu-latest]')), 'list')
    flags(one(OK.replace('runs-on: ubuntu-24.04',
      'strategy:\n      matrix:\n        os: [ubuntu-latest, ubuntu-24.04]\n    runs-on: ${{ matrix.os }}')), 'matrix value')
  })

  test('QUIET on ubuntu-24.04, and on ubuntu-latest mentioned only in a comment', () => {
    clean(one(OK), 'pinned runner')
    clean(one(OK.replace('runs-on: ubuntu-24.04', 'runs-on: ubuntu-24.04  # not ubuntu-latest (D23)')), 'trailing comment')
    clean(one(OK.replace('jobs:\n', '# ubuntu-latest moves to 26.04 in October\njobs:\n')), 'whole-line comment')
  })
})

// ─────────────────────────────────────────────────────────────────────────────────────────────────
describe('rule 5 — a fromJSON matrix needs a fallback or a job-level if:', () => {
  // A plan job, then two matrix jobs. The SECOND is the gate3.yml `faults` shape the first version
  // of this rule never looked at, because it only matched the first fromJSON matrix in the file.
  const job = (name, key, out, extra = '') => `  ${name}:
    needs: plan
${extra}    runs-on: ubuntu-24.04
    strategy:
      fail-fast: false
      matrix:
        ${key}: \${{ fromJSON(needs.plan.outputs.${out}) }}
    steps:
      - run: echo \${{ matrix.${key} }}
`
  const wf = (a, b) => OK + a + b
  const GUARD_A = "    if: needs.plan.outputs.shards != '[]'\n"
  const GUARD_B = "    if: needs.plan.outputs.faults != ''\n"

  test('FIRES on an unguarded matrix in the SECOND job when the first is guarded', () => {
    const r = flags(one(wf(job('clean', 'shard', 'shards', GUARD_A), job('faults', 'scenario', 'faults'))), 'second job unguarded')
    assert.match(r.out, /fromJSON/)
    assert.match(r.out, /scenario:/, 'the finding must name the second matrix')
    assert.doesNotMatch(r.out, /shard:/, 'the guarded first matrix must not be reported')
  })

  test('FIRES on both when neither is guarded', () => {
    const r = flags(one(wf(job('clean', 'shard', 'shards'), job('faults', 'scenario', 'faults'))), 'two unguarded')
    assert.match(r.out, /2 problem\(s\)/)
  })

  test('FIRES when the only if: mentioning it is a STEP condition, not the job', () => {
    const stepIf = job('faults', 'scenario', 'faults').replace('      - run: echo',
      "      - if: needs.plan.outputs.faults != '[]'\n        run: echo")
    flags(one(OK + stepIf), 'step-level if: is not a guard')
  })

  test('QUIET when every job is guarded by its own if:, or has a fallback', () => {
    clean(one(wf(job('clean', 'shard', 'shards', GUARD_A), job('faults', 'scenario', 'faults', GUARD_B))), 'both guarded')
    const fallback = job('faults', 'scenario', 'faults').replace('outputs.faults)', "outputs.faults || '[\"none\"]')")
    clean(one(wf(job('clean', 'shard', 'shards', GUARD_A), fallback)), 'guard + fallback')
  })
})

// ─────────────────────────────────────────────────────────────────────────────────────────────────
describe('the linter fails CLOSED rather than reporting a pass it did not earn', () => {
  test('REFUSES to report a pass when given no roots', () => {
    let threw = null
    try { execFileSync(process.execPath, [LINT], { encoding: 'utf8', stdio: 'pipe' }) } catch (e) { threw = e }
    assert.ok(threw, 'the linter exited 0 with no arguments')
    assert.equal(threw.status, 2)
    assert.match(String(threw.stderr), /refusing to report a pass/)
  })

  test('REFUSES to report a pass when the roots contain no workflows', () => {
    // The exact failure mode that matters: CI checks out siblings, one of them is missing, and the
    // linter "passes" over nothing. Scanning zero workflows is not a clean bill of health.
    const empty = join(root(), `empty-${seq++}`)
    mkdirSync(empty, { recursive: true })
    let threw = null
    try { execFileSync(process.execPath, [LINT, empty], { encoding: 'utf8', stdio: 'pipe' }) } catch (e) { threw = e }
    assert.ok(threw, 'the linter reported a pass having scanned zero workflows')
    assert.equal(threw.status, 2)
    assert.match(String(threw.stderr), /scanned 0 workflows/)
  })

  test('REFUSES a root that does not exist', () => {
    let threw = null
    try {
      execFileSync(process.execPath, [LINT, join(root(), 'nowhere')], { encoding: 'utf8', stdio: 'pipe' })
    } catch (e) { threw = e }
    assert.ok(threw, 'a nonexistent root reported clean')
    assert.equal(threw.status, 2)
  })

  test('only files under .github/workflows are linted — a stray yaml elsewhere is not a workflow', () => {
    const dir = join(root(), `stray-${seq++}`)
    mkdirSync(join(dir, 'config'), { recursive: true })
    mkdirSync(join(dir, '.github', 'workflows'), { recursive: true })
    writeFileSync(join(dir, 'config', 'thing.yml'), 'no_name_no_pipefail: true\n')
    writeFileSync(join(dir, '.github', 'workflows', 'w.yml'), OK)
    const out = execFileSync(process.execPath, [LINT, dir], { encoding: 'utf8' })
    assert.match(out, /1 workflow\(s\)/, 'the stray yaml outside .github/workflows was counted or linted')
  })

  test('every finding names a file and a line, or nobody can act on it', () => {
    const noName = OK.replace(/^name: build-base\n/, '')
    const r = flags(one(noName, 'unnamed.yml'), 'no name')
    assert.match(r.out, /unnamed\.yml:\d+/, `a finding with no file:line is a finding nobody fixes. Got:\n${r.out}`)
  })

  test('multiple defects in one file are ALL reported, not just the first', () => {
    const wrecked = `on:
  workflow_dispatch:
jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/upload-artifact@v4
        with: { name: art-\${{ matrix.profile }} }
`
    const r = flags(one(wrecked), 'a workflow with all three defects')
    assert.match(r.out, /3 problem\(s\)/,
      `all three rules should fire on this file; a linter that stops at the first turns one fix into three rounds. Got:\n${r.out}`)
  })
})

// ─────────────────────────────────────────────────────────────────────────────────────────────────
describe('the workflows this repository actually ships', () => {
  test('every checked-out Auros workflow passes the linter', () => {
    const roots = ['auros-base', 'auros-recipes', 'auros-installer', 'auros-web', '.']
      .filter((r) => { try { readdirSync(join(REPO, r, '.github', 'workflows')); return true } catch { return false } })
    assert.ok(roots.length > 0, 'no workflows are checked out here, so this test proves nothing — it refuses to pass')
    const out = execFileSync(process.execPath, [LINT, ...roots], { cwd: REPO, encoding: 'utf8' })
    assert.match(out, /no problems/)
  })

  test('and the same linter goes RED on those same workflows with one character changed', () => {
    // Non-vacuity for the test above. It is not enough that the real workflows pass; the run that
    // passed them has to be a run that COULD have failed. This takes a real shipped workflow, breaks
    // it by exactly one edit, and asserts the linter notices.
    const wfDir = join(REPO, 'auros-base', '.github', 'workflows')
    let names
    try { names = readdirSync(wfDir).filter((f) => /\.ya?ml$/.test(f)) } catch { names = [] }
    if (names.length === 0) {
      // Fall back to the meta repo's own workflows rather than skipping — a skip here would quietly
      // remove the only proof that the previous test can fail.
      const metaDir = join(REPO, '.github', 'workflows')
      names = readdirSync(metaDir).filter((f) => /\.ya?ml$/.test(f))
      assert.ok(names.length > 0, 'no workflow anywhere to break — this test cannot prove anything and says so')
      const text = readFileSync(join(metaDir, names[0]), 'utf8')
      flags(one(text.replace(/^name:.*\n/m, '')), 'a real workflow with its name removed')
      return
    }
    const text = readFileSync(join(wfDir, names[0]), 'utf8')
    flags(one(text.replace(/^name:.*\n/m, '')), 'a real workflow with its name removed')
  })
})

test.after(() => { if (ROOT) rmSync(ROOT, { recursive: true, force: true }) })
