#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The direct-connection ledger of graph-store step 2 b, as a check: three lists of the places still
 * wired to `GraphDraft`, and a gate that names whoever enters or leaves one of them.
 *
 * `GraphDraft` is going away. Until it does, the ledger in
 * `docs/handover/reviews/graph-store-step2-b-ledger.cn.md` records which places still connect to it
 * directly, "one row per commit", so that "deleted" is a name and not a claim. This file is the same
 * three lists as a gate, because a ledger nobody checks is a promise nobody keeps.
 *
 * ## Why these are lists and not numbers
 *
 * Author's ruling of 2026-11-03 13:30 (`docs/handover/reviews/questions-2026-10-29.md`): a count
 * that can drift while the behaviour stays the same protects nothing.
 *
 *   * The `references` count is **gone**. It counted how many times the whole word `GraphDraft`
 *     appears under `src/`, `include/` and `modules/`, comments included: turning a member into a
 *     free function wrote the type twice where the member wrote it once (+2), and one sentence
 *     explaining the type added one (+1). Neither is a connection, so the number described nothing.
 *   * `files`, `constructions` and `decode` are kept, and each is a **list of members**:
 *       - `files` -- the files that name the whole word at all. Membership is the question ("is this
 *         file still wired to the draft?"), not how many times a file says it.
 *       - `constructions` -- `make_unique<...GraphDraft>`: every place that makes a new editable
 *         graph. One member is one ownership action, so a new member is a new way to own a draft.
 *       - `decode` -- `GraphDraft::decode`: every place that turns a frozen graph into an editable
 *         copy. One member is one reading boundary.
 *
 * A member is a path (`files`) or a `file#symbol` (`constructions`, `decode`). Never a line number: a
 * line moves with every edit above it, and a check that renames its own evidence on a one-line
 * insertion is a check people learn to ignore.
 *
 * `files` is a list and not a count even though a machine can write the type into a comment and move
 * it: the ruling keeps this list, and the report below prints the line a new member stands on, so a
 * reader sees whether a file entered because of code or because of prose. That reading is the point
 * of naming the member; a total could not show it at all.
 *
 * ## What the gate says
 *
 * Every list is compared both ways, and both ways are red:
 *
 *   * a member the tree has and the list does not -- **entered** -- is a new direct connection, named
 *     with the place it appears and that line's own text;
 *   * a member the list has and the tree does not -- **left** -- is a connection somebody removed,
 *     and it stays red until the same commit takes the member off the list: that deletion IS the
 *     record of the gain, and an unrecorded gain is spent again later by an equal-looking entry.
 *
 * `--record` writes the lists the tree has now. It refuses to add a member -- an addition needs
 * `--force`, loudly, and the diff of `baseline.json` then shows the new connection for a reviewer to
 * refuse -- and records a removal without friction, because removing one is the whole purpose of the
 * step. A check whose baseline can be refreshed without friction is not a check; a check that makes
 * every deletion an argument is one people stop running.
 *
 * ## The files that must name the type nowhere
 *
 * `ZERO_FILES` is the endpoint, and it was already a list: a handle names a transaction, a reader
 * reaches the draft through it, and the boundary does not name the type at all. It is checked apart
 * from the three lists, because the endpoint is a fact about those files and not a zero on a total.
 *
 * ## What this reads, and what it does not
 *
 * `git grep` reads the tracked tree, so a new file counts once it is `git add`ed -- the check
 * measures what the commit would ship. It does not read `test/`, `docs/` or `stdlib/`, so this file
 * and the ledger page can name `GraphDraft` as often as they like.
 *
 * Record the lists after a commit that removed a connection:
 *
 *     node test/cases/runtime/graph_draft_ledger/check.mjs --record
 */
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
// test/cases/runtime/graph_draft_ledger -> the checkout root.
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const BASELINE_PATH = path.join(HERE, 'baseline.json')

// The roots the ledger reads. `test/`, `docs/` and `stdlib/` are deliberately absent: the ledger
// page and this checker talk about `GraphDraft` by name all day, and Camel sources never do.
const SOURCE_ROOTS = ['src', 'include', 'modules']

// The transaction's own two files: the wall, not a resident (see the header).
const TRANSACTION_FILES = new Set(['graph_txn.h', 'graph_txn.cpp'])

// `-n` for every ledger: one output line per matching source line, carrying `path:line:text`. The
// member is derived from the path (and the symbol), so no ledger counts anything any more -- `-l`
// (one line per file) and `-o` (one line per match) were the two ways to turn a listing into a
// number, and both are gone with the numbers.
const LEDGERS = [
    {
        key: 'files',
        label: 'files',
        flags: ['-n', '-w'],
        pattern: 'GraphDraft',
        bySymbol: false,
    },
    {
        key: 'constructions',
        label: 'constructions',
        flags: ['-n'],
        pattern: 'make_unique<[^>]*GraphDraft>',
        bySymbol: true,
    },
    { key: 'decode', label: 'decode', flags: ['-n'], pattern: 'GraphDraft::decode', bySymbol: true },
]

// The files that must name the type nowhere: every place a handle is made and a graph is read.
// A non-zero count here is the end of the step coming undone, whatever the lists say.
const ZERO_FILES = [
    'modules/graph/handle.h',
    'modules/graph/handle.cpp',
    'modules/graph/read.cpp',
    'modules/rewrite/fact.cpp',
    'modules/rewrite/module.cpp',
]

const RECORD = process.argv.includes('--record')
const FORCE = process.argv.includes('--force')

function fail(message) {
    console.log(`[LEDGER] cannot read the lists: ${message}`)
    process.exit(2)
}

/**
 * `git grep`, from the checkout root. Exit 1 is "no match" (not an error); anything else is an
 * error, because a check that cannot read the tree must not answer "nothing entered".
 */
function gitGrep(flags, pattern, paths = SOURCE_ROOTS) {
    const args = ['grep', ...flags, pattern, '--', ...paths]
    const result = spawnSync('git', args, {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        maxBuffer: 256 * 1024 * 1024,
    })
    if (result.error) fail(`git did not run (${result.error.message}); this check needs the checkout`)
    if (result.status === 1) return []
    if (result.status !== 0) {
        const detail = (result.stderr || '').trim()
        fail(`git ${args.join(' ')} exited ${result.status}${detail ? `: ${detail}` : ''}`)
    }
    return result.stdout.split('\n').filter((line) => line !== '')
}

function isTransactionFile(relative) {
    return TRANSACTION_FILES.has(path.posix.basename(relative))
}

/** `path:line:text`, the shape `git grep -n` prints. */
function parseLocation(line) {
    const first = line.indexOf(':')
    const second = line.indexOf(':', first + 1)
    return {
        path: line.slice(0, first),
        line: Number(line.slice(first + 1, second)),
        text: line.slice(second + 1),
    }
}

const sourceCache = new Map()

function sourceLines(relative) {
    if (!sourceCache.has(relative)) {
        let lines = null
        try {
            lines = fs.readFileSync(path.join(REPO_ROOT, ...relative.split('/')), 'utf8').split('\n')
        } catch {
            lines = null
        }
        sourceCache.set(relative, lines)
    }
    return sourceCache.get(relative)
}

// A definition or declaration opens at column 0 in this tree; members and statements are indented.
// The nearest such line above a match names the function the match lives in. It is a heuristic, and
// it is only ever used to point a reader at a place: the member key falls back to `file#-` when
// nothing is found, and the verdict never depends on which symbol was picked.
const DEFINITION = /^[A-Za-z_~][\w:<>,\s*&]*?\b([A-Za-z_]\w*)\s*\(/
const NOT_A_DEFINITION = /^(?:if|for|while|switch|return|else|do|case|namespace|using)\b/

function symbolAt(relative, lineNumber) {
    const lines = sourceLines(relative)
    if (!lines) return null
    for (let index = Math.min(lineNumber, lines.length) - 1; index >= 0; index--) {
        const text = lines[index]
        if (!text || /^\s/.test(text)) continue
        if (text.trimEnd().endsWith(';')) continue
        if (NOT_A_DEFINITION.test(text)) continue
        const match = DEFINITION.exec(text)
        if (match) return match[1]
    }
    return null
}

/** The line a member stands on, cut short: a reader of a red needs the text, not the whole line. */
function lineText(entry) {
    const carried = sourceLines(entry.path)
    const text = carried ? carried[entry.line - 1] : entry.text
    return (text ?? '').trim().slice(0, 160)
}

/**
 * Reads the three lists. Each is a sorted array of members, and `where` keeps the first place each
 * member appears, so a red can name it without a second `git grep`.
 */
function measure() {
    const lists = {}
    const where = {}
    for (const ledger of LEDGERS) {
        const entries = gitGrep(ledger.flags, ledger.pattern)
            .map(parseLocation)
            .filter((entry) => !isTransactionFile(entry.path))
        const first = new Map()
        for (const entry of entries) {
            const member = ledger.bySymbol
                ? `${entry.path}#${symbolAt(entry.path, entry.line) || '-'}`
                : entry.path
            if (!first.has(member)) first.set(member, entry)
        }
        lists[ledger.key] = [...first.keys()].sort(byMember)
        where[ledger.key] = Object.fromEntries(first)
    }
    return { lists, where }
}

/** Path order, then symbol order: `path.posix` comparison, so three platforms agree. */
function byMember(a, b) {
    return a < b ? -1 : a > b ? 1 : 0
}

// True when the file on disk was written by the counted form of this check (the one with
// `numbers.references` in it). It is not read as a list -- a number is not a member, and reading it
// as one would report every member of the tree as having entered -- so `--record` replaces it and the
// gate refuses until somebody does.
let countedForm = false

function readBaseline() {
    if (!fs.existsSync(BASELINE_PATH)) return null
    let baseline
    try {
        baseline = JSON.parse(fs.readFileSync(BASELINE_PATH, 'utf8'))
    } catch (error) {
        fail(`baseline.json is not readable JSON (${error.message})`)
    }
    if (!baseline.lists || typeof baseline.lists !== 'object') {
        countedForm = true
        return null
    }
    for (const ledger of LEDGERS) {
        if (!Array.isArray(baseline.lists[ledger.key])) {
            fail(`${path.relative(REPO_ROOT, BASELINE_PATH)} has no \`lists.${ledger.key}\` array`)
        }
    }
    return baseline
}

/** The members one side has and the other does not, sorted, with where each stands. */
function difference(measured, baseline) {
    const entered = []
    const left = []
    for (const ledger of LEDGERS) {
        const now = new Set(measured.lists[ledger.key])
        const recorded = new Set(baseline.lists[ledger.key])
        for (const member of measured.lists[ledger.key]) {
            if (!recorded.has(member)) entered.push({ ledger, member, where: measured.where[ledger.key][member] })
        }
        for (const member of [...recorded].sort(byMember)) {
            if (!now.has(member)) left.push({ ledger, member })
        }
    }
    return { entered, left }
}

/**
 * The line that says whether the files the step is about are clean, and the red when one is not.
 * The lists can lose members while one of these gains a mention, which is why the endpoint is
 * written as a fact about these files rather than as an empty total.
 */
function zeroFilesLine() {
    const dirty = []
    for (const file of ZERO_FILES) {
        const lines = gitGrep(['-n', '-w'], 'GraphDraft', [file]).filter((entry) => entry !== '')
        if (lines.length > 0) dirty.push(`${file}: ${lines.length}`)
    }
    if (dirty.length === 0) {
        return `[LEDGER] zero files ok: ${ZERO_FILES.join(', ')} name the type nowhere`
    }
    for (const entry of dirty) console.log(`[LEDGER] RED ${entry} name the type, and must not`)
    console.log(
        '[LEDGER] RED these files are where the step ends: a handle names a transaction, a reader ' +
            'reaches the draft through it, and the boundary does not name the type at all',
    )
    process.exit(1)
}

function printDifference({ entered, left }) {
    for (const one of entered) {
        console.log(
            `[LEDGER] RED entered ${one.ledger.label}: ${one.member}  ` +
                `(first at ${one.where.path}:${one.where.line}: ${lineText(one.where)})`,
        )
    }
    for (const one of left) {
        console.log(`[LEDGER] RED left ${one.ledger.label}: ${one.member}`)
    }
    if (entered.length > 0) {
        console.log(
            '[LEDGER] RED an entered member is a new direct connection to the type this step removes: ' +
                'take it out, or record it with --record --force in a commit that says why it stays',
        )
    }
    if (left.length > 0) {
        console.log(
            `[LEDGER] RED a left member is a connection somebody removed: take it off the list in this ` +
                `commit (--record), because that deletion is the record of the gain`,
        )
    }
    console.log(
        `[LEDGER] RED the list is ${path.relative(REPO_ROOT, BASELINE_PATH)}: ` +
            `${entered.length} entered, ${left.length} left`,
    )
}

const measured = measure()
const baseline = readBaseline()

if (RECORD) {
    const now = Object.fromEntries(LEDGERS.map((ledger) => [ledger.key, measured.lists[ledger.key]]))
    if (!baseline) {
        console.log(
            countedForm
                ? `[LEDGER] the baseline on disk is the counted form of this check (numbers, among them ` +
                      `the deleted \`references\`); replacing it with the three lists: ${summary(measured.lists)}`
                : `[LEDGER] no baseline at ${path.relative(REPO_ROOT, BASELINE_PATH)}; recording the first one: ` +
                      summary(measured.lists),
        )
    } else {
        const { entered, left } = difference(measured, baseline)
        if (entered.length > 0 && !FORCE) {
            printDifference({ entered, left })
            console.log(
                '[LEDGER] refusing to record a member that entered the list; --force records it anyway, ' +
                    'with the diff, so a reviewer can refuse it',
            )
            process.exit(2)
        }
        if (entered.length > 0 || left.length > 0) {
            if (entered.length > 0) {
                console.log('[LEDGER] --force: recording members that entered; the diff of baseline.json says so')
            }
            printDifference({ entered, left })
        }
    }
    const document = {
        comment:
            'The baseline of the direct-connection ledger (graph-store step 2 b), as the three lists the ' +
            'check compares member by member: `files` (paths that name the whole word `GraphDraft`), ' +
            '`constructions` (`file#symbol` of every `make_unique<...GraphDraft>`) and `decode` ' +
            '(`file#symbol` of every `GraphDraft::decode`). A member that enters the tree is a new direct ' +
            'connection and fails the check; a member that leaves it fails until the same commit takes it ' +
            'off this list. There is no count of references here: the author deleted it (ruling of ' +
            '2026-11-03 13:30), because a line of prose could move it while nothing was wired to the draft. ' +
            'Record the lists with check.mjs --record.',
        lists: now,
    }
    // LF and a trailing newline: this file is read by the check on every platform, and a change to it
    // should diff as one line per member that entered or left.
    fs.writeFileSync(BASELINE_PATH, `${JSON.stringify(document, null, 2)}\n`, 'utf8')
    console.log(
        `[LEDGER] recorded ${summary(measured.lists)} in ${path.relative(REPO_ROOT, BASELINE_PATH)}`,
    )
    process.exit(0)
}

if (!baseline) {
    fail(
        countedForm
            ? `${path.relative(REPO_ROOT, BASELINE_PATH)} is the counted form of this check (numbers, ` +
                  'among them the deleted `references`): re-record it with --record, which writes the three lists'
            : `no baseline at ${path.relative(REPO_ROOT, BASELINE_PATH)}; record one with --record first`,
    )
}

function summary(lists) {
    return LEDGERS.map((ledger) => `${ledger.label} ${lists[ledger.key].length} member(s)`).join(', ')
}

console.log(`[LEDGER] ${summary(measured.lists)}, compared with the list member by member`)

console.log(zeroFilesLine())

const { entered, left } = difference(measured, baseline)

if (entered.length > 0 || left.length > 0) {
    printDifference({ entered, left })
    process.exit(1)
}

console.log('[LEDGER] ok: every member is on the list, and every member of the list is in the tree')
process.exit(0)