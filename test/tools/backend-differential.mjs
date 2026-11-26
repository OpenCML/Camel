#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The two interpreters of one program: `std::nvm` is the reference semantics, and every other back
 * end is read against it (design principles 9.2). This tool takes the programs the test plans
 * declare, runs each one twice -- once on `std::nvm`, once on `std::fvm` -- and reports, program by
 * program, whether the two produced the same answer, with a summary line of counts at the end.
 *
 * **What one program is.** A `[[tests]]` block of a plan under `test/plans/` declares a command
 * line: the case, the words before it (a subcommand, an option such as `--arg n=3`), the passes
 * written after it, and as the last of those a scheduling pass -- or none, in which case the run
 * falls through to `std::default` (design principles 9.1). That command line, with the scheduling
 * pass taken out, is one program here: the same front end, the same rewriting passes, the same
 * arguments, and only the back end replaced. A case whose plans declare two pipelines is two
 * programs, because a rewriting pass in front of the run is part of the program and dropping it
 * changes what runs. Two blocks that differ only in which scheduling pass they name are one
 * program, which is the point of the reading.
 *
 * The plan tree is read rather than the case tree because `test/plans/**` is the record of how a
 * case is run -- its case, its arguments, its environment, the pass list in front of the back end --
 * and a case read on its own has no one command line. Every case measured must be under
 * `test/cases/`; a program a plan declares against `examples/` or elsewhere is not part of this
 * reading and appears as an `[EXCLUDED]` count, never as a silent omission.
 *
 * **What is left out, and why.** Each exclusion is a count on an `[EXCLUDED]` line, by reason:
 *
 *   - a block that drives a script of this repository (`tool`), sets `envs`, asks for a precondition
 *     (`skip_unless`), names a `benchmark`, or carries `--timeit`: its command line is not "this
 *     program on this back end". This also rules out a program whose standard output is a clock
 *     rather than a value, which cannot agree with itself and would be misread as a disagreement;
 *   - a block whose last scheduling pass is neither side (`std::jit`, `std::prl`, `std::tf`) or is a
 *     rewriting pass -- the run then falls through to `std::default`, which is a third program this
 *     tool does not measure; a scheduling pass that carries `with` parameters, because a parameter
 *     is not necessarily one both sides accept, so the two halves would no longer be one program;
 *   - a pipeline that ends in `std::nop`, or that runs a pass that prints the graph before it runs:
 *     its standard output is a drawing, not the program's value;
 *   - a block that expects a front-end refusal (`expect_diagnostic_name` of `SemanticDiag::`,
 *     `ParseDiag::`, `LexDiag::`): the two back ends share the front end by construction, so a
 *     refusal says nothing about either of them;
 *   - a block whose command line is a subcommand of the tool (`check`, `inspect`), which reads a
 *     source file and runs nothing;
 *   - a block that writes into the plan runner's own scratch directory (`${test_tmp}`), which this
 *     tool does not provide;
 *   - a plan under `test/plans/probe/`: a probe is a lane's temporary reading, not the suite.
 *
 * **What "the same answer" means.** Exit code, standard output and standard error, each after the
 * normalizers the block itself declares (`normalizers`, the list `test/tools/normalizers.mjs`
 * defines) or, when it declares none, the four a plan's `[defaults]` usually carries
 * (`strip_ansi`, `normalize_newlines`, `trim_trailing_space`, `normalize_paths`). Nothing else is
 * normalized away: no number is compared at a tolerance, no line is dropped. A difference whose only
 * text is a hexadecimal address -- which the compiler writes and the run chooses -- is named as such
 * in the field it moved (`stdout(address)`, `stderr(address)`) and stays a difference, so a reader
 * can see how much of what moved is that. The one word the two commands do not share is the side's
 * pass, which this tool appends to the end of every pipeline (`std::nvm` or `std::fvm`): a diagnostic
 * that names the pass written after the last one names this tool's own argument, so both texts read
 * that word as one placeholder before they are compared or shown.
 *
 * **A disagreement is confirmed before it is reported.** Each difference is measured a second time,
 * both sides. A side that does not agree with its own first run is reported as `unstable`: the
 * program's answer depends on something other than the program (the clock, an address the allocator
 * chose, a map's order). That is not a back-end difference and is not counted as one, and it is
 * counted at all so that a program which cannot be compared is never read as one that agreed.
 *
 * **The classes**, all of them on the `[DIFFERENTIAL]` line:
 *
 *   - `agree`       the same exit code, stdout and stderr, and something was printed;
 *   - `agree-empty` the same, with nothing on stdout on either side: the exit code is the reading;
 *   - `differ`      a confirmed difference, in the field named beside it;
 *   - `no-answer`   one side did not run: it crashed, timed out, or printed nothing at all while the
 *                   other side ran;
 *   - `refused`     neither side ran: both exited non-zero with nothing printed, so only the refusal
 *                   could be compared (a case that does not compile at this tree);
 *   - `unstable`    a side disagreed with its own first run, so no comparison of the two is a
 *                   reading.
 *
 * **The controls** (`--self-test`). A harness that answered "agree" for everything would pass every
 * acceptance read on it, so the tool has to be seen answering both ways: it runs a handful of the
 * selected programs with `std::nvm` on *both* sides and requires `differ=0` (a program must agree
 * with itself), and then one real case beside a scratch copy of it in which one character of a line
 * it printed is changed -- same back end, same pipeline, so the planted difference is the only one
 * there is -- and requires that pair to be seen as `differ`. Exit code 0 only when both hold.
 *
 * Usage:
 *
 *   node test/tools/backend-differential.mjs [--camel <path>] [--jobs N] [--limit N]
 *                                            [--only <text>] [--report <file.json>]
 *                                            [--quiet] [--strict] [--self-test] [--list]
 *
 * `--list` prints the selection -- one line per program, then the exclusion counts -- and exits
 * without running anything, which is how the size of the reading is audited before it is taken.
 *
 * `--camel` names the install to measure, by the one rule in `test/tools/camel-arg.mjs`; with none it
 * is this tree's `out/latest`. Exit status: 0 when the reading completed (whatever it found), 1
 * under `--strict` when a program differs or a side did not run, 2 when the arguments or the install
 * are unusable. The reading is a measurement, not a gate leg: the plan that calls this tool asserts
 * that it produced a reading (`test/plans/feat/linear/backend_differential.plan.toml`).
 */

