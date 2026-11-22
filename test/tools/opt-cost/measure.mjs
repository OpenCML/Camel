// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Where the time of `perf_fib_smoke.opt_twice` goes, measured as shares **of the same run**:
//
//   node test/tools/opt-cost/measure.mjs                 measure every backend, three pipelines
//   node test/tools/opt-cost/measure.mjs --backends jit   one backend
//   node test/tools/opt-cost/measure.mjs --json           the numbers, as one JSON document
//
// Why this tool exists. `opt.splice_place / perf_fib_smoke.opt_twice.{nvm,fvm,jit,prl}` crossed
// the runner's cost line (`timeout_ms / 4`, `TIMEOUT_MARGIN` in test/tools/run-tests.mjs) on all
// four back ends at once -- 6480 / 6410 / 6550 / 6440 ms of CPU against 5000 -- and the reading the
// author asked for is not "raise the line" but "measure where the slow-down is, and report each
// check's share" (internal/reviews/questions-2026-10-29.md, the dispatch of 2026-11-19 to the
// 0.2.45 blocker list, S3-2). A single absolute figure is not that reading: a millisecond on a
// shared host is not evidence (the rule `test/tools/para-speedup.mjs` states and this tool follows).
//
// What it measures, and what each number means:
//
//   * `--time-phases` (`docs/cli.md`) writes one JSON object to standard error with the wall and
//     CPU time of every phase of the run -- start-up, parse, GCT, **each pass**, teardown. The
//     passes are the window this tool needs: `std::opt` twice is the test, so the two `pass`
//     entries are the optimizer's own cost and everything else is the program and its compile.
//     `total` is the sum of the depth-0 phases, which do not overlap; a nested phase is inside its
//     parent and is never added.
//   * the same program through three pipelines of the *same* run: the reference (`<backend>` only,
//     the plan's `perf_fib_smoke.reference`), one `std::opt` (`perf_fib_smoke.opt`) and two
//     (`perf_fib_smoke.opt_twice`, the red entry). The share of the red entry's CPU that the two
//     optimizer passes take is a ratio inside one run, so the host's load hits both sides alike --
//     which is why a share is read and an absolute wall is not.
//   * `--trace effects` and `-vv` count **how many times** the whole-program checks are asked in
//     one `std::opt` run: one `effect containment: checked N call site(s)` line per pass-boundary
//     question (`src/kernel/execute/pass.cpp#passBoundaryViolation`, asked per rewrite session by
//     `src/passes/opt/inline/engine.cpp`, since 2026-11-18) and one `boundary asked after session`
//     line per session the engine completed. A count is not a duration, but it is what turns "the
//     check is 30% of the pass" into "the check is asked N times per pass over the whole program",
//     and it is independent of the host.
//
// The tool judges nothing: it prints the reading and exits 1, the way the fixtures under
// `test/tools/selftest/` do. `test/tools/opt-cost/measure.plan.toml` is the entry an explicit
// `-Plan` runs; it declares `expect_exit = 1`, the exit this file gives a reading, and its
// verification refuses on purpose, so a run of the plan reports the table that was printed.
// This file is not under `test/plans/`, so no tier selects it by itself.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { takeCamel } from '../camel-arg.mjs'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..')
const CASE = path.join(REPO_ROOT, 'test', 'cases', 'linear', 'jit', 'perf_fib_smoke.cml')

// The three pipelines the plan runs on this case, in the order it runs them.
const PIPELINES = [
    { label: 'reference', passes: [] },
    { label: 'opt', passes: ['std::opt'] },
    { label: 'opt_twice', passes: ['std::opt', 'std::opt'] },
]

const BACKENDS = ['nvm', 'fvm', 'jit', 'prl']

function usage(message) {
    if (message) console.error(`opt-cost: ${message}`)
    console.error('usage: node test/tools/opt-cost/measure.mjs [--backends nvm,fvm,jit,prl] [--json] [--camel <path>]')
    process.exit(2)
}

function parseArgs(args) {
    const options = { backends: BACKENDS.slice(), json: false }
    for (let i = 0; i < args.length; i++) {
        const arg = args[i]
        if (arg === '--json') options.json = true
        else if (arg === '--backends') {
            const value = args[++i]
            if (value === undefined) usage('--backends needs a comma-separated list')
            options.backends = value.split(',').map((one) => one.trim()).filter((one) => one.length > 0)
            for (const one of options.backends) {
                if (!BACKENDS.includes(one)) usage(`unknown backend '${one}' (known: ${BACKENDS.join(', ')})`)
            }
        } else usage(`unknown argument '${arg}'`)
    }
    return options
}

// The install root a `camel` binary belongs to: `<root>/bin/camel` -> `<root>`.
function installOf(camel) {
    return path.dirname(path.dirname(camel))
}

