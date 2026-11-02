#!/usr/bin/env node
/**
 * The layer's size, counted twice: what the plan files declare for it, and what a run starts.
 *
 * Both numbers are printed on one line, because for a whole night the milestone layer was quoted as
 * `3330` while a run of it started `3404`, and each was read as "the layer". The difference is a
 * feature (`backends` expands one entry into several tests, `compare_to` pulls a companion in); the
 * other direction is the defect: an entry declared for this layer that neither runs nor is skipped
 * appears in no summary, so a plan proves less than its file says while looking green.
 *
 * **How the negative is taken, and why it is not a plan.** The run selects with one predicate, so
 * today `started >= declared` holds by construction: no plan file can make the check fire. That is
 * not a reason to skip the negative -- it is the reason the negative is a unit probe of the rule
 * (`tierAccountingProblems`) with a hand-fed `declared 2 / started 1` row: the rule is what a future
 * selector change has to keep honest, and a fabricated plan would have proven only that I can write
 * a green test. What the plans do carry, end to end, is the shape of both directions:
 * declared == started on a plain layer, and declared < started when a named plan runs entries of
 * another layer in full.
 *
 * Red when: the line stops carrying both numbers; a `declared > started` row passes; a legal row is
 * refused; or the runner's own line drifts from this module.
 *
 * Run: node tier_accounting.mjs
 */

import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'
import { tierAccountingLine, tierAccountingProblems } from '../../tools/tier-accounting.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..')
const RUNNER = path.join(REPO_ROOT, 'test', 'tools', 'run-tests.mjs')
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(REPO_ROOT, 'tmp', 'tier-accounting-'))

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

// ---- the rule, on rows nobody ran -------------------------------------------------------------
{
    const line = tierAccountingLine('milestone', [
        { plan: 'a.plan.toml', declared: 4, started: 6 },
        { plan: 'b.plan.toml', declared: 2, started: 2 },
    ])
    check(
        'the line carries the plan files, the declared entries and the started tests',
        line === 'milestone: 2 plan file(s), 6 declared test entries, 8 started',
        line,
    )
}

{
    const problems = tierAccountingProblems([{ plan: 'thin.plan.toml', declared: 2, started: 1 }])
    check('declared 2 / started 1 is a problem', problems.length === 1, JSON.stringify(problems))
    check('the problem names the plan', /thin\.plan\.toml/.test(problems[0] || ''), problems[0])
    check('the problem carries both numbers', /declares 2/.test(problems[0] || '') && /started 1\b/.test(problems[0] || ''), problems[0])
    check('the problem says neither ran nor were skipped', /neither ran nor were skipped/.test(problems[0] || ''), problems[0])
}

{
    const problems = tierAccountingProblems([
        { plan: 'even.plan.toml', declared: 3, started: 3 },
        { plan: 'expanded.plan.toml', declared: 2, started: 9 },
        { plan: 'swept.plan.toml', declared: 3104, started: 3404 },
    ])
    check('declared equal to or below started is not a problem', problems.length === 0, JSON.stringify(problems))
}

// ---- the runner's own line, on plans it really runs -------------------------------------------
const CASE = path.join(SCRATCH, 'shape.mjs')
fs.writeFileSync(CASE, 'console.log("line one")\n')

function writePlan(name, tier, entries) {
    const planPath = path.join(SCRATCH, `${name}.plan.toml`)
    const lines = [`id = "probe.${name}"`, `tier = "${tier}"`, '']
    for (const entry of entries) {
        lines.push('[[tests]]')
        lines.push(`name = "${entry.name}"`)
        if (entry.tier) lines.push(`tier = "${entry.tier}"`)
        lines.push('kind = "behavior"')
        lines.push('tool = "node"')
        lines.push(`case = "${CASE.replace(/\\/g, '/')}"`)
        lines.push('args = ["${case}"]')
        lines.push('expect_exit = 0')
        lines.push('expect_stdout_contains = ["line one"]')
        lines.push('timeout_ms = 30000')
        lines.push('')
    }
    fs.writeFileSync(planPath, lines.join('\n'))
    return planPath
}

function run(planPath, tier) {
    const result = spawnSync(process.execPath, [RUNNER, planPath, '--jobs', '1', '--tier', tier], {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        timeout: 120000,
        env: { ...process.env, CAMEL_TEST_TMPDIR: SCRATCH, CAMEL_TEST_SHOW_OUTPUT: '' },
    })
    return { status: result.status, output: `${result.stdout || ''}${result.stderr || ''}`.replace(/\x1b\[[0-9;]*m/g, '') }
}

// A plan of the layer it runs: the two numbers agree, and nothing is red.
{
    const plan = writePlan('plain', 'milestone', [{ name: 'probe.plain.one' }, { name: 'probe.plain.two' }])
    const out = run(plan, 'milestone')
    check('a milestone run prints the accounting line', /\[tier\] milestone: 1 plan file\(s\), 2 declared test entries, 2 started/.test(out.output), out.output)
    check('a plan that starts what it declares is not red', out.status === 0, `status ${out.status}\n${out.output}`)
}

// A named plan runs in full, so it starts entries of another layer too: declared below started, and
// that direction is the feature, not a failure.
{
    const plan = writePlan('mixed', 'milestone', [
        { name: 'probe.mixed.slow' },
        { name: 'probe.mixed.fast', tier: 'fast' },
    ])
    const out = run(plan, 'milestone')
    check(
        'declared below started is printed and not red',
        /\[tier\] milestone: 1 plan file\(s\), 1 declared test entries, 2 started/.test(out.output) && out.status === 0,
        `status ${out.status}\n${out.output}`,
    )
    check('it does not report the plans that declare more than they start', !/declare more tests/.test(out.output), out.output)
}

// The fast layer, the one every run of this tree uses: the line is there for it too.
{
    const plan = writePlan('fastlayer', 'fast', [{ name: 'probe.fastlayer.one' }])
    const out = run(plan, 'fast')
    check('a fast run prints its own line', /\[tier\] fast: 1 plan file\(s\), 1 declared test entries, 1 started/.test(out.output), out.output)
}

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)