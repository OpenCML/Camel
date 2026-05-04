import fs from 'fs'
import path from 'path'
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
const COLORS = {
    green: '\x1b[32m',
    red: '\x1b[31m',
    cyan: '\x1b[36m',
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

function loadVars() {
    if (!fs.existsSync(VARS_PATH)) return {}
    return parseTomlFile(VARS_PATH)
}

function normalizePathText(text) {
    const root = REPO_ROOT.replace(/\\/g, '/')
    return text.replaceAll(root, '<ROOT>').replaceAll(root.toLowerCase(), '<ROOT>')
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

function formatMetric(result) {
    if (result.benchmark) {
        return `${formatDurationMs(result.benchmark.mean_ms)} +/- ${formatDurationMs(result.benchmark.ci95_ms)}`
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

function printRunSummary(summary) {
    const total = summary.pass + summary.fail
    console.log('')
    console.log(`${COLORS.cyan}[SUMMARY]${COLORS.reset} total: ${total}, pass: ${summary.pass}, fail: ${summary.fail}`)
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

function compareField(field, actual, baseline) {
    if (field === 'exit_code') return actual.exitCode === baseline.exitCode
    if (field === 'stdout_normalized') return actual.normalized.stdout === baseline.normalized.stdout
    if (field === 'stderr_normalized') return actual.normalized.stderr === baseline.normalized.stderr
    if (field === 'output_normalized') return actual.normalized.output === baseline.normalized.output
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
    for (const needle of test.expect_stderr_contains || []) {
        if (!result.normalized.stderr.includes(needle)) {
            failures.push(`stderr does not contain '${needle}'`)
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
            const expected = fs.readFileSync(goldenPath, 'utf8')
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
    return failures
}

function runOneTest(test, sharedVars, logContext) {
    const casePath = path.resolve(path.dirname(test.__planPath), test.case)
    const scope = {
        ...sharedVars,
        case: casePath,
    }
    const args = expandArgs(test.args || [], scope)
    const env = { ...process.env }
    env.CAMEL_HOME = path.join(REPO_ROOT, 'out', 'latest')
    env.PATH = `${path.join(env.CAMEL_HOME, 'bin')}${path.delimiter}${env.PATH || ''}`

    const started = performance.now()
    const proc = spawnSync(CAMEL_EXE, args, {
        cwd: REPO_ROOT,
        env,
        encoding: 'utf8',
        timeout: test.timeout_ms || 10000,
    })
    const ended = performance.now()
    const raw = {
        command: `${CAMEL_EXE} ${args.join(' ')}`.trim(),
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
                wall_ms: result.wallMs,
                plan_path: test.__planPath,
            },
            null,
            2
        )
    )
}

function loadPlans(targets, tier) {
    const planPaths = targets.length > 0 ? targets.flatMap((target) => walkPlans(target)) : walkPlans(PLANS_ROOT)
    const plans = planPaths
        .map((planPath) => {
            const doc = parseTomlFile(planPath)
            const tests = (doc.tests || []).map((test) => ({ ...test, __planPath: planPath }))
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

    const summary = { pass: 0, fail: 0 }
    const completed = new Map()

    for (const plan of plans) {
        printPlanHeader(plan)
        for (const test of plan.tests) {
            const result = runOneTest(test, sharedVars, { options, completed })
            completed.set(test.name, result)
            summary[result.status]++
            const metric = formatMetric(result)
            printStatusLine(result.status, test.name, metric)
            if (result.failures.length > 0) {
                if (test.failure_note) {
                    printDetailLines([`note: ${test.failure_note}`])
                }
                printDetailLines(result.failures)
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
