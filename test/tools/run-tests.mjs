// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import fs from 'fs'
import path from 'path'
import os from 'os'
import { performance } from 'perf_hooks'
import { spawn, spawnSync } from 'child_process'
import { fileURLToPath } from 'url'
import { parseTomlFile } from './toml-lite.mjs'
import { compareGirDotIsomorphic } from './gir-dot.mjs'
import { applyNormalizers } from './normalizers.mjs'
import { CUDA_HIDDEN, CUDA_VISIBLE_DEVICES, hidesCudaDevices, pythonEntryPoints } from './cuda-visibility.mjs'
import { RUN_DIRS_KEPT, SCRATCH_DIR, formatRetention, pruneRunnerOutput, runnerOutputPath } from './runner-output.mjs'

const __filename = fileURLToPath(import.meta.url)
const TOOLS_ROOT = path.dirname(__filename)
const TEST_ROOT = path.dirname(TOOLS_ROOT)
const PLANS_ROOT = path.join(TEST_ROOT, 'plans')
const VARS_PATH = path.join(TEST_ROOT, 'vars.toml')
// The wrapper a `cpu_budget_ms` test runs under: it reaps exactly that test's process tree and
// reports the CPU time the kernel charged it on file descriptor 3 (its header has the why; the
// rule it implements is quoted where the reading is judged, in runOneTest).
const CHILD_CPU_TOOL = path.join(TOOLS_ROOT, 'child-cpu.mjs')
// Whether this host's kernel can report the CPU time of a process tree at all: the reading
// test/tools/child-cpu.mjs makes is `cutime` + `cstime` of /proc/self/stat, which no other platform
// has. Where it exists the wrapper runs for every test and the margin rule is judged on CPU time;
// where it does not (a Windows or macOS developer machine) the wrapper cannot report a figure
// either, so it is not started for the tests that do not ask for one and the margin rule falls back
// to the wall clock -- and the run prints that it did, once. The shared host the gate runs on is
// Linux, so the fallback never decides a gate.
const CAN_READ_CHILD_CPU = fs.existsSync('/proc/self/stat')
// Where a run's record goes. `test/results` unless CAMEL_TEST_RESULTS_ROOT names another tree: a
// check that has to assert what the retention kept drives two real runs into a scratch tree
// (test/cases/tools/retention/retention.mjs), and the tree it prunes is the one it writes.
const RESULTS_ROOT = process.env.CAMEL_TEST_RESULTS_ROOT
    ? path.resolve(process.env.CAMEL_TEST_RESULTS_ROOT)
    : path.join(TEST_ROOT, 'results')
const LOG_ROOT = path.join(RESULTS_ROOT, 'log')
const REPO_ROOT = path.dirname(TEST_ROOT)
const IS_WINDOWS = process.platform === 'win32'
// The build a run addresses. A Release build lands in out/latest; a Debug build has its own tree
// at out/debug (scripts/collect-out.js), so `--build debug` cannot silently read a Release binary
// and the two configurations can be tested without rebuilding one over the other.
const BUILDS = {
    release: { dir: 'latest', label: 'Release' },
    debug: { dir: 'debug', label: 'Debug' },
}

/**
 * The fields a plan file may contain. A key that is not here is an error, not a comment: a test
 * that wrote `expect_stdout_exact` under a misspelled name used to pass while asserting nothing.
 * `desc` is human-readable documentation, and `perf_budget_platform` is the provenance of a
 * benchmark budget; both are accepted, and neither turns into an assertion.
 */
const PLAN_FIELDS = new Set([
    'id', 'desc', 'order', 'tier', 'debug_tier', 'parallel', 'tags', 'defaults', 'tests', 'sweeps',
])
const TEST_FIELDS = new Set([
    'name', 'desc', 'kind', 'case', 'args', 'envs', 'tool', 'timeout_ms', 'cpu_budget_ms', 'verify_timeout_ms',
    'skip_unless', 'skip_unless_timeout_ms', 'parallel', 'tier', 'sweep', 'cuda_devices',
    'known_fail', 'known_fail_note', 'failure_note',
    'backends', 'backends_agree', 'compare_to', 'compare_fields',
    'golden', 'snapshot_stream', 'snapshot_compare', 'normalizers',
    'expect_exit', 'expect_stdout_contains', 'expect_stdout_exact', 'expect_stdout',
    'expect_stdout_starts_with', 'expect_stdout_not_contains', 'expect_stderr_contains',
    'expect_output_contains', 'expect_output_not_contains',
    'expect_diagnostic_name', 'expect_diagnostic_code',
    'expect_diagnostic_line', 'expect_diagnostic_column',
    'perf_budget_mean_ms', 'perf_budget_tolerance_pct', 'perf_budget_platform',
    'verify_script', 'verify_script_path', 'verify_js',
])
const SWEEP_FIELDS = new Set([
    'name', 'plans', 'exclude', 'envs', 'skip_env_prefix', 'timeout_scale',
    'fast_sample', 'risk_tags', 'risk_sample',
])

/** A plan file that contradicts the schema; it fails the plan, not the run. */
class PlanSchemaError extends Error {
    constructor(message) {
        super(message)
        this.name = 'PlanSchemaError'
    }
}

function planLabel(planPath) {
    const relative = path.relative(REPO_ROOT, planPath)
    if (relative && !relative.startsWith('..') && !path.isAbsolute(relative)) {
        return relative.split(path.sep).join('/')
    }
    return planPath
}

/** Levenshtein distance, for "did you mean": a one-letter typo must name the field it meant. */
function editDistance(lhs, rhs) {
    let previous = Array.from({ length: rhs.length + 1 }, (_, index) => index)
    for (let i = 1; i <= lhs.length; i++) {
        const current = [i]
        for (let j = 1; j <= rhs.length; j++) {
            const substitute = previous[j - 1] + (lhs[i - 1] === rhs[j - 1] ? 0 : 1)
            current[j] = Math.min(previous[j] + 1, current[j - 1] + 1, substitute)
        }
        previous = current
    }
    return previous[rhs.length]
}

function closestField(key, allowed) {
    let best = null
    let bestDistance = 3
    for (const field of allowed) {
        const distance = editDistance(key, field)
        if (distance < bestDistance) {
            best = field
            bestDistance = distance
        }
    }
    return best
}

function checkFields(planPath, entry, allowed, where) {
    for (const key of Object.keys(entry)) {
        if (allowed.has(key)) continue
        const suggestion = closestField(key, allowed)
        const hint = suggestion ? `; did you mean '${suggestion}'?` : ''
        throw new PlanSchemaError(`${planLabel(planPath)}: ${where}: unknown key '${key}'${hint}`)
    }
}

function checkFieldType(planPath, entry, field, where, type) {
    const value = entry[field]
    if (value === undefined) return
    const ok = type === 'string'
        ? typeof value === 'string'
        : type === 'number'
            ? typeof value === 'number' && Number.isFinite(value)
            : type === 'boolean'
                ? typeof value === 'boolean'
                : false
    if (ok) return
    const shown = type === 'string' ? JSON.stringify(value) : String(value)
    throw new PlanSchemaError(`${planLabel(planPath)}: ${where}: ${field} must be a ${type}, got ${shown}`)
}

/**
 * Refuse a plan the runner does not fully understand before any of its tests run. Every key of
 * the plan, of `[defaults]`, of a `[[tests]]` entry and of a `[[sweeps]]` entry is checked, so a
 * misspelled assertion cannot silently assert nothing. There is deliberately no way to turn
 * this off from a plan file (or from the environment): a plan that the runner only partly
 * understands is not evidence.
 */
function validatePlanSchema(doc, planPath) {
    checkFields(planPath, doc, PLAN_FIELDS, 'plan')
    if (doc.defaults !== undefined) {
        if (doc.defaults === null || typeof doc.defaults !== 'object' || Array.isArray(doc.defaults)) {
            throw new PlanSchemaError(`${planLabel(planPath)}: [defaults] must be a table`)
        }
        checkFields(planPath, doc.defaults, TEST_FIELDS, '[defaults]')
    }
    for (const [index, test] of (doc.tests || []).entries()) {
        if (test === null || typeof test !== 'object' || Array.isArray(test)) {
            throw new PlanSchemaError(`${planLabel(planPath)}: test #${index + 1} is not a table`)
        }
        const where = typeof test.name === 'string' && test.name.length > 0
            ? `test '${test.name}'`
            : `test #${index + 1}`
        checkFields(planPath, test, TEST_FIELDS, where)
    }
    for (const [index, sweep] of (doc.sweeps || []).entries()) {
        if (sweep === null || typeof sweep !== 'object' || Array.isArray(sweep)) {
            throw new PlanSchemaError(`${planLabel(planPath)}: [[sweeps]] entry #${index + 1} is not a table`)
        }
        const where = typeof sweep.name === 'string' && sweep.name.length > 0
            ? `[[sweeps]] entry '${sweep.name}'`
            : `[[sweeps]] entry #${index + 1}`
        checkFields(planPath, sweep, SWEEP_FIELDS, where)
    }
    // The fields this change implemented get a type check too: a plan that sets one of them to a
    // number must fail here, where the file and the test are named, not later inside an assertion.
    const entries = [
        ['[defaults]', doc.defaults],
        ...(doc.tests || []).map((test, index) => [
            typeof test.name === 'string' && test.name.length > 0 ? `test '${test.name}'` : `test #${index + 1}`,
            test,
        ]),
    ]
    for (const [where, entry] of entries) {
        if (!entry) continue
        checkFieldType(planPath, entry, 'expect_stdout_exact', where, 'string')
        checkFieldType(planPath, entry, 'expect_stdout', where, 'string')
        checkFieldType(planPath, entry, 'perf_budget_platform', where, 'string')
        checkFieldType(planPath, entry, 'cuda_devices', where, 'boolean')
        checkFieldType(planPath, entry, 'cpu_budget_ms', where, 'number')
        // A CPU budget is the test's cost bound and the timeout is only its liveness bound, so the
        // two are declared together: a test that states a budget and leaves the timeout at the
        // runner's 10 s default would be killed long before its own work could cross it, and the
        // plan would be asserting a bound nothing reaches.
        if (typeof entry.cpu_budget_ms === 'number' && typeof entry.timeout_ms !== 'number') {
            throw new PlanSchemaError(
                `${planLabel(planPath)}: ${where}: cpu_budget_ms needs timeout_ms: the budget is the ` +
                    `cost bound and the timeout is the liveness bound (a hang, not a slow run)`
            )
        }
    }
}

function camelHome(build) {
    return path.join(REPO_ROOT, 'out', BUILDS[build].dir)
}

// The install a run measures: `--camel <path>` names it, else `out/<build>`. One install for the
// whole run -- `${camel}`, what `preflightBuild` checks, and the root every test child reads as
// `CAMEL_HOME` are all this -- and the environment never chooses it: `CAMEL_HOME` is the install
// location's convention for the binary and the SDK, not a switch over what a run measures.
function camelBinary(options) {
    return options.camel || toolExecutable('camel', options.build)
}

function camelInstall(options) {
    return path.dirname(path.dirname(camelBinary(options)))
}

function toolExecutable(tool, build) {
    const ext = IS_WINDOWS ? '.exe' : ''
    if (tool === 'node') return process.execPath
    return path.join(camelHome(build), 'bin', `${tool}${ext}`)
}
const COLORS = {
    green: '\x1b[32m',
    red: '\x1b[31m',
    cyan: '\x1b[36m',
    yellow: '\x1b[33m',
    reset: '\x1b[0m',
}

// Test scratch. Every run gets its own root, tmp/test/run-<pid>, and every test gets a
// directory under it (`<run>/<plan>/<test>`), exposed as `${test_tmp}` and as
// CAMEL_TEST_TMPDIR in the child environment. A test that writes a file writes there, so a
// file left by another test, or by an earlier run, cannot be read as this run's output (the
// failure that motivated this: a stale model at the path a refused export names made "a
// refused export must not write a model" fail). A program that still names its own output path
// takes it from CAMEL_TEST_TMPDIR, so the file lands under the per-test directory; only a path
// that a refusal test names without ever writing it stays literal, and the pass's `out`
// parameter isolates the one refusal case that does write.
//
// The per-test directory is removed after the test. The run removes its own root when the run
// succeeded and keeps it (and prints the path) when a test failed, so the files are there to
// read. It never removes another run's root: two runners can run at the same time.
const TEST_SCRATCH_ROOT = runnerOutputPath(SCRATCH_DIR, REPO_ROOT)
const TEST_SCRATCH_RUN = path.join(TEST_SCRATCH_ROOT, `run-${process.pid}`)

function timestamp() {
    const now = new Date()
    const pad = (n) => String(n).padStart(2, '0')
    return `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}_${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`
}

function ensureDir(dir) {
    fs.mkdirSync(dir, { recursive: true })
}

// CAMEL_TEST_RUN_DIRS_KEEP=<n> lowers the retention bound for one run. The default and its reason
// live beside the policy (test/tools/runner-output.mjs); this is the one number a check sets small
// to watch the bound hold across two real runs (docs/test-plans.md, "Runner output").
function resultsKeep() {
    const raw = process.env.CAMEL_TEST_RUN_DIRS_KEEP
    if (raw === undefined || raw === '') return RUN_DIRS_KEPT
    const parsed = Number(raw)
    if (!Number.isInteger(parsed) || parsed < 0) {
        throw new Error(`CAMEL_TEST_RUN_DIRS_KEEP needs a non-negative integer, got '${raw}'`)
    }
    return parsed
}

/** One git command's trimmed stdout, or null when git is unavailable or refuses. */
function gitOutput(args) {
    const proc = spawnSync('git', args, { cwd: REPO_ROOT, encoding: 'utf8', windowsHide: true, timeout: 30000 })
    if (proc.error || proc.status !== 0) return null
    return proc.stdout.trim()
}

/**
 * What this run was, written before its first test: the revision, the build and tier, and every
 * plan it selected. This is the manifest that replaces the copy of each plan and of each case the
 * runner used to write into every test's status directory -- a copy nothing ever read back, made
 * once per test, so a 5,667-test gate rewrote the same plan file 5,667 times (see writeLogFiles).
 * With it, a run directory says which plan, at which revision, produced it without carrying a copy
 * of the checkout's own files.
 */
function writeRunManifest(logDir, options, plans) {
    const revision = gitOutput(['rev-parse', 'HEAD'])
    const status = gitOutput(['status', '--porcelain', '--untracked-files=no'])
    fs.writeFileSync(
        path.join(logDir, 'run.json'),
        JSON.stringify(
            {
                started_at: new Date().toISOString(),
                build: options.build,
                tier: options.tier,
                jobs: options.jobs,
                update_golden: options.updateGolden,
                results_root: RESULTS_ROOT,
                // null, not false: git unavailable means the provenance is unknown, and a manifest
                // that claimed the tree was clean would be worse than one that shrugs.
                revision,
                dirty: status === null ? null : status.length > 0,
                targets: options.targets.map((target) => planLabel(target)),
                plans: plans.map((plan) => planLabel(plan.__planPath)),
            },
            null,
            2
        )
    )
}

// CAMEL_TEST_TIMEOUT_SCALE=<n> multiplies every test and verification timeout. For running
// the plans under a diagnostic mode that slows every program down (collection stress,
// safepoint poisoning, a sanitizer build), where the timeouts would otherwise decide.
const TIMEOUT_SCALE = (() => {
    const scale = Number(process.env.CAMEL_TEST_TIMEOUT_SCALE)
    return Number.isFinite(scale) && scale > 0 ? scale : 1
})()

// A Debug build runs the same program through unoptimised C++: a loop of a million rounds takes
// tens of seconds where the Release build takes under one. The debug layer multiplies a test's
// own timeout by this, so a plan's budget still means "this is slow", not "this is Debug".
const DEBUG_BUILD_TIMEOUT_SCALE = 4

// How much room a test must leave under its `timeout_ms` to count as a real bound: the run fails
// when a passing test used more than a quarter of it **of its own CPU time**. The quantity is the
// work's, not the host's, and deliberately so: judged on the wall clock, this rule failed
// the plan runner's own self-test at random on the shared host -- 1 of 12 runs at a 60 s bound,
// and twice in two agents' gates -- while every run inside it was green,
// because a passing test's wall clock on a busy host is the neighbours' scheduling. Four machines'
// worth of headroom is what keeps a bound a bound. A test that needs more time states it in a
// comment next to the number (docs/test-plans.md). A test that declares `cpu_budget_ms` is judged
// on that budget instead and does not read the wall clock as a bound at all: its timeout is a
// liveness bound (see the rule quoted where the reading is judged), so this rule has nothing to
// say about it.
const TIMEOUT_MARGIN = 4

