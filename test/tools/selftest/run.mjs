// Self-test of the plan runner. It covers three things:
//
//   * the back-end matrix (`backends`, `backends_agree`, `known_fail`, the `[defaults]` table):
//     matrix.plan.toml, whose tests pass and fail on purpose;
//   * the per-test scratch directory: scratch.plan.toml is run twice, and a test must not see
//     another test's (or an earlier run's) files in its `${test_tmp}` directory;
//   * parallel execution, the serial mark and the tier filter: parallel.plan.toml must print the
//     same status lines in the same order with `--jobs 4` as with `--jobs 1`, and its tests must
//     overlap; the tests of serial.plan.toml marked `parallel = false` must overlap nothing; and
//     tier.plan.toml must be selected test by test by `--tier fast|slow|all`.
import fs from 'fs'
import path from 'path'
import { spawnSync } from 'child_process'
import { fileURLToPath } from 'url'
import { plansForChanges } from '../../runner.mjs'

const here = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(here, '..', '..', '..')
const runner = path.join(path.dirname(here), 'run-tests.mjs')
const stripAnsi = (text) => text.replace(/\x1b\[[0-9;]*m/g, '')
const problems = []

function runRunner(args) {
    const proc = spawnSync(process.execPath, [runner, ...args], { encoding: 'utf8', cwd: repoRoot })
    return { status: proc.status, output: stripAnsi(`${proc.stdout}${proc.stderr}`) }
}

function statusLines(output) {
    return [...output.matchAll(/^\[(PASS|FAIL|XFAIL|SKIP)\] (\S+)/gm)].map((match) => `${match[1]} ${match[2]}`)
}

function summaryLine(output) {
    return output.match(/^\[SUMMARY\].*$/m)?.[0] || 'none'
}

function readIntervals(file) {
    const intervals = new Map()
    for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
        const match = /^(\S+) (start|end) (\d+)$/.exec(line.trim())
        if (!match) continue
        const entry = intervals.get(match[1]) || {}
        entry[match[2]] = Number(match[3])
        intervals.set(match[1], entry)
    }
    return intervals
}

// --- the back-end matrix ---------------------------------------------------------------------
const matrix = runRunner([path.join(here, 'matrix.plan.toml')])
const output = matrix.output
const backends = ['nvm', 'fvm', 'jit', 'prl']
const expected = { plain: 'PASS', starts_with: 'PASS', starts_with_fails: 'FAIL' }
const expectAll = (name, status, exceptions = {}) => {
    for (const backend of backends) {
        expected[`${name}.${backend}`] = exceptions[backend] || status
    }
}
expectAll('agree', 'PASS')
expectAll('stdout_differs', 'PASS', { jit: 'FAIL' })
expectAll('stdout_differs_known', 'PASS', { jit: 'XFAIL' })
expectAll('known_but_passes', 'PASS', { prl: 'FAIL' })
expectAll('column_differs', 'PASS', { fvm: 'FAIL' })
expectAll('exit_differs', 'PASS', { prl: 'FAIL' })
expectAll('fields_not_listed', 'PASS')

const actual = {}
for (const [, status, name] of output.matchAll(/^\[(PASS|FAIL|XFAIL|SKIP)\] (\S+)/gm)) {
    actual[name] = status
}
for (const [name, status] of Object.entries(expected)) {
    if (actual[name] !== status) problems.push(`${name}: expected ${status}, got ${actual[name]}`)
}
for (const name of Object.keys(actual)) {
    if (!(name in expected)) problems.push(`unexpected test ${name}`)
}
const mustContain = [
    'differs from stdout_differs.nvm: stdout_normalized',
    'differs from column_differs.nvm: diagnostics',
    'differs from exit_differs.nvm: exit_code',
    "marked known_fail on 'prl' but passes: remove the mark",
    "the fixture's jit prints 41",
    'stdout does not start with "2\\n"',
    '[SUMMARY] total: 31, pass: 25, fail: 5, known failures: 1',
]
for (const text of mustContain) {
    if (!output.includes(text)) problems.push(`output lacks: ${text}`)
}
// Failures fail the run; a known failure alone would not.
if (matrix.status !== 1) problems.push(`runner exit ${matrix.status}, expected 1`)

