#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT
//
// Backend speed lane (D-1125-35), handover one: measure only, change nothing.
//
// Two modes, one command line:
//
//   node run-timing.mjs --mode timeit --point <label> --out results/<label>.timeit.json
//   node run-timing.mjs --mode stdout --point <label> --out results/<label>.stdout.json
//
// `timeit` runs, for every (program, back end) cell,
//
//     camel --output-format json --timeit <backend>/<N> [--threads T] [--arg ...] <program> [prePass...] <backend>
//
// so the *pass* is measured N times from a fresh graph inside ONE process. Sample 1 is the first
// execution and samples 2..N are what follows it; JIT compiles on the first call and the taskflow
// pool starts its workers on the first task, so those two samples are NOT the same thing and this
// script keeps them apart (`first_ms` vs `steady_ms`). A cell is never reported with one number.
//
// `--timeit` never prints `main`'s result, so correctness needs the second mode: `stdout` runs the
// same cell once as a plain run and keeps its standard output, for a line-by-line comparison of the
// three commits.
//
// The cell list is fixed; the plan page names the five programs and the four back ends.

import { spawnSync } from 'child_process'
import fs from 'fs'
import os from 'os'
import path from 'path'
import { fileURLToPath } from 'url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(HERE, '..', '..', '..')

const PROGRAMS = [
    { id: 'call', program: 'benchmarks/parallel/cases/fib.cml', pre: [], extra: [], iters: 5 },
    { id: 'loop', program: 'benchmarks/parallel/cases/small_calls.cml', pre: [], extra: [], iters: 5 },
    {
        id: 'tensor',
        program: 'benchmarks/parallel/cases/tensor_train.cml',
        pre: ['std::macro'],
        extra: [],
        iters: 5,
    },
    {
        id: 'branch',
        program: 'showcase/d4-mcts/search.cml',
        pre: [],
        extra: ['--arg', 'seed=42', '--arg', 'playouts=2000', '--arg', 'position=XX.OO....'],
        iters: 5,
        // `--timeit` cannot measure this one: its SECOND iteration fails to compile with
        // `SemanticDiag::NoMatchingFunction` -- a `(place: int) => bool pure` against
        // `prelude::filter: <f: (item: int) => bool> (r: Range) => int[]` -- so the pass runs once
        // and then the run dies. The defect is reported, not worked around silently; the cell is
        // measured by repeated processes instead, and the column is labelled as such.
        mode: 'process',
    },
    // Sub-millisecond work: more samples, because the spread of five of them is not a median.
    { id: 'closure', program: 'examples/functions/closures.cml', pre: [], extra: [], iters: 9 },
]

// `null` threads means the option is absent, i.e. the machine's own core count: the same choice on
// all three commits, which is what makes the readings comparable.
const BACKENDS = [
    { id: 'nvm', pass: 'std::nvm', threads: null },
    { id: 'fvm', pass: 'std::fvm', threads: null },
    { id: 'jit', pass: 'std::jit', threads: null },
    { id: 'prl-1', pass: 'std::prl', threads: 1 },
    { id: 'prl-4', pass: 'std::prl', threads: 4 },
    { id: 'prl-16', pass: 'std::prl', threads: 16 },
]

function parseArgs(argv) {
    const out = { mode: 'timeit', point: 'unlabelled', camel: null, out: null, only: null }
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i]
        if (a === '--mode') out.mode = argv[++i]
        else if (a === '--point') out.point = argv[++i]
        else if (a === '--camel') out.camel = argv[++i]
        else if (a === '--out') out.out = argv[++i]
        else if (a === '--only') out.only = argv[++i].split(',')
        else throw new Error(`unknown argument: ${a}`)
    }
    return out
}

function camelPath(explicit) {
    if (explicit) return explicit
    const p = path.join(ROOT, 'out', 'latest', 'bin', 'camel.exe')
    if (!fs.existsSync(p)) throw new Error(`no camel.exe at ${p}`)
    return p
}

/** Busy cores: everything the machine gave to processes between two samples. */
function cpuSnapshot() {
    let idle = 0
    let total = 0
    for (const c of os.cpus()) {
        for (const k of Object.keys(c.times)) total += c.times[k]
        idle += c.times.idle
    }
    return { idle, total, at: Date.now() }
}

function busyCores(a, b) {
    const seconds = (b.at - a.at) / 1000
    if (seconds <= 0) return null
    // `os.cpus()` times are milliseconds; the machine gave (busy ms / 1000) core-seconds over
    // `seconds` of wall clock, and that quotient is the number of cores it gave away.
    return Math.round(((b.total - b.idle - (a.total - a.idle)) / 1000 / seconds) * 100) / 100
}

function cellList(only) {
    const cells = []
    for (const prog of PROGRAMS) {
        for (const be of BACKENDS) {
            const key = `${prog.id}/${be.id}`
            if (only && !only.includes(prog.id) && !only.includes(be.id) && !only.includes(key)) continue
            cells.push({ prog, be })
        }
    }
    return cells
}

/** Cells the `timeit` mode can measure: everything the compiler will run more than once. */
function wantsTimeit(prog) {
    return prog.mode !== 'process'
}

function env() {
    const e = { ...process.env }
    e.CAMEL_HOME = path.join(ROOT, 'out', 'latest')
    e.PATH = path.join(ROOT, 'out', 'latest', 'bin') + ';' + e.PATH
    return e
}

/** Finds the JSON object the run wrote, tolerating a leading log line. */
function pickJson(text) {
    try {
        return JSON.parse(text)
    } catch {}
    const start = text.indexOf('{')
    const end = text.lastIndexOf('}')
    if (start >= 0 && end > start) {
        try {
            return JSON.parse(text.slice(start, end + 1))
        } catch {}
    }
    return null
}

