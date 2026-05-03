import path from 'path'
import { spawnSync } from 'child_process'
import { BASEDIR, logStep, logFail } from './common.js'

const TESTS_ROOT = path.join(BASEDIR, 'test')
const RUNNER = path.join(TESTS_ROOT, 'tools', 'run-tests.mjs')
const PLANS_ROOT = path.join(TESTS_ROOT, 'plans')

export const TARGETS = {
    smoke: path.join(PLANS_ROOT, 'smoke', 'core.plan.toml'),
    regression: path.join(PLANS_ROOT, 'regression'),
    functional: path.join(PLANS_ROOT, 'functional'),
    parse: path.join(PLANS_ROOT, 'functional', 'parse'),
    compile: path.join(PLANS_ROOT, 'functional', 'compile'),
    opt: path.join(PLANS_ROOT, 'functional', 'opt'),
    trans: path.join(PLANS_ROOT, 'functional', 'trans'),
    linear: path.join(PLANS_ROOT, 'functional', 'linear'),
    linearNvm: path.join(PLANS_ROOT, 'functional', 'linear', 'nvm.plan.toml'),
    linearJit: path.join(PLANS_ROOT, 'functional', 'linear', 'jit.plan.toml'),
    para: path.join(PLANS_ROOT, 'functional', 'para'),
    modules: path.join(PLANS_ROOT, 'functional', 'modules'),
    modulesStd: path.join(PLANS_ROOT, 'functional', 'modules', 'std.plan.toml'),
    modulesNn: path.join(PLANS_ROOT, 'functional', 'modules', 'nn.plan.toml'),
    perf: path.join(PLANS_ROOT, 'perf')
}

export const MAIN_TARGETS = [
    TARGETS.smoke,
    TARGETS.regression,
    TARGETS.functional
]

export const FULL_TARGETS = [
    ...MAIN_TARGETS,
    TARGETS.perf
]

export function resolveTargetName(name) {
    const compact = String(name).replace(/^[./\\]+/, '')
    if (compact === 'smoke') return TARGETS.smoke
    if (compact === 'regression') return TARGETS.regression
    if (compact === 'functional') return TARGETS.functional
    if (compact === 'parse' || compact === 'functional/parse' || compact === 'functional\\parse') return TARGETS.parse
    if (compact === 'compile' || compact === 'functional/compile' || compact === 'functional\\compile') return TARGETS.compile
    if (compact === 'opt' || compact === 'functional/opt' || compact === 'functional\\opt') return TARGETS.opt
    if (compact === 'trans' || compact === 'functional/trans' || compact === 'functional\\trans') return TARGETS.trans
    if (compact === 'linear' || compact === 'functional/linear' || compact === 'functional\\linear') return TARGETS.linear
    if (compact === 'linear/nvm' || compact === 'linear\\nvm' || compact === 'functional/linear/nvm' || compact === 'functional\\linear\\nvm') return TARGETS.linearNvm
    if (compact === 'linear/jit' || compact === 'linear\\jit' || compact === 'functional/linear/jit' || compact === 'functional\\linear\\jit') return TARGETS.linearJit
    if (compact === 'para' || compact === 'functional/para' || compact === 'functional\\para') return TARGETS.para
    if (compact === 'modules') return TARGETS.modules
    if (compact === 'modules/std' || compact === 'modules\\std' || compact === 'functional/modules/std' || compact === 'functional\\modules\\std') return TARGETS.modulesStd
    if (compact === 'modules/nn' || compact === 'modules\\nn' || compact === 'functional/modules/nn' || compact === 'functional\\modules\\nn') return TARGETS.modulesNn
    if (compact === 'perf') return TARGETS.perf
    return path.resolve(BASEDIR, name)
}

export function runHarness({ label, targets, runnerArgs = [] }) {
    const resolvedTargets = targets.map(resolveTargetName)
    logStep(`${label}: ${resolvedTargets.length} target(s)`)
    const proc = spawnSync(process.execPath, [RUNNER, ...runnerArgs, ...resolvedTargets], {
        cwd: BASEDIR,
        stdio: 'inherit',
        env: process.env
    })
    if (proc.error) {
        logFail(proc.error.message)
        process.exit(1)
    }
    process.exit(typeof proc.status === 'number' ? proc.status : 1)
}
