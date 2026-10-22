#!/usr/bin/env node
// The manual entry point for performance measurement (handover H167).
//
// The perf layer is deliberately outside `--changed` and outside `npm run test` (H154 section 6,
// test/plan-selection.json): its plans are long, two of them hold gigabytes, four compare against
// PyTorch, and a threshold on a shared host reds for being busy. The consequence is that nothing
// notices a performance regression. This script is the one place that can measure it on purpose:
// it is manual, it is never wired into a gate, and it only measures the plans whose cost allows a
// measurement to be repeated.
//
//   node scripts/perf-gate.mjs                  measure and compare against test/perf-baseline.json
//   node scripts/perf-gate.mjs --list            the ten perf plans, and why each is in or out
//   node scripts/perf-gate.mjs --update          measure and rewrite the baseline (seed it on the host)
//   node scripts/perf-gate.mjs --reps 9 --threshold 20
//   node scripts/perf-gate.mjs --plans perf.startup --json
//
// What it measures is the exact commands the gateable plans time, not the plan runner around them.
// Every repetition is one fresh `camel` process; the repetitions are interleaved (round by round,
// with the order rotated each round) so that a change in the host's load hits every case alike,
// and the verdict reads medians, never a single run.
//
// Two numbers per repetition: the wall clock, and on Linux the child's own CPU time (user+system),
// read from the `cutime`/`cstime` fields of /proc/self/stat around the child. Wall clock is what a
// user waits; CPU time is what the work costs. H142 made the same choice for the same reason: at
// load 200 the wall clock is mostly waiting for a core, while the CPU column's two independent runs
// differed by under 3%. A case whose child CPU is at least 250 ms is
// judged on CPU, where the 10 ms tick of /proc is a few percent at most; a shorter one is judged on
// the wall clock, where the spread is the protection.
//
// The verdicts, in order:
//   * the case has no baseline            -> "no baseline", never a failure;
//   * a wall-clock case whose run left the load band the baseline was recorded in
//                                         -> "inconclusive", never a pass: the line says "measured
//     at load N" and the next step is to rerun within the band. A CPU case is compared at any load,
//     because child CPU time is what the work costs rather than how long the host made it wait:
//     H142 judged by CPU for that reason, and its two independent CPU runs differed by under 3%,
//     while the wall clock at load 150..215 is 2-5x the quiet one;
//   * the current and baseline spreads (p10..p90) overlap -> "ok (spread overlaps)": a regression
//     is never claimed from numbers that a busy host can produce;
//   * otherwise slower by at least --threshold percent -> "REGRESSION" (exit 1);
//   * otherwise faster by at least --threshold percent -> "faster", else "ok".
// The wall band is `baseline load + 2 + 10%` on the 1-minute average of the worst round: a
// millisecond number read on a host that is a tenth busier is not the same number. Exit codes:
// 0 no regression (including "inconclusive" and "no baseline"), 1 a regression, 2 a usage error or
// a refused plan, 3 a measured command failed.
//
// The baseline is data, not a ceiling: test/perf-baseline.json records the medians, both spreads,
// the exact command of every case, the commit, the build's own stamp (out/latest/build-info.json),
// the repetition count and the host load at the time. It is seeded on the Linux host because the
// numbers are that host's; a baseline from another machine is not comparable (H142).
//
// The plans this script refuses are refused by name, with the policy that keeps them out of every
// gate. `--list` prints the whole classification; the numbers behind it are in
// handover note ⟨f14c90d⟩ (H167), and docs/test-plans.md "Performance measurement" states the rule.

import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const DEFAULT_BASELINE = path.join(REPO_ROOT, 'test', 'perf-baseline.json')
const BASELINE_SCHEMA = 'camel-perf-gate/1'
// /proc/self/stat reports children in USER_HZ ticks, which is 100 on every Linux architecture.
const CLOCK_TICKS = 100
// Below this, one /proc tick is more than a percent of the number, so the wall clock is the read.
const CPU_METRIC_MIN_MS = 250
const DEFAULT_REPS = 5
const DEFAULT_WARMUP = 1
const DEFAULT_THRESHOLD_PCT = 15
const CHILD_TIMEOUT_MS = 900000
// The cheap cases of perf.startup are milliseconds: their spread is the whole verdict, so they take
// at least this many samples even when --reps is small (21 is what test/tools/startup-cost.mjs
// uses). Four commands x 21 runs cost about two seconds.
const MIN_REPS_WALL_CASE = 21

const REGISTRY_ALL = path.join(REPO_ROOT, 'test', 'cases', 'modules', 'stdlib', 'registry_all.cml')
const IMPORTS_NOTHING = path.join(REPO_ROOT, 'test', 'cases', 'perf', 'startup', 'imports_nothing.cml')
const FIB_RECURSIVE = path.join(REPO_ROOT, 'test', 'cases', 'perf', 'fib_recursive.cml')

