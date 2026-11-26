// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The CPU time of one chosen pass of one pipeline, read out of the compiler's own `--time-phases`
// report and judged as a **ratio against the same pass's earlier run in the same run**:
//
//   node test/tools/opt-cost/second_pass.mjs --case test/cases/... --max-ratio 14
//   node test/tools/opt-cost/second_pass.mjs --case ... --max-ratio 14 --pass std::opt --index 2
//
// Why a ratio, and not a ceiling in milliseconds. The reading this entry takes is `std::opt` run
// twice: the first run optimizes the program, and the second runs over the graph the first one left
// behind. Over `perf_fib_smoke`'s timed helper that was 207.9 ms of CPU for the first
// and 1673.8 ms for the second. An absolute ceiling on the second number was this tool's first
// shape, and it was not portable: CPU milliseconds are a host's speed as much as the pass's work,
// and the same commit was red on win-x64 and on macos-arm64 while green here -- CI 44938160097 read
// `[FAIL] second_pass.perf_fib_smoke 2.96 s` on macos-arm64, `[FAIL] second_pass.perf_fib_smoke
// 2.33 s` and `[FAIL] second_pass.concat_chain 1.95 s` on win-x64, against 2.02 s and 1.69 s of the
// same tests on the reference host -- so the bound that held on one host was red on the others for a
// reason that is not a regression.
//
// What the ratio cancels, and why it does. Both readings are the CPU time of the *same* pass
// (`std::opt`, the same code over the same rules library) over the *same* program, taken in the
// *same* process with the same work beside them; they differ only in how much graph the pass is
// given, and that is an amount of work, not a property of the machine. A host N times slower at
// this work stretches both readings by the same N, so the number the entry judges does not move,
// and the question it asks -- "the second pass over this program must not cost more than K times
// the first" -- holds on every host. The one thing it does not cancel is a host whose slowness is
// specifically in memory rather than in instructions: the second pass walks a graph the inliner
// unrolled and the first one walks a graph it has not, so a host slow in cache and memory reads a
// somewhat higher ratio than one that is not. That drift is a fraction of the gap the entry
// separates (the helper that built the chain reads 20.3 times on the reference host, the folded one
// 8.0), and it is what the margin on the bound is for. A bound per platform was rejected instead:
// it writes the machine's name into the criterion, and a regression then hides behind the machine
// that has the generous one.
//
// The unit is CPU time (user + system) per pass, which is what `--time-phases` writes in `cpu_ns`
// and what a ratio should be read from: a shared host stretches the wall clock of a CPU-bound pass
// several times over without the pass having used more CPU (the rule `test/tools/startup-cost.mjs`
// states). The tool prints the run's own pass list beside the judged number, so a failure can be
// read without re-running anything.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { takeCamel } from '../camel-arg.mjs'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..')

// The smallest CPU time a baseline may read for the ratio to be a number. Below it the division
// measures the clock's resolution and the host's jitter rather than the pass, so the reading is
// refused rather than passed (the floor the parallel speed-up guard keeps for its two windows).
const MIN_BASELINE_CPU_MS = 1.0

function usage(message) {
    if (message) console.error(`opt-second-pass: ${message}`)
    console.error(
        'usage: node test/tools/opt-cost/second_pass.mjs --case <path> --max-ratio <times> ' +
            '[--camel <path>] [--pass std::opt] [--index 2] [--baseline-index 1] [--backend jit] [--json]',
    )
    process.exit(2)
}

