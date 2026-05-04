import { MAIN_TARGETS, TARGETS, runHarness } from './test-harness.js'

const args = process.argv.slice(2)
const runnerArgs = []
const targets = []

for (const arg of args) {
    if (arg === '--smoke') targets.push(TARGETS.smoke)
    else if (arg === '--feat') targets.push(TARGETS.feat)
    else if (arg === '--parse') targets.push(TARGETS.parse)
    else if (arg === '--compile') targets.push(TARGETS.compile)
    else if (arg === '--opt') targets.push(TARGETS.opt)
    else if (arg === '--trans') targets.push(TARGETS.trans)
    else if (arg === '--linear') targets.push(TARGETS.linear)
    else if (arg === '--linear:nvm') targets.push(TARGETS.linearNvm)
    else if (arg === '--linear:jit') targets.push(TARGETS.linearJit)
    else if (arg === '--para') targets.push(TARGETS.para)
    else if (arg === '--modules') targets.push(TARGETS.modules)
    else if (arg === '--modules:std') targets.push(TARGETS.modulesStd)
    else if (arg === '--modules:nn') targets.push(TARGETS.modulesNn)
    else if (arg === '--perf') targets.push(TARGETS.perf)
    else if (arg.startsWith('-')) runnerArgs.push(arg)
    else targets.push(arg)
}

runHarness({
    label: 'test',
    targets: targets.length > 0 ? targets : MAIN_TARGETS,
    runnerArgs
})