// agents/test.md, "Test Execution": the sentence this script quotes when it refuses a plan.
const POLICY =
    'agents/test.md: "the GC poison sweep, the GC stress plans (`gc.collection_trees_*`, ' +
    '`gc_elder_*`), the progen random-program checks, the PyTorch comparisons and the perf/timeit ' +
    'plans are heavy: they saturate every core for minutes and are worth running only at a ' +
    'milestone, by the coordinator, on a quiet machine."'

// The ten plans of test/plans/perf (H167 classified them): what each one measures, what it costs
// on the shared host, whether it already has a baseline or a ceiling of its own, and whether this
// gate can take it. `cases` exist only for the gateable plans: the argv is the plan's own command,
// so the number the gate reads is the number the plan reads. The costs are the plan comments' own
// measurements plus H167's one run of each gateable plan; the report has the evidence.
const PLANS = [
    {
        id: 'perf.startup',
        file: 'test/plans/perf/startup.plan.toml',
        gateable: true,
        measures:
            'everything camel does before the program runs: the process (--version), a one-line ' +
            'program, inspect --ops over the whole standard library, and the same imports on the run path',
        cost: 'a few seconds per repetition: the whole plan is 2.1 s (H167)',
        own:
            'yes: four ceilings (H142) and declaration.cost, whose share of the compile work is ' +
            'load-invariant (H152)',
        cases: [
            {
                name: 'startup.version',
                // The process itself: dynamic loading and argument parsing, with no Context.
                argv: (camel) => [camel, '--version'],
                minReps: MIN_REPS_WALL_CASE,
            },
            {
                name: 'startup.imports_nothing',
                // The smallest compile and run: Context, builtin modules, one parse, one std::nvm.
                argv: (camel) => [camel, IMPORTS_NOTHING, 'std::nvm'],
                minReps: MIN_REPS_WALL_CASE,
            },
            {
                name: 'startup.inspect_ops',
                // Where the nine declaration modules (stdlib/<module>.cml) are parsed and compiled.
                argv: (camel) => [camel, 'inspect', '--ops', REGISTRY_ALL],
                minReps: MIN_REPS_WALL_CASE,
            },
            {
                name: 'startup.imports_all',
                // The same imports on the run path instead of the operator listing.
                argv: (camel) => [camel, REGISTRY_ALL, 'std::nvm'],
                minReps: MIN_REPS_WALL_CASE,
            },
        ],
    },
    {
        id: 'perf.fib',
        file: 'test/plans/perf/fib.plan.toml',
        gateable: true,
        measures:
            'recursive Fibonacci timed with --timeit on nvm, fvm and jit, each also after ' +
            'std::opt::inline (ten timed iterations per case)',
        cost: 'about 11 s per repetition for all six cases (H167)',
        own: 'no budget: the plan records the means and its header says why the old budgets were dropped',
        cases: [
            { name: 'fib.nvm_timeit', argv: (camel) => timeit(camel, 'std::nvm', false) },
            { name: 'fib.inline_nvm_timeit', argv: (camel) => timeit(camel, 'std::nvm', true) },
            { name: 'fib.fvm_timeit', argv: (camel) => timeit(camel, 'std::fvm', false) },
            { name: 'fib.inline_fvm_timeit', argv: (camel) => timeit(camel, 'std::fvm', true) },
            { name: 'fib.jit_timeit', argv: (camel) => timeit(camel, 'std::jit', false) },
            { name: 'fib.inline_jit_timeit', argv: (camel) => timeit(camel, 'std::jit', true) },
        ],
    },
    {
        id: 'perf.para',
        file: 'test/plans/perf/para.plan.toml',
        gateable: false,
        refusal: 'expensive',
        measures:
            'an Asian option Monte Carlo, a 100M-element mergesort and a Monte Carlo pi estimator, ' +
            'one timed iteration each on std::nvm and std::prl',
        cost:
            '178.7 s for one repetition of all six cases (H167); asian_option_mc.nvm alone is ' +
            '67.5 s, and merge_sort holds 3.8 GB (nvm) or 11.9 GB (prl) resident',
        own: 'no budget; the plan has timeouts only',
    },
    {
        id: 'perf.gc',
        file: 'test/plans/perf/gc.plan.toml',
        gateable: false,
        refusal: 'policy',
        measures:
            'minute-long soaks and gigabyte-scale live sets of both collectors, with gc_stats ' +
            'assertions on promotion, elder space and pause bounds',
        cost: '81-105 s per soak case on the development machine; timeouts up to 900 s',
        own: 'assertions on what must not grow with the length of the run, not on time',
    },
    {
        id: 'perf.collections_fuzz',
        file: 'test/plans/perf/collections_fuzz.plan.toml',
        gateable: false,
        refusal: 'policy',
        measures:
            'random and differential Map/Set programs against a Python reference, four back ends ' +
            'and four representations (3200 + 960 + 720 + 108 runs)',
        cost: 'minutes to tens of minutes; timeouts 30 and 60 min',
        own: 'exact agreement with the reference; no time assertion',
    },
    {
        id: 'perf.training_steps',
        file: 'test/plans/perf/training_steps.plan.toml',
        gateable: false,
        refusal: 'policy',
        measures:
            'GPU training steps (a matmul dependency chain, the C2 MLP step, a small Transformer ' +
            'step) timed with --timeit, plus a profiler case',
        cost: 'six cases on the GPU, timeouts 180-300 s',
        own: 'one 100%-tolerance budget (mlp_step.fvm, 500 ms); the rest are recorded numbers',
    },
    {
        id: 'perf.showcase_c1',
        file: 'test/plans/perf/showcase_c1.plan.toml',
        gateable: false,
        refusal: 'policy',
        measures: 'the C1 linear/logistic regression showcase against its PyTorch counterpart, small profile',
        cost: 'seconds to a minute; needs numpy, torch and psutil (skip_unless)',
        own: 'the comparison\'s numeric tolerances, not a time budget',
    },
    {
        id: 'perf.showcase_c2',
        file: 'test/plans/perf/showcase_c2.plan.toml',
        gateable: false,
        refusal: 'policy',
        measures: 'the C2 MLP/MNIST showcase against its PyTorch counterpart, small profile',
        cost: 'seconds to a minute; needs numpy, torch and psutil (skip_unless)',
        own: "the comparison's numeric tolerances, not a time budget",
    },
    {
        id: 'perf.showcase_c6',
        file: 'test/plans/perf/showcase_c6.plan.toml',
        gateable: false,
        refusal: 'policy',
        measures: 'the C6 VAE showcase against its PyTorch counterpart, small profile',
        cost: 'seconds to a minute; needs numpy, torch and psutil (skip_unless)',
        own: "the comparison's numeric tolerances, not a time budget",
    },
    {
        id: 'perf.showcase_d5',
        file: 'test/plans/perf/showcase_d5.plan.toml',
        gateable: false,
        refusal: 'policy',
        measures: 'the D5 Tree-LSTM showcase against its PyTorch counterpart, small profile',
        cost: 'seconds to a minute; needs numpy, torch and psutil (skip_unless)',
        own: "the comparison's numeric tolerances, not a time budget",
    },
]

