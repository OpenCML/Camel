// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The parallel-versus-serial speed-up of the cheap parallel workload, asserted as a property of one
// run (the author's ruling of 2026-10-23, docs/handover/reviews/questions-2026-10-23.md,
// "perf-para-gate"):
//
//   "选 B：把已量过的便宜子集（monte_carlo_pi.prl，约 1.6 s）立成可进闸门的计划。度量取同一次运行里
//    并行对串行的加速比（与绝对墙钟相比对负载不那么敏感），并记 CPU 时间；整条 perf.para 仍在发布前跑。"
//
// The measured quantity is therefore the ratio of the two sides **of the same run**, not a stored
// baseline, and CPU time is recorded beside it. The expensive full `perf.para` stays where it is
// (before a release); this tool prices the one case the ruling names, `monte_carlo_pi`.
//
//   node test/tools/para-speedup.mjs                  measure both sides and gate the property
//   node test/tools/para-speedup.mjs --json           the numbers, without the gate
//   node test/tools/para-speedup.mjs --rounds 2       two interleaved rounds instead of one
//   node test/tools/para-speedup.mjs --require-cores  the host precondition (the plan's skip_unless)
//   node test/tools/para-speedup.mjs --min-cores 9999 the refusal a host that cannot show the
//                                                     property gets (the plan's second test)
//
// What it runs, exactly as `test/plans/perf/para.plan.toml` runs that case: the same program, timed
// by `--timeit <pass>/1`, once on the plan's own serial side (`std::nvm`, the baseline NVM scheduler
// that plan pairs with `std::prl`) and once on its parallel side (`std::prl`), with no `-t` on
// either side, so the parallel side's width is the machine's core count -- the plan the ruling keeps
// for a release passes none either. The pass window `--timeit` prints excludes the compile of both
// sides, so the two walls are the same window over the same workload; the workload's own receipt
// (`Running N times`, `Samples per simulation`, `Total hits`) is read too and must be identical on
// both sides, so a speed-up of two different programs cannot be reported.
//
// The property, and what it is not:
//
//   * speed-up: the serial pass wall divided by the parallel pass wall, both measured in this run.
//     The guard trusts this **ratio**, and only this ratio: the host's load hits both sides in the
//     same window, which is why the ruling chose it over an absolute wall clock. It trusts neither
//     wall as a work number and gates no absolute wall -- a millisecond read on a shared host is not
//     evidence (the same ruling, "declaration-cost-gate" (a)). The floor is 1.5 rather than a quiet
//     host's reading: the ratio is the parallel side's average core count over the serial side's, so
//     on a host with C cores the quiet reading is about C, and 1.5 is the point where "more than one
//     core did the work" is beyond argument. Measured on the reference host (AMD EPYC 7343, 32
//     threads) on 2026-10-23: 14.33 at load 58.6/72.1/79.3 and 16.18 at 94.5/80.6/80.0 -- the two
//     readings differ by 13% where the load differs by 20%, which is the insensitivity the ruling
//     bought. The noise of a single round is known too: the planted red ran the identical command
//     twice (73,126.2 ms and 67,448.1 ms, both `--timeit std::nvm/1`, at load 87) and the ratio of
//     two identical measurements came out 1.08, so one round carries about 8% of noise, against a
//     reading ten times above its floor.
//   * cores: the average number of cores each side really used, which is the child's CPU time
//     (user + system) divided by its own process wall -- a number the host's load cannot inflate,
//     only deflate. The parallel side must be above 1.5 and the serial side at or below 1.25: that
//     is the ruling's "the parallel side must actually use more than one core while the serial side
//     does not". Measured in the same runs: the parallel side 7.10 and 6.66, the serial side 0.50
//     and 0.41 -- one thread getting half a core or less, with the host's load at 58-95 on 32 cores.
//     The pair is what catches a parallel side that ran on one thread: the speed-up alone cannot,
//     because a one-thread scheduler of another back end can still be faster than `std::nvm` -- on
//     this workload it is not (the native kernel is nearly all of the wall), so the planted
//     serial-only run reds both lines (speedup 1.08, cores 0.47), and the core line is the one that
//     cannot be fooled by a faster single thread.
//   * the CPU time of each side is **reported, not gated**. Child CPU time is the work number (the
//     2026-10-23 ruling), but a ceiling on it would be a number taken from one quiet host's day,
//     which is exactly the stored-baseline measurement the ruling replaced. Both sides cost the same
//     CPU time here (31,660.0 ms against 32,040.0 ms): that agreement is the evidence that the ratio
//     compares the same work, and it is printed for a reader rather than asserted.
//
// The slack, said out loud: at load 94.5 the parallel side read 6.66 cores against the 1.5 floor, so
// the host may be about 4.4 times busier still before this line goes red, and the speed-up line
// (16.18 against 1.5) has ten times its margin. A host whose load leaves the parallel side under 1.5
// cores on average is a host whose reading is not evidence -- the line says so when it reds -- and
// that is the honest ceiling of a shared box, not a reason to lower the floor.
//
// A host with fewer usable cores is a decision, not an accident: below `--min-cores` (4) the
// parallel side cannot be shown to use more than one core with any margin -- the quiet reading is
// the core count itself, so 4 cores leave 2.7x room and 2 leave none -- and the plan's `skip_unless`
// turns that into a **skip with the reason printed**, never a pass: a check that examined nothing
// must not report one. The gate mode refuses there as well (exit 2, naming the host), in case the
// tool is run without the precondition. `--min-cores` names the requirement, which is how the
// refusal is demonstrated on a host that has enough cores.
//
// The child's CPU time is read the way scripts/perf-gate.mjs and test/tools/startup-cost.mjs read
// it: the delta of `cutime` + `cstime` in /proc/self/stat around the child, in 10 ms ticks. A
// platform that cannot account a child's CPU time has no reading for the core property, so that is
// an error here, never a fall back to the wall clock.
//
// One reading is deliberately not offered: there is no switch that pins the parallel side to one
// thread. `-t`/`--threads` would be the obvious one, and it does not reach the pass under
// `--timeit` -- the context `--timeit` builds carries the recursion and macro limits but not
// `maxThreads` (tools/camel-cli/timeit.cpp, next to tools/camel-cli/main.cpp, which sets it). A
// switch that silently measured the machine's whole width would be worse than none; the red this
// guard is trusted against was planted by running both sides serially instead.

