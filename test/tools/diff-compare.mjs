#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The differential comparison of core abstractions §18: **one tool, configured with what compares to
 * what**, that reads three things about a run of the same program --
 *
 *   1. **output**: exit status, signal and timeout always (every case declares `expect_exit`), and
 *      the **text** where the plan declares the text -- a block that pins its output
 *      (`expect_stdout_exact` / `expect_stdout`, or a `golden` snapshot compared as text) is compared
 *      line by line, numbers at the **declared error bound** (design principles 7.1: a transcendental
 *      answer is promised in ulps of its declared value, not bit for bit) and everything else byte
 *      for byte, after the block's own `normalizers`. A block that asserts fragments, a pattern or
 *      bounds inside a `verify_script` declares a narrower promise, and its wording is reported as
 *      `not-pinned` rather than compared: `mm.gc_pacing` prints `minor_pause_ms=0.11` and asserts the
 *      peak stays inside a window, and a comparison that read the figure would invent a difference;
 *   2. **the canonical form of the graph**: the program's pipeline run to the point where the
 *      scheduler takes it, dumped as GIR DOT and compared **up to isomorphism** (node and edge
 *      identities are not part of the canonical form; shapes are), by the same comparison a plan's
 *      `snapshot_compare = "gir_dot_isomorphic"` uses (`test/tools/gir-dot.mjs`);
 *   3. **time**: CPU milliseconds per run, **reported and never judged**, because a wall clock on a
 *      shared host measures the neighbours and a CPU figure is not a promise the language makes.
 *
 * Why it exists: stage 0 of the refactor moves the dispatch of passes, the loading of source
 * modules and the reading of rules without changing what a program does. "Nothing changed" is not
 * a thing anybody can assert about a 6,000-case suite by reading it, so those steps are accepted
 * against **this tool's difference list being empty**, and that list has to be produced by
 * something that can also say it saw a difference when one is planted (`--self-test`).
 *
 * ---------------------------------------------------------------------------------------------
 * Two configurations ship with it, and they are the same tool with different sides:
 *
 *   A. **the same suite on two commits** (`test/tools/diff-compare/two-commits.example.json`):
 *      `a.install` and `b.install` name two install roots -- two builds of two revisions, each with
 *      its own `bin/` and `stdlib/`. Every comparison runs the *current checkout's* plan files and
 *      cases against both installs, so the programs are the same and only the compiler differs.
 *      This is the configuration stage 0's acceptance reads.
 *
 *   B. **the reference executor against the other back ends**
 *      (`test/tools/diff-compare/backends.example.json`): both sides name the same install, and
 *      each side names the pass list it runs the program with -- `std::nvm` against `std::fvm`,
 *      `std::jit` or `std::prl`. The block's own terminal scheduling pass is replaced by the side's
 *      list, so a case that pins `std::jit` is compared with `std::nvm` instead of with itself.
 *      The graph reading is not part of this configuration and says so: the two sides share the
 *      front end by construction, so the graph they produce is the same graph, and comparing it
 *      would read as a pass whatever the back ends do. The reading that carries meaning here is
 *      the output one.
 *
 * ---------------------------------------------------------------------------------------------
 * What "the whole executable suite" means, and what it does not:
 *
 * Every `[[tests]]` block of every plan under `select.plans` is one case -- the *block*, not the
 * runner's expansion of it, because the plan is what declares the suite: a block that lists three
 * `backends` is one declared case, run here exactly as it is written. The report's first line is
 *
 *   [COVERAGE] declared=<blocks in the plans> compared=<blocks this run measured> excluded=<n>
 *
 * and every excluded block is named with its reason on an `[EXCLUDED]` line, grouped by reason:
 * a case that drives a script of this repository instead of the compiler (`tool = "python"`,
 * `"node"`, `"camel-format"`) is not a reading about the compiler at all; a case of the `debug`
 * tier is the deliberate abort that needs a Debug build, which a Release install cannot answer.
 * A block whose invocation prints no DOT is counted as `not-a-graph` rather than as a graph that
 * matched: the two are different numbers, and only one of them is a reading.
 *
 * ---------------------------------------------------------------------------------------------
 * Usage:
 *
 *   node test/tools/diff-compare.mjs --config <file.json> [--jobs N] [--limit N] [--only <text>]
 *                                    [--report <file.json>] [--verbose]
 *   node test/tools/diff-compare.mjs --config <file.json> --self-test
 *
 * `--limit N` measures only the first N comparable cases (`--limit 0` takes the coverage reading
 * and nothing else). Exit status: 0 when no case differs, 1 when one does, 2 when the configuration
 * or a side is unusable. `--self-test` exits 0 only when both planted differences were seen.
 *
 * The self-test plants, in a scratch copy of a real case, (a) a changed string literal -- the
 * output comparison must report a difference while the graph comparison still reads the same shape
 * -- and (b) a deleted statement -- the graph comparison must report a difference. A harness that
 * cannot see a difference it planted itself would report "identical" for everything, which is the
 * one failure mode this file exists to rule out.
 */

import { spawn } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { performance } from 'node:perf_hooks'
import url from 'node:url'

import { compareGirDotIsomorphic } from './gir-dot.mjs'
import { applyNormalizers } from './normalizers.mjs'
import { expandArgs, loadPlanTests } from './run-tests.mjs'
import { parseTomlFile } from './toml-lite.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
/// The repository root: the first ancestor of this tool that holds the standard library, so a tool
/// copied into a worktree or a scratch directory still reads the tree it belongs to.
const REPO_ROOT = (() => {
    let dir = path.resolve(HERE)
    for (;;) {
        try {
            fs.readFileSync(path.join(dir, 'stdlib', 'std.cml'), 'utf8')
            return dir
        } catch {
            const parent = path.dirname(dir)
            if (parent === dir) throw new Error(`no stdlib/std.cml above ${HERE}`)
            dir = parent
        }
    }
})()
const VARS_PATH = path.join(REPO_ROOT, 'test', 'vars.toml')
const CHILD_CPU = path.join(HERE, 'child-cpu.mjs')
const IS_WINDOWS = process.platform === 'win32'
const CAMEL_BINARY = IS_WINDOWS ? 'camel.exe' : 'camel'
const CASE_TOKEN = '${case}'
/// A word that asks a back end to schedule and run a graph: the terminal pass of a pipeline.
/// `std::prl segments=true` is one word of the same family (a pass and its parameters, as
/// `test/vars.toml` writes them), which is why the match is on the pass name and not on the word.
const SCHEDULING_PASS = /^std::(?:nvm|fvm|jit|prl)(?:\s|$)/
const DOT_HEADER = /(?:^|\n)\s*(?:strict\s+)?digraph\b/
/// The tools a case may name that are binaries of the install itself (`bin/<tool>`), so that a
/// comparison of two builds reads the build's own tools and not a script of this repository.
const INSTALL_TOOLS = new Set(['camel', 'camel-format'])

