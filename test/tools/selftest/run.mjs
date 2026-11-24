// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Self-test of the plan runner. It covers:
//
//   * the back-end matrix (`backends`, `backends_agree`, `known_fail`, the `[defaults]` table):
//     matrix.plan.toml, whose tests pass and fail on purpose;
//   * the plan schema: a misspelled key or a known key of the wrong type fails its own plan with
//     the file, the test and the key named, while a valid plan in the same directory still runs;
//     `expect_stdout_exact` (and its legacy name `expect_stdout`) compares the whole output;
//   * the `known_fail` gate: a marked case that names no owner, no reason, a placeholder reason, a
//     reason that names no wall or no sentence is refused where its plan is loaded, by name, and an
//     entry that carries all four loads, runs and reports `[XFAIL]` with its owner printed;
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
//   * the entries a sweep does not cover, counted: how many `negative` entries its source plans
//      declare, how many it copies, and how many it leaves out (the line below `[calibration]`);
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
import os from 'os'
import path from 'path'
import { execFileSync, spawnSync } from 'child_process'
import { fileURLToPath } from 'url'
import { changedBase, plansForChanges, resolvedSelection } from '../../runner.mjs'
import { measuredInstall, negativeSweepAccounting, negativeSweepLine } from '../run-tests.mjs'
import { overSelectionProblems } from './over-selection.mjs'
import { cudaVisibilityAudit } from '../check-cuda-visibility.mjs'

