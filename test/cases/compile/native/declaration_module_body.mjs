#!/usr/bin/env node
/**
 * A `.cml` beside a `.cmo` of the same name is the declaration module of that C++ module: its
 * top-level functions are declarations, so a body written there is reported at the function
 * instead of being dropped. The pair is made in the test's own scratch directory out of the
 * stdlib `bytes` module, because the repository has no C++ module still written twice.
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
// A declaration module with a body in it: the one mistake the rule forbids.
fs.writeFileSync(
    path.join(SCRATCH, 'bytes.cml'),
    `module bytes

export func encode(text: string): Bytes

export func size(content: Bytes): int {
    return 0
}
`,
)
fs.writeFileSync(
    path.join(SCRATCH, 'main.cml'),
    `import bytes

func main(): int {
    return bytes::size(bytes::encode('abc'))
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

let diagnostics = []
try {
    diagnostics = JSON.parse(proc.stdout)
} catch {
    check('the run prints a diagnostics array', false, `${proc.stdout} ${proc.stderr}`)
}

const found = diagnostics.find((d) => d.data && d.data.name === 'SemanticDiag::DeclarationModuleHasBody')
check('the body in the declaration module is a diagnostic', Boolean(found), JSON.stringify(diagnostics.map((d) => d.data && d.data.name)))
if (found) {
    check('exit code is 1', proc.status === 1, String(proc.status))
    check('code is the catalog code', found.code === '0x03000177', String(found.code))
    check(
        'the diagnostic is located in the declaration module',
        path.basename(found.data.modulePath || '') === 'bytes.cml',
        String(found.data.modulePath),
    )
    check('the message names the module and the function', found.message.includes("'bytes'") && found.message.includes('size'), found.message)
    check('the diagnostic is located at the signature of the function', found.range && found.range.start.line === 4, JSON.stringify(found.range))
}

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)