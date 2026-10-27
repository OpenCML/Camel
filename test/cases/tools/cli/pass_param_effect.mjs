#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A parameter written as a `key=value` word after the pass name is the pass's `with` parameter,
 * and it reaches the pass: `std::fvm stats=<file>` writes the bytecode statistics of the run to
 * that file, and the same pipeline without the word writes no file (design principles 9.3).
 *
 * An effect, not a parse: the test would pass if the word were accepted and dropped. It reads
 * the file back, so the parameter's value has to arrive at the pass.
 *
 * Usage: node pass_param_effect.mjs
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const HOME = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
const CAMEL = path.join(HOME, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const SCRATCH =
    process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-pass-param-'))

fs.mkdirSync(SCRATCH, { recursive: true })
const program = path.join(SCRATCH, 'effect.cml')
fs.writeFileSync(
    program,
    'func main(): int sync {\n    println("camel-ran-marker")\n    return 0\n}\n'
)

const camel = (args) =>
    spawnSync(CAMEL, args, {
        env: { ...process.env, CAMEL_HOME: HOME },
        encoding: 'utf8',
        cwd: SCRATCH,
        timeout: 60000,
    })

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

// A value with a backslash would be an escape inside the Camel string literal; both platforms
// accept `/` in a path.
const written = path.join(SCRATCH, 'written.json').replaceAll('\\', '/')
const withParameter = camel([program, `std::fvm stats="${written}"`])
check('the new form runs the pass', withParameter.status === 0, withParameter.stderr)
check(
    'the program ran',
    withParameter.stdout.includes('camel-ran-marker'),
    withParameter.stdout
)
check('the parameter value reached the pass: the stats file exists', fs.existsSync(written), written)
if (fs.existsSync(written)) {
    const stats = fs.readFileSync(written, 'utf8')
    check('the stats file is the pass\'s JSON', stats.includes('"graphs"') && stats.includes('main'), stats)
}

const before = new Set(fs.readdirSync(SCRATCH))
const withoutParameter = camel([program, 'std::fvm'])
check('the pass runs without the parameter', withoutParameter.status === 0, withoutParameter.stderr)
const appeared = fs.readdirSync(SCRATCH).filter((name) => !before.has(name))
check(
    'no parameter, no stats file: the default is used',
    appeared.length === 0,
    appeared.join(', ')
)

console.log(`pass parameter effect: ${failed ? 'FAIL' : 'PASS'}`)
process.exit(failed ? 1 : 0)