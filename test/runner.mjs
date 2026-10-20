import path from 'path'
import fs from 'fs'
import { spawnSync } from 'child_process'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
const TEST_ROOT = path.dirname(__filename)
const REPO_ROOT = path.dirname(TEST_ROOT)
const PLANS_ROOT = path.join(TEST_ROOT, 'plans')
const RUNNER = path.join(TEST_ROOT, 'tools', 'run-tests.mjs')
const SELECTION_PATH = path.join(TEST_ROOT, 'plan-selection.json')
// `origin/fsh-dev` is the branch handover work is based on: `--changed` answers what this
// checkout changed relative to it.
const CHANGED_BASE = 'origin/fsh-dev'

const TARGETS = {
    smoke: path.join(PLANS_ROOT, 'smoke'),
    feat: path.join(PLANS_ROOT, 'feat'),
    parse: path.join(PLANS_ROOT, 'feat', 'parse'),
    compile: path.join(PLANS_ROOT, 'feat', 'compile'),
    opt: path.join(PLANS_ROOT, 'feat', 'opt'),
    trans: path.join(PLANS_ROOT, 'feat', 'trans'),
    linear: path.join(PLANS_ROOT, 'feat', 'linear'),
    linearNvm: path.join(PLANS_ROOT, 'feat', 'linear'),
    linearJit: path.join(PLANS_ROOT, 'feat', 'linear'),
    para: path.join(PLANS_ROOT, 'feat', 'para'),
    modules: path.join(PLANS_ROOT, 'feat', 'modules'),
    modulesStd: path.join(PLANS_ROOT, 'feat', 'modules', 'std.plan.toml'),
    modulesNn: path.join(PLANS_ROOT, 'feat', 'modules', 'nn.plan.toml'),
    perf: path.join(PLANS_ROOT, 'perf'),
    all: PLANS_ROOT
}

const DEFAULT_TARGETS = [
    path.join(PLANS_ROOT, 'smoke'),
    path.join(PLANS_ROOT, 'feat')
]

// Options that take a value: the value belongs to the option, not to the target list.
const VALUE_FLAGS = new Set(['--tier', '--jobs'])

function resolvePlanPathFromSegments(segments) {
    if (segments.length === 0) return null
    const base = path.join(PLANS_ROOT, ...segments)
    const planPath = `${base}.plan.toml`
    if (fs.existsSync(planPath)) return planPath
    if (fs.existsSync(base)) return base
    return null
}

function resolveNamedTarget(name) {
    const compact = String(name).replace(/^[./\\]+/, '')
    if (compact === 'all') return TARGETS.all
    if (compact === 'smoke') return TARGETS.smoke
    if (compact === 'feat') return TARGETS.feat
    if (compact === 'parse' || compact === 'feat/parse' || compact === 'feat\\parse') return TARGETS.parse
    if (compact === 'compile' || compact === 'feat/compile' || compact === 'feat\\compile') return TARGETS.compile
    if (compact === 'opt' || compact === 'feat/opt' || compact === 'feat\\opt') return TARGETS.opt
    if (compact === 'trans' || compact === 'feat/trans' || compact === 'feat\\trans') return TARGETS.trans
    if (compact === 'linear' || compact === 'feat/linear' || compact === 'feat\\linear') return TARGETS.linear
    if (compact === 'linear/nvm' || compact === 'linear\\nvm' || compact === 'feat/linear/nvm' || compact === 'feat\\linear\\nvm') return TARGETS.linearNvm
    if (compact === 'linear/jit' || compact === 'linear\\jit' || compact === 'feat/linear/jit' || compact === 'feat\\linear\\jit') return TARGETS.linearJit
    if (compact === 'para' || compact === 'feat/para' || compact === 'feat\\para') return TARGETS.para
    if (compact === 'modules') return TARGETS.modules
    if (compact === 'modules/std' || compact === 'modules\\std' || compact === 'feat/modules/std' || compact === 'feat\\modules\\std') return TARGETS.modulesStd
    if (compact === 'modules/nn' || compact === 'modules\\nn' || compact === 'feat/modules/nn' || compact === 'feat\\modules\\nn') return TARGETS.modulesNn
    if (compact === 'perf') return TARGETS.perf

    const dotted = resolvePlanPathFromSegments(compact.split('.').filter(Boolean))
    if (dotted) return dotted
    const dottedFeat = resolvePlanPathFromSegments(['feat', ...compact.split('.').filter(Boolean)])
    if (dottedFeat) return dottedFeat
    const slashed = resolvePlanPathFromSegments(compact.split(/[\\/]+/).filter(Boolean))
    if (slashed) return slashed
    const slashedFeat = resolvePlanPathFromSegments(['feat', ...compact.split(/[\\/]+/).filter(Boolean)])
    if (slashedFeat) return slashedFeat
    return null
}

function resolveTarget(input) {
    const named = resolveNamedTarget(input)
    if (named) return named
    return path.resolve(process.cwd(), input)
}

