// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Self-test of the two load-class guards:
//
//   * test/tools/check-load-class.mjs must fire on bad/ (two tests writing one path, a fixed
//     port, a fixed temporary path in a case) and pass on good/ (the same shape under
//     ${test_tmp}). A guard that cannot fail is not a guard;
//   * the runner's margin rule (TIMEOUT_MARGIN in test/tools/run-tests.mjs) must fail a test
//     that uses a quarter of its timeout or more (margin/tight.plan.toml: 400 ms against
//     1000 ms) and pass the same work with room (margin/room.plan.toml: 400 ms against 120 s,
//     which no amount of load on a working machine turns into 30 s);
//   * a test that declares `cpu_budget_ms` is judged on its own CPU time instead of the wall
//     clock (the author's ruling of 2026-10-23, quoted in run-tests.mjs and
//     docs/test-plans.md): the same waiting that margin/tight.plan.toml fails passes when the
//     bound is CPU (cpu-budget/room.plan.toml), CPU work over the budget is failed and named
//     (cpu-budget/over.plan.toml), and a test that stops working is still killed by its
//     liveness bound (cpu-budget/liveness.plan.toml). A CPU budget with nothing on the other
//     side of it would be a bound that examined nothing: cpu-budget/no-report.plan.toml is
//     green in its exit code and its stdout and fails anyway, because the wrapper's reading
//     never arrives.
//
//   node test/tools/selftest/load-class/run.mjs
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(here, '..', '..', '..', '..')
const checker = path.join(repoRoot, 'test', 'tools', 'check-load-class.mjs')
const runner = path.join(repoRoot, 'test', 'tools', 'run-tests.mjs')

const problems = []
const stripAnsi = (text) => text.replace(/\x1b\[[0-9;]*m/g, '')

function run(command, args) {
    const proc = spawnSync(command, args, { encoding: 'utf8', cwd: repoRoot })
    return { status: proc.status, output: stripAnsi(`${proc.stdout}${proc.stderr}`) }
}

function expect(name, condition, detail) {
    if (!condition) problems.push(`${name}: ${detail}`)
}

// (1) The shared-place guard is red on the bad fixture, for each of the three rules.
{
    const bad = run(process.execPath, [checker, '--root', path.join(here, 'bad')])
    expect('bad exits 1', bad.status === 1, `exit ${bad.status}`)
    expect(
        'bad reports the shared path',
        bad.output.includes('writer.one') && bad.output.includes('writer.two') &&
            bad.output.includes('selftest-shared.out'),
        bad.output
    )
    expect(
        'bad reports the fixed port',
        bad.output.includes('listener.fixed_port') && bad.output.includes('8765'),
        bad.output
    )
    expect(
        'bad reports the fixed temporary path',
        bad.output.includes('/tmp/selftest-load-class.log'),
        bad.output
    )
}
{
    const good = run(process.execPath, [checker, '--root', path.join(here, 'good')])
    expect('good exits 0', good.status === 0, good.output)
    expect('good passes', good.output.includes('PASS every test writes'), good.output)
}

// (2) The runner's margin rule fires on a tight bound and not on a real one.
{
    const tight = run(process.execPath, [
        runner,
        path.join(repoRoot, 'test', 'tools', 'selftest', 'margin', 'tight.plan.toml'),
        '--jobs',
        '1',
    ])
    expect('tight run exits 1', tight.status === 1, `exit ${tight.status}: ${tight.output}`)
    expect(
        'tight run names the margin',
        tight.output.includes('[FAIL] tight_bound') && tight.output.includes('margin'),
        tight.output
    )
}
{
    const room = run(process.execPath, [
        runner,
        path.join(repoRoot, 'test', 'tools', 'selftest', 'margin', 'room.plan.toml'),
        '--jobs',
        '1',
    ])
    expect('room run exits 0', room.status === 0, room.output)
    expect('room run passes', room.output.includes('[PASS] room_to_spare'), room.output)
}

// (3) A test whose bound is its own CPU time is judged on CPU and not on the wall clock.
{
    const room = run(process.execPath, [
        runner,
        path.join(repoRoot, 'test', 'tools', 'selftest', 'cpu-budget', 'room.plan.toml'),
        '--jobs',
        '1',
    ])
    expect('cpu-budget room exits 0', room.status === 0, `exit ${room.status}: ${room.output}`)
    expect(
        'cpu-budget room passes with the wall clock over the margin',
        room.output.includes('[PASS] cpu_budget_load_room') && room.output.includes('cpu of'),
        room.output
    )
}
{
    const over = run(process.execPath, [
        runner,
        path.join(repoRoot, 'test', 'tools', 'selftest', 'cpu-budget', 'over.plan.toml'),
        '--jobs',
        '1',
    ])
    expect('cpu-budget over exits 1', over.status === 1, `exit ${over.status}: ${over.output}`)
    expect(
        'cpu-budget over names the CPU figure and the budget',
        over.output.includes('[FAIL] cpu_budget_exceeded') &&
            over.output.includes('of CPU against a cpu_budget_ms'),
        over.output
    )
}
{
    const liveness = run(process.execPath, [
        runner,
        path.join(repoRoot, 'test', 'tools', 'selftest', 'cpu-budget', 'liveness.plan.toml'),
        '--jobs',
        '1',
    ])
    expect('cpu-budget liveness exits 1', liveness.status === 1, `exit ${liveness.status}: ${liveness.output}`)
    expect(
        'cpu-budget liveness is killed by the timeout, not failed on CPU',
        liveness.output.includes('[FAIL] cpu_budget_liveness') &&
            liveness.output.includes('liveness bound') &&
            !liveness.output.includes('of CPU against a cpu_budget_ms'),
        liveness.output
    )
}

{
    const missing = run(process.execPath, [
        runner,
        path.join(repoRoot, 'test', 'tools', 'selftest', 'cpu-budget', 'no-report.plan.toml'),
        '--jobs',
        '1',
    ])
    expect('cpu-budget no-report exits 1', missing.status === 1, `exit ${missing.status}: ${missing.output}`)
    expect(
        'cpu-budget no-report fails for the missing reading alone',
        missing.output.includes('[FAIL] cpu_budget_no_report') &&
            missing.output.includes('[SUMMARY] total: 1, pass: 0, fail: 1') &&
            missing.output.includes('has no CPU reading') &&
            // The fixture makes the wrapper's one read of /proc/self/stat fail, so the runner must
            // name that reading and not a missing report: a report it never opened would be a
            // different case.
            missing.output.includes('no /proc/self/stat') &&
            // The command exited 0 and printed exactly what the plan expects: if either of those
            // had failed, the run would say so, and this fixture would be proving something else.
            !missing.output.includes('expected exit 0, got') &&
            !missing.output.includes('stdout does not contain') &&
            !missing.output.includes('expect_stdout'),
        missing.output
    )
}

// The fixture run writes a log directory under test/results like any other run; the outer
// runner keeps its own. Nothing here deletes a path the caller owns.
if (problems.length > 0) {
    for (const problem of problems) console.log(`FAILED: ${problem}`)
    process.exit(1)
}
console.log(
    'load-class self-test ok: 3 shared-place rules fire on a bad plan, none on a good one, ' +
        'the runner refuses a timeout with no margin, a CPU budget passes what the wall clock ' +
        'fails, fails work over it, leaves the liveness bound to catch a test that waits, and ' +
        'fails a test whose CPU reading never arrives even when its exit code and stdout are green'
)