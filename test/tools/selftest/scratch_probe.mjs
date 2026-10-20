// Fixture of test/tools/selftest/run.mjs: checks the runner's per-test scratch directory
// (`${test_tmp}` / CAMEL_TEST_TMPDIR) and the cleanup of a fixed path under tmp/test.
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
} else if (mode === 'absent-path') {
    if (fs.existsSync(target)) fail(`a stale file survived the run cleanup: ${target}`)
    console.log('stale file is gone')
} else {
    fail(`unknown mode: ${mode}`)
}