function parseArgs(args) {
    const options = {
        case: null,
        maxRatio: null,
        pass: 'std::opt',
        index: 2,
        baselineIndex: 1,
        backend: 'jit',
        json: false,
    }
    for (let i = 0; i < args.length; i++) {
        const arg = args[i]
        const value = args[i + 1]
        if (arg === '--json') options.json = true
        else if (arg === '--case') {
            if (value === undefined) usage('--case needs a path')
            options.case = value
            i += 1
        } else if (arg === '--max-ratio') {
            if (value === undefined) usage('--max-ratio needs a number of times')
            options.maxRatio = Number(value)
            if (!Number.isFinite(options.maxRatio) || options.maxRatio <= 0) {
                usage(`--max-ratio: '${value}' is not a positive number`)
            }
            i += 1
        } else if (arg === '--pass') {
            if (value === undefined) usage('--pass needs a pass name')
            options.pass = value
            i += 1
        } else if (arg === '--index') {
            if (value === undefined) usage('--index needs a number')
            options.index = Number(value)
            if (!Number.isInteger(options.index) || options.index < 1) usage(`--index: '${value}' is not a positive integer`)
            i += 1
        } else if (arg === '--baseline-index') {
            if (value === undefined) usage('--baseline-index needs a number')
            options.baselineIndex = Number(value)
            if (!Number.isInteger(options.baselineIndex) || options.baselineIndex < 1) {
                usage(`--baseline-index: '${value}' is not a positive integer`)
            }
            i += 1
        } else if (arg === '--backend') {
            if (value === undefined) usage('--backend needs a name')
            options.backend = value
            i += 1
        } else usage(`unknown argument '${arg}'`)
    }
    if (options.case === null) usage('--case is required')
    if (options.maxRatio === null) usage('--max-ratio is required')
    if (options.index === options.baselineIndex) {
        usage(
            '--index and --baseline-index name the same run of the pass: ' +
                'the ratio would be 1 by construction',
        )
    }
    return options
}

// The install root a `camel` binary belongs to: `<root>/bin/camel` -> `<root>`.
function installOf(camel) {
    return path.dirname(path.dirname(camel))
}

function runInvocation(camel, argv) {
    const run = spawnSync(camel, argv, {
        cwd: REPO_ROOT,
        stdio: ['ignore', 'pipe', 'pipe'],
        encoding: 'utf8',
        timeout: 300000,
        maxBuffer: 256 * 1024 * 1024,
        env: { ...process.env, CAMEL_HOME: installOf(camel) },
    })
    return {
        exit: run.status === null ? -1 : run.status,
        stdout: run.stdout || '',
        stderr: run.stderr || '',
        failed: run.error !== undefined,
    }
}

// The `--time-phases` document: one JSON object written to standard error, after whatever the run
// had to say. A `pass` phase is one pass of the pipeline; the depth-0 ones do not overlap.
function parsePhases(stderr) {
    const at = stderr.indexOf('{"schema"')
    if (at < 0) return null
    try {
        return JSON.parse(stderr.slice(at))
    } catch {
        return null
    }
}

