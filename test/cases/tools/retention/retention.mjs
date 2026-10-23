#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The bound the runner enforces on its own output tree, as a property.
 *
 * A run directory is the runner's own business and nothing in the repository reads an old one back,
 * so it may be pruned; a directory a live run owns may not, and neither may one the runner did not
 * write. That is the whole policy (test/tools/runner-output.mjs), and this check states it twice:
 *
 *   `--policy`  drives the policy itself over a planted tree under `${test_tmp}` -- five run
 *               directories whose runs are gone, one whose pid is this process's (a live run owning
 *               a directory that is outside the kept window by age), one whose name the runner never
 *               writes, and five scratch roots (stale, live, young, foreign, this run's own). The
 *               same fixture is planted twice: the first copy is pruned with a bound of two, the
 *               second with a bound that covers every directory. What the first loses and the
 *               second keeps is the same tree under two bounds, so the removals can only come from
 *               the bound -- a policy that pruned nothing fails here, and so does one that pruned
 *               everything. The entries and bytes removed are asserted against what was planted, so
 *               the accounting a run prints is the tree's own numbers and not a decoration.
 *
 *   `--runner`  runs the real runner twice (`CAMEL_TEST_RESULTS_ROOT` into a scratch tree,
 *               `CAMEL_TEST_RUN_DIRS_KEEP=1`) over the same kind of planted tree, and then asserts
 *               what a person would: the two runs' own directories are there with their manifest
 *               and summary, every planted directory whose run is gone is gone, the one a live run
 *               owns and the one the runner never wrote are untouched, and the run's record holds
 *               no copy of a plan or a case -- the manifest names the plan and each test's
 *               `meta.json` names the two paths a reader needs. It also requires the nested runs to
 *               have printed their own `[RETAIN]` line with a non-zero removal count: a runner that
 *               stopped calling the policy fails here, not silently.
 *
 * Usage: node retention.mjs --policy | --runner     (uses CAMEL_TEST_TMPDIR, else a fresh temp dir)
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'
import { LOG_DIR, SCRATCH_DIR, pruneRunnerOutput, processAlive, runnerOutputPath } from '../../../tools/runner-output.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const RUNNER = path.join(REPO_ROOT, 'test', 'tools', 'run-tests.mjs')
const NESTED_PLAN = path.join(REPO_ROOT, 'test', 'tools', 'selftest', 'scratch.plan.toml')
const MODE = process.argv[2] || ''
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-retention-'))
const OWNED_SCRATCH = !process.env.CAMEL_TEST_TMPDIR

// Pids no process has, on either host: above Linux's `pid_max` and ids Windows never hands out.
// `processAlive` answers "gone" for them on both, so a planted directory is one whose run is gone
// without the check depending on when some child of this process happened to exit. The pid that is
// certainly alive is this process's own.
const DEAD_PID = 2147483646
const OTHER_DEAD_PID = 2147483645
const THIRD_DEAD_PID = 2147483644
const LIVE_PID = process.pid

const BASE_MS = Date.UTC(2026, 0, 1)
const MINUTE_MS = 60000
const HOUR_MS = 60 * 60 * 1000
// One planted run directory: `pass/`, `planted.plan/`, `planted.test/`, `stdout.txt`, `meta.json`.
const PLANTED_BYTES = 1024 + 512
const PLANTED_ENTRIES = 5
const RUN_DIR_COUNT = 5

// The names, with the placeholder that carries the pid each one is planted for.
const RUN_DIR_NAMES = {
    dead: Array.from({ length: RUN_DIR_COUNT }, (_, index) => `20260101_00000${index + 1}_${DEAD_PID}`),
    live: `20200101_000000_${LIVE_PID}`,
    foreign: 'a-directory-a-person-made',
}
const SCRATCH_ROOT_NAMES = {
    stale: `run-${DEAD_PID}`,
    live: `run-${LIVE_PID}`,
    young: `run-${OTHER_DEAD_PID}`,
    foreign: 'not-a-scratch-root',
    current: `run-${THIRD_DEAD_PID}`,
}

let failures = 0
// The one line this case prints when every check passed, set by whichever mode ran.
let summary = ''
function check(ok, message) {
    if (!ok) {
        failures++
        console.log(`FAIL ${message}`)
    }
}

function names(dir) {
    try {
        return fs.readdirSync(dir).sort()
    } catch {
        return []
    }
}

/** One planted run directory, with its payload and the mtime that decides its place in the order. */
function plantRunDir(logRoot, name, modifiedMs) {
    const statusDir = path.join(logRoot, name, 'pass', 'planted.plan', 'planted.test')
    fs.mkdirSync(statusDir, { recursive: true })
    fs.writeFileSync(path.join(statusDir, 'stdout.txt'), 'p'.repeat(1024))
    fs.writeFileSync(path.join(statusDir, 'meta.json'), 'm'.repeat(512))
    const when = new Date(modifiedMs)
    fs.utimesSync(path.join(logRoot, name), when, when)
}

function plantScratchRoot(scratchRoot, name, modifiedMs) {
    const dir = path.join(scratchRoot, name)
    fs.mkdirSync(path.join(dir, 'planted.plan', 'planted.test'), { recursive: true })
    fs.writeFileSync(path.join(dir, 'plan-kept.txt'), 'n'.repeat(64))
    const when = new Date(modifiedMs)
    fs.utimesSync(dir, when, when)
}

/**
 * The same tree every time: five directories whose runs are gone (one minute apart, oldest first),
 * one older still and owned by this live process, and one whose name the runner never writes. The
 * scratch side is the same shape one level down: a stale root, a live one, a young one, a foreign
 * one, and this run's own.
 */
function buildFixture(root) {
    const logRoot = runnerOutputPath(LOG_DIR, root)
    const scratchRoot = runnerOutputPath(SCRATCH_DIR, root)
    fs.rmSync(root, { recursive: true, force: true })
    fs.mkdirSync(logRoot, { recursive: true })
    fs.mkdirSync(scratchRoot, { recursive: true })
    const placed = [
        [RUN_DIR_NAMES.foreign, 0],
        [RUN_DIR_NAMES.live, 1],
        ...RUN_DIR_NAMES.dead.map((name, index) => [name, 2 + index]),
    ]
    for (const [name, minutes] of placed) plantRunDir(logRoot, name, BASE_MS + minutes * MINUTE_MS)
    const now = Date.now()
    plantScratchRoot(scratchRoot, SCRATCH_ROOT_NAMES.stale, now - 3 * HOUR_MS)
    plantScratchRoot(scratchRoot, SCRATCH_ROOT_NAMES.live, now - 3 * HOUR_MS)
    plantScratchRoot(scratchRoot, SCRATCH_ROOT_NAMES.young, now)
    plantScratchRoot(scratchRoot, SCRATCH_ROOT_NAMES.foreign, now - 3 * HOUR_MS)
    plantScratchRoot(scratchRoot, SCRATCH_ROOT_NAMES.current, now - 3 * HOUR_MS)
    return {
        root,
        logRoot,
        scratchRoot,
        planted: placed.map(([name]) => name),
        ownedScratchRoot: path.join(scratchRoot, SCRATCH_ROOT_NAMES.current),
    }
}

/** Everything under `dir`, as base names, one entry at a time. */
function fileNamesUnder(dir) {
    const found = []
    const stack = [dir]
    while (stack.length > 0) {
        const current = stack.pop()
        for (const child of fs.readdirSync(current, { withFileTypes: true })) {
            const full = path.join(current, child.name)
            if (child.isDirectory()) stack.push(full)
            else if (child.isFile()) found.push(child.name)
        }
    }
    return found
}

// --- the policy itself --------------------------------------------------------------------------
function policyMode() {
    const pruned = buildFixture(path.join(SCRATCH, 'policy-pruned'))
    const control = buildFixture(path.join(SCRATCH, 'policy-control'))
    const dead = RUN_DIR_NAMES.dead
    const removedExpected = dead.slice(0, RUN_DIR_COUNT - 2)
    const keptExpected = [...dead.slice(-2), RUN_DIR_NAMES.live, RUN_DIR_NAMES.foreign]

    check(processAlive(DEAD_PID) === false, `a pid past either host's range must read as gone (${DEAD_PID})`)
    check(processAlive(LIVE_PID) === true, 'this process must read as alive')

    const report = pruneRunnerOutput({
        repoRoot: pruned.root,
        keep: 2,
        currentScratchRoot: pruned.ownedScratchRoot,
    })
    // A prune that examined nothing is a failure, not a pass.
    check(
        report.run_dirs.scanned === pruned.planted.length && report.run_dirs.scanned > 0,
        `the prune must have examined the planted directories: scanned ${report.run_dirs.scanned} of ${pruned.planted.length}`
    )
    for (const name of removedExpected) {
        check(!fs.existsSync(path.join(pruned.logRoot, name)), `a run directory older than the bound was kept: ${name}`)
    }
    for (const name of keptExpected) {
        check(fs.existsSync(path.join(pruned.logRoot, name)), `a run directory the bound keeps was removed: ${name}`)
    }
    check(
        report.run_dirs.removed_names.slice().sort().join(',') === removedExpected.slice().sort().join(','),
        `removed ${report.run_dirs.removed_names.join(', ') || 'nothing'}, expected only ${removedExpected.join(', ')}`
    )
    check(report.run_dirs.live === 1, `the live run's directory must be kept for its pid, not its age (live ${report.run_dirs.live})`)
    check(report.run_dirs.foreign === 1, `a directory the runner never wrote must be counted, not removed (foreign ${report.run_dirs.foreign})`)
    check(
        report.run_dirs.entries_removed === removedExpected.length * PLANTED_ENTRIES
            && report.run_dirs.bytes_removed === removedExpected.length * PLANTED_BYTES,
        `the removal must be counted while it happens: ${report.run_dirs.entries_removed} entries, `
            + `${report.run_dirs.bytes_removed} bytes (expected ${removedExpected.length * PLANTED_ENTRIES} and `
            + `${removedExpected.length * PLANTED_BYTES})`
    )

    // The scratch roots: a stale one whose run is gone goes, and a live one, a young one, a foreign
    // one and this run's own stay, whatever their age.
    const scratchKept = [
        SCRATCH_ROOT_NAMES.live,
        SCRATCH_ROOT_NAMES.young,
        SCRATCH_ROOT_NAMES.foreign,
        SCRATCH_ROOT_NAMES.current,
    ]
    check(!fs.existsSync(path.join(pruned.scratchRoot, SCRATCH_ROOT_NAMES.stale)), 'a stale scratch root was kept')
    for (const name of scratchKept) {
        check(fs.existsSync(path.join(pruned.scratchRoot, name)), `a scratch root the policy keeps was removed: ${name}`)
    }
    check(
        report.scratch_roots.scanned === Object.keys(SCRATCH_ROOT_NAMES).length,
        `the prune examined ${report.scratch_roots.scanned} scratch roots, expected ${Object.keys(SCRATCH_ROOT_NAMES).length}`
    )

    // Twice is the same as once: the bound is a state, not an event.
    const again = pruneRunnerOutput({
        repoRoot: pruned.root,
        keep: 2,
        currentScratchRoot: pruned.ownedScratchRoot,
    })
    check(
        again.run_dirs.removed === 0 && again.scratch_roots.removed === 0,
        'a second prune removed something the first should already have settled'
    )

    // The control: the same tree, a bound that covers every directory. Nothing may be removed, so
    // what the first copy lost can only have been lost to the bound.
    const held = pruneRunnerOutput({ repoRoot: control.root, keep: control.planted.length + 1, scratchKeepMs: Infinity })
    check(
        held.run_dirs.removed === 0 && held.scratch_roots.removed === 0,
        `with no bound to enforce, nothing may be removed (${held.run_dirs.removed} run dirs, ${held.scratch_roots.removed} roots)`
    )
    for (const name of control.planted) {
        check(fs.existsSync(path.join(control.logRoot, name)), `the control lost a run directory: ${name}`)
    }

    summary = `PASS retention.policy: a bound of 2 removed ${report.run_dirs.removed} of ${report.run_dirs.scanned} run directories `
        + `(${report.run_dirs.entries_removed} entries, ${report.run_dirs.bytes_removed} bytes), kept the newest two, the live run's `
        + `and the foreign one; the same tree under a bound of ${control.planted.length + 1} lost nothing`
}

// --- two real runs ------------------------------------------------------------------------------
function runnerMode() {
    const resultsRoot = path.join(SCRATCH, 'results')
    const logRoot = path.join(resultsRoot, 'log')
    fs.rmSync(resultsRoot, { recursive: true, force: true })
    fs.mkdirSync(logRoot, { recursive: true })
    RUN_DIR_NAMES.dead.forEach((name, index) => plantRunDir(logRoot, name, BASE_MS + index * MINUTE_MS))
    plantRunDir(logRoot, RUN_DIR_NAMES.live, BASE_MS - MINUTE_MS)
    plantRunDir(logRoot, RUN_DIR_NAMES.foreign, BASE_MS - 2 * MINUTE_MS)

    const nested = []
    for (let attempt = 0; attempt < 2; attempt++) {
        const proc = spawnSync(process.execPath, [RUNNER, NESTED_PLAN, '--jobs', '2'], {
            cwd: REPO_ROOT,
            encoding: 'utf8',
            windowsHide: true,
            timeout: 120000,
            env: { ...process.env, CAMEL_TEST_RESULTS_ROOT: resultsRoot, CAMEL_TEST_RUN_DIRS_KEEP: '1' },
        })
        const output = `${proc.stdout || ''}${proc.stderr || ''}`.replace(/\x1b\[[0-9;]*m/g, '')
        nested.push({ pid: proc.pid, status: proc.status, output })
        check(
            proc.status === 0 && output.includes('[SUMMARY] total: 3, pass: 3, fail: 0'),
            `nested run ${attempt + 1} did not pass its plan (exit ${proc.status}${proc.error ? `, ${proc.error.message}` : ''}): `
                + output.slice(-400)
        )
    }
    // The runner enforces the bound itself: the first nested run must report what it pruned.
    const receipt = nested[0].output.match(/^\[RETAIN\].*$/m)
    check(
        Boolean(receipt) && /run dirs: \d+ scanned, [1-9]\d* removed/.test(receipt[0]),
        `the nested run did not prune: ${receipt ? receipt[0] : 'no [RETAIN] line'}`
    )

    // The two runs' own directories, found by the pid the runner names them for.
    const ownDirs = nested.map(({ pid }) => names(logRoot).find((name) => name.endsWith(`_${pid}`)))
    nested.forEach(({ pid }, index) => {
        check(Boolean(ownDirs[index]), `the run directory of nested run ${index + 1} (pid ${pid}) is not there: ${names(logRoot).join(', ')}`)
    })
    for (const name of RUN_DIR_NAMES.dead) {
        check(!fs.existsSync(path.join(logRoot, name)), `a run directory whose run is gone was left behind: ${name}`)
    }
    check(fs.existsSync(path.join(logRoot, RUN_DIR_NAMES.live)), "a live run's directory was removed")
    check(fs.existsSync(path.join(logRoot, RUN_DIR_NAMES.foreign)), 'a directory the runner never wrote was removed')

    const lastRun = ownDirs[1] ? path.join(logRoot, ownDirs[1]) : null
    let recorded = 0
    if (lastRun) {
        check(fs.existsSync(path.join(lastRun, 'summary.json')), 'the run did not write its summary.json')
        const manifestPath = path.join(lastRun, 'run.json')
        check(fs.existsSync(manifestPath), 'the run did not write its manifest')
        if (fs.existsSync(manifestPath)) {
            const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'))
            check(
                Array.isArray(manifest.plans) && manifest.plans.some((plan) => plan.endsWith('test/tools/selftest/scratch.plan.toml')),
                `the manifest does not name the plan that ran: ${JSON.stringify(manifest.plans)}`
            )
            check(
                typeof manifest.revision === 'string' && /^[0-9a-f]{40}$/.test(manifest.revision),
                `the manifest does not name the revision it ran at: ${JSON.stringify(manifest.revision)}`
            )
        }
        // Nothing of the checkout is copied into the record, and the two paths a reader needs are in
        // the test's own meta.json.
        const recordNames = new Set([
            'stdout.txt', 'stderr.txt', 'output.txt', 'meta.json',
            'verify_stdout.txt', 'verify_stderr.txt', 'summary.json', 'run.json',
        ])
        const seen = fileNamesUnder(lastRun)
        recorded = seen.length
        const unexpected = [...new Set(seen.filter((name) => !recordNames.has(name)))]
        check(unexpected.length === 0, `the run's record holds files that are not its own: ${unexpected.join(', ')}`)
        const metaPath = path.join(lastRun, 'pass', 'selftest.scratch', 'scratch.absent', 'meta.json')
        check(fs.existsSync(metaPath), `a test's meta.json is missing: ${metaPath}`)
        if (fs.existsSync(metaPath)) {
            const meta = JSON.parse(fs.readFileSync(metaPath, 'utf8'))
            check(
                typeof meta.plan_path === 'string' && fs.existsSync(meta.plan_path)
                    && typeof meta.case_path === 'string' && fs.existsSync(meta.case_path),
                `a test's meta.json must name the plan and the case that produced it: ${JSON.stringify([meta.plan_path, meta.case_path])}`
            )
        }
    }

    summary = `PASS retention.runner: two runs with a bound of 1 kept their own two directories and the live run's, `
        + `removed all ${RUN_DIR_NAMES.dead.length} directories whose runs were gone, left the foreign one, `
        + `and recorded ${recorded} files, none of them a copy of the checkout`
}

if (MODE === '--policy') {
    policyMode()
} else if (MODE === '--runner') {
    runnerMode()
} else {
    console.log('usage: node retention.mjs --policy | --runner')
    process.exit(2)
}

if (failures > 0) {
    console.log(`FAIL retention: ${failures} check(s) failed`)
    if (OWNED_SCRATCH) console.log(`the planted tree is at ${SCRATCH}`)
    process.exit(1)
}
if (OWNED_SCRATCH) fs.rmSync(SCRATCH, { recursive: true, force: true })
console.log(summary)