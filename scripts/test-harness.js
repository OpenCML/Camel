import path from 'path'
import { spawnSync } from 'child_process'
import { BASEDIR, logStep, logFail } from './common.js'

const TESTS_ROOT = path.join(BASEDIR, 'test')
const RUNNER = path.join(TESTS_ROOT, 'tools', 'run-tests.mjs')
const PLANS_ROOT = path.join(TESTS_ROOT, 'plans')

export const TARGETS = {
    smoke: path.join(PLANS_ROOT, 'smoke', 'core.plan.toml'),
    regression: path.join(PLANS_ROOT, 'regression', 'language.plan.toml'),
    errors: path.join(PLANS_ROOT, 'errors.plan.toml'),
    modules: path.join(PLANS_ROOT, 'modules.plan.toml'),
    runLn: path.join(PLANS_ROOT, 'run', 'ln.plan.toml'),
    runJit: path.join(PLANS_ROOT, 'run', 'jit.plan.toml'),
    runNn: path.join(PLANS_ROOT, 'run', 'nn.plan.toml'),
    runPara: path.join(PLANS_ROOT, 'run', 'para.plan.toml'),
    macro: path.join(PLANS_ROOT, 'macro.plan.toml'),
    passes: path.join(PLANS_ROOT, 'passes', 'inline.plan.toml'),
    perf: path.join(PLANS_ROOT, 'perf', 'fib.plan.toml'),
    trans: path.join(PLANS_ROOT, 'trans', 'cpp.plan.toml')
}

export const MAIN_TARGETS = [
    TARGETS.smoke,
    TARGETS.regression,
    TARGETS.errors,
    TARGETS.modules,
    TARGETS.runLn,
    TARGETS.runJit,
    TARGETS.runNn,
    TARGETS.macro,
    TARGETS.trans
]

export const FULL_TARGETS = [
    ...MAIN_TARGETS,
    TARGETS.passes,
    TARGETS.perf,
    TARGETS.runPara
]

export const RUN_TARGETS = [
    TARGETS.runLn,
    TARGETS.runJit,
    TARGETS.runNn,
    TARGETS.runPara
]

export function resolveTargetName(name) {
    const compact = String(name).replace(/^[./\\]+/, '')
    if (compact === 'smoke') return TARGETS.smoke
    if (compact === 'regression') return TARGETS.regression
    if (compact === 'errors') return TARGETS.errors
    if (compact === 'modules') return TARGETS.modules
    if (compact === 'run/ln' || compact === 'run\\ln') return TARGETS.runLn
    if (compact === 'run/jit' || compact === 'run\\jit') return TARGETS.runJit
    if (compact === 'run/nn' || compact === 'run\\nn') return TARGETS.runNn
    if (compact === 'run/para' || compact === 'run\\para') return TARGETS.runPara
    if (compact === 'macro') return TARGETS.macro
    if (compact === 'passes' || compact === 'passes/inline' || compact === 'passes\\inline') return TARGETS.passes
    if (compact === 'perf') return TARGETS.perf
    if (compact === 'trans' || compact === 'trans/cpp' || compact === 'trans\\cpp') return TARGETS.trans
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
