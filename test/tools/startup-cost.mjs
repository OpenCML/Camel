// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The cost of everything `camel` does before the program runs, read in each child's
// CPU time (user + system) instead of the wall clock, and the cost of the declaration modules
// 
//
// The unit is the rule (docs/handover/reviews/questions-2026-10-23.md,
// "declaration-cost-gate" (a)):
//
//   "改量 CPU 时间，不量墙钟。共享主机上的墙钟随负载漂移，这正是 40/45/100/90 ms 在负载 106–130
//     下报红的原因；进程的 CPU 时间（用户态 + 内核态）基本不随负载变（该判定已经用 CPU 中位数）。
//    四条命令的天花板改为按 CPU 时间、取安静主机中位数加余量；"负载下红不算回归"的口径因此不需要。"
//
// So a crossed ceiling is a regression and never "the host was busy"; the 1/5/15-minute load average
// is printed beside the numbers as provenance, not as an excuse, and the wall clock is not measured
// at all. The four ceilings are CPU-time ceilings: the quiet host's median plus a margin, both
// recorded next to them with the samples they came from.
//
//   node test/tools/startup-cost.mjs                 measure and check the ceilings
//   node test/tools/startup-cost.mjs --json          the numbers, without the gate
//   node test/tools/startup-cost.mjs --reps 40       more processes per sample
//   node test/tools/startup-cost.mjs --samples 9     more samples per command
//
// Every line carries the ceiling it is read against and a `result=PASS|FAIL`, and a crossed ceiling
// is named on standard output (`startup-cost: FAILED <measurement>: cpu median <value> ms exceeds
// ceiling <bound> ms`) -- the stream the plan keeps. A reason on standard error is a reason the
// gate's reader never sees. A command that cannot run at all is that measurement's result, not a
// reason to lose the rest of the log.
//
// Node cannot ask for RUSAGE_CHILDREN, so a child's CPU time is the delta of the CPU time of every
// child reaped so far (`cutime` + `cstime` of /proc/self/stat, fields 16 and 17; spawnSync has
// already waited for the child when it returns, and the kernel folds a reaped child's CPU into those
// fields at that point). The kernel reports them in USER_HZ ticks, 10 ms, which is a third of the
// smallest number here, so one sample is not one child: a sample is the CPU time of `runs_per_sample`
// fresh processes divided by that count, which puts the tick's share under a millisecond. That is
// also why the reading is a sample mean and the spread is across samples.
//
// The four commands: `camel --version` is the process itself, dynamic loading, static initialisation
// and argument parsing, with no context. `imports_nothing` adds the smallest compile and run.
// `inspect_ops` imports every standard library module (`registry_all.cml`), so it is where the
// declaration modules (`stdlib/<module>.cml`) are parsed and compiled, and `imports_all` runs the
// same program through `std::nvm` instead of listing its operators.
//
// The last measurement, `declaration.cost`, is the compiler's own `--time-phases` report of one
// `inspect --ops` run: the parse, GCT and declare phases of the declaration modules, per declaration
// and in total, read in the CPU time the report writes for each phase (`cpu_ns`, beside the `wall_ns`
// it always carried). The four ceilings above are tens of milliseconds wide and cannot resolve it;
// the phase report can, and it reads some 28 us of CPU per declaration on this tree at load 68-93,
// which is what the per-declaration ceiling is built on. Its two remaining readings are the two the
// author chose on 2026-10-23 ("declaration-cost-gate" (b)):
//
//   "选 3 + 2：结构断言"每个（阶段, 模块）每轮恰好出现一次"管重复编译；绝对的 CPU 总量管整体变慢；
//    30% 共享率这条线退休。"
//
// The structural assertion is checked on every round: each (phase, module) among the pinned phases
// appears exactly once, and a pair that appears twice names itself. The absolute total is the
// declaration modules' parse + GCT + declare work of one run in milliseconds -- the number
// ($5.3 of, which records the wall-clock ceilings and the 30% share this
// rule retired) proposed gating after the share stopped moving with the declaration path. The 30%
// share of the compile work is deleted, not commented out: it was the load-invariant half of a pair
// whose other half was the wall clock, and with CPU time there is nothing left for it to excuse.
//
// Why this measurement is CPU too, and not the wall time the report had carried since the wall
// clock of those same phases read 21 us per declaration on the quiet host and 75-104 us in the
// windows of 2026-10-23 at load 106-112 -- three to five times the number for the same declarations,
// because a wall clock follows whatever else the host is doing. A ceiling on it is red on a busy day
// and green on a quiet one for the same binary, which is the wall-clock reading that was retired; the CPU time
// of the same rounds read 27-29 us in every window measured at load 68-112. So the compiler writes
// `cpu_ns` per phase beside `wall_ns` (src/utils/phase_timer.cpp), and a platform with no process CPU
// clock makes this measurement report an error rather than fall back to the wall clock.
//
// The declaration modules are read from the tree, not listed here: pinned the nine of
// its day, thirteen more were migrated, and this tool kept pricing 182 declarations while this tree
// compiled 291 -- and the share's denominator, which includes the modules the numerator no longer
// covered, went on growing. The set comes from `scripts/stdlib-declarations.mjs`, which already
// derives it from `stdlib/<module>.cml` + the C++ module's `BuiltinModule`; the next
// migration is measured the day its `.cml` lands. The measurement also refuses to run when a derived
// module has no phase in the report or no declaration in its `.cml`: those would price a subset and
// call it the whole.

