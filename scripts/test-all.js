import { FULL_TARGETS, TARGETS, runHarness, resolveTargetName } from './test-harness.js'

function parseArgs(argv) {
    const options = {
        runnerArgs: [],
        customTargets: [],
        selectedTargets: new Set(),
        suites: [],
        feat: false,
        all: false
    }

    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '--all') {
            options.all = true
        } else if (arg === '--feat') {
            options.feat = true
        } else if (arg === '--modules') {
            options.selectedTargets.add(TARGETS.modules)
        } else if (arg === '--modules:std') {
            options.selectedTargets.add(TARGETS.modulesStd)
        } else if (arg === '--modules:nn') {
            options.selectedTargets.add(TARGETS.modulesNn)
        } else if (arg === '--parse') {
            options.selectedTargets.add(TARGETS.parse)
        } else if (arg === '--compile') {
            options.selectedTargets.add(TARGETS.compile)
        } else if (arg === '--opt') {
            options.selectedTargets.add(TARGETS.opt)
        } else if (arg === '--trans') {
            options.selectedTargets.add(TARGETS.trans)
        } else if (arg === '--linear') {
            options.selectedTargets.add(TARGETS.linear)
        } else if (arg === '--linear:nvm') {
            options.selectedTargets.add(TARGETS.linearNvm)
        } else if (arg === '--linear:jit') {
            options.selectedTargets.add(TARGETS.linearJit)
        } else if (arg === '--para') {
            options.selectedTargets.add(TARGETS.para)
        } else if (arg === '--suite' && i + 1 < argv.length) {
            options.suites.push(argv[++i])
        } else if (arg === '--smoke') {
            options.selectedTargets.add(TARGETS.smoke)
        } else if (arg === '--perf') {
            options.selectedTargets.add(TARGETS.perf)
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
            selected.add(resolveTargetName(suite))
        }
    }
    if (options.feat) {
        selected.add(TARGETS.feat)
    }
    targets = selected.size > 0 ? [...selected] : FULL_TARGETS
}

runHarness({
    label: 'test:all',
    targets,
    runnerArgs: options.runnerArgs
})