import { execFileSync, spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { performance } from 'node:perf_hooks'
import url from 'node:url'

import { takeCamel } from './camel-arg.mjs'
import { applyNormalizers } from './normalizers.mjs'
import { expandArgs, loadPlanTests } from './run-tests.mjs'
import { parseTomlFile } from './toml-lite.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))

/// The repository root: the first ancestor of this tool that holds the standard library. The library
/// is `stdlib/std.cml` before the layout move and `src/stdlib/std.cml` after it, so either marker
/// answers and a tool copied into a worktree still reads the tree it belongs to.
const ROOT_MARKERS = [
    ['stdlib', 'std.cml'],
    ['src', 'stdlib', 'std.cml']
]
const holdsLibrary = (dir) =>
    ROOT_MARKERS.some((parts) => {
        try {
            fs.readFileSync(path.join(dir, ...parts), 'utf8')
            return true
        } catch {
            return false
        }
    })
const REPO_ROOT = (() => {
    let dir = path.resolve(HERE)
    for (;;) {
        if (holdsLibrary(dir)) return dir
        const parent = path.dirname(dir)
        if (parent === dir) throw new Error(`no stdlib/std.cml or src/stdlib/std.cml above ${HERE}`)
        dir = parent
    }
})()

const CASES_ROOT = path.join(REPO_ROOT, 'test', 'cases')
const PLAN_ROOT = path.join(REPO_ROOT, 'test', 'plans')
const PROBE_PLANS = path.join(PLAN_ROOT, 'probe')
const VARS_PATH = path.join(REPO_ROOT, 'test', 'vars.toml')
const IS_WINDOWS = process.platform === 'win32'

/// The two sides. `std::default` is a third name a block may write, and it is not a side: a block
/// that writes it asks for whatever the default is, so its two halves are the two sides below.
const SIDES = [
    { name: 'nvm', pass: 'std::nvm' },
    { name: 'fvm', pass: 'std::fvm' }
]
/// The passes that mean one of the two sides, so that a terminal scheduling pass can be replaced by
/// the side's pass: `std::linear` is `std::nvm`, `std::fastvm` is `std::fvm` (`docs/cli.md`,
/// "Default pipeline").
const SIDE_OF_PASS = new Map([
    ['std::nvm', 'nvm'],
    ['std::nodevm', 'nvm'],
    ['std::linear', 'nvm'],
    ['std::fvm', 'fvm'],
    ['std::fastvm', 'fvm'],
    ['std::default', 'default']
])
/// A pass whose whole job is to write the graph out: a program that ends in one, or that runs one
/// before it runs, has a drawing in its standard output rather than a value.
const PRINTING_PASSES = new Set(['std::gir', 'std::rgir', 'std::agir', 'std::argir', 'std::jgir', 'std::cpp', 'std::nop'])
/// The tool's own subcommands: the first word of a block's command line names one of them when the
/// block reads a source file instead of running a program.
const SUBCOMMANDS = new Set(['check', 'inspect', 'format', 'lsp', 'debug'])
/// What a block declares when the front end refuses the case. The back ends are reached only by a
/// program the front end accepted, so these blocks are not a reading about a back end.
const FRONT_END_REFUSALS = /^(Semantic|Parse|Lex)Diag::/
const DEFAULT_NORMALIZERS = ['strip_ansi', 'normalize_newlines', 'trim_trailing_space', 'normalize_paths']

const USAGE = `usage: node test/tools/backend-differential.mjs [--camel <path>] [--jobs N] [--limit N]
                                              [--only <text>] [--report <file.json>]
                                              [--quiet] [--strict] [--self-test]`

const relative = (file) => path.relative(REPO_ROOT, file).replace(/\\/g, '/')

// ---------------------------------------------------------------------------------------------
// Selection: the programs the plan tree declares, over the cases under test/cases/.
// ---------------------------------------------------------------------------------------------

function* planFiles(dir) {
    let entries
    try {
        entries = fs.readdirSync(dir, { withFileTypes: true })
    } catch {
        return
    }
    for (const entry of entries) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) {
            if (path.resolve(full) === path.resolve(PROBE_PLANS)) continue
            yield* planFiles(full)
        } else if (entry.name.endsWith('.plan.toml')) {
            yield full
        }
    }
}