import { spawnSync } from 'child_process'
import fs from 'fs'
import os from 'os'
import path from 'path'
import { fileURLToPath } from 'url'
import { declarationModules } from '../../scripts/stdlib-declarations.mjs'
import { takeCamel } from './camel-arg.mjs'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const REGISTRY_ALL = path.join(REPO_ROOT, 'test', 'cases', 'modules', 'stdlib', 'registry_all.cml')
const IMPORTS_NOTHING = path.join(REPO_ROOT, 'test', 'cases', 'perf', 'startup', 'imports_nothing.cml')

// The phases whose rows price the declaration work, and the population of the structural assertion:
// a (phase, module) pair is a phase from this list and the module its `detail` names.
const PINNED_PHASES = ['parse', 'gct', 'declare']

// The modules whose `.cml` beside a C++ `.cmo` declares the operators of that module (design
// principles 6.4): their declarations are the work `declaration.cost` measures. Derived from the
// tree, so a new declaration module is counted without editing this tool.
const DECLARATION_MODULES = declarationModules().map((module) => module.name)
if (DECLARATION_MODULES.length === 0) {
    throw new Error('no declaration module under stdlib/: the measurement would price no declaration')
}

// /proc/self/stat reports children in USER_HZ ticks, which is 100 on every Linux architecture
// (scripts/perf-gate.mjs reads the same two fields for the same reason).
const CLOCK_TICKS = 100
// One sample is this many fresh processes, so the kernel's 10 ms tick is divided by it before it
// reaches a number whose quiet median is a few milliseconds.
const RUNS_PER_SAMPLE = 12
// Samples per command. The median is what the ceiling is read against and the spread across samples
// is printed beside it.
const SAMPLES = 5
const WARMUPS = 2
// Below this the 10 ms tick is more than a millisecond of one sample's mean, which is the resolution
// the small ceilings are built on.
const MIN_RUNS_PER_SAMPLE = 8

// The install this run measures: `--camel <path>` names it (main parses the argument), else
// `$CAMEL_HOME/bin/camel`, else this tree's `out/latest/bin/camel`. One rule, in one place:
// test/tools/camel-arg.mjs.
let CAMEL = null

function camelExe() {
    return CAMEL
}

function usage(message) {
    if (message) console.error(`startup-cost: ${message}`)
    console.error(
        'usage: node test/tools/startup-cost.mjs [--json] [--reps N] [--samples N] [--camel PATH]'
    )
    process.exit(2)
}

// The CPU time of every child reaped so far, in seconds, as RUSAGE_CHILDREN reports it: /proc
// self/stat fields 16 and 17. Null on a platform that has no such accounting -- and a null is
// reported as the measurement's error, never as a reason to read the wall clock instead.
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
        `the CPU time of a child cannot be read on this platform (${process.platform}): ` +
        'the ceilings are CPU-time ceilings, not wall-clock ones ' +
        '(docs/handover/reviews/questions-2026-10-23.md, declaration-cost-gate (a)), so this ' +
        'command has no reading here'
    )
}

