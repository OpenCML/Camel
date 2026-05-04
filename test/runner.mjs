import path from 'path'
import fs from 'fs'
import { fileURLToPath } from 'url'
import { spawnSync } from 'child_process'

const __filename = fileURLToPath(import.meta.url)
const TEST_ROOT = path.dirname(__filename)
const REPO_ROOT = path.dirname(TEST_ROOT)
const PLANS_ROOT = path.join(TEST_ROOT, 'plans')
const RUNNER = path.join(TEST_ROOT, 'tools', 'run-tests.mjs')

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

    for (let i = 0; i < argv.length; ++i) {
        const arg = argv[i]
        if (
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
            if (arg === '--tier' && i + 1 < argv.length) {
                runnerArgs.push(argv[++i])
            }
        } else {
            targets.push(resolveTarget(arg))
        }
    }

    return {
        runnerArgs,
        targets: targets.length > 0 ? targets : DEFAULT_TARGETS
    }
}

export function main(argv = process.argv.slice(2)) {
    const options = parseArgs(argv)
    const proc = spawnSync(process.execPath, [RUNNER, ...options.runnerArgs, ...options.targets], {
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
