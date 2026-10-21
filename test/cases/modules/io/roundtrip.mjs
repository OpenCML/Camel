// The read side of `io` needs input a plain plan run cannot give it: this verifier writes two
// characters into the child's stdin (a pipe, so it is not a terminal) and checks that the program
// read them through `io:get_char`/`io:get_chars` and wrote them back. It also turns raw mode off,
// which is the writer side of the module and succeeds when raw mode was never entered.
import assert from 'node:assert/strict'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'

const cases = path.dirname(fileURLToPath(import.meta.url))
const repo = path.resolve(cases, '../../../../..')
const runtime = process.env.CAMEL_HOME || path.join(repo, 'out/latest')
const camel = path.join(runtime, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const casePath = path.join(cases, 'roundtrip.cml')

const result = spawnSync(camel, [casePath, 'std::nvm'], {
    input: 'ab',
    encoding: 'utf8',
    timeout: 20000,
    env: { ...process.env },
})
assert.ifError(result.error)
assert.equal(result.status, 0, result.stdout + result.stderr)
// `has_input` sees the two pending characters, `get_char` reads 'a', `get_chars(4)` reads 'b'.
assert.equal(result.stdout, 'true\nab\n')
assert.equal(result.stderr, '')
console.log('io terminal read/write round trip passed')