// Median, p10 and p90 in milliseconds. The host is shared, so the median is the number the gate
// reads and the percentiles say how wide the spread was.
function summarize(samples) {
    const sorted = [...samples].sort((a, b) => a - b)
    const at = (fraction) => sorted[Math.min(sorted.length - 1, Math.floor(fraction * sorted.length))]
    return {
        n: sorted.length,
        median: at(0.5),
        p10: at(0.1),
        p90: at(0.9),
        min: sorted[0],
        max: sorted[sorted.length - 1],
    }
}

function runOnce(argv) {
    const run = spawnSync(argv[0], argv.slice(1), {
        cwd: REPO_ROOT,
        stdio: 'ignore',
        maxBuffer: 1 << 26,
    })
    if (run.error) {
        throw new Error(`could not start ${argv[0]}: ${run.error.message}`)
    }
    if (run.signal) {
        throw new Error(`${argv.join(' ')} was killed by ${run.signal}`)
    }
    if (run.status !== 0) {
        throw new Error(`${argv.join(' ')} exited ${run.status}`)
    }
}

// One sample: `runsPerSample` fresh processes, and the CPU time the kernel charged for all of them.
function measureCpu(argv, warmups, samples, runsPerSample) {
    for (let i = 0; i < warmups; i++) {
        runOnce(argv)
    }
    const means = []
    for (let sample = 0; sample < samples; sample++) {
        const before = childrenCpuSeconds()
        if (before === null) {
            throw new Error(cpuUnavailableReason())
        }
        for (let i = 0; i < runsPerSample; i++) {
            runOnce(argv)
        }
        const after = childrenCpuSeconds()
        if (after === null) {
            throw new Error(cpuUnavailableReason())
        }
        if (after <= before) {
            throw new Error(
                `${runsPerSample} runs of ${argv.join(' ')} were charged no CPU time: ` +
                    'the reading examined nothing'
            )
        }
        means.push(((after - before) * 1000) / runsPerSample)
    }
    return { runsPerSample, ...summarize(means) }
}

// The declarations the standard library writes: one body-less `export func` per operator of a
// declared module. Counting them keeps the unit of `declaration.cost` in the sources.
function declarationCount() {
    let count = 0
    for (const module of DECLARATION_MODULES) {
        const text = fs.readFileSync(path.join(REPO_ROOT, 'stdlib', `${module}.cml`), 'utf8')
        const declarations = (text.match(/^\s*export\s+func\s/gm) || []).length
        if (declarations === 0) {
            throw new Error(`stdlib/${module}.cml declares no \`export func\`: the unit of declaration.cost would come from nothing`)
        }
        count += declarations
    }
    return count
}

// The phase report the compiler writes to stderr with CAMEL_TIME_PHASES=-, as a JSON object.
function phaseReport(stderr) {
    const at = stderr.lastIndexOf('{"schema"')
    const end = stderr.lastIndexOf('}')
    if (at < 0 || end < at) {
        return null
    }
    try {
        return JSON.parse(stderr.slice(at, end + 1))
    } catch {
        return null
    }
}

// Every (phase, module) pair of the pinned phases in one round, with how often the round reported
// it. The structural assertion reads this: a pair that appears twice is a module compiled twice,
// which no ratio and no total can name.
function pinnedPairs(report) {
    const pairs = new Map()
    for (const phase of report.phases) {
        if (!PINNED_PHASES.includes(phase.name)) {
            continue
        }
        const key = `${phase.name} ${phase.detail}`
        pairs.set(key, (pairs.get(key) || 0) + 1)
    }
    return pairs
}

