#!/usr/bin/env node
// Precondition of the tests that need CUDA device N to be there
// (`skip_unless = ["node", "test/tools/have-cuda-device.mjs", "0"]`).
//
// The probe asks the question through the surface that existed before `--devices`: a program
// that prints `tiles:available()` and runs with no options, so a broken `--devices` cannot make
// the test skip instead of fail, and the skip's reason can name the device that was wanted.
// Exit 0 where the device is there, 1 with the reason elsewhere.

import os from 'node:os'
import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const ordinal = process.argv[2] || '0'
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')

const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-device-'))
const source = path.join(dir, 'probe.cml')
fs.writeFileSync(
    source,
    'module probe_device\nimport { available } from tiles\nfunc main(): int sync {\n    println(available())\n    return 0\n}\n',
)
const proc = spawnSync(camel, [source, 'std::nvm'], {
    cwd: ROOT,
    env: { ...process.env, CAMEL_HOME: home },
    encoding: 'utf8',
    timeout: 120000,
})
fs.rmSync(dir, { recursive: true, force: true })
if (proc.error) {
    console.log(`cannot run ${camel}: ${proc.error.message}`)
    process.exit(1)
}
if (proc.status !== 0) {
    const output = `${proc.stdout || ''}${proc.stderr || ''}`.trim().split('\n')[0]
    console.log(`tiles:available() exited ${proc.status}: ${output}`)
    process.exit(1)
}
if ((proc.stdout || '').trim() !== 'true') {
    console.log(`CUDA device ${ordinal} is not available (tiles:available() is false)`)
    process.exit(1)
}
process.exit(0)