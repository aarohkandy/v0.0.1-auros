#!/usr/bin/env node
// Catch the GitHub Actions YAML mistakes whose symptom is silence.
//
// The one that cost us: `with: { name: x-${{ matrix.f }}, path: ... }`. Inside a YAML FLOW MAPPING the
// braces of `${{ }}` are structural, so the document is invalid, so GitHub rejects the whole workflow.
// The symptom is not an error message — it is a run that appears named by its FILE PATH instead of its
// `name:`, with no jobs and no log. Easy to stare past.
//
// Also checks that every workflow sets pipefail (D19), after a pipe through `tail` made a failing
// build score as a passing step.
import { readdirSync, readFileSync, statSync, existsSync } from 'node:fs'
import { join } from 'node:path'
import { relative } from 'node:path'

const roots = process.argv.slice(2)
if (!roots.length) { console.error('workflow-lint: no roots given — refusing to report a pass'); process.exit(2) }

const problems = []
let scanned = 0

const walk = (d) => {
  let es = []
  try { es = readdirSync(d, { withFileTypes: true }) } catch { return }
  for (const e of es) {
    const p = join(d, e.name)
    if (e.isDirectory()) { if (!['node_modules', '.git', 'dist'].includes(e.name)) walk(p); continue }
    if (!/\.ya?ml$/.test(e.name)) continue
    if (!p.includes('.github/workflows')) continue
    scanned++
    check(p)
  }
}