// --- the declared error bound, read out of the plan runner ---------------------
//
// The tolerance is not written here. `test/tools/run-tests.mjs` already defines exactly this
// function for plan assertions -- `ulp(actual, declared, bound, bits)`, with `ulpStep` and
// `ulpDistance` beside it (`docs/test-plans.md`, "verify_script") -- but it is defined inside the
// *source* of the verify helper module, so it is a string there and cannot be imported. Reading it
// out of that file is what keeps one definition: a case that asserts a number at 2 ulps and this
// tool's comparison of two runs of that case cannot disagree about what 2 ulps means. If that
// block moves or is rewritten, this refuses loudly rather than falling back to a second,
// silently different tolerance.
function ulpFromRunner() {
    const runner = path.join(HERE, 'run-tests.mjs')
    const source = fs.readFileSync(runner, 'utf8')
    const lines = source.split('\n')
    const begin = lines.findIndex((line) => line.startsWith('const ulpStep = '))
    const end = lines.findIndex((line) =>
        line.startsWith('const match = (pattern, text = input.normalized.output)'),
    )
    if (begin < 0 || end <= begin) {
        throw new Error(
            `cannot find the ulp helper in ${runner}: this tool compares numbers at the bound that ` +
                'file declares, and it will not fall back to a second copy of the rule',
        )
    }
    const block = lines.slice(begin, end).join('\n')
    for (const name of ['const ulpStep = ', 'const ulpDistance = ', 'const ulp = ']) {
        if (!block.includes(name)) {
            throw new Error(`the ulp helper block in ${runner} no longer defines ${name.trim()}`)
        }
    }
    // The block is self-contained by construction (it reads only `Math` and its own two
    // functions), so evaluating it costs nothing else.
    const factory = new Function(`${block}\nreturn { ulp, ulpDistance }`)
    const helpers = factory()
    if (typeof helpers.ulp !== 'function' || typeof helpers.ulpDistance !== 'function') {
        throw new Error(`the ulp helper block in ${runner} did not evaluate to two functions`)
    }
    return { ulp: helpers.ulp, source: `run-tests.mjs:${begin + 1}-${end}` }
}

const { ulp, source: ULP_SOURCE } = ulpFromRunner()

// --- the command line --------------------------------------------------------

function parseArguments(argv) {
    const options = {
        config: null,
        jobs: null,
        limit: null,
        only: null,
        verbose: false,
        report: null,
        selfTest: false,
        help: false,
    }
    const valueOf = (word, next) => {
        if (next === undefined || String(next).startsWith('--')) throw new Error(`${word} needs a value`)
        return next
    }
    for (let index = 0; index < argv.length; index += 1) {
        const word = argv[index]
        switch (word) {
            case '--config':
                options.config = valueOf(word, argv[(index += 1)])
                break
            case '--jobs':
                options.jobs = Number(valueOf(word, argv[(index += 1)]))
                break
            case '--limit':
                options.limit = Number(valueOf(word, argv[(index += 1)]))
                break
            case '--only':
                options.only = valueOf(word, argv[(index += 1)])
                break
            case '--report':
                options.report = valueOf(word, argv[(index += 1)])
                break
            case '--verbose':
                options.verbose = true
                break
            case '--self-test':
                options.selfTest = true
                break
            case '--help':
            case '-h':
                options.help = true
                break
            default:
                throw new Error(`unexpected argument '${word}'`)
        }
    }
    return options
}

function usage() {
    return [
        'usage: node test/tools/diff-compare.mjs --config <file.json> [--jobs N] [--limit N]',
        '                                          [--only <text>] [--report <file.json>] [--verbose]',
        '       node test/tools/diff-compare.mjs --config <file.json> --self-test',
    ].join('\n')
}

// --- the configuration: what compares to what --------------------------------

function sideOf(raw, which) {
    if (!raw || typeof raw.install !== 'string') {
        throw new Error(
            `config.${which}.install is required: the install root of a build (the directory that ` +
                'holds bin/ and stdlib/), which is what CAMEL_HOME points at',
        )
    }
    const install = path.resolve(raw.install)
    const binary = path.join(install, 'bin', CAMEL_BINARY)
    if (!fs.existsSync(binary)) {
        throw new Error(`config.${which}.install has no bin/${CAMEL_BINARY}: ${install}`)
    }
    let passes = null
    if (raw.passes !== undefined) {
        if (!Array.isArray(raw.passes) || raw.passes.length === 0) {
            throw new Error(`config.${which}.passes must be a non-empty list of pass words`)
        }
        passes = raw.passes.map((entry) => String(entry))
    }
    return {
        which,
        name: raw.name ? String(raw.name) : which,
        commit: raw.commit ? String(raw.commit) : null,
        install,
        binary,
        passes,
    }
}

function readConfig(file) {
    const raw = JSON.parse(fs.readFileSync(file, 'utf8'))
    // A configuration with one side is the self-check: the same install on both sides, which must
    // read as zero differences in every comparison or the tool itself has noise.
    const a = sideOf(raw.a, 'a')
    const b = raw.b === undefined ? { ...a, which: 'b', name: `${a.name} (again)` } : sideOf(raw.b, 'b')
    const select = raw.select || {}
    const compare = raw.compare || {}
    const output = raw.output || {}
    const graph = raw.graph || {}
    const config = {
        label: raw.label ? String(raw.label) : path.basename(file, '.json'),
        a,
        b,
        select: {
            plans: (select.plans || ['test/plans']).map((entry) => String(entry)),
            skip_tiers: (select.skip_tiers || ['debug']).map((entry) => String(entry)),
        },
        compare: {
            output: compare.output !== false,
            graph: compare.graph !== false,
            time: compare.time !== false,
        },
        output: {
            // The bound is a claim about a program, not a knob: a pure refactor's outputs are
            // bit-identical, so the default is the licence's two ulps at the declared width (53
            // bits, an f64 answer), and a case that declares a wider bound says so here.
            bound_ulp: Number.isFinite(output.bound_ulp) ? output.bound_ulp : 2,
            bits: Number.isFinite(output.bits) ? output.bits : 53,
            overrides: Array.isArray(output.overrides) ? output.overrides : [],
        },
        graph: { pass: graph.pass === undefined ? 'std::gir' : String(graph.pass) },
        timeout_scale: Number.isFinite(raw.timeout_scale) ? raw.timeout_scale : 2,
        jobs: Number.isFinite(raw.jobs) ? raw.jobs : 8,
    }
    // A side that names no pass list runs the block as written; a side that names one replaces the
    // block's terminal scheduling pass with it. Both sides must therefore agree on whether this is
    // a back-end comparison at all.
    if ((a.passes === null) !== (b.passes === null)) {
        throw new Error(
            'config: both sides must name a `passes` list or neither -- one side substituting a ' +
                'back end while the other runs the program as written would compare two questions',
        )
    }
    return config
}