import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { takeCamel } from './camel-arg.mjs'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const DEFAULT_CASE = path.join(REPO_ROOT, 'test', 'cases', 'para', 'core', 'monte_carlo_pi.cml')

// The plan's own two sides (test/plans/perf/para.plan.toml): the baseline NVM scheduler and the PRL
// scheduler, each timed by `--timeit <pass>/1` over the same program.
const SERIAL_BACKEND = 'std::nvm'
const PARALLEL_BACKEND = 'std::prl'

// /proc/self/stat reports children in USER_HZ ticks, which is 100 on every Linux architecture.
const CLOCK_TICKS = 100
// The cores the property needs (see the header): below this the plan skips with the reason printed.
const MIN_CORES = 4
// The two floors and the serial side's ceiling: 1.5 is "more than one core did the work", the 1.25
// is one thread plus the accounting noise of an otherwise idle process.
const SPEEDUP_FLOOR = 1.5
const CONCURRENCY_FLOOR = 1.5
const SERIAL_CONCURRENCY_CEILING = 1.25
// Below this the pass window is too short for a ratio to mean anything (the same 250 ms rule
// scripts/perf-gate.mjs applies to a child's CPU time): the workload did not run.
const MIN_PASS_WALL_MS = 250
// One round. The serial side of this workload costs about a minute on the reference host (63.5 s to
// 77.2 s, the share of a loaded host it gets), so a second round doubles a measurement whose floors
// are already 4.4x and 10.8x away from their reading while a single round's noise is 8%; `--rounds`
// is there for a deeper look, and the plan does not ask for one.
const DEFAULT_ROUNDS = 1
// A child that has not finished in ten minutes is not measuring a busy host, it is hung: the plan's
// own timeout (900 s) is the backstop behind this one, so the reason a reader sees is this message.
const CHILD_TIMEOUT_MS = 600000

