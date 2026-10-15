import fs from 'fs'
import path from 'path'
import os from 'os'
import { performance } from 'perf_hooks'
import { spawnSync } from 'child_process'
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
const CAMEL_EXE = path.join(REPO_ROOT, 'out', 'latest', 'bin', IS_WINDOWS ? 'camel.exe' : 'camel')
const TOOL_EXES = {
    camel: CAMEL_EXE,
    python: null, // resolved on use (pythonExecutable)
    node: process.execPath,
    'camel-format': path.join(
        REPO_ROOT,
        'out',
        'latest',
        'bin',
        IS_WINDOWS ? 'camel-format.exe' : 'camel-format'
    ),
}
const COLORS = {
    green: '\x1b[32m',
    red: '\x1b[31m',
    cyan: '\x1b[36m',
    yellow: '\x1b[33m',
    reset: '\x1b[0m',
}

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

function parseArgs(argv) {
    const options = {
        updateGolden: false,
        tier: null,
        targets: [],
    }
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '--update-golden') {
            options.updateGolden = true
        } else if (arg === '--tier' && i + 1 < argv.length) {
            options.tier = argv[++i]
        } else {
            options.targets.push(path.resolve(process.cwd(), arg))
        }
    }
    return options
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
    if (result.benchmark) {
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
function checkPrecondition(test, sharedVars) {
    if (!test.skip_unless || test.skip_unless.length === 0) {
        return null
    }
    const casePath = path.resolve(path.dirname(test.__planPath), test.case)
    const [cmd, ...args] = expandArgs(test.skip_unless, { ...sharedVars, case: casePath })
    const proc = spawnSync(cmd, args, {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        timeout: test.skip_unless_timeout_ms || 120000,
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

function runVerification(test, result) {
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
    }
    fs.writeFileSync(payloadPath, JSON.stringify(payload, null, 2))

    const proc = spawnSync(process.execPath, [scriptPath, payloadPath], {
        cwd: REPO_ROOT,
        env: process.env,
        encoding: 'utf8',
        timeout: (test.verify_timeout_ms || 5000) * TIMEOUT_SCALE,
    })
    fs.rmSync(tempDir, { recursive: true, force: true })

    const timedOut = Boolean(proc.error && proc.error.code === 'ETIMEDOUT')
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

function validateTest(test, result, context) {
    const failures = []
    if (typeof test.expect_exit === 'number' && result.exitCode !== test.expect_exit) {
        failures.push(`expected exit ${test.expect_exit}, got ${result.exitCode}`)
    }
    for (const needle of test.expect_stdout_contains || []) {
        if (!result.normalized.stdout.includes(needle)) {
            failures.push(`stdout does not contain '${needle}'`)
        }
    }
    if (typeof test.expect_stdout_starts_with === 'string'
        && !result.normalized.stdout.startsWith(test.expect_stdout_starts_with)) {
        failures.push(`stdout does not start with ${JSON.stringify(test.expect_stdout_starts_with)}`)
    }
    for (const needle of test.expect_stderr_contains || []) {
        if (!result.normalized.stderr.includes(needle)) {
            failures.push(`stderr does not contain '${needle}'`)
        }
    }
    for (const needle of test.expect_output_contains || []) {
        if (!result.normalized.output.includes(needle)) {
            failures.push(`output does not contain '${needle}'`)
        }
    }
    for (const needle of test.expect_output_not_contains || []) {
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
        const base = context.completed.get(test.compare_to)
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
        const base = context.completed.get(test.__backendReference)
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
    result.verification = runVerification(test, result)
    if (result.verification && !result.verification.ok) {
        failures.push(...verifyFailureLines(result.verification))
    }
    return failures
}

function runOneTest(test, sharedVars, logContext) {
    const casePath = path.resolve(path.dirname(test.__planPath), test.case)
    const toolName = test.tool || 'camel'
    const toolExe = toolName === 'python' ? pythonExecutable() : TOOL_EXES[toolName]
    if (!toolExe) {
        throw new Error(`unknown test tool: ${toolName}`)
    }
    const scope = {
        ...sharedVars,
        case: casePath,
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
    for (const entry of test.envs || []) {
        const text = String(entry)
        const eq = text.indexOf('=')
        if (eq <= 0) {
            throw new Error(`invalid env entry for ${test.name}: ${text}`)
        }
        env[text.slice(0, eq)] = text.slice(eq + 1)
    }
    env.CAMEL_HOME = path.join(REPO_ROOT, 'out', 'latest')
    // Windows commonly inherits `Path`; a second `PATH` key would hide the compiler paths.
    const pathKey = IS_WINDOWS ? Object.keys(env).find((key) => key.toUpperCase() === 'PATH') || 'PATH' : 'PATH'
    env[pathKey] = `${path.join(env.CAMEL_HOME, 'bin')}${path.delimiter}${env[pathKey] || ''}`

    const started = performance.now()
    const proc = spawnSync(toolExe, args, {
        cwd: REPO_ROOT,
        env,
        encoding: 'utf8',
        timeout: (test.timeout_ms || 10000) * TIMEOUT_SCALE,
    })
    const ended = performance.now()
    const raw = {
        command: `${toolExe} ${args.join(' ')}`.trim(),
        exitCode: typeof proc.status === 'number' ? proc.status : null,
        signal: proc.signal ?? null,
        timedOut: Boolean(proc.error && proc.error.code === 'ETIMEDOUT'),
        errorMessage: proc.error ? proc.error.message : null,
        wallMs: ended - started,
        stdout: proc.stdout || '',
        stderr: proc.stderr || '',
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
    }

    if (test.kind === 'benchmark' && raw.stdout) {
        result.benchmark = benchmarkFromJson(raw.stdout)
    }

    const failures = validateTest(test, result, logContext)
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
    const tests = (doc.tests || [])
        .map((test) => ({ ...(doc.defaults || {}), ...test, __planPath: planPath }))
        .flatMap(expandBackends)
    return { doc, tests }
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
        const sources = sweep.plans
            .flatMap((entry) => walkPlans(path.resolve(path.dirname(planPath), entry)))
            .filter((source) => source !== planPath)
            .filter((source) => !exclude.some((item) => source.split(path.sep).join('/').includes(item)))
            .sort()
        for (const source of sources) {
            for (const test of loadPlanTests(source).tests) {
                if (test.kind !== 'behavior' && test.kind !== 'snapshot') continue
                if (test.sweep === false || test.__knownFail) continue
                if ((test.tool || 'camel') !== 'camel') continue
                const own = (test.envs || []).map((entry) => String(entry))
                if (prefix && own.some((entry) => entry.startsWith(prefix))) continue
                out.push({
                    ...test,
                    name: rename(test.name),
                    envs: [...own, ...(sweep.envs || [])],
                    timeout_ms: (test.timeout_ms || 10000) * scale,
                    verify_timeout_ms: (test.verify_timeout_ms || 5000) * scale,
                    __backendReference: test.__backendReference ? rename(test.__backendReference) : null,
                    __sweep: sweep.name,
                })
            }
        }
    }
    return out
}

function loadPlans(targets, tier) {
    const planPaths = targets.length > 0 ? targets.flatMap((target) => walkPlans(target)) : walkPlans(PLANS_ROOT)
    const plans = planPaths
        .map((planPath) => {
            const { doc, tests: ownTests } = loadPlanTests(planPath)
            const tests = [...ownTests, ...expandSweeps(doc, planPath)]
            return { ...doc, __planPath: planPath, tests }
        })
        .filter((plan) => !tier || plan.tier === tier)
        .sort((lhs, rhs) => {
            const lhsOrder = Number.isFinite(lhs.order) ? lhs.order : Number.MAX_SAFE_INTEGER
            const rhsOrder = Number.isFinite(rhs.order) ? rhs.order : Number.MAX_SAFE_INTEGER
            if (lhsOrder !== rhsOrder) return lhsOrder - rhsOrder
            return lhs.__planPath.localeCompare(rhs.__planPath)
        })
    return plans
}

function main() {
    const options = parseArgs(process.argv.slice(2))
    const sharedVars = loadVars()
    const plans = loadPlans(options.targets, options.tier)
    ensureDir(LOG_ROOT)
    const logDir = path.join(LOG_ROOT, timestamp())
    ensureDir(logDir)

    console.log(`${COLORS.cyan}[INFO]${COLORS.reset} camel executable: ${CAMEL_EXE}`)
    console.log(`${COLORS.cyan}[INFO]${COLORS.reset} discovered plans: ${plans.length}`)

    const summary = { pass: 0, fail: 0, skip: 0, xfail: 0 }
    const completed = new Map()

    for (const plan of plans) {
        printPlanHeader(plan)
        for (const test of plan.tests) {
            const skipReason = checkPrecondition(test, sharedVars)
            if (skipReason) {
                summary.skip++
                printStatusLine('skip', test.name)
                printDetailLines([skipReason])
                continue
            }
            const result = runOneTest(test, sharedVars, { options, completed })
            completed.set(test.name, result)
            if (test.__knownFail) {
                // A known failure that fails is reported and counted apart, and does not fail
                // the run. One that passes fails the run: the mark is out of date.
                if (result.status === 'fail') {
                    result.status = 'xfail'
                } else {
                    result.status = 'fail'
                    result.failures.push(
                        `marked known_fail on '${test.__backend}' but passes: remove the mark`
                    )
                }
            }
            summary[result.status]++
            const metric = formatMetric(result)
            printStatusLine(result.status, test.name, metric)
            if (result.failures.length > 0) {
                const note = test.__knownFail ? test.known_fail_note || test.failure_note : test.failure_note
                if (note) {
                    printDetailLines([`note: ${note}`])
                }
                printDetailLines(result.failures)
                if (result.status === 'fail') printFailedOutput(result)
            }
            writeLogFiles(plan, test, result, result.status, logDir)
        }
    }

    fs.writeFileSync(
        path.join(logDir, 'summary.json'),
        JSON.stringify(
            {
                generated_at: new Date().toISOString(),
                camel_exe: CAMEL_EXE,
                update_golden: options.updateGolden,
                summary,
            },
            null,
            2
        )
    )

    printRunSummary(summary)

    const exitCode = summary.fail > 0 ? 1 : 0
    process.exit(exitCode)
}

main()