// --- the suite: every `[[tests]]` block of every plan ------------------------

function walkPlans(target) {
    const resolved = path.resolve(REPO_ROOT, target)
    if (!fs.existsSync(resolved)) throw new Error(`no such plan target: ${target}`)
    if (fs.statSync(resolved).isFile()) return [resolved]
    const found = []
    for (const entry of fs.readdirSync(resolved, { withFileTypes: true }).sort((l, r) => (l.name < r.name ? -1 : 1))) {
        const child = path.join(resolved, entry.name)
        if (entry.isDirectory()) found.push(...walkPlans(path.relative(REPO_ROOT, child)))
        else if (entry.name.endsWith('.plan.toml')) found.push(child)
    }
    return found
}

function planLabel(planPath) {
    const relative = path.relative(REPO_ROOT, planPath)
    return relative.startsWith('..') ? planPath : relative.split(path.sep).join('/')
}

/**
 * One entry per `[[tests]]` block. The runner's own loader is asked about the same file first: the
 * schema is the runner's to state (a block it rejects has no command line this tool may build), and
 * asking it is what keeps this count and the runner's reading of the file from drifting apart.
 */
function loadBlocks(config, options) {
    const blocks = []
    const planProblems = []
    for (const target of config.select.plans) {
        for (const planPath of walkPlans(target)) {
            let doc
            try {
                doc = parseTomlFile(planPath)
            } catch (error) {
                planProblems.push(`${planLabel(planPath)}: ${error.message}`)
                continue
            }
            let schemaError = null
            try {
                loadPlanTests(planPath)
            } catch (error) {
                schemaError = error.message
            }
            const planId = doc.id || path.basename(planPath, '.plan.toml')
            const defaults = doc.defaults || {}
            for (const [index, written] of (doc.tests || []).entries()) {
                const block = {
                    ...defaults,
                    ...written,
                    __planPath: planPath,
                    __planId: planId,
                    __planTier: doc.tier,
                    __index: index,
                }
                const label = `${planId}/${block.name || `#${index + 1}`}`
                if (schemaError) block.__excluded = `the plan schema rejects this plan: ${schemaError}`
                if (options.only && !label.includes(options.only)) block.__skipped = 'not named by --only'
                blocks.push(block)
            }
        }
    }
    return { blocks, planProblems }
}

/// How one block is classified before anything runs. Three answers matter: `compared`, `excluded`
/// (with the reason, which is what `[COVERAGE]` accounts for), and `time-only` (a timed run whose
/// standard output is a measurement, so only the time reading applies to it).
function classify(block, config) {
    if (block.__excluded) return { kind: 'excluded', reason: block.__excluded }
    if (block.__skipped) return { kind: 'excluded', reason: block.__skipped }
    const tool = block.tool || 'camel'
    // A tool of the install is part of what a build produces, so its cases are compared; a tool of
    // the *harness* (`node`, `python`) runs a script of this repository and would report a
    // difference of the harness as one of the compiler.
    if (!INSTALL_TOOLS.has(tool)) {
        return {
            kind: 'excluded',
            reason:
                `tool=${tool}: the case drives a script of this repository rather than a binary of ` +
                'the install, so a difference here would be a difference of the harness',
        }
    }
    const tier = block.tier ?? block.__planTier ?? 'fast'
    if (config.select.skip_tiers.includes(String(tier))) {
        return {
            kind: 'excluded',
            reason:
                `tier=${tier}: this tier is not run on a Release install (the deliberate abort of ` +
                'test/plans/debug needs a Debug build)',
        }
    }
    const args = (block.args || []).map((word) => String(word))
    const namesCase = args.some((word) => word.includes(CASE_TOKEN))
    const timed = args.includes('--timeit') || block.kind === 'benchmark'
    if (timed) {
        return {
            kind: 'time-only',
            reason: 'a timed run (--timeit): its standard output is a measurement, so only the time reading applies',
            namesCase,
        }
    }
    if (block.case === undefined) {
        return { kind: 'excluded', reason: 'the block names no case, so there is no program to compare' }
    }
    const casePath = path.resolve(path.dirname(block.__planPath), block.case)
    if (!fs.existsSync(casePath)) {
        return { kind: 'excluded', reason: `the case file does not exist: ${planLabel(casePath)}` }
    }
    // Only the compiler's own cases have a pipeline to draw: `camel-format` prints formatted text,
    // and `std::gir` appended to its command line would ask it a question it does not take.
    return {
        kind: 'compared',
        namesCase,
        graphable: tool === 'camel' && namesCase,
        pinnedStreams: pinnedStreams(block),
    }
}

/**
 * Which of a run's streams this block declares to be the program's answer.
 *
 * `expect_stdout_exact` / `expect_stdout` pin **standard output** and say nothing about standard
 * error, which is where the compiler writes its own notices (`[INFO] <GCGraphBuild> ... staticArea=0x...`
 * carries an address the run chose). A `golden` snapshot compared as text pins the stream
 * `snapshot_stream` names, `stdout` by default. Everything else -- fragments, patterns, bounds read
 * inside a `verify_script` -- declares a promise about something narrower than a whole stream, and
 * the text there is not a reading this tool can take.
 *
 * An empty list is therefore not "compare nothing": it is the honest statement that this case never
 * promised its wording, and the tool reports it as `not-pinned`.
 */
function pinnedStreams(block) {
    const streams = new Set()
    if (block.expect_stdout_exact !== undefined || block.expect_stdout !== undefined) streams.add('stdout')
    if (block.golden !== undefined) {
        const comparison = String(block.snapshot_compare === undefined ? 'text_exact' : block.snapshot_compare)
        if (comparison !== 'gir_dot_isomorphic') streams.add(String(block.snapshot_stream || 'stdout'))
    }
    return [...streams]
}

// --- running one side --------------------------------------------------------

function pythonExecutable() {
    // Only `${python}` in a plan's arguments needs this, and a `tool = "python"` block is excluded
    // before it runs; the name is resolved anyway so a plan that hands `${python}` to the compiler
    // is not a surprise.
    if (process.env.CAMEL_PYTHON) return process.env.CAMEL_PYTHON
    return IS_WINDOWS ? 'python' : 'python3'
}

function varsFor(side) {
    const written = fs.existsSync(VARS_PATH) ? parseTomlFile(VARS_PATH) : {}
    return { ...written, python: pythonExecutable(), camel: side.binary, install: side.install }
}

/// The binary a block's `tool` names, inside the install under test: `bin/camel` for the compiler,
/// `bin/camel-format` for the formatter. A tool the install does not have is a difference the run
/// reports as unmeasurable rather than a difference it hides.
function binaryFor(block, side) {
    const tool = block.tool || 'camel'
    if (tool === 'camel') return side.binary
    return path.join(side.install, 'bin', `${tool}${IS_WINDOWS ? '.exe' : ''}`)
}