const check = (file) => {
  const text = readFileSync(file, 'utf8')
  const lines = text.split('\n')

  lines.forEach((line, i) => {
    // Strip what cannot be structural before looking at braces at all:
    //   * quoted scalars — a `${{ }}` inside quotes is legal, the braces sit inside a string;
    //   * a trailing `# comment`, which YAML does not parse and which would otherwise fire falsely.
    // Quotes come off FIRST so that a `#` inside a quoted scalar is not mistaken for a comment.
    const bare = line.replace(/"[^"]*"|'[^']*'/g, '').replace(/\s#.*$/, '')

    // Where does a flow mapping OPEN on this line? `key: {` — the brace after a colon.
    //
    // The earlier version of this test asked whether the line looked like a COMPLETE flow mapping
    // (`:\s*\{[^}]*$` or `:\s*\{.*\}\s*$`) and missed the one shape that matters most: a mapping
    // that opens on this line and continues on the next —
    //     with: { name: art-${{ matrix.profile }},
    //             path: out/ }
    // Neither alternative matched, because the `}}` of the expression defeated `[^}]*$` while the
    // trailing comma defeated `\}\s*$`. So the exact bug this linter was written for went
    // undetected whenever somebody wrapped the line. Caught by tools/workflow-lint.test.mjs.
    //
    // The question to ask is simply: does a flow mapping open here, and is there an unquoted
    // expression after it? That is true of every broken shape and false of every legal one.
    const open = bare.search(/:\s*\{/)
    if (open !== -1) {
      const after = bare.slice(open)
      if (/\$\{\{/.test(after)) {
        problems.push({ file, line: i + 1, why:
          'a ${{ }} expression sits unquoted inside a YAML flow mapping { }. The braces are structural, ' +
          'so GitHub rejects the whole workflow — and the symptom is a run named by its file path with ' +
          'no jobs and no log, not an error. Quote the value, or use block style.', text: line.trim().slice(0, 120) })
      }
    }
  })

  // `${NAME}` IN AN `env:` VALUE — a shell expansion written where an expression was meant.
  //
  // GitHub interpolates `${{ }}` and nothing else. `RECIPE: ${RECIPE}` is not an error, not a
  // warning, and not empty: it sets the variable to the nine literal characters `${RECIPE}`. Every
  // later `"$RECIPE"` then expands back to that literal, so a job that binds an input this way
  // computes paths that cannot exist and dies before it does the thing it exists to do.
  //
  // This cost us the validator on the only path an order takes. auros-recipes/build-recipe.yml had
  // `RECIPE: ${RECIPE}` at the job level, so `node dist/cli.js validate customers/${RECIPE}/recipe.yaml`
  // exited 2 on EVERY run — the recipe was never validated, never compiled, never gated. The failure
  // looked like infrastructure, and the file two lines away used the correct `${{ }}` syntax, so it
  // read as a convention rather than as a typo.
  //
  // Scope: `env:` values only, which is where the mistake is invisible. A `${VAR}` inside a `run:`
  // block is an ordinary shell expansion and entirely correct.
  {
    let inEnv = false
    let envIndent = 0
    lines.forEach((line, i) => {
      if (/^\s*#/.test(line) || line.trim() === '') return
      const indent = line.length - line.trimStart().length
      if (inEnv && indent <= envIndent) inEnv = false
      const m = /^(\s*)env:\s*$/.exec(line)
      if (m) { inEnv = true; envIndent = m[1].length; return }
      if (!inEnv) return
      const kv = /^\s*[A-Za-z_][A-Za-z0-9_]*:\s*(.+?)\s*$/.exec(line)
      if (!kv) return
      const value = kv[1]
      if (/\$\{\{/.test(value)) return                 // a real expression: fine
      if (!/\$\{[A-Za-z_][A-Za-z0-9_]*\}/.test(value)) return
      problems.push({ file, line: i + 1, why:
        'an `env:` value uses ${NAME} where a GitHub expression was meant. GitHub interpolates only ' +
        '${{ }}, so this variable is set to the LITERAL string "${NAME}" — the job does not fail, it ' +
        'quietly carries the wrong value into every step that reads it. Write ${{ inputs.name }}, ' +
        '${{ matrix.name }} or ${{ env.NAME }}; if a shell expansion really was intended, do it ' +
        'inside the `run:` block where it is one.', text: line.trim().slice(0, 120) })
    })
  }

  // A workflow GitHub REJECTS AT STARTUP produces a run with zero jobs, listed by its file path, and
  // `gh run view --log-failed` shows nothing at all, because no step ever ran. That happened to
  // gate3.yml. Document structure was fine; the expressions were not.
  //
  // These catch the two that actually occur: a matrix built from an input or expression that can be
  // empty or a non-list, and a `uses:` pointing at a local file that is not in the repository.
  //
  // EVERY fromJSON matrix in EVERY job is checked. The first version matched only the first one in
  // the file, so gate3.yml's second matrix (`faults`) was never looked at; and it accepted an `if:`
  // ANYWHERE in the file as the guard, so a step's `if:` could silence a job-level hole.
  //
  // A job whose OWN `if:` tests the same expression is SAFE: GitHub evaluates the condition first
  // and skips the job, so the matrix is never expanded. That is the mitigation this rule's message
  // recommends, and the rule flagged a correctly-guarded job in propagate.yml until it accounted for
  // it. A lint that does not honour its own advice teaches people to ignore it.
  {
    const jobsAt = lines.findIndex(l => /^jobs:\s*$/.test(l))
    const starts = []
    if (jobsAt !== -1) {
      for (let i = jobsAt + 1; i < lines.length; i++) {
        if (/^[A-Za-z]/.test(lines[i])) break                                  // next top-level key
        if (/^  [\w-]+:\s*(#.*)?$/.test(lines[i])) starts.push(i)
      }
    }
    starts.forEach((s, k) => {
      const end = k + 1 < starts.length ? starts[k + 1] : lines.length
      const job = lines.slice(s + 1, end)
      // Job-level keys are the job's direct children: the indent of its first non-blank, non-comment line.
      const first = job.find(l => l.trim() && !/^\s*#/.test(l))
      const childIndent = first ? first.length - first.trimStart().length : 4
      const jobIf = job.filter(l => l.length - l.trimStart().length === childIndent && /^\s*if:/.test(l)).join('\n')
      let matrixIndent = -1
      job.forEach((l, j) => {
        if (!l.trim() || /^\s*#/.test(l)) return
        const ind = l.length - l.trimStart().length
        if (matrixIndent !== -1 && ind <= matrixIndent) matrixIndent = -1
        const mm = /^(\s*)matrix:(.*)$/.exec(l)
        let expr = null
        if (mm) { matrixIndent = mm[1].length; expr = /\$\{\{\s*fromJSON\(([^)]*)\)/.exec(mm[2]) }
        else if (matrixIndent !== -1) expr = /^\s*[\w-]+:\s*\$\{\{\s*fromJSON\(([^)]*)\)/.exec(l)
        if (!expr) return
        const arg = expr[1]
        if (/\|\|/.test(arg) || /'\[/.test(arg)) return                        // has a fallback
        const leaf = arg.trim().split(/[.\s]/).pop().replace(/[^\w]/g, '')
        if (jobIf && (new RegExp('\\b' + leaf + '\\b').test(jobIf) || /!=\s*'\[\]'/.test(jobIf))) return
        problems.push({ file, line: s + 2 + j, why:
          'a matrix built from fromJSON() with no fallback. If the expression yields an empty list, null, ' +
          'or a non-array, GitHub REJECTS the workflow at startup: zero jobs, the run listed by file path, ' +
          'and --log-failed shows nothing because nothing ran. Give it a default, e.g. ' +
          "fromJSON(needs.x.outputs.y || '[\"placeholder\"]'), or guard the job with an if:.",
          text: l.trim().slice(0, 120) })
      })
    })
  }
  for (const m of text.matchAll(/^\s*uses:\s*(\.\/[^\s#]+)/gm)) {
    const rel = m[1].replace(/^\.\//, '')
    const repoRoot = file.slice(0, file.indexOf('.github/workflows'))
    if (repoRoot && !existsSync(join(repoRoot, rel))) {
      problems.push({ file, line: text.slice(0, m.index).split('\n').length, why:
        `uses: ${m[1]} but that file does not exist in this repository. A local \`uses:\` that cannot ` +
        'resolve is a startup rejection, not a step failure — zero jobs and an empty log.',
        text: m[0].trim() })
    }
  }

  // D19: a step that cannot fail is not a check.
  const topLevel = lines.filter(l => /^[A-Za-z]/.test(l)).map(l => l.split(':')[0])
  // The check used to begin `!topLevel.includes('defaults') && …`, so ANY top-level `defaults:`
  // block exempted the file — including one that only set a working-directory, or a shell with no
  // pipefail in it. That is a false negative in the one rule whose whole point is that a step must
  // be able to fail. What matters is whether a pipefail shell is actually declared, not whether a
  // key named `defaults` exists. Caught by tools/workflow-lint.test.mjs; no shipped workflow relied
  // on the hole (checked across all 25 before tightening).
  if (!/shell:\s*bash -[a-z]*e[a-z]*o pipefail/.test(text) && !/shell:\s*pwsh/.test(text)) {
    problems.push({ file, line: 1, why:
      'no pipefail default. A step that pipes through `tail`, `head` or `tee` takes the LAST command\'s ' +
      'exit status, so a failing build scores as a passing step — which happened here (D19). Add ' +
      'defaults.run.shell: bash -euo pipefail {0}.', text: '(whole file)' })
  }

  // D23: `ubuntu-latest` migrates to 26.04 between 2026-10-19 and 2026-11-19, under us, with no
  // commit to point at. Any non-comment occurrence fires — `runs-on: ubuntu-latest`, a quoted or
  // list form, or a matrix value that later feeds `runs-on: ${{ matrix.os }}`.
  lines.forEach((line, i) => {
    if (!/\bubuntu-latest\b/.test(line.replace(/(^|\s)#.*$/, ''))) return
    problems.push({ file, line: i + 1, why:
      'ubuntu-latest is forbidden (D23): it moves to 26.04 from 2026-10-19, so the OS under this job ' +
      'changes with no commit. Pin runs-on: ubuntu-24.04 and move it deliberately.', text: line.trim().slice(0, 120) })
  })

  // The parse-failure symptom itself: a file with no name: is one GitHub will show by path.
  if (!topLevel.includes('name')) {
    problems.push({ file, line: 1, why: 'no top-level `name:` — GitHub will list this workflow by its file path.', text: '(whole file)' })
  }
}

for (const r of roots) walk(r)
if (!scanned) { console.error('workflow-lint: scanned 0 workflows — refusing to report a pass'); process.exit(2) }

if (!problems.length) { console.log(`workflow-lint: ${scanned} workflow(s), no problems.`); process.exit(0) }
console.error(`\nworkflow-lint: ${problems.length} problem(s) across ${scanned} workflow(s)\n`)
for (const p of problems) console.error(`  ${relative(process.cwd(), p.file)}:${p.line}\n    ${p.why}\n    ${p.text}\n`)
process.exit(1)
