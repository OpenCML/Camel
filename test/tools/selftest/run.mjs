// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Self-test of the plan runner. It covers:
//
//   * the back-end matrix (`backends`, `backends_agree`, `known_fail`, the `[defaults]` table):
//     matrix.plan.toml, whose tests pass and fail on purpose;
//   * the plan schema: a misspelled key or a known key of the wrong type fails its own plan with
//     the file, the test and the key named, while a valid plan in the same directory still runs;
//     `expect_stdout_exact` (and its legacy name `expect_stdout`) compares the whole output;
//   * the payload a `verify_script_path` receives: the resolved case path under `case_path` and
//      the other fields docs/test-plans.md documents;
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
//   * the identity of a sweep copy: two source plans that both have a test named `shapes` keep
//      four distinct names, and one source test reused under two sweeps keeps two;
//   * a benchmark whose timed pass fails: `--timeit` then reports the failed iteration with no
//      mean, and the runner reports that failure instead of throwing on the missing metric;
//   * the `--changed` mapping: the directory rule and the file rules together (the mirrored
//     plan plus the plans outside that tree that catch the same change), the layered plans that
//     follow `--tier`, the forbidden perf/GC-stress/progen boundary, the base branch and its
//     CAMEL_TEST_BASE override;
//   * every tracked file is selected by a rule or names an allowed rule-less class: a new kind
//     of file (a new extension under a new path) cannot be dropped from `--changed` silently
//     
//   * every plan file under test/plans/ is selected by at least one case path it names, and a
//      target may omit the `.plan.toml` suffix of a plan file;
//   * the mirror image: no fast case path selects a slow-cost plan in full unless that plan is
//     the only plan that runs the case, or the selection file records the shared case with a
//      reason.
import fs from 'fs'
import path from 'path'
import { execFileSync, spawnSync } from 'child_process'
import { fileURLToPath } from 'url'
import { changedBase, plansForChanges, resolvedSelection } from '../../runner.mjs'
import { overSelectionProblems } from './over-selection.mjs'
import { cudaVisibilityAudit } from '../check-cuda-visibility.mjs'

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