const GATEABLE = PLANS.filter((plan) => plan.gateable).map((plan) => plan.id)

// The plan's own command for one Fibonacci case: `--timeit std::<backend>/10`, the program, then
// the passes the plan names (std::opt::inline before the backend when the case inlines).
function timeit(camel, backend, inline) {
    const argv = [camel, '--timeit', `${backend}/10`, '--output-format', 'json', FIB_RECURSIVE]
    if (inline) {
        argv.push('std::opt::inline')
    }
    argv.push(backend)
    return argv
}

function camelExe() {
    if (process.env.CAMEL_EXE) {
        return process.env.CAMEL_EXE
    }
    const home = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
    return path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
}

function usage(message) {
    if (message) {
        console.error(`perf-gate: ${message}`)
    }
    console.error(
        'usage: node scripts/perf-gate.mjs [--list] [--plans perf.startup,perf.fib] [--reps N]\n' +
            '       [--warmup N] [--update] [--baseline FILE] [--threshold PCT] [--json]'
    )
    process.exit(2)
}

function parseArgs(argv) {
    const options = {
        list: false,
        json: false,
        update: false,
        plans: null,
        reps: DEFAULT_REPS,
        warmup: DEFAULT_WARMUP,
        threshold: DEFAULT_THRESHOLD_PCT,
        baseline: DEFAULT_BASELINE,
    }
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        const value = () => {
            if (i + 1 >= argv.length) {
                usage(`${arg} needs a value`)
            }
            return argv[++i]
        }
        if (arg === '--list') {
            options.list = true
        } else if (arg === '--json') {
            options.json = true
        } else if (arg === '--update') {
            options.update = true
        } else if (arg === '--plans') {
            options.plans = value()
                .split(',')
                .map((id) => id.trim())
                .filter(Boolean)
        } else if (arg === '--reps') {
            options.reps = Number(value())
        } else if (arg === '--warmup') {
            options.warmup = Number(value())
        } else if (arg === '--threshold') {
            options.threshold = Number(value())
        } else if (arg === '--baseline') {
            options.baseline = path.resolve(value())
        } else if (arg === '--help' || arg === '-h') {
            usage()
        } else {
            usage(`unknown argument "${arg}"`)
        }
    }
    if (!Number.isInteger(options.reps) || options.reps < 3) {
        usage('--reps needs an integer of at least 3 (5 or more is better: the verdict reads the spread)')
    }
    if (!Number.isInteger(options.warmup) || options.warmup < 0) {
        usage('--warmup needs an integer of at least 0')
    }
    if (!Number.isFinite(options.threshold) || options.threshold < 0) {
        usage('--threshold needs a non-negative number of percent')
    }
    return options
}

// The gateable subset asked for by --plans, or the whole gateable set. A plan that is not gateable
// is refused here, before anything runs, so that "refuse by name" costs nothing.
function selectPlans(options) {
    const wanted = options.plans || GATEABLE
    const selected = []
    for (const id of wanted) {
        const plan = PLANS.find((entry) => entry.id === id)
        if (!plan) {
            usage(`no plan "${id}"; --list prints the ten plans of test/plans/perf`)
        }
        if (!plan.gateable) {
            refuse(plan)
        }
        if (!selected.includes(plan)) {
            selected.push(plan)
        }
    }
    return selected
}

