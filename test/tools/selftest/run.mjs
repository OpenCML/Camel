// Self-test of the plan runner. It covers:
//
//   * the back-end matrix (`backends`, `backends_agree`, `known_fail`, the `[defaults]` table):
//     matrix.plan.toml, whose tests pass and fail on purpose;
//   * the per-test scratch directory: scratch.plan.toml is run twice, and a test must not see
//     another test's (or an earlier run's) files in its `${test_tmp}` directory; the scratch
//     directory must lie under this run's `tmp/test/run-<pid>`; a file another run keeps under
//     tmp/test survives, and the run root is removed on success and kept on failure;
//   * parallel execution, the serial mark and the tier filter: parallel.plan.toml must print the
//     same status lines in the same order with `--jobs 4` as with `--jobs 1`, and its tests must
//     overlap; the tests of serial.plan.toml marked `parallel = false` must overlap nothing;
//     tier.plan.toml must be selected test by test by `--tier fast|slow|all` when its DIRECTORY
//     is the target, and run in full when its FILE is the target;
//   * the risk-based fast sample of a `[[sweeps]]` entry: three copies for a plan tagged
//     collections, one for the rest, every copy under `--tier all`;
//   * the `--changed` mapping: directory rule first, file rules second, the base branch and its
//     CAMEL_TEST_BASE override.
import fs from 'fs'
import path from 'path'
import { spawnSync } from 'child_process'
import { fileURLToPath } from 'url'
import { changedBase, plansForChanges } from '../../runner.mjs'

