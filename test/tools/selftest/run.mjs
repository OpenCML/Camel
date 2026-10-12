// Self-test of the plan runner's back-end matrix (`backends`, `backends_agree`, `known_fail`,
// the `[defaults]` table): runs the runner on matrix.plan.toml, whose tests pass and fail on
// purpose, and checks the status of each.
import path from 'path'
import { spawnSync } from 'child_process'
import { fileURLToPath } from 'url'

const here = path.dirname(fileURLToPath(import.meta.url))
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

if (problems.length > 0) {
    console.error(problems.join('\n'))
    console.error('--- runner output ---')
    console.error(output)
    process.exit(1)
}
console.log('runner self-test ok')