function refuse(plan) {
    if (plan.refusal === 'expensive') {
        console.error(
            `perf-gate: refusing to run "${plan.id}": it is measurable but not gateable.\n\n` +
                `  ${plan.id} ${plan.measures}.\n` +
                `  ${plan.cost}.\n` +
                '  A repetition costs minutes, so the median cannot be repeated often enough to be a\n' +
                '  gate; H167 measured the plan once and recorded the number in\n' +
                '  handover note ⟨f14c90d⟩ (H167). If a milestone needs it, run that plan itself\n' +
                '  (docs/test-plans.md, "Layers: fast, slow And debug").\n'
        )
    } else {
        console.error(
            `perf-gate: refusing to run "${plan.id}": the project forbids the heavy plans on a\n` +
                'shared host.\n\n' +
                `  ${plan.id} ${plan.measures}.\n` +
                `  ${plan.cost}.\n` +
                `  policy -- ${POLICY}\n` +
                '  handover note ⟨0ee01ba⟩ (H154) section 2 records this plan as not run for exactly that\n' +
                '  reason, and test/plan-selection.json keeps it out of --changed.\n' +
                '  A milestone run names the plan file to the test runner itself; this gate never\n' +
                '  measures it.\n'
        )
    }
    process.exit(2)
}

function readLoad() {
    // /proc/loadavg is the 1, 5 and 15 minute averages. Other platforms report no load average
    // (os.loadavg() is [0, 0, 0] on Windows), and a comparison without it is inconclusive below.
    if (process.platform !== 'linux') {
        return null
    }
    try {
        const parts = fs.readFileSync('/proc/loadavg', 'utf8').trim().split(/\s+/)
        return { load1: Number(parts[0]), load5: Number(parts[1]), load15: Number(parts[2]) }
    } catch {
        return null
    }
}

// The CPU time of every child reaped so far, in seconds, as RUSAGE_CHILDREN reports it: /proc
// self/stat fields 16 and 17. spawnSync has already waited for the child when it returns, and the
// kernel folds a reaped child's CPU into these fields at that point, so the delta around one spawn
// is exactly that child. Node cannot ask for RUSAGE_CHILDREN, and this is the load-invariant half
// of the measurement (H142: the wall clock at load 215 is 2-3 times the quiet one, the CPU time is
// within a few percent).
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

function timeOne(argv, env) {
    const before = childrenCpuSeconds()
    const started = process.hrtime.bigint()
    const run = spawnSync(argv[0], argv.slice(1), {
        cwd: REPO_ROOT,
        env,
        stdio: 'ignore',
        maxBuffer: 1 << 26,
        timeout: CHILD_TIMEOUT_MS,
    })
    const wallMs = Number(process.hrtime.bigint() - started) / 1e6
    const after = childrenCpuSeconds()
    if (run.error) {
        throw new Error(`could not start ${argv[0]}: ${run.error.message}`)
    }
    if (run.signal) {
        throw new Error(`${displayArgv(argv)} was killed by ${run.signal} after ${wallMs.toFixed(0)} ms`)
    }
    if (run.status !== 0) {
        throw new Error(`${displayArgv(argv)} exited ${run.status}`)
    }
    const cpuMs = before === null || after === null ? null : (after - before) * 1000
    return { wallMs, cpuMs }
}

// Median, p10 and p90 in milliseconds. The host is shared, so the median is the number the verdict
// reads and the percentiles say how wide the spread was; one slow sample is not a regression.
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

function naturalMetric(entry) {
    if (entry.cpu && entry.cpu.median >= CPU_METRIC_MIN_MS) {
        return 'cpu'
    }
    return 'wall'
}

// One round measures every case once, in an order rotated by the round number, so that no case
// always runs first (a warm cache or a neighbour's burst would otherwise land on the same case).
function roundOrder(cases, round) {
    const offset = round % cases.length
    return [...cases.slice(offset), ...cases.slice(0, offset)]
}

function measure(cases, options, env, onRound) {
    const reps = new Map(cases.map((entry) => [entry.name, Math.max(options.reps, entry.minReps || 0)]))
    const samples = new Map(cases.map((entry) => [entry.name, { wall: [], cpu: [] }]))
    for (let round = 0; round < options.warmup; round++) {
        for (const entry of roundOrder(cases, round)) {
            timeOne(entry.argv(), env) // warm-up: page cache and the loader, not a sample
        }
        onRound()
    }
    const rounds = Math.max(...reps.values())
    for (let round = 0; round < rounds; round++) {
        for (const entry of roundOrder(cases, round)) {
            if (round >= reps.get(entry.name)) {
                continue
            }
            const sample = timeOne(entry.argv(), env)
            samples.get(entry.name).wall.push(sample.wallMs)
            if (sample.cpuMs !== null) {
                samples.get(entry.name).cpu.push(sample.cpuMs)
            }
        }
        onRound()
    }
    const measured = {}
    for (const entry of cases) {
        const taken = samples.get(entry.name)
        measured[entry.name] = {
            plan: entry.plan,
            command: displayArgv(entry.argv()),
            wall: summarize(taken.wall),
            cpu: taken.cpu.length > 0 ? summarize(taken.cpu) : null,
        }
        measured[entry.name].metric = naturalMetric(measured[entry.name])
    }
    return measured
}

