#!/usr/bin/env node
/**
 * A plan that names a run option value the CLI does not take is a *plan* error, and the runner says
 * so before it starts anything: `plan:line`, the test, the word as written, and the values the CLI
 * reads. It must not arrive as a failing test -- `--gc-check verify=1` did, and six entries of
 * `jit_framed` were investigated as collector defects because of it.
 *
 * The probe writes the plans it needs (a bad one and a good one) into its own scratch directory and
 * runs the runner against them, so the property is held without changing any plan of the tree.
 *
 * Two runs, not one per shape. Every start of the runner pays a price before it starts anything --
 * it prunes test/results and reads the summary of every run directory there
 * (test/tools/runner-output.mjs#pruneRunnerOutput) -- and on a host whose tree is large that price
 * is what the probe's own CPU time is made of. The shapes a plan may name are eight independent
 * entries, so they share one plan and one run, and each still reports its own verdict line. Only the
 * bad plan has to stand alone: its claim is that *nothing* started, which is read off a run that
 * holds no other plan to print a `[PASS]` into.
 *
 * Red when: the load accepts a value the CLI refuses; the message stops naming the file and line or
 * the legal values; the run turns the plan error into a `[FAIL]`; or a legal compound value
 * (`alloc=3,safepoint=1,kind=major`, `poison,verify=both`, `--gc mark-sweep`) or a pass parameter
 * (`std::opt safe_fp=true`) is refused -- a check that only knows a whitelist would refuse those,
 * and that would be worse than no check.
 *
 * Run: node arg_domain_refused.mjs
 */

import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const RUNNER = path.join(REPO_ROOT, 'test', 'tools', 'run-tests.mjs')
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(REPO_ROOT, 'tmp', 'arg-domain-'))

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

// A case the probe's own plans run: nothing about Camel, so the plan layer is the only thing under
// test here.
const CASE = path.join(SCRATCH, 'trivial.mjs')
fs.writeFileSync(CASE, 'console.log("ok")\n')

function writePlan(name, args) {
    const planPath = path.join(SCRATCH, `${name}.plan.toml`)
    const text = [
        `id = "probe.${name}"`,
        'tier = "fast"',
        '',
        '[[tests]]',
        `name = "probe.${name}.entry"`,
        'kind = "behavior"',
        'tool = "node"',
        `case = "${CASE.replace(/\\/g, '/')}"`,
        `args = ${JSON.stringify(args)}`,
        'expect_exit = 0',
        'expect_stdout_contains = ["ok"]',
        'timeout_ms = 30000',
        '',
    ].join('\n')
    fs.writeFileSync(planPath, text)
    return { planPath, text }
}

// One plan of one entry per accepted shape. Each entry is judged on its own status line, so a shape
// that stopped being accepted is still red by name; the run's exit code is the only reading the
// entries share.
function writeShapePlan(name, entries) {
    const lines = [`id = "probe.${name}"`, 'tier = "fast"', '']
    for (const entry of entries) {
        lines.push(
            '[[tests]]',
            `name = "probe.${name}.${entry.name}"`,
            'kind = "behavior"',
            'tool = "node"',
            `case = "${CASE.replace(/\\/g, '/')}"`,
            `args = ${JSON.stringify(['${case}', ...entry.args])}`,
            'expect_exit = 0',
            'expect_stdout_contains = ["ok"]',
            'timeout_ms = 30000',
            '',
        )
    }
    const planPath = path.join(SCRATCH, `${name}.plan.toml`)
    fs.writeFileSync(planPath, lines.join('\n'))
    return { planPath }
}

function runPlans(plans) {
    const result = spawnSync(process.execPath, [RUNNER, ...plans, '--jobs', '1', '--tier', 'fast'], {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        timeout: 120000,
        env: { ...process.env, CAMEL_TEST_TMPDIR: SCRATCH },
    })
    return {
        status: result.status,
        output: `${result.stdout || ''}${result.stderr || ''}`.replace(/\x1b\[[0-9;]*m/g, ''),
    }
}

// 1. The historical spelling: `--gc-check verify=1`.
const bad = writePlan('bad', ['--gc-check', 'verify=1'])
const badRun = runPlans([bad.planPath])
const argLine = bad.text.split('\n').findIndex((line) => line.includes('verify=1')) + 1
check('a plan naming --gc-check verify=1 makes the run refuse (exit 2)', badRun.status === 2, `status ${badRun.status}\n${badRun.output}`)
check('the message is a plan error', /\[PLAN ERROR\]/.test(badRun.output), badRun.output)
check(
    `the message names plan:line (${path.basename(bad.planPath)}:${argLine})`,
    badRun.output.includes(`${path.basename(bad.planPath)}:${argLine}`),
    badRun.output,
)
check('the message names the legal values', /before\|after\|both/.test(badRun.output), badRun.output)
check('the message names the test', badRun.output.includes('probe.bad.entry'), badRun.output)
check('nothing ran: no [PASS] and no [FAIL] of a test', !/\[PASS\]|\[FAIL\]/.test(badRun.output), badRun.output)

// 2. A value the CLI takes, in the shapes the plans of this tree use: each one must be accepted.
//
// The words come after `${case}` here, unlike a real plan, because this probe runs its entries with
// `tool = "node"`: node would read `--gc-check` as one of its own options and exit 9. Where the words
// sit is not what this check is about -- `argProblems` reads them wherever they are -- and what must
// not happen is a plan error.
const goodShapes = [
    ['--gc-stress', 'alloc=3,safepoint=1,kind=major', '--gc-check', 'verify=both'],
    ['--gc-check', 'poison,verify=both'],
    ['--gc-check', 'moves=1'],
    ['--gc', 'mark-sweep'],
    ['--gc', 'generational'],
    ['--gc', 'gen'],
    ['--gc-check', 'poison'],
]
// 3. A pass parameter is not an option of the run: `std::opt safe_fp=true` is the compiler's
// business, and a check that guessed at it would refuse a plan that works. It is the eighth entry of
// the same plan: it asks the same question as the seven above (`argProblems` leaves it alone), and
// its own status line still says whether it ran.
const good = writeShapePlan('good', [
    ...goodShapes.map((args, index) => ({ name: `shape${index}`, args })),
    { name: 'pass_parameter', args: ['--gc-check', 'poison', 'std::opt safe_fp=true'] },
])
const goodRun = runPlans([good.planPath])
for (const [index, args] of goodShapes.entries()) {
    check(
        `a plan naming ${JSON.stringify(args)} runs (exit 0)`,
        goodRun.status === 0 &&
            !/\[PLAN ERROR\]/.test(goodRun.output) &&
            goodRun.output.includes(`[PASS] probe.good.shape${index}`),
        `status ${goodRun.status}\n${goodRun.output}`,
    )
}
check(
    'a plan naming a pass parameter after the case still loads (no plan error)',
    !/\[PLAN ERROR\]/.test(goodRun.output) && goodRun.output.includes('[PASS] probe.good.pass_parameter'),
    goodRun.output,
)

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)