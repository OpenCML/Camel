// The cost of everything `camel` does before the program runs (handover H142).
//
// Four commands, each a fresh process, timed with `process.hrtime.bigint()` around the whole
// child, and each gated on a ceiling that is a few times the median measured on the Linux host.
// The gate is deliberately generous: it catches "the standard library is now loaded before the
// program runs" or "a declaration module costs milliseconds", not a few percent of noise on a
// shared host.
//
//   node test/tools/startup-cost.mjs            measure and check the ceilings
//   node test/tools/startup-cost.mjs --reps 41  the same, with more samples
//   node test/tools/startup-cost.mjs --json     the numbers, without the gate
//
// `camel --version` is the process itself: dynamic loading, static initialisation and argument
// parsing, with no context. `imports_nothing` adds the smallest compile and run. `inspect_ops`
// imports every standard library module (`registry_all.cml`), so it is where the declaration
// modules (`stdlib/<module>.cml`) are parsed and compiled, and `imports_all` runs the same
// program through `std::nvm` instead of listing its operators.
//
// The last measurement, `declaration.cost`, is the one that can see a change of a few
// milliseconds in that work: it asks the compiler for its own `--time-phases` report of one
// `inspect --ops` run and adds up the parse, GCT and declare phases of the nine declaration
// modules, divided by the declarations they hold (handover H152). The four ceilings above are
// tens of milliseconds wide and cannot resolve it; the phase report can, and it reads about
// 20 us per declaration, which is what the ceiling is built on. The same line also prints the
// declarations' share of the compile work of the whole run: load slows that work and the
// declarations alike, so the share is the half of the pair that does not move with the host.

import { spawnSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const REGISTRY_ALL = path.join(REPO_ROOT, 'test', 'cases', 'modules', 'stdlib', 'registry_all.cml')
const IMPORTS_NOTHING = path.join(REPO_ROOT, 'test', 'cases', 'perf', 'startup', 'imports_nothing.cml')

// The modules whose `.cml` beside a C++ `.cmo` declares the operators of that module (design
// principles 6.4): their declarations are the work `declaration.cost` measures.
const DECLARATION_MODULES = ['bytes', 'str', 'math', 'json', 'io', 're', 'random', 'fs', 'time']

function camelExe() {
    if (process.env.CAMEL_EXE) {
        return process.env.CAMEL_EXE
    }
    const home = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
    return path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
}

// Median, p10 and p90 in milliseconds. The host is shared, so the median is the number the gate
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

function timeOne(argv) {
    const started = process.hrtime.bigint()
    const run = spawnSync(argv[0], argv.slice(1), {
        cwd: REPO_ROOT,
        stdio: 'ignore',
        maxBuffer: 1 << 26,
    })
    const elapsed = Number(process.hrtime.bigint() - started) / 1e6
    if (run.error) {
        throw new Error(`could not start ${argv[0]}: ${run.error.message}`)
    }
    if (run.status !== 0) {
        throw new Error(`${argv.join(' ')} exited ${run.status}`)
    }
    return elapsed
}

function measure(argv, warmup, reps) {
    for (let i = 0; i < warmup; i++) {
        timeOne(argv)
    }
    const samples = []
    for (let i = 0; i < reps; i++) {
        samples.push(timeOne(argv))
    }
    return summarize(samples)
}

// The declarations the standard library writes: one body-less `export func` per operator of a
// declared module. Counting them keeps the unit of `declaration.cost` in the sources.
function declarationCount() {
    let count = 0
    for (const module of DECLARATION_MODULES) {
        const text = fs.readFileSync(path.join(REPO_ROOT, 'stdlib', `${module}.cml`), 'utf8')
        count += (text.match(/^\s*export\s+func\s/gm) || []).length
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

// Microseconds of parse + GCT + declare per declaration, from one `inspect --ops` run: the
// compiler's own timing of the nine declaration modules, which the process wall clock cannot
// resolve (handover H152). The share is the same sum over the compile work of every module in the
// run: under load both grow together, so the share is the number that does not read as load.
function declarationCost(reps) {
    const argv = [camelExe(), 'inspect', '--ops', REGISTRY_ALL]
    const env = { ...process.env, CAMEL_TIME_PHASES: '-' }
    const declarations = declarationCount()
    const samples = []
    const shares = []
    for (let i = 0; i < reps; i++) {
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
        let nanos = 0
        let total = 0
        for (const phase of report.phases) {
            if (phase.name !== 'parse' && phase.name !== 'gct' && phase.name !== 'declare') {
                continue
            }
            total += phase.wall_ns
            if (DECLARATION_MODULES.includes(phase.detail)) {
                nanos += phase.wall_ns
            }
        }
        samples.push(nanos / 1e3 / declarations)
        shares.push(nanos / total)
    }
    return { declarations, ...summarize(samples), share: summarize(shares.map((s) => s * 100)) }
}

// The ceilings. The medians they are built on, measured on the Linux host at H142 (21-101
// samples, the numbers and their spread are in docs/handover/reports/H142.md): 5.0 / 8.5 / 26.1 /
// 23.6 ms on a quiet host. The last two ceilings are about four times that; the first two are five
// to eight times, because 5-8 ms is below this host's scheduling noise: 3 x 5 ms would flake at the
// 20 / 36.5 ms medians seen at load average 215. For the same reason the ceilings are above the
// medians at load average 150 (16.5 / 19.3 / 53.4 / 51.5 ms) and 215 (20 / 36 / 87 / 72 ms). The
// gate is a smoke alarm ("the standard library is loaded before the program runs", "a declaration
// module costs milliseconds"), not a percentage budget; it cannot see the 4.6 ms the declaration
// modules cost today, and docs/handover/reports/H142.md is where that number is recorded.
const CASES = [
    {
        name: 'version',
        argv: () => [camelExe(), '--version'],
        ceilingMs: 40,
    },
    {
        name: 'imports_nothing',
        argv: () => [camelExe(), IMPORTS_NOTHING, 'std::nvm'],
        ceilingMs: 45,
    },
    {
        name: 'inspect_ops',
        argv: () => [camelExe(), 'inspect', '--ops', REGISTRY_ALL],
        ceilingMs: 100,
    },
    {
        name: 'imports_all',
        argv: () => [camelExe(), REGISTRY_ALL, 'std::nvm'],
        ceilingMs: 90,
    },
]

function main() {
    const args = process.argv.slice(2)
    const json = args.includes('--json')
    const repsAt = args.indexOf('--reps')
    const reps = repsAt >= 0 ? Number(args[repsAt + 1]) : 21
    if (!Number.isInteger(reps) || reps < 5) {
        console.error('startup-cost: --reps needs an integer of at least 5')
        return 2
    }
    if (!fs.existsSync(camelExe())) {
        console.error(`startup-cost: no camel binary at ${camelExe()}`)
        return 2
    }
    const results = []
    const failures = []
    for (const testCase of CASES) {
        const summary = measure(testCase.argv(), 3, reps)
        results.push({ name: testCase.name, ceiling_ms: testCase.ceilingMs, ...summary })
        // `--json` is the reporting mode: it prints the numbers and gates nothing.
        if (!json && summary.median > testCase.ceilingMs) {
            failures.push(
                `${testCase.name}: median ${summary.median.toFixed(1)} ms exceeds ceiling ${testCase.ceilingMs} ms`
            )
        }
    }
    // Three times the ~20 us the declaration modules cost per declaration at H152: a smoke alarm
    // for "a declaration now costs hundreds of microseconds", not a percentage budget. A
    // regression of a few milliseconds stays under it; the printed number is what is read.
    const DECLARATION_CEILING_US = 60
    // The share of the compile work, which does not read as load: 18-19% at load average 14-30,
    // and about 45% if the 4.6 ms of H142 came back. The ceiling is the one that can catch that.
    const DECLARATION_SHARE_CEILING = 30
    const declarations = declarationCost(reps)
    if (!json && declarations.median > DECLARATION_CEILING_US) {
        failures.push(
            `declaration.cost: median ${declarations.median.toFixed(1)} us per declaration ` +
                `exceeds ceiling ${DECLARATION_CEILING_US} us`
        )
    }
    if (!json && declarations.share.median > DECLARATION_SHARE_CEILING) {
        failures.push(
            `declaration.cost: the declaration modules are ${declarations.share.median.toFixed(1)}% ` +
                `of the compile work, over the ceiling of ${DECLARATION_SHARE_CEILING}%`
        )
    }
    if (json) {
        console.log(JSON.stringify({ camel: camelExe(), reps, results, declaration: declarations }, null, 2))
        return 0
    }
    for (const result of results) {
        console.log(
            `startup.cost ${result.name.padEnd(16)} median=${result.median.toFixed(1)} ms ` +
                `p10=${result.p10.toFixed(1)} p90=${result.p90.toFixed(1)} ` +
                `min=${result.min.toFixed(1)} max=${result.max.toFixed(1)} ` +
                `n=${result.n} ceiling=${result.ceiling_ms} ms`
        )
    }
    console.log(
        `declaration.cost declarations=${declarations.declarations} ` +
            `per_declaration_us=${declarations.median.toFixed(1)} ` +
            `p10=${declarations.p10.toFixed(1)} p90=${declarations.p90.toFixed(1)} ` +
            `min=${declarations.min.toFixed(1)} max=${declarations.max.toFixed(1)} ` +
            `share=${declarations.share.median.toFixed(1)}% ` +
            `n=${declarations.n} ceiling=${DECLARATION_CEILING_US} us`
    )
    if (failures.length > 0) {
        for (const failure of failures) {
            console.error(`startup-cost: ${failure}`)
        }
        return 1
    }
    return 0
}

process.exit(main())