function runInvocation(camel, argv) {
    const startedAt = process.hrtime.bigint()
    const run = spawnSync(camel, argv, {
        cwd: REPO_ROOT,
        stdio: ['ignore', 'pipe', 'pipe'],
        encoding: 'utf8',
        timeout: 300000,
        maxBuffer: 256 * 1024 * 1024,
        env: { ...process.env, CAMEL_HOME: installOf(camel) },
    })
    const wallMs = Number(process.hrtime.bigint() - startedAt) / 1e6
    return {
        exit: run.status === null ? -1 : run.status,
        stdout: run.stdout || '',
        stderr: run.stderr || '',
        wallMs,
        failed: run.error !== undefined,
    }
}

// The `--time-phases` document: one JSON object written to standard error, after whatever the run
// had to say. `phases` holds every phase, nested ones included, each with its own wall and CPU time.
function parsePhases(stderr) {
    const at = stderr.indexOf('{"schema"')
    if (at < 0) return null
    try {
        return JSON.parse(stderr.slice(at))
    } catch {
        return null
    }
}

// The depth-0 phases do not overlap, so their CPU times add up to the run's own. A `pass` phase is
// one pass of the pipeline; the pipeline's own steps are inside it.
function summarize(doc) {
    const top = doc.phases.filter((one) => one.depth === 0)
    const passes = top.filter((one) => one.name === 'pass')
    return {
        totalCpuMs: top.reduce((sum, one) => sum + one.cpu_ns, 0) / 1e6,
        totalWallMs: top.reduce((sum, one) => sum + one.wall_ns, 0) / 1e6,
        passes: passes.map((one) => ({
            name: one.detail || one.name,
            cpuMs: one.cpu_ns / 1e6,
            wallMs: one.wall_ns / 1e6,
        })),
    }
}

