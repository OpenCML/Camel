// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
// `inspect --ops` run and adds up the parse, GCT and declare phases of the declaration
// modules, divided by the declarations they hold (handover H152). The four ceilings above are
// tens of milliseconds wide and cannot resolve it; the phase report can, and it reads about
// 20 us per declaration, which is what the ceiling is built on. The same line also prints the
// declarations' share of the compile work of the whole run: load slows that work and the
// declarations alike, so the share is the half of the pair that does not move with the host.
//
// The declaration modules are read from the tree, not listed here (H177): H152 pinned the nine of
// its day, thirteen more were migrated, and this tool kept pricing 182 declarations while this tree
// compiled 291 -- and the share's denominator, which includes the modules the numerator no longer
// covered, went on growing. The set comes from `scripts/stdlib-declarations.mjs`, which already
// derives it from `stdlib/<module>.cml` + the C++ module's `BuiltinModule` (handover H137); the next
// migration is measured the day its `.cml` lands. The measurement also refuses to run when a derived
// module has no phase in the report or no declaration in its `.cml`: those would price a subset and
// call it the whole.

import { spawnSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'
import { declarationModules } from '../../scripts/stdlib-declarations.mjs'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const REGISTRY_ALL = path.join(REPO_ROOT, 'test', 'cases', 'modules', 'stdlib', 'registry_all.cml')
const IMPORTS_NOTHING = path.join(REPO_ROOT, 'test', 'cases', 'perf', 'startup', 'imports_nothing.cml')

// The modules whose `.cml` beside a C++ `.cmo` declares the operators of that module (design
// principles 6.4): their declarations are the work `declaration.cost` measures. Derived from the
// tree (H177), so a new declaration module is counted without editing this tool.
const DECLARATION_MODULES = declarationModules().map((module) => module.name)
if (DECLARATION_MODULES.length === 0) {
    throw new Error('no declaration module under stdlib/: the measurement would price no declaration')
}

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

// Microseconds of parse + GCT + declare per declaration, from one `inspect --ops` run: the
// compiler's own timing of the declaration modules, which the process wall clock cannot
// resolve (handover H152). The share is the same sum over the compile work of every module in the
// run: under load both grow together, so the share is the number that does not read as load.
function declarationCost(reps) {
    const argv = [camelExe(), 'inspect', '--ops', REGISTRY_ALL]
    const env = { ...process.env, CAMEL_TIME_PHASES: '-' }
    const declarations = declarationCount()
    const samples = []
    const shares = []
    const seen = new Set()
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
                seen.add(phase.detail)
            }
        }
        if (total === 0) {
            throw new Error(`${argv.join(' ')} reported no parse, gct or declare phase: the share has no denominator`)
        }
        samples.push(nanos / 1e3 / declarations)
        shares.push(nanos / total)
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
    return { declarations, ...summarize(samples), share: summarize(shares.map((s) => s * 100)) }
}

// The ceilings. The medians they are built on, measured on the Linux host at H142 (21-101
// samples, the numbers and their spread are in handover note ⟨c49da87⟩ (H142)): 5.0 / 8.5 / 26.1 /
// 23.6 ms on a quiet host. The last two ceilings are about four times that; the first two are five
// to eight times, because 5-8 ms is below this host's scheduling noise: 3 x 5 ms would flake at the
// 20 / 36.5 ms medians seen at load average 215. For the same reason the ceilings are above the
// medians at load average 150 (16.5 / 19.3 / 53.4 / 51.5 ms) and 215 (20 / 36 / 87 / 72 ms). The
// gate is a smoke alarm ("the standard library is loaded before the program runs", "a declaration
// module costs milliseconds"), not a percentage budget; it cannot see the 4.6 ms the declaration
// modules cost in H142's day, and handover note ⟨c49da87⟩ (H142) is where that number is recorded.
//
// They were re-measured, not re-set, at H177 (same host, load average 21-22): 5.1 / 7.4 / 38.3 /
// 37.0 ms. The two commands that import every module were about 2.6 and 2.4 times the quiet median
// there, not four, because `tiles`, `tensor`, `nn` and `autodiff` load too, and because `pyplot`
// then initialised CPython from its `load()` (about 9.5 ms). H221 removed that last part: `import`
// executes no code (design principles 3.1), the interpreter is created by the first `plot` or `py_*`
// call, and the two full-import medians fell from 42.9 / 40.7 to 31.8 / 31.5 ms (load 10.7, 41
// samples; docs/technical/50_pyplot_cpython_init.md §4.3). The ceilings stay where they are on
// purpose. They are smoke alarms ("the standard library is loaded before the program runs"), the
// medians now sit about three times below them, and a first `plot` still pays the ~9 ms, so
// tightening them would turn this host's noise into a red gate without saying anything true. The
// numbers of both windows are in handover notes ⟨83d6558⟩ (H177) and the H221 report.
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
    // H177 measured 21.0 us over all 286 declarations (20.3 us over H152's nine), so the per
    // declaration cost did not move as the set grew. H221 measured 22.3 us over the 345 of this
    // tree, so this ceiling is unchanged too.
    const DECLARATION_CEILING_US = 60
    // The share of the compile work, which does not read as load: 18-19% at load average 14-30
    // in H152's tree, and about 45% if the 4.6 ms of H142 came back. H177 measured 17.8% with all
    // 286 declarations counted (10.9% over H152's nine alone), so the ceiling still passes. Read
    // it as a coarse alarm, not as a declaration budget: the denominator is the whole import +
    // compile region of the run, and H221 took CPython's ~9.5 ms out of it (the interpreter is
    // created at the first use now), so the share rose to 24.2% for the same 22.3 us per
    // declaration (load 10.7; docs/technical/50_pyplot_cpython_init.md §4.3). The ceiling was
    // left unchanged: it is not too tight, and lowering it to 25% would turn an honest change of
    // the import path into a red gate.
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