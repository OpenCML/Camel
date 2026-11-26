// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Self-check of `scripts/gates.mjs`: a gate that does not return is killed at its limit, together
 * with the process it started, and the run is red for that gate.
 *
 * The fake gate sleeps 30 s and starts a child that sleeps 30 s too; each writes its pid to a file.
 * Run with a 2 s limit, the run has to come back well before 30 s, exit non-zero, say on the gate's
 * line that it was killed, and leave neither pid alive.
 */
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const GATES = process.env.CAMEL_GATES_TOOL || path.join(HERE, 'gates.mjs')

let checks = 0
let failed = 0
function check(name, ok, detail = '') {
    checks++
    if (!ok) failed++
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${ok ? '' : `: ${detail}`}`)
}

function alive(pid) {
    try {
        process.kill(pid, 0)
        return true
    } catch (error) {
        return error.code === 'EPERM'
    }
}

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-gates-test-'))
try {
    const pids = path.join(work, 'pids.txt')
    const fake = path.join(work, 'fake-gate.mjs')
    fs.writeFileSync(
        fake,
        [
            "import { spawn } from 'node:child_process'",
            "import fs from 'node:fs'",
            "const child = spawn(process.execPath, ['-e', 'setTimeout(() => {}, 30000)'], { stdio: 'ignore' })",
            `fs.writeFileSync(${JSON.stringify(pids)}, process.pid + ' ' + child.pid)`,
            'setTimeout(() => {}, 30000)',
            '',
        ].join('\n'),
    )

    const started = Date.now()
    const run = spawnSync(process.execPath, [GATES, '--script', fake, '--timeout', '2'], {
        encoding: 'utf8',
        timeout: 20000,
    })
    const seconds = (Date.now() - started) / 1000
    const text = `${run.stdout || ''}${run.stderr || ''}`
    console.log(text.trimEnd())

    check('the run returned long before the fake gate would have', seconds < 15, `${seconds.toFixed(1)} s`)
    check('the run exits non-zero', run.status === 1, `exit ${run.status}`)
    check('the gate line is a FAIL that says it was killed', /FAIL exit=killed/.test(text), 'no FAIL line')

    const written = fs.existsSync(pids) ? fs.readFileSync(pids, 'utf8').trim().split(' ').map(Number) : []
    check('the fake gate started and recorded its two pids', written.length === 2 && written.every(Number.isInteger), pids)
    for (const [label, pid] of [['the gate', written[0]], ['the gate\'s child', written[1]]]) {
        check(`${label} is not left running`, !alive(pid), `pid ${pid} is alive`)
        if (alive(pid)) {
            try {
                process.kill(pid, 'SIGKILL')
            } catch {
                /* already gone */
            }
        }
    }
} finally {
    fs.rmSync(work, { recursive: true, force: true })
}
// The process-trace gate asks one question of a range and none of the whole tree: with `--range` it
// carries the range to the checker, without it the checker only lists.
{
    const listed = (...extra) =>
        spawnSync(process.execPath, [GATES, '--list', ...extra], { encoding: 'utf8', timeout: 20000 }).stdout || ''
    const traceLine = (text) => text.split(/\r?\n/).find((line) => line.includes('check-process-traces')) || ''
    const plain = traceLine(listed())
    const ranged = traceLine(listed('--range', 'aaa..bbb'))
    check('the process-trace gate only lists the tree without a range', plain.includes('--list') && !plain.includes('--range'), plain)
    check('and judges the lines of a range when given one', ranged.includes('--range aaa..bbb') && !ranged.includes('--list'), ranged)
}
console.log(`gates: ${checks} checks, ${failed} failed`)
process.exit(failed === 0 ? 0 : 1)
