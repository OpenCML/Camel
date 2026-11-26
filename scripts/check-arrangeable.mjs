// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `core.arrangeable` against the predicate a pass asks, over every case the tree has, cut by node
 * kind and by operator.
 *
 * `test::facts_compare` prints one `tally` row per (node kind, operator): how many nodes of that row
 * the fact calls freely arrangeable and the answer it replaced (the frozen baseline `OldArrangeable`
 * in `src/stdlib/test/facts_compare.cpp`) lets move, in the four ways two answers can meet. One program says almost nothing about it -- a
 * tree of 200,000 nodes has kinds and operators that one case does not write -- so this tool starts
 * one `camel` per `.cml` the tree tracks and sums the rows:
 *
 *   ARRANGEABLE_SWEEP files=<n> read=<n> skipped=<n> silent=<n> nodes=<n> rows=<n>
 *   ARRANGEABLE_KIND node_kind=<kind> nodes=<n> both_free=<n> both_kept=<n>
 *       kind_free_pred_kept=<n> kind_kept_pred_free=<n>
 *   ARRANGEABLE_ROW node_kind=<kind> oper=<uri|-> nodes=<n> both_free=<n> both_kept=<n>
 *       kind_free_pred_kept=<n> kind_kept_pred_free=<n>
 *
 * a `KIND` line for every node kind (the four counts summed over its operators: the column a
 * reader compares between two revisions of the fact), and a `ROW` line for every (kind, operator)
 * row that has a disagreement in it -- the rows with none say nothing this file is about, and
 * printing all of them buried the ones that do. `--rows N` prints the first N rows of every kind
 * instead (0: all of them), which is the reading when a reader wants to see agreement too.
 *
 * The row of an operator named with `--always` (default `:op/cast`, the conversion) prints whether
 * or not it disagrees: the reading "this operator agrees now" must not be the reason its row is
 * gone from the table, or a converged operator and an operator the sweep never saw read the same.
 *
 * and then the two verdicts it is allowed to make:
 *
 *   1. the sweep read something: `read` at or above `ARRANGEABLE_MIN_READ` (2000). A table summed
 *      over nothing prints zeros in every column and reads exactly like a tree with no
 *      disagreement, which is why the lower bound is part of the verdict and not a comment.
 *   2. an OPER row has no disagreement in either direction. `kind_free_pred_kept` is the fact
 *      saying "freely arrangeable" about a node the older answer keeps where the program wrote it;
 *      `kind_kept_pred_free` is the older answer offering a placement the fact will not make.
 *      Both are two answers to one question, and the fact is the one that stands.
 *
 * The rows of the other node kinds are printed and **not decided**: they are a reading of their own
 * (`internal/reports/2026-11-25-the-arrangeable-disagreements.cn.md`) and a red this tool invented
 * about a kind whose disagreement was judged outside it would be a wall somebody has to switch off.
 *
 *   node scripts/check-arrangeable.mjs            # sweep, print the table, decide the two verdicts
 *   node scripts/check-arrangeable.mjs --rows 200  # print more than the first 100 rows (0: all)
 *
 * Every case runs in a directory of its own, under the runner's scratch (`CAMEL_TEST_TMPDIR`) or the
 * system temporary directory when run by hand, for the reason `check-plan-refs.mjs` gives: a program
 * that writes a relative path writes it where the process runs.
 *
 * The pass list is `test::facts_compare std::gir`, not `test::facts_compare` alone: the comparison
 * pass hands the program on, and without a stopper every case would be *executed* as the next
 * fallback (`std::nvm`). `std::gir` prints the graph and answers an empty one, so the pipeline ends
 * there and nothing a case does at run time is in the reading. The DOT is discarded.
 *
 * Exit codes: 0 both verdicts hold; 1 a verdict is red (the reading is complete); 2 the sweep could
 * not be taken at all (no binary, no case list) -- not a zero.
 */