// The install this run measures: `--camel <path>` names it (main parses the argument), else
// `$CAMEL_HOME/bin/camel`, else this tree's `out/latest/bin/camel`. One rule, in one place:
// test/tools/camel-arg.mjs.
function usage(message) {
    if (message) console.error(`para-speedup: ${message}`)
    console.error(
        'usage: node test/tools/para-speedup.mjs [--json] [--require-cores] [--rounds N] ' +
            '[--min-cores N] [--case FILE] [--camel PATH]'
    )
    process.exit(2)
}

// The cores this process may really use: `os.availableParallelism` follows the CPU affinity mask and
// a cgroup quota on Linux, which `os.cpus().length` does not (a gate started under `taskset` or in a
// container would otherwise claim the machine's whole count).
function usableCores() {
    if (typeof os.availableParallelism === 'function') {
        return os.availableParallelism()
    }
    return os.cpus().length
}

// The CPU time of every child reaped so far, in seconds, as RUSAGE_CHILDREN reports it: /proc
// self/stat fields 16 and 17. Null on a platform that has no such accounting -- and a null is an
// error here, never a reason to read the wall clock instead.
function childrenCpuSeconds() {
    if (process.platform !== 'linux') {
        return null
    }
    try {
        const stat = fs.readFileSync('/proc/self/stat', 'utf8')
        // The command name (field 2) is in parentheses and may contain spaces, so the fields are
        // counted from the last ')'. The first one is field 3 (state), which makes cutime field 16
        // index 13 and cstime field 17 index 14.
        const fields = stat.slice(stat.lastIndexOf(')') + 2).trim().split(/\s+/)
        const cutime = Number(fields[13])
        const cstime = Number(fields[14])
        if (!Number.isFinite(cutime) || !Number.isFinite(cstime)) {
            return null
        }
        return (cutime + cstime) / CLOCK_TICKS
    } catch {
        return null
    }
}

function cpuUnavailableReason() {
    return (
        `the CPU time of a child cannot be read on this platform (${process.platform}): the core ` +
        'property is read from it, so this measurement has no reading here ' +
        '(docs/handover/reviews/questions-2026-10-23.md, perf-para-gate)'
    )
}

function relative(p) {
    const rel = path.relative(REPO_ROOT, p)
    return rel.startsWith('..') ? p : rel
}

function displayArgv(argv) {
    const camel = relative(argv[0])
    const rest = argv.slice(1).map((token) => (token.startsWith(REPO_ROOT) ? relative(token) : token))
    return [camel, ...rest].join(' ')
}

// The median, the smallest and the largest of a sample. With an even count the median is the mean of
// the two middle samples; `n` is printed beside it so a reader knows what it was.
function summarize(samples) {
    const sorted = [...samples].sort((a, b) => a - b)
    const middle = sorted.length >> 1
    const median =
        sorted.length % 2 === 1 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2
    return { n: sorted.length, median, min: sorted[0], max: sorted[sorted.length - 1] }
}

// One side's command, exactly as the plan writes it: `camel --timeit <pass>/1 <case> <pass>`.
function invocation(camel, backend, casePath) {
    return [camel, '--timeit', `${backend}/1`, casePath, backend]
}

// The pass window `--timeit` timed: `[timeit] i/N pass=std::prl elapsed=1234.5678 ms`. This is the
// window the speed-up reads; it excludes the compile, which both sides pay outside it.
function parsePassWallMs(stdout) {
    const match = /^\[timeit\] (\d+)\/(\d+) pass=(\S+) elapsed=([0-9.]+) ms$/m.exec(stdout)
    if (match === null) {
        return null
    }
    const ms = Number(match[4])
    return Number.isFinite(ms) && ms > 0 ? ms : null
}

