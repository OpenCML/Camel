#!/usr/bin/env node
/**
 * A core pass exists only as the declaration in `stdlib/std.cml` plus its kernel (design
 * principles 9.3): the declaration is what gives the pass its name, documentation and
 * parameters. This case pins what happens when the standard library cannot be read — the
 * pipeline must refuse the pass and say which module and which file it looked at, never fall
 * back to a kernel running with parameters invented somewhere else.
 *
 * The install's own `stdlib` cannot be hidden from the search path (`out/latest/stdlib` is
 * always a root), so the case takes the earlier root over: `CAMEL_STD_LIB` points at a
 * directory whose `std.cml` is a valid module with no pass declarations.
 *
 * Usage: node pass_declarations_missing.mjs
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
    process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-passes-'))
const STDLIB = path.join(SCRATCH, 'stdlib')

fs.mkdirSync(STDLIB, { recursive: true })
const declaration = path.join(STDLIB, 'std.cml')
fs.writeFileSync(declaration, '// no pass declarations\n')
const program = path.join(SCRATCH, 'program.cml')
fs.writeFileSync(program, 'func main(): int sync {\n    return 0\n}\n')

function camel(args) {
    const env = { ...process.env, CAMEL_HOME: HOME, CAMEL_STD_LIB: STDLIB }
    return spawnSync(CAMEL, args, { env, encoding: 'utf8', cwd: SCRATCH, timeout: 60000 })
}

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

// The program itself is fine: the failure is about the pass declarations, not about the source.
const control = camel(['check', '-O', 'short', program])
check('the program compiles without the pass declarations', control.status === 0, control.stderr)

const run = camel([program, 'std::opt safe_fp=true'])
const output = `${run.stdout}${run.stderr}`
check('a core pass is refused', run.status === 1, String(run.status))
check(
    'the refusal names the module',
    output.includes("module 'std'"),
    output,
)
check(
    'the refusal names the declaration file it read',
    output.includes(declaration) && output.includes("declares no pass 'opt'"),
    output,
)
check(
    'the refusal is not the unknown-pass answer',
    !output.includes('available passes are'),
    output,
)

console.log(`pass declarations missing: ${failed ? 'FAIL' : 'PASS'}`)
process.exit(failed ? 1 : 0)