function isInside(root, target) {
    const rel = path.relative(root, target)
    return rel !== '' && !rel.startsWith('..') && !path.isAbsolute(rel)
}

function passNameOf(word) {
    if (typeof word !== 'string') return ''
    const match = /^([A-Za-z0-9_:]+)/.exec(word)
    return match ? match[1] : ''
}

/// `name::name` and no `=`: a pass word, as opposed to an option (`--arg`, `-E`) or the value of one
/// (`n=3`). The last one of a block's command line is the scheduling pass, wherever the options sit.
function isPassWord(word) {
    if (typeof word !== 'string' || word.startsWith('-') || word.includes('=')) return false
    return word.includes('::')
}

/// The reasons a block declares no program this tool measures. Every one of them is a count on an
/// `[EXCLUDED]` line: a block left out silently would make the reading's size a guess.
function reasonToLeaveOut(test) {
    if (test.__planError) return 'the plan entry could not be read'
    if (test.tool && test.tool !== 'camel') return 'the block drives a script of this repository'
    if (test.skip_unless) return 'the block asks for a precondition (skip_unless)'
    if (Array.isArray(test.envs) && test.envs.length) return 'the block sets envs'
    if (test.kind === 'benchmark') return 'the block is a benchmark'
    const args = (test.args || []).map(String)
    if (args.some((word) => word.includes('--timeit'))) return 'the block is a timing measurement (--timeit)'
    if (args.some((word) => word.includes('${test_tmp}'))) return "the block writes into the runner's scratch (${test_tmp})"
    if (FRONT_END_REFUSALS.test(String(test.expect_diagnostic_name || ''))) {
        return 'the block expects a front-end refusal, which the two back ends share'
    }
    return null
}

/**
 * Every program the plan tree declares, deduplicated by the command line that is not its scheduling
 * pass, in plan order; and the count of every reason a block declared none.
 */