const here = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(here, '..', '..', '..')
const runner = path.join(path.dirname(here), 'run-tests.mjs')
const stripAnsi = (text) => text.replace(/\x1b\[[0-9;]*m/g, '')
const problems = []
const scratchRoot = path.join(repoRoot, 'tmp', 'test')
// When the self-test runs under the runner, this is its own scratch directory; a direct run
// keeps the logs under tmp/ with its own pid in the name.
const probeLogDir = process.env.CAMEL_TEST_TMPDIR || path.join(repoRoot, 'tmp')
fs.mkdirSync(probeLogDir, { recursive: true })

function runRunner(args, extraEnv = {}) {
    const proc = spawnSync(process.execPath, [runner, ...args], {
        encoding: 'utf8',
        cwd: repoRoot,
        env: { ...process.env, ...extraEnv },
    })
    return { status: proc.status, output: stripAnsi(`${proc.stdout}${proc.stderr}`), pid: proc.pid }
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

// A failed run keeps its root and says where: the per-test directories are gone, but what a
// program wrote outside them (or a report left later) is evidence, not garbage.
const matrixRoot = path.join(scratchRoot, `run-${matrix.pid}`)
if (!output.includes(`[INFO] test scratch kept: ${matrixRoot}`)) {
    problems.push('the failed run did not print the kept test root')
}
if (!fs.existsSync(matrixRoot)) problems.push(`the failed run removed its root: ${matrixRoot}`)
fs.rmSync(matrixRoot, { recursive: true, force: true })

// --- the per-test scratch directory -----------------------------------------------------------
// A model written by one test must not be visible to another; a second run must inherit nothing
// from the first; and the scratch directory must be under this run's own tmp/test/run-<pid>.
const scratchPlan = path.join(here, 'scratch.plan.toml')
// Unique per self-test process: two runners may run the self-test at the same time, and each
// keeps its own marker under tmp/test (outside every run-<pid> root).
const markerFile = path.join(scratchRoot, `marker-from-another-run-${process.pid}.txt`)
for (const attempt of [1, 2]) {
    fs.mkdirSync(scratchRoot, { recursive: true })
    fs.writeFileSync(markerFile, 'a file another run keeps')
    const scratch = runRunner([scratchPlan], { SELFTEST_MARKER: markerFile })
    const where = `scratch run ${attempt}`
    if (scratch.status !== 0) problems.push(`${where}: runner exit ${scratch.status}, expected 0`)
    for (const name of ['scratch.write', 'scratch.absent', 'scratch.other_run_untouched']) {
        if (!scratch.output.includes(`[PASS] ${name}`)) problems.push(`${where}: ${name} did not pass`)
    }
    if (!scratch.output.includes('[SUMMARY] total: 3, pass: 3, fail: 0')) {
        problems.push(`${where}: unexpected summary (${summaryLine(scratch.output)})`)
    }
    // The test checked, from inside, that its scratch directory is under a `run-*` root of
    // tmp/test (scratch.other_run_untouched). Here the run's own root, named for its process,
    // must be gone after a successful run.
    const runRoot = path.join(scratchRoot, `run-${scratch.pid}`)
    if (fs.existsSync(runRoot)) problems.push(`${where}: the successful run kept its root ${runRoot}`)
    if (!fs.existsSync(markerFile)) problems.push(`${where}: removed a file another run keeps`)
}
fs.rmSync(markerFile, { force: true })

// --- parallel execution: same output, overlapping tests --------------------------------------
const parallelPlan = path.join(here, 'parallel.plan.toml')
const parallelLog = path.join(probeLogDir, `selftest-parallel-${process.pid}.log`)
fs.rmSync(parallelLog, { force: true })
const parallel = runRunner(['--jobs', '4', parallelPlan], { SELFTEST_PROBE_LOG: parallelLog })
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
const serialSamePlan = runRunner(['--jobs', '1', parallelPlan], { SELFTEST_PROBE_LOG: parallelLog })
if (serialSamePlan.status !== 0) problems.push(`serial run of parallel.plan: exit ${serialSamePlan.status}`)
const parallelOrder = statusLines(parallel.output)
const serialOrder = statusLines(serialSamePlan.output)
if (parallelOrder.join('\n') !== serialOrder.join('\n')) {
    problems.push(
        `parallel output order differs from serial:\n  parallel: ${parallelOrder.join(', ')}\n  serial:   ${serialOrder.join(', ')}`
    )
}
const expectedOrder = ['p1', 'p2', 'p3', 'p4', 'p5', 'p6']
    .map((name) => `PASS parallel.${name}`)
    .join('\n')
if (parallelOrder.join('\n') !== expectedOrder) {
    problems.push(`parallel run printed tests out of plan order: ${parallelOrder.join(', ')}`)
}
fs.rmSync(parallelLog, { force: true })

// --- a serial-marked test overlaps nothing ----------------------------------------------------
const serialPlan = path.join(here, 'serial.plan.toml')
const serialLog = path.join(probeLogDir, `selftest-serial-${process.pid}.log`)
fs.rmSync(serialLog, { force: true })
const serial = runRunner(['--jobs', '4', serialPlan], { SELFTEST_PROBE_LOG: serialLog })
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
fs.rmSync(serialLog, { force: true })

// --- the tier filter, and the named plan that ignores it ---------------------------------------
// With the DIRECTORY as the target the layer selects test by test; with the FILE as the target
// the plan runs in full, because naming the plan is the intent.
const tierDir = path.join(here, 'tier')
const tierPlan = path.join(tierDir, 'tier.plan.toml')
const tierFast = runRunner(['--tier', 'fast', tierDir])
const tierSlow = runRunner(['--tier', 'slow', tierDir])
const tierAll = runRunner(['--tier', 'all', tierDir])
if (!tierFast.output.includes('[SUMMARY] total: 2, pass: 2, fail: 0')) {
    problems.push(`--tier fast (directory): unexpected summary (${summaryLine(tierFast.output)})`)
}
if (statusLines(tierFast.output).join(', ') !== 'PASS tier.fast, PASS tier.default') {
    problems.push(`--tier fast (directory) ran: ${statusLines(tierFast.output).join(', ')}`)
}
if (!tierSlow.output.includes('[SUMMARY] total: 1, pass: 1, fail: 0')) {
    problems.push(`--tier slow (directory): unexpected summary (${summaryLine(tierSlow.output)})`)
}
if (statusLines(tierSlow.output).join(', ') !== 'PASS tier.slow') {
    problems.push(`--tier slow (directory) ran: ${statusLines(tierSlow.output).join(', ')}`)
}
if (!tierAll.output.includes('[SUMMARY] total: 3, pass: 3, fail: 0')) {
    problems.push(`--tier all (directory): unexpected summary (${summaryLine(tierAll.output)})`)
}
// `all` is the Release layers, fast and slow: the debug-layer test of the fixture must stay out
// of it, because an aborting test would run against the Release binary and never abort.
if (statusLines(tierAll.output).includes('PASS tier.debug')) {
    problems.push('--tier all ran the debug test; the debug layer is opt-in')
}
// A debug layer runs on the Debug build; asking for it on the Release build is a contradiction
// the runner refuses before it starts a test.
const debugOnRelease = runRunner(['--tier', 'debug', '--build', 'release', tierDir])
if (debugOnRelease.status === 0 || !debugOnRelease.output.includes('--tier debug runs on the Debug build')) {
    problems.push(`--tier debug --build release: expected a refusal, got exit ${debugOnRelease.status}: ${debugOnRelease.output.slice(0, 200)}`)
}
for (const [label, run] of [
    ['default tier', runRunner([tierPlan])],
    ['--tier fast', runRunner(['--tier', 'fast', tierPlan])],
    ['--tier slow', runRunner(['--tier', 'slow', tierPlan])],
]) {
    if (!run.output.includes('[SUMMARY] total: 4, pass: 4, fail: 0')) {
        problems.push(`named plan (${label}): unexpected summary (${summaryLine(run.output)})`)
    }
    const ran = statusLines(run.output)
    if (ran.join(', ') !== 'PASS tier.fast, PASS tier.default, PASS tier.slow, PASS tier.debug') {
        problems.push(`named plan (${label}) ran: ${ran.join(', ')}`)
    }
    if (!run.output.includes('named plan target(s) run in full')) {
        problems.push(`named plan (${label}): the run did not say the named plan runs in full`)
    }
}

// --- the risk-based fast sample of a sweep -----------------------------------------------------
// sweep/ holds two source plans (five tests tagged collections, four scalar tests) and the plan
// that sweeps them. In the fast layer the risky plan keeps three copies and the plain one one;
// `--tier all` keeps every copy. The source plans themselves always run.
const sweepDir = path.join(here, 'sweep')
const sweepFast = runRunner(['--tier', 'fast', sweepDir])
if (sweepFast.status !== 0) problems.push(`sweep fast: runner exit ${sweepFast.status}, expected 0`)
if (!sweepFast.output.includes('[SUMMARY] total: 13, pass: 13, fail: 0')) {
    problems.push(`sweep fast: unexpected summary (${summaryLine(sweepFast.output)})`)
}
const fastPoison = statusLines(sweepFast.output).filter((line) => line.startsWith('PASS poison:'))
const riskyPoison = fastPoison.filter((line) => line.includes('poison:risky.'))
const plainPoison = fastPoison.filter((line) => line.includes('poison:plain.'))
if (fastPoison.length !== 4 || riskyPoison.length !== 3 || plainPoison.length !== 1) {
    problems.push(
        `sweep fast sampled ${fastPoison.length} copies (risky ${riskyPoison.length}, plain ${plainPoison.length}), expected 3 + 1`
    )
}
const sweepAll = runRunner(['--tier', 'all', sweepDir])
if (sweepAll.status !== 0) problems.push(`sweep all: runner exit ${sweepAll.status}, expected 0`)
if (!sweepAll.output.includes('[SUMMARY] total: 18, pass: 18, fail: 0')) {
    problems.push(`sweep all: unexpected summary (${summaryLine(sweepAll.output)})`)
}
if (statusLines(sweepAll.output).filter((line) => line.includes('poison:')).length !== 9) {
    problems.push('sweep --tier all did not keep every copy')
}

// --- the --changed mapping ---------------------------------------------------------------------
const hasPlan = (selection, name) => selection !== null && selection.names.includes(name)
const hasTarget = (selection, relative) =>
    selection !== null && selection.targets.some((target) => path.resolve(target) === path.join(repoRoot, relative))
const scheduler = plansForChanges(['src/passes/sched/fastvm/dispatch.cpp'])
if (!hasPlan(scheduler, 'smoke') || !hasPlan(scheduler, 'feat/linear')) {
    problems.push(`--changed: src/passes/sched/ selected ${scheduler ? scheduler.names.join(', ') : 'nothing'}`)
}
const docs = plansForChanges(['docs/test-plans.md'])
if (!hasPlan(docs, 'feat/docs_links.plan.toml')) {
    problems.push(`--changed: docs/ selected ${docs ? docs.names.join(', ') : 'nothing'}`)
}
// Directory first: the module's own plan directory, not the broad `modules/` mapping.
const tensor = plansForChanges(['modules/tensor/ops/matmul.cpp'])
if (!hasTarget(tensor, 'test/plans/feat/modules/tensor')) {
    problems.push(
        `--changed: modules/tensor/ selected ${tensor ? tensor.targets.join(', ') || tensor.names.join(', ') : 'nothing'}`
    )
}
if (tensor && (tensor.names.length !== 0 || tensor.targets.length !== 1)) {
    problems.push(`--changed: modules/tensor/ fell through to the file rules as well: ${JSON.stringify(tensor)}`)
}
const nn = plansForChanges(['modules/nn/ops.cpp'])
if (!hasTarget(nn, 'test/plans/feat/modules/nn.plan.toml')) {
    problems.push(`--changed: modules/nn/ did not select its plan file`)
}
// A test case takes the directory rule too, down to the deepest plan that mirrors its
// directory: test/cases/compile/enum/ selects feat/compile/enum.plan.toml, not all of
// feat/compile; test/cases/modules/tensor/ selects the tensor plan directory.
for (const [file, expectedTarget] of [
    ['test/cases/compile/enum/shapes.cml', 'test/plans/feat/compile/enum.plan.toml'],
    ['test/cases/modules/tensor/bounds.cml', 'test/plans/feat/modules/tensor'],
]) {
    const selection = plansForChanges([file])
    if (!hasTarget(selection, expectedTarget)) {
        problems.push(`--changed: ${file} did not select ${expectedTarget}`)
    }
}
// test/cases/loops/ has no plan directory of its own: the file rule names the linear plan.
const loopCase = plansForChanges(['test/cases/loops/onnx_refused.cml'])
if (!hasPlan(loopCase, 'feat/linear/loops.plan.toml')) {
    problems.push(
        `--changed: test/cases/loops/ selected ${loopCase ? loopCase.names.join(', ') : 'nothing'}`
    )
}
const planFile = 'test/plans/smoke/core.plan.toml'
const ownPlan = plansForChanges([planFile])
if (!hasTarget(ownPlan, planFile)) {
    problems.push(`--changed: ${planFile} did not select itself`)
}
if (plansForChanges(['no/such/directory/file.txt']) !== null) {
    problems.push('--changed: an unmatched path selected plans instead of falling back')
}
// The base branch: origin/develop by default, overridden by CAMEL_TEST_BASE.
const savedBase = process.env.CAMEL_TEST_BASE
delete process.env.CAMEL_TEST_BASE
if (changedBase() !== 'origin/develop') {
    problems.push(`--changed: the default base is ${changedBase()}, expected origin/develop`)
}
process.env.CAMEL_TEST_BASE = 'origin/release'
if (changedBase() !== 'origin/release') {
    problems.push(`--changed: CAMEL_TEST_BASE did not override the base (${changedBase()})`)
}
if (savedBase === undefined) delete process.env.CAMEL_TEST_BASE
else process.env.CAMEL_TEST_BASE = savedBase

if (problems.length > 0) {
    console.error(problems.join('\n'))
    console.error('--- runner output ---')
    console.error(output)
    process.exit(1)
}
console.log('runner self-test ok')