// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// `--update-golden` rewrites the snapshot of the entries the run starts, and `--entry` decides which
// those are. A plan of two snapshot entries whose goldens hold stale text:
//   * updated with one entry selected, the selected golden is rewritten and the other keeps its bytes;
//   * updated with no selection, both are rewritten (the whole-plan meaning of `--update-golden`);
//   * an entry pattern that names nothing starts nothing and changes nothing.
// The fixture lives in `test/results/` (gitignored) and runs a node one-liner; `--camel` names an empty
// stand-in file because the runner only asks that an install exists, and nothing here starts it.
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(HERE, '..', '..')
const WORK = path.join(ROOT, 'test', 'results', 'golden-scope-selftest')
let checks = 0
let failed = 0

function check(name, ok, detail) {
    checks++
    if (ok) {
        console.log(`ok    ${name}`)
    } else {
        failed++
        console.log(`FAIL  ${name}${detail ? `: ${detail}` : ''}`)
    }
}

const STALE = 'stale\n'
const goldens = { a: path.join(WORK, 'golden', 'a.txt'), b: path.join(WORK, 'golden', 'b.txt') }
const planPath = path.join(WORK, 'inner.plan.toml')
const stubCamel = path.join(WORK, 'install', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')

function entry(name, printed) {
    return [
        '[[tests]]',
        `name = "${name}"`,
        'kind = "snapshot"',
        'tool = "node"',
        `args = ["-e", "console.log('${printed}')"]`,
        `golden = "golden/${name}.txt"`,
        'timeout_ms = 30000',
        '',
    ]
}

function reset() {
    fs.rmSync(WORK, { recursive: true, force: true })
    fs.mkdirSync(path.dirname(goldens.a), { recursive: true })
    fs.writeFileSync(planPath, ['id = "self.golden_scope"', '', ...entry('a', 'fresh a'), ...entry('b', 'fresh b')].join('\n'))
    for (const file of Object.values(goldens)) fs.writeFileSync(file, STALE)
    fs.mkdirSync(path.dirname(stubCamel), { recursive: true })
    fs.writeFileSync(stubCamel, '')
}

function update(...extra) {
    const run = spawnSync(
        process.execPath,
        [path.join('test', 'tools', 'run-tests.mjs'), '--update-golden', planPath, '--jobs', '1', '--camel', stubCamel, ...extra],
        { cwd: ROOT, encoding: 'utf8', timeout: 180000 },
    )
    return { status: run.status, out: `${run.stdout || ''}${run.stderr || ''}`.replace(/\x1b\[[0-9;]*m/g, '') }
}
const text = (file) => fs.readFileSync(file, 'utf8')

reset()
let run = update('--entry', 'a')
check('one entry selected: its golden is rewritten', text(goldens.a) === 'fresh a\n', run.out.slice(-600))
check('one entry selected: the other golden keeps its bytes', text(goldens.b) === STALE, `now ${JSON.stringify(text(goldens.b))}`)

reset()
run = update('--entry', 'self.golden_scope/b')
check('plan-qualified name selects the same way', text(goldens.b) === 'fresh b\n' && text(goldens.a) === STALE)

reset()
run = update('--entry', 'a', '--entry', 'b')
check('two --entry words select both', text(goldens.a) === 'fresh a\n' && text(goldens.b) === 'fresh b\n')

reset()
run = update('--entry', '*')
check('a wildcard selects every entry', text(goldens.a) === 'fresh a\n' && text(goldens.b) === 'fresh b\n')

reset()
run = update()
check('no selection: both goldens are rewritten', text(goldens.a) === 'fresh a\n' && text(goldens.b) === 'fresh b\n', run.out.slice(-600))

reset()
run = update('--entry', 'nothing_like_this')
check('an entry that names nothing is a refusal that changes nothing',
    run.status === 2 && text(goldens.a) === STALE && text(goldens.b) === STALE, `exit ${run.status}`)

fs.rmSync(WORK, { recursive: true, force: true })
console.log(`golden-scope: ${checks} checks, ${failed} failed`)
process.exit(failed === 0 ? 0 : 1)