// --- the per-test scratch directory -----------------------------------------------------------
// A file a previous run could have left at a fixed path under tmp/test must be cleared before the
// first test; a model written by one test must not be visible to another; and a second run must
// inherit nothing from the first. The fixture plan is run twice for exactly that last point.
const scratchPlan = path.join(here, 'scratch.plan.toml')
const staleFile = path.join(repoRoot, 'tmp', 'test', 'stale_from_previous_run.txt')
for (const attempt of [1, 2]) {
    fs.mkdirSync(path.dirname(staleFile), { recursive: true })
    fs.writeFileSync(staleFile, 'a model a previous run left behind')
    const scratch = runRunner([scratchPlan])
    const where = `scratch run ${attempt}`
    if (scratch.status !== 0) problems.push(`${where}: runner exit ${scratch.status}, expected 0`)
    for (const name of ['scratch.write', 'scratch.absent', 'scratch.legacy_path_cleared']) {
        if (!scratch.output.includes(`[PASS] ${name}`)) problems.push(`${where}: ${name} did not pass`)
    }
    if (!scratch.output.includes('[SUMMARY] total: 3, pass: 3, fail: 0')) {
        problems.push(`${where}: unexpected summary (${summaryLine(scratch.output)})`)
    }
    if (fs.existsSync(staleFile)) problems.push(`${where}: the planted stale file was not cleared`)
    if (fs.existsSync(path.join(repoRoot, 'tmp', 'test'))) {
        problems.push(`${where}: tmp/test was left behind`)
    }
}

// --- parallel execution: same output, overlapping tests --------------------------------------
const parallelPlan = path.join(here, 'parallel.plan.toml')
const parallelLog = path.join(repoRoot, 'tmp', 'selftest-parallel.log')
fs.rmSync(parallelLog, { force: true })
const parallel = runRunner(['--jobs', '4', parallelPlan])
if (parallel.status !== 0) problems.push(`parallel run: runner exit ${parallel.status}, expected 0`)
if (!parallel.output.includes('[SUMMARY] total: 6, pass: 6, fail: 0')) {
    problems.push(`parallel run: unexpected summary (${summaryLine(parallel.output)})`)
}
const parallelIntervals = fs.existsSync(parallelLog) ? readIntervals(parallelLog) : new Map()
if (parallelIntervals.size !== 6) {
    problems.push(`parallel run: ${parallelIntervals.size} of 6 tests recorded an interval`)
}
const parallelEntries = [...parallelIntervals.entries()]
let overlapped = false
for (let i = 0; i < parallelEntries.length; i++) {
    for (let j = i + 1; j < parallelEntries.length; j++) {
        const [, a] = parallelEntries[i]
        const [, b] = parallelEntries[j]
        if (a.start < b.end && b.start < a.end) overlapped = true
    }
}
if (!overlapped) problems.push('parallel run: no two tests overlapped, so nothing ran in parallel')

// The same plan serially: the status lines, in order, must be identical to the parallel run's.
fs.rmSync(parallelLog, { force: true })
const serialSamePlan = runRunner(['--jobs', '1', parallelPlan])
if (serialSamePlan.status !== 0) problems.push(`serial run of parallel.plan: exit ${serialSamePlan.status}`)
const parallelOrder = statusLines(parallel.output)
const serialOrder = statusLines(serialSamePlan.output)
if (parallelOrder.join('\n') !== serialOrder.join('\n')) {
    problems.push(`parallel output order differs from serial:\n  parallel: ${parallelOrder.join(', ')}\n  serial:   ${serialOrder.join(', ')}`)
}
const expectedOrder = ['p1', 'p2', 'p3', 'p4', 'p5', 'p6'].map((name) => `PASS parallel.${name}`).join('\n')
if (parallelOrder.join('\n') !== expectedOrder) {
    problems.push(`parallel run printed tests out of plan order: ${parallelOrder.join(', ')}`)
}