// What the workload itself reports (test/cases/para/core/monte_carlo_pi.cml): how many repetitions
// and samples it ran, and what it computed. Two sides that did not run the same workload cannot be
// compared, and this receipt is the only place that says so.
function parseWorkload(stdout) {
    const sizes = /^Samples per simulation: (\d+), total samples: (\d+)$/m.exec(stdout)
    const hits = /^Total hits = (\d+)$/m.exec(stdout)
    const repeat = /^Running (\d+) times of Monte Carlo simulation\.\.\.$/m.exec(stdout)
    if (sizes === null || hits === null || repeat === null) {
        return null
    }
    return {
        repeat: Number(repeat[1]),
        samplesPerWorker: Number(sizes[1]),
        totalSamples: Number(sizes[2]),
        hits: hits[1],
    }
}

// One fresh `camel` process, with the child's own CPU time (user + system) and its process wall read
// around it, and the pass window and workload receipt parsed out of its stdout.
function runInvocation(argv) {
    const before = childrenCpuSeconds()
    if (before === null) {
        throw new Error(cpuUnavailableReason())
    }
    const startedAt = process.hrtime.bigint()
    const run = spawnSync(argv[0], argv.slice(1), {
        cwd: REPO_ROOT,
        stdio: ['ignore', 'pipe', 'pipe'],
        encoding: 'utf8',
        maxBuffer: 1 << 26,
        timeout: CHILD_TIMEOUT_MS,
    })
    const wallMs = Number(process.hrtime.bigint() - startedAt) / 1e6
    const after = childrenCpuSeconds()
    if (run.error) {
        throw new Error(`could not start ${argv[0]}: ${run.error.message}`)
    }
    if (run.signal) {
        throw new Error(`${displayArgv(argv)} was killed by ${run.signal}`)
    }
    if (run.status !== 0) {
        const detail = `${(run.stdout || '').trim()} ${(run.stderr || '').trim()}`.trim()
        throw new Error(`${displayArgv(argv)} exited ${run.status}: ${detail.slice(-500)}`)
    }
    if (after === null) {
        throw new Error(cpuUnavailableReason())
    }
    if (after <= before) {
        throw new Error(`${displayArgv(argv)} was charged no CPU time: the reading examined nothing`)
    }
    const passWallMs = parsePassWallMs(run.stdout)
    if (passWallMs === null) {
        throw new Error(
            `${displayArgv(argv)} printed no "[timeit] i/N pass=... elapsed=" line: there is no ` +
                'pass window to read the speed-up from'
        )
    }
    const workload = parseWorkload(run.stdout)
    if (workload === null) {
        throw new Error(
            `${displayArgv(argv)} printed no workload receipt (Running N times / Samples per ` +
                'simulation / Total hits): the two sides cannot be shown to be the same workload'
        )
    }
    return {
        passWallMs,
        cpuMs: (after - before) * 1000,
        wallMs,
        concurrency: ((after - before) * 1000) / wallMs,
        workload,
    }
}

function side(argv, samples) {
    return {
        argv,
        samples,
        passWall: summarize(samples.map((one) => one.passWallMs)),
        cpu: summarize(samples.map((one) => one.cpuMs)),
        wall: summarize(samples.map((one) => one.wallMs)),
        concurrency: summarize(samples.map((one) => one.concurrency)),
    }
}

function parseArgs(args, camel) {
    const options = {
        json: false,
        requireCores: false,
        rounds: DEFAULT_ROUNDS,
        minCores: MIN_CORES,
        casePath: DEFAULT_CASE,
        camel,
    }
    for (let i = 0; i < args.length; i++) {
        const at = args[i]
        if (at === '--json') {
            options.json = true
        } else if (at === '--require-cores') {
            options.requireCores = true
        } else if (at === '--rounds') {
            options.rounds = Number(args[++i])
        } else if (at === '--min-cores') {
            options.minCores = Number(args[++i])
        } else if (at === '--case') {
            options.casePath = path.resolve(args[++i])
        } else {
            throw new Error(`unknown argument ${at}`)
        }
    }
    if (!Number.isInteger(options.rounds) || options.rounds < 1) {
        throw new Error('--rounds needs a positive integer')
    }
    if (!Number.isInteger(options.minCores) || options.minCores < 2) {
        throw new Error('--min-cores needs an integer of at least 2')
    }
    return options
}

function loadText() {
    return os.loadavg().map((value) => value.toFixed(1)).join('/')
}

