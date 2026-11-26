// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `core.arrangeable`'s own answers, over every case the tree has, cut by node kind, by operator and
 * by the family the answer comes from.
 *
 * `test::facts_compare` prints one `answers_row` per (node kind, operator): how many nodes of that
 * row the fact answers freely arrangeable (`free`, all four terms of design principles 3.4),
 * answers no (`kept`), or answers nothing for at all (`no_fact`). One program says almost nothing
 * about it -- a tree of a quarter of a million nodes has kinds and operators that one case does not
 * write -- so this tool starts one `camel` per `.cml` the tree tracks and sums the rows:
 *
 *   ARRANGEABLE_SWEEP files=<n> read=<n> skipped=<n> silent=<n> nodes=<n> free=<n> kept=<n>
 *       no_fact=<n> rows=<n>
 *   ARRANGEABLE_KIND node_kind=<kind> nodes=<n> free=<n> kept=<n> no_fact=<n>
 *   ARRANGEABLE_FAMILY family=<name> nodes=<n> free=<n> kept=<n> no_fact=<n>
 *   ARRANGEABLE_ROW node_kind=<kind> oper=<uri|-> nodes=<n> free=<n> kept=<n> no_fact=<n>
 *   ARRANGEABLE_SAMPLE <family> <file> <graph>:<node>
 *
 * a `KIND` line for every node kind, a `FAMILY` line for every family the tree answered in, and a
 * `ROW` line for the first `--rows N` rows (default 60, `0` for all) plus every operator named with
 * `--always` (default `:op/cast`, the conversion) whether or not it is inside that window: the
 * reading "this operator's row is as it is" must not be the reason its row is missing from the table.
 * `SAMPLE` gives up to three nodes per family, with the program they came from -- a count says a
 * family is kept, an example says in which program and at which node to look.
 *
 * and then the verdicts it is allowed to make:
 *
 *   1. the sweep read something: `read` at or above `ARRANGEABLE_MIN_READ` (1700). A table summed
 *      over nothing prints zeros in every column and reads exactly like a tree whose nodes are all
 *      free, which is why the lower bound is part of the verdict and not a comment.
 *   2. no node went unanswered: `no_fact` is zero. The fact answers at every node of every graph, or
 *      it does not, and "does not" is a fact-layer defect rather than a placement decision -- there
 *      is no reading of an absent answer, so the sweep refuses one.
 *   3. nothing was dropped in the reading: `silent` is zero. A case that printed an answers line but
 *      whose row this tool cannot parse would otherwise leave the table smaller without saying so.
 *
 * There is no fourth verdict about *agreement*: `core.arrangeable` is one answer to one question, and
 * this tool would have to hold a second copy of it to compare against. What the tables are for is the
 * opposite reading -- which kinds, operators and families the tree's nodes fall into, so that a change
 * to the fact moves a row a reader can name instead of a total.
 *
 *   node scripts/check-arrangeable.mjs            # sweep, print the tables, decide the three verdicts
 *   node scripts/check-arrangeable.mjs --rows 200  # print more than the first 60 rows (0: all)
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
 * Exit codes: 0 all three verdicts hold; 1 a verdict is red (the reading is complete); 2 the sweep
 * could not be taken at all (no binary, no case list) -- not a zero.
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
// The floor under the sweep's coverage, and not a coverage goal: this tree reads 2,981 of its 4,400
// cases, and the rest want the arguments or the module roots their own plans give them (the same
// reason `check-plan-refs.mjs` skips the cases it sweeps that do the same). 1,700 is well under that
// reading on purpose, so a case that stops compiling for a real reason moves the number visibly
// instead of turning the sweep red for a count it never promised.
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

const ROW_FIELDS = ['nodes', 'free', 'kept', 'no_fact']
const FAMILY_FIELDS = ['nodes', 'free', 'kept', 'no_fact']

/// One line of the pass's census into its fields, or null when a field is missing: a line the pass
/// printed short is a defect of the pass, and this tool is not the place to paper over it.
function parseFields(line, prefix, names) {
    const body = line.slice(prefix.length).trim()
    const fields = {}
    for (const word of body.split(/\s+/)) {
        const at = word.indexOf('=')
        if (at <= 0) continue
        fields[word.slice(0, at)] = word.slice(at + 1)
    }
    for (const name of names) {
        if (!/^\d+$/.test(fields[name] || '')) return null
    }
    const row = {}
    for (const name of names) row[name] = Number(fields[name])
    for (const [key, value] of Object.entries(fields)) {
        if (names.includes(key)) continue
        row[key] = value
    }
    return row
}