function selectPrograms() {
    const vars = fs.existsSync(VARS_PATH) ? parseTomlFile(VARS_PATH) : {}
    const excluded = new Map()
    // A reason's count, plus the distinct names the reason was about (a pass, a tool, a
    // subcommand): one line per reason, so the reading's size is a short list and not a page.
    const exclude = (reason, detail) => {
        const known = excluded.get(reason) || { count: 0, details: new Set() }
        known.count += 1
        if (detail) known.details.add(String(detail))
        excluded.set(reason, known)
    }
    const programs = new Map()
    let blocks = 0
    let plans = 0

    for (const planPath of planFiles(PLAN_ROOT)) {
        plans += 1
        let loaded
        try {
            loaded = loadPlanTests(planPath)
        } catch (error) {
            exclude('the plan did not load', String(error.message).slice(0, 60))
            continue
        }
        for (const test of loaded.tests) {
            blocks += 1
            const reason = reasonToLeaveOut(test)
            if (reason) {
                exclude(reason)
                continue
            }
            const casePath = path.resolve(path.dirname(planPath), test.case)
            if (!isInside(CASES_ROOT, casePath)) {
                exclude('the case is not under test/cases/')
                continue
            }
            let expanded
            try {
                expanded = expandArgs(test.args || [], { ...vars, case: casePath, backend: vars.run_nvm })
            } catch {
                exclude('an argument did not expand')
                continue
            }
            const at = expanded.indexOf(casePath)
            if (at < 0) {
                exclude('the case is not one whole argument of the command line')
                continue
            }
            const leading = expanded.slice(0, at)
            const trailing = expanded.slice(at + 1)
            if (leading.length && SUBCOMMANDS.has(leading[0])) {
                exclude('the command line is a subcommand', leading[0])
                continue
            }
            if (trailing.some((word) => PRINTING_PASSES.has(passNameOf(word)))) {
                exclude('the pipeline prints the graph (or ends in std::nop) instead of running')
                continue
            }
            let terminalAt = -1
            for (let index = trailing.length - 1; index >= 0; index -= 1) {
                if (isPassWord(trailing[index])) {
                    terminalAt = index
                    break
                }
            }
            if (terminalAt >= 0) {
                const terminal = trailing[terminalAt]
                if (/\s/.test(terminal)) {
                    exclude('the scheduling pass carries parameters, so the two halves would not be one program')
                    continue
                }
                const side = SIDE_OF_PASS.get(passNameOf(terminal))
                if (side === undefined) {
                    exclude('the terminal pass is neither side', passNameOf(terminal))
                    continue
                }
            }
            const prefix = terminalAt >= 0 ? trailing.filter((_, index) => index !== terminalAt) : [...trailing]
            const key = `${casePath}\u0000${[...leading, '|', ...prefix].join('\u0000')}`
            const block = `${relative(planPath)}#${test.name}`
            const known = programs.get(key)
            if (known) {
                known.blocks.push(block)
                continue
            }
            programs.set(key, {
                casePath,
                caseRel: relative(casePath),
                leading,
                prefix,
                normalizers: Array.isArray(test.normalizers) ? test.normalizers : DEFAULT_NORMALIZERS,
                blocks: [block]
            })
        }
    }
    return { plans, blocks, programs: [...programs.values()], excluded }
}

// ---------------------------------------------------------------------------------------------
// Running one side.
// ---------------------------------------------------------------------------------------------