function displayArgv(argv) {
    const prefix = REPO_ROOT + path.sep
    // Repository-relative and with '/' on every platform: the baseline and the listing are read on
    // the Linux host and on the Windows workstations alike.
    return argv
        .map((arg) => (arg.startsWith(prefix) ? arg.slice(prefix.length) : arg).replace(/\\/g, '/'))
        .join(' ')
}

function displayPath(target) {
    // Same rule as displayArgv: repo-relative, and with '/' whichever platform is reading.
    const prefix = REPO_ROOT + path.sep
    const shown = target.startsWith(prefix) ? target.slice(prefix.length) : target
    return shown.replace(/\\/g, '/')
}

function formatMs(value) {
    if (!Number.isFinite(value)) {
        return 'n/a'
    }
    return value >= 1000 ? `${(value / 1000).toFixed(2)} s` : `${value.toFixed(1)} ms`
}

function formatSpread(summary) {
    return `${formatMs(summary.median)} [${formatMs(summary.p10)}, ${formatMs(summary.p90)}]`
}

function git(args) {
    const run = spawnSync('git', args, { cwd: REPO_ROOT, encoding: 'utf8' })
    if (run.error || run.status !== 0) {
        return null
    }
    return run.stdout.trim()
}

function revisionInfo() {
    const revision = git(['rev-parse', 'HEAD'])
    if (!revision) {
        return { available: false }
    }
    const status = git(['status', '--porcelain'])
    const dirtyFiles = status ? status.split('\n').filter(Boolean).length : 0
    return {
        available: true,
        revision,
        subject: git(['log', '-1', '--format=%s']),
        dirty: dirtyFiles > 0,
        dirtyFiles,
    }
}

function installDir(camel) {
    return path.dirname(path.dirname(camel))
}

// The build's own stamp (H149): which sources produced this install, and whether they were dirty.
// It is the strongest condition a baseline can record, because it names the binary, not the tree.
function buildStamp(camel) {
    const file = path.join(installDir(camel), 'build-info.json')
    try {
        const stamp = JSON.parse(fs.readFileSync(file, 'utf8'))
        return {
            install: path.relative(REPO_ROOT, installDir(camel)) || '.',
            revision: stamp.revision || null,
            dirty: stamp.dirty === undefined ? null : stamp.dirty,
            fingerprint: stamp.fingerprint && stamp.fingerprint.hash ? stamp.fingerprint.hash : null,
            built_at: stamp.builtAt || null,
        }
    } catch {
        return null
    }
}

function hostInfo() {
    return {
        name: os.hostname(),
        platform: process.platform,
        arch: process.arch,
        cores: os.cpus().length,
        cpu: os.cpus()[0] ? os.cpus()[0].model : 'unknown',
        kernel: os.release(),
    }
}

function loadSummary(samples) {
    const present = samples.filter(Boolean)
    if (present.length === 0) {
        return null
    }
    return {
        start: present[0],
        end: present[present.length - 1],
        max: present.reduce((worst, load) => (load.load1 > worst.load1 ? load : worst), present[0]),
        samples: present.length,
    }
}

function readBaseline(file) {
    if (!fs.existsSync(file)) {
        return null
    }
    const text = fs.readFileSync(file, 'utf8')
    let baseline
    try {
        baseline = JSON.parse(text)
    } catch (error) {
        throw new Error(`${path.relative(REPO_ROOT, file)} is not readable JSON: ${error.message}`)
    }
    if (!baseline || baseline.schema !== BASELINE_SCHEMA) {
        throw new Error(
            `${path.relative(REPO_ROOT, file)} has schema ${baseline && baseline.schema}, this script writes ${BASELINE_SCHEMA}`
        )
    }
    return baseline
}

function writeBaseline(file, measured, planIds, options, revision, build, host, load) {
    const cases = {}
    for (const [name, entry] of Object.entries(measured)) {
        cases[name] = { plan: entry.plan, command: entry.command, metric: entry.metric, wall: entry.wall, cpu: entry.cpu }
    }
    const baseline = {
        schema: BASELINE_SCHEMA,
        description:
            'Medians and spreads of the commands of the gateable perf plans, measured on the host ' +
            'named below. `node scripts/perf-gate.mjs` compares against this file; `--update` ' +
            'rewrites it from a run on the same kind of host. A baseline from another machine is ' +
            'not comparable (docs/test-plans.md, "Performance measurement"; H167).',
        written_at: new Date().toISOString(),
        written_by: 'node scripts/perf-gate.mjs --update',
        plans: planIds,
        reps: options.reps,
        warmup: options.warmup,
        metric_rule:
            `child CPU time when its median is at least ${CPU_METRIC_MIN_MS} ms ` +
            '(RUSAGE_CHILDREN via /proc/self/stat, Linux), else the wall clock',
        load_band:
            'a wall-clock case is comparable when its worst 1-minute load is at most baseline load + 2 ' +
            '+ 10%; a CPU case is compared at any load, because child CPU time does not grow with load',
        load: load,
        host: host,
        commit: revision,
        build: build,
        cases: cases,
    }
    fs.writeFileSync(file, JSON.stringify(baseline, null, 2) + '\n')
    return baseline
}