// Milliseconds of parse + GCT + declare per declaration and in total, from `rounds` runs of
// `inspect --ops`: the compiler's own CPU accounting of the declaration modules, which the process
// clock cannot resolve. The same rounds carry the structural assertion of the
//  2026-10-23 rule, so a duplicate compilation is named by the round that reported it.
function declarationCost(rounds) {
    const argv = [camelExe(), 'inspect', '--ops', REGISTRY_ALL]
    const env = { ...process.env, CAMEL_TIME_PHASES: '-' }
    const declarations = declarationCount()
    const perDeclaration = []
    const totals = []
    const seen = new Set()
    let duplicates = 0
    for (let round = 0; round < rounds; round++) {
        const run = spawnSync(argv[0], argv.slice(1), {
            cwd: REPO_ROOT,
            env,
            stdio: ['ignore', 'ignore', 'pipe'],
            encoding: 'utf8',
            maxBuffer: 1 << 26,
        })
        if (run.error) {
            throw new Error(`could not start ${argv[0]}: ${run.error.message}`)
        }
        if (run.status !== 0) {
            throw new Error(`${argv.join(' ')} exited ${run.status}`)
        }
        const report = phaseReport(run.stderr)
        if (report === null) {
            throw new Error(`${argv.join(' ')} wrote no phase report`)
        }
        const pairs = pinnedPairs(report)
        if (pairs.size === 0) {
            throw new Error(
                `${argv.join(' ')} reported no ${PINNED_PHASES.join(', ')} phase: ` +
                    'the structural assertion would examine nothing'
            )
        }
        for (const name of PINNED_PHASES) {
            if (![...pairs.keys()].some((key) => key.startsWith(`${name} `))) {
                throw new Error(
                    `${argv.join(' ')} reported no ${name} phase: the structural assertion ` +
                        'would examine nothing for it'
                )
            }
        }
        const repeated = [...pairs.entries()].filter(([, times]) => times > 1)
        if (repeated.length > 0) {
            throw new Error(
                `round ${round + 1} compiled ` +
                    repeated.map(([key, times]) => `(${key}) ${times} times`).join(', ') +
                    ': each (phase, module) appears exactly once per round'
            )
        }
        duplicates += repeated.length
        let nanos = 0
        for (const phase of report.phases) {
            if (!PINNED_PHASES.includes(phase.name) || !DECLARATION_MODULES.includes(phase.detail)) {
                continue
            }
            if (!(phase.cpu_ns >= 0)) {
                throw new Error(
                    `${argv.join(' ')} reported no CPU time for the phase ${phase.name} ` +
                        `${phase.detail}: without a process CPU clock the measurement would have to ` +
                        'read the wall clock, which follows the host\'s load'
                )
            }
            nanos += phase.cpu_ns
            seen.add(phase.detail)
        }
        if (nanos <= 0) {
            throw new Error(
                `${argv.join(' ')} charged the declaration modules no ${PINNED_PHASES.join(', ')} ` +
                    'time: the measurement examined nothing'
            )
        }
        perDeclaration.push(nanos / 1e3 / declarations)
        totals.push(nanos / 1e6)
    }
    // A derived module that the compiler did not compile (a `.cml` that no program imports, a
    // registry that stopped reaching it) contributes declarations to the unit but no time to the
    // numerator: the measurement would price a subset and print it as the whole. Name it instead.
    const missing = DECLARATION_MODULES.filter((module) => !seen.has(module))
    if (missing.length > 0) {
        throw new Error(
            `no phase report line for declaration module(s) ${missing.join(', ')}: ` +
                'the measurement would price only the modules the run compiled',
        )
    }
    return {
        declarations,
        rounds,
        duplicates,
        perDeclaration: summarize(perDeclaration),
        total: summarize(totals),
    }
}

// The ceilings, all in CPU milliseconds, as the quiet host's median plus a margin. Both numbers are
// re-measured here rather than remembered: the samples below are the ones this tree's ceilings were
// set from, with the host's load beside them, and the comment names the window they came from. The
// reading is the median of the samples and the spread is min..max; a ceiling nobody can reproduce is
// worse than none, so a crossed one has to be a regression a reader could reproduce on a quiet host.
//
// Measured on the Linux host (AMD EPYC 7343, 32 threads) on 2026-10-23, five samples of twelve fresh
// processes each (sixty per command) in each of four windows (load 92.9, 75.2, 72.6 and 68.7); the
// quiet column is the window at load 21.4, which read the same CPU time:
//
//   version          median 4.2-5.0 ms (samples 4.2-5.8)     quiet 4.89
//   imports_nothing  median 7.5-10.0 ms (samples 7.5-10.8)    quiet 7.01
//   inspect_ops      median 39.2-44.2 ms (samples 38.3-45.0)  quiet 37.97
//   imports_all      median 40.8-42.5 ms (samples 37.5-43.3)  quiet 36.19
//
// CPU time is the unit: the wall clock drifts with a shared host's load, CPU time does not. The wall-clock ceilings it replaces read
// 40/45/100/90 ms against the same work, and at load 106-130 they went red while `--version`, which
// compiles nothing, read 13.0 ms; the CPU median of that same command moves between 4.2 and 5.4 ms
// from load 21 to load 215. Every ceiling here is about two and a half times the quiet median of its
// command: enough for this host's sample spread and for the ten to thirty percent that CPU time still
// moves with frequency and neighbours, and tight enough that "the standard library is loaded before
// the program runs" -- the small commands' cost jumping to the full-import cost, +30 ms -- crosses
// `version` and `imports_nothing` on the first sample. It is a smoke alarm, not a percentage budget,
// exactly as the wall-clock ceilings it replaces were.
const CASES = [
    {
        name: 'version',
        argv: () => [camelExe(), '--version'],
        ceilingMs: 12,
    },
    {
        name: 'imports_nothing',
        argv: () => [camelExe(), IMPORTS_NOTHING, 'std::nvm'],
        ceilingMs: 18,
    },
    {
        name: 'inspect_ops',
        argv: () => [camelExe(), 'inspect', '--ops', REGISTRY_ALL],
        ceilingMs: 95,
    },
    {
        name: 'imports_all',
        argv: () => [camelExe(), REGISTRY_ALL, 'std::nvm'],
        ceilingMs: 90,
    },
]

