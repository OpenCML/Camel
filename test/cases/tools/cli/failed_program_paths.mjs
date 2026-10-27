#!/usr/bin/env node
/**
 * The other commands that compile the program must answer the same question `inspect` does
 * did anything fail, and does the exit code say so? This case pins the paths
 * that were already right, so the shared decision in `tools/camel-cli/module_failures.h` cannot
 * drift back into a command that prints a result over a program that did not compile.
 *
 * A `--timeit` run is the sensitive one: it must never report a time for a program that failed
 * to compile. Today it reports `[timeit] i/N ... failed` with the diagnostics, no `elapsed=`
 * line and no summary, and exits 1. Run, check and timeit are exercised on the same five broken
 * inputs:
 *
 *   1. a semantic error;
 *   2. an import of a module that does not exist;
 *   3. an import of a module that exists and does not compile (a dependency of the program);
 *   4. a declaration module that exists and fails to load (through `CAMEL_STD_LIB`);
 *   5. a pass name that does not exist (timeit only).
 *
 * plus a healthy program under `--timeit` as the control: exit 0, an `elapsed=` line, a summary.
 *
 * Usage: node failed_program_paths.mjs
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
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-paths-'))
const STDLIB = path.join(SCRATCH, 'stdlib')

fs.mkdirSync(STDLIB, { recursive: true })
fs.copyFileSync(path.join(HOME, 'stdlib', 'math.cmo'), path.join(STDLIB, 'math.cmo'))
const realDeclaration = fs.readFileSync(path.join(HOME, 'stdlib', 'math.cml'), 'utf8')
const brokenDeclaration = `${realDeclaration.trimEnd()}

// Added by the case: this names an operator the module does not register.
export func no_such_operator(x: i32): i32
`
fs.writeFileSync(path.join(STDLIB, 'math.cml'), brokenDeclaration)

const file = (name, text) => {
    const target = path.join(SCRATCH, name)
    fs.writeFileSync(target, text)
    return target
}

const semantic = file('semantic.cml', 'func main(): int {\n    let x = undefined_name + 1\n    return x\n}\n')
const missingImport = file('missing_import.cml', 'import no_such_module\n\nfunc main(): int {\n    return 0\n}\n')
const brokenModule = file('brokenlib.cml', 'export func broken(p: int): int sync {\n    println(\n    return p\n}\n')
const brokenImport = file('broken_import.cml', 'import brokenlib\n\nfunc main(): int {\n    return 0\n}\n')
const healthy = file('healthy.cml', 'import math\n\nfunc main(): int {\n    return 0\n}\n')

function camel(args, stdlib) {
    const env = { ...process.env, CAMEL_HOME: HOME }
    if (stdlib) {
        env.CAMEL_STD_LIB = stdlib
    } else {
        delete env.CAMEL_STD_LIB
    }
    return spawnSync(CAMEL, args, {
        env,
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

// A failed `--timeit` names the iteration and reports the diagnostics; it must not report a
// number for an iteration that never produced a program.
function checkTimeitFailed(label, result, diagnosticName, passName = 'std::nvm') {
    const named = passName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
    check(`${label}: exit 1`, result.status === 1, String(result.status))
    check(
        `${label}: the failed iteration is named`,
        new RegExp(`\\[timeit\\] 1/1 pass=${named} failed`).test(result.stdout),
        result.stdout || result.stderr,
    )
    check(`${label}: the diagnostic is reported`, result.stdout.includes(diagnosticName), result.stdout)
    check(`${label}: no elapsed time is reported`, !result.stdout.includes('elapsed='), result.stdout)
    check(`${label}: no summary is printed`, !result.stdout.includes('mean'), result.stdout)
}

// 1. A semantic error: run, check and timeit all refuse it.
const semRun = camel(['-E', 'short', semantic])
check('semantic error: run exits 1', semRun.status === 1, String(semRun.status))
check('semantic error: run reports the diagnostic', semRun.stdout.includes('UnresolvedReference'), semRun.stdout)

const semCheck = camel(['check', '-O', 'short', semantic])
check('semantic error: check exits 1', semCheck.status === 1, String(semCheck.status))
check('semantic error: check reports the diagnostic', semCheck.stdout.includes('UnresolvedReference'), semCheck.stdout)

checkTimeitFailed('semantic error: timeit', camel(['--timeit', 'std::nvm/1', semantic]), 'UnresolvedReference')

// 2. An import of a module that does not exist.
const missingRun = camel(['-E', 'short', missingImport])
check('missing import: run exits 1', missingRun.status === 1, String(missingRun.status))
check('missing import: run reports the diagnostic', missingRun.stdout.includes('ModuleNotFound'), missingRun.stdout)

const missingCheck = camel(['check', '-O', 'short', missingImport])
check('missing import: check exits 1', missingCheck.status === 1, String(missingCheck.status))
check('missing import: check reports the diagnostic', missingCheck.stdout.includes('ModuleNotFound'), missingCheck.stdout)

checkTimeitFailed('missing import: timeit', camel(['--timeit', 'std::nvm/1', missingImport]), 'ModuleNotFound')

// 3. An import of a module that exists and does not compile. The import is a dependency of the
// program, so every command that compiles it must fail at the compile step; a pass library that
// does not compile is not this case (it is not a dependency, and pass resolution reports it).
const brokenRun = camel(['-E', 'short', brokenImport])
check('broken import: run exits 1', brokenRun.status === 1, String(brokenRun.status))
check(
    'broken import: run reports the imported module\'s diagnostic',
    brokenRun.stdout.includes('ModuleParseFailed') &&
        brokenRun.stdout.includes('SyntaxDiag::MissingClosingDelimiter'),
    brokenRun.stdout,
)

const brokenCheck = camel(['check', '-O', 'short', brokenImport])
check('broken import: check exits 1', brokenCheck.status === 1, String(brokenCheck.status))
check(
    'broken import: check reports the imported module\'s diagnostic',
    brokenCheck.stdout.includes('ModuleParseFailed'),
    brokenCheck.stdout,
)

// The iteration's own diagnostics stop at its first error, so the imported module's parse
// diagnostic is what it reports (the run and check cases above see the importer's summary too).
checkTimeitFailed(
    'broken import: timeit',
    camel(['--timeit', 'std::nvm/1', brokenImport]),
    'MissingClosingDelimiter',
)

// 4. A declaration module that exists and fails to load.
const badRun = camel(['-E', 'short', healthy], STDLIB)
check('declaration load failure: run exits 1', badRun.status === 1, String(badRun.status))
check(
    'declaration load failure: run reports the diagnostic',
    badRun.stdout.includes('NativeDeclarationNotLoaded'),
    badRun.stdout,
)

const badCheck = camel(['check', '-O', 'short', healthy], STDLIB)
check('declaration load failure: check exits 1', badCheck.status === 1, String(badCheck.status))
check(
    'declaration load failure: check reports the diagnostic',
    badCheck.stdout.includes('NativeDeclarationNotLoaded'),
    badCheck.stdout,
)

checkTimeitFailed(
    'declaration load failure: timeit',
    camel(['--timeit', 'std::nvm/1', healthy], STDLIB),
    'NativeDeclarationNotLoaded',
)

// 5. A pass name that does not exist is an error of the command line, not of the program, and it
// is a failed iteration like any other.
checkTimeitFailed(
    'unknown pass: timeit',
    camel(['--timeit', 'std::no_such_pass/1', healthy]),
    'UnrecognizedGraphPass',
    'std::no_such_pass',
)

// Control: a healthy program under --timeit does report a time and a summary.
fs.writeFileSync(path.join(STDLIB, 'math.cml'), realDeclaration)
const okTimeit = camel(['--timeit', 'std::nvm/1', healthy], STDLIB)
check('healthy timeit: exit 0', okTimeit.status === 0, `${okTimeit.status}: ${okTimeit.stderr}`)
check('healthy timeit: an elapsed time is reported', okTimeit.stdout.includes('elapsed='), okTimeit.stdout)
check('healthy timeit: the summary is printed', okTimeit.stdout.includes('mean'), okTimeit.stdout)

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)