function spreadsOverlap(baseline, current) {
    return current.p10 <= baseline.p90 && baseline.p10 <= current.p90
}

function bandLimit(baselineLoad) {
    // The wall-clock band: a tenth busier plus two, on the 1-minute average.
    return baselineLoad + 2 + baselineLoad * 0.1
}

function compare(measured, baseline, options) {
    const rows = []
    const load = baseline.load ? baseline.load.max.load1 : null
    const currentLoad = options.currentLoad
    let bandIssue = null
    if (!baseline.load) {
        bandIssue = 'the baseline records no host load'
    } else if (!currentLoad) {
        bandIssue = 'this platform reports no load average, so a wall-clock measurement cannot be placed against the baseline'
    } else if (currentLoad.load1 > bandLimit(load)) {
        bandIssue =
            `wall clock measured at load ${currentLoad.load1.toFixed(1)}; the baseline was measured at ` +
            `load ${load.toFixed(1)} and the band is ${bandLimit(load).toFixed(1)}`
    }
    for (const [name, entry] of Object.entries(measured)) {
        const baselineCase = baseline.cases[name]
        if (!baselineCase) {
            rows.push({ name, entry, verdict: 'no baseline', detail: null, metric: entry.metric })
            continue
        }
        const metric = baselineCase.metric === 'cpu' || baselineCase.metric === 'wall' ? baselineCase.metric : entry.metric
        const base = baselineCase[metric]
        const now = entry[metric]
        if (!base || !now) {
            rows.push({
                name,
                entry,
                metric,
                verdict: 'inconclusive',
                detail: `the baseline has ${metric === 'cpu' ? 'CPU' : 'wall-clock'} numbers this run cannot produce`,
            })
            continue
        }
        const ratio = now.median / base.median
        let row = { name, entry, metric, base, now, ratio }
        // Only a wall-clock number is a load reading as well as a work reading; a CPU number is
        // compared at any load (see the header).
        if (bandIssue && metric === 'wall') {
            row.verdict = 'inconclusive'
            row.detail = bandIssue
        } else if (spreadsOverlap(base, now)) {
            row.verdict = 'ok'
            row.detail = 'spread overlaps'
        } else if (ratio > 1 + options.threshold / 100) {
            row.verdict = 'regression'
            row.detail =
                `median ${formatMs(base.median)} -> ${formatMs(now.median)} ` +
                `(+${((ratio - 1) * 100).toFixed(0)}%), spreads do not overlap ` +
                `(baseline ${formatMs(base.p10)}..${formatMs(base.p90)}, now ${formatMs(now.p10)}..${formatMs(now.p90)})`
        } else if (ratio < 1 - options.threshold / 100) {
            row.verdict = 'faster'
            row.detail = `median ${formatMs(base.median)} -> ${formatMs(now.median)} (${((ratio - 1) * 100).toFixed(0)}%)`
        } else {
            row.verdict = 'ok'
            row.detail = null
        }
        rows.push(row)
    }
    return { rows, bandIssue }
}

function printTable(rows) {
    const header =
        'case'.padEnd(24) +
        'plan'.padEnd(14) +
        'metric'.padEnd(7) +
        'n'.padStart(4) +
        '  ' +
        'baseline (median [p10, p90])'.padEnd(28) +
        '  ' +
        'current'.padEnd(28) +
        '  ' +
        'ratio'.padStart(6) +
        '  verdict'
    console.log(header)
    for (const row of rows) {
        const n = row.now ? row.now.n : row.entry[row.metric].n
        console.log(
            row.name.padEnd(24) +
                (row.entry.plan || '').padEnd(14) +
                row.metric.padEnd(7) +
                String(n).padStart(4) +
                '  ' +
                (row.base ? formatSpread(row.base) : '-').padEnd(28) +
                '  ' +
                formatSpread(row.now || row.entry[row.metric]).padEnd(28) +
                '  ' +
                (row.ratio ? `${row.ratio.toFixed(2)}x` : '-').padStart(6) +
                '  ' +
                row.verdict +
                (row.detail && row.verdict === 'ok' ? ` (${row.detail})` : '')
        )
    }
}