// `test/runner.mjs` is the command `node scripts/test.js` runs: target resolution, `--changed`
// and `--list` live there, above the plan runner. A check of a target shorthand has to go
//  through it, not through run-tests.mjs, or it would test a different parser.
function runTestCli(args, extraEnv = {}) {
    const proc = spawnSync(process.execPath, [path.join(repoRoot, 'test', 'runner.mjs'), ...args], {
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
const expected = {
    plain: 'PASS',
    starts_with: 'PASS',
    starts_with_fails: 'FAIL',
    exact: 'PASS',
    exact_fails: 'FAIL',
    exact_legacy: 'PASS',
}
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
expected['not_contains'] = 'PASS'
expected['not_contains_fails'] = 'FAIL'

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
    "stdout contains '42'",
    'expect_stdout_exact differs: first difference at offset 2 (expected 4 chars, got 3 chars)',
    '[SUMMARY] total: 36, pass: 28, fail: 7, known failures: 1',
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

// --- the plan schema: an unknown key or a bad type fails its own plan -------------------------
// Four plan files share one directory: a misspelled key, a known key of the wrong type, a CPU
// budget with no liveness bound beside it, and a valid plan. The three invalid plans must fail
// with the file, the test and the key named; the valid one must still run, so the error fails the
// plan and not the run.
const strictDir = path.join(here, 'strict')
const strict = runRunner([strictDir])
if (strict.status !== 1) problems.push(`strict: runner exit ${strict.status}, expected 1`)
if (!strict.output.includes('[SUMMARY] total: 4, pass: 1, fail: 3')) {
    problems.push(`strict: unexpected summary (${summaryLine(strict.output)})`)
}
if (!strict.output.includes('[PASS] valid')) problems.push('strict: the valid plan did not run')
if (!strict.output.includes('[FAIL] <invalid plan>')) {
    problems.push('strict: an invalid plan was not reported as a failing plan')
}
for (const text of [
    "test/tools/selftest/strict/unknown_key.plan.toml: test 'unknown': unknown key "
        + "'expect_stdout_exactt'; did you mean 'expect_stdout_exact'?",
    "test/tools/selftest/strict/wrong_type.plan.toml: test 'wrong_type': "
        + 'expect_stdout_exact must be a string, got 42',
    "test/tools/selftest/strict/cpu_budget_no_timeout.plan.toml: test "
        + "'cpu_budget_without_timeout': cpu_budget_ms needs timeout_ms: the budget is the cost "
        + 'bound and the timeout is the liveness bound (a hang, not a slow run)',
]) {
    if (!strict.output.includes(text)) problems.push(`strict: output lacks: ${text}`)
}
const strictRoot = path.join(scratchRoot, `run-${strict.pid}`)
if (!strict.output.includes(`[INFO] test scratch kept: ${strictRoot}`)) {
    problems.push('strict: the failed run did not print the kept test root')
}
fs.rmSync(strictRoot, { recursive: true, force: true })

//  --- the verify payload ----------------------------------------------------------------
// The fixture's script asserts that `case_path` is the resolved case path and that the other
// documented fields are present. Before the fix `case_path` was undefined here, so this plan
// failed with `undefined !== <path>`.
const payloadPlan = path.join(here, 'verify_payload.plan.toml')
const payload = runRunner([payloadPlan])
if (payload.status !== 0) problems.push(`verify payload: runner exit ${payload.status}, expected 0`)
if (!payload.output.includes('[SUMMARY] total: 1, pass: 1, fail: 0')) {
    problems.push(`verify payload: unexpected summary (${summaryLine(payload.output)})`)
}
if (!payload.output.includes('[PASS] payload.case_path')) {
    problems.push(`verify payload: case_path was not delivered:\n${payload.output}`)
}
fs.rmSync(path.join(scratchRoot, `run-${payload.pid}`), { recursive: true, force: true })

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
//  `--changed`'s `layer: true` rule: the plan file stays a named target but follows the
// tier, so a fast plan's slow test is filtered instead of run in full.
const tierLayered = runRunner(['--tier', 'fast', '--selection-layer', tierPlan, tierPlan])
if (!tierLayered.output.includes('[SUMMARY] total: 2, pass: 2, fail: 0')) {
    problems.push(`layered named plan: unexpected summary (${summaryLine(tierLayered.output)})`)
}
if (statusLines(tierLayered.output).join(', ') !== 'PASS tier.fast, PASS tier.default') {
    problems.push(`layered named plan ran: ${statusLines(tierLayered.output).join(', ')}`)
}
if (!tierLayered.output.includes('selected plan target(s) follow --tier fast')) {
    problems.push('layered named plan: the run did not say the selected plan follows the tier')
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
const riskyPoison = fastPoison.filter((line) => line.includes('poison:selftest.sweep.risky:'))
const plainPoison = fastPoison.filter((line) => line.includes('poison:selftest.sweep.plain:'))
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

//  --- a sweep copy's identity carries its source plan ------------------------------------
// Two source plans both have a test named `shapes`. A copy is named
// `<sweep>:<source plan id>:<source test>`; the source plan is what keeps the two apart. Before
// it was part of the name both plans' copies were called `poison:shapes.*`, and a back end waited
// for, and compared against, whichever `poison:shapes.nvm` ran last: the run reported a false
// failure (`differs from poison:shapes.nvm` or `reference back end did not run`) although every
// test had run. The fixture's two `shapes` print different words, so a reference that resolved
// to the other plan fails on `stdout_normalized` instead of passing by accident.
const collisionPlan = path.join(here, 'sweep_names', 'collision.plan.toml')
const collision = runRunner([collisionPlan])
if (collision.status !== 0) problems.push(`sweep collision: runner exit ${collision.status}, expected 0`)
if (!collision.output.includes('[SUMMARY] total: 4, pass: 4, fail: 0')) {
    problems.push(`sweep collision: unexpected summary (${summaryLine(collision.output)})`)
}
const collisionExpected = [
    'PASS poison:selftest.sweep.collision.alpha:shapes.nvm',
    'PASS poison:selftest.sweep.collision.alpha:shapes.fvm',
    'PASS poison:selftest.sweep.collision.beta:shapes.nvm',
    'PASS poison:selftest.sweep.collision.beta:shapes.fvm',
]
if (statusLines(collision.output).join('\n') !== collisionExpected.join('\n')) {
    problems.push(`sweep collision ran: ${statusLines(collision.output).join(', ')}`)
}
if (collision.output.includes('poison:shapes.')) {
    problems.push('sweep collision: a copy is still named after the source test alone')
}
fs.rmSync(path.join(scratchRoot, `run-${collision.pid}`), { recursive: true, force: true })

// The other direction: one source test legitimately reused under two sweeps is two identities,
// and each copy's back ends compare against their own sweep's base. The fixture prints the
// sweep's environment variable, so the two sweeps' outputs differ and a copy that resolved its
// reference against the other sweep would fail.
const reusePlan = path.join(here, 'sweep_names', 'reuse.plan.toml')
const reuse = runRunner([reusePlan])
if (reuse.status !== 0) problems.push(`sweep reuse: runner exit ${reuse.status}, expected 0`)
if (!reuse.output.includes('[SUMMARY] total: 4, pass: 4, fail: 0')) {
    problems.push(`sweep reuse: unexpected summary (${summaryLine(reuse.output)})`)
}
const reuseExpected = [
    'PASS poison:selftest.sweep.reuse.source:shapes.nvm',
    'PASS poison:selftest.sweep.reuse.source:shapes.fvm',
    'PASS poison_again:selftest.sweep.reuse.source:shapes.nvm',
    'PASS poison_again:selftest.sweep.reuse.source:shapes.fvm',
]
if (statusLines(reuse.output).join('\n') !== reuseExpected.join('\n')) {
    problems.push(`sweep reuse ran: ${statusLines(reuse.output).join(', ')}`)
}
fs.rmSync(path.join(scratchRoot, `run-${reuse.pid}`), { recursive: true, force: true })

// --- the --changed mapping ---------------------------------------------------------------------
const hasPlan = (selection, name) => selection !== null && selection.names.includes(name)
const hasLayered = (selection, name) => selection !== null && selection.layered.includes(name)
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
// A directory rule names the plan that mirrors the path, and the file rule adds the plans
//  outside that tree that the same change can break: tensor keeps its own plans and adds
// the autodiff plan, which is layered so its slow PyTorch comparisons stay out of --tier fast.
const tensor = plansForChanges(['modules/tensor/ops/matmul.cpp'])
if (!hasTarget(tensor, 'test/plans/feat/modules/tensor')) {
    problems.push(
        `--changed: modules/tensor/ selected ${tensor ? tensor.targets.join(', ') || tensor.names.join(', ') : 'nothing'}`
    )
}
if (!hasPlan(tensor, 'feat/modules/index_semantics.plan.toml')) {
    problems.push('--changed: modules/tensor/ did not add the operator/index semantics plans')
}
if (!hasLayered(tensor, 'feat/modules/autodiff.plan.toml')) {
    problems.push('--changed: modules/tensor/ did not layer the autodiff plan')
}
// A layered plan is a plan to run, just one that follows the tier: it must also be selected, or
// the runner would never see it at all.
if (!hasPlan(tensor, 'feat/modules/autodiff.plan.toml')) {
    problems.push('--changed: the layered autodiff plan is not among the selected plans')
}
const nn = plansForChanges(['modules/nn/ops.cpp'])
if (!hasTarget(nn, 'test/plans/feat/modules/nn.plan.toml')) {
    problems.push(`--changed: modules/nn/ did not select its plan file`)
}
// The plans no directory rule can reach: a tiles change selects the CPU plan and the GPU plans.
const tiles = plansForChanges(['modules/tiles/src/foo.cpp'])
for (const plan of [
    'feat/modules/tiles.plan.toml',
    'gpu/tiles.plan.toml',
    'gpu/tiles_rows.plan.toml',
    'gpu/tiles_transfer.plan.toml',
]) {
    if (!hasPlan(tiles, plan)) problems.push(`--changed: modules/tiles/ did not select ${plan}`)
}
// `layer: true` keeps the slow PyTorch comparisons out of an ordinary change while the fast
// cases of the same plan still run.
const onnx = plansForChanges(['modules/onnx/exporter.cpp'])
if (!hasLayered(onnx, 'feat/modules/onnx.plan.toml') || !hasPlan(onnx, 'feat/trans')) {
    problems.push('--changed: modules/onnx/ did not select its layered plans')
}
const autodiff = plansForChanges(['modules/autodiff/pullback.cpp'])
if (!hasLayered(autodiff, 'feat/modules/autodiff.plan.toml')) {
    problems.push('--changed: modules/autodiff/ did not layer its plan')
}
// The optimizer, and the rule library it runs, select the opt plans (a directory, so the slow
// progen plan is filtered by --tier fast).
for (const file of ['src/passes/opt/generic/algebra.cpp', 'stdlib/rules.cml']) {
    const selection = plansForChanges([file])
    if (!hasPlan(selection, 'feat/opt') || !hasPlan(selection, 'feat/modules/rules.plan.toml')) {
        problems.push(
            `--changed: ${file} selected ${selection ? selection.names.join(', ') : 'nothing'}`
        )
    }
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
// The forbidden layers stay unselectable: perf, the GC-stress plans and progen. A change there
// falls back to the fast layer rather than running a plan the project keeps for a quiet machine.
for (const file of [
    'benchmarks/train.py',
    'test/cases/perf/fib_recursive.cml',
    'test/plans/perf/fib.plan.toml',
    'test/plans/gpu/tiles_regions.plan.toml',
    'test/plans/feat/mm/gc.plan.toml',
    'test/plans/feat/opt/progen.plan.toml',
]) {
    if (plansForChanges([file]) !== null) {
        problems.push(`--changed: ${file} selected a plan; perf/GC-stress/progen must be unselectable`)
    }
}
// Every plan the selection file names must exist, and no rule may wire in a forbidden plan.
const selectionDoc = JSON.parse(fs.readFileSync(path.join(repoRoot, 'test', 'plan-selection.json'), 'utf8'))
const forbiddenPlan = (name) =>
    name === 'perf' ||
    name.startsWith('perf/') ||
    name === 'feat/mm/gc' ||
    name.startsWith('feat/mm/gc.') ||
    name.startsWith('feat/mm/gc_') ||
    name === 'feat/opt/progen' ||
    name.startsWith('feat/opt/progen.') ||
    name.includes('tiles_regions')
for (const rule of selectionDoc.rules || []) {
    for (const entry of rule.plans || []) {
        const name = typeof entry === 'string' ? entry : entry.plan
        const base = path.join(repoRoot, 'test', 'plans', ...String(name).split('/'))
        if (!fs.existsSync(base) && !fs.existsSync(`${base}.plan.toml`)) {
            problems.push(`plan-selection.json: '${name}' is not a plan in test/plans`)
        }
        if (forbiddenPlan(name)) {
            problems.push(`plan-selection.json: '${name}' is a plan an ordinary change must not run`)
        }
    }
}
//  --- a rule-less file class cannot appear silently -------------------------------------
// `--changed` applies the rules per file. A file no rule matches is dropped from the union, and
// when another file in the same change does match, there is no fallback to notice it: the class
// runs nothing. That is how stdlib/signatures, the doc guards, the golden files and the debugger
// harness were being missed. This check walks every tracked file and fails on one that neither a
// directory rule nor a file rule selects, unless it names a class the project deliberately does
// not select. The allowlist is only for the forbidden layers (perf, GC stress, progen, PyTorch)
// and for files no plan can read (vendored code, packaging, CI configuration).
const RULELESS_ALLOWED = [
    {
        match: /^hello\.cml$/,
        why: 'a sample program at the repository root; no test plan reads it',
    },
    {
        match: /^benchmarks\//,
        why: 'the benchmarks and their PyTorch baselines are the manual perf layer (H167); a change here must not select a perf plan',
    },
    {
        match: /^showcase\//,
        why: 'the showcase programs are the manual perf layer (H167)',
    },
    {
        match: /^pypi\//,
        why: 'the Python packaging sources; no test plan reads them',
    },
    {
        match: /^third_party\//,
        why: 'vendored third-party code; no plan owns it',
    },
    {
        match: /^(\.github|\.vscode|\.husky)\//,
        why: 'CI and editor configuration; no test plan reads it',
    },
    {
        match: /^(LICENSE|ABOUT|ZEN|changes\.log|conanfile\.txt|package-lock\.json|export_matrix\.(csv|md))$/,
        why: 'repository metadata and generated release artifacts; no plan reads them',
    },
    {
        match: /^\.(clang-format|gitattributes|gitignore|prettierrc)$/,
        why: 'repository-wide formatting/ignore configuration; no plan reads it',
    },
    {
        match: /^test\/\.gitignore$|^test\/README\.md$|^test\/plans\/\.gitkeep$/,
        why: 'test scaffolding and its own documentation; no plan reads them',
    },
    {
        match: /^test\/perf-baseline\.json$/,
        why: 'the manual perf baseline (H167); a change here must not select a perf plan',
    },
    {
        match: /^test\/cases\/opt\/progen\/(README\.md|findings\.json)$/,
        why: 'the progen corpus documentation and its findings index are read by the manual progen tool (H167); the finding and judgement programs next to them are named by rules',
    },
    {
        match: /^test\/cases\/perf\//,
        why: 'the perf cases are run by perf plans only, and perf is manual (H167)',
    },
    {
        match: /^test\/plans\/perf\//,
        why: 'the perf plans are deliberately not selectable (H161 section 7)',
    },
    {
        match: /^test\/plans\/feat\/mm\/gc(\.plan\.toml|_collections\.plan\.toml|_elder_exhaustion\.plan\.toml)$/,
        why: 'the GC-stress plans are deliberately not selectable',
    },
    {
        match: /^test\/plans\/feat\/opt\/progen\.plan\.toml$/,
        why: 'progen is deliberately not selectable',
    },
    {
        match: /^test\/plans\/gpu\/tiles_(regions|decorator)\.plan\.toml$/,
        why: 'these GPU plans are GC stress and deliberately not selectable',
    },
]
// The index plus the files git would add: a class added but not committed yet must be caught
// too (the red proof adds one, and the remote host receives it as an untracked file). Ignored
// paths stay out, so build output and tmp/ do not trip the check.
const trackedFiles = execFileSync('git', ['ls-files', '-z', '--cached', '--others', '--exclude-standard'], {
    cwd: repoRoot,
    encoding: 'utf8',
})
    .split('\0')
    .filter(Boolean)
const uncoveredFiles = trackedFiles.filter(
    (file) => plansForChanges([file]) === null && !RULELESS_ALLOWED.some((entry) => entry.match.test(file))
)
if (uncoveredFiles.length > 0) {
    const classes = new Map()
    for (const file of uncoveredFiles) {
        const segments = file.split('/')
        const top = segments.length > 1 ? `${segments[0]}/` : '(root) '
        const extension = path.extname(file) || '(none)'
        const key = `${top}*${extension}`
        if (!classes.has(key)) classes.set(key, [])
        classes.get(key).push(file)
    }
    const named = [...classes.entries()]
        .map(([key, files]) => `${key} (${files[0]}${files.length > 1 ? ` and ${files.length - 1} more` : ''})`)
        .join('; ')
    problems.push(
        `plan-selection: ${uncoveredFiles.length} tracked file(s) match no rule and are not an allowed rule-less class: ${named}`
    )
}
for (const entry of RULELESS_ALLOWED) {
    if (!trackedFiles.some((file) => entry.match.test(file))) {
        problems.push(`plan-selection: allowlist entry matches no tracked file (${entry.why})`)
    }
}

//  --- every plan file of the branch is reachable from a case path ------------------------
// `--changed` selects plans through files, never through a plan's existence. A plan that no case
//  path selects is a plan the change that breaks its cases never runs, and the per-file check
// cannot see it: the case files themselves are covered, they just select another plan. Both sides
// are derived from the tree (no count is written down): the plan files under test/plans/, the
// `case` entries each one names (resolved the way the runner resolves them), and the selection
// `plansForChanges` returns for every case path and its directory. A plan is selected when the
// selection resolves to its file, or to a directory that contains it.
//
// The verdict is about the **branch**, so the population is the plan files git has in its index
//  A plan file that is only in the working tree -- an agent's scratch plan, written to try a
// case out and deleted again -- is not one of the branch's plans, and the live tree is shared with
// whoever is working in it: two gates of another task went red on this check, naming an untracked
// draft that had nothing to do with the branch under test, and passed again once the draft was
// removed. Such a file is named as a note instead, so the hole is still visible to the agent that
// wrote it, and the same file committed makes this check red for real -- which is the gate that
// matters, because nothing reaches develop uncommitted. The walk itself stays over the directory
//  (a plan file's absence from git is a fact about git, not about the runner), and keeps its
// own untracked files on purpose: a new class that is not committed yet must still be caught.
const SELFTEST_PLANS_ROOT = path.join(repoRoot, 'test', 'plans')
function walkPlanFiles(dir, out = []) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) walkPlanFiles(full, out)
        else if (entry.name.endsWith('.plan.toml')) out.push(full)
    }
    return out
}
const relFromRepo = (candidate) => path.relative(repoRoot, candidate).split(path.sep).join('/')
const trackedPlanFiles = new Set(
    execFileSync('git', ['ls-files', '-z', '--cached', '--', 'test/plans'], { cwd: repoRoot, encoding: 'utf8' })
        .split('\0')
        .filter(Boolean)
)
// The split is only meaningful while git's index actually holds plan files: an index that listed
// none would turn every plan below into an untracked note and this check would never fire again --
// a guard that cannot fail is not a guard.
if (trackedPlanFiles.size === 0) {
    problems.push(
        'plan-selection: git lists no plan file in its index under test/plans, so the reachability check below examined nothing (H368)'
    )
}
function planCasePaths(planFile) {
    const text = fs.readFileSync(planFile, 'utf8')
    const cases = []
    for (const match of text.matchAll(/^[ \t]*case[ \t]*=[ \t]*"([^"]*)"/gm)) {
        // `${case}` and the other variables are the runner's, not a path in this tree.
        if (match[1].includes('${')) continue
        cases.push(path.resolve(path.dirname(planFile), match[1].replace(/\\/g, '/')))
    }
    return cases
}
function selectionSelects(selection, planFile) {
    if (!selection) return false
    for (const target of resolvedSelection(selection)) {
        const stat = fs.statSync(target, { throwIfNoEntry: false })
        if (!stat) continue
        if (stat.isFile()) {
            if (path.resolve(target) === planFile) return true
            continue
        }
        const inside = path.relative(target, planFile)
        if (inside && !inside.startsWith('..') && !path.isAbsolute(inside)) return true
    }
    return false
}
const selectionByPath = new Map()
function selectionForPath(candidate) {
    if (!selectionByPath.has(candidate)) selectionByPath.set(candidate, plansForChanges([candidate]))
    return selectionByPath.get(candidate)
}
// The plans an ordinary change must not run are not holes when no case path selects them: the
// policy names them on purpose. The exemptions are derived from the selection file and the plan
// text, so a new plan of one of these classes is covered without a list of plans here.
const selfRuleForHoles = (selectionDoc.rules || []).find((rule) => rule.self === true)
const selfExcepted = ((selfRuleForHoles && selfRuleForHoles.except) || [])
    .map((entry) => String(entry).replace(/\\/g, '/'))
function planExemption(planFile, planText) {
    const rel = relFromRepo(planFile)
    if (rel.startsWith('test/plans/perf/')) {
        return 'the perf layer is manual (H167): a change there must not select a perf plan'
    }
    if (selfExcepted.some((entry) => rel === entry || rel.startsWith(entry))) {
        return 'the test/plans self rule deliberately excludes it (GC stress, progen)'
    }
    const topLevel = planText.split(/^\[\[/m)[0]
    if (/^[ \t]*tier[ \t]*=[ \t]*"debug"/m.test(topLevel)) {
        return 'it is in the debug layer, which `--changed` never runs'
    }
    if (planCasePaths(planFile).length === 0 && /^\[\[sweeps\]\]/m.test(planText)) {
        return 'it has no cases of its own: it sweeps the cases of other plans, and only its plan file selects it'
    }
    return null
}
const allPlanFiles = walkPlanFiles(SELFTEST_PLANS_ROOT)
const unreachablePlans = []
const scratchPlans = []
for (const planFile of allPlanFiles) {
    const planText = fs.readFileSync(planFile, 'utf8')
    if (planExemption(planFile, planText)) continue
    const casePaths = planCasePaths(planFile)
    const candidates = []
    for (const casePath of casePaths) {
        candidates.push(relFromRepo(casePath))
        candidates.push(`${relFromRepo(path.dirname(casePath))}/`)
    }
    const entry = candidates.length === 0
        ? `${relFromRepo(planFile)} (names no case)`
        : candidates.some((candidate) => selectionSelects(selectionForPath(candidate), planFile))
            ? null
            : `${relFromRepo(planFile)} (its cases are under ${relFromRepo(path.dirname(casePaths[0]))}/)`
    if (entry === null) continue
    if (trackedPlanFiles.has(relFromRepo(planFile))) unreachablePlans.push(entry)
    else scratchPlans.push(entry)
}
if (unreachablePlans.length > 0) {
    problems.push(
        `plan-selection: ${unreachablePlans.length} plan file(s) no case path selects, so --changed can never run them: `
        + `${unreachablePlans.join('; ')}. Add a rule that names the plan for the directory its cases live in (H201).`
    )
} else if (scratchPlans.length > 0) {
    // Not a problem: these files are in the working tree and not in git's index, so they are not
    // the branch's plans (see the block's comment). A scratch plan that no case path selects is
    // worth saying out loud to whoever left it there, and it is not worth a red run for everyone
    // else in the tree.
    console.log(
        `note: ${scratchPlans.length} untracked plan file(s) under test/plans/ no case path selects, so --changed `
        + `would never run them if they were committed: ${scratchPlans.join('; ')}. They are not in git's index, `
        + `so they are not the branch's plans and this check does not fail on them (H368).`
    )
}

//  --- no fast case path selects a slow-cost plan in full --------------------------------
// The mirror of the check above. A plan file target runs in full whatever `--tier` says, so a
// directory rule that resolves a fast case directory to a slow plan makes every edit in that
// directory pay for the whole plan. The check derives the cost class from the tree (the plan's
// tier, its tests' tiers and timeouts), the plan that runs each case from the plans' `case`
// entries, and the selection from `plansForChanges`; the only recorded exception is
// `slowCaseOwners` in plan-selection.json, and it must name a slow plan that shares a case with
// a fast plan, so an exception that excuses nothing fails too.
problems.push(...overSelectionProblems(repoRoot))

//  --- a plan target may omit its `.plan.toml` suffix ------------------------------------
// `node scripts/test.js test/plans/feat/exemplars` used to resolve to a directory that does not
// exist and exit ENOENT, although the plan file next to it does; two agents lost time on it in
// one day. A target that is neither a directory nor a file resolves to `<target>.plan.toml`, and
// a bare plan id keeps working. `--list` prints the resolved target without running the plan.
for (const target of ['test/plans/feat/exemplars', 'feat.exemplars']) {
    const listed = runTestCli([target, '--list'])
    if (listed.status !== 0 || !listed.output.includes('exemplars.plan.toml')) {
        problems.push(
            `target shorthand: '${target} --list' did not resolve to exemplars.plan.toml: ${listed.output.trim()}`
        )
    }
}
// The suffix-less target must also run: naming a plan file runs it in full, so the tier fixture's
// four tests (fast, default, slow, debug) all run whatever the tier says.
const shorthand = runTestCli([path.join(here, 'tier', 'tier')])
if (shorthand.status !== 0 || !shorthand.output.includes('[SUMMARY] total: 4, pass: 4, fail: 0')) {
    problems.push(`target shorthand: a plan target without its suffix did not run in full (${summaryLine(shorthand.output)})`)
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

// --- a benchmark whose timed pass fails --------------------------------------------------------
//  gives the `--timeit` failure payload no `mean_ms`; before the runner learnt to read it, the
// payload crashed the runner while it formatted the metric. The test must fail, with the
// diagnostic, and the summary must still be printed.
const benchPlan = path.join(here, 'benchmark_failure.plan.toml')
const bench = runRunner([benchPlan])
if (bench.status !== 1) problems.push(`benchmark failure: exit ${bench.status}, expected 1`)
if (/runner error/.test(bench.output)) problems.push('benchmark failure: a runner error was reported')
if (!/\[FAIL\] failing_pass/.test(bench.output)) {
    problems.push(`benchmark failure: the test was not reported as failing:\n${bench.output}`)
}
if (!/the timed pass failed/.test(bench.output)) {
    problems.push('benchmark failure: the payload failure reason is missing')
}
if (!/RuntimeDiag::DivisionByZero/.test(bench.output)) {
    problems.push('benchmark failure: the diagnostic of the failed iteration is missing')
}
if (!/\[SUMMARY\].*fail: 1/.test(bench.output)) {
    problems.push(`benchmark failure: ${summaryLine(bench.output)}`)
}

// --- the gate split line -----------------------------------------------------------------------
// The gate's wall time is build + test, and the next person has to be able to see a regression in
// it. The remote helper exports the build's own clock as CAMEL_GATE_BUILD_SECONDS; the runner
// prints `[SPLIT] build <b> s, test <t> s, total <b+t> s`. Both the format and the arithmetic are
// pinned here, on the real runner, with a value the runner could not have guessed. Without the
// variable the runner built nothing in this invocation, and says so (0.0), which is also the
// -NoBuild path's value.
const splitPlan = path.join(here, 'scratch.plan.toml')
const splitWithBuild = runRunner([splitPlan, '--jobs', '1'], { CAMEL_GATE_BUILD_SECONDS: '12.5' })
const splitMatch = splitWithBuild.output.match(/^\[SPLIT\] build ([\d.]+) s, test ([\d.]+) s, total ([\d.]+) s$/m)
if (!splitMatch) {
    problems.push(`gate split: the runner printed no [SPLIT] line:\n${splitWithBuild.output}`)
} else {
    const [build, test, total] = splitMatch.slice(1).map(Number)
    if (build !== 12.5) problems.push(`gate split: build ${build}, expected 12.5 (CAMEL_GATE_BUILD_SECONDS)`)
    if (Math.abs(total - (build + test)) > 0.05) {
        problems.push(`gate split: total ${total} is not build ${build} + test ${test}`)
    }
}
const splitNoBuild = runRunner([splitPlan, '--jobs', '1'], { CAMEL_GATE_BUILD_SECONDS: '' })
if (!/^\[SPLIT\] build 0\.0 s, test [\d.]+ s, total [\d.]+ s$/m.test(splitNoBuild.output)) {
    problems.push(`gate split: a run that built nothing did not print build 0.0 s:\n${splitNoBuild.output}`)
}

// --- CUDA visibility ---------------------------------------------------------------------------
// A CPU-only Python program that imports a CUDA-aware library reaches a card without naming one
// (`torch.optim.Adam(...).step()` retains every card's primary context), so it dies with
// `CUDA_ERROR_UNKNOWN` while another tenant holds a card: the same test is green or red by what
// the rest of the host is running. The runner therefore hides the machine's devices from every
// Python child that did not declare `cuda_devices = true` (docs/test-plans.md, "CUDA visibility").
//
// `cuda-visibility/visibility.plan.toml` pins what that means, test by test: the untouched node
// reference, the hidden Python default, the `skip_unless` precondition that gets the same value
// (a skip there would lose a test silently), the declared opt-in that must see exactly what the
// node child saw, and a test whose own `envs` win. `cuda-visibility/unpinned/` is the plan the
// guard must refuse, and the guard must pass on this tree with a pinned population: a guard that
// examined nothing would otherwise pass by finding nothing.
const visibilityPlan = path.join(here, 'cuda-visibility', 'visibility.plan.toml')
const visibility = runRunner([visibilityPlan])
for (const name of ['reference.node', 'hidden.python', 'hidden.precondition', 'declared.python', 'pinned.python']) {
    if (!new RegExp(`^\\[PASS\\] ${name.replace('.', '\\.')}\\b`, 'm').test(visibility.output)) {
        problems.push(`cuda visibility: '${name}' did not pass (${summaryLine(visibility.output)})`)
    }
}
if (!/\[SUMMARY\] total: 5, pass: 5, fail: 0\b/.test(visibility.output) || /\[SKIP\]/.test(visibility.output)) {
    problems.push(
        `cuda visibility: the hidden default, its precondition, the opt-in and a test's own value ` +
            `were not all exercised: ${summaryLine(visibility.output)}`
    )
}
const treeAudit = cudaVisibilityAudit(repoRoot)
for (const problem of treeAudit.problems) problems.push(`cuda visibility: ${problem}`)
const unpinnedAudit = cudaVisibilityAudit(path.join(here, 'cuda-visibility', 'unpinned'), false)
const unpinnedProblems = unpinnedAudit.problems.join('\n')
for (const [name, reason] of [
    ['unpinned.harness', 'reaches Python outside'],
    ['unpinned.names_a_card', 'sets CUDA_VISIBLE_DEVICES=0'],
]) {
    if (!unpinnedProblems.includes(name) || !unpinnedProblems.includes(reason)) {
        problems.push(
            `cuda visibility: the guard did not refuse '${name}' for '${reason}':\n` +
                `${unpinnedProblems || '<it found no problem at all>'}`
        )
    }
}
if (unpinnedAudit.problems.length !== 2) {
    problems.push(
        `cuda visibility: the deliberately unpinned fixture has ${unpinnedAudit.problems.length} ` +
            `problem(s), expected 2, so the guard is refusing something else as well or missing one:\n${unpinnedProblems}`
    )
}

if (problems.length > 0) {
    console.error(problems.join('\n'))
    console.error('--- runner output ---')
    console.error(output)
    process.exit(1)
}
console.log('runner self-test ok')