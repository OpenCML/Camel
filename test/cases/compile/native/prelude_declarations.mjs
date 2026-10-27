#!/usr/bin/env node
/**
 * The prelude's declaration file (`prelude.cml` of the standard library) is the one
 * source of the built-in signatures, so every program is compiled with it read: an invalid file
 * fails `camel check` of a plain program, and the diagnostics are located in that file. A copy of
 * the standard library in the test's own scratch directory gets a broken prelude and is named by
 * `CAMEL_STD_LIB`; the same program checks clean against the real one.
 *
 * Usage: node prelude_declarations.mjs
 */

import fs from 'fs'
import os from 'os'
import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const HOME = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
const CAMEL = path.join(HOME, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-prelude-'))

fs.mkdirSync(SCRATCH, { recursive: true })
const stdlib = path.join(SCRATCH, 'stdlib')
fs.cpSync(path.join(HOME, 'stdlib'), stdlib, { recursive: true })
const prelude = path.join(stdlib, 'prelude.cml')
fs.appendFileSync(prelude, '\nexport func broken(: int\n')
const program = path.join(SCRATCH, 'plain.cml')
fs.writeFileSync(program, 'func main(): int sync {\n    return 0\n}\n')

const check = (extra) =>
    spawnSync(CAMEL, ['check', program], {
        encoding: 'utf8',
        cwd: SCRATCH,
        env: { ...process.env, CAMEL_HOME: HOME, ...extra },
        timeout: 60000,
    })

let failed = false
const verdict = (label, ok, detail) => {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

const good = check({})
verdict('a plain program checks clean against the real prelude', good.status === 0, `${good.status}\n${good.stdout}${good.stderr}`)

const bad = check({ CAMEL_STD_LIB: stdlib })
const text = `${bad.stdout}${bad.stderr}`
verdict('a broken prelude fails the check of a plain program', bad.status !== 0, `${bad.status}\n${text}`)
verdict(
    'the diagnostic is located in the prelude file',
    text.includes('prelude.cml') && /prelude\.cml:\d+:\d+/.test(text),
    text,
)

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)