function printConditions(caseCount, options, revision, build, host, load) {
    const shown = path.relative(REPO_ROOT, options.camel) || options.camel
    log(`perf-gate: camel ${shown.replace(/\\/g, '/')} (${path.relative(REPO_ROOT, installDir(options.camel)).replace(/\\/g, '/') || '.'})`)
    if (build) {
        log(
            `perf-gate: build ${build.revision ? build.revision.slice(0, 10) : 'unknown'}` +
                `${build.dirty === null ? '' : build.dirty ? ' (dirty)' : ' (clean)'}` +
                `${build.fingerprint ? `, fingerprint ${build.fingerprint.slice(0, 12)}` : ''}` +
                `${build.built_at ? `, built ${build.built_at}` : ''}`
        )
    }
    if (load) {
        log(
            `perf-gate: host ${host.name}, ${host.cores} cores, load ${load.load1.toFixed(1)} / ` +
                `${load.load5.toFixed(1)} / ${load.load15.toFixed(1)} (1/5/15 min)`
        )
    } else {
        log(`perf-gate: host ${host.name}, ${host.cores} cores, load not reported on this platform`)
    }
    log(
        `perf-gate: ${caseCount} cases from ${options.selected.map((plan) => plan.id).join(', ')}: ` +
            `${options.reps} repetitions, ${options.warmup} warm-up rounds, the millisecond cases at least ` +
            `${MIN_REPS_WALL_CASE}`
    )
    if (revision.available) {
        log(
            `perf-gate: commit ${revision.revision.slice(0, 10)}` +
                `${revision.dirty ? ` (dirty: ${revision.dirtyFiles} file${revision.dirtyFiles === 1 ? '' : 's'})` : ' (clean)'}`
        )
    }
}

function printList(options) {
    const count = PLANS.length
    const gateable = PLANS.filter((plan) => plan.gateable)
    console.log(
        `perf-gate: ${count} plans in test/plans/perf; ${gateable.length} are gateable here ` +
            `(${gateable.map((plan) => plan.id).join(', ')}).`
    )
    console.log('This gate is manual: it is never selected by --changed and never runs under npm run test.')
    for (const plan of PLANS) {
        const verdict = plan.gateable
            ? 'gateable'
            : plan.refusal === 'expensive'
              ? 'not gateable: minutes per repetition'
              : 'refused by policy'
        console.log(`\n${plan.id}  (${plan.file})`)
        console.log(`  ${verdict}`)
        console.log(`  measures: ${plan.measures}`)
        console.log(`  cost on the shared host: ${plan.cost}`)
        console.log(`  its own baseline or ceiling: ${plan.own}`)
        if (plan.gateable) {
            for (const one of plan.cases) {
                console.log(`  command: ${displayArgv(one.argv(camelExe()))}`)
            }
        }
    }
    let baseline = null
    try {
        baseline = readBaseline(options.baseline)
    } catch (error) {
        console.log(`\nbaseline: ${displayPath(options.baseline)} is unusable (${error.message})`)
    }
    const baselinePath = displayPath(options.baseline)
    if (baseline) {
        const load = baseline.load ? `, load ${baseline.load.max.load1.toFixed(1)}` : ''
        console.log(
            `\nbaseline: ${baselinePath} (${baseline.written_at}, commit ` +
                `${baseline.commit && baseline.commit.revision ? baseline.commit.revision.slice(0, 10) : 'unknown'}, ` +
                `${Object.keys(baseline.cases).length} cases, ${baseline.reps} reps${load})`
        )
    } else {
        console.log(`\nbaseline: ${baselinePath} does not exist; --update seeds it on the host`)
    }
    console.log('\nthe default --plans is the gateable set; docs/test-plans.md, "Performance measurement"')
}

// Progress and conditions go to stdout, except in --json mode, where stdout is the JSON alone.
function log(message) {
    if (!globalThis.PERF_GATE_QUIET) {
        console.log(message)
    }
}