/**
 * A counting semaphore with FIFO order. A normal test holds one slot; a test marked
 * `parallel = false` asks for every slot, so it starts only when no other test is running and
 * no other test starts while it runs. FIFO keeps a queued serial test ahead of tests queued
 * after it, and no holder ever waits for a slot another holder needs, so the queue cannot
 * deadlock. A test waiting for another test's result (`priority`) goes to the front: it must
 * follow its reference, not the whole run.
 */
class Slots {
    constructor(count) {
        this.free = count
        this.queue = []
    }

    /**
     * `priority` puts a test in front of the queue. A test that waits for another test's result
     * (a comparison against a reference test, a back-end agreement check) must not queue behind
     * every other test of the run: it would run last, and the in-order printer would hold back
     * the whole run's output until then. It queues as soon as its reference is done, which is
     * after the plan's earlier tests have already taken their slots.
     */
    acquire(count, priority = false) {
        if (count <= this.free && this.queue.length === 0) {
            this.free -= count
            return Promise.resolve()
        }
        return new Promise((resolve) => {
            if (priority) this.queue.unshift({ count, resolve })
            else this.queue.push({ count, resolve })
        })
    }

    release(count) {
        this.free += count
        while (this.queue.length > 0 && this.queue[0].count <= this.free) {
            const next = this.queue.shift()
            this.free -= next.count
            next.resolve()
        }
    }
}

/**
 * A test is run alone (`parallel = false`, on the test or on its plan) when it shares something
 * outside its own scratch directory: a fixed path another test writes, a compiler cache, the
 * wall clock. A benchmark is run alone as well unless it says otherwise: its numbers mean
 * nothing next to a machine busy with other tests. A test may say `parallel = true` to opt in.
 */
function isSerial(plan, test) {
    if (test.parallel === true) return false
    if (test.parallel === false || plan.parallel === false) return true
    return test.kind === 'benchmark'
}

/**
 * Run one child process and collect its output, as `spawnSync` did, but without blocking the
 * runner: the other tests run while this one waits. The timeout kills the whole process tree
 * (a test may have started a compiler or an interpreter of its own).
 *
 * `cpuReport` opens a fourth pipe, on which test/tools/child-cpu.mjs writes the CPU time the
 * kernel charged the command's process tree. The runner cannot read that itself: the one reading
 * Node has of a child's CPU is the `cutime` + `cstime` delta of /proc/self/stat, which sums every
 * child this process reaped, and the runner reaps every test's child from one process.
 */
function runProcess(command, args, { cwd, env, timeoutMs, marks, cpuReport = false }) {
    return new Promise((resolve) => {
        const started = performance.now()
        // A test must not write into the tree it is testing. A Python harness that imports a module
        // beside it (`test/cases/perf/reduce_layout.py` imports `step_budget.py`) leaves a
        // `__pycache__` behind, and one left in a checkout makes the case ledger's answer depend on
        // which test ran first -- the ledger reads the filesystem, so it sees a cache written this
        // run. The interpreter is told not to write bytecode; a test that needs its own value sets
        // `PYTHONDONTWRITEBYTECODE` in its `envs`, and that wins because those are already in `env`.
        const childEnv = { ...(env || process.env) }
        if (!('PYTHONDONTWRITEBYTECODE' in childEnv)) childEnv.PYTHONDONTWRITEBYTECODE = '1'
        let stdout = ''
        let stderr = ''
        let cpuReportText = ''
        let timedOut = false
        let settled = false
        let timer = null
        let child = null
        const finish = (status, signal, error) => {
            if (settled) return
            settled = true
            // The child is gone at this point (on error, never existed). `spawnedAt` and
            // `exitedAt` are what lets the scheduler separate "a child is running" from "the
            // slot is held while the runner does its own work".
            if (marks) marks.exitedAt = performance.now()
            if (timer) clearTimeout(timer)
            resolve({
                status: typeof status === 'number' ? status : null,
                signal: signal ?? null,
                error: error || null,
                timedOut,
                wallMs: performance.now() - started,
                stdout,
                stderr,
                cpuReport: cpuReportText,
            })
        }
        try {
            child = spawn(command, args, {
                cwd,
                env: childEnv,
                stdio: cpuReport ? ['ignore', 'pipe', 'pipe', 'pipe'] : ['ignore', 'pipe', 'pipe'],
                windowsHide: true,
                // On POSIX a process group lets the timeout take the whole tree down; on Windows
                // `taskkill /T` does that.
                detached: !IS_WINDOWS,
            })
            // `spawn` returns only after the operating system created the child (fork/exec or
            // CreateProcess), so the interval before this mark is runner work, not test work.
            if (marks) {
                marks.spawnedAt = performance.now()
                marks.spawnMs = marks.spawnedAt - started
            }
        } catch (error) {
            finish(null, null, error)
            return
        }
        child.stdout.setEncoding('utf8')
        child.stderr.setEncoding('utf8')
        child.stdout.on('data', (chunk) => {
            stdout += chunk
        })
        child.stderr.on('data', (chunk) => {
            stderr += chunk
        })
        if (cpuReport) {
            const report = child.stdio[3]
            report.setEncoding('utf8')
            report.on('data', (chunk) => {
                cpuReportText += chunk
            })
        }
        child.on('error', (error) => finish(null, null, error))
        child.on('close', (status, signal) => finish(status, signal, null))
        if (timeoutMs > 0) {
            timer = setTimeout(() => {
                timedOut = true
                killProcessTree(child)
            }, timeoutMs)
        }
    })
}

function killProcessTree(child) {
    if (!child || !child.pid) return
    if (IS_WINDOWS) {
        const killed = spawnSync('taskkill', ['/pid', String(child.pid), '/T', '/F'], {
            stdio: 'ignore',
            windowsHide: true,
        })
        if (killed.error || killed.status !== 0) {
            try {
                child.kill('SIGKILL')
            } catch {
                /* already gone */
            }
        }
        return
    }
    try {
        process.kill(-child.pid, 'SIGKILL')
    } catch {
        try {
            child.kill('SIGKILL')
        } catch {
            /* already gone */
        }
    }
}

function walkPlans(entryPath, files = []) {
    const stats = fs.statSync(entryPath)
    if (stats.isFile()) {
        if (entryPath.endsWith('.plan.toml')) files.push(entryPath)
        return files
    }
    for (const name of fs.readdirSync(entryPath)) {
        walkPlans(path.join(entryPath, name), files)
    }
    return files
}

const DEFAULT_JOBS = (() => {
    const count = typeof os.availableParallelism === 'function' ? os.availableParallelism() : os.cpus().length
    return Math.max(1, count || 1)
})()

// The layers are `fast`, `slow`, `milestone` and `all`: one name per destination, so "the slow
// layer" is only ever the release gate's slow layer (the plans named in SLOW_PLANS, run in full)
// and never the unsampled copies of a sweep. `milestone` is what only runs at a milestone: the
// unsampled copies of a sweep and the plans the policy holds back (benchmarks, soaks, fuzzing,
// PyTorch comparisons, progen). `all` is everything but `debug`.
//
// `debug` is not a layer of an ordinary run: it selects the plans that exercise views, data
// blocks and the executor on a Debug build, where the view-escape assertion exists at all.
const TIERS = new Set(['fast', 'slow', 'milestone', 'all', 'debug'])

function positiveInteger(value, what) {
    const parsed = Number(value)
    if (!Number.isInteger(parsed) || parsed < 1) {
        throw new Error(`${what} needs a positive integer, got '${value}'`)
    }
    return parsed
}

function parseArgs(argv) {
    const envBuild = process.env.CAMEL_TEST_BUILD
    if (envBuild !== undefined && !Object.hasOwn(BUILDS, envBuild)) {
        throw new Error(`CAMEL_TEST_BUILD must be one of ${Object.keys(BUILDS).join(', ')}, got '${envBuild}'`)
    }
    const options = {
        updateGolden: false,
        // `fast` is the default layer: the full test set is opt-in (`--tier all`, `npm run test:all`).
        tier: 'fast',
        // Which build the tests run against. `release` is out/latest; `debug` is out/debug.
        build: envBuild || 'release',
        buildExplicit: envBuild !== undefined,
        // The install this run measures, named by `--camel <path>`. With none the build above
        // decides (`out/<build>`); with one, it is this binary, its root is every child's
        // `CAMEL_HOME`, and `--build` only still selects the layer the run addresses.
        camel: null,
        // `--jobs` defaults to the processor count: one test per core, no more. `--jobs 1` gives
        // exactly the historical serial execution, which is how the runner's own self-test
        // compares parallel output with serial output.
        jobs: DEFAULT_JOBS,
        targets: [],
        // Plan files that `--changed` selected through a `layer: true` rule: they stay named
        // targets but follow `--tier`, so a plan whose slow cases must not run from an ordinary
        // change runs only its fast layer. `--run-changed` produces this; it is not a user knob.
        layeredPlans: new Set(),
    }
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '--update-golden') {
            options.updateGolden = true
        } else if (arg === '--selection-layer' && i + 1 < argv.length) {
            for (const entry of argv[++i].split(',')) {
                if (entry.trim()) options.layeredPlans.add(path.resolve(process.cwd(), entry.trim()))
            }
        } else if (arg === '--tier' && i + 1 < argv.length) {
            const tier = argv[++i]
            if (!TIERS.has(tier)) throw new Error(`--tier must be one of ${[...TIERS].join(', ')}, got '${tier}'`)
            options.tier = tier
        } else if (arg.startsWith('--tier=')) {
            const tier = arg.slice('--tier='.length)
            if (!TIERS.has(tier)) throw new Error(`--tier must be one of ${[...TIERS].join(', ')}, got '${tier}'`)
            options.tier = tier
        } else if (arg === '--build' && i + 1 < argv.length) {
            const build = argv[++i]
            if (!Object.hasOwn(BUILDS, build)) throw new Error(`--build must be one of ${Object.keys(BUILDS).join(', ')}, got '${build}'`)
            options.build = build
            options.buildExplicit = true
        } else if (arg.startsWith('--build=')) {
            const build = arg.slice('--build='.length)
            if (!Object.hasOwn(BUILDS, build)) throw new Error(`--build must be one of ${Object.keys(BUILDS).join(', ')}, got '${build}'`)
            options.build = build
            options.buildExplicit = true
        } else if (arg === '--camel') {
            if (i + 1 >= argv.length) throw new Error('--camel needs the path of the install to measure')
            options.camel = path.resolve(process.cwd(), argv[++i])
        } else if (arg.startsWith('--camel=')) {
            const install = arg.slice('--camel='.length)
            if (install === '') throw new Error('--camel needs the path of the install to measure')
            options.camel = path.resolve(process.cwd(), install)
        } else if (arg === '--jobs' && i + 1 < argv.length) {
            options.jobs = positiveInteger(argv[++i], '--jobs')
        } else if (arg.startsWith('--jobs=')) {
            options.jobs = positiveInteger(arg.slice('--jobs='.length), '--jobs')
        } else {
            options.targets.push(path.resolve(process.cwd(), arg))
        }
    }
    // The debug layer never runs on a Release binary: its whole point is the assertions that are
    // compiled out of one, and the escape case aborts. `--tier debug` therefore selects the
    // Debug build; saying `--build release` with it is a contradiction, not a preference.
    if (options.tier === 'debug') {
        if (options.buildExplicit && options.build !== 'debug') {
            throw new Error(`--tier debug runs on the Debug build; --build ${options.build} was given`)
        }
        options.build = 'debug'
    }
    return options
}

/**
 * Refuse to run when the install this run measures is not built. `--camel <path>` names it; with
 * none it is `out/<build>`, written by `collect-out.js <build>`. The debug layer additionally
 * refuses anything but a Debug binary: a binary left there by another configuration is caught by
 * its own --version line, which names the build.
 */
function preflightBuild(options) {
    const build = options.build
    const exe = camelBinary(options)
    if (!fs.existsSync(exe)) {
        console.error(
            `${COLORS.red}[FAIL]${COLORS.reset} no ${BUILDS[build].label} binary at ${exe}`
        )
        if (options.camel) {
            console.error('    --camel names the install this run measures; it must be a built install\'s bin/camel.')
        } else {
            console.error(
                `    Build it with the shared helper: pwsh -NoProfile -File ${path.join(path.dirname(REPO_ROOT), 'camel-build.ps1')} -Path ${REPO_ROOT} -Config ${BUILDS[build].label}`
            )
            console.error(`    The ${BUILDS[build].label} tier never falls back to out/latest.`)
        }
        process.exit(2)
    }
    if (build !== 'debug') return
    const home = camelInstall(options)
    const pathKey = IS_WINDOWS ? Object.keys(process.env).find((key) => key.toUpperCase() === 'PATH') || 'PATH' : 'PATH'
    const probe = spawnSync(exe, ['--version'], {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        timeout: 30000,
        env: {
            ...process.env,
            CAMEL_HOME: home,
            [pathKey]: `${path.join(home, 'bin')}${path.delimiter}${process.env[pathKey] || ''}`,
        },
    })
    const text = `${probe.stdout || ''}${probe.stderr || ''}`
    if (probe.error || !/\(Debug\b/.test(text)) {
        console.error(`${COLORS.red}[FAIL]${COLORS.reset} ${exe} is not a Debug build: ${text.trim() || probe.error || 'no version'}`)
        console.error('    Rebuild it with: npm run debug (through the shared helper, -Config Debug).')
        process.exit(2)
    }
}

/**
 * The Python interpreter for test preconditions and `tool = "python"` tests: the active virtual
 * environment's, else the benchmark environment's (benchmarks/.venv), else PATH's.
 */
function pythonExecutable() {
    const venvPython = (venv) =>
        IS_WINDOWS ? path.join(venv, 'Scripts', 'python.exe') : path.join(venv, 'bin', 'python')
    for (const venv of [process.env.VIRTUAL_ENV, path.join(REPO_ROOT, 'benchmarks', '.venv')]) {
        if (venv && fs.existsSync(venvPython(venv))) return venvPython(venv)
    }
    return IS_WINDOWS ? 'python' : 'python3'
}

function loadVars(options) {
    // Built-in variables; test/vars.toml may override them. `${camel}` is the compiler of the run,
    // the same path the runner prints as `[INFO] camel executable` and records as `camel_exe` in
    // summary.json: a plan that runs a program which starts a compiler names the install with
    // `--camel ${camel}`, a command-line argument and never an environment switch.
    const builtins = {
        python: pythonExecutable(),
        camel: camelBinary(options),
    }
    if (!fs.existsSync(VARS_PATH)) return builtins
    return { ...builtins, ...parseTomlFile(VARS_PATH) }
}

function parseDiagnostics(text) {
    const diagnostics = []
    const clean = applyNormalizers(text || '', ['strip_ansi', 'normalize_newlines', 'trim_trailing_space'])
    // A diagnostic that belongs to the run's configuration rather than to a line of the
    // program (a device named by --devices, for one) has no source position; the short format
    // writes `?` there. It is still a diagnostic: name and code can be asserted, and line and
    // column stay absent.
    //
    // The message of one diagnostic may run onto further lines, and the short format closes the
    // last of them with `(name=..., code=...)`: `SemanticDiag::NoMatchingFunction` writes the
    // overload list of a call to a function of the program that way. So it is the header that
    // starts a diagnostic and the block runs to the next header, where the suffix is read. A
    // message read this way is read whole; the suffix of a wrapped message can no longer be
    // taken for another diagnostic's, and one whose message stays on the header line reads
    // exactly as before.
    const header = /^(?:\([^)]+\))?(.*?):(\d+|\?):(\d+|\?): \[[^\]]+\]: /gm
    const starts = []
    let match
    while ((match = header.exec(clean)) !== null) {
        starts.push({
            path: match[1],
            line: match[2],
            column: match[3],
            begin: match.index,
            after: header.lastIndex,
        })
    }
    const suffix = /\(name=([^,]+), code=(0x[0-9A-Fa-f]+)\)/
    for (let i = 0; i < starts.length; ++i) {
        const end = i + 1 < starts.length ? starts[i + 1].begin : clean.length
        const closed = suffix.exec(clean.slice(starts[i].after, end))
        if (closed === null) continue // a message with no name to assert (--error-format text)
        diagnostics.push({
            path: starts[i].path,
            line: starts[i].line === '?' ? undefined : Number.parseInt(starts[i].line, 10),
            column: starts[i].column === '?' ? undefined : Number.parseInt(starts[i].column, 10),
            name: closed[1],
            code: closed[2],
        })
    }
    return diagnostics
}