const here = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(here, '..', '..', '..')
const runner = path.join(path.dirname(here), 'run-tests.mjs')
// What this host records about a death is the host's, and two checks below read a fixture that
// states one death in each of the two vocabularies: a POSIX host reports the signal and no exit
// status, Windows reports the exit status and no signal (signal.plan.toml, `abrt`/`abrt_as_exit`).
const IS_WINDOWS = process.platform === 'win32'
const stripAnsi = (text) => text.replace(/\x1b\[[0-9;]*m/g, '')
// The width every child run of this self-test is taken at. A runner that is not told takes
// `availableParallelism` -- 32 on the gate host -- and then its readings move with its neighbours:
// on 2026-11-03 the sweep fixture's 13 cases came back all FAIL with `pass: 0` while the same
// fixture, run at 32 on a quieter host the same day, was 13/13 (`docs/handover/reviews/
// 2026-11-03-the-self-test-is-red-on-develop-and-has-no-name.cn.md`). All thirteen failing, none
// passing, is the shape of "squeezed out", not the shape of "one assertion chose wrongly"; and a
// judgement that moves with the load is neither a red nor a green (author ruling 2026-11-02 16:50).
// The checks below compare against fixed strings, so the width they are taken at has to be fixed
// too. Four is a width this fixture's work is small enough to fill and small enough not to swamp;
// one would make the wall time unacceptable, and it is not what the plan layer runs at either.
const SELFTEST_JOBS = '4'
// Printed at the end and read by nothing: how many child runs this file started, and what the
// whole thing cost in CPU and in wall time. The three numbers belong together because the bound
// this self-test runs under rose twice on readings that were never the same kind, and a host that
// reports its own cost is the only thing that stops the next raise from being a guess.
// See docs/handover/reviews/2026-11-03-a-timeout-should-be-derived-from-a-measurement-not-a-constant.cn.md.
const cost = { children: 0, cpuStart: process.cpuUsage(), wallStart: process.hrtime.bigint() }
// What each child run was actually started with, counted at the spawn and never read back from
// `SELFTEST_JOBS`. The distinction is the whole point of the [calibration] line: the constant says
// what this file meant to pass, and a line that prints the constant is not a reading -- blanking the
// constant left the line saying `jobs=4` while every child took the runner's default width, so the
// one thing the line exists for (the width these readings were taken at) was the one thing it never
// measured. `inherited` is the state of a child started with no width of ours and therefore with the
// runner's own (`availableParallelism`); it is a different word on the line for exactly that reason.
const widthTally = { fixed: 0, named: 0, inherited: 0, cli: 0 }
const widthsPassed = new Set()
const problems = []
const scratchRoot = path.join(repoRoot, 'tmp', 'test')
// When the self-test runs under the runner, this is its own scratch directory; a direct run
// keeps the logs under tmp/ with its own pid in the name.
const probeLogDir = process.env.CAMEL_TEST_TMPDIR || path.join(repoRoot, 'tmp')
fs.mkdirSync(probeLogDir, { recursive: true })

function runRunner(args, extraEnv = {}) {
    cost.children += 1
    // A child run that names its own width (`--jobs 1` for the serial check, `--jobs 4` for the
    // parallel one) keeps it: the width is the subject of those checks. Every other child run is
    // taken at `SELFTEST_JOBS`, which is what makes its reading independent of the load it is
    // started under. The flag goes before the target, where the runner reads it.
    //
    // What this call appends is recorded here, where the decision is made, because the line that
    // reports it used to report the constant instead -- see `widthTally`. A blank `SELFTEST_JOBS`
    // appends nothing rather than an empty `--jobs`: that is a child with no width of ours, and it
    // is the state the control experiment puts this file in to show the line can tell the two apart.
    const namesItsOwnWidth = args.includes('--jobs')
    const width = namesItsOwnWidth || !SELFTEST_JOBS ? [] : ['--jobs', SELFTEST_JOBS]
    if (namesItsOwnWidth) {
        widthTally.named += 1
    } else if (width.length > 0) {
        widthTally.fixed += 1
        widthsPassed.add(SELFTEST_JOBS)
    } else {
        widthTally.inherited += 1
    }
    const proc = spawnSync(process.execPath, [runner, ...width, ...args], {
        encoding: 'utf8',
        cwd: repoRoot,
        env: { ...process.env, ...extraEnv },
    })
    return { status: proc.status, output: stripAnsi(`${proc.stdout}${proc.stderr}`), pid: proc.pid }
}

// `test/runner.mjs` is the command `node scripts/test.js` runs: target resolution, `--changed`
// and `--list` live there, above the plan runner. A check of a target shorthand has to go
// through it, not through run-tests.mjs, or it would test a different parser.
function runTestCli(args, extraEnv = {}) {
    cost.children += 1
    // No width here, and none wanted: these calls only resolve a target (`--list`), so the width
    // they would run at is not part of what they check. They are counted in their own bucket so the
    // [calibration] line can account for every child it printed a total for.
    widthTally.cli += 1
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

// This fixture's own status lines: the entries whose name carries `needle`, in the order the run
// printed them. A run of eight fixtures has eight fixtures' lines, and a claim about one of them has
// to be read off that one.
function statusLinesMatching(output, needle) {
    return statusLines(output).filter((line) => line.includes(needle))
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

// The intervals the probe recorded under the names `pattern` matches, and nothing else: several
// fixtures of one run write to one log, and a count read off the whole log would be the count of the
// run rather than the count of the fixture. The names are the probe's (`p1`, `s1`), not the tests'
// (`parallel.p1`, `serial.first`): selftest_probe.mjs logs the name it was handed.
function intervalsNamed(file, pattern) {
    const kept = new Map()
    // A run that never started writes no log, and a missing log is a reading of zero intervals --
    // not a crash of this file, which has to reach its own `problems` list to say so.
    if (!fs.existsSync(file)) return kept
    for (const [name, entry] of readIntervals(file)) {
        if (pattern.test(name)) kept.set(name, entry)
    }
    return kept
}

// One plan's own lines. The runner prints `[PLAN] : <plan id>` when a plan's first entry starts and
// that plan's results in plan order under it, so the lines between two headers are one plan's: its
// status lines, its failure notes, its repair hints. This is what lets several fixtures share one
// run without the second one's reading being taken off the first one's lines -- an "unexpected
// test" or a "no runner error" is a claim about a plan, and it stays one.
function planSection(output, planId) {
    const marker = `[PLAN] : ${planId}`
    const start = output.indexOf(marker)
    if (start < 0) return ''
    const rest = output.slice(start + marker.length)
    const next = rest.indexOf('[PLAN] :')
    return next < 0 ? rest : rest.slice(0, next)
}

// --- is there an install to measure? -----------------------------------------------------------
// Every check below reads the output of a child run, and a child run does not start without an
// install: `preflightBuild` prints `[FAIL] no Release binary at <out/latest/bin/camel.exe>` and
// exits 2. Asked once per check, that single state came back as a page of reds in the shape
// "expected PASS, got undefined" and "unexpected summary (none)" -- none of them about the code,
// all of them about a binary that is not there, and two of them were tracked for a day as a defect
// that did not exist. So the question is asked once, here, before any check starts, and it is
// answered with ONE red that says what was not measured, why, and what to do about it.
//
// It is a red (`[FAIL]`, exit 1) and not a `[SKIP]`: `[SKIP]` is this project's word for a host
// state that makes a measurement a plan DID run impossible (no CUDA card, no driver), and it leaves
// the run green. Here nothing ran, because this tree was not built -- a skip would report "green,
// nothing to see", and a red does not have to mean "the code is wrong" as long as it says which of
// the two it is. The line below says "not measured", not "failed", for exactly that reason.
//
// This is also not a deletion of the checks: with an install present every one of them runs, and a
// run in which the install disappears halfway still reddens the check that lost its summary.
const install = measuredInstall()
if (!install.present) {
    console.error(
        `[FAIL] self-test: not measured -- no testable install: no ${install.label} binary at ` +
            `${install.exe}, and preflightBuild (test/tools/run-tests.mjs) refuses to start a run ` +
            `without one, so no child ran and none of this file's checks was evaluated.`
    )
    console.error(
        '    Not measured: the back-end matrix, the plan-schema refusals, the known_fail gate, ' +
            'expect_signal, the per-test scratch directory, parallel and serial execution, the tier ' +
            'filter, the sweep sample, --changed, a failing benchmark, the gate split, CUDA ' +
            'visibility, and the GPU card a test is given.'
    )
    console.error(`    Build it first: ${install.buildCommand}`)
    process.exit(1)
}

// --- the runs, grouped by what they claim -----------------------------------------------------
// A child run is not free: before it starts anything it asks the checkout what it is (`git status`
// twice, the watcher over every directory), reads the plans it selected, prunes `test/results` and
// writes its own record. On the gate host that fixed cost is 0.3 s of the 0.4 s an empty fixture
// costs, and it is the reason this file used to be the slowest single entry of the gate: 39 starts
// paid it 39 times. What a run can carry is decided by what it *reports*, and it reports one exit
// code plus one status line per entry, so the claims below group by the configuration a run is
// taken at -- its exit code, its width, its tier, its environment and its entry point -- and
// inside one configuration each claim is read off its own entries (its own plan, its own names, its
// own message) and never off a neighbour's.
//
// 39 subjects, 22 runs. The 17 starts that are gone were not 17 claims: a subject is not a
// configuration. Nine green fixtures were one run, four failing ones another, the three sweep
// samples were already in the same runs as the tier fixture that owns their layer, the two claims
// taken at `--jobs 1` were one run, and `tier.plan.toml` was started twice for the same reading
// (`--tier fast` and no `--tier` at all: the runner's default tier IS fast, so those two runs were
// one run twice -- the only genuine duplicate in the 39).
//
// What this costs is one line per fixture: an exact `[SUMMARY]` counts the run, not the fixture, so
// a per-fixture count is read as the count of that fixture's own entries and refusals. Nothing is
// asserted less: every name, message and count below is the one it was, and each is attributed to
// the plan it belongs to.

// --- the fixtures whose failure is the point: one run, four subjects ---------------------------
// The back-end matrix, the plan-schema refusals, the `known_fail` gate and the failing benchmark all
// expect exit 1, all run at the default width and tier, and none of them claims that nothing
// started -- so they are one run. Each is read off its own lines: the matrix's 36 named entries out
// of its own `[PLAN] : selftest.matrix` section (which is what keeps "no unexpected test" a claim
// about the matrix), the eight refusals by the plan file each message names, and the benchmark by
// the failure note of its one entry.
const failures = runRunner([
    path.join(here, 'matrix.plan.toml'),
    path.join(here, 'strict'),
    path.join(here, 'knownfail'),
    path.join(here, 'benchmark_failure.plan.toml'),
])
const output = failures.output
const matrixOutput = planSection(output, 'selftest.matrix')
const backends = ['nvm', 'fvm', 'jit', 'prl']
const expected = {
    plain: 'PASS',
    starts_with: 'PASS',
    starts_with_fails: 'XFAIL',
    exact: 'PASS',
    exact_fails: 'XFAIL',
    exact_legacy: 'PASS',
}
const expectAll = (name, status, exceptions = {}) => {
    for (const backend of backends) {
        expected[`${name}.${backend}`] = exceptions[backend] || status
    }
}
expectAll('agree', 'PASS')
// The six entries whose failure is the point are marked `known_fail` in the plan, so they report
// `[XFAIL]` and are counted as known failures. One entry is red on purpose and stays red:
// `known_but_passes` is the fixture for a mark that has healed, and marking it would be the same
// mistake the other way round. If one of the marked ones ever passes, the runner reports
// "marked known_fail on '<backend>' but passes: remove the mark" and this fixture turns red --
// which is what keeps "expected to fail" from becoming "never fails".
expectAll('stdout_differs', 'PASS', { jit: 'XFAIL' })
expectAll('stdout_differs_known', 'PASS', { jit: 'XFAIL' })
expectAll('known_but_passes', 'PASS', { prl: 'FAIL' })
expectAll('column_differs', 'PASS', { fvm: 'XFAIL' })
expectAll('exit_differs', 'PASS', { prl: 'XFAIL' })
expectAll('fields_not_listed', 'PASS')
expected['not_contains'] = 'PASS'
expected['not_contains_fails'] = 'XFAIL'

const actual = {}
for (const [, status, name] of matrixOutput.matchAll(/^\[(PASS|FAIL|XFAIL|SKIP)\] (\S+)/gm)) {
    actual[name] = status
}
for (const [name, status] of Object.entries(expected)) {
    if (actual[name] !== status) problems.push(`${name}: expected ${status}, got ${actual[name]}`)
}
// The matrix's own entries are the 36 named above and no other: read out of this plan's section,
// so a neighbour's entry in the same run cannot make the matrix look larger than it is.
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
    // One summary for the run, so the arithmetic is read once, over all four fixtures: 36 matrix
    // entries + 4 strict + 6 known_fail + 1 benchmark; 28 + 1 + 0 + 0 passed, 1 + 3 + 5 + 1 failed,
    // and 7 + 0 + 1 + 0 were marked known. A lost or doubled entry anywhere in the four moves this
    // line, and the per-fixture counts are read below from the entries and refusals themselves.
    '[SUMMARY] total: 47, pass: 29, fail: 10, known failures: 8',
]
for (const text of mustContain) {
    if (!output.includes(text)) problems.push(`output lacks: ${text}`)
}
// Failures fail the run; a known failure alone would not.
if (failures.status !== 1) problems.push(`runner exit ${failures.status}, expected 1`)

// A failed run keeps its root and says where: the per-test directories are gone, but what a
// program wrote outside them (or a report left later) is evidence, not garbage. One run, one root.
const matrixRoot = path.join(scratchRoot, `run-${failures.pid}`)
if (!output.includes(`[INFO] test scratch kept: ${matrixRoot}`)) {
    problems.push('the failed run did not print the kept test root')
}
if (!fs.existsSync(matrixRoot)) problems.push(`the failed run removed its root: ${matrixRoot}`)
fs.rmSync(matrixRoot, { recursive: true, force: true })

// --- the plan schema: an unknown key or a bad type fails its own plan -------------------------
// Four plan files share one directory: a misspelled key, a known key of the wrong type, a CPU
// budget with no liveness bound beside it, and a valid plan. The three invalid plans must fail
// with the file, the test and the key named; the valid one must still run, so the error fails the
// plan and not the run. The directory is one of the four targets of the `failures` run above.
const strict = failures
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

// --- the `known_fail` gate: a marked case carries an owner, a wall and a sentence --------------
// The author's ruling of 2026-11-03 00:30: a case that cannot pass today stays in the plan with the
// mark, who is responsible for making it green, and why it cannot pass. Two of the three used to be
// prose inside `known_fail_note`, which nothing checked and a review had to remember to read; they
// are fields now, and a plan that omits either is refused where it is loaded. A fourth field, the
// sentence the case prints today, is refused the same way: without it the runner takes any failure
// as the expected one, and another defect can wear the mark's name. Six plan files share one
// directory -- five ways to be incomplete and one complete entry. The five must fail with the file,
// the case and the missing (or unusable) field named; the complete one must load, run, and report
// `[XFAIL]` with its owner printed beside the mark. A default owner or a default reason would make
// the five pass and this block red, which is the direction that matters.
const knownfail = failures
// The five readings, each a refusal that names its own missing thing. Kept as pairs so the verdict
// printed below is read off the output and not off this file's intentions.
const knownfailReadings = [
    [
        'missing-owner',
        "test/tools/selftest/knownfail/no_owner.plan.toml: test 'no_owner': known_fail needs "
            + 'known_fail_owner',
    ],
    [
        'missing-reason',
        "test/tools/selftest/knownfail/no_reason.plan.toml: test 'no_reason': known_fail needs "
            + 'known_fail_reason',
    ],
    [
        'placeholder-reason',
        "test/tools/selftest/knownfail/vague_reason.plan.toml: test 'vague_reason': "
            + 'known_fail_reason is "待修", which says nothing about the wall',
    ],
    [
        'reason-names-no-wall',
        "test/tools/selftest/knownfail/no_wall.plan.toml: test 'no_wall': known_fail_reason names "
            + 'no wall',
    ],
    [
        'missing-msg',
        "test/tools/selftest/knownfail/no_msg.plan.toml: test 'no_msg': known_fail needs "
            + 'known_fail_msg',
    ],
]
const knownfailVerdict = knownfailReadings.map(([what, needle]) => {
    const seen = knownfail.output.includes(needle)
    if (!seen) problems.push(`known_fail gate: ${what} was not refused by name; output lacks: ${needle}`)
    return `${what}=${seen ? 'refused' : 'MISSING'}`
})
// The complete entry is the control: the same mark, refused only for want of a field, must load and
// run. Its status and its owner are two different readings -- one says the gate let it through, the
// other says the field reaches the person reading the gate.
if (!/^\[XFAIL\] complete\b/m.test(knownfail.output)) {
    problems.push(`known_fail gate: the complete entry did not report [XFAIL]:\n${knownfail.output}`)
}
// The owner is printed through `printDetailLines`, which splits `key: value` into two padded
// columns -- the line reads `[INFO] known_fail_owner   the runner self-test`, with no colon left.
// The assertion is on that printed form, because that is the form the reader of a gate sees.
if (!/\[INFO\] known_fail_owner\s+the runner self-test\b/.test(knownfail.output)) {
    problems.push('known_fail gate: the owner was not printed on the [XFAIL] line')
}
knownfailVerdict.push(`all-three=${/^\[XFAIL\] complete\b/m.test(knownfail.output) ? 'accepted' : 'MISSING'}`)
// What would end the red, printed beside the name of every known failure the run accepted. The
// complete fixture carries the other three fields and no `known_fail_ends_when`, so its row is the
// reading the roster exists to expose -- `(no end condition)` in red, which is a red nobody has said
// how to end. Two readings, both off the output: the row for this fixture, and the closing count
// agreeing with the rows above it. The count is the metric a mark cannot hide inside -- before this,
// a red with no stated end was one number among `known failures: N` and nothing said which -- so the
// assertion is its arithmetic and not a literal, since this run's other fixtures are marked too.
const rosterRows = [...knownfail.output.matchAll(/^\[KNOWN\] (\S+): (.*)$/gm)]
const rosterCount = knownfail.output.match(
    /^\[KNOWN\] (\d+) of (\d+) known failure\(s\) carry no known_fail_ends_when$/m
)
const rosterFixture = rosterRows.some(
    ([, name, ends]) => name === 'selftest.knownfail.complete/complete' && ends.trim() === '(no end condition)'
)
if (!rosterFixture) {
    problems.push(`known_fail gate: the roster did not name the missing end condition:\n${knownfail.output}`)
}
const rosterArithmetic = rosterCount !== null
    && Number(rosterCount[2]) === rosterRows.length
    && Number(rosterCount[1]) === rosterRows.filter(([, , ends]) => ends.trim() === '(no end condition)').length
if (!rosterArithmetic) {
    problems.push('known_fail gate: the roster count does not agree with the rows it prints')
}
knownfailVerdict.push(`roster=${rosterFixture && rosterArithmetic ? 'named' : 'MISSING'}`)
console.log(`[known_fail gate] ${knownfailVerdict.join('; ')}`)
// The two directories' refusals are counted where their plans are read, which is the one thing the
// single summary cannot say: it counts the run. Each refusal names its own plan file, so the count
// per directory is read from those lines and not from a total. Three in `strict/`, five here, and
// the fifth of the signal fixture's plans is refused for want of a signal in its own run.
const refusedIn = (dir) =>
    (failures.output.match(new RegExp(`\\[INFO\\] runner error: test/tools/selftest/${dir}/`, 'g')) || []).length
if (refusedIn('strict') !== 3) {
    problems.push(`strict: ${refusedIn('strict')} plan(s) refused, expected 3`)
}
if (refusedIn('knownfail') !== 5) {
    problems.push(`known_fail gate: ${refusedIn('knownfail')} plan(s) refused, expected 5`)
}

// --- expect_signal: a run that is meant to die of a named signal -------------------------------
// "This process dies of SIGABRT" had no way to be written: `expect_exit` compares an exit code a
// killed process does not have, and `known_fail` says the run is wrong today -- it turns the entry
// red the day the process stops dying, which is the opposite of what such an entry means. The
// fixture dies three ways, and only the first is green: of the signal the entry names, of a signal
// it does not name, and not at all. The last two are the discrimination, and the fifth entry writes
// no signal at all, which must be refused where the plan is read.
//
// The green entry is stated twice by the fixture, because what a host records about a death is the
// host's: POSIX ends the child with `signal = 'SIGABRT'` and no exit code, and Windows has no signal
// to report at all -- the same abort ends it with exit status 134. Each host reads the entry it can
// record and skips the other one BY NAME, with the host's answer printed as the reason, so the two
// hosts read the same five entries and the same summary; and the abort itself is measured on both,
// in the shape that host has for it. Skipping is not how this fixture turns green: an entry that
// vanished would be a lost measurement, and this check fails if either the pass or the skip is missing.
const signalDir = path.join(here, 'signal')
const signal = runRunner([signalDir])
if (signal.status !== 1) problems.push(`expect_signal: runner exit ${signal.status}, expected 1`)
if (!signal.output.includes('[SUMMARY] total: 5, pass: 1, fail: 3, skip: 1')) {
    problems.push(`expect_signal: unexpected summary (${summaryLine(signal.output)})`)
}
// The abort in this host's vocabulary, and the skip that says which vocabulary this run could not
// read. Both are asserted: a run in which neither entry passed is not the reading this fixture
// describes, and a run in which neither entry was reported as skipped has lost one silently.
const abrtRead = IS_WINDOWS ? 'abrt_as_exit' : 'abrt'
const abrtSkipped = IS_WINDOWS ? 'abrt' : 'abrt_as_exit'
if (!new RegExp(`^\\[PASS\\] ${abrtRead}\\b`, 'm').test(signal.output)) {
    problems.push(
        `expect_signal: the abort was not read as ${abrtRead} on this host:\n${signal.output}`
    )
}
if (!new RegExp(`^\\[SKIP\\] ${abrtSkipped}\\b`, 'm').test(signal.output)) {
    problems.push(
        `expect_signal: ${abrtSkipped} was neither run nor skipped by name on this host:\n${signal.output}`
    )
}
if (!signal.output.includes(IS_WINDOWS ? 'no signal deaths to report' : 'reports deaths by signal')) {
    problems.push(
        `expect_signal: the skip reason did not name this host's reading:\n${signal.output}`
    )
}
// The other direction, in the same words: a death by a signal the entry did not name is reported as
// the death that happened, and each host names that death its own way.
const wrongDeath = IS_WINDOWS
    ? 'expected signal SEGV, got exit 134'
    : 'expected signal SEGV, got signal SIGABRT'
if (!signal.output.includes(wrongDeath)) {
    problems.push(`expect_signal: a death by another signal was not reported that way:\n${signal.output}`)
}
if (!signal.output.includes('expected signal 15, got exit 0')) {
    problems.push(`expect_signal: a run that ended normally was not reported as not having died:\n${signal.output}`)
}
if (!signal.output.includes('expect_signal must name a signal')) {
    problems.push(`expect_signal: a plan that named no signal was not refused:\n${signal.output}`)
}
fs.rmSync(path.join(scratchRoot, `run-${signal.pid}`), { recursive: true, force: true })

//  --- the fixtures that are green on purpose: one run, eight subjects --------------------------
// Eight fixtures expect exit 0 at the default width and tier, and each of them claims something
// about its own entries: the payload a `verify_script_path` receives, a scratch directory per test,
// the order and the overlap of a parallel run, a serial mark overlapping nothing, the identity of a
// sweep copy, the CUDA visibility of a Python child, and a plan file named as a target running in
// full. They are one run because a run reports one exit code and one status line per entry -- the
// exit is read once, here, and every claim below is read off the entries that make it
// (`parallel.*`, `serial.*`, `poison:...`, `tier.*`), out of that plan's own lines, or out of its
// own intervals in the probe log.
//
// `tier.plan.toml` is that eighth subject, and it is where the file lost two starts rather than
// one: it used to be started twice, once with no `--tier` and once with `--tier fast`, and the
// runner's default tier IS fast (`test/tools/run-tests.mjs`, `tier: 'fast'`) -- so those two runs
// took the same reading of the same configuration, which is the one genuine duplicate among the 39.
// The explicit tiers this fixture is about are read further down, each in its own run.
const markerFile = path.join(scratchRoot, `marker-from-another-run-${process.pid}.txt`)
const intervalLog = path.join(probeLogDir, `selftest-intervals-${process.pid}.log`)
// One probe log per self-test process, appended to by every fixture that runs `selftest_probe.mjs`,
// so each reading below is filtered to the names it is about (`intervalsNamed`).
fs.rmSync(intervalLog, { force: true })
// A file another run keeps, under tmp/test and outside every `run-<pid>` root: this run must
// neither read it nor remove it, and neither must the second run of the scratch fixture below.
fs.mkdirSync(scratchRoot, { recursive: true })
fs.writeFileSync(markerFile, 'a file another run keeps')
const green = runRunner(
    [
        path.join(here, 'verify_payload.plan.toml'),
        path.join(here, 'scratch.plan.toml'),
        path.join(here, 'parallel.plan.toml'),
        path.join(here, 'serial.plan.toml'),
        path.join(here, 'sweep_names', 'collision.plan.toml'),
        path.join(here, 'sweep_names', 'reuse.plan.toml'),
        path.join(here, 'cuda-visibility', 'visibility.plan.toml'),
        path.join(here, 'tier', 'tier.plan.toml'),
    ],
    { SELFTEST_PROBE_LOG: intervalLog, SELFTEST_MARKER: markerFile }
)
if (green.status !== 0) problems.push(`green fixtures: runner exit ${green.status}, expected 0`)
// One summary for eight fixtures: 1 payload + 3 scratch + 6 parallel + 3 serial + 4 collision +
// 4 reuse + 5 visibility + 5 tier = 31 entries, every one of them passing. Every one of those
// entries is named below, so a lost or doubled entry moves this line and its own fixture's reading.
if (!green.output.includes('[SUMMARY] total: 31, pass: 31, fail: 0')) {
    problems.push(`green fixtures: unexpected summary (${summaryLine(green.output)})`)
}
// Naming a plan file is the intent, and eight of them were named: the run has to say so, because
// that is what makes all five entries of `tier.plan.toml` run at a tier that selects two of them.
if (!green.output.includes('named plan target(s) run in full: 8')) {
    problems.push(`green fixtures: the run did not say the named plans run in full (${summaryLine(green.output)})`)
}

//  --- the verify payload ----------------------------------------------------------------
// The fixture's script asserts that `case_path` is the resolved case path and that the other
// documented fields are present. Before the fix `case_path` was undefined here, so this plan
// failed with `undefined !== <path>`.
if (!green.output.includes('[PASS] payload.case_path')) {
    problems.push(`verify payload: case_path was not delivered:\n${green.output}`)
}

// --- the per-test scratch directory -----------------------------------------------------------
// A model written by one test must not be visible to another; a second run must inherit nothing
// from the first; and the scratch directory must be under this run's own tmp/test/run-<pid>.
const scratchPlan = path.join(here, 'scratch.plan.toml')
// The second run is the one claim of this fixture that cannot share a run: "a second run inherits
// nothing from the first" needs a first run, and the first one is inside `green` above. What it
// asserts is the same three entries plus the two facts about the root and the marker, read again --
// so a runner that reused one fixed scratch directory would fail here and only here.
fs.writeFileSync(markerFile, 'a file another run keeps')
const scratchAgain = runRunner([scratchPlan], { SELFTEST_MARKER: markerFile })
for (const [where, run] of [
    ['scratch run 1', green],
    ['scratch run 2', scratchAgain],
]) {
    if (run.status !== 0) problems.push(`${where}: runner exit ${run.status}, expected 0`)
    for (const name of ['scratch.write', 'scratch.absent', 'scratch.other_run_untouched']) {
        if (!run.output.includes(`[PASS] ${name}`)) problems.push(`${where}: ${name} did not pass`)
    }
    // The test checked, from inside, that its scratch directory is under a `run-*` root of
    // tmp/test (scratch.other_run_untouched). Here the run's own root, named for its process,
    // must be gone after a successful run.
    const runRoot = path.join(scratchRoot, `run-${run.pid}`)
    if (fs.existsSync(runRoot)) problems.push(`${where}: the successful run kept its root ${runRoot}`)
    if (!fs.existsSync(markerFile)) problems.push(`${where}: removed a file another run keeps`)
}
if (!scratchAgain.output.includes('[SUMMARY] total: 3, pass: 3, fail: 0')) {
    problems.push(`scratch run 2: unexpected summary (${summaryLine(scratchAgain.output)})`)
}
fs.rmSync(markerFile, { force: true })

// --- parallel execution: same output, overlapping tests --------------------------------------
const parallelPlan = path.join(here, 'parallel.plan.toml')
const parallel = green
const parallelIntervals = intervalsNamed(intervalLog, /^p[1-6]$/)
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

// The same plan serially, in the run taken at `--jobs 1` below: the status lines, in order, must be
// identical to the parallel run's. Declared here so the two readings of the same plan are compared
// where the plan is, not where the run is.
const parallelOrder = statusLinesMatching(parallel.output, 'parallel.')
const expectedOrder = ['p1', 'p2', 'p3', 'p4', 'p5', 'p6']
    .map((name) => `PASS parallel.${name}`)
    .join('\n')
if (parallelOrder.join('\n') !== expectedOrder) {
    problems.push(`parallel run printed tests out of plan order: ${parallelOrder.join(', ')}`)
}

// --- a serial-marked test overlaps nothing ----------------------------------------------------
const serialPlan = path.join(here, 'serial.plan.toml')
const serial = green
const serialIntervals = intervalsNamed(intervalLog, /^s[1-3]$/)
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
// Its three entries, by name: one serial-marked pair and the ordinary test that queues behind them.
const serialLines = statusLinesMatching(serial.output, 'serial.')
if (serialLines.join(', ') !== 'PASS serial.first, PASS serial.second, PASS serial.parallel_neighbour') {
    problems.push(`serial-marked run ran: ${serialLines.join(', ')}`)
}

// --- the tier filter, and the named plan that ignores it ---------------------------------------
// With the DIRECTORY as the target the layer selects test by test; with the FILE as the target
// the plan runs in full, because naming the plan is the intent.
//
// A layer is a run's own property: one run is one `--tier`, so the four layers are four runs and
// they cannot be fewer. What they carry is not only this fixture: the sweep fixture below samples
// its copies per layer, so its entries ride in the same run as the tier fixture's -- same tier,
// same width, and their names are their own (`poison:...` against `tier.*`). That is three starts
// less, and no reading of either fixture moves.
const tierDir = path.join(here, 'tier')
const tierPlan = path.join(tierDir, 'tier.plan.toml')
const sweepDir = path.join(here, 'sweep')
const tierFast = runRunner(['--tier', 'fast', tierDir, sweepDir])
const tierSlow = runRunner(['--tier', 'slow', tierDir])
const tierMilestone = runRunner(['--tier', 'milestone', tierDir, sweepDir])
const tierAll = runRunner(['--tier', 'all', tierDir, sweepDir])
// The tier fixture's own entries, by name: the sweep's entries in the same run do not start with
// `tier.`, so a fixture that widened or narrowed moves this reading and nothing else.
const tierLinesFast = statusLines(tierFast.output).filter((line) => line.includes('tier.'))
const tierLinesMilestone = statusLines(tierMilestone.output).filter((line) => line.includes('tier.'))
const tierLinesAll = statusLines(tierAll.output).filter((line) => line.includes('tier.'))
if (!tierFast.output.includes('[SUMMARY] total: 15, pass: 15, fail: 0')) {
    problems.push(`--tier fast (directory): unexpected summary (${summaryLine(tierFast.output)})`)
}
if (tierLinesFast.join(', ') !== 'PASS tier.fast, PASS tier.default') {
    problems.push(`--tier fast (directory) ran: ${tierLinesFast.join(', ')}`)
}
if (!tierSlow.output.includes('[SUMMARY] total: 1, pass: 1, fail: 0')) {
    problems.push(`--tier slow (directory): unexpected summary (${summaryLine(tierSlow.output)})`)
}
if (statusLines(tierSlow.output).join(', ') !== 'PASS tier.slow') {
    problems.push(`--tier slow (directory) ran: ${statusLines(tierSlow.output).join(', ')}`)
}
// The milestone layer is its own name, not a synonym for slow: the fast and the slow runs above
// must not have selected it.
if (!tierMilestone.output.includes('[SUMMARY] total: 6, pass: 6, fail: 0')) {
    problems.push(`--tier milestone (directory): unexpected summary (${summaryLine(tierMilestone.output)})`)
}
if (tierLinesMilestone.join(', ') !== 'PASS tier.milestone') {
    problems.push(`--tier milestone (directory) ran: ${tierLinesMilestone.join(', ')}`)
}
if (!tierAll.output.includes('[SUMMARY] total: 22, pass: 22, fail: 0')) {
    problems.push(`--tier all (directory): unexpected summary (${summaryLine(tierAll.output)})`)
}
// `all` is the Release layers, fast, slow and milestone: the debug-layer test of the fixture must
// stay out of it, because an aborting test would run against the Release binary and never abort.
if (tierLinesAll.includes('PASS tier.debug')) {
    problems.push('--tier all ran the debug test; the debug layer is opt-in')
}
// A debug layer runs on the Debug build; asking for it on the Release build is a contradiction
// the runner refuses before it starts a test. This one writes no run directory at all, which is
// why it is not a subject of any other run here.
const debugOnRelease = runRunner(['--tier', 'debug', '--build', 'release', tierDir])
if (debugOnRelease.status === 0 || !debugOnRelease.output.includes('--tier debug runs on the Debug build')) {
    problems.push(`--tier debug --build release: expected a refusal, got exit ${debugOnRelease.status}: ${debugOnRelease.output.slice(0, 200)}`)
}
// A named plan runs in full whatever the layer says -- at the default layer it is the `green` run
// above, where `tier.plan.toml` was one of eight named targets and all five of its entries ran at a
// layer that selects two of them. The two layers below are named explicitly, because the flag has
// to be typed for the reading to be about the flag.
for (const [label, run] of [
    ['--tier slow', runRunner(['--tier', 'slow', tierPlan])],
    ['--tier milestone', runRunner(['--tier', 'milestone', tierPlan])],
]) {
    if (!run.output.includes('[SUMMARY] total: 5, pass: 5, fail: 0')) {
        problems.push(`named plan (${label}): unexpected summary (${summaryLine(run.output)})`)
    }
    const ran = statusLines(run.output)
    if (ran.join(', ') !== 'PASS tier.fast, PASS tier.default, PASS tier.slow, PASS tier.milestone, PASS tier.debug') {
        problems.push(`named plan (${label}) ran: ${ran.join(', ')}`)
    }
    if (!run.output.includes('named plan target(s) run in full')) {
        problems.push(`named plan (${label}): the run did not say the named plan runs in full`)
    }
}
// `--changed`'s `layer: true` rule: the plan file stays a named target but follows the
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
//
// The three layers are the three runs above that already name a `--tier`, so the sweep's entries
// are read out of those runs: `poison:` is its own name space, and the counts below are that
// fixture's own entries, not the run's. The sweep is a copy of the layer, so it needs no start of
// its own.
if (tierFast.status !== 0) problems.push(`sweep fast: runner exit ${tierFast.status}, expected 0`)
const fastPoison = statusLines(tierFast.output).filter((line) => line.startsWith('PASS poison:'))
const riskyPoison = fastPoison.filter((line) => line.includes('poison:selftest.sweep.risky:'))
const plainPoison = fastPoison.filter((line) => line.includes('poison:selftest.sweep.plain:'))
if (fastPoison.length !== 4 || riskyPoison.length !== 3 || plainPoison.length !== 1) {
    problems.push(
        `sweep fast sampled ${fastPoison.length} copies (risky ${riskyPoison.length}, plain ${plainPoison.length}), expected 3 + 1`
    )
}
if (tierAll.status !== 0) problems.push(`sweep all: runner exit ${tierAll.status}, expected 0`)
if (statusLines(tierAll.output).filter((line) => line.includes('poison:')).length !== 9) {
    problems.push('sweep --tier all did not keep every copy')
}
// The copies the sample did not take are the milestone layer, by name: `--tier milestone` selects
// exactly those five (risky 2, plain 3) and none of the four the fast layer sampled.
if (tierMilestone.status !== 0) problems.push(`sweep milestone: runner exit ${tierMilestone.status}, expected 0`)
const milestonePoison = statusLines(tierMilestone.output).filter((line) => line.includes('poison:'))
const milestoneRisky = milestonePoison.filter((line) => line.includes('poison:selftest.sweep.risky:'))
const milestonePlain = milestonePoison.filter((line) => line.includes('poison:selftest.sweep.plain:'))
if (milestonePoison.length !== 5 || milestoneRisky.length !== 2 || milestonePlain.length !== 3) {
    problems.push(
        `sweep --tier milestone ran ${milestonePoison.length} copies (risky ${milestoneRisky.length}, ` +
            `plain ${milestonePlain.length}), expected the 5 unsampled ones (2 + 3)`
    )
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
const collision = green
const collisionExpected = [
    'PASS poison:selftest.sweep.collision.alpha:shapes.nvm',
    'PASS poison:selftest.sweep.collision.alpha:shapes.fvm',
    'PASS poison:selftest.sweep.collision.beta:shapes.nvm',
    'PASS poison:selftest.sweep.collision.beta:shapes.fvm',
]
// Filtered to this fixture's copies: in a run of eight fixtures a reading of "everything the run
// printed" would be the run's, and the claim here is that THIS fixture's four copies are named
// after their source plan and that no copy is named after the bare source test.
const collisionLines = statusLinesMatching(collision.output, 'selftest.sweep.collision')
if (collisionLines.join('\n') !== collisionExpected.join('\n')) {
    problems.push(`sweep collision ran: ${collisionLines.join(', ')}`)
}
if (collision.output.includes('poison:shapes.')) {
    problems.push('sweep collision: a copy is still named after the source test alone')
}

// The other direction: one source test legitimately reused under two sweeps is two identities,
// and each copy's back ends compare against their own sweep's base. The fixture prints the
// sweep's environment variable, so the two sweeps' outputs differ and a copy that resolved its
// reference against the other sweep would fail.
const reusePlan = path.join(here, 'sweep_names', 'reuse.plan.toml')
const reuse = green
const reuseExpected = [
    'PASS poison:selftest.sweep.reuse.source:shapes.nvm',
    'PASS poison:selftest.sweep.reuse.source:shapes.fvm',
    'PASS poison_again:selftest.sweep.reuse.source:shapes.nvm',
    'PASS poison_again:selftest.sweep.reuse.source:shapes.fvm',
]
const reuseLines = statusLinesMatching(reuse.output, 'selftest.sweep.reuse')
if (reuseLines.join('\n') !== reuseExpected.join('\n')) {
    problems.push(`sweep reuse ran: ${reuseLines.join(', ')}`)
}

// --- the --changed mapping ---------------------------------------------------------------------
const hasPlan = (selection, name) => selection !== null && selection.names.includes(name)
const hasLayered = (selection, name) => selection !== null && selection.layered.includes(name)
const hasTarget = (selection, relative) =>
    selection !== null && selection.targets.some((target) => path.resolve(target) === path.join(repoRoot, relative))
const scheduler = plansForChanges(['lib/std/fvm/fastvm.cpp'])
if (!hasPlan(scheduler, 'smoke') || !hasPlan(scheduler, 'feat/linear')) {
    problems.push(`--changed: lib/std/fvm/ selected ${scheduler ? scheduler.names.join(', ') : 'nothing'}`)
}
const docs = plansForChanges(['docs/test-plans.md'])
if (!hasPlan(docs, 'feat/docs_links.plan.toml')) {
    problems.push(`--changed: docs/ selected ${docs ? docs.names.join(', ') : 'nothing'}`)
}
// A directory rule names the plan that mirrors the path, and the file rule adds the plans
// outside that tree that the same change can break: tensor keeps its own plans and adds
// the autodiff plan, which is layered so its slow PyTorch comparisons stay out of --tier fast.
const tensor = plansForChanges(['lib/tensor/ops/binary_ops.cpp'])
if (!hasTarget(tensor, 'test/plans/feat/modules/tensor')) {
    problems.push(
        `--changed: lib/tensor/ selected ${tensor ? tensor.targets.join(', ') || tensor.names.join(', ') : 'nothing'}`
    )
}
if (!hasPlan(tensor, 'feat/modules/index_semantics.plan.toml')) {
    problems.push('--changed: lib/tensor/ did not add the operator/index semantics plans')
}
if (!hasLayered(tensor, 'feat/modules/autodiff.plan.toml')) {
    problems.push('--changed: lib/tensor/ did not layer the autodiff plan')
}
// A layered plan is a plan to run, just one that follows the tier: it must also be selected, or
// the runner would never see it at all.
if (!hasPlan(tensor, 'feat/modules/autodiff.plan.toml')) {
    problems.push('--changed: the layered autodiff plan is not among the selected plans')
}
const nn = plansForChanges(['lib/nn/layers.cpp'])
if (!hasTarget(nn, 'test/plans/feat/modules/nn.plan.toml')) {
    problems.push(`--changed: lib/nn/ did not select its plan file`)
}
// The plans no directory rule can reach: a tiles change selects the CPU plan and the GPU plans.
const tiles = plansForChanges(['lib/tiles/src/region_extract.cpp'])
for (const plan of [
    'feat/modules/tiles.plan.toml',
    'gpu/tiles.plan.toml',
    'gpu/tiles_rows.plan.toml',
    'gpu/tiles_transfer.plan.toml',
]) {
    if (!hasPlan(tiles, plan)) problems.push(`--changed: lib/tiles/ did not select ${plan}`)
}
// `layer: true` keeps the slow PyTorch comparisons out of an ordinary change while the fast
// cases of the same plan still run.
const onnx = plansForChanges(['lib/onnx/exporter.cpp'])
if (!hasLayered(onnx, 'feat/modules/onnx.plan.toml') || !hasPlan(onnx, 'feat/trans')) {
    problems.push('--changed: lib/onnx/ did not select its layered plans')
}
const autodiff = plansForChanges(['lib/autodiff/pullback.cpp'])
if (!hasLayered(autodiff, 'feat/modules/autodiff.plan.toml')) {
    problems.push('--changed: lib/autodiff/ did not layer its plan')
}
// The optimizer, and the rule library it runs, select the opt plans (a directory, so the slow
// progen plan is filtered by --tier fast).
for (const file of ['lib/std/cxx/opt/generic/algebra.cpp', 'lib/rules/rules.cml']) {
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
const loopCase = plansForChanges(['test/cases/loops/onnx_exported.cml'])
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
// The forbidden layers stay unselectable: perf and the GC-stress plans. A change there falls back
// to the fast layer rather than running a plan the project keeps for a quiet machine. The progen
// plan is here for a different reason: editing the plan file itself must fall back too, and only
// the rule for its own harness may name it (checked below).
for (const file of [
    'benchmarks/train.py',
    'test/cases/perf/fib_recursive.cml',
    'test/plans/perf/fib.plan.toml',
    'test/plans/gpu/tiles_regions.plan.toml',
    'test/plans/feat/mm/gc.plan.toml',
    'test/plans/feat/opt/progen.plan.toml',
]) {
    if (plansForChanges([file]) !== null) {
        problems.push(
            `--changed: ${file} selected a plan; perf/GC-stress must be unselectable and a progen plan edit must fall back`
        )
    }
}
// The one door into the random-program plan, and the reason it is a door and not a hole: the
// harness that writes the programs is the one change that has to run them. It is selected by name,
// so the whole plan runs -- the batch and the seed the batch leaves out -- because a plan that is
// selected and then filtered down to one entry is the same "nobody runs it" in a new place.
const progenHarness = plansForChanges(['test/tools/progen/generate.py'])
const PROGEN_PLAN = 'feat/opt/progen.plan.toml'
if (!hasPlan(progenHarness, PROGEN_PLAN)) {
    problems.push(
        `--changed: test/tools/progen/ selected ${progenHarness ? progenHarness.names.join(', ') : 'nothing'}, not the progen plan`
    )
} else if (hasLayered(progenHarness, PROGEN_PLAN)) {
    problems.push(
        '--changed: the progen plan is layered, so the tier filter would drop its batch; a harness change must run the plan in full'
    )
}
// The lock: a progen case and the plan file itself still select nothing of the kind, so the door
// above is the harness and not the corpus.
const progenCase = plansForChanges(['test/cases/opt/progen/findings/inline_fvm_arm_effect_lost.cml'])
if (hasPlan(progenCase, PROGEN_PLAN)) {
    problems.push('--changed: a progen case selected the progen plan; only test/tools/progen/ may')
}
const HARNESS_DOOR = 'test/tools/progen/'
const namesOnlyTheHarnessDoor = (rule) =>
    Array.isArray(rule.paths) &&
    rule.paths.length > 0 &&
    rule.paths.every((p) => typeof p === 'string' && (p === HARNESS_DOOR || p.startsWith(HARNESS_DOOR)))
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
        if (forbiddenPlan(name) && !(name.startsWith('feat/opt/progen') && namesOnlyTheHarnessDoor(rule))) {
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
// not select. The allowlist is for the forbidden layers (perf, GC stress, PyTorch) and for the
// progen plan file, which is reachable only from its own harness, plus files no plan can read.
// (Vendored code, packaging, CI configuration.)
const RULELESS_ALLOWED = [
    {
        match: /^hello\.cml$/,
        why: 'a sample program at the repository root; no test plan reads it',
    },
    {
        match: /^benchmarks\//,
        why: 'the benchmarks and their PyTorch baselines are the manual perf layer; a change here must not select a perf plan',
    },
    {
        match: /^showcase\//,
        why: 'the showcase programs are the manual perf layer',
    },
    {
        match: /^promo\//,
        why: 'the promo film, the logo and the motion-graphics sources are the manual marketing material; no test plan reads them',
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
        why: 'the manual perf baseline; a change here must not select a perf plan',
    },
    {
        match: /^test\/cases\/opt\/progen\/(README\.md|findings\.json)$/,
        why: 'the progen corpus documentation and its findings index are read by the manual progen tool; the finding and judgement programs next to them are named by rules',
    },
    {
        match: /^test\/cases\/perf\//,
        why: 'the perf cases are run by perf plans only, and perf is manual',
    },
    {
        match: /^test\/plans\/perf\//,
        why: 'the perf plans are deliberately not selectable',
    },
    {
        match: /^test\/plans\/feat\/mm\/gc(\.plan\.toml|_collections\.plan\.toml|_elder_exhaustion\.plan\.toml|_root_window\.plan\.toml)$/,
        why: 'the GC-stress plans are deliberately not selectable',
    },
    {
        match: /^test\/plans\/feat\/opt\/progen\.plan\.toml$/,
        why: 'a progen plan edit falls back to the fast layer: the plan is named only by the rule for test/tools/progen/, not by its own file or by a case',
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
// path selects is a plan the change that breaks its cases never runs, and the per-file check
// cannot see it: the case files themselves are covered, they just select another plan. Both sides
// are derived from the tree (no count is written down): the plan files under test/plans/, the
// `case` entries each one names (resolved the way the runner resolves them), and the selection
// `plansForChanges` returns for every case path and its directory. A plan is selected when the
// selection resolves to its file, or to a directory that contains it.
//
// The verdict is about the **branch**, so the population is the plan files git has in its index
// A plan file that is only in the working tree -- an agent's scratch plan, written to try a
// case out and deleted again -- is not one of the branch's plans, and the live tree is shared with
// whoever is working in it: two gates of another task went red on this check, naming an untracked
// draft that had nothing to do with the branch under test, and passed again once the draft was
// removed. Such a file is named as a note instead, so the hole is still visible to the agent that
// wrote it, and the same file committed makes this check red for real -- which is the gate that
// matters, because nothing reaches develop uncommitted. The walk itself stays over the directory
// (a plan file's absence from git is a fact about git, not about the runner), and keeps its
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
        'plan-selection: git lists no plan file in its index under test/plans, so the reachability check below examined nothing'
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
        return 'the perf layer is manual: a change there must not select a perf plan'
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
        + `${unreachablePlans.join('; ')}. Add a rule that names the plan for the directory its cases live in.`
    )
} else if (scratchPlans.length > 0) {
    // Not a problem: these files are in the working tree and not in git's index, so they are not
    // the branch's plans (see the block's comment). A scratch plan that no case path selects is
    // worth saying out loud to whoever left it there, and it is not worth a red run for everyone
    // else in the tree.
    console.log(
        `note: ${scratchPlans.length} untracked plan file(s) under test/plans/ no case path selects, so --changed `
        + `would never run them if they were committed: ${scratchPlans.join('; ')}. They are not in git's index, `
        + `so they are not the branch's plans and this check does not fail on them.`
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
// five tests (fast, default, slow, milestone, debug) all run whatever the tier says.
const shorthand = runTestCli([path.join(here, 'tier', 'tier')])
if (shorthand.status !== 0 || !shorthand.output.includes('[SUMMARY] total: 5, pass: 5, fail: 0')) {
    problems.push(`target shorthand: a plan target without its suffix did not run in full (${summaryLine(shorthand.output)})`)
}

//  --- a target that names a directory holding no plan refuses ------------------------------
// `test/plans/feat/docs_links` is the plan's fixture directory; the plan file is the sibling
// `docs_links.plan.toml`, so the suffix-less shorthand above does not apply (the directory
// exists). Naming it used to reach no plan, start nothing and explain that with the layer
// sentence -- "none of their entries is in this layer", which is an answer about plans that were
// reached. It now refuses, names the plan beside it, and prints no [SUMMARY] at all: a run that
// started nothing must not look finished.
const barren = runTestCli(['test/plans/feat/docs_links'])
if (barren.status !== 2) problems.push(`target that names no plan: exit ${barren.status}, expected 2`)
if (!/holds no \*\.plan\.toml/.test(barren.output) || !/docs_links\.plan\.toml/.test(barren.output)) {
    problems.push(`target that names no plan: the refusal did not name the plan file beside it: ${barren.output.trim()}`)
}
if (/\[SUMMARY\]/.test(barren.output)) problems.push('target that names no plan: a [SUMMARY] was printed for a run that started nothing')
// `--list` is the other entry point onto the same target and used to answer it differently: it
// printed the directory and exited 0, so "what would this run?" was answered with a path no plan
// is behind. Same exit code, same sentence -- the sentence is asserted equal, so a rewording in
// one of the two places cannot leave them describing the same mistake in two ways.
const barrenList = runTestCli(['test/plans/feat/docs_links', '--list'])
if (barrenList.status !== 2) problems.push(`--list on a target that names no plan: exit ${barrenList.status}, expected 2`)
const sentenceOf = (output) => (output.match(/^\[PLAN ERROR\].*$/m) || [''])[0].trim()
if (!sentenceOf(barrenList.output)) {
    problems.push(`--list on a target that names no plan: no [PLAN ERROR] line: ${barrenList.output.trim()}`)
} else if (sentenceOf(barrenList.output) !== sentenceOf(barren.output)) {
    problems.push(`--list and the run disagree about the same target: ${sentenceOf(barrenList.output)} vs ${sentenceOf(barren.output)}`)
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

//  --- a measured empty selection starts nothing, and says the count --------------------------
// `--changed` with nothing to narrow by used to hand an empty target list to `run-tests.mjs`, whose
// `loadPlans` reads "no target" as the whole plan tree: the run was the whole fast layer. So 0
// changed files started 336 plan files and printed a `[SUMMARY]` with no failure -- the same three
// numbers a correct selection of one plan prints, and the same exit code. The fallback is gone
// (nothing is selected, so nothing starts), and the count is a line of its own: "changed: 0
// target(s)" is the only thing that tells a zero from a layer in a log, and a selection that really
// did select plans must not print it, or the line would mean nothing.
const changedScratch = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-changed-'))
const fallbackLine = (output) =>
    (output.match(/^\[WARN\] --changed:.*falling back to the whole fast layer \(\d+ plan file\(s\).*$/m) || [
        '',
    ])[0]
const emptyChangedList = path.join(changedScratch, 'empty.txt')
fs.writeFileSync(emptyChangedList, '')
const emptySelection = runTestCli(['--changed', '--changed-files', emptyChangedList, '--list'])
if (emptySelection.status !== 0) {
    problems.push(`--changed on an empty list: exit ${emptySelection.status}, expected 0`)
}
if (!/^\[INFO\] changed: 0 target\(s\)/m.test(emptySelection.output)) {
    problems.push(
        `--changed on an empty list did not print 'changed: 0 target(s)': ${emptySelection.output.trim()}`
    )
}
if (fallbackLine(emptySelection.output)) {
    problems.push(
        `--changed on an empty list still widens to the layer: ${emptySelection.output.trim()}`
    )
}
if (/\(the whole fast layer: smoke \+ feat\)/.test(emptySelection.output)) {
    problems.push(
        `--changed on an empty list says the layer is what it runs: ${emptySelection.output.trim()}`
    )
}
// The run path, not only the listing: a zero has to reach the runner as a zero, or the widest
// reading of `--changed` is the one nobody checked.
const emptyRun = runRunner(['--selection-empty'])
if (emptyRun.status !== 0) {
    problems.push(`--selection-empty: exit ${emptyRun.status}, expected 0`)
}
if (!/discovered plans: 0/.test(emptyRun.output)) {
    problems.push(`--selection-empty widened to the plan tree: ${summaryLine(emptyRun.output)}`)
}
if (!/\[SUMMARY\].*total: 0, pass: 0, fail: 0/.test(emptyRun.output)) {
    problems.push(`--selection-empty: ${summaryLine(emptyRun.output)}`)
}
const matchedChangedList = path.join(changedScratch, 'matched.txt')
fs.writeFileSync(matchedChangedList, 'test/cases/compile/enum/shapes.cml\n')
const matchedSelection = runTestCli(['--changed', '--changed-files', matchedChangedList, '--list'])
// Two plans, and one of them follows `--tier`. The quantity is not incidental: a rule-matching
// file selects the plan that mirrors its directory AND the plans the rule adds beside it, and since
// 2026-11-14 the `test/cases/` rule carries `{ "plan": "feat/parse/frontend.plan.toml", "layer":
// true }` (test/plan-selection.json; the reason is in that rule's own comment: the frontend plan
// hashes every `.cml` of the repository, so a case that lands without its record is a file that
// changed with nothing reading it). `select 1 plan(s)` was written on 2026-11-05, when the rule had
// one plan; it was the count of that day, and it is now the count of a tree without `also`. A count
// alone is a thin reading, so both plan files are asserted by name beside it and so is the layer
// mark: a rule that dropped `also` reads 1 and goes red here, and an `also` written without
// `layer` reads `select 2 plan(s)` with no tier note and goes red as well -- which is the case
// worth pinning, because an unlayered frontend plan would run its slow fuzz test on every case
// edit. The paths are compared after `\` is read as `/`: the runner prints the resolved targets as
// `path.resolve` writes them, so on Windows they arrive with backslashes.
const matchedText = matchedSelection.output.replace(/\\/g, '/')
if (!/select 2 plan\(s\), 1 follow --tier/.test(matchedText)) {
    problems.push(
        `--changed on a rule-matching file did not select the plan it mirrors and the plan the rule `
        + `adds beside it: ${matchedSelection.output.trim()}`
    )
}
for (const plan of ['test/plans/feat/compile/enum.plan.toml', 'test/plans/feat/parse/frontend.plan.toml']) {
    if (!matchedText.includes(plan)) {
        problems.push(
            `--changed on a rule-matching file did not name ${plan}: ${matchedSelection.output.trim()}`
        )
    }
}
if (/^\[INFO\] changed: 0 target\(s\)/m.test(matchedSelection.output)) {
    problems.push(
        `--changed printed the zero-target line for a selection that selected plans: ${matchedSelection.output.trim()}`
    )
}
if (fallbackLine(matchedSelection.output)) {
    problems.push(
        `--changed printed the fallback sentence for a selection that selected plans: ${matchedSelection.output.trim()}`
    )
}
fs.rmSync(changedScratch, { recursive: true, force: true })

// --- a benchmark whose timed pass fails --------------------------------------------------------
// gives the `--timeit` failure payload no `mean_ms`; before the runner learnt to read it, the
// payload crashed the runner while it formatted the metric. The test must fail, with the
// diagnostic, and the summary must still be printed.
const benchPlan = path.join(here, 'benchmark_failure.plan.toml')
const bench = failures
// This plan's own lines. The four fixtures of the run refuse eight plans between them, and those
// refusals are printed as runner errors too -- so "the runner did not crash on the payload" is a
// claim about this plan's section, and stays one.
const benchSection = planSection(bench.output, 'selftest.benchmark_failure')
if (/runner error/.test(benchSection)) problems.push('benchmark failure: a runner error was reported')
if (!/\[FAIL\] failing_pass/.test(benchSection)) {
    problems.push(`benchmark failure: the test was not reported as failing:\n${benchSection}`)
}
if (!/the timed pass failed/.test(benchSection)) {
    problems.push('benchmark failure: the payload failure reason is missing')
}
if (!/RuntimeDiag::DivisionByZero/.test(benchSection)) {
    problems.push('benchmark failure: the diagnostic of the failed iteration is missing')
}

// --- the claims taken at a width of their own: two runs -----------------------------------------
// Three fixtures need `--jobs 1`. The serial comparison of the parallel fixture needs it as its
// subject: the same plan at one job must print the same lines in the same order. The card a GPU
// test is given needs it because each of its two readings is one test. The gate split is not about
// the width at all -- it pins the build clock the remote helper exports and the arithmetic that
// adds it to the test's own -- but it needs a run that starts a test, and one run is as good as
// another for it. So the three share two runs, split by the environment each was going to set
// anyway: the one with a build clock and an idle host, and the one with neither.
//
// The gate's wall time is build + test, and the next person has to be able to see a regression in
// it. Without the variable the runner built nothing in this invocation, and says so (0.0), which is
// also the -NoBuild path's value.
const devicePlan = path.join(here, 'free-device', 'device.plan.toml')
const idleListing = [
    '0, GPU-00000000-0000-0000-0000-000000000001, 20000, 24576, NVIDIA GeForce RTX 3090',
    '1, GPU-00000000-0000-0000-0000-000000000002, 12, 24576, NVIDIA GeForce RTX 3090',
    '2, GPU-00000000-0000-0000-0000-000000000003, 20000, 24576, NVIDIA GeForce RTX 3090',
].join('\n')
const busyListing = [
    '0, GPU-00000000-0000-0000-0000-000000000001, 20000, 24576, NVIDIA GeForce RTX 3090',
    '1, GPU-00000000-0000-0000-0000-000000000002, 19000, 24576, NVIDIA GeForce RTX 3090',
    '2, GPU-00000000-0000-0000-0000-000000000003, 20000, 24576, NVIDIA GeForce RTX 3090',
].join('\n')
const serialSamePlan = runRunner([parallelPlan, devicePlan, '--jobs', '1'], {
    CAMEL_GATE_BUILD_SECONDS: '12.5',
    CAMEL_TEST_GPU_CARDS: idleListing,
})
const splitNoBuild = runRunner([tierPlan, devicePlan, '--jobs', '1'], {
    CAMEL_GATE_BUILD_SECONDS: '',
    CAMEL_TEST_GPU_CARDS: busyListing,
})
// The same plan as the parallel run above, one job at a time: the lines, in order, must be the
// parallel run's. Filtered to `parallel.`, because the run also starts the card fixture.
if (serialSamePlan.status !== 0) problems.push(`serial run of parallel.plan: exit ${serialSamePlan.status}`)
// Six of the parallel fixture and two of the card fixture, all of them run: the same arithmetic the
// two separate runs used to print as `6` and `2`, over the union of what this run selected.
if (!/\[SUMMARY\] total: 8, pass: 8, fail: 0\b/.test(serialSamePlan.output)) {
    problems.push(`run at --jobs 1: unexpected summary (${summaryLine(serialSamePlan.output)})`)
}
if (/\[SKIP\]/.test(serialSamePlan.output)) {
    problems.push(`run at --jobs 1: a skip on an idle host, where both card entries must run:\n${serialSamePlan.output}`)
}
const serialOrder = statusLinesMatching(serialSamePlan.output, 'parallel.')
if (parallelOrder.join('\n') !== serialOrder.join('\n')) {
    problems.push(
        `parallel output order differs from serial:\n  parallel: ${parallelOrder.join(', ')}\n  serial:   ${serialOrder.join(', ')}`
    )
}
const splitMatch = serialSamePlan.output.match(/^\[SPLIT\] build ([\d.]+) s, test ([\d.]+) s, total ([\d.]+) s$/m)
if (!splitMatch) {
    problems.push(`gate split: the runner printed no [SPLIT] line:\n${serialSamePlan.output}`)
} else {
    const [build, test, total] = splitMatch.slice(1).map(Number)
    if (build !== 12.5) problems.push(`gate split: build ${build}, expected 12.5 (CAMEL_GATE_BUILD_SECONDS)`)
    if (Math.abs(total - (build + test)) > 0.05) {
        problems.push(`gate split: total ${total} is not build ${build} + test ${test}`)
    }
}
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
const visibility = green
for (const name of ['reference.node', 'hidden.python', 'hidden.precondition', 'declared.python', 'pinned.python']) {
    if (!new RegExp(`^\\[PASS\\] ${name.replace('.', '\\.')}\\b`, 'm').test(visibility.output)) {
        problems.push(`cuda visibility: '${name}' did not pass (${summaryLine(visibility.output)})`)
    }
}
// Its five entries are the run's five `reference|hidden|declared|pinned` entries, and none of them
// may be skipped: a skip would lose the reading the plan was written for. Read by name, because a
// `[SKIP]` anywhere in a run of eight fixtures is some other fixture's business.
const visibilityLines = statusLines(visibility.output).filter((line) =>
    /^(PASS|FAIL|XFAIL|SKIP) (reference|hidden|declared|pinned)\./.test(line)
)
if (visibilityLines.length !== 5 || visibilityLines.some((line) => !line.startsWith('PASS'))) {
    problems.push(
        `cuda visibility: the hidden default, its precondition, the opt-in and a test's own value ` +
            `were not all exercised: ${visibilityLines.join(', ')}`
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

// --- the card a GPU test is given ---------------------------------------------------------------
// A GPU test that names no card used to get device 0, and on a host others share that card is
// often full: the run then died with "CUDA device 0 has no free memory for a context", which read
// as a defect in the code under test. `cuda_free_device = true` makes the choice the runner's
// (docs/test-plans.md, "A card that is not device 0"), and the two answers of the host are pinned
// here on one host: the listing is supplied, so neither case needs a card or a driver.
//
// The busy answer must be a SKIP that names every card, never a pass and never a fail: a skip is
// what this host state is, and a red would be blamed on the code.
//
// The two listings were spent on the two runs above, one each: the idle listing on the run taken at
// `--jobs 1` with a build clock, the busy one on the run with neither. Their entries are their own
// names (`free_card`, `names_its_own_card`), so a summary of the run cannot stand in for them and
// is not asked to.
const idle = serialSamePlan
if (!/^\[PASS\] free_card\b/m.test(idle.output)) {
    problems.push(
        `free device: the test was not given the lowest idle card of the listing, so its exact ` +
            `stdout assertion failed:\n${idle.output}`
    )
}
if (!/^\[PASS\] names_its_own_card\b/m.test(idle.output)) {
    problems.push('free device: a test that named its own card was not run on an idle host')
}

const busy = splitNoBuild
// The other run's own arithmetic: five entries of the tier fixture and two of the card fixture, one
// of the two skipped. A host with no idle card must not fail the run, and the skip must be exactly
// one -- which is what `skip: 1` counts, and what the two named reads below pin to the right entry.
if (busy.status !== 0) {
    problems.push(`free device: a host with no idle card failed the run (exit ${busy.status})`)
}
if (!/\[SUMMARY\] total: 7, pass: 6, fail: 0, skip: 1\b/.test(busy.output)) {
    problems.push(`free device: unexpected summary (${summaryLine(busy.output)})`)
}
if (!/^\[SKIP\] free_card\b/m.test(busy.output) || /^\[FAIL\] free_card\b/m.test(busy.output)) {
    problems.push(`free device: no idle card did not skip the test by name:\n${busy.output}`)
}
for (const text of [
    'no idle CUDA device',
    'all 3 cards on this host are held by another process',
    'card 0 20000/24576 MiB',
    'card 2 20000/24576 MiB',
]) {
    if (!busy.output.includes(text)) problems.push(`free device: the skip reason lacks: ${text}`)
}
// A test that writes the variable itself is neither given a card nor skipped for want of one: the
// skip is the selection's, and it does not reach past a test that made its own decision.
if (!/^\[PASS\] names_its_own_card\b/m.test(busy.output)) {
    problems.push('free device: a test that named its own card was skipped or failed')
}
// The counts the run's own summary carried: two entries on the idle host, both run; two on the busy
// one, one run and one skipped. Read as the two names' lines rather than as a total, because both
// runs now carry other fixtures too -- and a `[SKIP]` that reached any other entry of either run
// was never one of these two fixtures' claims and would have made this line red in the old shape.
if (statusLines(busy.output).filter((line) => line.startsWith('SKIP ') && !line.startsWith('SKIP free_card')).length > 0) {
    problems.push(`free device: a skip reached an entry of another fixture:\n${busy.output}`)
}

// A reading, printed and used by nothing: how many child runs it took, what width this file put
// them at, and what the run cost in wall time on this host. `timeout_ms` is untouched -- that is
// what makes this line safe to add, and why it is a reading rather than a decision.
//
// Printed before the verdict, and on a failing run too. The run that most needs to know the width
// its numbers were taken at is the one that went red, and a check that only speaks when it is green
// is the shape this project keeps calling a fake reading: on the day the sweep fixture came back
// `pass: 0` the line was never printed, because the file exits with `problems` first.
//
// The CPU figure this line used to print is gone, and it is not coming back as `process.cpuUsage()`:
// that call charges this process only, and nearly all of the work here is in children, so it read
// close to 0 -- a number of the wrong kind, printed beside one of the right kind. The reading that
// decides is the runner's: every test, this file included, runs under `test/tools/child-cpu.mjs`,
// which reaps exactly one command and reports the CPU the kernel charged it and every process it
// waited for, this file's children and their children among them. See the measurements in
// test/plans/feat/consistency/runner.plan.toml.
//
// Reading the children from here instead would mean running each one through child-cpu.mjs, and that
// wrapper is a Node process of its own: ~40 ms of CPU apiece over the 22 child runs below, added to
// the very figure it measures -- seconds against a bound (13.0 s of work plus a 0.5 s band) that this
// file's own runs are judged by. A measurement that moves the thing it measures is worse than the
// number it replaces, so the reading stays where the runner makes it.
const spentWall = Number(process.hrtime.bigint() - cost.wallStart) / 1e9
// `width` is what was appended, taken from the set of values actually passed at the spawn; with the
// constant blanked the set is empty and the line says `inherited`. Four buckets that add up to the
// child count printed beside them: `fixed` took our `--jobs`, `own` named one in the call, `cli` only
// resolved a target, and `inherited` took the runner's default width.
const widthTaken = widthsPassed.size > 0 ? `fixed(${[...widthsPassed].join('|')})` : 'inherited'
console.log(
    `[calibration] child-runs=${cost.children}; width=${widthTaken}; `
        + `fixed=${widthTally.fixed}, own=${widthTally.named}, inherited=${widthTally.inherited}, `
        + `cli=${widthTally.cli}; wall=${spentWall.toFixed(1)} s`
)
// The entries a sweep does not cover, counted rather than described: what the source plans of every
// `[[sweeps]]` entry declare as `negative`, how many of those a sweep copies, how many it leaves out
// and which ruling the code implements (`a`: a `negative` entry is copied; `b`: its kind is left
// out). It reads the plan tree through the expansion itself, so it cannot disagree with the run.
//
// It is here because the count and the comment disagreed once and nothing said so: from the
// sweep's first commit the kind filter left `negative` out while the docstring above `expandSweeps`
// named only benchmarks and differential tests as left out, and 1258 entries -- 73% of the tree's
// `negative` ones -- were never copied. A line that prints the three numbers on every gate run is
// the difference between a coverage gap somebody has to remember and one that is in front of the
// reader; the counts are a reading, so they are printed and not asserted.
//
// The walk is the runner's own loader over the plan tree, a couple of hundred milliseconds for the
// whole tree (0.197 s over 372 plan files, measured on this machine), which is why it can sit in
// this file instead of needing a check of its own.
const sweepAccounting = negativeSweepAccounting(allPlanFiles)
console.log(negativeSweepLine(sweepAccounting))
if (problems.length > 0) {
    console.error(problems.join('\n'))
    console.error('--- runner output ---')
    console.error(output)
    process.exit(1)
}
console.log('runner self-test ok')