// How many whole-program questions the optimizer's one run asks, from two runs and for a reason: the
// log counts (`-vv`) and the trace counts (`--trace effects`) are different readings of the same run,
// and a build that accepts one but not the other must not read as "the check was never asked". Each
// count is taken from wherever the binary wrote it, so a stream that moved does not read as zero.
function countChecks(camel, backend) {
    // The scope list keeps the log to the two scopes the counts come from: the whole run at `-vv` is
    // tens of megabytes, and a reading that costs a buffer overflow reads as a smaller number.
    const logged = runInvocation(camel, [
        '-vv',
        '--log-include',
        'DraftSession,Opt',
        CASE,
        'std::opt',
        'std::opt',
        `std::${backend}`,
    ])
    const traced = runInvocation(camel, ['--trace', 'effects', CASE, 'std::opt', 'std::opt', `std::${backend}`])
    const logText = `${logged.stdout}\n${logged.stderr}`
    const traceText = `${traced.stdout}\n${traced.stderr}`
    const sessions = (logText.match(/Decoded runtime graph '[^']*' into editable draft/g) || []).length
    const boundaryAsks = (logText.match(/boundary asked after session /g) || []).length
    // One line per sub-pass of the optimizer, with the rewrites it made: `std::cxx::fold: 16
    // rewrite(s)` (the name is the pass's own, so it is read as written and not by a fixed prefix --
    // `std::opt::fold` before 2026-11-18, `std::cxx::fold` since). Each of those is a draft commit,
    // which is what the commit checks are asked of.
    const rewrites = new Map()
    for (const match of logText.matchAll(/<Opt> (\S+): (\d+) rewrite\(s\)/g)) {
        rewrites.set(match[1], (rewrites.get(match[1]) || 0) + Number(match[2]))
    }
    let containmentCalls = 0
    let containmentSites = 0
    let containmentDeclined = 0
    for (const match of traceText.matchAll(/effect containment: checked (\d+) call site\(s\), declined (\d+)/g)) {
        containmentCalls += 1
        containmentSites += Number(match[1])
        containmentDeclined += Number(match[2])
    }
    return {
        exit: logged.exit,
        traceExit: traced.exit,
        logRead: sessions > 0 || boundaryAsks > 0,
        traceRead: containmentCalls > 0,
        drafts: sessions,
        boundaryAsks,
        rewrites: [...rewrites].sort((one, two) => one[0].localeCompare(two[0])),
        rewriteTotal: [...rewrites.values()].reduce((sum, one) => sum + one, 0),
        containmentCalls,
        containmentSites,
        containmentDeclined,
    }
}

function pad(text, width) {
    return String(text).padEnd(width)
}

function fixed(ms, width = 9) {
    return (Number.isFinite(ms) ? ms.toFixed(1) : '-').padStart(width)
}

function main() {
    let options
    let camel
    try {
        const named = takeCamel(process.argv.slice(2), REPO_ROOT, usage)
        camel = named.camel
        options = parseArgs(named.rest)
    } catch (error) {
        console.error(`opt-cost: ${error.message}`)
        return 2
    }
    if (!fs.existsSync(camel)) {
        console.log(`opt-cost: REFUSED no camel binary at ${camel}`)
        return 2
    }
    if (!fs.existsSync(CASE)) {
        console.log(`opt-cost: REFUSED no case at ${CASE}`)
        return 2
    }

    // The build the numbers belong to, on the first line of the reading: a table without the commit
    // it was taken on is a table a later reader has to guess the tree of.
    const version = runInvocation(camel, ['--version'])
    const build = `${version.stdout}\n${version.stderr}`
        .split('\n')
        .map((one) => one.trim())
        .find((one) => one.length > 0) || '<no --version output>'

    const rows = []
    const troubles = []
    for (const backend of options.backends) {
        for (const pipeline of PIPELINES) {
            const argv = ['--time-phases', CASE, ...pipeline.passes, `std::${backend}`]
            const run = runInvocation(camel, argv)
            const doc = parsePhases(run.stderr)
            if (doc === null) {
                troubles.push(`${backend}/${pipeline.label}: no --time-phases document (exit ${run.exit})`)
                rows.push({ backend, pipeline: pipeline.label, exit: run.exit, phases: null })
                continue
            }
            const phases = summarize(doc)
            const opt = phases.passes.filter((one) => one.name === 'std::opt')
            const optCpu = opt.reduce((sum, one) => sum + one.cpuMs, 0)
            rows.push({
                backend,
                pipeline: pipeline.label,
                exit: run.exit,
                totalCpuMs: Number(phases.totalCpuMs.toFixed(1)),
                totalWallMs: Number(phases.totalWallMs.toFixed(1)),
                optCpuMs: Number(optCpu.toFixed(1)),
                optShare: phases.totalCpuMs > 0 ? Number((optCpu / phases.totalCpuMs).toFixed(3)) : null,
                passes: phases.passes.map((one) => ({ name: one.name, cpuMs: Number(one.cpuMs.toFixed(1)) })),
            })
        }
    }

    // The counts are asked of one back end: the number of sessions and of boundary questions is a
    // property of the optimizer's run, not of the scheduler it hands its graph to.
    const countBackend = options.backends.includes('jit') ? 'jit' : options.backends[0]
    const counts = countChecks(camel, countBackend)

    if (options.json) {
        console.log(JSON.stringify({ camel, build, case: path.relative(REPO_ROOT, CASE), rows, counts, troubles }, null, 2))
        return 1
    }

    console.log(`opt-cost camel=${path.relative(REPO_ROOT, camel)} case=${path.relative(REPO_ROOT, CASE)}`)
    console.log(`  build ${build}`)
    // One space between the columns, so a `-` and a `0.0` cannot be read as one number.
    const cells = (values) => `  ${values.map((one) => pad(one, 10)).join('')}`
    console.log(cells(['backend', 'pipeline', 'exit', 'cpu_ms', 'wall_ms', 'opt_ms', 'opt_share']) + 'pass(m)')
    for (const row of rows) {
        if (row.phases === null) {
            console.log(cells([row.backend, row.pipeline, row.exit]) + '(no phases document)')
            continue
        }
        console.log(
            cells([
                row.backend,
                row.pipeline,
                row.exit,
                fixed(row.totalCpuMs),
                fixed(row.totalWallMs),
                fixed(row.optCpuMs),
                row.optShare === null ? '-' : `${(row.optShare * 100).toFixed(1)}%`,
            ]) + row.passes.filter((one) => one.name === 'std::opt').map((one) => one.cpuMs.toFixed(1)).join(', '),
        )
    }
    // The two optimizer passes of the red entry against the one-pass entry: the difference is what
    // the second pass costs, and it is the number a "run the pass twice" entry pays twice.
    for (const backend of options.backends) {
        const once = rows.find((one) => one.backend === backend && one.pipeline === 'opt')
        const twice = rows.find((one) => one.backend === backend && one.pipeline === 'opt_twice')
        const base = rows.find((one) => one.backend === backend && one.pipeline === 'reference')
        if (!once || !twice || !base || once.phases === null || twice.phases === null || base.phases === null) continue
        console.log(
            `  second pass ${pad(backend, 8)} +${fixed(twice.optCpuMs - once.optCpuMs, 6)} ms of cpu on an ` +
                `already optimized graph; the two passes are ${(100 * twice.optCpuMs / twice.totalCpuMs).toFixed(1)}% ` +
                `of the entry's ${twice.totalCpuMs.toFixed(1)} ms, against ${(100 * base.totalCpuMs / twice.totalCpuMs).toFixed(1)}% ` +
                `for the reference run`,
        )
    }
    console.log(
        `  checks(${countBackend}) drafts=${counts.drafts} boundary_asks=${counts.boundaryAsks} ` +
            `rewrites=${counts.rewriteTotal}` +
            (counts.rewrites.length > 0 ? ` (${counts.rewrites.map(([name, count]) => `${name.split('::').pop()}:${count}`).join(', ')})` : '') +
            ` containment_calls=${counts.containmentCalls} call_sites=${counts.containmentSites} ` +
            `declined=${counts.containmentDeclined}` +
            (counts.logRead ? '' : ` [no -vv lines: exit ${counts.exit}]`) +
            (counts.traceRead ? '' : ` [no --trace effects lines: exit ${counts.traceExit}]`),
    )
    for (const trouble of troubles) console.log(`  trouble: ${trouble}`)
    // A reading, not a verdict: the plan that runs this expects exit 0 and therefore prints the
    // table above as its failure.
    return 1
}

process.exitCode = main()