function main() {
    let options
    let camel
    try {
        const named = takeCamel(process.argv.slice(2), REPO_ROOT, usage)
        camel = named.camel
        options = parseArgs(named.rest)
    } catch (error) {
        console.error(`opt-second-pass: ${error.message}`)
        return 2
    }
    const casePath = path.isAbsolute(options.case) ? options.case : path.join(REPO_ROOT, options.case)
    if (!fs.existsSync(camel)) {
        console.log(`opt-second-pass: REFUSED no camel binary at ${camel}`)
        return 2
    }
    if (!fs.existsSync(casePath)) {
        console.log(`opt-second-pass: REFUSED no case at ${casePath}`)
        return 2
    }

    const version = runInvocation(camel, ['--version'])
    const build =
        `${version.stdout}\n${version.stderr}`
            .split('\n')
            .map((one) => one.trim())
            .find((one) => one.length > 0) || '<no --version output>'

    // The pipeline runs the pass as many times as the larger index asks for: the baseline and the
    // judged run have to be in one process, which is the whole of what the ratio cancels.
    const runs = Math.max(options.index, options.baselineIndex)
    const passes = ['--time-phases', casePath]
    for (let run = 1; run <= runs; run++) passes.push(options.pass)
    passes.push(`std::${options.backend}`)
    const run = runInvocation(camel, passes)
    const doc = parsePhases(run.stderr)
    if (doc === null) {
        console.log(
            `opt-second-pass: REFUSED the run wrote no --time-phases document (exit ${run.exit}): ` +
                `${run.stderr.trim().split('\n').slice(-3).join(' / ')}`,
        )
        return 2
    }

    const top = doc.phases.filter((one) => one.depth === 0)
    const totalCpuMs = top.reduce((sum, one) => sum + one.cpu_ns, 0) / 1e6
    const seen = new Map()
    const listed = []
    let judged = null
    let baseline = null
    for (const one of top) {
        if (one.name !== 'pass') continue
        const detail = one.detail || one.name
        const nth = (seen.get(detail) || 0) + 1
        seen.set(detail, nth)
        listed.push(`${detail}#${nth}:${(one.cpu_ns / 1e6).toFixed(1)}`)
        if (detail !== options.pass) continue
        if (nth === options.index) judged = one.cpu_ns / 1e6
        if (nth === options.baselineIndex) baseline = one.cpu_ns / 1e6
    }
    if (judged === null || baseline === null) {
        const missing = judged === null ? `#${options.index}` : `#${options.baselineIndex}`
        console.log(
            `opt-second-pass: REFUSED the run has no ${options.pass} ${missing} ` +
                `(its passes are ${listed.join(', ') || 'none'})`,
        )
        return 2
    }
    // A baseline this small is the clock and the host's jitter, not the pass: dividing by it would
    // report a large ratio for a run that did no work, so the reading is refused, not passed.
    if (baseline < MIN_BASELINE_CPU_MS) {
        console.log(
            `opt-second-pass: REFUSED the ${options.pass} #${options.baselineIndex} reads ` +
                `${baseline.toFixed(3)} ms of CPU, under the ${MIN_BASELINE_CPU_MS} ms a ratio needs: ` +
                `the division would measure the clock (its passes are ${listed.join(', ')})`,
        )
        return 2
    }

    const ratio = judged / baseline
    const verdict = ratio <= options.maxRatio ? 'PASS' : 'FAIL'
    if (options.json) {
        console.log(
            JSON.stringify(
                {
                    build,
                    case: path.relative(REPO_ROOT, casePath),
                    backend: options.backend,
                    pass: options.pass,
                    index: options.index,
                    baselineIndex: options.baselineIndex,
                    cpuMs: Number(judged.toFixed(1)),
                    baselineCpuMs: Number(baseline.toFixed(1)),
                    ratio: Number(ratio.toFixed(2)),
                    maxRatio: options.maxRatio,
                    totalCpuMs: Number(totalCpuMs.toFixed(1)),
                    passes: listed,
                    result: verdict,
                    exit: run.exit,
                },
                null,
                2,
            ),
        )
        return verdict === 'PASS' ? 0 : 1
    }

    // What the next expectation has to know: `case=` is a path of this repository, so it carries the
    // host's separator (`test\cases\...` on win-x64, `test/cases/...` on Linux). A plan's assertion
    // must therefore name the case by its file name, or hold no path at all -- one that pinned a
    // spelling was red on the other host over a line that read `ratio=6.93 max_ratio=14
    // result=PASS` (CI 44943488386), which is a red about a separator and not about a reading.
    console.log(`opt_second_pass build ${build}`)
    console.log(
        `opt_second_pass case=${path.relative(REPO_ROOT, casePath)} backend=${options.backend} ` +
            `pass=${options.pass} index=${options.index} baseline_index=${options.baselineIndex} ` +
            `cpu_ms=${judged.toFixed(1)} baseline_cpu_ms=${baseline.toFixed(1)} ratio=${ratio.toFixed(2)} ` +
            `max_ratio=${options.maxRatio} total_cpu_ms=${totalCpuMs.toFixed(1)} result=${verdict}`,
    )
    console.log(`opt_second_pass passes=${listed.join(' ')} exit=${run.exit}`)
    if (verdict === 'FAIL') {
        console.log(
            `opt-second-pass: FAILED the ${options.pass} #${options.index} of ` +
                `${path.relative(REPO_ROOT, casePath)} on ${options.backend} cost ` +
                `${judged.toFixed(1)} ms of CPU, ${ratio.toFixed(2)} times the ${baseline.toFixed(1)} ms ` +
                `of its #${options.baselineIndex} in the same run, against ${options.maxRatio} times ` +
                `(the run's total is ${totalCpuMs.toFixed(1)} ms; its passes are ${listed.join(', ')})`,
        )
        return 1
    }
    return 0
}

process.exitCode = main()