function environmentFor(install) {
    const env = { ...process.env }
    env.CAMEL_HOME = install
    // One line per diagnostic, the format `test/tools/diff-compare.mjs` reads a run in: one
    // diagnostic renders the same way on both sides, which is what makes a position comparable.
    env.CAMEL_ERROR_FORMAT = 'short'
    env.PYTHONDONTWRITEBYTECODE = '1'
    const key = IS_WINDOWS ? Object.keys(env).find((name) => name.toUpperCase() === 'PATH') || 'PATH' : 'PATH'
    env[key] = `${path.join(install, 'bin')}${path.delimiter}${env[key] || ''}`
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

function runOnce(command, args, { cwd, env, timeoutMs }) {
    return new Promise((resolve) => {
        const started = performance.now()
        let stdout = ''
        let stderr = ''
        let timedOut = false
        let settled = false
        let child = null
        const timer = setTimeout(() => {
            timedOut = true
            killTree(child)
        }, timeoutMs)
        try {
            child = spawn(command, args, { cwd, env, windowsHide: true, detached: !IS_WINDOWS })
        } catch (error) {
            clearTimeout(timer)
            resolve({ exit: null, signal: null, stdout: '', stderr: String(error.message), timedOut: false, ms: 0 })
            return
        }
        child.stdout.setEncoding('utf8')
        child.stderr.setEncoding('utf8')
        child.stdout.on('data', (chunk) => {
            stdout += chunk
        })
        child.stderr.on('data', (chunk) => {
            stderr += chunk
        })
        const finish = (code, signal) => {
            if (settled) return
            settled = true
            clearTimeout(timer)
            resolve({ exit: code, signal, stdout, stderr, timedOut, ms: performance.now() - started })
        }
        child.on('error', () => finish(null, null))
        child.on('close', (code, signal) => finish(code, signal))
    })
}

// ---------------------------------------------------------------------------------------------
// Comparing the two answers.
// ---------------------------------------------------------------------------------------------

function maskAddresses(text) {
    return String(text).replace(/0x[0-9a-fA-F]+/g, '<ADDR>')
}

/// The two commands this tool runs differ in one word: the side's own pass is appended to the end of
/// the pipeline, so a diagnostic that names the pass written after the last one (an empty graph, the
/// pipeline a run reports, the pass that could not take the program) names a word the two commands do
/// not share. That word is this tool's, not the program's: it reads as one placeholder on both sides
/// before the two texts are compared or shown, and the disagreement it would otherwise report is the
/// argument the tool was called with.
const APPENDED_PASS = /\bstd::(?:nvm|fvm)\b/g

/// What the two sides are read against each other on: the text each one printed, after the words the
/// harness itself put on its command line are taken out of the comparison.
function comparableText(text) {
    return String(text).replace(APPENDED_PASS, '<side>')
}

/// A field whose only difference is the text of a hexadecimal address, which the compiler writes and
/// the run chooses: still a difference, named apart from a value that moved.
function differenceBetween(left, right) {
    const a = comparableText(left)
    const b = comparableText(right)
    if (a === b) return null
    return maskAddresses(a) === maskAddresses(b) ? 'address' : 'value'
}

function fieldsThatMoved(a, b) {
    const fields = []
    if (a.exit !== b.exit) fields.push('exit')
    if (a.timedOut !== b.timedOut) fields.push('timeout')
    const out = differenceBetween(a.stdout, b.stdout)
    if (out) fields.push(`stdout(${out})`)
    const err = differenceBetween(a.stderr, b.stderr)
    if (err) fields.push(`stderr(${err})`)
    return fields
}

/// A side that did not run: it crashed, timed out, or printed nothing at all while the other printed
/// something.
function sideDidNotRun(reading, other) {
    if (reading.timedOut) return 'timed out'
    if (reading.signal) return `killed by ${reading.signal}`
    if (reading.exit === null) return 'could not be started'
    const barren = reading.stdout === '' && reading.stderr === ''
    const otherRan = other.stdout !== '' || other.stderr !== ''
    if (barren && otherRan) return `exit ${reading.exit}, no output at all`
    return null
}

/**
 * The verdict for one program from the two readings. `unstable` is decided by the caller, which
 * measures a disagreement a second time; this function reads one pair.
 */
function classifyPair(a, b) {
    const refusedNvm = sideDidNotRun(a, b)
    if (refusedNvm) return { verdict: 'no-answer', fields: [`nvm: ${refusedNvm}`] }
    const refusedFvm = sideDidNotRun(b, a)
    if (refusedFvm) return { verdict: 'no-answer', fields: [`fvm: ${refusedFvm}`] }
    const fields = fieldsThatMoved(a, b)
    if (fields.length) return { verdict: 'differ', fields }
    if (a.stdout !== '') return { verdict: 'agree', fields: [] }
    // Nothing on standard output: the exit code (and the diagnostic, when both stopped with one) is
    // the whole answer. A run that faulted says so on standard error; one that refused never began.
    if (a.exit !== 0 && !/RuntimeDiag::/.test(a.stderr)) return { verdict: 'refused', fields: [] }
    return { verdict: 'agree-empty', fields: [] }
}

// ---------------------------------------------------------------------------------------------
// The run.
// ---------------------------------------------------------------------------------------------

async function mapWithConcurrency(items, jobs, work) {
    const results = new Array(items.length)
    let next = 0
    const worker = async () => {
        for (;;) {
            const index = next
            next += 1
            if (index >= items.length) return
            results[index] = await work(items[index], index)
        }
    }
    await Promise.all(Array.from({ length: Math.max(1, Math.min(jobs, items.length || 1)) }, worker))
    return results
}

function commandFor(program, side, camel, env, timeoutMs) {
    const args = [...program.leading, program.casePath, ...program.prefix, side.pass]
    return runOnce(camel, args, { cwd: REPO_ROOT, env, timeoutMs })
}

async function measureProgram(program, camel, env, options) {
    const roots = { installRoot: path.dirname(path.dirname(camel)) }
    const normalize = (reading) => ({
        ...reading,
        stdout: applyNormalizers(reading.stdout, program.normalizers, roots),
        stderr: applyNormalizers(reading.stderr, program.normalizers, roots)
    })
    const reading = async (side) => normalize(await commandFor(program, side, camel, env, options.timeoutMs))
    const [a, b] = await Promise.all([reading(SIDES[0]), reading(SIDES[1])])
    let verdict = classifyPair(a, b)
    if (verdict.verdict === 'differ' || verdict.verdict === 'no-answer') {
        const [aAgain, bAgain] = await Promise.all([reading(SIDES[0]), reading(SIDES[1])])
        const unstable = []
        if (fieldsThatMoved(a, aAgain).length) unstable.push('nvm')
        if (fieldsThatMoved(b, bAgain).length) unstable.push('fvm')
        if (unstable.length) verdict = { verdict: 'unstable', fields: unstable }
    }
    return { program, a, b, verdict }
}

const quote = (text) => JSON.stringify(String(text))
const clip = (text, width = 120) => (text.length > width ? `${text.slice(0, width - 3)}...` : text)

/// One line of the field that moved, so a reader of the report does not have to run the program to
/// know what the disagreement is about. At most one line, and never instead of the verdict. The line
/// is the comparable text, so the pass this tool appended reads as the placeholder it is compared as.
function hint(a, b, fields) {
    for (const field of fields) {
        if (!field.startsWith('stdout') && !field.startsWith('stderr')) continue
        const stream = field.startsWith('stdout') ? 'stdout' : 'stderr'
        const left = comparableText(a[stream]).split('\n')
        const right = comparableText(b[stream]).split('\n')
        const at = left.findIndex((line, index) => line !== right[index])
        const index = at < 0 ? Math.min(left.length, right.length) : at
        const show = (text) => (text === undefined ? '<nothing>' : quote(clip(text)))
        return `\n    ${stream} line ${index + 1}: nvm ${show(left[index])} | fvm ${show(right[index])}`
    }
    const show = (text) => (text === '' ? '<empty>' : quote(clip(text)))
    return `\n    nvm exit ${a.exit} stdout ${show(a.stdout)} | fvm exit ${b.exit} stdout ${show(b.stdout)}`
}

function lineFor(result) {
    const { program, a, b, verdict } = result
    switch (verdict.verdict) {
        case 'differ':
            return `[differ] ${program.caseRel} :: ${verdict.fields.join(', ')}${hint(a, b, verdict.fields)}`
        case 'no-answer':
            return `[no-answer] ${program.caseRel} :: ${verdict.fields.join(', ')}${hint(a, b, verdict.fields)}`
        case 'unstable':
            return `[unstable] ${program.caseRel} :: ${verdict.fields.join(', ')} does not agree with itself`
        case 'refused':
            return `[refused] ${program.caseRel} :: exit ${a.exit}, nothing printed`
        default:
            return `[${verdict.verdict}] ${program.caseRel}`
    }
}

function summaryLine(results, options, wallMs) {
    const counts = new Map()
    const fields = new Map()
    for (const result of results) {
        counts.set(result.verdict.verdict, (counts.get(result.verdict.verdict) || 0) + 1)
        for (const field of result.verdict.fields) {
            const name = field.split(/[:(]/)[0]
            fields.set(name, (fields.get(name) || 0) + 1)
        }
    }
    const count = (name) => counts.get(name) || 0
    const fieldText = [...fields.entries()]
        .sort((left, right) => right[1] - left[1])
        .map(([name, value]) => `${name}=${value}`)
        .join(' ')
    const cases = new Set(results.map((result) => result.program.casePath)).size
    return (
        `[DIFFERENTIAL] cases=${cases} programs=${results.length} agree=${count('agree')} ` +
        `agree-empty=${count('agree-empty')} differ=${count('differ')} no-answer=${count('no-answer')} ` +
        `refused=${count('refused')} unstable=${count('unstable')} | fields ${fieldText || '(none)'} | ` +
        `jobs=${options.jobs} wall=${(wallMs / 1000).toFixed(1)}s`
    )
}

function treeIdentity() {
    try {
        const head = execFileSync('git', ['rev-parse', '--short', 'HEAD'], { cwd: REPO_ROOT, encoding: 'utf8' }).trim()
        const status = execFileSync('git', ['status', '--porcelain'], { cwd: REPO_ROOT, encoding: 'utf8' })
        const dirty = status.split('\n').filter(Boolean).length
        return `${head}${dirty ? ` (dirty: ${dirty} files)` : ''}`
    } catch {
        return 'unknown'
    }
}

// ---------------------------------------------------------------------------------------------
// The controls.
// ---------------------------------------------------------------------------------------------

async function selfTest(programs, camel, env, options, out) {
    let ok = true
    const sample = programs.slice(0, Math.min(8, programs.length))
    // (a) A program must agree with itself: one program, `std::nvm` on both sides.
    const same = await mapWithConcurrency(sample, options.jobs, async (program) => {
        const [left, right] = await Promise.all([
            commandFor(program, SIDES[0], camel, env, options.timeoutMs),
            commandFor(program, SIDES[0], camel, env, options.timeoutMs)
        ])
        return fieldsThatMoved(left, right)
    })
    const disagreed = same.filter((fields) => fields.length).length
    out(`[self-test] self-agreement (std::nvm against std::nvm) over ${sample.length} program(s): differ=${disagreed}`)
    if (disagreed !== 0) {
        out('[self-test] a program disagreed with itself: FAIL')
        ok = false
    }
    // (b) A difference that was planted must be seen, and planted on one side only: run one real case,
// then a scratch copy of it with one character of what it printed changed, on the same back end.
// Neither the back end nor the pipeline differs, so the only difference is the one planted, and a
// comparator that reads "agree" here cannot be trusted with a real pair.
    const preferred = programs.find((program) => program.caseRel === 'test/cases/linear/nvm/hello.cml')
    const source = preferred || programs.find((program) => program.prefix.length === 0) || programs[0]
    const text = fs.readFileSync(source.casePath, 'utf8')
    const real = await commandFor(source, SIDES[0], camel, env, options.timeoutMs)
    // What the case printed and what the source says: a line that is in both can be changed in one
    // place of the source and is certain to move the output. Where the program prints nothing of its
    // own (a dump, a diagnostic), fall back to the first quoted string of the source, and if there is
    // none either, say so rather than pass the control by luck.
    const printed = real.stdout
        .split('\n')
        .map((line) => line.trim())
        .find((line) => line.length >= 3 && text.includes(line))
    const literal = printed ? null : /'([^'\\\n]{3,})'/.exec(text)
    const from = printed || (literal && literal[0])
    if (!from) {
        out(`[self-test] planted difference: no line of ${source.caseRel}'s output can be found in its source: FAIL`)
        return false
    }
    const changed = text.replace(from, `${from.slice(0, -1)}?`)
    if (changed === text) {
        out(`[self-test] planted difference: nothing changed in ${source.caseRel}: FAIL`)
        return false
    }
    const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-backend-differential-'))
    const planted = { ...source, casePath: path.join(scratch, path.basename(source.casePath)) }
    fs.writeFileSync(planted.casePath, changed)
    const right = await commandFor(planted, SIDES[0], camel, env, options.timeoutMs)
    fs.rmSync(scratch, { recursive: true, force: true })
    const verdict = classifyPair(real, right)
    const seen = verdict.verdict === 'differ'
    out(
        `[self-test] planted difference: seen=${seen ? 'yes' : 'no'} ` +
            `(${verdict.verdict}${verdict.fields.length ? `: ${verdict.fields.join(', ')}` : ''}) in ${source.caseRel}`
    )
    if (!seen) {
        out('[self-test] the harness cannot see a difference it planted itself: FAIL')
        ok = false
    }
    return ok
}

