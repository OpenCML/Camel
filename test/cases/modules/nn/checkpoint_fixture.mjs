// A committed checkpoint is loaded by the current compiler and native `ckpt` code, and the bytes
// on disk are the same after the run. The fixture was written before the tree library was
// rewritten; loading it is the compatibility check that a library change cannot perform.
import assert from 'node:assert/strict'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'
import { createHash } from 'node:crypto'

const cases = path.dirname(fileURLToPath(import.meta.url))
const repo = path.resolve(cases, '../../../..')
const runtime = process.env.CAMEL_HOME || path.join(repo, 'out/latest')
const camel = path.join(runtime, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const fixture = path.join(cases, 'fixtures/checkpoint-v1')
const loadCase = path.join(cases, 'checkpoint_fixture_load.cml')

const sha256 = bytes => createHash('sha256').update(bytes).digest('hex')
function snapshotTree(directory) {
    const entries = []
    const walk = (current, prefix) => {
        for (const name of fs.readdirSync(current).sort()) {
            const full = path.join(current, name)
            if (fs.statSync(full).isDirectory()) walk(full, `${prefix}${name}/`)
            else entries.push(`${prefix}${name}:${sha256(fs.readFileSync(full))}`)
        }
    }
    walk(directory, '')
    return entries
}

assert(fs.existsSync(fixture), `the committed checkpoint fixture is missing: ${fixture}`)
const before = snapshotTree(fixture)

const run = spawnSync(camel, [loadCase, 'std::nvm', '--arg', `dir=${fixture}`], {
    cwd: repo,
    encoding: 'utf8',
    env: { ...process.env, CAMEL_HOME: runtime },
})
assert.equal(run.status, 0, `loading the fixture failed:\n${run.stdout}\n${run.stderr}`)

// The checkpoint holds 1.0 and -2.0 as f32, the int64 values 9007199254740993 and -2 (their
// bytes, as `to_bytes` gives them), an empty [2, 0] tensor, a string, a tuple and a bool.
const want = [
    'ee4ac73c2bd27756ab82780f27c73a7bc4d3f0bb6acb37e008bc27eccd7e588b',
    '[1, 0, 0, 0, 0, 0, 32, 0, 254, 255, 255, 255, 255, 255, 255, 255]',
    '[]',
    '中文 checkpoint',
    '(3, 0.5)',
    'true',
].join('\n')
assert.equal(run.stdout.trim(), want, `the loaded values changed:\n${run.stdout}`)

const after = snapshotTree(fixture)
assert.deepEqual(after, before, 'loading a checkpoint modified the files on disk')
console.log('checkpoint fixture: loaded, values and bytes unchanged')