function median(xs) {
    if (xs.length === 0) return null
    const s = [...xs].sort((a, b) => a - b)
    const m = Math.floor(s.length / 2)
    return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2
}

function runTimeit(camel, cell, iters) {
    const { prog, be } = cell
    const args = ['--output-format', 'json', '--timeit', `${be.pass}/${iters}`]
    if (be.threads !== null) args.push('--threads', String(be.threads))
    args.push(...prog.extra, prog.program, ...prog.pre, be.pass)
    const t0 = Date.now()
    const r = spawnSync(camel, args, { cwd: ROOT, env: env(), encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 })
    const json = pickJson(r.stdout || '')
    const samples = json && Array.isArray(json.samples_ms) ? json.samples_ms : []
    const first = samples.length ? samples[0] : null
    const rest = samples.slice(1)
    return {
        program: prog.id,
        backend: be.id,
        pass: be.pass,
        threads: be.threads,
        iters,
        argv: args,
        exit: r.status,
        wall_ms: Date.now() - t0,
        samples_ms: samples,
        first_ms: first,
        steady_ms: median(rest),
        steady_min_ms: rest.length ? Math.min(...rest) : null,
        steady_max_ms: rest.length ? Math.max(...rest) : null,
        failed: json ? json.failed === true : null,
        stderr_head: (r.stderr || '').slice(0, 400),
    }
}

function runStdout(camel, cell) {
    const { prog, be } = cell
    const args = []
    if (be.threads !== null) args.push('--threads', String(be.threads))
    args.push(...prog.extra, prog.program, ...prog.pre, be.pass)
    const r = spawnSync(camel, args, { cwd: ROOT, env: env(), encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 })
    return {
        program: prog.id,
        backend: be.id,
        threads: be.threads,
        argv: args,
        exit: r.status,
        stdout: r.stdout,
        stderr: r.stderr,
    }
}

/**
 * N whole processes of the program. Every run pays start-up, parse, compile and teardown, so this
 * reading is NOT the pass time of the `timeit` cells and its column says so. It is used only where
 * `--timeit` cannot run the pass twice, and it is the same shape at all three commits.
 */
function runProcessRuns(camel, cell, n) {
    const { prog, be } = cell
    const args = []
    if (be.threads !== null) args.push('--threads', String(be.threads))
    args.push(...prog.extra, prog.program, ...prog.pre, be.pass)
    const runs = []
    let lastOut = ''
    let exit = 0
    for (let i = 0; i < n; i++) {
        const t0 = Date.now()
        const r = spawnSync(camel, args, { cwd: ROOT, env: env(), encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 })
        const ms = Date.now() - t0
        runs.push(ms)
        exit = r.status
        if (i === 0) lastOut = r.stdout || ''
    }
    const rest = runs.slice(1)
    return {
        program: prog.id,
        backend: be.id,
        pass: be.pass,
        threads: be.threads,
        iters: n,
        argv: args,
        exit,
        measurement: 'whole process (start-up + parse + pass + teardown)',
        samples_ms: runs,
        first_ms: runs[0],
        steady_ms: median(rest),
        steady_min_ms: rest.length ? Math.min(...rest) : null,
        steady_max_ms: rest.length ? Math.max(...rest) : null,
        stdout_first_run: lastOut,
        stderr_head: '',
    }
}

const args = parseArgs(process.argv.slice(2))
const camel = camelPath(args.camel)
const cells = cellList(args.only)

const loadBefore = cpuSnapshot()
const started = new Date().toISOString()
const results = []
for (const cell of cells) {
    let one
    if (args.mode === 'stdout') one = runStdout(camel, cell)
    else if (args.mode === 'process') one = runProcessRuns(camel, cell, cell.prog.iters)
    else {
        if (!wantsTimeit(cell.prog)) continue
        one = runTimeit(camel, cell, cell.prog.iters)
    }
    results.push(one)
    const tail =
        args.mode === 'stdout'
            ? `${(one.stdout || '').split('\n').filter(Boolean).length} lines`
            : `first=${one.first_ms} steady=${one.steady_ms}`
    process.stderr.write(`${args.point} ${one.program}/${one.backend} exit=${one.exit} ${tail}\n`)
}
const loadAfter = cpuSnapshot()

const payload = {
    point: args.point,
    mode: args.mode,
    host: os.hostname(),
    cores: os.cpus().length,
    started,
    finished: new Date().toISOString(),
    busy_cores: busyCores(loadBefore, loadAfter),
    node: process.version,
    camel,
    results,
}
const out = args.out || path.join(HERE, 'results', `${args.point}.${args.mode}.json`)
fs.mkdirSync(path.dirname(out), { recursive: true })
fs.writeFileSync(out, JSON.stringify(payload, null, 2))

if (args.mode !== 'stdout') {
    const csv = path.join(path.dirname(out), `${args.point}.${args.mode}.csv`)
    const head = 'program,backend,threads,iters,first_ms,steady_ms,steady_min_ms,steady_max_ms,samples_ms'
    const rows = results.map((r) =>
        [
            r.program,
            r.backend,
            r.threads === null ? 'default' : r.threads,
            r.iters,
            r.first_ms,
            r.steady_ms,
            r.steady_min_ms,
            r.steady_max_ms,
            `"${(r.samples_ms || []).join(' ')}"`,
        ].join(',')
    )
    fs.writeFileSync(csv, [head, ...rows].join('\n') + '\n')
    process.stderr.write(`wrote ${csv}\n`)
}

process.stderr.write(
    `round load: busy_cores=${payload.busy_cores} of ${payload.cores} (${started} .. ${payload.finished})\n`
)
process.stderr.write(`wrote ${out}\n`)