function expandArgToken(token, scope) {
    const exact = String(token).match(/^\$\{([A-Za-z0-9_.-]+)\}$/)
    if (exact) {
        const value = scope[exact[1]]
        if (Array.isArray(value)) return value.flatMap((item) => expandArgToken(item, scope))
        if (value === undefined) throw new Error(`unknown variable ${exact[1]}`)
        return [String(value)]
    }
    return [String(token).replace(/\$\{([A-Za-z0-9_.-]+)\}/g, (_, name) => {
        if (!(name in scope)) throw new Error(`unknown variable ${name}`)
        const value = scope[name]
        if (Array.isArray(value)) throw new Error(`array variable ${name} must be used as a full token`)
        return String(value)
    })]
}

function expandArgs(args, scope) {
    const expanded = []
    for (const token of args || []) {
        expanded.push(...expandArgToken(token, scope))
    }
    return expanded
}

/**
 * Substitute `${name}` in an assertion string (the `expect_*` fields), so an expectation about a
 * path a program prints can name this test's scratch directory. Unknown names are errors, as in
 * `args`.
 */
function expandTextTokens(text, scope) {
    return String(text).replace(/\$\{([A-Za-z0-9_.-]+)\}/g, (_, name) => {
        if (!(name in scope)) throw new Error(`unknown variable ${name}`)
        const value = scope[name]
        if (Array.isArray(value)) throw new Error(`array variable ${name} cannot be used in text`)
        return String(value)
    })
}

function formatDurationMs(valueMs, digits = 2) {
    if (Math.abs(valueMs) >= 1000) {
        return `${(valueMs / 1000).toFixed(digits)} s`
    }
    return `${valueMs.toFixed(digits)} ms`
}

function benchmarkSampleCount(benchmark) {
    if (Array.isArray(benchmark.samples_ms)) {
        return benchmark.samples_ms.length
    }
    if (Number.isInteger(benchmark.iterations) && benchmark.iterations >= 0) {
        return benchmark.iterations
    }
    return null
}

/**
 * The CPU reading of a test that declared `cpu_budget_ms`: the last JSON line
 * test/tools/child-cpu.mjs wrote on file descriptor 3. A missing or unreadable reading is a reason
 * the caller turns into a failure -- a budget the runner cannot read has examined nothing.
 */
function readCpuReport(text) {
    const line = String(text || '')
        .split('\n')
        .map((entry) => entry.trim())
        .filter(Boolean)
        .pop()
    if (!line) {
        return { cpuMs: null, reason: 'the runner got no CPU report from test/tools/child-cpu.mjs' }
    }
    let report
    try {
        report = JSON.parse(line)
    } catch (error) {
        return { cpuMs: null, reason: `the CPU report is not JSON (${error.message})` }
    }
    if (!Number.isFinite(report.cpu_ms)) {
        return { cpuMs: null, reason: report.reading || 'the child reported no CPU figure' }
    }
    return { cpuMs: report.cpu_ms, reason: null }
}

function formatMetric(result) {
    // A test judged on its own CPU time prints the CPU figure it was judged on, with the budget it
    // was read against and the wall clock beside it as information: on this host the wall figure is
    // the neighbours' scheduling, and a reader who wants it should not have to open meta.json.
    if (typeof result.cpuBudgetMs === 'number') {
        const reading = Number.isFinite(result.cpuMs)
            ? formatDurationMs(result.cpuMs)
            : 'no reading'
        return `${reading} cpu of ${formatDurationMs(result.cpuBudgetMs)} budget, ` +
            `${formatDurationMs(result.wallMs)} wall`
    }
    // A failed `--timeit` iteration reports the diagnosis, not a mean: the failure is the
    // test's result, so the status line carries no number.
    if (result.benchmark && Number.isFinite(result.benchmark.mean_ms)) {
        const samples = benchmarkSampleCount(result.benchmark)
        const sampleSuffix = samples === null ? '' : ` (n=${samples})`
        if (samples !== null && samples < 2) {
            return `${formatDurationMs(result.benchmark.mean_ms)}${sampleSuffix}`
        }
        return `${formatDurationMs(result.benchmark.mean_ms)} +/- ${formatDurationMs(result.benchmark.ci95_ms)}${sampleSuffix}`
    }
    if (typeof result.wallMs === 'number') {
        return formatDurationMs(result.wallMs)
    }
    return ''
}

