#!/usr/bin/env node
/**
 * A `.cml` beside a `.cmo` of the same name is the declaration module of that C++ module, and it
 *  may hold Camel definitions as well as the declarations of the C++ operators (rule
 *  impl-kernel-site, 2026-10-23: "声明模块可以有函数体，`DeclarationModuleHasBody` 删除"; the
 * pair is one module; design principles 10.4: everything else is defined in Camel). The pair is
 * made in the test's own scratch directory out of the stdlib `bytes` module: the module's own
 * `.cml` gains one definition, which the program uses together with the C++ operators the same
 * file declares.
 *
 * Usage: node declaration_module_body.mjs
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
fs.copyFileSync(path.join(HOME, 'stdlib', 'bytes.cmo'), path.join(SCRATCH, 'bytes.cmo'))
// The module's own declarations, with a Camel definition beside them: one module is the two
// files, and what the module adds in Camel belongs in this one.
const declarations = fs.readFileSync(path.join(REPO_ROOT, 'stdlib', 'bytes.cml'), 'utf8')
fs.writeFileSync(
    path.join(SCRATCH, 'bytes.cml'),
    declarations +
        `
/// Twice the number of bytes in the binary value, written in Camel over the operator this same
/// file declares, size, which the C++ module implements.
export func twice(content: Bytes): int {
    return size(content) + size(content)
}
`,
)
fs.writeFileSync(
    path.join(SCRATCH, 'main.cml'),
    `import bytes

func main(): int sync {
    let text = bytes::encode('abc')
    bytes::twice(text)->println
    return 0
}
`,
)

const proc = spawnSync(CAMEL, ['--output-format', 'json', path.join(SCRATCH, 'main.cml')], {
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

// No diagnostic means the compiler prints nothing but what the program prints: a run that
// reports anything at all answers with the diagnostics array instead.
const out = proc.stdout.trim()
check('exit code is 0', proc.status === 0, `${proc.status}: ${proc.stdout} ${proc.stderr}`)
check('the declaration module is used with no diagnostic', !out.startsWith('['), out)
check(
    'the definition runs, over the operator the same file declares',
    out === '6',
    out,
)

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)