function main() {
    const options = parseArgs(process.argv.slice(2))
    if (options.list) {
        printList(options)
        return 0
    }
    options.selected = selectPlans(options)
    options.camel = camelExe()
    if (!fs.existsSync(options.camel)) {
        console.error(`perf-gate: no camel binary at ${options.camel}; build the tree first (npm run build, or camel-remote.ps1)`)
        return 2
    }
    globalThis.PERF_GATE_QUIET = options.json

    const cases = []
    for (const plan of options.selected) {
        for (const one of plan.cases) {
            cases.push({ ...one, plan: plan.id, argv: () => one.argv(options.camel) })
        }
    }
    // The child finds its stdlib from its own directory; setting CAMEL_HOME to the install this
    // script measured keeps an exported CAMEL_HOME from pointing the child at a different build.
    const env = { ...process.env }
    if (!process.env.CAMEL_HOME && !process.env.CAMEL_EXE) {
        env.CAMEL_HOME = installDir(options.camel)
    }
    const revision = revisionInfo()
    const build = buildStamp(options.camel)
    const host = hostInfo()
    const startLoad = readLoad()
    printConditions(cases.length, options, revision, build, host, startLoad)
    log('perf-gate: measuring; every repetition is a fresh camel process, the slowest case is seconds')

    const loads = [startLoad]
    let measured
    try {
        measured = measure(cases, options, env, () => loads.push(readLoad()))
    } catch (error) {
        console.error(`perf-gate: ${error.message}`)
        return 3
    }
    const load = loadSummary(loads)
    options.currentLoad = load ? load.max : null
    if (load) {
        log(
            `perf-gate: load during the run: ${load.start.load1.toFixed(1)} -> ${load.end.load1.toFixed(1)}, ` +
                `worst 1-minute ${load.max.load1.toFixed(1)}`
        )
    }

    if (options.update) {
        const baseline = writeBaseline(
            options.baseline,
            measured,
            options.selected.map((plan) => plan.id),
            options,
            revision,
            build,
            host,
            load
        )
        log(
            `perf-gate: wrote ${displayPath(options.baseline)}: ` +
                `${Object.keys(baseline.cases).length} cases, ${options.reps} repetitions` +
                `${load ? `, load ${load.max.load1.toFixed(1)}` : ''}`
        )
        if (host.cores > 0 && load && load.max.load1 > host.cores / 2) {
            log(
                `perf-gate: caution: this baseline was seeded at load ${load.max.load1.toFixed(1)} on ` +
                    `${host.cores} cores. A wall-clock run above load ${bandLimit(load.max.load1).toFixed(1)} is ` +
                    'inconclusive against it; the CPU-judged cases compare at any load.'
            )
        }
        if (!options.json) {
            return 0
        }
    }

    let baseline = null
    try {
        baseline = readBaseline(options.baseline)
    } catch (error) {
        console.error(`perf-gate: ${error.message}`)
        return 2
    }

    // A baseline is a property of one machine. A different name or core count is the cheap signal
    // that this run is not on it; the numbers would still compare, so say so instead of pretending.
    if (baseline && baseline.host && (baseline.host.name !== host.name || baseline.host.cores !== host.cores)) {
        console.error(
            `perf-gate: caution: the baseline was recorded on ${baseline.host.name} (${baseline.host.cores} cores); ` +
                `this is ${host.name} (${host.cores} cores). A baseline from another machine is not comparable; ` +
                '--update reseeds it here.'
        )
    }

    if (options.json) {
        const payload = {
            schema: BASELINE_SCHEMA,
            measured_at: new Date().toISOString(),
            camel: options.camel,
            host,
            load,
            reps: options.reps,
            warmup: options.warmup,
            plans: options.selected.map((plan) => plan.id),
            commit: revision,
            build,
            baseline: baseline ? { path: displayPath(options.baseline), written_at: baseline.written_at } : null,
            cases: measured,
        }
        if (baseline && !options.update) {
            payload.verdicts = compare(measured, baseline, options).rows.map((row) => ({
                case: row.name,
                metric: row.metric,
                verdict: row.verdict,
                ratio: row.ratio || null,
            }))
        }
        console.log(JSON.stringify(payload, null, 2))
        return 0
    }

    if (!baseline) {
        console.log(
            `\nperf-gate: no baseline at ${displayPath(options.baseline)}; nothing to ` +
                'compare. Run --update on the host to seed one.'
        )
        console.log('verdict: no regression claim (there is no baseline)')
        return 0
    }

    console.log(
        `perf-gate: baseline ${displayPath(options.baseline)} ` +
            `(${baseline.written_at}, commit ` +
            `${baseline.commit && baseline.commit.revision ? baseline.commit.revision.slice(0, 10) : 'unknown'}, ` +
            `${baseline.reps} reps, load ${baseline.load ? baseline.load.max.load1.toFixed(1) : 'unknown'})`
    )
    const { rows, bandIssue } = compare(measured, baseline, options)
    console.log('')
    printTable(rows)
    const regressions = rows.filter((row) => row.verdict === 'regression')
    const inconclusive = rows.filter((row) => row.verdict === 'inconclusive')
    const without = rows.filter((row) => row.verdict === 'no baseline')
    const faster = rows.filter((row) => row.verdict === 'faster')
    const ok = rows.filter((row) => row.verdict === 'ok')
    const counts =
        `${ok.length} ok, ${faster.length} faster, ${regressions.length} regression, ` +
        `${inconclusive.length} inconclusive, ${without.length} without a baseline`
    for (const row of regressions) {
        console.log(`\nREGRESSION ${row.name} (${row.metric}): ${row.detail}`)
    }
    for (const row of inconclusive) {
        console.log(`\ninconclusive ${row.name}: ${row.detail} -- not a pass`)
    }
    for (const row of without) {
        console.log(`\nno baseline for ${row.name}; the current number is ${formatSpread(row.entry[row.metric])}`)
    }
    console.log(`\nsummary: ${counts}`)
    if (regressions.length > 0) {
        console.log(`verdict: ${regressions.length} regression(s) against the baseline`)
        return 1
    }
    if (inconclusive.length > 0) {
        const loadText = options.currentLoad
            ? `measured at load ${options.currentLoad.load1.toFixed(1)}`
            : 'without a load average'
        console.log(
            `verdict: no regression in ${ok.length + faster.length} comparable case(s); ` +
                `${inconclusive.length} inconclusive (${loadText}: not a pass, rerun when the host is inside the band)`
        )
        return 0
    }
    console.log('verdict: no regression against the baseline (spreads overlap or the change is below the threshold)')
    return 0
}

process.exit(main())