// Three times the ~20 us of one declaration: a smoke alarm for "a declaration now costs
// hundreds of microseconds", not a percentage budget. The number does not move with the unit the
//  2026-10-23 rule changed: it read 27.5-28.5 us of CPU per declaration in every window measured at
// load 68-93, against 21.0 us of wall on a quiet host, so it keeps the margin it was given.
const DECLARATION_CEILING_US = 60
// The absolute total of the same work, which is the half of the pair that the per-declaration
// ceiling cannot see: 4.6 ms of declaration regression spread over this tree's 347 declarations is
// only 13 us per declaration, far under 60, but it moves the total. sized its candidate at 9 ms
// against the 6.04 ms of its quiet window (1.5 times the quiet total, the regression named); this
// tree's total read 9.6-9.9 ms in every window measured at load 68-93, and the quiet wall reading
// of 21.0 us per declaration is 7.6 ms over 347 declarations, with this host's CPU readings some
// third above a quiet wall reading. Twelve milliseconds is that calibration applied to this tree:
// the quiet total plus the regression the line was chosen to name, 1.2 times the busiest median
// measured here and 1.6 times the quiet one. It prices this tree's declaration count, so it is
// re-baselined when a module is migrated (the printed `declarations=` says by how much) -- the
// per-declaration ceiling above is the one that does not move with the count.
const DECLARATION_TOTAL_CEILING_MS = 12
// Rounds of `inspect --ops` for `declaration.cost`: each round is one sample of the two numbers and
// one application of the structural assertion.
const DECLARATION_ROUNDS = 21