function scratchOf(side, block) {
    const safe = (text) => String(text).replace(/[^\w.-]+/g, '_')
    return path.join('tmp', 'diff', safe(side.name), safe(block.__planId), safe(block.name || `#${block.__index + 1}`))
}

function scopeOf(side, block) {
    const vars = varsFor(side)
    const scope = {
        ...vars,
        case: path.resolve(path.dirname(block.__planPath), block.case),
        test_tmp: scratchOf(side, block).split(path.sep).join('/'),
    }
    // `${backend}` is not a name but the *pass list* `test/vars.toml` writes for one (`run_nvm` is
    // `["std::nvm"]`), which is why the runner substitutes it as a whole token. The runner expands a
    // block that lists `backends` into one run per back end; this tool compares the block once, so
    // the value is the first back end it names, and the coverage line reports how many blocks that
    // applies to rather than leaving the difference in granularity unsaid.
    const first = Array.isArray(block.backends) ? block.backends[0] : null
    if (first !== undefined && first !== null && vars[`run_${String(first)}`] !== undefined) {
        scope.backend = vars[`run_${String(first)}`]
    }
    return scope
}

/// The words a side runs, for one reading. `graph` replaces the terminal scheduling pass with the
/// graph pass, so what is drawn is the graph the scheduler would have taken; a side with a `passes`
/// list (the back-end configuration) replaces it with that list instead.
function argumentsFor(block, side, mode, config) {
    let args = expandArgs(block.args || [], scopeOf(side, block))
    const first = args.findIndex((word) => SCHEDULING_PASS.test(String(word)))
    if (mode === 'graph') {
        if (first >= 0) args = args.slice(0, first)
        return [...args, config.graph.pass]
    }
    if (side.passes !== null) {
        if (first >= 0) args = args.slice(0, first)
        return [...args, ...side.passes]
    }
    return args
}

function environmentFor(block, side) {
    const env = { ...process.env }
    const scratch = path.join(REPO_ROOT, scratchOf(side, block))
    fs.mkdirSync(scratch, { recursive: true })
    env.CAMEL_HOME = side.install
    env.CAMEL_ERROR_FORMAT = 'short'
    env.CAMEL_TEST_TMPDIR = scratch
    env.PYTHONDONTWRITEBYTECODE = '1'
    const pathKey = IS_WINDOWS ? Object.keys(env).find((key) => key.toUpperCase() === 'PATH') || 'PATH' : 'PATH'
    env[pathKey] = `${path.join(side.install, 'bin')}${path.delimiter}${env[pathKey] || ''}`
    const scope = scopeOf(side, block)
    for (const entry of block.envs || []) {
        const text = String(entry).replace(/\$\{([A-Za-z0-9_.-]+)\}/g, (_, name) => {
            if (!(name in scope)) throw new Error(`unknown variable ${name} in envs of ${block.name}`)
            return String(scope[name])
        })
        const eq = text.indexOf('=')
        if (eq <= 0) throw new Error(`invalid env entry for ${block.name}: ${text}`)
        env[text.slice(0, eq)] = text.slice(eq + 1)
    }
    return env
}

function killTree(child) {
    if (!child || child.exitCode !== null || child.signalCode !== null) return
    try {
        if (IS_WINDOWS) spawn('taskkill', ['/pid', String(child.pid), '/T', '/F'], { windowsHide: true })
        else process.kill(-child.pid, 'SIGKILL')
    } catch {
        try {
            child.kill('SIGKILL')
        } catch {
            /* the child is already gone */
        }
    }
}

/**
 * One run, under the wrapper the plan runner uses for every test (`test/tools/child-cpu.mjs`): the
 * wrapper reaps exactly one command, so the CPU figure it reports on the fourth pipe is that
 * command's own -- the reading a plan's `cpu_budget_ms` is judged against, and the one this tool
 * reports instead of a wall clock.
 */
function runOnce(command, args, { cwd, env, timeoutMs }) {
    return new Promise((resolve) => {
        const started = performance.now()
        let stdout = ''
        let stderr = ''
        let report = ''
        let timedOut = false
        let settled = false
        let timer = null
        const finish = (status, signal, error) => {
            if (settled) return
            settled = true
            clearTimeout(timer)
            let cpuMs = null
            let reading = null
            const line = report
                .split('\n')
                .map((entry) => entry.trim())
                .filter(Boolean)
                .pop()
            if (line) {
                try {
                    const payload = JSON.parse(line)
                    cpuMs = typeof payload.cpu_ms === 'number' ? payload.cpu_ms : null
                    reading = payload.reading || null
                } catch {
                    reading = 'the CPU wrapper wrote no readable report'
                }
            }
            resolve({
                command: `${command} ${args.join(' ')}`.trim(),
                status: typeof status === 'number' ? status : null,
                signal: signal ?? null,
                error: error ? error.message || String(error) : null,
                timedOut,
                wallMs: performance.now() - started,
                cpuMs,
                cpuReading: reading,
                stdout,
                stderr,
            })
        }
        let child
        try {
            child = spawn(process.execPath, [CHILD_CPU, '--', command, ...args], {
                cwd,
                env,
                stdio: ['ignore', 'pipe', 'pipe', 'pipe'],
                windowsHide: true,
                detached: !IS_WINDOWS,
            })
        } catch (error) {
            finish(null, null, error)
            return
        }
        timer = setTimeout(() => {
            timedOut = true
            killTree(child)
        }, timeoutMs)
        child.stdout.setEncoding('utf8')
        child.stderr.setEncoding('utf8')
        child.stdio[3].setEncoding('utf8')
        child.stdout.on('data', (chunk) => (stdout += chunk))
        child.stderr.on('data', (chunk) => (stderr += chunk))
        child.stdio[3].on('data', (chunk) => (report += chunk))
        child.on('error', (error) => finish(null, null, error))
        child.on('close', (status, signal) => finish(status, signal, null))
    })
}

// --- the three comparisons ---------------------------------------------------

function safeRealpath(target) {
    try {
        return fs.realpathSync(target)
    } catch {
        return target
    }
}

