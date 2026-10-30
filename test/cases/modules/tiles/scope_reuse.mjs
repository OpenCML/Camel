// One scope, called once and called twice: the unit of device compilation is the scope's own
// artifact (tiles.compiled_scope, modules/tiles/src/compiled_scope.cpp#prepareProgram), not the
// call. The relation pinned here is that invariant, with no absolute number in it: the second
// call launches the same work again and compiles nothing.
//
// The driver is a node script rather than a Python one so that nothing has to be declared for the
// device-layer census (test/tools/check-cuda-visibility.mjs counts Python tests); the plan entry
// gives it an idle card and the child inherits it.
import { execFileSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { takeCamel } from '../../../tools/camel-arg.mjs'

const here = path.dirname(fileURLToPath(import.meta.url))
const root = path.resolve(here, '../../../..')
function usage(message) {
    if (message) console.error(`scope_reuse: ${message}`)
    console.error('usage: node test/cases/modules/tiles/scope_reuse.mjs [--camel PATH]')
    process.exit(2)
}

const { camel: exe, rest } = takeCamel(process.argv.slice(2), root, usage)
if (rest.length) usage(`unexpected argument ${rest[0]}`)
// The install this case measures also owns the children's `CAMEL_HOME` (test/tools/camel-arg.mjs).
const home = path.dirname(path.dirname(exe))

const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-scope-reuse-'))
function deviceWork(caseName) {
    const report = path.join(scratch, `${caseName}.json`)
    execFileSync(
        exe,
        [
            path.join(here, `${caseName}.cml`),
            `std::profile json=${report}`,
            'tiles::compile',
            'std::nvm',
        ],
        {
            cwd: root,
            env: { ...process.env, CAMEL_HOME: home },
            encoding: 'utf8',
            maxBuffer: 64 * 1024 * 1024,
        },
    )
    const counters = JSON.parse(fs.readFileSync(report, 'utf8')).device_work
    if (!counters) throw new Error(`${caseName}: the report has no device_work`)
    return counters
}

let once
let twice
try {
    once = deviceWork('scope_calls_once')
    twice = deviceWork('scope_calls_twice')
} finally {
    fs.rmSync(scratch, { recursive: true, force: true })
}

if (once.compiles === 0) throw new Error(`the scope did not compile at all: ${JSON.stringify(once)}`)
if (once.kernels === 0) throw new Error(`the scope did not run on a device: ${JSON.stringify(once)}`)
// The second call launches the same work again...
if (twice.kernels !== 2 * once.kernels) {
    throw new Error(
        `two calls do not launch twice the work of one: once ${JSON.stringify(once)}, twice ${JSON.stringify(twice)}`,
    )
}
// ...and compiles nothing: the artifact belongs to the scope, so the count of compilations is the
// count of scopes, not the count of calls.
if (twice.compiles !== once.compiles) {
    throw new Error(
        `the second call compiled again: once ${JSON.stringify(once)}, twice ${JSON.stringify(twice)}`,
    )
}
console.log(
    `scope reuse: once kernels=${once.kernels} compiles=${once.compiles}, twice kernels=${twice.kernels} compiles=${twice.compiles}`,
)