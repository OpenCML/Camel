// Self-test of the plan runner's back-end matrix (`backends`, `backends_agree`, `known_fail`,
// the `[defaults]` table): runs the runner on matrix.plan.toml, whose tests pass and fail on
// purpose, and checks the status of each. It also runs the scratch fixture twice: a test must not
// see another test's (or an earlier run's) files in its `${test_tmp}` directory.
import fs from 'fs'
import path from 'path'
import { spawnSync } from 'child_process'
import { fileURLToPath } from 'url'

const here = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(here, '..', '..', '..')
const runner = path.join(path.dirname(here), 'run-tests.mjs')
const proc = spawnSync(process.execPath, [runner, path.join(here, 'matrix.plan.toml')], {
    encoding: 'utf8',
})
const output = `${proc.stdout}${proc.stderr}`.replace(/\x1b\[[0-9;]*m/g, '')

const backends = ['nvm', 'fvm', 'jit', 'prl']
const expected = { plain: 'PASS', starts_with: 'PASS', starts_with_fails: 'FAIL' }
const expectAll = (name, status, exceptions = {}) => {
    for (const backend of backends) {
        expected[`${name}.${backend}`] = exceptions[backend] || status
    }
}
expectAll('agree', 'PASS')
expectAll('stdout_differs', 'PASS', { jit: 'FAIL' })
expectAll('stdout_differs_known', 'PASS', { jit: 'XFAIL' })
expectAll('known_but_passes', 'PASS', { prl: 'FAIL' })
expectAll('column_differs', 'PASS', { fvm: 'FAIL' })
expectAll('exit_differs', 'PASS', { prl: 'FAIL' })
expectAll('fields_not_listed', 'PASS')

const problems = []
const actual = {}
for (const [, status, name] of output.matchAll(/^\[(PASS|FAIL|XFAIL|SKIP)\] (\S+)/gm)) {
    actual[name] = status
}
for (const [name, status] of Object.entries(expected)) {
    if (actual[name] !== status) problems.push(`${name}: expected ${status}, got ${actual[name]}`)
}
for (const name of Object.keys(actual)) {
    if (!(name in expected)) problems.push(`unexpected test ${name}`)
}
const mustContain = [
    'differs from stdout_differs.nvm: stdout_normalized',
    'differs from column_differs.nvm: diagnostics',
    'differs from exit_differs.nvm: exit_code',
    "marked known_fail on 'prl' but passes: remove the mark",
    "the fixture's jit prints 41",
    'stdout does not start with "2\\n"',
    '[SUMMARY] total: 31, pass: 25, fail: 5, known failures: 1',
]
for (const text of mustContain) {
    if (!output.includes(text)) problems.push(`output lacks: ${text}`)
}
// Failures fail the run; a known failure alone would not.
if (proc.status !== 1) problems.push(`runner exit ${proc.status}, expected 1`)

// --- the per-test scratch directory -----------------------------------------------------------
// A file a previous run could have left at a fixed path under tmp/test must be cleared before the
// first test; a model written by one test must not be visible to another; and a second run must
// inherit nothing from the first. The fixture plan is run twice for exactly that last point.
const scratchPlan = path.join(here, 'scratch.plan.toml')
const staleFile = path.join(repoRoot, 'tmp', 'test', 'stale_from_previous_run.txt')
for (const attempt of [1, 2]) {
    fs.mkdirSync(path.dirname(staleFile), { recursive: true })
    fs.writeFileSync(staleFile, 'a model a previous run left behind')
    const scratchProc = spawnSync(process.execPath, [runner, scratchPlan], { encoding: 'utf8' })
    const scratchOutput = `${scratchProc.stdout}${scratchProc.stderr}`.replace(/\x1b\[[0-9;]*m/g, '')
    const where = `scratch run ${attempt}`
    if (scratchProc.status !== 0) problems.push(`${where}: runner exit ${scratchProc.status}, expected 0`)
    for (const name of ['scratch.write', 'scratch.absent', 'scratch.legacy_path_cleared']) {
        if (!scratchOutput.includes(`[PASS] ${name}`)) problems.push(`${where}: ${name} did not pass`)
    }
    if (!scratchOutput.includes('[SUMMARY] total: 3, pass: 3, fail: 0')) {
        problems.push(`${where}: unexpected summary (${scratchOutput.match(/\[SUMMARY\].*/)?.[0] || 'none'})`)
    }
    if (fs.existsSync(staleFile)) problems.push(`${where}: the planted stale file was not cleared`)
    if (fs.existsSync(path.join(repoRoot, 'tmp', 'test'))) {
        problems.push(`${where}: tmp/test was left behind`)
    }
}

if (problems.length > 0) {
    console.error(problems.join('\n'))
    console.error('--- runner output ---')
    console.error(output)
    process.exit(1)
}
console.log('runner self-test ok')