function printPlanHeader(plan) {
    const relativePath = path.relative(PLANS_ROOT, plan.__planPath).replace(/\\/g, '/')
    const showPath = relativePath.split('.')[0].replace(/\//g, '.')
    const planLabel = plan.desc || plan.id || '.'
    console.log('')
    console.log(`${COLORS.cyan}[PLAN]${COLORS.reset} ${showPath}: ${planLabel}`)
}

function printStatusLine(status, name, metric = '') {
    const label = status === 'pass'
        ? `${COLORS.green}[PASS]${COLORS.reset}`
        : status === 'skip'
            ? `${COLORS.yellow}[SKIP]${COLORS.reset}`
            : status === 'xfail'
                ? `${COLORS.yellow}[XFAIL]${COLORS.reset}`
                : `${COLORS.red}[FAIL]${COLORS.reset}`
    const left = `${label} ${name}`
    if (!metric) {
        console.log(left)
        return
    }
    const width = 78
    const spaces = ' '.repeat(Math.max(1, width - left.length - metric.length))
    console.log(`${left}${spaces}${metric}`)
}

function printDetailLines(lines) {
    for (const line of lines) {
        const shortKv = line.match(/^([^:]{1,24}):\s+(.+)$/)
        if (shortKv && shortKv[2].length <= 60) {
            console.log(`    ${COLORS.cyan}[INFO]${COLORS.reset} ${shortKv[1].padEnd(28)} ${shortKv[2]}`)
        } else {
            console.log(`    ${COLORS.cyan}[INFO]${COLORS.reset} ${line}`)
        }
    }
}

/**
 * The end of what a failed test wrote, for a log that is all there is to read (continuous
 * integration, where the files of test/results are not at hand): with CI or
 * CAMEL_TEST_SHOW_OUTPUT set, the last lines of standard output and standard error follow the
 * failure, line ends and control characters made visible.
 */
const SHOW_FAILED_OUTPUT = Boolean(process.env.CAMEL_TEST_SHOW_OUTPUT || process.env.CI)

function printFailedOutput(result) {
    if (!SHOW_FAILED_OUTPUT) return
    const maxLines = 12
    const maxWidth = 240
    for (const [name, text] of [['stdout', result.stdout], ['stderr', result.stderr]]) {
        if (!text) {
            console.log(`    ${COLORS.cyan}[${name}]${COLORS.reset} (empty)`)
            continue
        }
        const lines = text.replace(/\n$/, '').split('\n')
        const shown = lines.slice(-maxLines)
        if (lines.length > shown.length) {
            console.log(`    ${COLORS.cyan}[${name}]${COLORS.reset} ... ${lines.length - shown.length} line(s) before`)
        }
        for (const line of shown) {
            const visible = line
                .replace(/\r/g, '\\r')
                .replace(/\x1b/g, '\\e')
                .replace(/[\x00-\x08\x0b-\x1f]/g, (c) => `\\x${c.charCodeAt(0).toString(16).padStart(2, '0')}`)
            const clipped = visible.length > maxWidth ? `${visible.slice(0, maxWidth)}...` : visible
            console.log(`    ${COLORS.cyan}[${name}]${COLORS.reset} ${clipped}`)
        }
    }
}

function printRunSummary(summary) {
    const total = summary.pass + summary.fail + summary.skip + summary.xfail
    const skipped = summary.skip > 0 ? `, skip: ${summary.skip}` : ''
    const known = summary.xfail > 0 ? `, known failures: ${summary.xfail}` : ''
    console.log('')
    console.log(`${COLORS.cyan}[SUMMARY]${COLORS.reset} total: ${total}, pass: ${summary.pass}, fail: ${summary.fail}${skipped}${known}`)
}

/**
 * The gate's three numbers, one line. A run that names the build it just paid for (the remote
 * helper exports CAMEL_GATE_BUILD_SECONDS) prints build + test = total; a run that built nothing
 * (a local plan run, or the -NoBuild path, which exports 0) prints build 0.0 s. The format is
 * pinned by test/tools/selftest/run.mjs, so a regression in the split is red, not a missing line.
 */
function gateSplit(testWallMs) {
    const raw = Number(process.env.CAMEL_GATE_BUILD_SECONDS || 0)
    const buildSeconds = Number.isFinite(raw) && raw > 0 ? raw : 0
    const testSeconds = testWallMs / 1000
    return { buildSeconds, testSeconds, totalSeconds: buildSeconds + testSeconds }
}

function formatGateSplit(split) {
    return `${COLORS.cyan}[SPLIT]${COLORS.reset} build ${split.buildSeconds.toFixed(1)} s, test ${split.testSeconds.toFixed(1)} s, total ${split.totalSeconds.toFixed(1)} s`
}

/**
 * Hide the machine's CUDA devices from a Python child that did not ask for them. Called before a
 * test's own `envs` are applied, so a value the test writes itself still wins; the rule and the
 * evidence behind it are in test/tools/cuda-visibility.mjs.
 */
function hideCudaDevicesFrom(env, test) {
    if (hidesCudaDevices(test)) env[CUDA_VISIBLE_DEVICES] = CUDA_HIDDEN
    return env
}

/**
 * A test's precondition: `skip_unless` is a command (same variable expansion as `args`) run from
 * the repository root before the test. A non-zero exit skips the test, reporting the command's
 * output as the reason; the test is then neither passed nor failed. Use it only for conditions
 * outside the code under test, such as network access for a dataset download.
 */
async function checkPrecondition(test, sharedVars, scratch, scratchRelative) {
    if (!test.skip_unless || test.skip_unless.length === 0) {
        return null
    }
    const casePath = path.resolve(path.dirname(test.__planPath), test.case)
    const scope = { ...sharedVars, case: casePath, test_tmp: scratchRelative }
    const [cmd, ...args] = expandArgs(test.skip_unless, scope)
    // The precondition is a Python child of the runner too, and it gets the same visibility as
    // the test it guards: a precondition that fails on a busy card would skip the test, and a
    // skip is a silent loss of coverage, not a red (docs/test-plans.md, "CUDA visibility").
    const env = { ...process.env, CAMEL_TEST_TMPDIR: scratch }
    if (pythonEntryPoints(test).precondition) hideCudaDevicesFrom(env, test)
    const proc = await runProcess(cmd, args, {
        cwd: REPO_ROOT,
        // The precondition writes where the test would: a dataset downloaded for it lands in
        // its own scratch directory, so two tests (or two runs) cannot corrupt one cache.
        env,
        timeoutMs: test.skip_unless_timeout_ms || 120000,
    })
    if (proc.status === 0) {
        return null
    }
    const detail = `${proc.stdout || ''}${proc.stderr || ''}`.trim() || proc.error?.message || `exit ${proc.status}`
    return `precondition not met (${test.skip_unless.join(' ')}): ${detail}`
}

function sanitizeSegment(value) {
    return String(value).replace(/[<>:"/\\|?*]+/g, '_')
}

/**
 * The fresh scratch directory of one test; it is removed when the test is done. The plan's id is
 * part of the path so that two tests of different plans with the same name (a sweep copies a
 * test under a prefix, but a future plan may not) cannot share a directory, which in a parallel
 * run would mean one test reading what another wrote.
 */
function createTestScratchDir(test) {
    const base = sanitizeSegment(test.name) || 'test'
    const planBase = sanitizeSegment(test.__planId || 'plan') || 'plan'
    let dir = path.join(TEST_SCRATCH_RUN, planBase, base)
    for (let suffix = 2; fs.existsSync(dir); suffix++) {
        dir = path.join(TEST_SCRATCH_RUN, planBase, `${base}-${suffix}`)
    }
    ensureDir(dir)
    return dir
}

function snapshotCompare(actual, expected, mode) {
    if (mode === 'gir_dot_isomorphic') {
        return compareGirDotIsomorphic(actual, expected)
    }
    return actual === expected ? { ok: true } : { ok: false, message: 'snapshot differs from golden' }
}

function benchmarkFromJson(rawStdout) {
    const payload = JSON.parse(rawStdout)
    if (payload.kind !== 'timeit') {
        throw new Error(`unexpected benchmark kind: ${payload.kind}`)
    }
    return payload
}

function inlineVerifyModuleSource(source) {
    const encodedSource = JSON.stringify(source)
    return `
import fs from 'fs'

const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const assert = (condition, message = 'assertion failed') => {
    if (!condition) throw new Error(message)
}
const fail = (message = 'verification failed') => {
    throw new Error(message)
}
const approx = (actual, expected, tolerance = 1e-9) =>
    Math.abs(Number(actual) - Number(expected)) <= tolerance
const match = (pattern, text = input.normalized.output) => {
    const regex = pattern instanceof RegExp ? pattern : new RegExp(pattern)
    const found = regex.exec(text)
    if (!found) throw new Error(\`pattern not found: \${regex}\`)
    return found
}
const helpers = { input, assert, fail, approx, match }
const inlineSource = ${encodedSource}

try {
    const trimmed = inlineSource.trim()
    if (
        trimmed.startsWith('(')
        || trimmed.startsWith('function')
        || trimmed.startsWith('async ')
    ) {
        const verify = (0, eval)(\`(\${inlineSource})\`)
        if (typeof verify !== 'function') {
            throw new Error('verify_script expression did not evaluate to a function')
        }
        await verify(helpers)
    } else {
        await (async () => {
${source}
        })()
    }
} catch (error) {
    console.error(error && error.stack ? error.stack : String(error))
    process.exit(1)
}
`
}

function verificationConfigOf(test) {
    const hasInline = typeof test.verify_script === 'string' && test.verify_script.trim().length > 0
    const hasPath = typeof test.verify_script_path === 'string' && test.verify_script_path.trim().length > 0
    const hasLegacyInline = typeof test.verify_js === 'string' && test.verify_js.trim().length > 0

    if (hasLegacyInline && hasInline) {
        return {
            error: 'verify_js and verify_script cannot both be set',
        }
    }
    if (hasPath && (hasInline || hasLegacyInline)) {
        return {
            error: 'verify_script_path cannot be combined with verify_script or verify_js',
        }
    }
    if (hasInline) {
        return {
            mode: 'inline',
            source: test.verify_script,
        }
    }
    if (hasPath) {
        return {
            mode: 'path',
            source: test.verify_script_path,
        }
    }
    if (hasLegacyInline) {
        return {
            mode: 'inline',
            source: test.verify_js,
        }
    }
    return null
}

async function runVerification(test, result) {
    const config = verificationConfigOf(test)
    if (!config) return null
    // The whole verification is slot-held time: the temporary directory, the payload, the
    // interpreter's startup and its removal. `childMs` is the part a process was actually
    // running in; the rest is the runner's own work.
    const verifyStarted = performance.now()
    const marks = {}
    if (config.error) {
        return {
            ok: false,
            sourceKind: 'config',
            scriptPath: '',
            command: '',
            exitCode: null,
            signal: null,
            timedOut: false,
            errorMessage: config.error,
            stdout: '',
            stderr: '',
            wallMs: performance.now() - verifyStarted,
            childMs: 0,
        }
    }

    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-test-verify-'))
    const setupMs = performance.now() - verifyStarted
    let scriptPath = ''
    let sourceKind = ''
    if (config.mode === 'path') {
        scriptPath = path.resolve(path.dirname(test.__planPath), config.source)
        sourceKind = 'script'
        if (!fs.existsSync(scriptPath)) {
            fs.rmSync(tempDir, { recursive: true, force: true })
            return {
                ok: false,
                sourceKind,
                scriptPath,
                command: '',
                exitCode: null,
                signal: null,
                timedOut: false,
                errorMessage: `verify script does not exist: ${scriptPath}`,
                stdout: '',
                stderr: '',
                wallMs: performance.now() - verifyStarted,
                childMs: 0,
            }
        }
    } else {
        scriptPath = path.join(tempDir, 'verify-inline.mjs')
        sourceKind = 'inline'
        fs.writeFileSync(scriptPath, inlineVerifyModuleSource(config.source))
    }

    const payloadPath = path.join(tempDir, 'input.json')
    // The documented payload fields are listed in docs/test-plans.md; every one of them must be
    // present, and a missing key is silently dropped by `JSON.stringify` (: `case_path` was
    // assembled from a result that did not carry it yet, so a script normalizing the absolute
    // source path out of its output could not, and its digest varied with the checkout).
    const payload = {
        repo_root: REPO_ROOT,
        test_root: TEST_ROOT,
        plan_path: test.__planPath,
        test_name: test.name,
        case_path: result.casePath,
        command: result.command,
        exit_code: result.exitCode,
        signal: result.signal,
        timed_out: result.timedOut,
        error_message: result.errorMessage,
        wall_ms: result.wallMs,
        stdout: result.stdout,
        stderr: result.stderr,
        output: result.output,
        normalized: result.normalized,
        diagnostics: result.diagnostics,
        benchmark: result.benchmark,
        // The CPU cost judgement, when the test declared one: the figure read on the test's own
        // process tree and the budget it was judged against (`null` for every other test).
        cpu_ms: result.cpuMs ?? null,
        cpu_budget_ms: result.cpuBudgetMs ?? null,
        test_tmp: result.testTmp,
        test_tmp_relative: result.testTmpRelative,
    }
    fs.writeFileSync(payloadPath, JSON.stringify(payload, null, 2))

    const proc = await runProcess(process.execPath, [scriptPath, payloadPath], {
        cwd: REPO_ROOT,
        env: process.env,
        timeoutMs: (test.verify_timeout_ms || 5000) * TIMEOUT_SCALE,
        marks,
    })
    fs.rmSync(tempDir, { recursive: true, force: true })
    const verifyFinished = performance.now()

    const timedOut = proc.timedOut
    return {
        ok: !timedOut && proc.status === 0,
        sourceKind,
        scriptPath,
        command: `${process.execPath} ${scriptPath} ${payloadPath}`,
        exitCode: typeof proc.status === 'number' ? proc.status : null,
        signal: proc.signal ?? null,
        timedOut,
        errorMessage: proc.error ? proc.error.message : null,
        stdout: proc.stdout || '',
        stderr: proc.stderr || '',
        wallMs: verifyFinished - verifyStarted,
        setupMs,
        spawnMs: marks.spawnMs ?? 0,
        childMs: marks.spawnedAt === undefined ? 0 : (marks.exitedAt ?? verifyFinished) - marks.spawnedAt,
        cleanupMs: verifyFinished - (marks.exitedAt ?? verifyFinished),
    }
}

function verifyFailureLines(verification) {
    const lines = []
    if (!verification) return lines
    if (verification.errorMessage) lines.push(`verification error: ${verification.errorMessage}`)
    if (verification.timedOut) lines.push('verification timed out')
    if (verification.exitCode !== 0) {
        lines.push(`verification exited ${verification.exitCode}`)
    }
    const detail = `${verification.stdout}${verification.stderr}`.trim()
    if (detail) {
        const clipped = detail.length > 2000 ? `${detail.slice(0, 2000)}...` : detail
        lines.push(`verification output: ${clipped}`)
    }
    if (lines.length === 0) {
        lines.push('verification failed')
    }
    return lines
}

/**
 * Where two strings first differ, with the text around the difference. An exact comparison that
 * only said "differs" would send the reader to the log for a trailing space; this names the
 * offset and shows both sides.
 */
function describeTextDifference(expected, actual) {
    const shared = Math.min(expected.length, actual.length)
    let index = 0
    while (index < shared && expected[index] === actual[index]) index++
    const window = (text) => {
        const start = Math.max(0, index - 30)
        const end = Math.min(text.length, index + 70)
        return `${start > 0 ? '...' : ''}${text.slice(start, end)}${end < text.length ? '...' : ''}`
    }
    // A short output is shown whole: the reader should not have to open the log to see the
    // trailing space or the extra line that made the comparison fail.
    const whole = expected.length + actual.length <= 400
    return `first difference at offset ${index} (expected ${expected.length} chars, got ${actual.length} chars): `
        + `expected ${JSON.stringify(whole ? expected : window(expected))}, `
        + `got ${JSON.stringify(whole ? actual : window(actual))}`
}

function compareField(field, actual, baseline) {
    if (field === 'exit_code') return actual.exitCode === baseline.exitCode
    if (field === 'stdout_normalized') return actual.normalized.stdout === baseline.normalized.stdout
    if (field === 'stderr_normalized') return actual.normalized.stderr === baseline.normalized.stderr
    if (field === 'output_normalized') return actual.normalized.output === baseline.normalized.output
    if (field === 'diagnostics') {
        // Name, code and source position of every diagnostic, in the order printed.
        const key = (result) => JSON.stringify(
            result.diagnostics.map((d) => [d.name, d.code, d.line, d.column])
        )
        return key(actual) === key(baseline)
    }
    throw new Error(`unsupported differential field: ${field}`)
}

/**
 * The result a test compares against. Test names are not unique across plans (`shapes.nvm` exists
 * in both `compile.enum` and `para.handoff`), so the plan's own test wins; `compare_to` that names
 * a test of another plan falls back to the name. A serial run's map was overwritten as tests
 * finished, which only gave the right answer because the reference of a plan always ran just
 * before its dependents; a parallel run must not depend on that.
 */
function lookupCompleted(context, name) {
    const scoped = context.planId ? context.completed.get(`${context.planId}/${name}`) : undefined
    return scoped ?? context.completed.get(name)
}

async function validateTest(test, result, context) {
    const failures = []
    const scope = context.scope || {}
    if (typeof test.expect_exit === 'number' && result.exitCode !== test.expect_exit) {
        failures.push(`expected exit ${test.expect_exit}, got ${result.exitCode}`)
    }
    for (const rawNeedle of test.expect_stdout_contains || []) {
        const needle = expandTextTokens(rawNeedle, scope)
        if (!result.normalized.stdout.includes(needle)) {
            failures.push(`stdout does not contain '${needle}'`)
        }
    }
    // `expect_stdout` is the legacy name of the same byte-exact comparison: a plan written
    // before `expect_stdout_exact` existed meant the whole output, and the values it holds are
    // whole outputs. Both compare the normalized stdout, as the other expectations do.
    for (const field of ['expect_stdout_exact', 'expect_stdout']) {
        if (typeof test[field] !== 'string') continue
        const expected = expandTextTokens(test[field], scope)
        if (result.normalized.stdout !== expected) {
            failures.push(`${field} differs: ${describeTextDifference(expected, result.normalized.stdout)}`)
        }
    }
    if (typeof test.expect_stdout_starts_with === 'string') {
        const expectedStart = expandTextTokens(test.expect_stdout_starts_with, scope)
        if (!result.normalized.stdout.startsWith(expectedStart)) {
            failures.push(`stdout does not start with ${JSON.stringify(expectedStart)}`)
        }
    }
    // The other direction: a plan that asserts a run says nothing about something (the profiler
    // off is silent, a diagnostic is not printed for a program that has no fault) needs a
    // negative assertion, and a missing one is not the same as a passing one.
    for (const rawNeedle of test.expect_stdout_not_contains || []) {
        const needle = expandTextTokens(rawNeedle, scope)
        if (result.normalized.stdout.includes(needle)) {
            failures.push(`stdout contains '${needle}'`)
        }
    }
    for (const rawNeedle of test.expect_stderr_contains || []) {
        const needle = expandTextTokens(rawNeedle, scope)
        if (!result.normalized.stderr.includes(needle)) {
            failures.push(`stderr does not contain '${needle}'`)
        }
    }
    for (const rawNeedle of test.expect_output_contains || []) {
        const needle = expandTextTokens(rawNeedle, scope)
        if (!result.normalized.output.includes(needle)) {
            failures.push(`output does not contain '${needle}'`)
        }
    }
    for (const rawNeedle of test.expect_output_not_contains || []) {
        const needle = expandTextTokens(rawNeedle, scope)
        if (result.normalized.output.includes(needle)) {
            failures.push(`output contains '${needle}'`)
        }
    }
    if (
        test.expect_diagnostic_name
        || test.expect_diagnostic_code
        || typeof test.expect_diagnostic_line === 'number'
        || typeof test.expect_diagnostic_column === 'number'
    ) {
        const matched = result.diagnostics.find((diagnostic) => {
            if (test.expect_diagnostic_name && diagnostic.name !== test.expect_diagnostic_name) return false
            if (test.expect_diagnostic_code && diagnostic.code !== test.expect_diagnostic_code) return false
            if (typeof test.expect_diagnostic_line === 'number' && diagnostic.line !== test.expect_diagnostic_line) return false
            if (typeof test.expect_diagnostic_column === 'number' && diagnostic.column !== test.expect_diagnostic_column) return false
            return true
        })
        if (!matched) {
            failures.push(
                `expected diagnostic ${[
                    test.expect_diagnostic_name || null,
                    test.expect_diagnostic_code || null,
                    typeof test.expect_diagnostic_line === 'number' ? `line ${test.expect_diagnostic_line}` : null,
                    typeof test.expect_diagnostic_column === 'number' ? `column ${test.expect_diagnostic_column}` : null,
                ].filter(Boolean).join(', ')}`
            )
        }
    }
    if (test.kind === 'snapshot') {
        const stream = test.snapshot_stream || 'stdout'
        const actualText = stream === 'stderr' ? result.normalized.stderr : result.normalized.stdout
        const goldenPath = path.resolve(path.dirname(test.__planPath), test.golden)
        if (context.options.updateGolden) {
            ensureDir(path.dirname(goldenPath))
            fs.writeFileSync(goldenPath, actualText)
        }
        if (!fs.existsSync(goldenPath)) {
            failures.push(`golden file does not exist: ${goldenPath}`)
        } else {
            // A golden file is text with LF line ends, also in a checkout that has CRLF (git's
            // core.autocrlf); what the program printed is compared as it is.
            const expected = fs.readFileSync(goldenPath, 'utf8').replace(/\r\n/g, '\n')
            const expectedText = applyNormalizers(expected, test.normalizers)
            const cmp = snapshotCompare(actualText, expectedText, test.snapshot_compare || 'text_exact')
            if (!cmp.ok) failures.push(cmp.message)
        }
    }
    if (test.kind === 'differential') {
        const base = lookupCompleted(context, test.compare_to)
        if (!base) {
            failures.push(`compare_to target not found: ${test.compare_to}`)
        } else {
            for (const field of test.compare_fields || []) {
                if (!compareField(field, result, base)) {
                    failures.push(`differential field mismatch: ${field}`)
                }
            }
        }
    }
    if (test.__backendReference) {
        // A test with `backends`: every back end after the first must agree with the first on
        // the listed fields.
        const base = lookupCompleted(context, test.__backendReference)
        if (!base) {
            failures.push(`reference back end did not run: ${test.__backendReference}`)
        } else {
            for (const field of test.backends_agree || []) {
                if (!compareField(field, result, base)) {
                    failures.push(`differs from ${test.__backendReference}: ${field}`)
                }
            }
        }
    }
    if (test.kind === 'benchmark') {
        if (!result.benchmark) {
            failures.push('missing benchmark payload')
        } else if (result.benchmark.failed) {
            // `--timeit` reports the iteration that failed, its diagnostics and no mean.
            // That is the test's result; it is reported here instead of the runner throwing on
            // the missing metric.
            const reasons = (result.benchmark.diagnostics || []).map(
                (diagnostic) =>
                    `${diagnostic.data?.name || diagnostic.code || 'diagnostic'}: ${diagnostic.message}`
            )
            const reason =
                reasons.length > 0
                    ? reasons.join('; ')
                    : result.benchmark.failure || 'no reason given'
            failures.push(
                `the timed pass failed in iteration ${result.benchmark.iteration ?? '?'}: ${reason}`
            )
        } else if (!Number.isFinite(result.benchmark.mean_ms)) {
            failures.push('benchmark payload has no mean_ms')
        } else if (typeof test.perf_budget_mean_ms === 'number') {
            const tolerance = Number(test.perf_budget_tolerance_pct || 0)
            const ceiling = test.perf_budget_mean_ms * (1 + tolerance / 100)
            if (result.benchmark.mean_ms > ceiling) {
                failures.push(
                    `benchmark mean ${result.benchmark.mean_ms.toFixed(2)} ms exceeds budget ${test.perf_budget_mean_ms.toFixed(2)} ms (+${tolerance}%)`
                )
            }
        }
    }
    // The test's own process is gone and the slot went back with it. What is left to check is the
    // verification script: a process of its own, so it takes a permit from the verification width
    // instead of a test slot, and the runner never has more than `jobs` of them alive next to the
    // `jobs` tests.
    // Only a test that has a verification script takes a permit: the other 3,747 tests must not
    // queue behind one.
    const verifying = verificationConfigOf(test) !== null
    if (context.clock) context.clock.validationAt = performance.now()
    if (verifying && context.verifications) await context.verifications.acquire(1)
    // Waiting for a permit is the runner's own queue, not the verification: it is measured apart
    // from the script's own time, because a queue that grows here is slot capacity nobody uses.
    if (context.clock) context.clock.verifyPermitAt = performance.now()
    try {
        result.verification = await runVerification(test, result)
    } finally {
        if (verifying && context.verifications) context.verifications.release(1)
    }
    if (context.clock) context.clock.verifyDoneAt = performance.now()
    if (result.verification && !result.verification.ok) {
        failures.push(...verifyFailureLines(result.verification))
    }
    return failures
}

async function runOneTest(test, sharedVars, logContext, scratch, scratchRelative, marks) {
    const casePath = path.resolve(path.dirname(test.__planPath), test.case)
    if (test.__planError) {
        return runnerErrorResult(casePath, test.__planError)
    }
    const toolName = test.tool || 'camel'
    const toolExe = toolName === 'python'
        ? pythonExecutable()
        : toolName === 'camel'
            ? camelBinary(logContext.options)
            : toolExecutable(toolName, logContext.options.build)
    if (!toolExe) {
        throw new Error(`unknown test tool: ${toolName}`)
    }
    // The scratch directory is created (and removed) by executePlans, before the precondition
    // runs: the precondition writes into it too. `${test_tmp}` in args, env values and
    // expectations is its repository-relative path; CAMEL_TEST_TMPDIR is the absolute one.
    const scope = {
        ...sharedVars,
        case: casePath,
        test_tmp: scratchRelative,
    }
    if (test.__backend) {
        const passes = sharedVars[`run_${test.__backend}`]
        if (passes === undefined) {
            throw new Error(`unknown back end '${test.__backend}' for ${test.name}: no run_${test.__backend} in test/vars.toml`)
        }
        scope.backend = passes
    }
    const args = expandArgs(test.args || [], scope)
    const env = { ...process.env }
    // Assertions on diagnostics (name, code, line, column) read the one-line text format.
    // A test of the format with source excerpts passes --error-format text, which wins.
    env.CAMEL_ERROR_FORMAT = 'short'
    env.CAMEL_TEST_TMPDIR = scratch
    // Before the test's own `envs`: a value the test writes for itself wins over the default.
    if (pythonEntryPoints(test).tool) hideCudaDevicesFrom(env, test)
    for (const entry of test.envs || []) {
        const text = expandTextTokens(entry, scope)
        const eq = text.indexOf('=')
        if (eq <= 0) {
            throw new Error(`invalid env entry for ${test.name}: ${text}`)
        }
        env[text.slice(0, eq)] = text.slice(eq + 1)
    }
    // Every test child reads the standard library and the runtime library of the install this run
    // measures: `CAMEL_HOME` is set to that install's root here, whichever way the install was
    // named.
    env.CAMEL_HOME = camelInstall(logContext.options)
    // Windows commonly inherits `Path`; a second `PATH` key would hide the compiler paths.
    const pathKey = IS_WINDOWS ? Object.keys(env).find((key) => key.toUpperCase() === 'PATH') || 'PATH' : 'PATH'
    env[pathKey] = `${path.join(env.CAMEL_HOME, 'bin')}${path.delimiter}${env[pathKey] || ''}`

    const timeoutMs =
        (test.timeout_ms || 10000) * TIMEOUT_SCALE *
        (logContext.options.build === 'debug' ? DEBUG_BUILD_TIMEOUT_SCALE : 1)
    // Every test on a host whose kernel reports CPU time runs under the wrapper that reports its
    // own: the command and its environment are unchanged, the wrapper reaps this test's tree and
    // puts the kernel's figure on a fourth pipe (test/tools/child-cpu.mjs). A test that declares
    // `cpu_budget_ms` needs the figure wherever it runs -- it is judged on it -- so it keeps the
    // wrapper on a host that cannot read one, where the reading comes back as the reason and the
    // test fails for it. The cost of the wrapper for the whole fast layer, measured on the host at
    // -Jobs 8: 2.80 min for smoke + feat against 2.60 min for the same 5,890 tests unwrapped.
    const cpuBudgetMs = typeof test.cpu_budget_ms === 'number' ? test.cpu_budget_ms : null
    const wrapForCpu = CAN_READ_CHILD_CPU || cpuBudgetMs !== null
    const launch = wrapForCpu
        ? { command: process.execPath, args: [CHILD_CPU_TOOL, '--', toolExe, ...args] }
        : { command: toolExe, args }
    const proc = await runProcess(launch.command, launch.args, {
        cwd: REPO_ROOT,
        env,
        timeoutMs,
        marks,
        cpuReport: wrapForCpu,
    })
    // The test program is gone, so the slot has done its job. Everything left -- resuming from the
    // child's `close` event, normalizing and asserting on what it printed, the verification script,
    // removing the scratch directory -- is the runner's own work. Measured over eight runs of the
    // smoke + feat fast layer at -Jobs 8: 255-532 s of all held slot time had no child in it (a
    // median of 280 s, 28% of the run's slots), and 213-488 s of that was the verification script
    // (1,920 scripts booting node, 88-145 ms each) and the `skip_unless` probes; the runner's own
    // steps in the slot were 2.9-4.4 s together.
    // A `parallel = false` test passes no hook and keeps its slot to the end (see executePlans).
    if (typeof logContext.releaseSlot === 'function') logContext.releaseSlot()
    const cpuReading = wrapForCpu ? readCpuReport(proc.cpuReport) : { cpuMs: null, reason: null }
    const raw = {
        command: `${toolExe} ${args.join(' ')}`.trim(),
        exitCode: proc.status,
        signal: proc.signal,
        timedOut: proc.timedOut,
        errorMessage: proc.error ? proc.error.message : null,
        wallMs: proc.wallMs,
        stdout: proc.stdout,
        stderr: proc.stderr,
    }
    raw.output = `${raw.stdout}${raw.stderr}`

    const normalized = {
        stdout: applyNormalizers(raw.stdout, test.normalizers),
        stderr: applyNormalizers(raw.stderr, test.normalizers),
        output: applyNormalizers(raw.output, test.normalizers),
    }

    const result = {
        ...raw,
        normalized,
        wallMs: raw.wallMs,
        timeoutMs,
        benchmark: null,
        diagnostics: parseDiagnostics(raw.output),
        testTmp: scratch,
        testTmpRelative: scratchRelative,
        // Set before validation: the verify script payload carries this as `case_path`, and the
        // script that normalizes a source path out of its output needs it.
        casePath,
        // The CPU cost judgement, if this test declared one: the figure the wrapper reported and
        // the budget it is read against, both recorded on the result (meta.json, the verify
        // payload, the status line) so a reader sees the numbers the verdict came from.
        cpuBudgetMs,
        cpuMs: cpuBudgetMs === null ? null : cpuReading.cpuMs,
        cpuReason: cpuBudgetMs === null ? null : cpuReading.reason,
    }

    if (test.kind === 'benchmark' && raw.stdout) {
        result.benchmark = benchmarkFromJson(raw.stdout)
    }
    // Everything above is the runner turning the child's bytes into a result: resuming from the
    // `close` event, normalizing the two streams, parsing the diagnostics. It all happens on the
    // runner's thread, after the child is gone, while the slot is still held.
    if (logContext.clock) logContext.clock.normalizedAt = performance.now()

    // The verification reads this test's output: executePlans removes the scratch directory
    // only after this returns.
    const failures = await validateTest(test, result, { ...logContext, scope })
    // The cost judgement of a test that declares `cpu_budget_ms`, in the unit
    // of 2026-10-23 asks for (docs/handover/reviews/questions-2026-10-23.md,
    // "declaration-cost-gate" (a)):
    //
    //   "改量 CPU 时间，不量墙钟。共享主机上的墙钟随负载漂移 ... 进程的 CPU 时间（用户态 + 内核态）
    //    基本不随负载变"
    //
    // So the figure judged here is the CPU time (user + system) the kernel charged the test's own
    // process tree, and a crossed budget is the test's work growing -- never "the host was busy".
    // `timeout_ms` stays, and is what it is for such a test: a liveness bound. A program that stops
    // working crosses the timeout while its CPU budget stands still, and a program that works
    // harder crosses the budget while the wall clock may say anything at all. The reading is in
    // 10 ms ticks (USER_HZ), which is a thousandth of the budget these plans declare.
    if (cpuBudgetMs !== null && result.timedOut) {
        failures.push(
            `killed at its ${formatDurationMs(timeoutMs)} liveness bound after ` +
                `${formatDurationMs(result.wallMs)}: a cpu_budget_ms test's timeout catches a program ` +
                `that stopped working, and the CPU budget is what catches one that works harder`
        )
    } else if (cpuBudgetMs !== null) {
        if (result.cpuMs === null) {
            failures.push(
                `cpu_budget_ms ${cpuBudgetMs.toFixed(0)} ms has no CPU reading: ${result.cpuReason} ` +
                    `(a budget the runner cannot read is not a bound; test/tools/child-cpu.mjs reads ` +
                    `a child's CPU from /proc/self/stat and needs Linux)`
            )
        } else if (result.cpuMs > cpuBudgetMs) {
            failures.push(
                `used ${formatDurationMs(result.cpuMs)} of CPU against a cpu_budget_ms of ` +
                    `${formatDurationMs(cpuBudgetMs)}: the test's own work grew (its wall clock read ` +
                    `${formatDurationMs(result.wallMs)}, which is not the bound on this host)`
            )
        }
    }
    // A timeout is a pass condition, so it is only a real bound when the test finishes with room to
    // spare. The room is measured in the work's own currency -- the CPU time the kernel charged this
    // test's process tree -- and not in the wall clock, which on a shared host belongs to whoever
    // else is running. This is the same reading, from the same wrapper, that `cpu_budget_ms` is
    // judged against, and the reason the wrapper runs for every test.
    //
    // What the rule cannot read it does not guess: on a host with no /proc/self/stat there is no
    // CPU figure for any test, so the wall clock is all there is and the run says so once at the
    // start (see the header in runPlans). Where the figure is available and a test's reading did
    // not arrive, the test fails rather than passing on a bound whose room nothing examined: a
    // margin that cannot be read is not a margin. A test whose own CPU exceeds its wall clock is
    // multi-threaded work: the reading sums its threads, so this rule reads the work, not the
    // schedule, and such a test states its cost as a `cpu_budget_ms` if its total work is what it
    // wants bounded. Measured over the whole fast layer on 2026-10-24: 21 of 5,873 tests used more
    // CPU than their wall clock (all ONNX-runtime, dlpack and fuzz cases), and every test in the
    // layer had at least four times its own CPU time of room under its timeout, so no bound in the
    // layer is read differently in a green run than it was before this rule.
    if (failures.length === 0 && cpuBudgetMs === null) {
        if (cpuReading.cpuMs !== null && cpuReading.cpuMs * TIMEOUT_MARGIN > timeoutMs) {
            failures.push(
                `used ${cpuReading.cpuMs.toFixed(0)} ms of CPU against a ${timeoutMs.toFixed(0)} ms timeout, ` +
                    `below the ${TIMEOUT_MARGIN}x margin: the bound is the work's own time, not a bound ` +
                    `(raise it with a stated reason, or make the test do less). The wall clock read ` +
                    `${result.wallMs.toFixed(0)} ms, which on a shared host is the neighbours'`
            )
        } else if (cpuReading.cpuMs === null && CAN_READ_CHILD_CPU) {
            failures.push(
                `no CPU reading to judge the margin of its ${timeoutMs.toFixed(0)} ms timeout by: ` +
                    `${cpuReading.reason || 'the wrapper reported nothing'}. A bound whose margin nobody ` +
                    `can read is not a bound, and it is not a pass either (H368)`
            )
        } else if (cpuReading.cpuMs === null && result.wallMs * TIMEOUT_MARGIN > timeoutMs) {
            // No /proc/self/stat on this host: the wall clock is the only reading there is.
            failures.push(
                `finished in ${result.wallMs.toFixed(0)} ms against a ${timeoutMs.toFixed(0)} ms timeout, ` +
                    `below the ${TIMEOUT_MARGIN}x margin: the bound is today's wall time, not a bound ` +
                    `(raise it with a stated reason, or make the test do less). This host reports no CPU ` +
                    `time (/proc/self/stat), so the wall clock is the reading this run has`
            )
        }
    }
    const status = failures.length === 0 ? 'pass' : 'fail'
    return { ...result, failures, status }
}

/**
 * One test's record. The plan and the case are deliberately not copied in: they used to be, once
 * per test, and nothing read either copy back. `run.json` at the run root carries the revision and
 * the list of plans this run selected, and this test's own `meta.json` carries `plan_path` and
 * `case_path`, so what produced this output is recorded by name rather than by a duplicate of a
 * file the checkout still holds. The copies were also the bulk of a run directory: the same plan
 * file was rewritten once for every test in it. See test/tools/runner-output.mjs, and
 * docs/test-plans.md, "Runner Output".
 */
function writeLogFiles(plan, test, result, status, logDir) {
    const statusDir = path.join(logDir, status, sanitizeSegment(plan.id), sanitizeSegment(test.name))
    ensureDir(statusDir)
    fs.writeFileSync(path.join(statusDir, 'stdout.txt'), result.stdout)
    fs.writeFileSync(path.join(statusDir, 'stderr.txt'), result.stderr)
    fs.writeFileSync(path.join(statusDir, 'output.txt'), result.output)
    if (result.verification) {
        fs.writeFileSync(path.join(statusDir, 'verify_stdout.txt'), result.verification.stdout)
        fs.writeFileSync(path.join(statusDir, 'verify_stderr.txt'), result.verification.stderr)
    }
    fs.writeFileSync(
        path.join(statusDir, 'meta.json'),
        JSON.stringify(
            {
                plan_id: plan.id,
                test: test.name,
                case_path: result.casePath,
                status,
                failure_note: test.failure_note || null,
                failures: result.failures,
                diagnostics: result.diagnostics,
                perf_budget_platform: test.perf_budget_platform || null,
                metrics: result.benchmark
                    ? {
                        mean_ms: result.benchmark.mean_ms,
                        ci95_ms: result.benchmark.ci95_ms,
                    }
                    : { wall_ms: result.wallMs },
                command: result.command,
                exit_code: result.exitCode,
                signal: result.signal,
                timed_out: result.timedOut,
                error_message: result.errorMessage,
                verification: result.verification
                    ? {
                        source_kind: result.verification.sourceKind,
                        script_path: result.verification.scriptPath,
                        command: result.verification.command,
                        ok: result.verification.ok,
                        exit_code: result.verification.exitCode,
                        signal: result.verification.signal,
                        timed_out: result.verification.timedOut,
                        error_message: result.verification.errorMessage,
                    }
                    : null,
                wall_ms: result.wallMs,
                timeout_ms: result.timeoutMs ?? null,
                // A test whose bound is its own CPU time records the figure and the budget beside
                // the wall clock, which is provenance for it and not the bound.
                cpu_ms: result.cpuMs ?? null,
                cpu_budget_ms: result.cpuBudgetMs ?? null,
                plan_path: test.__planPath,
                test_tmp: result.testTmpRelative || null,
            },
            null,
            2
        )
    )
    // The log files are written while no test's slot is held, on the one thread every test shares:
    // `printResult` times the call and records the window, because the cost is paid by whichever
    // slots the write blocked, not by the test whose log it is.
}

/**
 * A test with `backends = ["nvm", "fvm", ...]` is one test per back end, named
 * `<name>.<backend>`, in that order. `${backend}` in its `args` is the pass list `run_<backend>`
 * of test/vars.toml. Every assertion of the entry holds for each of them; in addition, each back
 * end after the first must equal the first on the fields listed in `backends_agree`. A back end
 * listed in `known_fail` is expected to fail (see main).
 */
function expandBackends(test) {
    if (!Array.isArray(test.backends)) return [test]
    const knownFail = new Set(test.known_fail || [])
    for (const backend of knownFail) {
        if (!test.backends.includes(backend)) {
            throw new Error(`known_fail names '${backend}', which is not in backends of ${test.name}`)
        }
    }
    return test.backends.map((backend, index) => ({
        ...test,
        name: `${test.name}.${backend}`,
        __backend: backend,
        __backendReference: index === 0 ? null : `${test.name}.${test.backends[0]}`,
        __knownFail: knownFail.has(backend),
    }))
}

/**
 * The tests of one plan file, own and backend-expanded. Exported because the plan inventory
 * (`scripts/plan-inventory.mjs`) must see the same tests this runner selects: two readings of a
 * plan file drift, one of them silently.
 */
export function loadPlanTests(planPath) {
    const doc = parseTomlFile(planPath)
    validatePlanSchema(doc, planPath)
    const planId = doc.id || path.basename(planPath, '.plan.toml')
    const tests = (doc.tests || [])
        .map((test) => ({ ...(doc.defaults || {}), ...test, __planPath: planPath, __planId: planId }))
        .flatMap(expandBackends)
    return { doc, tests }
}

/**
 * A plan the schema rejected. It becomes one failing test of that plan, so the rest of the run
 * still runs and reports; the failure names the plan file, the test and the key. Silently
 * dropping the plan would hide exactly the defect this exists to expose.
 */
function invalidPlan(planPath, error) {
    const id = path.basename(planPath, '.plan.toml')
    return {
        id,
        __planPath: planPath,
        __error: error.message,
        tests: [{
            name: '<invalid plan>',
            kind: 'behavior',
            case: path.basename(planPath),
            args: [],
            __planPath: planPath,
            __planId: id,
            __planError: error.message,
        }],
    }
}

/**
 * A `[[sweeps]]` entry of a plan runs the tests of other plans again under an extra
 * environment, with all their assertions:
 *
 *   [[sweeps]]
 *   name = "poison"                     # named "<name>:<source plan id>:<original name>"
 *   plans = ["../modules", "../linear"] # plan files or directories, relative to this plan
 *   exclude = ["modules/onnx"]          # substrings of plan paths that are left out
 *   envs = ["CAMEL_GC_POISON=1"]
 *   skip_env_prefix = "CAMEL_GC_"       # leave out tests that set such a variable themselves
 *   timeout_scale = 20
 *
 * It is how a property that every program must keep (here: no raw pointer is held across a
 * safepoint) is checked against whatever the other plans exercise, including tests added
 * later. Benchmarks and differential tests are left out, and so is a test that sets
 * `sweep = false`.
 */
export function expandSweeps(doc, planPath) {
    const out = []
    for (const sweep of doc.sweeps || []) {
        if (!sweep.name || !Array.isArray(sweep.plans)) {
            throw new Error(`a [[sweeps]] entry of ${planPath} needs a name and plans`)
        }
        const exclude = (sweep.exclude || []).map((item) => String(item))
        const scale = Number.isFinite(sweep.timeout_scale) ? sweep.timeout_scale : 1
        const prefix = sweep.skip_env_prefix || null
        // `fast_sample = N` keeps N tests per source plan in the fast layer and puts the rest in
        // the milestone layer: the default developer run still checks the property against every
        // plan, and the full sweep (every eligible test) is `--tier milestone` away. The
        // sample prefers a test that runs the program to completion, since that is the one that
        // exercises the most of the collector; a plan of negative tests still contributes one.
        //
        // The sample is risk-based: a source plan whose `tags` contain one of `risk_tags` keeps
        // `risk_sample` tests (collections, closures and iterators move objects across
        // safepoints in more ways, so one sample is thin there), the rest keep `fast_sample`.
        const fastSample = Number.isInteger(sweep.fast_sample) && sweep.fast_sample > 0 ? sweep.fast_sample : 0
        const riskTags = new Set((sweep.risk_tags || []).map((tag) => String(tag)))
        const riskSample = Number.isInteger(sweep.risk_sample) && sweep.risk_sample > 0 ? sweep.risk_sample : 0
        // A plan named twice (a directory target and a file inside it, say) is one source: two
        // expansions of it would be two copies with the same name and the same work.
        const sources = [...new Set(sweep.plans
            .flatMap((entry) => walkPlans(path.resolve(path.dirname(planPath), entry)))
            .filter((source) => source !== planPath)
            .filter((source) => !exclude.some((item) => source.split(path.sep).join('/').includes(item))))]
            .sort()
        for (const source of sources) {
            const { doc: sourceDoc, tests: sourceTests } = loadPlanTests(source)
            // A copy is named after the sweep, its source plan and the source test. The source
            // plan is part of the name because two plans may both have a test called `shapes`
            // without it the copies collide, and a back end waits for, and compares
            // against, whichever copy of its reference ran last. `id` is the plan's stable
            // identifier, unique across the run; the fallback matches loadPlanTests.
            const sourceId = sourceDoc.id || path.basename(source, '.plan.toml')
            const rename = (name) => `${sweep.name}:${sourceId}:${name}`
            const risky = (sourceDoc.tags || []).some((tag) => riskTags.has(String(tag)))
            const sampleCount = fastSample > 0 && risky && riskSample > fastSample ? riskSample : fastSample
            const eligible = []
            for (const test of sourceTests) {
                if (test.kind !== 'behavior' && test.kind !== 'snapshot') continue
                if (test.sweep === false || test.__knownFail) continue
                if ((test.tool || 'camel') !== 'camel') continue
                const own = (test.envs || []).map((entry) => String(entry))
                if (prefix && own.some((entry) => entry.startsWith(prefix))) continue
                eligible.push(test)
            }
            const sampled = new Set()
            if (sampleCount > 0) {
                const preferred = eligible.filter(
                    (test) => (test.expect_exit === undefined || test.expect_exit === 0) &&
                        test.tier !== 'slow' && test.tier !== 'milestone'
                )
                for (const test of [...preferred, ...eligible]) {
                    if (sampled.size >= sampleCount) break
                    sampled.add(test)
                }
            }
            for (const test of eligible) {
                const own = (test.envs || []).map((entry) => String(entry))
                out.push({
                    ...test,
                    name: rename(test.name),
                    envs: [...own, ...(sweep.envs || [])],
                    timeout_ms: (test.timeout_ms || 10000) * scale,
                    verify_timeout_ms: (test.verify_timeout_ms || 5000) * scale,
                    __backendReference: test.__backendReference ? rename(test.__backendReference) : null,
                    __sweep: sweep.name,
                    ...(sampleCount > 0 ? { tier: sampled.has(test) ? 'fast' : 'milestone' } : {}),
                })
            }
        }
    }
    return out
}

/**
 * A test's layer: `tier` on the test first, then its plan. Only `slow` and `milestone` are named
 * destinations; anything else (the old `smoke`/`feat`/`benchmark` labels included) is the fast
 * layer, so a plan written before the split still runs by default. `milestone` is the layer that
 * only runs at a milestone, either because the plan file says so or because a sweep left the copy
 * unsampled. `debug` is a further layer selected only by `--tier debug`: a test that aborts when
 * an assertion fires, or that is only meaningful with assertions compiled in, lives there and is
 * never part of an ordinary or `all` run.
 *
 * Exported because the plan inventory (`scripts/plan-inventory.mjs`) asks the same question when it
 * decides whether a plan's slow tests have a layer: a second copy of this rule would drift from it.
 */
export function testTier(plan, test) {
    const tier = test.tier !== undefined
        ? test.tier
        : plan.tier === 'slow'
            ? 'slow'
            : plan.tier === 'milestone'
                ? 'milestone'
                : plan.tier === 'debug'
                    ? 'debug'
                    : 'fast'
    if (tier !== 'fast' && tier !== 'slow' && tier !== 'milestone' && tier !== 'debug') {
        throw new Error(`test '${test.name}' of ${plan.id || plan.__planPath}: tier must be 'fast', 'slow', 'milestone' or 'debug'`)
    }
    return tier
}

/**
 * Select the tests of a layer. A differential test or a back end that compares against an earlier
 * test needs that test's result, so the earlier test is kept even when its own layer is the other
 * one: a fast run must not fail because it left out the reference a selected test compares
 * against.
 *
 * `debug` is a plan-set layer, not a label a plan carries instead of its normal layer: the plans
 * that exercise views, data blocks and the executor hot paths still run fast in Release, and
 * `debug_tier = true` on the plan also selects them on a Debug build. Their debug-only tests say
 * `tier = "debug"`. `all` therefore leaves those tests out: it runs on the Release binary, where
 * the debug layer's abort case would not abort.
 */
function selectTier(plans, tier, namedPlans = new Set()) {
    const order = []
    for (const plan of plans) {
        for (const test of plan.tests) order.push({ plan, test })
    }
    const wants = (plan, test) => {
        // A plan the schema rejected fails in every layer: its tests never ran, so no layer can
        // honestly say the plan is clean.
        if (test.__planError) return true
        if (tier === 'all') return testTier(plan, test) !== 'debug'
        if (tier === 'debug') return plan.debug_tier === true || testTier(plan, test) === 'debug'
        return testTier(plan, test) === tier
    }
    const indexOfName = new Map()
    const indexOfPlanName = new Map()
    order.forEach((entry, index) => {
        if (!indexOfName.has(entry.test.name)) indexOfName.set(entry.test.name, index)
        indexOfPlanName.set(`${entry.plan.id}/${entry.test.name}`, index)
    })
    const selected = new Set()
    order.forEach((entry, index) => {
        // A plan file named as a target runs in full: naming the plan is the intent, and a plan
        // whose tests are mostly slow would otherwise run almost nothing. A directory target
        // still selects the layer, so the whole-tree `--tier` stays meaningful.
        if (namedPlans.has(entry.plan.__planPath) || wants(entry.plan, entry.test)) {
            selected.add(index)
        }
    })
    const pending = [...selected]
    while (pending.length > 0) {
        const entry = order[pending.pop()]
        const test = entry.test
        const references = []
        if (test.kind === 'differential' && test.compare_to) references.push(test.compare_to)
        if (test.__backendReference) references.push(test.__backendReference)
        for (const reference of references) {
            const index = indexOfPlanName.get(`${entry.plan.id}/${reference}`) ?? indexOfName.get(reference)
            if (index !== undefined && !selected.has(index)) {
                selected.add(index)
                pending.push(index)
            }
        }
    }
    const kept = new Set(selected)
    const selectedPlans = []
    let cursor = 0
    for (const plan of plans) {
        const tests = []
        for (const test of plan.tests) {
            if (kept.has(cursor)) tests.push(test)
            cursor++
        }
        if (tests.length > 0) selectedPlans.push({ ...plan, tests })
    }
    return selectedPlans
}

function loadPlans(targets, tier, layeredPlans = new Set()) {
    // A plan file named on the command line (or by `--changed`'s self rule) runs in full; the
    // plans a directory reaches follow the layer. The same plan reached both ways is named,
    // unless `--changed` marked it `layer: true`: then the tier filter applies to it as well.
    const namedPlans = new Set()
    const discovered = []
    for (const target of targets) {
        const resolved = path.resolve(target)
        if (fs.statSync(resolved).isFile() && !layeredPlans.has(resolved)) namedPlans.add(resolved)
        discovered.push(...walkPlans(target))
    }
    if (targets.length === 0) discovered.push(...walkPlans(PLANS_ROOT))
    // `--all` names the plans root and a plan may also be named explicitly; the same plan must
    // not run twice.
    const planPaths = [...new Set(discovered.map((planPath) => path.resolve(planPath)))]
    const plans = planPaths
        .map((planPath) => {
            try {
                const { doc, tests: ownTests } = loadPlanTests(planPath)
                const tests = [...ownTests, ...expandSweeps(doc, planPath)]
                return { ...doc, __planPath: planPath, tests }
            } catch (error) {
                // A schema error is a property of this plan file: fail this plan and go on. Any
                // other error is a defect of the runner or of a sweep target, and stays fatal.
                if (!(error instanceof PlanSchemaError)) throw error
                return invalidPlan(planPath, error)
            }
        })
        .sort((lhs, rhs) => {
            const lhsOrder = Number.isFinite(lhs.order) ? lhs.order : Number.MAX_SAFE_INTEGER
            const rhsOrder = Number.isFinite(rhs.order) ? rhs.order : Number.MAX_SAFE_INTEGER
            if (lhsOrder !== rhsOrder) return lhsOrder - rhsOrder
            return lhs.__planPath.localeCompare(rhs.__planPath)
        })
    return selectTier(plans, tier, namedPlans)
}

/** The result of a test the runner itself could not run: its failure is reported like any other. */
function runnerErrorResult(casePath, message) {
    return {
        command: '',
        exitCode: null,
        signal: null,
        timedOut: false,
        errorMessage: message,
        wallMs: 0,
        stdout: '',
        stderr: '',
        output: '',
        normalized: { stdout: '', stderr: '', output: '' },
        diagnostics: [],
        benchmark: null,
        testTmp: null,
        testTmpRelative: null,
        verification: null,
        cpuBudgetMs: null,
        cpuMs: null,
        cpuReason: null,
        failures: [`runner error: ${message}`],
        status: 'fail',
        casePath,
    }
}

/**
 * Slot accounting. "The child is slow" and "the runner is busy" are different findings, and a
 * plan wall alone cannot tell them apart: both show up as time the plan took. Every test records
 * when it took a slot, when its child process came into being, when that child was gone, and when
 * the slot went back. The two intervals that answer the question are `pre_spawn_ms` (slot acquired
 * -> child created) and `post_exit_ms` (child gone -> slot released); `no_child_ms` is their sum,
 * which is the slot held while no test process was running.
 *
 * A total is not a finding, so each of the two is cut at the marks that name a mechanism:
 *
 *   pre_spawn_ms = scratch + probe + prep + spawn
 *                  the per-test directory's `mkdir`, the `skip_unless` probe, the runner's own
 *                  argument and environment expansion, and the `spawn` call itself.
 *   post_exit_ms = settle + validate + verify + cleanup + tail
 *                  resuming from the child's `close` event and normalizing what it printed, the
 *                  assertions on that output (a golden is a synchronous read), the verification
 *                  script, removing the scratch directory, and the last hair before the semaphore
 *                  goes back.
 *
 * Neither `probe_ms` nor `verify_ms` is the test program: a `skip_unless` probe is a check of the
 * host, and a verification script reads the result after the child is gone. Each is recorded whole
 * as well as in its in-slot part, so a version that moves one out of the slot can be compared with
 * one that does not.
 */
function roundMs(value) {
    return Math.round(value * 10) / 10
}

/** Milliseconds of the absolute interval `[start, end]` that fall inside any of `windows`. */
function coveredMs(windows, start, end) {
    let total = 0
    for (const window of windows) {
        total += Math.max(0, Math.min(end, window.end) - Math.max(start, window.start))
    }
    return total
}

function slotTiming(entry, clock, marks, windows) {
    if (clock.acquiredAt === undefined && clock.probeAt === undefined) return null
    const base = clock.runStart
    const at = (value) => (value === undefined ? null : Math.round(value - base))
    const hasSlot = clock.acquiredAt !== undefined
    const acquiredAt = clock.acquiredAt ?? base
    const releasedAt = clock.releasedAt ?? performance.now()
    const spawnedAt = marks.spawnedAt
    const exitedAt = marks.exitedAt
    const clamp = (value) => Math.max(0, value)
    // The whole cost of a step, whether or not it ran while the slot was held: every step is
    // reported both ways, because a step that moved out of the slot still costs the plan its time.
    const probeMs = clock.probeAt === undefined ? 0 : (clock.preconditionAt ?? releasedAt) - clock.probeAt
    const settleMs = clock.normalizedAt === undefined ? 0 : clock.normalizedAt - (exitedAt ?? clock.normalizedAt)
    const validateMs =
        clock.validationAt === undefined || clock.normalizedAt === undefined
            ? 0
            : clock.validationAt - clock.normalizedAt
    const cleanupMs = clock.removeMs ?? 0
    // The permit queue ahead of the verification script: runner work, but not the script's.
    const verifyWaitMs =
        clock.validationAt === undefined || clock.verifyPermitAt === undefined
            ? 0
            : clock.verifyPermitAt - clock.validationAt
    // Everything this entry still owed after it let the slot go, including the printing its result
    // waited behind. Summed over the entries and divided by the width, this is the part of the run
    // that no slot can cover.
    const postReleaseMs = clock.settledAt === undefined ? 0 : clock.settledAt - releasedAt
    const childMs = spawnedAt !== undefined && exitedAt !== undefined ? exitedAt - spawnedAt : 0
    // A test the precondition skipped never took a slot: its whole cost is the probe.
    const preSpawnMs = hasSlot ? (spawnedAt ?? releasedAt) - acquiredAt : 0
    const postExitMs = hasSlot ? releasedAt - (exitedAt ?? releasedAt) : 0
    // Walk the marks in order and take what each one added since the previous one, bounded by the
    // slot's release: a mark that is absent, or that lies outside the slot, contributes nothing.
    // That makes each family a partition of its interval whether or not a step happened, and it
    // stays correct when a step moves out of the slot.
    const walk = (from) => {
        let cursor = from
        return (mark) => {
            if (mark === undefined) return 0
            const bounded = Math.min(mark, releasedAt)
            if (bounded < cursor) return 0
            const span = bounded - cursor
            cursor = bounded
            return span
        }
    }
    const beforeSlot = walk(acquiredAt)
    const scratchMs = hasSlot ? beforeSlot(clock.scratchAt) : 0
    const probeInSlotMs = hasSlot ? beforeSlot(clock.preconditionAt) : 0
    const spawnMs = marks.spawnMs ?? 0
    const prepMs = hasSlot ? clamp(preSpawnMs - scratchMs - probeInSlotMs - spawnMs) : 0
    const afterExit = walk(exitedAt ?? releasedAt)
    const settleInSlotMs = hasSlot ? afterExit(clock.normalizedAt) : 0
    const validateInSlotMs = hasSlot ? afterExit(clock.validationAt) : 0
    const verifyInSlotMs = hasSlot ? afterExit(clock.verifyDoneAt) : 0
    const cleanupInSlotMs = hasSlot ? afterExit(clock.removeAt) : 0
    const tailMs = hasSlot
        ? clamp(postExitMs - settleInSlotMs - validateInSlotMs - verifyInSlotMs - cleanupInSlotMs)
        : 0
    const result = entry.outcome ? entry.outcome.result : null
    const verification = result ? result.verification : null
    const verifyMs = verification && verification.wallMs ? verification.wallMs : 0
    return {
        plan: entry.plan.id,
        test: entry.test.name,
        status: entry.outcome ? entry.outcome.status : 'fail',
        serial: clock.serial === true,
        // How many of the run's slot permits this test held: a `parallel = false` test and a
        // benchmark ask for all of them, so it is the one case where a held slot is not one
        // eighth of the run's capacity.
        tokens: hasSlot ? clock.tokens ?? 1 : 0,
        slot_ms: hasSlot ? roundMs(releasedAt - acquiredAt) : 0,
        child_ms: roundMs(childMs),
        pre_spawn_ms: roundMs(preSpawnMs),
        post_exit_ms: roundMs(postExitMs),
        no_child_ms: roundMs(preSpawnMs + postExitMs),
        // Slot time that is neither a child nor a probe of the host: the runner's own work, which
        // is the part a scheduler change can move or make cheap.
        idle_ms: roundMs(clamp(preSpawnMs + postExitMs - probeInSlotMs)),
        queue_ms: hasSlot
            ? roundMs(acquiredAt - (clock.dependenciesDoneAt ?? clock.enqueuedAt ?? acquiredAt))
            : 0,
        // `_ms` is the step whole wherever it ran; `_in_slot_ms` is the part of it that held a slot.
        probe_ms: roundMs(probeMs),
        probe_in_slot_ms: roundMs(probeInSlotMs),
        settle_ms: roundMs(settleMs),
        validate_ms: roundMs(validateMs),
        verify_ms: roundMs(verifyMs),
        verify_in_slot_ms: roundMs(verifyInSlotMs),
        verify_child_ms: roundMs(verification && verification.childMs ? verification.childMs : 0),
        verify_wait_ms: roundMs(verifyWaitMs),
        post_release_ms: roundMs(postReleaseMs),
        cleanup_ms: roundMs(cleanupMs),
        cleanup_in_slot_ms: roundMs(cleanupInSlotMs),
        components_ms: {
            scratch: roundMs(scratchMs),
            probe: roundMs(probeInSlotMs),
            prep: roundMs(prepMs),
            spawn: roundMs(spawnMs),
            settle: roundMs(settleInSlotMs),
            validate: roundMs(validateInSlotMs),
            verify: roundMs(verifyInSlotMs),
            cleanup: roundMs(cleanupInSlotMs),
            tail: roundMs(tailMs),
        },
        log_write_ms: 0,
        // How much of this test's post-exit window was the runner writing log files for *other*
        // tests: log writing is synchronous on the runner's only thread, so it blocks the event
        // loop for everyone and shows up here rather than in the writer's own record.
        log_block_ms: hasSlot ? roundMs(coveredMs(windows, exitedAt ?? acquiredAt, releasedAt)) : 0,
        // When this test held a slot, relative to the start of the run: the occupancy timeline
        // the empty-slot question needs. A slot is held from `acquired` to `released`; a test
        // child runs from `spawned` to `exited`.
        at: {
            enqueued: at(clock.enqueuedAt),
            acquired: at(clock.acquiredAt),
            spawned: at(spawnedAt),
            exited: at(exitedAt),
            released: at(hasSlot ? clock.releasedAt : clock.preconditionAt),
            settled: at(clock.settledAt),
        },
    }
}

function percentile(sorted, fraction) {
    if (sorted.length === 0) return 0
    const index = Math.min(sorted.length - 1, Math.max(0, Math.ceil(fraction * sorted.length) - 1))
    return sorted[index]
}

function distribution(values) {
    const sorted = [...values].sort((lhs, rhs) => lhs - rhs)
    const total = sorted.reduce((sum, value) => sum + value, 0)
    return {
        median: roundMs(percentile(sorted, 0.5)),
        p90: roundMs(percentile(sorted, 0.9)),
        p99: roundMs(percentile(sorted, 0.99)),
        max: roundMs(sorted.length > 0 ? sorted[sorted.length - 1] : 0),
        total: Math.round(total),
    }
}

function topContributors(records, field, count = 20) {
    return [...records]
        .sort((lhs, rhs) => rhs[field] - lhs[field])
        .slice(0, count)
        .map((record) => ({
            plan: record.plan,
            test: record.test,
            status: record.status,
            tokens: record.tokens,
            ms: roundMs(record[field]),
            child_ms: record.child_ms,
            slot_ms: record.slot_ms,
            pre_spawn_ms: record.pre_spawn_ms,
            post_exit_ms: record.post_exit_ms,
            probe_ms: record.probe_ms,
            verify_ms: record.verify_ms,
            verify_wait_ms: record.verify_wait_ms,
            cleanup_ms: record.cleanup_ms,
            log_block_ms: record.log_block_ms,
            components_ms: record.components_ms,
        }))
}

/**
 * Occupancy over the run, one bucket per second: how many slots were held, and in how many of
 * them a test child was running. An empty slot (held - children) is work the scheduler never
 * handed out, and a held slot with no child is runner work. Both are in the same picture, which
 * is the only way to tell them apart after the fact.
 */
function buildTimeline(records, planWallMs, jobs, bucketMs = 1000) {
    const buckets = Math.max(1, Math.ceil(planWallMs / bucketMs))
    const held = new Array(buckets).fill(0)
    const children = new Array(buckets).fill(0)
    const overlap = (start, end, bucket) => {
        const bucketStart = bucket * bucketMs
        return Math.max(0, Math.min(end, bucketStart + bucketMs) - Math.max(start, bucketStart)) / bucketMs
    }
    for (const record of records) {
        const { acquired, released, spawned, exited } = record.at
        if (acquired === null || released === null) continue
        for (let bucket = Math.floor(acquired / bucketMs); bucket < buckets && bucket * bucketMs < released; bucket++) {
            held[bucket] += overlap(acquired, released, bucket)
        }
        if (spawned !== null && exited !== null) {
            for (let bucket = Math.floor(spawned / bucketMs); bucket < buckets && bucket * bucketMs < exited; bucket++) {
                children[bucket] += overlap(spawned, exited, bucket)
            }
        }
    }
    const round = (value) => Math.round(value * 100) / 100
    return {
        bucket_ms: bucketMs,
        slots_held: held.map(round),
        children: children.map(round),
    }
}

/**
 * Event-loop lag. The runner has one JavaScript thread, and every synchronous thing it does around
 * a test happens there: creating a scratch directory, reading a golden, writing the log files. A
 * synchronous section blocks *every* slot at once, and it hides from the per-test intervals: a
 * child that finished while the runner was writing another test's log is not seen to have finished
 * until the write returns, so the wait is charged to that child's `child_ms` and its own
 * `post_exit_ms` stays small. The one measurement that cannot hide it is a timer that reports how
 * late it fired: `gap - period` is time the loop could not service.
 */
const LOOP_SAMPLE_MS = 25

function startLoopMonitor() {
    const stalls = []
    let last = performance.now()
    const timer = setInterval(() => {
        const now = performance.now()
        const gap = now - last
        last = now
        if (gap >= LOOP_SAMPLE_MS * 2) {
            stalls.push({ start: now - gap, end: now, ms: gap })
        }
    }, LOOP_SAMPLE_MS)
    if (typeof timer.unref === 'function') timer.unref()
    return () => {
        clearInterval(timer)
        return stalls
    }
}

/**
 * The whole run's slot accounting, for `summary.json`. A total without names is how the wrong
 * thing gets optimized, so every total that has a distribution also names its top contributors.
 */
function summarizeSlotTimings(records, planWallMs, jobs, stalls, logWindows, runStartMs) {
    const sum = (field) => records.reduce((total, record) => total + (record[field] || 0), 0)
    const childTotal = sum('child_ms')
    const componentNames = ['scratch', 'probe', 'prep', 'spawn', 'settle', 'validate', 'verify', 'cleanup', 'tail']
    const componentTotals = {}
    for (const name of componentNames) {
        componentTotals[name] = Math.round(
            records.reduce((total, record) => total + (record.components_ms[name] || 0), 0)
        )
    }
    const sortedStalls = [...stalls].sort((lhs, rhs) => rhs.ms - lhs.ms)
    const stallTotal = stalls.reduce((total, stall) => total + stall.ms, 0)
    // A stall is the runner's own thread being unavailable. Naming which of its known synchronous
    // sections the stall overlapped is what turns "the loop was busy" into "the loop was writing
    // logs": the log windows are exact, so the overlap is not a guess.
    const stallCovered = stalls.reduce(
        (total, stall) => total + coveredMs(logWindows, stall.start, stall.end),
        0
    )
    // A `parallel = false` test and a benchmark hold every permit, so `jobs * wall` minus the
    // one-permit sum is not the idle capacity: `slot_ms` counts their slot once. Both quantities
    // are reported, because "the run never handed out this slot" and "this test locked the others
    // out on purpose" are different answers to why the ratio is below `jobs`.
    const capacityMs = records.reduce((total, record) => total + (record.tokens || 1) * record.slot_ms, 0)
    const serialExclusiveMs = records.reduce(
        (total, record) => total + ((record.tokens || 1) - 1) * record.slot_ms,
        0
    )
    return {
        tests: records.length,
        plan_wall_ms: Math.round(planWallMs),
        // Inside a slot: the test child, and the runner work that still happens while the slot is
        // held (creating the child).
        child_ms_total: Math.round(childTotal),
        slot_ms_total: Math.round(sum('slot_ms')),
        no_child_ms_total: Math.round(sum('no_child_ms')),
        idle_ms_total: Math.round(sum('idle_ms')),
        // Each step whole, wherever it ran, and the part of it that held a slot. The runner's own
        // work is the sum of these; moving one out of the slot changes `_in_slot_ms`, not `_ms`.
        scratch_ms_total: Math.round(componentTotals.scratch),
        prep_ms_total: Math.round(componentTotals.prep),
        spawn_ms_total: Math.round(componentTotals.spawn),
        settle_ms_total: Math.round(sum('settle_ms')),
        settle_in_slot_ms_total: Math.round(componentTotals.settle),
        validate_ms_total: Math.round(sum('validate_ms')),
        validate_in_slot_ms_total: Math.round(componentTotals.validate),
        cleanup_ms_total: Math.round(sum('cleanup_ms')),
        cleanup_in_slot_ms_total: Math.round(sum('cleanup_in_slot_ms')),
        probe_ms_total: Math.round(sum('probe_ms')),
        probe_in_slot_ms_total: Math.round(sum('probe_in_slot_ms')),
        verify_ms_total: Math.round(sum('verify_ms')),
        verify_in_slot_ms_total: Math.round(sum('verify_in_slot_ms')),
        verify_child_ms_total: Math.round(
            records.reduce((total, record) => total + (record.verify_child_ms || 0), 0)
        ),
        // The queue in front of the verification width, and what each entry still owed after it
        // released its slot. `post_release_ms_total / jobs` is the part of the wall no slot covers,
        // so it is the number that says whether the run is still slot-bound or not.
        verify_wait_ms_total: Math.round(sum('verify_wait_ms')),
        post_release_ms_total: Math.round(sum('post_release_ms')),
        post_release_per_slot_ms: Math.round(sum('post_release_ms') / jobs),
        last_release_ms: Math.round(
            records.reduce((total, record) => Math.max(total, record.at.released || 0), 0)
        ),
        last_settled_ms: Math.round(
            records.reduce((total, record) => Math.max(total, record.at.settled || 0), 0)
        ),
        log_write_ms_total: Math.round(sum('log_write_ms')),
        // The same writing, charged to the tests whose slot it blocked instead of to the writer.
        log_block_ms_total: Math.round(sum('log_block_ms')),
        log_writes: records.filter((record) => record.log_write_ms > 0).length,
        probes: records.filter((record) => record.probe_ms > 0).length,
        verifications: records.filter((record) => record.verify_ms > 0).length,
        // The reported ratio: how many tests' children ran at once, on average, over the plan.
        avg_concurrency: planWallMs > 0 ? Math.round((childTotal / planWallMs) * 100) / 100 : 0,
        // What the slots were actually doing: every held slot, whether or not a child ran in it.
        avg_slots_held: planWallMs > 0 ? Math.round((sum('slot_ms') / planWallMs) * 100) / 100 : 0,
        // Capacity, counted in permits: `jobs * wall` is what the run had, minus what the tests
        // held (a serial test counts `jobs` times) is what nobody used.
        capacity_slot_ms_total: Math.round(planWallMs * jobs),
        serial_tests: records.filter((record) => (record.tokens || 1) > 1).length,
        serial_exclusive_ms_total: Math.round(serialExclusiveMs),
        empty_slot_ms_total: Math.max(0, Math.round(planWallMs * jobs - capacityMs)),
        no_child_ms: distribution(records.map((record) => record.no_child_ms)),
        pre_spawn_ms: distribution(records.map((record) => record.pre_spawn_ms)),
        post_exit_ms: distribution(records.map((record) => record.post_exit_ms)),
        idle_ms: distribution(records.map((record) => record.idle_ms)),
        queue_ms: distribution(records.map((record) => record.queue_ms)),
        child_ms: distribution(records.map((record) => record.child_ms)),
        probe_ms: distribution(records.map((record) => record.probe_ms)),
        verify_ms: distribution(records.map((record) => record.verify_ms)),
        verify_wait_ms: distribution(records.map((record) => record.verify_wait_ms)),
        post_release_ms: distribution(records.map((record) => record.post_release_ms)),
        cleanup_ms: distribution(records.map((record) => record.cleanup_ms)),
        log_block_ms: distribution(records.map((record) => record.log_block_ms)),
        // The runner's own thread, measured instead of inferred.
        loop_lag: {
            sample_ms: LOOP_SAMPLE_MS,
            threshold_ms: LOOP_SAMPLE_MS * 2,
            stall_count: stalls.length,
            stall_ms_total: Math.round(stallTotal),
            stall_ms_covered_by_log_write: Math.round(stallCovered),
            max_stall_ms: roundMs(sortedStalls.length > 0 ? sortedStalls[0].ms : 0),
            top_stalls: sortedStalls.slice(0, 20).map((stall) => ({
                at_ms: Math.round(stall.start - runStartMs),
                ms: roundMs(stall.ms),
            })),
        },
        // `scratch`, `probe`, `prep`, `spawn`, `settle`, `validate`, `verify`, `cleanup` and `tail`
        // are the partition of every held slot's not-child time, so they sum to `no_child_ms_total`;
        // the `_ms_total` fields above are the same steps whole, wherever they ran.
        components_ms: componentTotals,
        timeline: buildTimeline(records, planWallMs, jobs),
        top_no_child_ms: topContributors(records, 'no_child_ms'),
        top_pre_spawn_ms: topContributors(records, 'pre_spawn_ms'),
        top_post_exit_ms: topContributors(records, 'post_exit_ms'),
        top_idle_ms: topContributors(records, 'idle_ms'),
        top_child_ms: topContributors(records, 'child_ms'),
        top_probe_ms: topContributors(records, 'probe_ms'),
        top_verify_ms: topContributors(records, 'verify_ms'),
        top_verify_wait_ms: topContributors(records, 'verify_wait_ms'),
        top_post_release_ms: topContributors(records, 'post_release_ms'),
        top_cleanup_ms: topContributors(records, 'cleanup_ms'),
        top_log_block_ms: topContributors(records, 'log_block_ms'),
    }
}

/**
 * Run every selected test, at most `options.jobs` at a time, and print the results in plan and
 * test order: a status line, its failure details and its log files are the same as a serial run's,
 * in the same place, whichever test finishes first. A test that compares against an earlier one
 * waits for it; a test marked serial (`isSerial`) holds every slot while it runs.
 */
async function executePlans(plans, options, sharedVars, logDir) {
    const entries = []
    for (const plan of plans) {
        for (const test of plan.tests) {
            entries.push({ plan, test, index: entries.length, dependencies: [], settled: false, outcome: null })
        }
    }
    const byName = new Map()
    const byPlanName = new Map()
    for (const entry of entries) {
        if (!byName.has(entry.test.name)) byName.set(entry.test.name, entry)
        byPlanName.set(`${entry.plan.id}/${entry.test.name}`, entry)
    }
    for (const entry of entries) {
        const references = []
        if (entry.test.kind === 'differential' && entry.test.compare_to) references.push(entry.test.compare_to)
        if (entry.test.__backendReference) references.push(entry.test.__backendReference)
        entry.dependencies = references
            .map((name) => byPlanName.get(`${entry.plan.id}/${name}`) || byName.get(name))
            .filter((dependency) => dependency && dependency !== entry)
    }

    const slots = new Slots(options.jobs)
    // Two things around a test are processes of their own but are not the test program, and neither
    // needs the test slot: a `skip_unless` precondition is the runner asking the host a question,
    // and a verification script reads the result after the child is gone. Each gets its own width,
    // so a probe that hangs for its whole timeout cannot own one of the run's slots.
    // Measured: 1,920 verification scripts booting node inside their slot were 168-265 s, and 75-83
    // probes inside theirs 44-304 s, of the 255-532 s this plan spent holding slots with no test
    // process in them.
    // One width for both, `--jobs`: a probe may hang for its whole `skip_unless_timeout_ms`, and a
    // one-wide lane turns one hung probe into a stall of every probe behind it. Measured with a
    // one-wide probe lane: two 120 s MNIST preconditions left 42 probe tests queued and the run's
    // last 150 s with no slot held at all. `jobs`-wide, the same timeouts cost one lane each.
    const probes = new Slots(options.jobs)
    const verifications = new Slots(options.jobs)
    const summary = { pass: 0, fail: 0, skip: 0, xfail: 0 }
    // Results by test name, as a serial run had them: a differential test and a back end after
    // the first read the result of the test they compare against.
    const completed = new Map()
    const startedAt = performance.now()
    // Windows in which the runner was synchronously writing a test's log files: the one section it
    // can point at when a slot's post-exit time turns out to be someone else's work.
    const logWindows = []
    const stopLoopMonitor = startLoopMonitor()
    let printed = 0
    let printedPlan = null

    const printResult = (entry) => {
        const { plan, test, outcome } = entry
        entry.logWriteMs = 0
        if (plan !== printedPlan) {
            printPlanHeader(plan)
            printedPlan = plan
        }
        if (outcome.status === 'skip') {
            summary.skip++
            printStatusLine('skip', test.name)
            printDetailLines([outcome.reason])
            return
        }
        summary[outcome.status]++
        printStatusLine(outcome.status, test.name, outcome.metric)
        if (outcome.failures.length > 0) {
            const note = test.__knownFail ? test.known_fail_note || test.failure_note : test.failure_note
            if (note) {
                printDetailLines([`note: ${note}`])
            }
            printDetailLines(outcome.failures)
            if (outcome.status === 'fail') printFailedOutput(outcome.result)
        }
        const writeStarted = performance.now()
        writeLogFiles(plan, test, outcome.result, outcome.status, logDir)
        entry.logWriteMs = performance.now() - writeStarted
        logWindows.push({ start: writeStarted, end: performance.now() })
    }

    // Results are printed strictly in plan and test order: whichever test finishes first, the
    // output is the serial run's output. A result waits until every earlier one is out.
    const drain = () => {
        while (printed < entries.length && entries[printed].settled) {
            printResult(entries[printed])
            printed++
        }
    }

    for (const entry of entries) {
        // When the slot was taken, when the child existed and when the slot went back: the
        // scheduler's own accounting, recorded for every test whether it passes, fails or skips.
        const clock = {}
        const marks = {}
        clock.runStart = startedAt
        entry.promise = (async () => {
            clock.enqueuedAt = performance.now()
            if (entry.dependencies.length > 0) {
                await Promise.all(entry.dependencies.map((dependency) => dependency.promise))
            }
            clock.dependenciesDoneAt = performance.now()
            const serial = isSerial(entry.plan, entry.test)
            const tokens = serial ? options.jobs : 1
            clock.serial = serial
            clock.tokens = tokens
            const wantsProbe = Array.isArray(entry.test.skip_unless) && entry.test.skip_unless.length > 0
            // A test that needs another test's result queues at the front: it starts right after
            // its reference instead of behind every test of the run, which would also delay the
            // in-order printer until the end.
            const priority = entry.dependencies.length > 0
            let scratch = null
            let probeHeld = false
            let slotAcquired = false
            let slotReleased = false
            // Idempotent: `runOneTest` releases the slot the moment the test's own process is gone,
            // and the finally block releases it for a test that never got that far (a skip, a
            // runner error).
            const releaseSlot = () => {
                if (slotReleased) return
                slotReleased = true
                if (slotAcquired) slots.release(tokens)
                clock.releasedAt = performance.now()
            }
            try {
                if (wantsProbe && !serial) {
                    // The probe runs before the slot is taken: it checks the host, it does not run
                    // the test, and the two MNIST probes plus five device probes on a host that
                    // cannot download held slots for 4-17 s each. A serial test keeps everything
                    // inside its slot: nothing may run next to it at all, probe included.
                    await probes.acquire(1)
                    probeHeld = true
                    scratch = createTestScratchDir(entry.test)
                    clock.scratchAt = performance.now()
                    const scratchRelative = path.relative(REPO_ROOT, scratch).split(path.sep).join('/')
                    clock.probeAt = performance.now()
                    const skipReason = await checkPrecondition(entry.test, sharedVars, scratch, scratchRelative)
                    clock.preconditionAt = performance.now()
                    probes.release(1)
                    probeHeld = false
                    if (skipReason) {
                        entry.outcome = { status: 'skip', reason: skipReason }
                        return
                    }
                }
                await slots.acquire(tokens, priority)
                slotAcquired = true
                clock.acquiredAt = performance.now()
                if (!wantsProbe || serial) {
                    scratch = createTestScratchDir(entry.test)
                    clock.scratchAt = performance.now()
                    const scratchRelative = path.relative(REPO_ROOT, scratch).split(path.sep).join('/')
                    // The mark is only taken for a test that has a precondition: `performance.now()`
                    // around a call that returns at once is a rounding artefact (0.1 ms) on
                    // thousands of tests, not a probe, and it would be counted as one.
                    if (wantsProbe) clock.probeAt = performance.now()
                    const skipReason = await checkPrecondition(entry.test, sharedVars, scratch, scratchRelative)
                    if (wantsProbe) clock.preconditionAt = performance.now()
                    if (skipReason) {
                        entry.outcome = { status: 'skip', reason: skipReason }
                        return
                    }
                }
                const scratchRelative = path.relative(REPO_ROOT, scratch).split(path.sep).join('/')
                let result
                try {
                    result = await runOneTest(
                        entry.test,
                        sharedVars,
                        // A serial test passes no hook and keeps its slot to the end: it was marked
                        // because it shares something outside its own scratch directory, and its
                        // verification script may read that same thing.
                        { options, completed, planId: entry.plan.id, clock, verifications, releaseSlot: serial ? null : releaseSlot },
                        scratch,
                        scratchRelative,
                        marks
                    )
                } catch (error) {
                    const casePath = path.resolve(path.dirname(entry.test.__planPath), entry.test.case)
                    result = runnerErrorResult(casePath, error && error.message ? error.message : String(error))
                }
                clock.validatedAt = performance.now()
                completed.set(`${entry.plan.id}/${entry.test.name}`, result)
                completed.set(entry.test.name, result)
                if (entry.test.__knownFail) {
                    // A known failure that fails is reported and counted apart, and does not fail
                    // the run. One that passes fails the run: the mark is out of date.
                    if (result.status === 'fail') {
                        result.status = 'xfail'
                    } else {
                        result.status = 'fail'
                        result.failures.push(
                            `marked known_fail on '${entry.test.__backend}' but passes: remove the mark`
                        )
                    }
                }
                entry.outcome = {
                    status: result.status,
                    result,
                    metric: formatMetric(result),
                    failures: result.failures,
                }
            } finally {
                if (probeHeld) probes.release(1)
                // The test's own directory is not this run's evidence: it goes whether the
                // test passed or failed. The run root stays on failure (main prints it).
                const removeStarted = performance.now()
                if (scratch) {
                    fs.rmSync(scratch, { recursive: true, force: true })
                }
                clock.removeAt = performance.now()
                clock.removeMs = clock.removeAt - removeStarted
                releaseSlot()
            }
        })()
            .catch((error) => {
                // A failure of the runner itself (a precondition helper, a verification script the
                // scheduler lost) must not lose the output of the tests around it: it becomes the
                // result of this test and the run continues to its summary.
                const casePath = path.resolve(path.dirname(entry.test.__planPath), entry.test.case)
                const result = runnerErrorResult(casePath, error && error.message ? error.message : String(error))
                entry.outcome = { status: 'fail', result, metric: '', failures: result.failures }
            })
            .finally(() => {
                clock.settledAt = performance.now()
                entry.timing = slotTiming(entry, clock, marks, logWindows)
                entry.settled = true
                drain()
            })
    }

    await Promise.all(entries.map((entry) => entry.promise))
    drain()
    const stalls = stopLoopMonitor()
    const timings = []
    for (const entry of entries) {
        if (!entry.timing) continue
        entry.timing.log_write_ms = roundMs(entry.logWriteMs || 0)
        timings.push(entry.timing)
    }
    return { summary, wallMs: performance.now() - startedAt, timings, stalls, logWindows, startedAt }
}

async function main() {
    const options = parseArgs(process.argv.slice(2))
    preflightBuild(options)
    // Writing golden files from several tests at once would make the content depend on which
    // test finished last; update-golden is a deliberate, serial maintenance run.
    if (options.updateGolden && options.jobs !== 1) {
        console.log(`${COLORS.cyan}[INFO]${COLORS.reset} --update-golden writes golden files: running serially (--jobs 1)`)
        options.jobs = 1
    }
    const sharedVars = loadVars(options)
    const namedPlans = options.targets.filter((target) => {
        try {
            return fs.statSync(target).isFile() && !options.layeredPlans.has(path.resolve(target))
        } catch {
            return false
        }
    })
    const plans = loadPlans(options.targets, options.tier, options.layeredPlans)
    // The pid keeps two runs started in the same second (the self-test does this) out of one
    // directory, whose logs and summary.json would otherwise overwrite each other.
    const logDir = path.join(LOG_ROOT, `${timestamp()}_${process.pid}`)
    // Retention before the record. The tree this run is about to add a directory to is the one that
    // grew without bound (test/tools/runner-output.mjs has the policy and the evidence): it keeps
    // the newest RUN_DIRS_KEPT run directories, never one a live run owns, and never one this
    // runner did not write. The current run's directory is named here, before it exists, so the
    // policy protects it by identity as well.
    const retention = pruneRunnerOutput({
        resultsRoot: RESULTS_ROOT,
        keep: resultsKeep(),
        currentRunDir: logDir,
        currentScratchRoot: TEST_SCRATCH_RUN,
    })
    ensureDir(LOG_ROOT)
    // A directory left by an earlier process whose pid this run reused is not this run's log, the
    // same rule the scratch root follows below: its files must not be read as this run's.
    fs.rmSync(logDir, { recursive: true, force: true })
    ensureDir(logDir)
    writeRunManifest(logDir, options, plans)
    const testCount = plans.reduce((count, plan) => count + plan.tests.length, 0)

    console.log(`${COLORS.cyan}[INFO]${COLORS.reset} camel executable (${options.build}): ${camelBinary(options)}`)
    console.log(`${COLORS.cyan}[INFO]${COLORS.reset} discovered plans: ${plans.length}`)
    console.log(
        `${COLORS.cyan}[INFO]${COLORS.reset} tier: ${options.tier}, tests: ${testCount}, jobs: ${options.jobs}`
    )
    if (!CAN_READ_CHILD_CPU) {
        // Said once, and only where it changes what the run means: on this host every test's
        // timeout margin is judged on the wall clock, because the kernel offers no CPU figure.
        console.log(
            `${COLORS.yellow}[WARN]${COLORS.reset} no /proc/self/stat here: a test's timeout margin is ` +
                `judged on the wall clock, which this host owns (the shared host judges it on CPU time)`
        )
    }
    console.log(`${COLORS.cyan}[RETAIN]${COLORS.reset} ${formatRetention(retention)}`)
    if (options.layeredPlans.size > 0 && options.tier !== 'all') {
        // `--changed` marked these plans `layer: true`: naming them is not "run the whole file",
        // it is "run the cases of this layer", so the slow cases that must not run from an
        // ordinary change are filtered while the fast ones still run.
        console.log(
            `${COLORS.cyan}[INFO]${COLORS.reset} selected plan target(s) follow --tier ${options.tier}: ${options.layeredPlans.size}`
        )
    }
    if (namedPlans.length > 0 && options.tier !== 'all') {
        // Naming a plan is the user's intent: it runs in full, whatever `--tier` says. A
        // directory target still selects the layer (see loadPlans).
        console.log(
            `${COLORS.cyan}[INFO]${COLORS.reset} named plan target(s) run in full: ${namedPlans.length} (the tier filter leaves a named plan alone)`
        )
    }
    if (testCount === 0 && options.tier !== 'all') {
        console.log(
            `${COLORS.cyan}[INFO]${COLORS.reset} this selection has no ${options.tier} tests: --tier all runs both layers`
        )
    }

    // The run owns tmp/test/run-<pid>: a root left by an earlier process with this pid (the pid
    // was reused) must not be read as this run's files.
    fs.rmSync(TEST_SCRATCH_RUN, { recursive: true, force: true })
    const outcome = await executePlans(plans, options, sharedVars, logDir)
    const testRootKept = outcome.summary.fail > 0
    if (testRootKept) {
        // The per-test directories are gone, but what a program wrote outside them (and the
        // root itself) is evidence: say where it is instead of removing it.
        console.log(`${COLORS.cyan}[INFO]${COLORS.reset} test scratch kept: ${TEST_SCRATCH_RUN}`)
    } else {
        fs.rmSync(TEST_SCRATCH_RUN, { recursive: true, force: true })
    }

    const split = gateSplit(outcome.wallMs)
    const slotTiming = summarizeSlotTimings(
        outcome.timings,
        outcome.wallMs,
        options.jobs,
        outcome.stalls,
        outcome.logWindows,
        outcome.startedAt
    )
    fs.writeFileSync(
        path.join(logDir, 'summary.json'),
        JSON.stringify(
            {
                generated_at: new Date().toISOString(),
                camel_exe: camelBinary(options),
                build: options.build,
                update_golden: options.updateGolden,
                tier: options.tier,
                jobs: options.jobs,
                wall_ms: outcome.wallMs,
                build_seconds: split.buildSeconds,
                test_seconds: split.testSeconds,
                total_seconds: split.totalSeconds,
                test_root: TEST_SCRATCH_RUN,
                test_root_kept: testRootKept,
                summary: outcome.summary,
                // Where the slots went, for every test: the slot held before a child existed and
                // after it was gone, plus the aggregates and the tests that own most of it.
                slot_timing: slotTiming,
                test_timings: outcome.timings,
            },
            null,
            2
        )
    )

    printRunSummary(outcome.summary)
    // One line for the flow, with the whole accounting behind it in summary.json: the ratio the
    // task is about, and the intervals that decide whether the ratio is the host's or the runner's.
    console.log(
        `${COLORS.cyan}[SLOT]${COLORS.reset} concurrency ${slotTiming.avg_concurrency}/${options.jobs}`
        + `, slots held ${slotTiming.avg_slots_held}/${options.jobs}`
        + `, children ${(slotTiming.child_ms_total / 1000).toFixed(1)} s`
        + `, no-child ${(slotTiming.no_child_ms_total / 1000).toFixed(1)} s`
        + ` (pre-spawn ${(slotTiming.pre_spawn_ms.total / 1000).toFixed(1)} s,`
        + ` post-exit ${(slotTiming.post_exit_ms.total / 1000).toFixed(1)} s)`
        + `, probes ${(slotTiming.probe_ms_total / 1000).toFixed(1)} s`
        + ` (in slot ${(slotTiming.probe_in_slot_ms_total / 1000).toFixed(1)} s)`
        + `, verifications ${(slotTiming.verify_ms_total / 1000).toFixed(1)} s`
        + ` (in slot ${(slotTiming.verify_in_slot_ms_total / 1000).toFixed(1)} s)`
        + `, log writes ${(slotTiming.log_write_ms_total / 1000).toFixed(1)} s`
        + ` (blocking ${(slotTiming.log_block_ms_total / 1000).toFixed(1)} s of slots)`
        + `, serial ${slotTiming.serial_tests} tests locking ${(slotTiming.serial_exclusive_ms_total / 1000).toFixed(1)} s`
        + `, empty capacity ${(slotTiming.empty_slot_ms_total / 1000).toFixed(1)} s`
        + `, after the last release ${((slotTiming.plan_wall_ms - slotTiming.last_release_ms) / 1000).toFixed(1)} s`
        + ` (${(slotTiming.post_release_per_slot_ms / 1000).toFixed(1)} s of it per slot)`
        + `, loop stalls ${(slotTiming.loop_lag.stall_ms_total / 1000).toFixed(1)} s`
    )
    console.log(
        `${COLORS.cyan}[INFO]${COLORS.reset} wall: ${(outcome.wallMs / 1000).toFixed(1)} s, jobs: ${options.jobs}, tier: ${options.tier}`
    )
    console.log(formatGateSplit(split))

    const exitCode = outcome.summary.fail > 0 ? 1 : 0
    process.exit(exitCode)
}

// Only a command that names this file as its entry point starts a run: the plan inventory imports
// `loadPlanTests` and `expandSweeps` from here, and importing a module must not run the tests.
if (process.argv[1] && path.resolve(process.argv[1]) === __filename) {
    main().catch((error) => {
        console.error(error && error.stack ? error.stack : String(error))
        if (fs.existsSync(TEST_SCRATCH_RUN)) {
            console.error(`${COLORS.cyan}[INFO]${COLORS.reset} test scratch kept: ${TEST_SCRATCH_RUN}`)
        }
        process.exit(1)
    })
}
