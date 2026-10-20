// Fixture of test/tools/selftest/run.mjs: checks the runner's per-test scratch directory
// (`${test_tmp}` / CAMEL_TEST_TMPDIR) and that a file another run keeps under tmp/test is
// outside it and survives.
import fs from 'fs'
import path from 'path'

function fail(message) {
    console.error(message)
    process.exit(1)
}

const [mode, target] = process.argv.slice(2)
if (mode === 'write') {
    if (!target || !fs.existsSync(target)) fail(`scratch directory does not exist: ${target}`)
    fs.writeFileSync(path.join(target, 'model.onnx'), 'a model written by scratch_probe')
    console.log(`wrote ${target}/model.onnx`)
} else if (mode === 'absent') {
    // A file that another test wrote (or an earlier run left) must not be visible here: this is
    // the shape of "a refused export must not write a model".
    if (!target || fs.existsSync(path.join(target, 'model.onnx'))) {
        fail(`another test's model is visible in this test's scratch directory: ${target}`)
    }
    console.log('scratch directory is clean')
} else if (mode === 'outside') {
    // `target` is this test's scratch directory, `other` a file another run keeps under
    // tmp/test: the runner must neither put it in this directory nor remove it.
    const other = process.env.SELFTEST_MARKER
    if (!other || !fs.existsSync(other)) fail(`the other run's file is missing: ${other}`)
    if (path.resolve(other).startsWith(path.resolve(target) + path.sep)) {
        fail(`the other run's file is inside this test's scratch directory: ${other}`)
    }
    // The scratch directory must be under a `run-<pid>` root of the tree the other file is in
    // (tmp/test), not a bare fixed path: that is what makes two runs independent.
    const runRoot = path.resolve(other, '..')
    const relative = path.relative(runRoot, path.resolve(target))
    if (relative.startsWith('..') || !/^run-[^\\/]+[\\/]/.test(relative)) {
        fail(`the scratch directory is not under a run-* root of ${runRoot}: ${target}`)
    }
    console.log("the other run's file is outside this scratch, under this run's root")
} else {
    fail(`unknown mode: ${mode}`)
}