function parseArgs(argv) {
    const runnerArgs = []
    const targets = []
    let changed = false

    for (let i = 0; i < argv.length; ++i) {
        const arg = argv[i]
        if (arg === '--changed') {
            changed = true
        } else if (
            arg === '--all' ||
            arg === '--smoke' ||
            arg === '--feat' ||
            arg === '--parse' ||
            arg === '--compile' ||
            arg === '--opt' ||
            arg === '--trans' ||
            arg === '--linear' ||
            arg === '--linear:nvm' ||
            arg === '--linear:jit' ||
            arg === '--para' ||
            arg === '--modules' ||
            arg === '--modules:std' ||
            arg === '--modules:nn' ||
            arg === '--perf'
        ) {
            targets.push(resolveTarget(arg.slice(2)))
        } else if (arg === '--suite' && i + 1 < argv.length) {
            targets.push(resolveTarget(argv[++i]))
        } else if (arg.startsWith('-')) {
            runnerArgs.push(arg)
            // `--tier fast` and `--jobs 8` carry their value; without this the value would be
            // read as a plan name.
            if (!arg.includes('=') && VALUE_FLAGS.has(arg) && i + 1 < argv.length) {
                runnerArgs.push(argv[++i])
            }
        } else {
            targets.push(resolveTarget(arg))
        }
    }

    return {
        runnerArgs,
        targets: targets.length > 0 ? targets : (changed ? [] : DEFAULT_TARGETS),
        changed,
    }
}

/** Files changed against `origin/fsh-dev`, committed or not; null when git cannot answer. */
function changedFiles() {
    const run = (args) => spawnSync('git', args, { cwd: REPO_ROOT, encoding: 'utf8', windowsHide: true })
    const diff = run(['diff', '--name-only', CHANGED_BASE])
    if (diff.error || diff.status !== 0) return null
    const untracked = run(['ls-files', '--others', '--exclude-standard'])
    const text = `${diff.stdout || ''}\n${untracked.status === 0 ? untracked.stdout || '' : ''}`
    const files = text
        .split(/\r?\n/)
        .map((line) => line.trim().replace(/\\/g, '/'))
        .filter(Boolean)
    return [...new Set(files)]
}

export function loadSelectionRules() {
    const doc = JSON.parse(fs.readFileSync(SELECTION_PATH, 'utf8'))
    return (doc.rules || []).map((rule) => ({
        paths: (rule.paths || []).map((entry) => String(entry).replace(/\\/g, '/')),
        plans: (rule.plans || []).map((entry) => String(entry)),
        // A rule may name no plans but select the changed plan file itself (`test/plans/`).
        self: rule.self === true,
    }))
}

/**
 * The plans a set of changed files selects. The first rule whose path prefix matches a file
 * names the plans for that file (`plans`), or is the changed plan file itself (`self`); the
 * union over all files is run in plan order by the runner. Null when no rule matched anything
 * (the caller then runs the whole fast layer).
 */
export function plansForChanges(files) {
    const rules = loadSelectionRules()
    const names = new Set()
    const targets = []
    let matched = false
    for (const file of files) {
        const rule = rules.find((candidate) =>
            candidate.paths.some((prefix) => file === prefix || file.startsWith(prefix))
        )
        if (!rule) continue
        matched = true
        for (const plan of rule.plans) names.add(plan)
        if (rule.self && file.endsWith('.plan.toml')) {
            const target = path.join(REPO_ROOT, ...file.split('/'))
            if (fs.existsSync(target)) targets.push(target)
        }
    }
    return matched ? { names: [...names], targets } : null
}

/** A plan named by the selection file, relative to test/plans (a directory or a *.plan.toml). */
function resolveSelectionTarget(name) {
    const base = path.join(PLANS_ROOT, ...name.replace(/\\/g, '/').split('/'))
    for (const candidate of [base, `${base}.plan.toml`]) {
        if (fs.existsSync(candidate)) return candidate
    }
    throw new Error(`plan-selection.json names '${name}', which is not under ${PLANS_ROOT}`)
}

function selectChangedTargets() {
    const files = changedFiles()
    if (files === null) {
        console.log(`[INFO] --changed: cannot read ${CHANGED_BASE}; running the whole fast layer`)
        return []
    }
    if (files.length === 0) {
        console.log(`[INFO] --changed: no change against ${CHANGED_BASE}; running the whole fast layer`)
        return []
    }
    const plans = plansForChanges(files)
    if (!plans || (plans.names.length === 0 && plans.targets.length === 0)) {
        console.log(`[INFO] --changed: no rule matches ${files.length} changed file(s); running the whole fast layer`)
        return []
    }
    const targets = [...plans.names.map(resolveSelectionTarget), ...plans.targets]
    console.log(`[INFO] --changed: ${files.length} changed file(s) select ${targets.length} plan(s)`)
    return targets
}

export function main(argv = process.argv.slice(2)) {
    const options = parseArgs(argv)
    const targets = options.changed && options.targets.length === 0 ? selectChangedTargets() : options.targets
    const proc = spawnSync(process.execPath, [RUNNER, ...options.runnerArgs, ...targets], {
        cwd: REPO_ROOT,
        stdio: 'inherit',
        env: process.env
    })
    if (proc.error) {
        console.error(proc.error.message)
        process.exit(1)
    }
    process.exit(typeof proc.status === 'number' ? proc.status : 1)
}

if (process.argv[1] && path.resolve(process.argv[1]) === __filename) {
    main()
}