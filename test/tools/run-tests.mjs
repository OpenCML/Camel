import fs from 'fs'
import path from 'path'
import os from 'os'
import { performance } from 'perf_hooks'
import { spawn, spawnSync } from 'child_process'
import { fileURLToPath } from 'url'
import { parseTomlFile } from './toml-lite.mjs'
import { compareGirDotIsomorphic } from './gir-dot.mjs'

const __filename = fileURLToPath(import.meta.url)
const TOOLS_ROOT = path.dirname(__filename)
const TEST_ROOT = path.dirname(TOOLS_ROOT)
const PLANS_ROOT = path.join(TEST_ROOT, 'plans')
const VARS_PATH = path.join(TEST_ROOT, 'vars.toml')
const RESULTS_ROOT = path.join(TEST_ROOT, 'results')
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
    'name', 'desc', 'kind', 'case', 'args', 'envs', 'tool', 'timeout_ms', 'verify_timeout_ms',
    'skip_unless', 'skip_unless_timeout_ms', 'parallel', 'tier', 'sweep',
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
    }
}

function camelHome(build) {
    return path.join(REPO_ROOT, 'out', BUILDS[build].dir)
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
const TEST_SCRATCH_ROOT = path.join(REPO_ROOT, 'tmp', 'test')
const TEST_SCRATCH_RUN = path.join(TEST_SCRATCH_ROOT, `run-${process.pid}`)

function timestamp() {
    const now = new Date()
    const pad = (n) => String(n).padStart(2, '0')
    return `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}_${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`
}

function ensureDir(dir) {
    fs.mkdirSync(dir, { recursive: true })
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
 */
function runProcess(command, args, { cwd, env, timeoutMs }) {
    return new Promise((resolve) => {
        const started = performance.now()
        let stdout = ''
        let stderr = ''
        let timedOut = false
        let settled = false
        let timer = null
        let child = null
        const finish = (status, signal, error) => {
            if (settled) return
            settled = true
            if (timer) clearTimeout(timer)
            resolve({
                status: typeof status === 'number' ? status : null,
                signal: signal ?? null,
                error: error || null,
                timedOut,
                wallMs: performance.now() - started,
                stdout,
                stderr,
            })
        }
        try {
            child = spawn(command, args, {
                cwd,
                env,
                stdio: ['ignore', 'pipe', 'pipe'],
                windowsHide: true,
                // On POSIX a process group lets the timeout take the whole tree down; on Windows
                // `taskkill /T` does that.
                detached: !IS_WINDOWS,
            })
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

// `debug` is not a layer of an ordinary run: it selects the plans that exercise views, data
// blocks and the executor on a Debug build, where the view-escape assertion exists at all.
const TIERS = new Set(['fast', 'slow', 'all', 'debug'])

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
        // `--jobs` defaults to the processor count: one test per core, no more. `--jobs 1` gives
        // exactly the historical serial execution, which is how the runner's own self-test
        // compares parallel output with serial output.
        jobs: DEFAULT_JOBS,
        targets: [],
    }
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '--update-golden') {
            options.updateGolden = true
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
 * Refuse to run the debug layer against anything but a Debug binary. out/debug is written by
 * `collect-out.js debug` (the Debug build), and its absence is the common mistake; a binary left
 * there by another configuration is caught by its own --version line, which names the build.
 */
function preflightBuild(build) {
    const exe = toolExecutable('camel', build)
    if (!fs.existsSync(exe)) {
        console.error(
            `${COLORS.red}[FAIL]${COLORS.reset} no ${BUILDS[build].label} binary at ${exe}`
        )
        console.error(
            `    Build it with the shared helper: pwsh -NoProfile -File ${path.join(path.dirname(REPO_ROOT), 'camel-build.ps1')} -Path ${REPO_ROOT} -Config ${BUILDS[build].label}`
        )
        console.error(`    The ${BUILDS[build].label} tier never falls back to out/latest.`)
        process.exit(2)
    }
    if (build !== 'debug') return
    const home = camelHome(build)
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

function loadVars() {
    // Built-in variables; test/vars.toml may override them.
    const builtins = { python: pythonExecutable() }
    if (!fs.existsSync(VARS_PATH)) return builtins
    return { ...builtins, ...parseTomlFile(VARS_PATH) }
}

/**
 * Replace the repository root with `<ROOT>` and write the rest of that path with forward slashes,
 * so a snapshot does not depend on where the checkout lives or on the platform's separator. The
 * root is recognised with forward slashes, with native separators, and with escaped backslashes
 * (a Windows path inside a quoted DOT or JSON string), in its own case and in lower case.
 */
function normalizePathText(text, repoRoot = REPO_ROOT) {
    const forward = repoRoot.replace(/\\/g, '/')
    let output = text
    if (repoRoot.includes('\\')) {
        // Windows: `C:\\repo\\a\\b` (escaped) first, then `C:\repo\a\b` (native). Only the
        // separator form the root itself was written in is rewritten in the rest of the path,
        // so an escape that follows the path (`\"`, `\n`) is left alone.
        const quote = (root) => root.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
        const rest = '[^\\s"\'`<>|*?,;]*'
        for (const [root, separator] of [
            [repoRoot.replace(/\\/g, '\\\\'), /\\\\/g],
            [repoRoot, /\\/g],
        ]) {
            const pattern = new RegExp(`${quote(root)}(${rest})`, 'gi')
            output = output.replace(pattern, (_, tail) => `<ROOT>${tail.replace(separator, '/')}`)
        }
    }
    return output.replaceAll(forward, '<ROOT>').replaceAll(forward.toLowerCase(), '<ROOT>')
}

function applyNormalizers(text, names) {
    let output = text
    for (const name of names || []) {
        if (name === 'strip_ansi') {
            output = output.replace(/\x1b\[[0-9;]*m/g, '')
        } else if (name === 'normalize_newlines') {
            output = output.replace(/\r\n/g, '\n')
        } else if (name === 'trim_trailing_space') {
            output = output
                .split('\n')
                .map((line) => line.replace(/[ \t]+$/g, ''))
                .join('\n')
        } else if (name === 'normalize_paths') {
            output = normalizePathText(output)
        } else if (name === 'normalize_addresses') {
            output = output.replace(/0x[0-9a-fA-F]+/g, '<ADDR>')
        } else if (name === 'normalize_origins') {
            // The source origin ids of a GIR dump depend on what was compiled before the dumped
            // program (a pass module written in Camel takes some).
            output = output.replace(/"origin": "[0-9]+"/g, '"origin": "<ORIGIN>"')
        } else if (name === 'normalize_duration_seconds') {
            output = output.replace(/computed in [0-9]+\.[0-9]+ seconds/g, 'computed in <TIME> seconds')
        }
    }
    return output
}

function parseDiagnostics(text) {
    const diagnostics = []
    const clean = applyNormalizers(text || '', ['strip_ansi', 'normalize_newlines', 'trim_trailing_space'])
    const pattern = /^(?:\([^)]+\))?(.*?):(\d+):(\d+): \[[^\]]+\]: .* \(name=([^,]+), code=(0x[0-9A-Fa-f]+)\)$/gm
    let match
    while ((match = pattern.exec(clean)) !== null) {
        diagnostics.push({
            path: match[1],
            line: Number.parseInt(match[2], 10),
            column: Number.parseInt(match[3], 10),
            name: match[4],
            code: match[5],
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

function formatMetric(result) {
    // A failed `--timeit` iteration reports the diagnosis, not a mean (H66): the failure is the
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
    const proc = await runProcess(cmd, args, {
        cwd: REPO_ROOT,
        // The precondition writes where the test would: a dataset downloaded for it lands in
        // its own scratch directory, so two tests (or two runs) cannot corrupt one cache.
        env: { ...process.env, CAMEL_TEST_TMPDIR: scratch },
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
        }
    }

    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-test-verify-'))
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
            }
        }
    } else {
        scriptPath = path.join(tempDir, 'verify-inline.mjs')
        sourceKind = 'inline'
        fs.writeFileSync(scriptPath, inlineVerifyModuleSource(config.source))
    }

    const payloadPath = path.join(tempDir, 'input.json')
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
        test_tmp: result.testTmp,
        test_tmp_relative: result.testTmpRelative,
    }
    fs.writeFileSync(payloadPath, JSON.stringify(payload, null, 2))

    const proc = await runProcess(process.execPath, [scriptPath, payloadPath], {
        cwd: REPO_ROOT,
        env: process.env,
        timeoutMs: (test.verify_timeout_ms || 5000) * TIMEOUT_SCALE,
    })
    fs.rmSync(tempDir, { recursive: true, force: true })

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
            // `--timeit` reports the iteration that failed, its diagnostics and no mean (H66).
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
    result.verification = await runVerification(test, result)
    if (result.verification && !result.verification.ok) {
        failures.push(...verifyFailureLines(result.verification))
    }
    return failures
}

async function runOneTest(test, sharedVars, logContext, scratch, scratchRelative) {
    const casePath = path.resolve(path.dirname(test.__planPath), test.case)
    if (test.__planError) {
        return runnerErrorResult(casePath, test.__planError)
    }
    const toolName = test.tool || 'camel'
    const toolExe = toolName === 'python' ? pythonExecutable() : toolExecutable(toolName, logContext.options.build)
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
    for (const entry of test.envs || []) {
        const text = expandTextTokens(entry, scope)
        const eq = text.indexOf('=')
        if (eq <= 0) {
            throw new Error(`invalid env entry for ${test.name}: ${text}`)
        }
        env[text.slice(0, eq)] = text.slice(eq + 1)
    }
    env.CAMEL_HOME = camelHome(logContext.options.build)
    // Windows commonly inherits `Path`; a second `PATH` key would hide the compiler paths.
    const pathKey = IS_WINDOWS ? Object.keys(env).find((key) => key.toUpperCase() === 'PATH') || 'PATH' : 'PATH'
    env[pathKey] = `${path.join(env.CAMEL_HOME, 'bin')}${path.delimiter}${env[pathKey] || ''}`

    const timeoutMs =
        (test.timeout_ms || 10000) * TIMEOUT_SCALE *
        (logContext.options.build === 'debug' ? DEBUG_BUILD_TIMEOUT_SCALE : 1)
    const proc = await runProcess(toolExe, args, {
        cwd: REPO_ROOT,
        env,
        timeoutMs,
    })
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
        benchmark: null,
        diagnostics: parseDiagnostics(raw.output),
        testTmp: scratch,
        testTmpRelative: scratchRelative,
    }

    if (test.kind === 'benchmark' && raw.stdout) {
        result.benchmark = benchmarkFromJson(raw.stdout)
    }

    // The verification reads this test's output: executePlans removes the scratch directory
    // only after this returns.
    const failures = await validateTest(test, result, { ...logContext, scope })
    const status = failures.length === 0 ? 'pass' : 'fail'
    return { ...result, failures, status, casePath }
}

function writeLogFiles(plan, test, result, status, logDir) {
    const statusDir = path.join(logDir, status, sanitizeSegment(plan.id), sanitizeSegment(test.name))
    ensureDir(statusDir)
    const planCopyPath = path.join(statusDir, 'plan.toml')
    fs.copyFileSync(test.__planPath, planCopyPath)
    if (fs.existsSync(result.casePath)) {
        fs.copyFileSync(result.casePath, path.join(statusDir, path.basename(result.casePath)))
    }
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
                plan_path: test.__planPath,
                test_tmp: result.testTmpRelative || null,
            },
            null,
            2
        )
    )
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

function loadPlanTests(planPath) {
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
 *   name = "poison"                     # the tests are named "<name>:<original name>"
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
function expandSweeps(doc, planPath) {
    const out = []
    for (const sweep of doc.sweeps || []) {
        if (!sweep.name || !Array.isArray(sweep.plans)) {
            throw new Error(`a [[sweeps]] entry of ${planPath} needs a name and plans`)
        }
        const exclude = (sweep.exclude || []).map((item) => String(item))
        const scale = Number.isFinite(sweep.timeout_scale) ? sweep.timeout_scale : 1
        const prefix = sweep.skip_env_prefix || null
        const rename = (name) => `${sweep.name}:${name}`
        // `fast_sample = N` keeps N tests per source plan in the fast layer and puts the rest in
        // the slow one: the default developer run still checks the property against every plan,
        // and the full sweep (every eligible test) is `--tier all` away. The
        // sample prefers a test that runs the program to completion, since that is the one that
        // exercises the most of the collector; a plan of negative tests still contributes one.
        //
        // The sample is risk-based: a source plan whose `tags` contain one of `risk_tags` keeps
        // `risk_sample` tests (collections, closures and iterators move objects across
        // safepoints in more ways, so one sample is thin there), the rest keep `fast_sample`.
        const fastSample = Number.isInteger(sweep.fast_sample) && sweep.fast_sample > 0 ? sweep.fast_sample : 0
        const riskTags = new Set((sweep.risk_tags || []).map((tag) => String(tag)))
        const riskSample = Number.isInteger(sweep.risk_sample) && sweep.risk_sample > 0 ? sweep.risk_sample : 0
        const sources = sweep.plans
            .flatMap((entry) => walkPlans(path.resolve(path.dirname(planPath), entry)))
            .filter((source) => source !== planPath)
            .filter((source) => !exclude.some((item) => source.split(path.sep).join('/').includes(item)))
            .sort()
        for (const source of sources) {
            const { doc: sourceDoc, tests: sourceTests } = loadPlanTests(source)
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
                    (test) => (test.expect_exit === undefined || test.expect_exit === 0) && test.tier !== 'slow'
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
                    ...(sampleCount > 0 ? { tier: sampled.has(test) ? 'fast' : 'slow' } : {}),
                })
            }
        }
    }
    return out
}