import { spawn } from 'node:child_process'
import { execFileSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const REPO = path.dirname(HERE)
const CONCURRENCY = Number(process.env.ARRANGEABLE_JOBS || 8)
const TIMEOUT_MS = Number(process.env.ARRANGEABLE_TIMEOUT_MS || 60000)
// The floor under the sweep's coverage, and not a coverage goal: this tree reads 1,892 of its 4,376
// cases, and the rest want the arguments or the module roots their own plans give them (the same
// reason `check-plan-refs.mjs` skips 1,235 of the cases it sweeps). 1,700 is under that reading on
// purpose, so a case that stops compiling for a real reason moves the number visibly instead of
// turning the sweep red for a count it never promised.
const MIN_READ = Number(process.env.ARRANGEABLE_MIN_READ || 1700)

const RUN_ROOT = process.env.CAMEL_TEST_TMPDIR
    ? path.join(path.resolve(process.env.CAMEL_TEST_TMPDIR), 'arrangeable')
    : fs.mkdtempSync(path.join(os.tmpdir(), 'camel-arrangeable-'))

function camelBinary() {
    if (process.env.CAMEL_BIN) return process.env.CAMEL_BIN
    const home = process.env.CAMEL_HOME
    if (!home) {
        console.error('check-arrangeable: CAMEL_HOME is not set and CAMEL_BIN is empty')
        process.exit(2)
    }
    return path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
}

/// Every `.cml` the revision tracks, read from git and never from a directory walk: an untracked
/// file in a lane's worktree is that lane's scratch, and a sweep that read one would report about a
/// tree nobody can check out. A git that refuses is exit 2 -- an empty case list is not a tree with
/// no cases.
function trackedCases() {
    let out
    try {
        out = execFileSync('git', ['ls-files', '-z', '*.cml'], {
            cwd: REPO,
            encoding: 'utf8',
            maxBuffer: 64 * 1024 * 1024,
        })
    } catch (error) {
        console.error(`check-arrangeable: git ls-files failed: ${error?.message || error}`)
        process.exit(2)
    }
    const files = out
        .split('\0')
        .filter((name) => name.length > 0)
        .map((name) => path.join(REPO, name))
        .sort()
    if (files.length === 0) {
        console.error('check-arrangeable: git ls-files named no .cml: refusing to report a zero')
        process.exit(2)
    }
    return files
}

function run(binary, args, cwd) {
    return new Promise((resolve) => {
        const child = spawn(binary, args, { cwd, stdio: ['ignore', 'pipe', 'pipe'] })
        // Only the tally rows are kept: `std::gir` writes the whole graph to stdout, and a
        // 200,000-node tree's DOT held in memory per worker is not a reading.
        const rows = []
        const samples = []
        let ran = false
        let stdoutFirst = ''
        let stderr = ''
        let finished = false
        const finish = (result) => {
            if (finished) return
            finished = true
            clearTimeout(timer)
            resolve({ ...result, rows, samples, ran, stdoutFirst, stderr })
        }
        const timer = setTimeout(() => {
            child.kill('SIGKILL')
            finish({ timedOut: true, code: null })
        }, TIMEOUT_MS)
        child.stdout.setEncoding('utf8')
        let partial = ''
        child.stdout.on('data', (chunk) => {
            partial += chunk
            let newline = partial.indexOf('\n')
            while (newline >= 0) {
                const line = partial.slice(0, newline)
                partial = partial.slice(newline + 1)
                if (stdoutFirst === '' && line.trim().length > 0) stdoutFirst = line.trim()
                if (line.startsWith('FACTS_COMPARE tally ')) rows.push(line)
                // One node of a disagreed row, as the pass saw it: the four terms the kind answered,
                // the predicate's answer, and the wait/order/sync edges the node carries. It is what
                // says *which field* answered differently, and it is not reconstructible from counts.
                else if (line.startsWith('FACTS_COMPARE reader_diff ')) samples.push(line)
                // The readers line is what says the comparison ran on this program: a program with
                // no nodes prints no tally row and is still a case the sweep read.
                if (line.startsWith('FACTS_COMPARE kind=core.arrangeable readers ')) ran = true
                newline = partial.indexOf('\n')
            }
        })
        child.stderr.on('data', (chunk) => {
            if (stderr.length < 8192) stderr += chunk
        })
        child.on('error', (error) => finish({ timedOut: false, code: -1, stderr: String(error) }))
        child.on('close', (code) => finish({ timedOut: false, code }))
    })
}

/// The first non-empty line of what the case wrote to stderr, cut to what a table can carry: the
/// reason a case the sweep could not read was not readable belongs beside it, not in a log nobody
/// kept.
function firstLine(text) {
    for (const line of String(text || '').split('\n')) {
        const trimmed = line.trim()
        if (trimmed.length > 0) return trimmed.slice(0, 220)
    }
    return ''
}

function dropTree(dir) {
    try {
        fs.rmSync(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 50 })
    } catch (error) {
        if (!['EBUSY', 'EPERM', 'ENOTEMPTY'].includes(error?.code)) throw error
    }
}