// ---------------------------------------------------------------------------------------------
// Entry point.
// ---------------------------------------------------------------------------------------------

function parseArguments(argv) {
    const options = {
        jobs: Math.max(1, Math.min(8, os.cpus().length)),
        limit: 0,
        only: null,
        report: null,
        quiet: false,
        strict: false,
        selfTest: false,
        list: false,
        timeoutMs: 30000
    }
    const rest = []
    const valueOf = (argv, index) => {
        const next = argv[index]
        if (next === undefined) {
            process.stderr.write(`${USAGE}\n`)
            process.exit(2)
        }
        return next
    }
    for (let index = 0; index < argv.length; index += 1) {
        const word = argv[index]
        if (word === '--jobs') options.jobs = Math.max(1, Number(valueOf(argv, (index += 1))))
        else if (word === '--limit') options.limit = Math.max(0, Number(valueOf(argv, (index += 1))))
        else if (word === '--only') options.only = valueOf(argv, (index += 1))
        else if (word === '--report') options.report = valueOf(argv, (index += 1))
        else if (word === '--timeout-ms') options.timeoutMs = Math.max(1000, Number(valueOf(argv, (index += 1))))
        else if (word === '--quiet') options.quiet = true
        else if (word === '--strict') options.strict = true
        else if (word === '--self-test') options.selfTest = true
        else if (word === '--list') options.list = true
        else if (word === '--help' || word === '-h') {
            process.stdout.write(`${USAGE}\n`)
            process.exit(0)
        } else rest.push(word)
    }
    return { options, rest }
}

