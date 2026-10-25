// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Load harness for the debug adapter <-> worker request lifecycle.
//
// The two DAP tests that flaked under load ("The program being debugged has ended." instead of
// the answer to a `continue`/`nextPass`) are run many times in a row while N busy loops keep
// the machine's run queues full. The scheduler pressure is the point: it widens the window
// between the worker deciding the answer to a command and the worker being killed.
//
//     node test/tools/debugger/race-harness.mjs                       # 25 rounds, 24 loops
//     node test/tools/debugger/race-harness.mjs --rounds 200 --load 24
//     node test/tools/debugger/race-harness.mjs --test dap_session
//     node test/tools/debugger/race-harness.mjs --test dap_pipeline,dap_session --load 0
//
// Every test is a standalone script under test/cases/tools/debugger/ that exits 0 when all its
// checks pass. One round = one fresh process per named test, in order. `--load N` starts N
// CPU-bound children of this process for the whole run; they exit by themselves when the
// harness does (their deadline is inherited, and a SIGTERM/SIGKILL of this process is also
// covered by the children watching this process' pid).
//
// The harness measures the load-dependent rate; it does not prove the mechanism. The two tests
// were once red about once in ten rounds on a loaded machine. A deterministic red needs a probe
// that widens the window between the command's answer and the end of the run: A probe widens
// that window, and the rate with and without it is measured.
//
// Exit code is 1 when at least one round failed, 0 otherwise: a CI job can use it directly,
// but a green run proves nothing unless --rounds is large enough to have seen a red before.

import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..')
const CASES = path.join(REPO_ROOT, 'test', 'cases', 'tools', 'debugger')

function parseArgs(argv) {
    const out = { rounds: 25, load: 24, tests: ['dap_pipeline', 'dap_session'], maxSeconds: 0 }
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i]
        if (a === '--rounds') out.rounds = Number(argv[++i])
        else if (a === '--load') out.load = Number(argv[++i])
        else if (a === '--test' || a === '--tests') out.tests = String(argv[++i]).split(',').map((s) => s.trim()).filter(Boolean)
        else if (a === '--max-seconds') out.maxSeconds = Number(argv[++i])
        else throw new Error(`unknown argument: ${a}`)
    }
    return out
}

/**
 * A CPU-bound child: a tight arithmetic loop, no allocation, no I/O. `deadline` bounds it so
 * an abandoned child cannot outlive the run; `parent` is the harness pid, checked often, so
 * killing the harness takes the load with it.
 */
function loadProgram(deadline, parent) {
    return `
const deadline = ${deadline};
const parent = ${parent};
let x = 0;
let i = 0;
for (;;) {
    for (let k = 0; k < 200000; k++) { x = (x + k * 3) % 1000003; }
    if (++i % 8 === 0) {
        if (Date.now() > deadline) break;
        try { process.kill(parent, 0); } catch { break; }
    }
}
process.exit(x === 42 ? 1 : 0);
`
}

function startLoad(n, deadline) {
    const children = []
    for (let i = 0; i < n; i++) {
        const child = spawn(process.execPath, ['-e', loadProgram(deadline, process.pid)], {
            stdio: 'ignore',
            detached: false,
        })
        child.on('error', () => {})
        children.push(child)
    }
    return children
}

/** Runs one test script to completion; returns `{ code, output, ms }`. */
function runOne(test, timeoutMs) {
    const file = path.join(CASES, `${test}.test.mjs`)
    return new Promise((resolve) => {
        const started = Date.now()
        const child = spawn(process.execPath, [file], {
            cwd: REPO_ROOT,
            stdio: ['ignore', 'pipe', 'pipe'],
        })
        let output = ''
        const take = (d) => {
            output += d
            // Enough to see the failure; a passing test prints one line.
            if (output.length > 200000) output = output.slice(-200000)
        }
        child.stdout.on('data', take)
        child.stderr.on('data', take)
        const timer = setTimeout(() => {
            try { child.kill('SIGKILL') } catch {}
        }, timeoutMs)
        child.on('error', (e) => {
            clearTimeout(timer)
            resolve({ code: -1, output: `spawn failed: ${e}`, ms: Date.now() - started })
        })
        child.on('exit', (code, signal) => {
            clearTimeout(timer)
            resolve({ code: signal ? -1 : code, output, ms: Date.now() - started, signal })
        })
    })
}