// --- a serial-marked test overlaps nothing ----------------------------------------------------
const serialPlan = path.join(here, 'serial.plan.toml')
const serialLog = path.join(repoRoot, 'tmp', 'selftest-serial.log')
fs.rmSync(serialLog, { force: true })
const serial = runRunner(['--jobs', '4', serialPlan])
if (serial.status !== 0) problems.push(`serial-marked run: runner exit ${serial.status}, expected 0`)
if (!serial.output.includes('[SUMMARY] total: 3, pass: 3, fail: 0')) {
    problems.push(`serial-marked run: unexpected summary (${summaryLine(serial.output)})`)
}
const serialIntervals = fs.existsSync(serialLog) ? readIntervals(serialLog) : new Map()
if (serialIntervals.size !== 3) {
    problems.push(`serial-marked run: ${serialIntervals.size} of 3 tests recorded an interval`)
}
const serialEntries = [...serialIntervals.entries()]
for (let i = 0; i < serialEntries.length; i++) {
    for (let j = i + 1; j < serialEntries.length; j++) {
        const [nameA, a] = serialEntries[i]
        const [nameB, b] = serialEntries[j]
        if (a.start < b.end && b.start < a.end) {
            problems.push(`serial-marked run: ${nameA} and ${nameB} overlapped`)
        }
    }
}

// --- the tier filter ---------------------------------------------------------------------------
const tierPlan = path.join(here, 'tier.plan.toml')
const tierFast = runRunner(['--tier', 'fast', tierPlan])
const tierSlow = runRunner(['--tier', 'slow', tierPlan])
const tierAll = runRunner(['--tier', 'all', tierPlan])
if (!tierFast.output.includes('[SUMMARY] total: 2, pass: 2, fail: 0')) {
    problems.push(`--tier fast: unexpected summary (${summaryLine(tierFast.output)})`)
}
if (statusLines(tierFast.output).join(', ') !== 'PASS tier.fast, PASS tier.default') {
    problems.push(`--tier fast ran: ${statusLines(tierFast.output).join(', ')}`)
}
if (!tierSlow.output.includes('[SUMMARY] total: 1, pass: 1, fail: 0')) {
    problems.push(`--tier slow: unexpected summary (${summaryLine(tierSlow.output)})`)
}
if (statusLines(tierSlow.output).join(', ') !== 'PASS tier.slow') {
    problems.push(`--tier slow ran: ${statusLines(tierSlow.output).join(', ')}`)
}
if (!tierAll.output.includes('[SUMMARY] total: 3, pass: 3, fail: 0')) {
    problems.push(`--tier all: unexpected summary (${summaryLine(tierAll.output)})`)
}

// --- the --changed mapping ---------------------------------------------------------------------
const hasPlan = (selection, name) => selection !== null && selection.names.includes(name)
const scheduler = plansForChanges(['src/passes/sched/fastvm/dispatch.cpp'])
if (!hasPlan(scheduler, 'smoke') || !hasPlan(scheduler, 'feat/linear')) {
    problems.push(`--changed: src/passes/sched/ selected ${scheduler ? scheduler.names.join(', ') : 'nothing'}`)
}
const docs = plansForChanges(['docs/test-plans.md'])
if (!hasPlan(docs, 'feat/docs_links.plan.toml')) {
    problems.push(`--changed: docs/ selected ${docs ? docs.names.join(', ') : 'nothing'}`)
}
const planFile = 'test/plans/smoke/core.plan.toml'
const ownPlan = plansForChanges([planFile])
if (!ownPlan || !ownPlan.targets.some((target) => path.resolve(target) === path.join(repoRoot, planFile))) {
    problems.push(`--changed: ${planFile} did not select itself`)
}
if (plansForChanges(['no/such/directory/file.txt']) !== null) {
    problems.push('--changed: an unmatched path selected plans instead of falling back')
}

if (problems.length > 0) {
    console.error(problems.join('\n'))
    console.error('--- runner output ---')
    console.error(output)
    process.exit(1)
}
console.log('runner self-test ok')