function main() {
    const named = takeCamel(process.argv.slice(2), REPO_ROOT, usage)
    CAMEL = named.camel
    const args = named.rest
    const json = args.includes('--json')
    const repsAt = args.indexOf('--reps')
    const runsPerSample = repsAt >= 0 ? Number(args[repsAt + 1]) : RUNS_PER_SAMPLE
    const samplesAt = args.indexOf('--samples')
    const samples = samplesAt >= 0 ? Number(args[samplesAt + 1]) : SAMPLES
    if (!Number.isInteger(runsPerSample) || runsPerSample < MIN_RUNS_PER_SAMPLE) {
        console.error(
            `startup-cost: --reps needs an integer of at least ${MIN_RUNS_PER_SAMPLE} ` +
                '(one sample divides the kernel\'s 10 ms CPU tick by it)'
        )
        return 2
    }
    if (!Number.isInteger(samples) || samples < 3) {
        console.error('startup-cost: --samples needs an integer of at least 3')
        return 2
    }
    if (!fs.existsSync(camelExe())) {
        console.error(`startup-cost: no camel binary at ${camelExe()}`)
        return 2
    }
    const results = []
    const failures = []
    for (const testCase of CASES) {
        // A command that cannot run (a missing file, a crash, a host that cannot read a child's CPU
        // time) is this case's result, not a reason to lose the other three: an exception here used
        // to leave standard output empty, and the plan could only report "expected exit 0, got 1".
        let summary
        try {
            summary = measureCpu(testCase.argv(), WARMUPS, samples, runsPerSample)
        } catch (error) {
            results.push({
                name: testCase.name,
                ceiling_ms: testCase.ceilingMs,
                error: error.message,
            })
            failures.push(`${testCase.name}: ${error.message}`)
            continue
        }
        results.push({ name: testCase.name, ceiling_ms: testCase.ceilingMs, ...summary })
        // `--json` is the reporting mode: it prints the numbers and gates nothing.
        if (!json && summary.median > testCase.ceilingMs) {
            failures.push(
                `${testCase.name}: cpu median ${summary.median.toFixed(1)} ms exceeds ceiling ${testCase.ceilingMs} ms`
            )
        }
    }
    let declarations = null
    try {
        declarations = declarationCost(DECLARATION_ROUNDS)
    } catch (error) {
        // Same rule as the four commands above: name what failed and why on standard output.
        failures.push(`declaration.cost: ${error.message}`)
    }
    let declarationVerdict = 'PASS'
    if (declarations !== null) {
        if (!json && declarations.perDeclaration.median > DECLARATION_CEILING_US) {
            declarationVerdict = 'FAIL'
            failures.push(
                `declaration.cost: cpu median ${declarations.perDeclaration.median.toFixed(1)} us per declaration ` +
                    `exceeds ceiling ${DECLARATION_CEILING_US} us`
            )
        }
        if (!json && declarations.total.median > DECLARATION_TOTAL_CEILING_MS) {
            declarationVerdict = 'FAIL'
            failures.push(
                `declaration.cost: the declaration modules' cpu total ${declarations.total.median.toFixed(1)} ms ` +
                    `exceeds ceiling ${DECLARATION_TOTAL_CEILING_MS} ms`
            )
        }
    }
    if (json) {
        console.log(
            JSON.stringify(
                {
                    camel: camelExe(),
                    samples,
                    runsPerSample,
                    results,
                    declaration: declarations,
                },
                null,
                2
            )
        )
        return 0
    }
    // The host's load, in the same log as the medians, as provenance: CPU time barely moves with it,
    // so it no longer has to separate a regression from a busy machine (the 2026-10-23 rule).
    console.log(
        `startup.cost metric=cpu_user_plus_system samples=${samples} runs_per_sample=${runsPerSample} ` +
            `host load=${os.loadavg().map((value) => value.toFixed(1)).join('/')}`
    )
    for (const result of results) {
        if (result.error !== undefined) {
            console.log(
                `startup.cost ${result.name.padEnd(16)} result=ERROR ${result.error} ` +
                    `ceiling=${result.ceiling_ms} ms`
            )
            continue
        }
        const verdict = result.median > result.ceiling_ms ? 'FAIL' : 'PASS'
        console.log(
            `startup.cost ${result.name.padEnd(16)} cpu_median=${result.median.toFixed(1)} ms ` +
                `cpu_min=${result.min.toFixed(1)} cpu_max=${result.max.toFixed(1)} ` +
                `n=${result.n} runs_per_sample=${result.runsPerSample} ` +
                `ceiling=${result.ceiling_ms} ms result=${verdict}`
        )
    }
    if (declarations !== null) {
        console.log(
            `declaration.cost declarations=${declarations.declarations} ` +
                `metric=cpu_user_plus_system ` +
                `per_declaration_us=${declarations.perDeclaration.median.toFixed(1)} ` +
                `us_min=${declarations.perDeclaration.min.toFixed(1)} ` +
                `us_max=${declarations.perDeclaration.max.toFixed(1)} ` +
                `total_ms=${declarations.total.median.toFixed(1)} ` +
                `total_min=${declarations.total.min.toFixed(1)} ` +
                `total_max=${declarations.total.max.toFixed(1)} ` +
                `rounds=${declarations.rounds} duplicates=${declarations.duplicates} ` +
                `ceiling=${DECLARATION_CEILING_US} us ` +
                `total_ceiling=${DECLARATION_TOTAL_CEILING_MS} ms ` +
                `result=${declarationVerdict}`
        )
    }
    // Standard output, not standard error: the plan keeps this stream as the test's log, and a
    // failure the reader cannot see is indistinguishable from a hang. Each line names the
    // measurement, the value and the bound it crossed.
    for (const failure of failures) {
        console.log(`startup-cost: FAILED ${failure}`)
    }
    return failures.length > 0 ? 1 : 0
}

process.exit(main())