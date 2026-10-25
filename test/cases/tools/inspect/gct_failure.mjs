#!/usr/bin/env node
/**
 *  `camel inspect --gct` must not print a construction tree it cannot vouch for. Before the
 * branch printed whatever tree the failed build left behind and exited 0, so an import of a
 * module that does not exist and a declaration file that exists and did not load both looked
 *  like a successful dump of a shorter tree. fixed the same silence for `--ops`; this case
 * holds the class of bug for the construction-tree dump.
 *
 * The declaration module is made in the test's scratch directory beside a copy of the real
 * `math` module's `.cmo`, and found through `CAMEL_STD_LIB` (the script sets the absolute path
 * on the child process). Seven runs:
 *
 *   1. a semantic error that only the graph build finds: exit 0 and the tree, because `--gct`
 *      stops at the construction tree (the tree names the unresolved reference; it is not
 *      partial);
 *   2. an import of a module that does not exist: exit 1, the diagnostic, no tree (this error is
 *      made while the construction tree is built);
 *   3. a declaration module that exists and failed to load: exit 1, the diagnostic located in
 *      the declaration file, no tree (same stage);
 *   4. the same program against the real declaration: exit 0, the tree;
 *   5. a library without `main`: exit 0, the tree (there is no entry requirement to satisfy);
 *   6. a type error that only the graph build would find: exit 0 and the tree, for the reason
 *      in run 1;
 *   7. `--gct --gir` on the semantic error: exit 1, no tree and no `digraph` (the combined dump
 *      compiles the graph too, so it prints nothing it could not vouch for).
 *
 * Usage: node gct_failure.mjs
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
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-gct-'))
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

const file = (name, text) => {
    const target = path.join(SCRATCH, name)
    fs.writeFileSync(target, text)
    return target
}

const semantic = file('semantic.cml', 'func main(): int {\n    let x = undefined_name + 1\n    return x\n}\n')
const missingImport = file('missing_import.cml', 'import no_such_module\n\nfunc main(): int {\n    return 0\n}\n')
const healthy = file('healthy.cml', 'import math\n\nfunc main(): int {\n    return 0\n}\n')
const library = file('library.cml', 'func twice(x: i32): i32 {\n    return x * 2\n}\n')
// The construction tree builds; the call only fails when the graph is built, which `--gct`
// does not do.
const typeError = file(
    'type_error.cml',
    'func f(x: i32): i32 {\n    return x\n}\n\nfunc main(): int {\n    return f(true)\n}\n',
)

function inspect(args, stdlib) {
    const env = { ...process.env, CAMEL_HOME: HOME }
    if (stdlib) {
        env.CAMEL_STD_LIB = stdlib
    } else {
        delete env.CAMEL_STD_LIB
    }
    return spawnSync(CAMEL, ['inspect', ...args], {
        env,
        encoding: 'utf8',
        cwd: SCRATCH,
        timeout: 60000,
    })
}

const lines = (text) => text.split('\n')
// The first line of a construction tree dump is `EXEC`; a declaration inside it is `|-DECL: ...`.
const printsTree = (text) => lines(text).some((line) => line === 'EXEC' || line.startsWith('|-DECL:'))
const printsGraph = (text) => lines(text).some((line) => line.startsWith('digraph '))

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

// 1. A semantic error is found after the construction tree is built: the dump stops at the tree,
//    so it is complete and printed. (The error belongs to the graph build, pinned in run 7.)
const sem = inspect(['--gct', semantic])
check('a semantic error alone does not suppress the --gct dump', sem.status === 0, String(sem.status))
check('its construction tree is printed', printsTree(sem.stdout), sem.stdout)
check('the tree names the unresolved reference', sem.stdout.includes('DREF: undefined_name'), sem.stdout)

// 2. An import that names a module that does not exist.
const missing = inspect(['--gct', missingImport])
check('an import of a missing module makes inspect --gct exit 1', missing.status === 1, String(missing.status))
check(
    'the module-not-found diagnostic is reported',
    missing.stdout.includes('SemanticDiag::ModuleNotFound'),
    missing.stdout || missing.stderr,
)
check('no tree is printed for the missing import', !printsTree(missing.stdout), missing.stdout)

// 3. A declaration module that exists and failed to load.
const badDecl = inspect(['--gct', healthy], STDLIB)
check(
    'a declaration module that fails to load makes inspect --gct exit 1',
    badDecl.status === 1,
    String(badDecl.status),
)
check(
    'the declaration diagnostic is reported',
    badDecl.stdout.includes('SemanticDiag::NativeDeclarationNotLoaded'),
    badDecl.stdout || badDecl.stderr,
)
check('the diagnostic is located in the declaration file', /math\.cml:\d+:\d+/.test(badDecl.stdout), badDecl.stdout)
check('no tree is printed for a declaration that did not load', !printsTree(badDecl.stdout), badDecl.stdout)

// 4. The same program against the real declaration: the same tool, the same env, a healthy module.
fs.writeFileSync(path.join(STDLIB, 'math.cml'), realDeclaration)
const ok = inspect(['--gct', healthy], STDLIB)
check('a healthy program still exits 0', ok.status === 0, `${ok.status}: ${ok.stderr}`)
check('its construction tree is printed', ok.stdout.includes('|-DECL: func main'), ok.stdout)
check('no diagnostic is reported for a healthy program', !/SemanticDiag::/.test(ok.stdout), ok.stdout)

// 5. A library has no `main` to run; `inspect` analyses it.
const lib = inspect(['--gct', library])
check('a library without main still exits 0', lib.status === 0, `${lib.status}: ${lib.stderr}`)
check('its construction tree is printed', lib.stdout.includes('|-DECL: func twice'), lib.stdout)

// 6. A later-stage error does not suppress the tree: `--gct` stops at the construction tree.
const typeErrorRun = inspect(['--gct', typeError])
check(
    'an error only the graph build would find does not suppress the --gct dump',
    typeErrorRun.status === 0,
    `${typeErrorRun.status}: ${typeErrorRun.stdout || typeErrorRun.stderr}`,
)
check('the tree of that program is printed', typeErrorRun.stdout.includes('|-DECL: func main'), typeErrorRun.stdout)

// 7. A combined dump prints nothing it could not vouch for.
const combined = inspect(['--gct', '--gir', semantic])
check('a failed --gct --gir dump exits 1', combined.status === 1, String(combined.status))
check('the diagnostic is reported for the combined dump', combined.stdout.includes('SemanticDiag::UnresolvedReference'), combined.stdout)
check('no construction tree is printed for the combined dump', !printsTree(combined.stdout), combined.stdout)
check('no graph is printed for the combined dump', !printsGraph(combined.stdout), combined.stdout)

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)