function hostLine(options) {
    return (
        `para.speedup metric=pass_wall_and_child_cpu host=${os.hostname()} platform=${process.platform} ` +
        `cores=${usableCores()} cores_os=${os.cpus().length} min_cores=${options.minCores} ` +
        `load=${loadText()} case=${relative(options.casePath)} rounds=${options.rounds}`
    )
}

function sideLine(label, measured, backend) {
    return (
        `para.speedup ${label} backend=${backend} threads=${usableCores()} (the machine's cores, no ` +
        `-t passed) pass_wall_ms=${measured.passWall.median.toFixed(1)} ` +
        `pass_wall_min_ms=${measured.passWall.min.toFixed(1)} ` +
        `pass_wall_max_ms=${measured.passWall.max.toFixed(1)} ` +
        `cpu_ms=${measured.cpu.median.toFixed(1)} proc_wall_ms=${measured.wall.median.toFixed(1)} ` +
        `concurrency=${measured.concurrency.median.toFixed(2)} n=${measured.passWall.n} ` +
        `invocation="${displayArgv(measured.argv)}"`
    )
}

// The precondition the plan runs first: the host's usable cores, and whether the property can be
// read here at all. A non-zero exit skips the test with this text as the reason (docs/test-plans.md,
// "Running The Tests"), which is why it says what the number would have been read from.
function requireCores(options) {
    const cores = usableCores()
    if (cores >= options.minCores) {
        console.log(
            `para.speedup precondition cores=${cores} cores_os=${os.cpus().length} ` +
                `need=${options.minCores} result=OK: the parallel side can be shown to use more ` +
                'than one core here'
        )
        return 0
    }
    console.log(
        `para.speedup precondition cores=${cores} cores_os=${os.cpus().length} ` +
            `need=${options.minCores} result=SKIP: this host cannot show that the parallel side ` +
            `used more than one core with any margin (the quiet reading would be ${cores}, against ` +
            `the ${CONCURRENCY_FLOOR} floor), so the speed-up of this run is not evidence either ` +
            'way. The test is skipped, not passed.'
    )
    return 1
}