async function main(argv) {
    const { options, rest } = parseArguments(argv)
    const { camel, rest: extra } = takeCamel(rest, REPO_ROOT, (message) => {
        process.stderr.write(`${message}\n${USAGE}\n`)
        process.exit(2)
    })
    if (extra.length) {
        process.stderr.write(`unexpected argument: ${extra[0]}\n${USAGE}\n`)
        return 2
    }
    if (!options.list && !fs.existsSync(camel)) {
        process.stderr.write(`no camel binary at ${camel}; name the install with --camel <install>/bin/camel\n`)
        return 2
    }
    const out = (line) => process.stdout.write(`${line}\n`)
    const install = path.dirname(path.dirname(camel))
    const env = environmentFor(install)
    const selection = selectPrograms()
    let programs = selection.programs
    if (options.only) programs = programs.filter((program) => program.caseRel.includes(options.only))
    if (options.limit > 0) programs = programs.slice(0, options.limit)
    out(
        `[differential] tree ${treeIdentity()} | install ${install} | ` +
            `${selection.plans} plan(s), ${selection.blocks} block(s)`
    )
    for (const [reason, entry] of [...selection.excluded.entries()].sort((left, right) => right[1].count - left[1].count)) {
        const names = [...entry.details].sort()
        const shown = names.slice(0, 12).join(', ') + (names.length > 12 ? `, +${names.length - 12} more` : '')
        out(`[EXCLUDED] n=${entry.count} ${reason}${shown ? ` (${shown})` : ''}`)
    }
    out(
        `[differential] ${programs.length} program(s) over ${new Set(programs.map((program) => program.casePath)).size} case(s)` +
            `${options.only ? ` (--only ${options.only})` : ''}${options.limit ? ` (--limit ${options.limit})` : ''}`
    )
    if (options.list) {
        for (const program of programs) {
            const words = [...program.leading, ...program.prefix]
            out(`${program.caseRel}${words.length ? ` :: ${words.join(' ')}` : ''}`)
        }
        out(`[LISTED] ${programs.length} program(s) over ${new Set(programs.map((program) => program.casePath)).size} case(s)`)
        return 0
    }
    if (!programs.length) {
        process.stderr.write('no program was selected, so this run measured nothing\n')
        return 2
    }
    if (options.selfTest && !(await selfTest(programs, camel, env, options, out))) return 1
    const started = performance.now()
    const results = await mapWithConcurrency(programs, options.jobs, async (program) => {
        const result = await measureProgram(program, camel, env, options)
        if (!options.quiet || !['agree', 'agree-empty'].includes(result.verdict.verdict)) out(lineFor(result))
        return result
    })
    const wallMs = performance.now() - started
    const summary = summaryLine(results, options, wallMs)
    out(summary)
    if (options.report) {
        const document = {
            summary,
            tree: treeIdentity(),
            install,
            camel,
            jobs: options.jobs,
            wall_ms: Math.round(wallMs),
            excluded: Object.fromEntries([...selection.excluded.entries()].map(([reason, entry]) => [reason, entry.count])),
            programs: results.map((result) => ({
                case: result.program.caseRel,
                words: [...result.program.leading, ...result.program.prefix],
                blocks: result.program.blocks,
                verdict: result.verdict.verdict,
                fields: result.verdict.fields,
                nvm: { exit: result.a.exit, stdout_bytes: result.a.stdout.length, stderr_bytes: result.a.stderr.length },
                fvm: { exit: result.b.exit, stdout_bytes: result.b.stdout.length, stderr_bytes: result.b.stderr.length }
            }))
        }
        const target = path.resolve(REPO_ROOT, options.report)
        fs.mkdirSync(path.dirname(target), { recursive: true })
        fs.writeFileSync(target, `${JSON.stringify(document, null, 1)}\n`)
    }
    const broke = results.filter((result) => ['differ', 'no-answer'].includes(result.verdict.verdict))
    if (options.strict && broke.length) return 1
    return 0
}

process.exitCode = await main(process.argv.slice(2))