// Copyright (c) 2026 The Camel Authors. All rights reserved.
// Use of this source code is governed by the Apache License 2.0.
//
// A case that crashes is not a green.
//
// The runner judged an exit code only when the plan wrote `expect_exit`, so an entry that forgot the
// field asked nothing about a crash: a case calling `process.exit(1)` was reported `[PASS]` and the
// run exited 0. This pins the default instead -- an entry that says nothing expects the ordinary end
// of a process -- and pins the two plans that need the opposite, so the fix cannot be "make every
// entry red".
//
// The plans are written into `test/results/` (gitignored): a plan whose cases are red on purpose
// must not be part of the tree a gate reads.
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(HERE, '..', '..')
const WORK = path.join(ROOT, 'test', 'results', 'tool-exit-selftest')
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

// The case file is the whole program, so the plan cannot differ from it in some other way: this test
// is about what the runner does with an exit code and nothing else.
function writePlan(name, entries) {
    const dir = path.join(WORK, name)
    fs.mkdirSync(dir, { recursive: true })
    const body = entries
        .map((e) => {
            if (e.case !== null) {
                fs.writeFileSync(path.join(dir, `${e.name}.mjs`), `process.exit(${e.code})\n`)
            }
            const exit = e.expect === undefined ? '' : `expect_exit = ${e.expect}\n`
            return (
                `\n[[tests]]\nname = "${e.name}"\nkind = "behavior"\ntool = "node"\n` +
                `case = "${e.case === null ? 'does_not_exist.mjs' : `${e.name}.mjs`}"\n` +
                `args = ["\${case}"]\n${exit}timeout_ms = 30000\n`
            )
        })
        .join('')
    fs.writeFileSync(path.join(dir, 'inner.plan.toml'), `id = "self.tool_exit.${name}"\n${body}`)
    return path.join(dir, 'inner.plan.toml')
}

function run(planPath) {
    const result = spawnSync(
        process.execPath,
        [path.join('test', 'tools', 'run-tests.mjs'), planPath, '--jobs', '1'],
        { cwd: ROOT, encoding: 'utf8', timeout: 180000 },
    )
    // The runner colours its lines even into a pipe, so every reading below is taken from the text
    // with the escape sequences removed: a red that is present but wrapped is still a red.
    const out = `${result.stdout || ''}${result.stderr || ''}`.replace(/\x1b\[[0-9;]*m/g, '')
    return { code: result.status, out }
}

// 1. A crash with no `expect_exit` is red, and the red says why. Before the fix this was the one
// shape that reported a green: measured on `probe_fail.mjs` calling `process.exit(1)`.
{
    const plan = writePlan('crash', [{ name: 'exits_one', code: 1 }])
    const { code, out } = run(plan)
    check('a case that exits 1 with no expectation is red', /\[FAIL\]\s+exits_one/.test(out), out.slice(-900))
    check(
        'and the red names the exit code it did not expect',
        /entry says nothing about the exit code/.test(out) && /exited [^,]*1/.test(out),
        out.slice(-900),
    )
    check('so the run is red', code === 1, `exit ${code}`)
}

// 2. The ordinary end of a process is still green, and an entry that *names* a failure still passes
// when the process fails that way. Without this pair "make it red" would be the only readable fix.
{
    const plan = writePlan('both', [
        { name: 'exits_zero', code: 0 },
        { name: 'expects_one', code: 1, expect: 1 },
    ])
    const { code, out } = run(plan)
    check('a case that exits 0 is green', /\[PASS\]\s+exits_zero/.test(out), out.slice(-900))
    check('an entry that expects exit 1 is green when the case exits 1', /\[PASS\]\s+expects_one/.test(out), out.slice(-900))
    check('and the run is green', code === 0 && /pass: 2, fail: 0/.test(out), `exit ${code}`)
}

// 3. A case file that is not there is a red today, and not the "not tested" the plan schema wants in
// the end: the child fails with ENOENT, which is a non-zero exit like any other. Pinned so the day
// that becomes exit 2 this test is the one that says so.
{
    const plan = writePlan('missing', [{ name: 'no_such_case', code: 0, case: null }])
    const { code, out } = run(plan)
    check('a case file that does not exist is not a green', /\[FAIL\]\s+no_such_case/.test(out), out.slice(-900))
    check('the missing file is named as an exit, not as a pass', code === 1, `exit ${code}`)
}

fs.rmSync(WORK, { recursive: true, force: true })
console.log(`tool-exit: ${checks} checks, ${failed} failed`)
process.exit(failed === 0 ? 0 : 1)