import { FULL_TARGETS, RUN_TARGETS, TARGETS, runHarness, resolveTargetName } from './test-harness.js'

function parseArgs(argv) {
    const options = {
        runnerArgs: [],
        customTargets: [],
        selectedTargets: new Set(),
        suites: [],
        run: false,
        all: false
    }

    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '--all') {
            options.all = true
        } else if (arg === '--modules') {
            options.selectedTargets.add(TARGETS.modules)
        } else if (arg === '--run') {
            options.run = true
        } else if (arg === '--suite' && i + 1 < argv.length) {
            options.suites.push(argv[++i])
        } else if (arg === '--smoke') {
            options.selectedTargets.add(TARGETS.smoke)
        } else if (arg === '--errors') {
            options.selectedTargets.add(TARGETS.errors)
        } else if (arg === '--macro') {
            options.selectedTargets.add(TARGETS.macro)
        } else if (arg === '--passes') {
            options.selectedTargets.add(TARGETS.passes)
        } else if (arg === '--perf') {
            options.selectedTargets.add(TARGETS.perf)
        } else if (arg === '--regression') {
            options.selectedTargets.add(TARGETS.regression)
        } else if (arg === '--trans') {
            options.selectedTargets.add(TARGETS.trans)
        } else if (arg.startsWith('-')) {
            options.runnerArgs.push(arg)
        } else {
            options.customTargets.push(arg)
        }
    }

    return options
}

const argv = process.argv.slice(2)
const options = parseArgs(argv)
let targets

if (options.customTargets.length > 0) {
    targets = options.customTargets
} else if (options.all) {
    targets = FULL_TARGETS
} else {
    const selected = new Set(options.selectedTargets)
    if (options.suites.length > 0) {
        for (const suite of options.suites) {
            selected.add(resolveTargetName(`run/${suite}`))
        }
    } else if (options.run) {
        for (const target of RUN_TARGETS) {
            selected.add(target)
        }
    }
    targets = selected.size > 0 ? [...selected] : FULL_TARGETS
}

runHarness({
    label: 'test:all',
    targets,
    runnerArgs: options.runnerArgs
})