/**
 * A test's layer: `tier` on the test first, then its plan. Only `slow` is special; anything else
 * (the old `smoke`/`feat`/`benchmark` labels included) is the fast layer, so a plan written
 * before the split still runs by default. `debug` is a third layer selected only by
 * `--tier debug`: a test that aborts when an assertion fires, or that is only meaningful with
 * assertions compiled in, lives there and is never part of an ordinary or `all` run.
 */
function testTier(plan, test) {
    const tier = test.tier !== undefined
        ? test.tier
        : plan.tier === 'slow'
            ? 'slow'
            : plan.tier === 'debug'
                ? 'debug'
                : 'fast'
    if (tier !== 'fast' && tier !== 'slow' && tier !== 'debug') {
        throw new Error(`test '${test.name}' of ${plan.id || plan.__planPath}: tier must be 'fast', 'slow' or 'debug'`)
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

function loadPlans(targets, tier) {
    // A plan file named on the command line (or by `--changed`'s self rule) runs in full; the
    // plans a directory reaches follow the layer. The same plan reached both ways is named.
    const namedPlans = new Set()
    const discovered = []
    for (const target of targets) {
        const resolved = path.resolve(target)
        if (fs.statSync(resolved).isFile()) namedPlans.add(resolved)
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
        failures: [`runner error: ${message}`],
        status: 'fail',
        casePath,
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
    const summary = { pass: 0, fail: 0, skip: 0, xfail: 0 }
    // Results by test name, as a serial run had them: a differential test and a back end after
    // the first read the result of the test they compare against.
    const completed = new Map()
    const startedAt = performance.now()
    let printed = 0
    let printedPlan = null

    const printResult = (entry) => {
        const { plan, test, outcome } = entry
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
        writeLogFiles(plan, test, outcome.result, outcome.status, logDir)
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
        entry.promise = (async () => {
            if (entry.dependencies.length > 0) {
                await Promise.all(entry.dependencies.map((dependency) => dependency.promise))
            }
            const tokens = isSerial(entry.plan, entry.test) ? options.jobs : 1
            // A test that needs another test's result queues at the front: it starts right after
            // its reference instead of behind every test of the run, which would also delay the
            // in-order printer until the end.
            await slots.acquire(tokens, entry.dependencies.length > 0)
            let scratch = null
            try {
                scratch = createTestScratchDir(entry.test)
                const scratchRelative = path.relative(REPO_ROOT, scratch).split(path.sep).join('/')
                const skipReason = await checkPrecondition(entry.test, sharedVars, scratch, scratchRelative)
                if (skipReason) {
                    entry.outcome = { status: 'skip', reason: skipReason }
                    return
                }
                let result
                try {
                    result = await runOneTest(
                        entry.test,
                        sharedVars,
                        { options, completed, planId: entry.plan.id },
                        scratch,
                        scratchRelative
                    )
                } catch (error) {
                    const casePath = path.resolve(path.dirname(entry.test.__planPath), entry.test.case)
                    result = runnerErrorResult(casePath, error && error.message ? error.message : String(error))
                }
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
                // The test's own directory is not this run's evidence: it goes whether the
                // test passed or failed. The run root stays on failure (main prints it).
                if (scratch) {
                    fs.rmSync(scratch, { recursive: true, force: true })
                }
                slots.release(tokens)
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
                entry.settled = true
                drain()
            })
    }

    await Promise.all(entries.map((entry) => entry.promise))
    drain()
    return { summary, wallMs: performance.now() - startedAt }
}

async function main() {
    const options = parseArgs(process.argv.slice(2))
    preflightBuild(options.build)
    // Writing golden files from several tests at once would make the content depend on which
    // test finished last; update-golden is a deliberate, serial maintenance run.
    if (options.updateGolden && options.jobs !== 1) {
        console.log(`${COLORS.cyan}[INFO]${COLORS.reset} --update-golden writes golden files: running serially (--jobs 1)`)
        options.jobs = 1
    }
    const sharedVars = loadVars()
    const namedPlans = options.targets.filter((target) => {
        try {
            return fs.statSync(target).isFile()
        } catch {
            return false
        }
    })
    const plans = loadPlans(options.targets, options.tier)
    ensureDir(LOG_ROOT)
    // The pid keeps two runs started in the same second (the self-test does this) out of one
    // directory, whose logs and summary.json would otherwise overwrite each other.
    const logDir = path.join(LOG_ROOT, `${timestamp()}_${process.pid}`)
    ensureDir(logDir)
    const testCount = plans.reduce((count, plan) => count + plan.tests.length, 0)

    console.log(`${COLORS.cyan}[INFO]${COLORS.reset} camel executable (${options.build}): ${toolExecutable('camel', options.build)}`)
    console.log(`${COLORS.cyan}[INFO]${COLORS.reset} discovered plans: ${plans.length}`)
    console.log(
        `${COLORS.cyan}[INFO]${COLORS.reset} tier: ${options.tier}, tests: ${testCount}, jobs: ${options.jobs}`
    )
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

    fs.writeFileSync(
        path.join(logDir, 'summary.json'),
        JSON.stringify(
            {
                generated_at: new Date().toISOString(),
                camel_exe: toolExecutable('camel', options.build),
                build: options.build,
                update_golden: options.updateGolden,
                tier: options.tier,
                jobs: options.jobs,
                wall_ms: outcome.wallMs,
                test_root: TEST_SCRATCH_RUN,
                test_root_kept: testRootKept,
                summary: outcome.summary,
            },
            null,
            2
        )
    )

    printRunSummary(outcome.summary)
    console.log(
        `${COLORS.cyan}[INFO]${COLORS.reset} wall: ${(outcome.wallMs / 1000).toFixed(1)} s, jobs: ${options.jobs}, tier: ${options.tier}`
    )

    const exitCode = outcome.summary.fail > 0 ? 1 : 0
    process.exit(exitCode)
}

main().catch((error) => {
    console.error(error && error.stack ? error.stack : String(error))
    if (fs.existsSync(TEST_SCRATCH_RUN)) {
        console.error(`${COLORS.cyan}[INFO]${COLORS.reset} test scratch kept: ${TEST_SCRATCH_RUN}`)
    }
    process.exit(1)
})