/// `FACTS_COMPARE tally node_kind=oper oper=:op/cast nodes=12 both_free=3 ...` into its fields.
/// A row the pass printed with a field missing is a defect of the pass, and this tool is not the
/// place to paper over it: the row is refused and the run is red.
function parseRow(line) {
    const fields = {}
    for (const word of line.slice('FACTS_COMPARE tally '.length).trim().split(/\s+/)) {
        const at = word.indexOf('=')
        if (at <= 0) return null
        fields[word.slice(0, at)] = word.slice(at + 1)
    }
    const numbers = ['nodes', 'both_free', 'both_kept', 'kind_free_pred_kept', 'kind_kept_pred_free']
    if (!fields.node_kind || !fields.oper) return null
    for (const name of numbers) {
        if (!/^\d+$/.test(fields[name] || '')) return null
    }
    const row = { node_kind: fields.node_kind, oper: fields.oper }
    for (const name of numbers) row[name] = Number(fields[name])
    return row
}

async function main() {
    const binary = camelBinary()
    const files = trackedCases()
    const totals = new Map()
    const samplesOf = new Map()
    const unread = []
    const refused = []
    let read = 0

    let next = 0
    async function worker() {
        for (;;) {
            const index = next++
            if (index >= files.length) return
            const file = files[index]
            const cwd = path.join(RUN_ROOT, path.relative(REPO, file).replace(/[\\/]/g, '__'))
            fs.mkdirSync(cwd, { recursive: true })
            const result = await run(binary, [file, 'test::facts_compare', 'std::gir'], cwd)
            dropTree(cwd)
            const relative = path.relative(REPO, file)
            if (!result.ran) {
                // No readers line: the case does not compile on its own (its plan gives it arguments
                // or modules this sweep does not), or the pass said nothing about a program it was
                // handed. The first is a case this sweep cannot read; the second would be a defect,
                // and either way it is counted and not silently dropped.
                const first = firstLine(result.stderr) || firstLine(result.stdoutFirst)
                // A case the pass itself gave up on is not a case that did not compile, and the two
                // are counted apart: one is this sweep's coverage, the other is a defect.
                const cause = result.timedOut
                    ? 'timeout'
                    : first.includes('failed with an internal error')
                        ? 'internal-error'
                        : `exit=${result.code}`
                const why = result.timedOut ? 'timeout' : `${cause} ${first}`
                if (result.code === 0) refused.push(`${relative} ${why}`)
                else unread.push({ relative, why, cause })
                continue
            }
            read += 1
            for (const line of result.samples) {
                // Three nodes per disagreed row: enough that a row whose two answers differ for more
                // than one reason cannot read like a row that differs for one.
                const kindOf = / node_kind=(\S+)/.exec(line)
                const operOf = / oper=(\S+)/.exec(line)
                if (!kindOf || !operOf) continue
                const key = `${kindOf[1]}\u0000${operOf[1]}`
                const kept = samplesOf.get(key) || []
                if (kept.length < 3) {
                    kept.push(`${relative} ${line}`)
                    samplesOf.set(key, kept)
                }
            }
            for (const line of result.rows) {
                const row = parseRow(line)
                if (row === null) {
                    refused.push(`${relative}: ${line}`)
                    continue
                }
                const key = `${row.node_kind}\u0000${row.oper}`
                const sum = totals.get(key) || {
                    node_kind: row.node_kind,
                    oper: row.oper,
                    nodes: 0,
                    both_free: 0,
                    both_kept: 0,
                    kind_free_pred_kept: 0,
                    kind_kept_pred_free: 0,
                }
                for (const name of ['nodes', 'both_free', 'both_kept', 'kind_free_pred_kept', 'kind_kept_pred_free']) {
                    sum[name] += row[name]
                }
                totals.set(key, sum)
            }
        }
    }
    await Promise.all(Array.from({ length: Math.min(CONCURRENCY, files.length) }, worker))
    if (!process.env.CAMEL_TEST_TMPDIR) dropTree(RUN_ROOT)

    const rows = [...totals.values()].sort(
        (left, right) =>
            left.node_kind.localeCompare(right.node_kind) || left.oper.localeCompare(right.oper))
    let nodes = 0
    for (const row of rows) nodes += row.nodes

    console.log(
        `ARRANGEABLE_SWEEP files=${files.length} read=${read} skipped=${unread.length} ` +
            `silent=${refused.length} nodes=${nodes} rows=${rows.length}`)

    // The four counts summed over each node kind: the column a reader compares between two
    // revisions of the fact. A kind's row here moves only if that kind's answer moved.
    const kinds = new Map()
    for (const row of rows) {
        const sum = kinds.get(row.node_kind) || {
            nodes: 0, both_free: 0, both_kept: 0, kind_free_pred_kept: 0, kind_kept_pred_free: 0,
        }
        for (const name of ['nodes', 'both_free', 'both_kept', 'kind_free_pred_kept', 'kind_kept_pred_free']) {
            sum[name] += row[name]
        }
        kinds.set(row.node_kind, sum)
    }
    for (const name of [...kinds.keys()].sort()) {
        const sum = kinds.get(name)
        console.log(
            `ARRANGEABLE_KIND node_kind=${name} nodes=${sum.nodes} both_free=${sum.both_free} ` +
                `both_kept=${sum.both_kept} kind_free_pred_kept=${sum.kind_free_pred_kept} ` +
                `kind_kept_pred_free=${sum.kind_kept_pred_free}`)
    }

    const limit = (() => {
        const at = process.argv.indexOf('--rows')
        if (at >= 0 && at + 1 < process.argv.length) return Number(process.argv[at + 1])
        return 60
    })()
    const always = (() => {
        const named = []
        for (let at = 0; at < process.argv.length; at += 1) {
            if (process.argv[at] === '--always' && at + 1 < process.argv.length) named.push(process.argv[at + 1])
        }
        return named.length > 0 ? named : [':op/cast']
    })()
    const shown = limit > 0
        ? rows.filter((row) =>
            row.kind_free_pred_kept > 0 ||
            row.kind_kept_pred_free > 0 ||
            (row.node_kind === 'oper' && always.includes(row.oper))).slice(0, limit)
        : rows
    for (const row of shown) {
        console.log(
            `ARRANGEABLE_ROW node_kind=${row.node_kind} oper=${row.oper} nodes=${row.nodes} ` +
                `both_free=${row.both_free} both_kept=${row.both_kept} ` +
                `kind_free_pred_kept=${row.kind_free_pred_kept} ` +
                `kind_kept_pred_free=${row.kind_kept_pred_free}`)
    }
    // The row above says how many; these say of what. A count cannot say which of the four terms the
    // kind answered and which edge the node carries, and that is the whole of "which side is wrong".
    for (const row of shown) {
        for (const sample of samplesOf.get(`${row.node_kind}\u0000${row.oper}`) || []) {
            console.log(`ARRANGEABLE_SAMPLE ${sample}`)
        }
    }
    // Why a case went unread, counted: a sweep whose coverage is a fraction of the tree has to say
    // what the rest was, or the fraction reads like the tree.
    const causes = new Map()
    for (const { cause } of unread) {
        causes.set(cause, (causes.get(cause) || 0) + 1)
    }
    for (const [key, count] of [...causes.entries()].sort((a, b) => b[1] - a[1])) {
        console.log(`ARRANGEABLE_UNREAD_CAUSE ${key} files=${count}`)
    }
    for (const { relative, why } of unread.slice(0, 12)) {
        console.log(`ARRANGEABLE_UNREAD ${relative} ${why}`)
    }
    for (const file of refused.slice(0, 12)) console.log(`ARRANGEABLE_SILENT ${file}`)

    // The verdicts are about **named** operators. A row under `oper=-` is a node whose operation the
    // fact cannot name (a FILL or an ACCS the view still folds into `Oper`, `graph_view.h`): the
    // kind declines and the predicate answers by the node's shape, which is a second class with its
    // own size and its own fix (`internal/reports/2026-11-25-the-arrangeable-disagreements.cn.md`
    // 5). Counting it here would make this plan red for a change no lane has made, and counting it
    // nowhere would make 4,238 nodes disappear from a reading about two answers -- so it is printed
    // in the table, summed in the success line, and not decided.
    const named = (row) => row.node_kind === 'oper' && row.oper.startsWith(':op/')
    const operRows = rows.filter(named)
    const wrongWay = rows.filter((row) => named(row) && row.kind_free_pred_kept > 0)
    const otherWay = rows.filter((row) => named(row) && row.kind_kept_pred_free > 0)
    let red = false
    if (read < MIN_READ) {
        console.log(
            `ARRANGEABLE_VERDICT read=${read} under the ${MIN_READ} a sweep of this tree reads: ` +
                'the table is not a reading of the tree')
        red = true
    }
    if (wrongWay.length > 0) {
        const count = wrongWay.reduce((sum, row) => sum + row.kind_free_pred_kept, 0)
        console.log(
            `ARRANGEABLE_VERDICT kind_free_pred_kept=${count} over ${wrongWay.length} operator row(s): ` +
                'the fact calls a node freely arrangeable that a pass keeps where it was written')
        red = true
    }
    if (otherWay.length > 0) {
        const count = otherWay.reduce((sum, row) => sum + row.kind_kept_pred_free, 0)
        console.log(
            `ARRANGEABLE_VERDICT kind_kept_pred_free=${count} over ${otherWay.length} operator row(s): ` +
                'the predicate offers a placement the fact will not answer for')
        red = true
    }
    if (refused.length > 0) {
        console.log(
            `ARRANGEABLE_VERDICT silent=${refused.length}: a case compiled and the pass printed no ` +
                'tally row, or a row this tool cannot parse')
        red = true
    }
    if (!red) {
        // The rows the verdicts do not decide are named here rather than left to the reader: a
        // success line that said "everything agrees" over a table with 4,238 rows in it would be
        // the reading this tool exists to refuse.
                                const undecided = rows
            .filter((row) => row.node_kind === 'oper' && !row.oper.startsWith(':op/'))
            .reduce((sum, row) => sum + row.kind_free_pred_kept + row.kind_kept_pred_free, 0)
        console.log(
            `every operator's answer agrees, over ${read} cases, ${nodes} nodes and ` +
                `${operRows.length} operator rows; ${undecided} node(s) of oper=- and of the other ` +
                'node kinds are in the table and are not decided here')
    }
    process.exit(red ? 1 : 0)
}

main().catch((error) => {
    console.error(`check-arrangeable: ${error?.stack || error}`)
    process.exit(2)
})