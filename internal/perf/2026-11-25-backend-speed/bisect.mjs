#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT
//
// Manual bisect over the lane's own measurement, for the cells the handover table shows moving by
// more than 20%.
//
// Why manual and not `git bisect run`: a step has to produce a READING (a number in milliseconds
// and the samples it came from), not just good/bad, because the reading is the deliverable and the
// threshold may have to be re-read when a step lands between two plateaus.
//
// A step is: check out the commit, build it (wiping `build/` and `out/` when the layout moved
// under `src/`, because CMake must not be handed one tree's cache for another tree's sources),
// measure ONE cell, compare.
//
//   node bisect.mjs --program loop --backend jit --sample first --threshold 400 \
//                   --lo 1fa180c4f --hi 6fcee16d0c --out results/bisect-loop-jit.jsonl
//
// `--sample first` is the first execution of the pass in a fresh process and is the right choice
// for this lane: within one process, `std::nvm` and `std::jit` readings on `small_calls` drift
// upward run over run at the newer commit (`... 950, 1430, 1458`), so a median of later samples
// measures that drift as much as the change being bisected.

import { spawnSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(HERE, '..', '..', '..')

function args() {
    const o = { iters: 3, threshold: null, lo: null, hi: null, maxSteps: 14, out: null }
    const a = process.argv.slice(2)
    for (let i = 0; i < a.length; i++) {
        const k = a[i].replace(/^--/, '')
        if (k === 'iters' || k === 'maxSteps') o[k] = parseInt(a[++i], 10)
        else if (k === 'threshold') o[k] = parseFloat(a[++i])
        else o[k] = a[++i]
    }
    if (!o.program || !o.backend || o.threshold === null || !o.lo || !o.hi) {
        throw new Error('need --program --backend --threshold --lo --hi [--sample first|steady]')
    }
    o.sample = o.sample || 'first'
    return o
}

const o = args()

function git(...a) {
    const r = spawnSync('git', a, { cwd: ROOT, encoding: 'utf8' })
    if (r.status !== 0) throw new Error(`git ${a.join(' ')}: ${r.stderr}`)
    return (r.stdout || '').trim()
}

function hasSrc() {
    return fs.existsSync(path.join(ROOT, 'src'))
}

function build(step) {
    const logDir = path.join(HERE, 'results')
    fs.mkdirSync(logDir, { recursive: true })
    const log = path.join(logDir, `bisect-build-${step}.log`)
    const r = spawnSync(process.execPath, ['./scripts/build.js'], {
        cwd: ROOT,
        encoding: 'utf8',
        maxBuffer: 256 * 1024 * 1024,
    })
    fs.writeFileSync(log, (r.stdout || '') + (r.stderr || ''))
    if (r.status !== 0) throw new Error(`build failed at ${step}; see ${log}`)
}

function measure() {
    const env = { ...process.env }
    env.CAMEL_HOME = path.join(ROOT, 'out', 'latest')
    env.PATH = path.join(ROOT, 'out', 'latest', 'bin') + ';' + env.PATH
    const progs = {
        call: { file: 'benchmarks/parallel/cases/fib.cml', pre: [], extra: [] },
        loop: { file: 'benchmarks/parallel/cases/small_calls.cml', pre: [], extra: [] },
        tensor: { file: 'benchmarks/parallel/cases/tensor_train.cml', pre: ['std::macro'], extra: [] },
        branch: {
            file: 'showcase/d4-mcts/search.cml',
            pre: [],
            extra: ['--arg', 'seed=42', '--arg', 'playouts=2000', '--arg', 'position=XX.OO....'],
        },
        closure: { file: 'examples/functions/closures.cml', pre: [], extra: [] },
    }
    const p = progs[o.program]
    const passMap = { nvm: 'std::nvm', fvm: 'std::fvm', jit: 'std::jit', prl: 'std::prl' }
    const [beId, threads] = o.backend.includes('-') ? o.backend.split('-') : [o.backend, null]
    const pass = passMap[beId]
    const a = ['--output-format', 'json', '--timeit', `${pass}/${o.iters}`]
    if (threads) a.push('--threads', threads)
    a.push(...p.extra, p.file, ...p.pre, pass)
    const r = spawnSync(path.join(ROOT, 'out', 'latest', 'bin', 'camel.exe'), a, {
        cwd: ROOT,
        env,
        encoding: 'utf8',
        maxBuffer: 64 * 1024 * 1024,
    })
    let json = null
    try {
        json = JSON.parse(r.stdout)
    } catch {
        const s = (r.stdout || '').indexOf('{')
        const e = (r.stdout || '').lastIndexOf('}')
        if (s >= 0 && e > s) {
            try {
                json = JSON.parse(r.stdout.slice(s, e + 1))
            } catch {}
        }
    }
    const samples = json && Array.isArray(json.samples_ms) ? json.samples_ms : []
    if (!samples.length) throw new Error(`no samples at this step (exit ${r.status})`)
    const rest = samples.slice(1)
    const med = rest.length
        ? [...rest].sort((x, y) => x - y)[Math.floor(rest.length / 2)]
        : samples[0]
    return { value: o.sample === 'first' ? samples[0] : med, samples, exit: r.status }
}

const steps = []
const out = o.out || path.join(HERE, 'results', `bisect-${o.program}-${o.backend}.jsonl`)
stdoutWrite(`bisect ${o.program}/${o.backend} ${o.sample} threshold=${o.threshold} ${o.lo}..${o.hi}`)

let good = git('rev-parse', o.lo)
let bad = git('rev-parse', o.hi)
let srcState = hasSrc()

for (let step = 1; step <= o.maxSteps; step++) {
    const range = git('rev-list', '--ancestry-path', '--reverse', `${good}..${bad}`).split('\n').filter(Boolean)
    if (range.length <= 1) {
        stdoutWrite(`converged after ${step - 1} steps: culprit = ${range[0] || 'direct child'}`)
        break
    }
    const mid = range[Math.floor((range.length - 1) / 2)]
    git('checkout', '--detach', mid)
    const nowSrc = hasSrc()
    if (nowSrc !== srcState) {
        // The layout moved: CMake must not see the other tree's cache.
        for (const d of ['build', 'out']) {
            const p = path.join(ROOT, d)
            if (fs.existsSync(p)) fs.rmSync(p, { recursive: true, force: true })
        }
        stdoutWrite(`step ${step} ${mid.slice(0, 10)} layout moved -> clean build; candidates left ${range.length}`)
        srcState = nowSrc
    } else {
        stdoutWrite(`step ${step} ${mid.slice(0, 10)} candidates left ${range.length}`)
    }
    build(step)
    const m = measure()
    const rec = {
        step,
        commit: mid,
        subject: git('log', '-1', '--format=%s', mid),
        date: git('log', '-1', '--format=%ci', mid),
        value: m.value,
        samples: m.samples,
        candidates_left: range.length,
        at: new Date().toISOString(),
    }
    steps.push(rec)
    fs.appendFileSync(out, JSON.stringify(rec) + '\n')
    stdoutWrite(`   -> ${m.value} ms (${m.samples.map((x) => x.toFixed(1)).join(', ')})`)
    if (m.value < o.threshold) good = mid
    else bad = mid
}

function stdoutWrite(s) {
    process.stderr.write(s + '\n')
}

const finalRange = git('rev-list', '--ancestry-path', '--reverse', `${good}..${bad}`).split('\n').filter(Boolean)
process.stderr.write(`\nfirst bad = ${bad}\nlast good = ${good}\ncandidates in between: ${finalRange.length}\n`)
for (const c of finalRange.slice(0, 12)) {
    process.stderr.write(`  ${c.slice(0, 10)} ${git('log', '-1', '--format=%ci %s', c)}\n`)
}
fs.writeFileSync(
    path.join(HERE, 'results', `bisect-${o.program}-${o.backend}.result.json`),
    JSON.stringify({ cell: `${o.program}/${o.backend}`, sample: o.sample, threshold: o.threshold, good, bad, finalRange, steps }, null, 2)
)