const args = parseArgs(process.argv.slice(2))
const runDeadline = Date.now() + 1000 * 60 * 60
const load = startLoad(args.load, runDeadline)
const startedAll = Date.now()

// The load is the experiment, so it is measured, not assumed: after the children have had a
// moment to enter their loops, read their scheduler state. A green run whose busy loops never
// started proves nothing, so that is an error of its own (exit 3), not a pass.
function processState(pid) {
    try {
        const stat = fs.readFileSync(`/proc/${pid}/stat`, 'utf8')
        return stat.slice(stat.lastIndexOf(')') + 2).split(' ')[0]
    } catch {
        return null
    }
}
await new Promise((resolve) => setTimeout(resolve, 200))
if (process.platform === 'linux' && args.load > 0) {
    const states = load.map((child) => processState(child.pid))
    const alive = states.filter((state) => state !== null && state !== 'Z').length
    const running = states.filter((state) => state === 'R').length
    console.log(
        `[race-harness] load: ${alive}/${args.load} busy loop(s) alive, ${running} in state R`,
    )
    if (alive < args.load) {
        for (const child of load) {
            try { child.kill('SIGKILL') } catch {}
        }
        console.log('[race-harness] the load did not start: a green run would not be evidence')
        process.exit(3)
    }
}

console.log(
    `[race-harness] ${args.rounds} round(s) x ${args.tests.join(', ')} with ${args.load} busy loop(s); ` +
        `node ${process.version}, ${os.cpus().length} cpus, load1 ${fs.readFileSync('/proc/loadavg', 'utf8').trim()}`,
)

const stats = Object.fromEntries(args.tests.map((t) => [t, { red: 0, green: 0, firstFail: null, failures: [] }]))
let stopped = false
const stop = () => {
    stopped = true
}
process.on('SIGINT', stop)
process.on('SIGTERM', stop)

for (let round = 1; round <= args.rounds && !stopped; round++) {
    if (args.maxSeconds > 0 && (Date.now() - startedAll) / 1000 > args.maxSeconds) {
        console.log(`[race-harness] --max-seconds reached after ${round - 1} round(s)`)
        break
    }
    const line = []
    for (const test of args.tests) {
        const r = await runOne(test, 180000)
        const s = stats[test]
        if (r.code === 0) {
            s.green++
            line.push(`${test}=ok`)
        } else {
            s.red++
            const failed = r.output
                .split('\n')
                .filter((l) => l.startsWith('FAILED:') || /checks failed/.test(l))
                .slice(0, 6)
            s.failures.push({ round, ms: r.ms, failed, output: r.output })
            if (!s.firstFail) s.firstFail = round
            line.push(`${test}=RED(${r.ms}ms): ${failed[0] || `exit ${r.code}`}`)
        }
    }
    console.log(`[race-harness] round ${round}/${args.rounds}: ${line.join(' | ')}`)
}

for (const child of load) {
    try { child.kill('SIGKILL') } catch {}
}

const seconds = ((Date.now() - startedAll) / 1000).toFixed(1)
console.log(`[race-harness] done in ${seconds} s; load1 ${fs.readFileSync('/proc/loadavg', 'utf8').trim()}`)
let red = 0
for (const test of args.tests) {
    const s = stats[test]
    red += s.red
    console.log(
        `[race-harness] ${test}: ${s.green} green / ${s.red} red of ${s.green + s.red}` +
            (s.firstFail ? ` (first red in round ${s.firstFail})` : ''),
    )
    if (s.failures.length > 0) {
        const f = s.failures[0]
        console.log(`[race-harness] ${test}: first red output (round ${f.round}, ${f.ms} ms):`)
        console.log(
            f.output
                .split('\n')
                .filter((l) => l.trim())
                .slice(-40)
                .map((l) => `    ${l}`)
                .join('\n'),
        )
    }
}
process.exit(red > 0 ? 1 : 0)