function run(binary, args, cwd) {
    return new Promise((resolve) => {
        const child = spawn(binary, args, { cwd, stdio: ['ignore', 'pipe', 'pipe'] })
        // Only the census lines are kept: `std::gir` writes the whole graph to stdout, and a
        // 100,000-node tree's DOT held in memory per worker is not a reading.
        const rows = []
        const families = []
        const samples = []
        let ran = false
        let stdoutFirst = ''
        let stderr = ''
        let finished = false
        const finish = (result) => {
            if (finished) return
            finished = true
            clearTimeout(timer)
            resolve({ ...result, rows, families, samples, ran, stdoutFirst, stderr })
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
                if (line.startsWith('FACTS_COMPARE answers_row ')) rows.push(line)
                else if (line.startsWith('FACTS_COMPARE answers_family ')) families.push(line)
                // The answers line is what says the census ran on this program: a program with no
                // nodes prints no row and is still a case the sweep read.
                if (line.startsWith('FACTS_COMPARE kind=core.arrangeable answers ')) ran = true
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

function addCounts(target, row, names) {
    for (const name of names) target[name] = (target[name] || 0) + row[name]
}

async function main() {
    const binary = camelBinary()
    const files = trackedCases()
    const totals = new Map()
    const familyTotals = new Map()
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
                // No answers line: the case does not compile on its own (its plan gives it arguments
                // or modules this sweep does not), or the pass said nothing about a program it was
                // handed. The first is a case this sweep cannot read; the second would be a defect,
                // and either way it is counted and not silently dropped.
                const first = firstLine(result.stderr) || firstLine(result.stdoutFirst)
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
            for (const line of result.rows) {
                const row = parseFields(line, 'FACTS_COMPARE answers_row ', [...ROW_FIELDS])
                if (row === null || !row.node_kind || !row.oper) {
                    refused.push(`${relative}: ${line}`)
                    continue
                }
                const key = `${row.node_kind}\u0000${row.oper}`
                const sum = totals.get(key) || { node_kind: row.node_kind, oper: row.oper }
                addCounts(sum, row, ROW_FIELDS)
                totals.set(key, sum)
            }
            for (const line of result.families) {
                const row = parseFields(line, 'FACTS_COMPARE answers_family ', [...FAMILY_FIELDS])
                if (row === null || !row.family) {
                    refused.push(`${relative}: ${line}`)
                    continue
                }
                const sum = familyTotals.get(row.family) || { family: row.family }
                addCounts(sum, row, FAMILY_FIELDS)
                familyTotals.set(row.family, sum)
                const kept = samplesOf.get(row.family) || []
                const example = row.kept_example
                if (example && example !== '-' && kept.length < 3) {
                    kept.push(`${relative} ${example}`)
                    samplesOf.set(row.family, kept)
                }
            }
        }
    }
    await Promise.all(Array.from({ length: Math.min(CONCURRENCY, files.length) }, worker))
    if (!process.env.CAMEL_TEST_TMPDIR) dropTree(RUN_ROOT)

    const rows = [...totals.values()].sort(
        (left, right) =>
            left.node_kind.localeCompare(right.node_kind) || left.oper.localeCompare(right.oper))
    const families = [...familyTotals.values()].sort((left, right) =>
        left.family.localeCompare(right.family))
    const total = { nodes: 0, free: 0, kept: 0, no_fact: 0 }
    for (const row of rows) addCounts(total, row, ROW_FIELDS)

    console.log(
        `ARRANGEABLE_SWEEP files=${files.length} read=${read} skipped=${unread.length} ` +
            `silent=${refused.length} nodes=${total.nodes} free=${total.free} ` +
            `kept=${total.kept} no_fact=${total.no_fact} rows=${rows.length}`)

    // The counts summed over each node kind and each family: the columns a reader compares between
    // two revisions of the fact. A kind's line moves only if that kind's answer moved, and a family's
    // line moves only if the term that decides it moved.
    const kinds = new Map()
    for (const row of rows) {
        const sum = kinds.get(row.node_kind) || {}
        addCounts(sum, row, ROW_FIELDS)
        kinds.set(row.node_kind, sum)
    }
    for (const name of [...kinds.keys()].sort()) {
        const sum = kinds.get(name)
        console.log(
            `ARRANGEABLE_KIND node_kind=${name} nodes=${sum.nodes} free=${sum.free} ` +
                `kept=${sum.kept} no_fact=${sum.no_fact}`)
    }
    for (const family of families) {
        console.log(
            `ARRANGEABLE_FAMILY family=${family.family} nodes=${family.nodes} free=${family.free} ` +
                `kept=${family.kept} no_fact=${family.no_fact}`)
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
    const named = (row) => row.node_kind === 'oper' && always.includes(row.oper)
    const shown = limit > 0
        ? rows.filter((row, index) => index < limit || named(row))
        : rows
    for (const row of shown) {
        console.log(
            `ARRANGEABLE_ROW node_kind=${row.node_kind} oper=${row.oper} nodes=${row.nodes} ` +
                `free=${row.free} kept=${row.kept} no_fact=${row.no_fact}`)
    }
    // The row above says how many; these say where. A count cannot say which program and node a kept
    // family came from, and that is what a reader needs to look at the answer itself.
    for (const family of families) {
        for (const sample of samplesOf.get(family.family) || []) {
            console.log(`ARRANGEABLE_SAMPLE ${family.family} ${sample}`)
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

    let red = false
    if (read < MIN_READ) {
        console.log(
            `ARRANGEABLE_VERDICT read=${read} under the ${MIN_READ} a sweep of this tree reads: ` +
                'the table is not a reading of the tree')
        red = true
    }
    if (total.no_fact > 0) {
        const declined = rows.filter((row) => row.no_fact > 0)
        console.log(
            `ARRANGEABLE_VERDICT no_fact=${total.no_fact} over ${declined.length} row(s): ` +
                'a node the fact answered nothing for has no reading at all ' +
                declined.slice(0, 8).map((row) => `${row.node_kind}/${row.oper}`).join(' '))
        red = true
    }
    if (refused.length > 0) {
        console.log(
            `ARRANGEABLE_VERDICT silent=${refused.length}: a case compiled and the pass printed no ` +
                'row, or a row this tool cannot parse')
        red = true
    }
    if (!red) {
        console.log(
            `every node the sweep read was answered by the fact: ${read} cases, ${total.nodes} nodes, ` +
                `${total.free} free, ${total.kept} kept, ${rows.length} (kind, operator) rows and ` +
                `${families.length} families`)
    }
    process.exit(red ? 1 : 0)
}

main().catch((error) => {
    console.error(`check-arrangeable: ${error?.stack || error}`)
    process.exit(2)
})