function main() {
    let options
    const named = takeCamel(process.argv.slice(2), REPO_ROOT, usage)
    try {
        options = parseArgs(named.rest, named.camel)
    } catch (error) {
        console.error(`para-speedup: ${error.message}`)
        return 2
    }
    if (options.requireCores) {
        return requireCores(options)
    }
    const cores = usableCores()
    if (cores < options.minCores) {
        // Never a pass: the property was not examined (the plan's skip_unless is the skip).
        console.log(
            `para.speedup: REFUSED this host has ${cores} usable core(s), the property needs ` +
                `${options.minCores}: the parallel side cannot be shown to use more than one core, ` +
                'and this check examined nothing'
        )
        return 2
    }
    if (!fs.existsSync(options.camel)) {
        console.log(`para.speedup: REFUSED no camel binary at ${options.camel}`)
        return 2
    }
    if (!fs.existsSync(options.casePath)) {
        console.log(`para.speedup: REFUSED no case at ${options.casePath}`)
        return 2
    }

    const serialArgv = invocation(options.camel, SERIAL_BACKEND, options.casePath)
    const parallelArgv = invocation(options.camel, PARALLEL_BACKEND, options.casePath)

    // Interleaved round by round with the order rotated, so a load that drifts during the run hits
    // both sides alike instead of the side that always ran second.
    const serialSamples = []
    const parallelSamples = []
    try {
        for (let round = 0; round < options.rounds; round++) {
            if (round % 2 === 0) {
                serialSamples.push(runInvocation(serialArgv))
                parallelSamples.push(runInvocation(parallelArgv))
            } else {
                parallelSamples.push(runInvocation(parallelArgv))
                serialSamples.push(runInvocation(serialArgv))
            }
        }
    } catch (error) {
        // A side that could not be measured is the whole measurement's result. It goes to standard
        // output with the other lines: a reason on standard error is one the gate's log does not keep.
        console.log(hostLine(options))
        console.log(`para.speedup: FAILED ${error.message}`)
        return 1
    }

    const serial = side(serialArgv, serialSamples)
    const parallel = side(parallelArgv, parallelSamples)
    const failures = []

    // The same workload on both sides, or the ratio measures nothing: the receipt the program
    // printed (repetitions, samples, hits) must agree across every invocation of both sides.
    const receipts = [...serial.samples, ...parallel.samples].map((one) => one.workload)
    const first = receipts[0]
    for (const one of receipts) {
        if (
            one.repeat !== first.repeat ||
            one.samplesPerWorker !== first.samplesPerWorker ||
            one.totalSamples !== first.totalSamples ||
            one.hits !== first.hits
        ) {
            failures.push(
                'the two sides did not run the same workload: the receipts differ ' +
                    `(${first.repeat}x${first.samplesPerWorker} hits=${first.hits} against ` +
                    `${one.repeat}x${one.samplesPerWorker} hits=${one.hits})`
            )
            break
        }
    }
    if (serial.passWall.median < MIN_PASS_WALL_MS) {
        failures.push(
            `the serial side's pass window is ${serial.passWall.median.toFixed(1)} ms, under the ` +
                `${MIN_PASS_WALL_MS} ms the ratio needs: the workload did not run`
        )
    }
    const speedup = serial.passWall.median / parallel.passWall.median
    if (speedup < SPEEDUP_FLOOR) {
        failures.push(
            `speedup ${speedup.toFixed(2)} is under ${SPEEDUP_FLOOR}: the parallel side did not ` +
                'finish the same workload faster than the serial side'
        )
    }
    if (parallel.concurrency.median < CONCURRENCY_FLOOR) {
        failures.push(
            `the parallel side used ${parallel.concurrency.median.toFixed(2)} cores on average, ` +
                `under ${CONCURRENCY_FLOOR}: it did not use more than one core`
        )
    }
    if (serial.concurrency.median > SERIAL_CONCURRENCY_CEILING) {
        failures.push(
            `the serial side used ${serial.concurrency.median.toFixed(2)} cores on average, over ` +
                `${SERIAL_CONCURRENCY_CEILING}: the serial side is not serial`
        )
    }

    const verdict = failures.length === 0 ? 'PASS' : 'FAIL'
    const verdictLine =
        `para.speedup speedup=${speedup.toFixed(2)} floor=${SPEEDUP_FLOOR} ` +
        `serial_concurrency=${serial.concurrency.median.toFixed(2)} ` +
        `ceiling=${SERIAL_CONCURRENCY_CEILING} ` +
        `parallel_concurrency=${parallel.concurrency.median.toFixed(2)} ` +
        `floor=${CONCURRENCY_FLOOR} ` +
        `serial_cpu_ms=${serial.cpu.median.toFixed(1)} ` +
        `parallel_cpu_ms=${parallel.cpu.median.toFixed(1)} ` +
        `load_after=${loadText()} ` +
        `workload=${first.repeat}x${first.samplesPerWorker} hits=${first.hits} result=${verdict}`

    if (options.json) {
        console.log(
            JSON.stringify(
                {
                    camel: options.camel,
                    case: options.casePath,
                    rounds: options.rounds,
                    cores: cores,
                    coresOs: os.cpus().length,
                    load: os.loadavg(),
                    host: hostLine(options),
                    serial,
                    parallel,
                    speedup,
                    floors: {
                        speedup: SPEEDUP_FLOOR,
                        parallelConcurrency: CONCURRENCY_FLOOR,
                        serialConcurrency: SERIAL_CONCURRENCY_CEILING,
                    },
                    verdict: verdictLine,
                    failures,
                },
                null,
                2
            )
        )
        // The reporting mode gates nothing: the failures are in the JSON above.
        return 0
    }

    console.log(hostLine(options))
    console.log(sideLine('serial', serial, SERIAL_BACKEND))
    console.log(sideLine('parallel', parallel, PARALLEL_BACKEND))
    console.log(verdictLine)
    for (const failure of failures) {
        console.log(`para.speedup: FAILED ${failure}`)
    }
    return failures.length > 0 ? 1 : 0
}

process.exit(main())