#!/usr/bin/env node
/**
 * A `.cml` beside a `.cmo` of the same name is that C++ module, not a second module beside it
 * (design principles 6.4; handover):
 *
 *   - a type the C++ module exports (`Json`) is written in the `.cml` without an import;
 *   - a constant it exports (`pi`) is written there without an import;
 *   - a type the `.cml` exports (`Tag`) is a name of the module: an importer reads it as
 *     `json::Tag`, beside the C++ module's `json::Json`;
 *   - a value the `.cml` exports (`tau`) is a name of the module too (`math::tau`);
 *   - a declaration of the `.cml` (`kind_of`, and `decode`, whose kernel the C++ module keeps)
 *     is what an importer's call reaches, and a name the declaration module does not write
 *     (`encode`, a computed signature) still answers from C++.
 *
 * The pair is made in the test's own scratch directory out of the stdlib `json` and `math`
 * modules, because the repository has no C++ module still written twice.
 *
 * Usage: node one_module.mjs
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
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-native-'))
const env = { ...process.env, CAMEL_HOME: HOME }

fs.mkdirSync(SCRATCH, { recursive: true })
for (const name of ['json', 'math']) {
    fs.copyFileSync(path.join(HOME, 'stdlib', `${name}.cmo`), path.join(SCRATCH, `${name}.cmo`))
}
// The declaration module of `json`: a signature over the C++ module's own type, and a type of
// its own, which an importer reads from the same module.
fs.writeFileSync(
    path.join(SCRATCH, 'json.cml'),
    `module json

/// The type the C++ module exports is this module's own type, written without an import.
#op('json:kind')
export func kind_of(value: Json): string

/// A name the C++ module implements, declared here because the declaration module is the
/// module's signature: the C++ resolver is the kernel, not a second set of names.
export func decode(text: string): Json!

/// A type this file adds to the module, beside the C++ module's types.
export type Tag = { level: int, text: string }
`,
)
// The declaration module of `math`: a value over a constant the C++ module exports.
fs.writeFileSync(
    path.join(SCRATCH, 'math.cml'),
    `module math

/// The constant the C++ module exports is this module's own, written without an import.
export let tau: float = pi * 2.0
`,
)
fs.writeFileSync(
    path.join(SCRATCH, 'main.cml'),
    `import json
import math
import { kind_of } from json

func main(): int sync {
    let kind: string = json::decode('123') ?-> kind_of !! 'none'
    println(kind)
    let tag: json::Tag = { level: 7, text: 'ok' }
    println(tag.level, tag.text)
    println(math::tau)
    println(json::encode([1, 2]))
    return 0
}
`,
)

const proc = spawnSync(CAMEL, [path.join(SCRATCH, 'main.cml')], {
    env,
    encoding: 'utf8',
    cwd: SCRATCH,
    timeout: 60000,
})

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

check('the program compiles and runs', proc.status === 0, `${proc.status}\n${proc.stdout}\n${proc.stderr}`)
check(
    'the declaration module uses the C++ type and the C++ constant, and the importer reads both modules as one',
    proc.stdout === 'int\n7 ok\n6.283185307179586\n[1,2]\n',
    JSON.stringify(proc.stdout),
)
check(
    'the C++ module still answers its own names through the declaration module',
    !proc.stderr.includes('error'),
    proc.stderr,
)

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)