import { MAIN_TARGETS, RUN_TARGETS, TARGETS, runHarness } from './test-harness.js'

const args = process.argv.slice(2)
const runnerArgs = []
const targets = []

for (const arg of args) {
    if (arg === '--smoke') targets.push(TARGETS.smoke)
    else if (arg === '--errors') targets.push(TARGETS.errors)
    else if (arg === '--modules') targets.push(TARGETS.modules)
    else if (arg === '--macro') targets.push(TARGETS.macro)
    else if (arg === '--passes') targets.push(TARGETS.passes)
    else if (arg === '--perf') targets.push(TARGETS.perf)
    else if (arg === '--trans') targets.push(TARGETS.trans)
    else if (arg === '--regression') targets.push(TARGETS.regression)
    else if (arg === '--run') targets.push(...RUN_TARGETS)
    else if (arg.startsWith('-')) runnerArgs.push(arg)
    else targets.push(arg)
}

runHarness({
    label: 'test',
    targets: targets.length > 0 ? targets : MAIN_TARGETS,
    runnerArgs
})