/// What a run printed, with every spelling of a path that is a property of the install or of the
/// scratch directory replaced, and then the block's **own** normalizers applied -- the same list the
/// plan runner applies before it asserts anything (`test/tools/normalizers.mjs` is the one
/// definition of them). A case that declares `normalize_addresses` prints addresses that are not
/// part of its promise; a comparison that read them would call two runs of one program different.
function normalize(text, side, block) {
    const scratch = path.join(REPO_ROOT, scratchOf(side, block))
    const pairs = [
        [scratch, '<TMP>'],
        [scratchOf(side, block).split(path.sep).join('/'), '<TMP>'],
        [side.install, '<INSTALL>'],
        [safeRealpath(side.install), '<INSTALL>'],
        [REPO_ROOT, '<ROOT>'],
        [safeRealpath(REPO_ROOT), '<ROOT>'],
    ]
    let out = String(text).replace(/\r\n/g, '\n').replace(/\x1b\[[0-9;]*m/g, '')
    for (const [from, to] of pairs) {
        if (from) out = out.split(from).join(to)
    }
    return applyNormalizers(out, block.normalizers, { installRoot: side.install })
}

const NUMBER_TOKEN = /[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?/g

/// One line's tokens: numbers apart from everything else, so that the non-numeric skeleton is
/// compared exactly and the numbers at the declared bound.
function tokenize(line) {
    const tokens = []
    let cursor = 0
    for (const match of line.matchAll(NUMBER_TOKEN)) {
        if (match.index > cursor) tokens.push({ text: line.slice(cursor, match.index), number: false })
        tokens.push({ text: match[0], number: true })
        cursor = match.index + match[0].length
    }
    if (cursor < line.length) tokens.push({ text: line.slice(cursor), number: false })
    return tokens
}

/**
 * Whether two differing lines differ only in numbers inside the declared bound.
 *
 * An integer is compared exactly: it is a count, an index or a code, and no licence promises those
 * two units apart. A number with a fractional part or an exponent is compared with `ulp` at the
 * declared bound, read off the **left** value's grid -- the rule the plan assertions use, with the
 * left side of this comparison playing the part of the declared value. A declared zero, a NaN and
 * an infinity are exact, which `ulp` itself already does.
 */
function sameWithinBound(left, right, bound, bits) {
    const lhs = tokenize(left)
    const rhs = tokenize(right)
    if (lhs.length !== rhs.length) return false
    for (let index = 0; index < lhs.length; index += 1) {
        const one = lhs[index]
        const other = rhs[index]
        if (one.number !== other.number) return false
        if (!one.number) {
            if (one.text !== other.text) return false
            continue
        }
        const oneInteger = !/[.eE]/.test(one.text)
        const otherInteger = !/[.eE]/.test(other.text)
        if (oneInteger && otherInteger) {
            if (Number(one.text) !== Number(other.text)) return false
        } else if (!ulp(Number(other.text), Number(one.text), bound, bits)) {
            return false
        }
    }
    return true
}

function linesOf(text) {
    // A trailing newline is a property of the program's last print, not a line of its own.
    const body = text.endsWith('\n') ? text.slice(0, -1) : text
    return body === '' ? [] : body.split('\n')
}

/**
 * The output comparison.
 *
 * Exit status, signal and timeout are promises in every case (`expect_exit`), so they are compared
 * always. The **text** is a promise only where the plan says so: a block that pins its output with
 * `expect_stdout_exact` / `expect_stdout`, or with a `golden` snapshot compared as text, declares
 * that this exact wording is the program's answer, and that text is compared line by line -- numbers
 * at the declared error bound, everything else byte for byte.
 *
 * A block that asserts fragments (`expect_stdout_contains`, `expect_stdout_starts_with`), a pattern
 * (`expect_stdout_matches`) or bounds inside a `verify_script` has declared that the wording is
 * *not* its promise: `mm.gc_pacing` prints `minor_pause_ms=0.11` and asserts the peak stays inside a
 * window, and a comparison that read the millisecond figure would report a difference the suite
 * never declared. Those cases read `not-pinned`: the count is reported, and it is not a difference.
 * Exit status is still compared for them, because a crash is a promise in every case.
 */
function compareOutput(one, other, sideOne, sideOther, block, bound, bits, streams) {
    const detail = []
    // Both sides unable to answer is not a difference and not a reading either: a case that times
    // out on both installs has told this tool nothing, and calling that "the same" would be the
    // kind of silent pass this file exists to avoid.
    if ((one.timedOut && other.timedOut) || (one.error && other.error)) {
        return {
            verdict: 'unmeasurable',
            detail: [
                `a: exit=${one.status} timeout=${one.timedOut}${one.error ? ` error=${one.error}` : ''}`,
                `b: exit=${other.status} timeout=${other.timedOut}${other.error ? ` error=${other.error}` : ''}`,
            ],
        }
    }
    if (one.timedOut !== other.timedOut) detail.push(`timeout: a=${one.timedOut} b=${other.timedOut}`)
    if (one.status !== other.status) detail.push(`exit: a=${one.status} b=${other.status}`)
    if (one.signal !== other.signal) detail.push(`signal: a=${one.signal} b=${other.signal}`)
    if (!Array.isArray(streams) || streams.length === 0) {
        // The wording was never declared comparable; what the run did (its status) was. The lines
        // are still counted -- not as a difference, but as the size of what the suite chose not to
        // pin, which is the one number that says whether that choice is hiding anything.
        let drifted = 0
        for (const stream of ['stdout', 'stderr']) {
            const left = linesOf(normalize(one[stream], sideOne, block))
            const right = linesOf(normalize(other[stream], sideOther, block))
            const total = Math.max(left.length, right.length)
            for (let index = 0; index < total; index += 1) {
                if (left[index] !== right[index]) drifted += 1
            }
        }
        return detail.length === 0
            ? { verdict: 'not-pinned', detail: [], drifted }
            : { verdict: 'differ', detail, drifted }
    }
    let numericOnly = detail.length === 0
    for (const stream of streams) {
        const left = linesOf(normalize(one[stream], sideOne, block))
        const right = linesOf(normalize(other[stream], sideOther, block))
        const total = Math.max(left.length, right.length)
        for (let index = 0; index < total; index += 1) {
            const leftLine = left[index]
            const rightLine = right[index]
            if (leftLine === rightLine) continue
            if (leftLine === undefined || rightLine === undefined) {
                numericOnly = false
                detail.push(
                    `${stream} line ${index + 1}: ${leftLine === undefined ? 'b has one more line' : 'a has one more line'}`,
                )
                continue
            }
            if (sameWithinBound(leftLine, rightLine, bound, bits)) {
                detail.push(
                    `${stream} line ${index + 1} within ${bound} ulp: ${JSON.stringify(leftLine.trim())} -> ` +
                        `${JSON.stringify(rightLine.trim())}`,
                )
                continue
            }
            numericOnly = false
            detail.push(
                `${stream} line ${index + 1}: a=${JSON.stringify(leftLine.trim())} b=${JSON.stringify(rightLine.trim())}`,
            )
        }
    }
    if (detail.length === 0) return { verdict: 'same', detail }
    // A run reports as within-bound only when *every* difference in it is a number inside the
    // bound; a timeout or an exit code is never a numeric difference.
    if (numericOnly) return { verdict: 'within-bound', detail }
    return { verdict: 'differ', detail }
}

/**
 * The graph comparison: the two dumps are compared up to isomorphism first (node and edge
 * identities are not part of a canonical form), and the exact text second, so that "the same graph
 * in a different drawing" is its own number rather than a difference or a silence.
 */
function compareGraph(one, other, sideOne, sideOther, block) {
    if ((one.timedOut && other.timedOut) || (one.error && other.error)) {
        return {
            verdict: 'unmeasurable',
            detail: [
                `a: exit=${one.status} timeout=${one.timedOut}${one.error ? ` error=${one.error}` : ''}`,
                `b: exit=${other.status} timeout=${other.timedOut}${other.error ? ` error=${other.error}` : ''}`,
            ],
        }
    }
    const left = normalize(one.stdout, sideOne, block)
    const right = normalize(other.stdout, sideOther, block)
    const leftIsDot = DOT_HEADER.test(left)
    const rightIsDot = DOT_HEADER.test(right)
    if (!leftIsDot && !rightIsDot) {
        // Neither side drew a graph: this invocation is not a pipeline run (a `check`, a `-E`, a
        // `--version`, a tree dump), so there is no canonical form here to compare and the reading
        // is `not-a-graph`. What the runs printed instead is the *output* reading's business: it is
        // already compared there, by the rule that case declares, and borrowing it here would count
        // one difference twice under a name that does not describe it.
        return { verdict: 'not-a-graph', detail: [] }
    }
    if (leftIsDot !== rightIsDot) {
        return { verdict: 'differ', detail: [`a printed a graph: ${leftIsDot}; b printed one: ${rightIsDot}`] }
    }
    if (left === right) return { verdict: 'same', detail: [] }
    const comparison = compareGirDotIsomorphic(right, left)
    if (comparison.ok) {
        return {
            verdict: 'isomorphic',
            detail: ['the same graph in a different drawing (node identities or order moved)'],
        }
    }
    return { verdict: 'differ', detail: [`graph: ${comparison.message}`] }
}

// --- the sweep ---------------------------------------------------------------

function pool(tasks, width) {
    let next = 0
    const workers = Array.from({ length: Math.max(1, Math.min(width, tasks.length)) }, async () => {
        for (;;) {
            const index = next
            next += 1
            if (index >= tasks.length) return
            await tasks[index]()
        }
    })
    return Promise.all(workers)
}

function boundFor(config, block) {
    const key = `${block.__planId}/${block.name}`
    for (const override of config.output.overrides) {
        if (override && (override.test === key || override.test === block.name)) {
            return {
                bound: Number.isFinite(override.bound_ulp) ? override.bound_ulp : config.output.bound_ulp,
                bits: Number.isFinite(override.bits) ? override.bits : config.output.bits,
            }
        }
    }
    return { bound: config.output.bound_ulp, bits: config.output.bits }
}

async function measure(block, verdict, config) {
    const timeOnly = verdict.kind === 'time-only'
    const timeoutMs = (typeof block.timeout_ms === 'number' ? block.timeout_ms : 10000) * config.timeout_scale
    const runs = {}
    for (const side of [config.a, config.b]) {
        runs[side.which] = {}
        const env = environmentFor(block, side)
        for (const mode of ['output', 'graph']) {
            if (mode === 'graph' && (timeOnly || !config.compare.graph)) continue
            if (mode === 'graph' && !verdict.graphable) {
                runs[side.which].graph = null
                continue
            }
            runs[side.which][mode] = await runOnce(binaryFor(block, side), argumentsFor(block, side, mode, config), {
                cwd: REPO_ROOT,
                env,
                timeoutMs,
            })
        }
    }
    if (timeOnly) {
        // The standard output of a `--timeit` run is a measurement, not a behaviour: the only
        // reading this tool takes of such a case is its time.
        return { block, timeOnly, output: null, graph: null, runs, timeoutMs }
    }
    const { bound, bits } = boundFor(config, block)
    let output = null
    let graph = null
    if (config.compare.output && runs.a.output && runs.b.output) {
        output = compareOutput(
            runs.a.output,
            runs.b.output,
            config.a,
            config.b,
            block,
            bound,
            bits,
            verdict.pinnedStreams,
        )
    }
    if (config.compare.graph) {
        graph =
            runs.a.graph && runs.b.graph
                ? compareGraph(runs.a.graph, runs.b.graph, config.a, config.b, block)
                : { verdict: 'not-a-graph', detail: ['the invocation names no case, so there is no pipeline to draw'] }
    }
    return { block, timeOnly, output, graph, runs, timeoutMs }
}

function formatMs(value) {
    return value === null || value === undefined ? 'n/a' : `${value.toFixed(1)}ms`
}

function median(values) {
    if (values.length === 0) return null
    const sorted = [...values].sort((l, r) => l - r)
    const middle = Math.floor(sorted.length / 2)
    return sorted.length % 2 === 0 ? (sorted[middle - 1] + sorted[middle]) / 2 : sorted[middle]
}

function summaryLines(rows, config) {
    const lines = []
    const output = { same: 0, 'within-bound': 0, 'not-pinned': 0, differ: 0, unmeasurable: 0 }
    const graph = { same: 0, isomorphic: 0, differ: 0, 'not-a-graph': 0, unmeasurable: 0 }
    const cpu = { a: 0, b: 0, both: 0 }
    const ratios = []
    for (const row of rows) {
        if (row.error) continue
        if (!row.timeOnly) {
            if (row.output) output[row.output.verdict] += 1
            if (row.graph) graph[row.graph.verdict] += 1
        }
        const one = row.runs.a?.output
        const other = row.runs.b?.output
        if (config.compare.time && one && other && one.cpuMs !== null && other.cpuMs !== null) {
            cpu.a += one.cpuMs
            cpu.b += other.cpuMs
            cpu.both += 1
            ratios.push(other.cpuMs / Math.max(one.cpuMs, 0.001))
        }
    }
    lines.push(
        `[OUTPUT] same=${output.same} within_bound=${output['within-bound']} differ=${output.differ} ` +
            `not_pinned=${output['not-pinned']} unmeasurable=${output.unmeasurable} ` +
            `bound=${config.output.bound_ulp}ulp@${config.output.bits}bits (${ULP_SOURCE})`,
    )
    lines.push(
        `[GRAPH] pass=${config.graph.pass} same=${graph.same} isomorphic=${graph.isomorphic} differ=${graph.differ} ` +
            `not_a_graph=${graph['not-a-graph']} unmeasurable=${graph.unmeasurable}`,
    )
    if (config.compare.time) {
        const ratio = cpu.a > 0 ? (cpu.b / cpu.a).toFixed(3) : 'n/a'
        const middle = median(ratios)
        lines.push(
            `[TIME] cases=${cpu.both} cpu_a=${(cpu.a / 1000).toFixed(2)}s cpu_b=${(cpu.b / 1000).toFixed(2)}s ` +
                `b_over_a=${ratio} median_ratio=${middle === null ? 'n/a' : middle.toFixed(3)} ` +
                '(report only, never a verdict)',
        )
    }
    return lines
}

async function sweep(config, options) {
    const started = performance.now()
    const { blocks, planProblems } = loadBlocks(config, options)
    const declared = blocks.length
    const excludedByReason = new Map()
    const comparable = []
    let timeOnly = 0
    for (const block of blocks) {
        const verdict = classify(block, config)
        if (verdict.kind === 'excluded') {
            const list = excludedByReason.get(verdict.reason) || []
            list.push(`${block.__planId}/${block.name || `#${block.__index + 1}`}`)
            excludedByReason.set(verdict.reason, list)
            continue
        }
        if (verdict.kind === 'time-only') timeOnly += 1
        comparable.push({ block, verdict })
    }
    const excluded = declared - comparable.length
    const planned = options.limit !== null ? comparable.slice(0, options.limit) : comparable
    const rows = new Array(planned.length)
    let done = 0
    const jobs = options.jobs || config.jobs
    await pool(
        planned.map((entry, index) => async () => {
            let row
            try {
                row = await measure(entry.block, entry.verdict, config)
            } catch (error) {
                row = { block: entry.block, error: error.message || String(error), output: null, graph: null, runs: {} }
            }
            rows[index] = row
            done += 1
            if (options.verbose) {
                const name = `${entry.block.__planId}/${entry.block.name}`
                const note = row.error
                    ? `error=${row.error}`
                    : `output=${row.output ? row.output.verdict : 'not-read'} graph=${row.graph ? row.graph.verdict : 'not-read'}`
                console.log(`[CASE] ${name} ${note}`)
            } else if (done % 500 === 0) {
                console.error(`[diff-compare] ${done}/${planned.length} cases measured`)
            }
        }),
        jobs,
    )
    const seconds = ((performance.now() - started) / 1000).toFixed(1)

    console.log(
        `[COVERAGE] declared=${declared} compared=${comparable.length} excluded=${excluded} ` +
            `measured=${planned.length} time_only=${timeOnly} ` +
            `backends=${comparable.filter((entry) => Array.isArray(entry.block.backends)).length} ` +
            `plans=${new Set(blocks.map((block) => block.__planPath)).size} jobs=${jobs}`,
    )
    for (const [reason, names] of [...excludedByReason.entries()].sort((l, r) => r[1].length - l[1].length)) {
        const shown = names.slice(0, 6).join(', ')
        const more = names.length > 6 ? `, ... (${names.length - 6} more)` : ''
        console.log(`[EXCLUDED] n=${names.length} ${reason} :: ${shown}${more}`)
    }
    for (const problem of planProblems) console.log(`[PLAN] ${problem}`)
    for (const line of summaryLines(rows, config)) console.log(line)

    const differences = []
    const withinBound = []
    const isomorphic = []
    const unmeasurable = []
    for (const row of rows) {
        const name = `${row.block.__planId}/${row.block.name}`
        if (row.error) {
            unmeasurable.push({ name, detail: [row.error] })
            continue
        }
        if (row.timeOnly) continue
        if (row.output && row.output.verdict === 'differ') {
            differences.push({ kind: 'output', name, detail: row.output.detail })
        }
        if (row.graph && row.graph.verdict === 'differ') {
            differences.push({ kind: 'graph', name, detail: row.graph.detail })
        }
        if (row.output && row.output.verdict === 'within-bound') withinBound.push({ name, detail: row.output.detail })
        if (row.graph && row.graph.verdict === 'isomorphic') isomorphic.push({ name, detail: row.graph.detail })
        for (const [mode, verdict] of [['output', row.output], ['graph', row.graph]]) {
            if (verdict && verdict.verdict === 'unmeasurable') {
                unmeasurable.push({ name: `${name} (${mode})`, detail: verdict.detail })
            }
        }
    }
    for (const entry of differences) {
        console.log(`[DIFF] ${entry.kind} ${entry.name}`)
        for (const line of entry.detail.slice(0, 6)) console.log(`  ${line}`)
        if (entry.detail.length > 6) console.log(`  ... (${entry.detail.length - 6} more lines)`)
    }
    for (const entry of withinBound.slice(0, 20)) {
        console.log(`[WITHIN-BOUND] ${entry.name}`)
        for (const line of entry.detail.slice(0, 3)) console.log(`  ${line}`)
    }
    for (const entry of isomorphic.slice(0, 20)) {
        console.log(`[ISOMORPHIC] ${entry.name}: ${entry.detail[0]}`)
    }
    for (const entry of unmeasurable.slice(0, 20)) {
        console.log(`[UNMEASURABLE] ${entry.name}`)
        for (const line of entry.detail) console.log(`  ${line}`)
    }
    const timeRows = rows
        .filter(
            (row) =>
                row.runs.a?.output &&
                row.runs.b?.output &&
                row.runs.a.output.cpuMs !== null &&
                row.runs.b.output.cpuMs !== null,
        )
        .map((row) => ({
            name: `${row.block.__planId}/${row.block.name}`,
            a: row.runs.a.output.cpuMs,
            b: row.runs.b.output.cpuMs,
            delta: row.runs.b.output.cpuMs - row.runs.a.output.cpuMs,
        }))
        .sort((l, r) => Math.abs(r.delta) - Math.abs(l.delta))
    for (const entry of timeRows.slice(0, 10)) {
        console.log(
            `[TIME-TOP] ${entry.name} a=${formatMs(entry.a)} b=${formatMs(entry.b)} ` +
                `delta=${formatMs(entry.delta)} (report only)`,
        )
    }
    const notPinned = rows.filter((row) => row.output && row.output.verdict === 'not-pinned')
    const textPinned = rows.filter((row) => row.output && row.output.verdict !== 'not-pinned').length
    const driftedCases = notPinned.filter((row) => (row.output.drifted || 0) > 0).length
    const driftedLines = notPinned.reduce((sum, row) => sum + (row.output.drifted || 0), 0)
    console.log(
        `[TOTAL] label=${config.label} declared=${declared} compared=${comparable.length} measured=${planned.length} ` +
            `output_differ=${differences.filter((entry) => entry.kind === 'output').length} ` +
            `graph_differ=${differences.filter((entry) => entry.kind === 'graph').length} ` +
            `within_bound=${withinBound.length} text_pinned=${textPinned} not_pinned=${notPinned.length} ` +
            `not_pinned_drifted=${driftedCases}/${driftedLines} ` +
            `isomorphic=${isomorphic.length} ` +
            `unmeasurable=${unmeasurable.length} seconds=${seconds}`,
    )
    if (options.report) {
        const payload = {
            label: config.label,
            sides: {
                a: { name: config.a.name, install: config.a.install, commit: config.a.commit },
                b: { name: config.b.name, install: config.b.install, commit: config.b.commit },
            },
            declared,
            compared: comparable.length,
            excluded: [...excludedByReason.entries()].map(([reason, names]) => ({ reason, count: names.length, names })),
            measured: planned.length,
            differences,
            withinBound,
            isomorphic,
            unmeasurable,
            notPinned: notPinned.map((row) => `${row.block.__planId}/${row.block.name}`),
            time: timeRows,
            seconds: Number(seconds),
        }
        fs.writeFileSync(path.resolve(options.report), JSON.stringify(payload, null, 2))
        console.log(`[REPORT] ${path.resolve(options.report)}`)
    }
    return differences.length > 0 ? 1 : 0
}

// --- the plant: does the tool see a difference it was given? -----------------

/**
 * A harness that reports "identical" for everything passes every acceptance and proves nothing, so
 * the tool is given two differences it planted itself, in a scratch copy of a case of this tree:
 *
 *   * a changed string literal -- the output comparison must report it, while the graph comparison
 *     must still read the shape as the same one (a literal is a value, not a structure);
 *   * a deleted statement -- the graph comparison must report it, because the graph loses a node.
 *
 * Both plants are differences of the *program*, read by the same comparison functions the sweep
 * uses, so no second install is involved.
 */
async function selfTest(config) {
    const { blocks } = loadBlocks(config, { only: null, limit: null })
    const candidate = blocks.find((block) => {
        if ((block.tool || 'camel') !== 'camel') return false
        const verdict = classify(block, config)
        if (verdict.kind !== 'compared' || !verdict.graphable) return false
        // The plant changes standard output, so the case has to be one whose standard output is a
        // promise; a case that pins nothing would read `not-pinned` and prove nothing.
        if (!verdict.pinnedStreams.includes('stdout')) return false
        const args = (block.args || []).map((word) => String(word))
        if (args.length !== 1) return false
        const casePath = path.resolve(path.dirname(block.__planPath), block.case)
        if (!fs.existsSync(casePath)) return false
        const text = fs.readFileSync(casePath, 'utf8')
        return (text.match(/println\(\s*"/g) || []).length >= 2
    })
    if (!candidate) {
        console.log(
            '[SELF-TEST] no case of this tree has two `println("...")` statements to plant a difference ' +
                'in; the tool is not proven and the run fails',
        )
        return 1
    }
    const casePath = path.resolve(path.dirname(candidate.__planPath), candidate.case)
    const source = fs.readFileSync(casePath, 'utf8')
    const scratch = path.join(REPO_ROOT, 'tmp', 'diff', 'self-test')
    fs.mkdirSync(scratch, { recursive: true })
    const label = `${candidate.__planId}/${candidate.name}`
    const side = config.a
    const env = { ...process.env, CAMEL_HOME: side.install, CAMEL_ERROR_FORMAT: 'short' }
    const runCase = (file, args) => runOnce(binaryFor(candidate, side), args, { cwd: REPO_ROOT, env, timeoutMs: 20000 })
    const base = path.basename(casePath, '.cml')

    // (a) the changed literal, and the graph that must not move for it
    const literal = source.match(/println\(\s*("[^"\n]*")/)
    if (!literal) {
        console.log('[SELF-TEST] the chosen case has no string literal to change')
        return 1
    }
    const changed = source.replace(literal[0], `println("planted difference: ${literal[1].slice(1, -1)}"`)
    const changedPath = path.join(scratch, `${base}_literal.cml`)
    fs.writeFileSync(changedPath, changed)
    const originOutput = await runCase(casePath, [casePath])
    const changedOutput = await runCase(changedPath, [changedPath])
    const outputVerdict = compareOutput(originOutput, changedOutput, side, side, candidate, 2, 53)
    console.log(`[SELF-TEST] plant=output expected=differ got=${outputVerdict.verdict} case=${label}`)
    for (const line of outputVerdict.detail.slice(0, 2)) console.log(`  ${line}`)

    // (b) the deleted statement
    const lines = source.split('\n')
    const statement = lines.findIndex((line) => /^\s*println\(/.test(line))
    if (statement < 0) {
        console.log('[SELF-TEST] the chosen case has no `println(...)` statement to delete')
        return 1
    }
    const trimmedPath = path.join(scratch, `${base}_no_statement.cml`)
    fs.writeFileSync(trimmedPath, lines.filter((_, index) => index !== statement).join('\n'))
    const graphOne = await runCase(casePath, [casePath, config.graph.pass])
    const graphOther = await runCase(trimmedPath, [trimmedPath, config.graph.pass])
    const graphVerdict = compareGraph(graphOne, graphOther, side, side, candidate)
    console.log(`[SELF-TEST] plant=graph expected=differ got=${graphVerdict.verdict} case=${label}`)
    for (const line of graphVerdict.detail.slice(0, 2)) console.log(`  ${line}`)

    // (c) the literal again, read as a graph: a changed value is not a changed shape
    const literalGraph = await runCase(changedPath, [changedPath, config.graph.pass])
    const literalGraphVerdict = compareGraph(graphOne, literalGraph, side, side, candidate)
    console.log(`[SELF-TEST] plant=graph-literal expected=same got=${literalGraphVerdict.verdict} case=${label}`)

    const seen = outputVerdict.verdict === 'differ' && graphVerdict.verdict === 'differ'
    const quiet = literalGraphVerdict.verdict === 'same' || literalGraphVerdict.verdict === 'isomorphic'
    console.log(
        `[SELF-TEST] seen=${seen && quiet} (a planted literal change must be an output difference; a ` +
            'planted statement deletion must be a graph difference)',
    )
    return seen && quiet ? 0 : 1
}

// --- entry -------------------------------------------------------------------

async function main(argv) {
    let options
    try {
        options = parseArguments(argv)
    } catch (error) {
        console.log(`diff-compare: ${error.message}\n${usage()}`)
        return 2
    }
    if (options.help) {
        console.log(usage())
        return 0
    }
    if (!options.config) {
        console.log(`diff-compare: --config is required\n${usage()}`)
        return 2
    }
    let config
    try {
        config = readConfig(options.config)
    } catch (error) {
        console.log(`diff-compare: ${error.message}`)
        return 2
    }
    console.log(
        `[CONFIG] label=${config.label} a=${config.a.name}@${config.a.commit || '?'} (${config.a.install}) ` +
            `b=${config.b.name}@${config.b.commit || '?'} (${config.b.install}) ` +
            `compare=${['output', 'graph', 'time'].filter((entry) => config.compare[entry]).join('+')}`,
    )
    try {
        return options.selfTest ? await selfTest(config) : await sweep(config, options)
    } catch (error) {
        console.log(`diff-compare: ${error && error.stack ? error.stack : error}`)
        return 2
    }
}

process.exitCode = await main(process.argv.slice(2))