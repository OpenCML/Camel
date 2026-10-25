#!/usr/bin/env node
/**
 * `camel inspect --ops` must not print an operator inventory it cannot vouch for. A declaration
 * module whose `.cml` exists and fails to load leaves its operators out of the listing; before
 *  the tool printed the shorter list and exited 0, which is how the `pyplot`/`python`
 *  registration gap survived until someone counted those seven names by hand.
 *
 * The declaration module is made in the test's scratch directory beside a copy of the real
 * `math` module's `.cmo`, and found through `CAMEL_STD_LIB` (the plan runner's `${test_tmp}` is
 * repository-relative, and `CAMEL_STD_LIB` is resolved against the entry directory, so this
 * script sets the absolute path on the child process). Three runs:
 *
 *   1. the broken declaration, imported: exit 1, the diagnostic and a line naming the module
 *      and the declaration file on stderr, no `OPERATOR` line on stdout;
 *   2. the same broken declaration present but not imported: exit 0, a listing (a module that
 *      is not imported is not an error);
 *   3. the real declaration: exit 0, the module's operators are listed (a healthy declaration
 *      module still works);
 *   4. a module whose signatures are computed in C++ (`env`, no `.cml` beside its `.cmo`): exit
 *      0, its operators are listed with the same override (there is no declaration file to fail).
 *
 * Usage: node declaration_load_failure.mjs
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
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-inspect-'))
const STDLIB = path.join(SCRATCH, 'stdlib')

fs.mkdirSync(STDLIB, { recursive: true })
fs.copyFileSync(path.join(HOME, 'stdlib', 'math.cmo'), path.join(STDLIB, 'math.cmo'))
const realDeclaration = fs.readFileSync(path.join(HOME, 'stdlib', 'math.cml'), 'utf8')
// A declaration that names no operator the module registers: the declaration file exists and
// cannot load. It is appended to the real declarations, so the run is about the load failing,
// not about a module that was never meant to have operators.
const brokenDeclaration = `${realDeclaration.trimEnd()}

// Added by the case: this names an operator the module does not register.
export func no_such_operator(x: i32): i32
`
fs.writeFileSync(path.join(STDLIB, 'math.cml'), brokenDeclaration)

const mainPath = path.join(SCRATCH, 'main.cml')
const noImportPath = path.join(SCRATCH, 'no_import.cml')
fs.writeFileSync(mainPath, 'import math\n\nfunc main(): int {\n    return 0\n}\n')
fs.writeFileSync(noImportPath, 'func main(): int {\n    return 0\n}\n')

function inspectOps(entry) {
    return spawnSync(CAMEL, ['inspect', '--ops', entry], {
        env: { ...process.env, CAMEL_HOME: HOME, CAMEL_STD_LIB: STDLIB },
        encoding: 'utf8',
        cwd: SCRATCH,
        timeout: 60000,
    })
}

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

const operatorLines = (text) => text.split('\n').filter((line) => line.startsWith('OPERATOR '))

// 1. The declaration file exists and failed to load: the listing must not be printed as if it
//    were complete.
const broken = inspectOps(mainPath)
check('a declaration module that fails to load makes inspect --ops exit 1', broken.status === 1, String(broken.status))
check(
    'the diagnostic is on stderr',
    broken.stderr.includes('SemanticDiag::NativeDeclarationNotLoaded') && broken.stderr.includes('code=0x0300017C'),
    broken.stderr || broken.stdout,
)
check('the diagnostic is located in the declaration file', /math\.cml:\d+:\d+/.test(broken.stderr), broken.stderr)
check(
    'a line names the module and the declaration file',
    /camel: the declaration module 'math' failed to load from '.*math\.cml'/.test(broken.stderr),
    broken.stderr,
)
check('no operator listing is printed for a listing that cannot be vouched for', operatorLines(broken.stdout).length === 0, broken.stdout)

// 2. The same broken file is present but the program does not import it: not an error.
const notImported = inspectOps(noImportPath)
check('a module that is not imported is not an error', notImported.status === 0, `${notImported.status}: ${notImported.stderr}`)
check('the listing is still printed when nothing is missing from it', operatorLines(notImported.stdout).length > 0, notImported.stdout)
check('no declaration failure is reported for a module that was not imported', !notImported.stderr.includes('failed to load'), notImported.stderr)

// 3. The real declaration of the same C++ module (which also has computed signatures in C++):
//    the same tool, the same env, a healthy module.
fs.writeFileSync(path.join(STDLIB, 'math.cml'), realDeclaration)
const healthy = inspectOps(mainPath)
check('a healthy declaration module still exits 0', healthy.status === 0, `${healthy.status}: ${healthy.stderr}`)
check(
    'its operators are listed',
    healthy.stdout.includes('"module":"math"') && healthy.stdout.includes('"uri":"math:abs_i"'),
    healthy.stdout.slice(0, 400),
)
check('no failure is reported for a healthy module', !healthy.stderr.includes('failed to load'), healthy.stderr)

// 4. A C++ module whose signatures are computed (`stdlib/signatures/env.cml`, no `.cml` beside
//    its `.cmo`): no declaration file exists, so there is nothing that could fail to load.
const computedPath = path.join(SCRATCH, 'computed.cml')
fs.writeFileSync(computedPath, 'import env\n\nfunc main(): int {\n    return 0\n}\n')
const computed = inspectOps(computedPath)
check(
    'a module whose signatures are computed in C++ still exits 0',
    computed.status === 0,
    `${computed.status}: ${computed.stderr}`,
)
check('its operators are listed', computed.stdout.includes('"module":"env"'), computed.stdout.slice(0, 400))

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)