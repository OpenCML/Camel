// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The CPU time of one chosen pass of one pipeline, read out of the compiler's own `--time-phases`
// report and judged against a ceiling:
//
//   node test/tools/opt-cost/second_pass.mjs --case test/cases/... --ceiling 600
//   node test/tools/opt-cost/second_pass.mjs --case ... --ceiling 600 --pass std::opt --index 2
//
// Why a tool and why this shape. `perf_fib_smoke.opt_twice` runs `std::opt` twice, and the second
// run is where a program the optimizer cannot settle cheaply shows itself: on 2026-11-22 it cost
// 3592.1 ms of CPU against 179.5 for the first, on a program whose whole run is 3831.5 ms. The
// entry in `test/plans/feat/opt/opt_second_pass.plan.toml` pins that number so that the defect
// cannot leave the tests by the route the timing helper takes -- the helper's own shape was what
// made the program expensive, and rewriting the helper (2026-11-22) took the workload away. The
// case under test is the entry's business, not this file's: `--case` names it.
//
// The unit is CPU time (user + system) per pass, which is what `--time-phases` writes in `cpu_ns`
// and what a ceiling that should mean a regression is read against: a shared host stretches the
// wall clock of a CPU-bound pass several times over without the pass having used more CPU (the
// rule `test/tools/startup-cost.mjs` states). The tool prints the run's own pass list beside the
// judged number, so a failure can be read without re-running anything.
//
// It exits 0 when the pass is inside its ceiling and 1 when it is not; 2 means the reading could
// not be taken (no phases document, no such pass in the pipeline), which is a broken measurement
// rather than a regression and is named as such on standard output.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { takeCamel } from '../camel-arg.mjs'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..')

function usage(message) {
    if (message) console.error(`opt-second-pass: ${message}`)
    console.error(
        'usage: node test/tools/opt-cost/second_pass.mjs --case <path> --ceiling <ms> ' +
            '[--camel <path>] [--pass std::opt] [--index 2] [--backend jit] [--json]',
    )
    process.exit(2)
}

function parseArgs(args) {
    const options = { case: null, ceiling: null, pass: 'std::opt', index: 2, backend: 'jit', json: false }
    for (let i = 0; i < args.length; i++) {
        const arg = args[i]
        const value = args[i + 1]
        if (arg === '--json') options.json = true
        else if (arg === '--case') {
            if (value === undefined) usage('--case needs a path')
            options.case = value
            i += 1
        } else if (arg === '--ceiling') {
            if (value === undefined) usage('--ceiling needs milliseconds')
            options.ceiling = Number(value)
            if (!Number.isFinite(options.ceiling) || options.ceiling <= 0) usage(`--ceiling: '${value}' is not a positive number`)
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
        } else if (arg === '--backend') {
            if (value === undefined) usage('--backend needs a name')
            options.backend = value
            i += 1
        } else usage(`unknown argument '${arg}'`)
    }
    if (options.case === null) usage('--case is required')
    if (options.ceiling === null) usage('--ceiling is required')
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

    const passes = ['--time-phases', casePath]
    for (let run = 1; run <= options.index; run++) passes.push(options.pass)
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
    for (const one of top) {
        if (one.name !== 'pass') continue
        const detail = one.detail || one.name
        const nth = (seen.get(detail) || 0) + 1
        seen.set(detail, nth)
        listed.push(`${detail}#${nth}:${(one.cpu_ns / 1e6).toFixed(1)}`)
        if (detail === options.pass && nth === options.index) judged = one.cpu_ns / 1e6
    }
    if (judged === null) {
        console.log(
            `opt-second-pass: REFUSED the run has no ${options.pass} #${options.index} ` +
                `(its passes are ${listed.join(', ') || 'none'})`,
        )
        return 2
    }

    const verdict = judged <= options.ceiling ? 'PASS' : 'FAIL'
    if (options.json) {
        console.log(
            JSON.stringify(
                {
                    build,
                    case: path.relative(REPO_ROOT, casePath),
                    backend: options.backend,
                    pass: options.pass,
                    index: options.index,
                    cpuMs: Number(judged.toFixed(1)),
                    ceilingMs: options.ceiling,
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

    console.log(`opt_second_pass build ${build}`)
    console.log(
        `opt_second_pass case=${path.relative(REPO_ROOT, casePath)} backend=${options.backend} ` +
            `pass=${options.pass} index=${options.index} cpu_ms=${judged.toFixed(1)} ` +
            `ceiling_ms=${options.ceiling} total_cpu_ms=${totalCpuMs.toFixed(1)} result=${verdict}`,
    )
    console.log(`opt_second_pass passes=${listed.join(' ')} exit=${run.exit}`)
    if (verdict === 'FAIL') {
        console.log(
            `opt-second-pass: FAILED the ${options.pass} #${options.index} of ` +
                `${path.relative(REPO_ROOT, casePath)} on ${options.backend} cost ` +
                `${judged.toFixed(1)} ms of CPU against a ceiling of ${options.ceiling} ms ` +
                `(the run's total is ${totalCpuMs.toFixed(1)} ms; its passes are ${listed.join(', ')})`,
        )
        return 1
    }
    return 0
}

process.exitCode = main()