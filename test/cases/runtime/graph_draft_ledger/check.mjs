#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The direct-connection ledger of graph-store step 2 b, as a check: three numbers that may go down
 * and may not go up.
 *
 * `GraphDraft` is going away. Until it does, the ledger in
 * `docs/handover/reviews/graph-store-step2-b-ledger.cn.md` records how many places still connect to
 * it directly, "one row per commit", so that "deleted" is a number and not a claim. This file is the
 * same three numbers as a gate, because a ledger nobody checks is a promise nobody keeps: the
 * numbers may only fall, and the last step must reach zero.
 *
 * ## What the three numbers are
 *
 * Section 0 of that page writes the definitions down; this file counts exactly that and invents no
 * definition of its own.
 *
 *   1. `files` / `references` -- the transaction-external references to the whole word `GraphDraft`
 *      under `src/`, `include/` and `modules/`: how many files and how many occurrences.
 *   2. `constructions` -- construction points: `make_unique<...GraphDraft>`, every place that makes a
 *      new editable graph.
 *   3. `decode` -- `GraphDraft::decode` occurrences.
 *
 * ## The two counting rules, and why they are written down
 *
 *   * **The transaction's own two files do not count** -- `include/camel/runtime/graph_txn.h` and
 *     `src/runtime/graph_txn.cpp`. They name `GraphDraft` because the transaction is the wall around
 *     it, not because a resident is still wired straight to it. They are dropped by basename, the
 *     same `grep -v -e graph_txn.h -e graph_txn.cpp` the ledger uses (six occurrences in two files
 *     today, so the difference is visible but not huge; it is the pieces of the wall, not a rounding
 *     error).
 *   * **Whole words, not substrings: `-w`.** Counting substrings counted the six letters inside
 *     `RuntimeGraphDraftSession`, which inflated the base point from 712 to 885 -- and that class
 *     name grows every time somebody writes it, so the noise grew with the prose while the number of
 *     residents did not move.
 *
 * ## Lines and occurrences are different numbers
 *
 * Every count below is taken with `-o`, which prints one output line per match instead of one per
 * source line, so `wc -l` (and `lines.length` here) is the number of occurrences:
 *
 *   * `git grep -lw GraphDraft` prints ONE LINE PER FILE (`-l`), so counting lines counts files.
 *   * `git grep -ow GraphDraft` prints ONE LINE PER MATCH (`-o`), so counting lines counts
 *     occurrences. Without `-o` a line that writes `GraphDraft` twice would count as one, and the
 *     number would depend on how the source happened to be wrapped that day -- which is how two
 *     people get 561 and 562 out of the same tree.
 *
 * Never swap the two flags: `-l` counts files, `-o` counts occurrences. Both are gated, because a
 * commit that adds a ninth reference to an already-counted file leaves the file count alone.
 *
 * ## `constructions` counts 3, where the ledger's older rows say 1
 *
 * Section 0's pattern is `make_unique<[^>]*GraphDraft>`, and it is the pattern used here. The rows
 * b3a..b3c-1 in the table say 1 because they counted the unqualified spelling `make_unique<GraphDraft>`
 * only. The qualified `std::make_unique<rt::GraphDraft>` in `modules/tiles/src/region_extract.cpp`
 * and `src/passes/test/resources.cpp` makes an editable graph directly all the same -- that is
 * exactly what this number is for -- so the wider pattern the section writes is the number gated
 * here. The two are recorded, not silently reconciled: 3 is the count on the row this check was
 * added at.
 *
 * ## The gate is `current <= baseline`, not `current == baseline`
 *
 * The whole point of this step is that the numbers go down. A decrease is green, and prints a
 * reminder to re-record the baseline so the gain cannot be spent later by an equal-looking rise; an
 * increase is red, and prints which number rose from what to what, and where the new site is, as
 * `file#line#symbol`. `file#symbol` (not a line number) is what the per-site record stores: line
 * numbers move with every edit above them, and a check that renames its own evidence on a one-line
 * insertion is a check people learn to ignore.
 *
 * ## What this reads, and what it does not
 *
 * `git grep` reads the tracked tree, so a new file counts once it is `git add`ed -- the check
 * measures what the commit would ship. It does not read `test/`, `docs/` or `stdlib/`, so this file
 * and the ledger page can name `GraphDraft` as often as they like.
 *
 * Record a new row after a commit that lowered a number:
 *
 *     node test/cases/runtime/graph_draft_ledger/check.mjs --record
 *
 * `--record` refuses to write a number that rose; `--force` overrides that, loudly, and the diff of
 * `baseline.json` then shows the rise for a reviewer to refuse. A check whose baseline can be
 * refreshed without friction is not a check.
 */
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
// test/cases/runtime/graph_draft_ledger -> the checkout root.
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const BASELINE_PATH = path.join(HERE, 'baseline.json')

// The roots the ledger counts. `test/`, `docs/` and `stdlib/` are deliberately absent: the ledger
// page and this checker talk about `GraphDraft` by name all day, and Camel sources never do.
const SOURCE_ROOTS = ['src', 'include', 'modules']

// The transaction's own two files: the wall, not a resident (see the header).
const TRANSACTION_FILES = new Set(['graph_txn.h', 'graph_txn.cpp'])

// The three numbers, in the order they are printed. `-l` for the file count, `-o` for every count of
// occurrences; `-n` rides along so a red can name the line without a second `git grep`.
const NUMBERS = [
    { key: 'files', label: 'files', flags: ['-l', '-w'], pattern: 'GraphDraft', countsFiles: true },
    { key: 'references', label: 'references', flags: ['-o', '-n', '-w'], pattern: 'GraphDraft' },
    {
        key: 'constructions',
        label: 'constructions',
        flags: ['-o', '-n'],
        pattern: 'make_unique<[^>]*GraphDraft>',
    },
    { key: 'decode', label: 'decode', flags: ['-o', '-n'], pattern: 'GraphDraft::decode' },
]

const RECORD = process.argv.includes('--record')
const FORCE = process.argv.includes('--force')

function fail(message) {
    console.log(`[LEDGER] cannot count: ${message}`)
    process.exit(2)
}

/**
 * `git grep`, from the checkout root. Exit 1 is "no match" (not an error); anything else is an
 * error, because a check that cannot read the tree must not answer "nothing rose".
 */
function gitGrep(flags, pattern) {
    const args = ['grep', ...flags, pattern, '--', ...SOURCE_ROOTS]
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
// it is only ever used to point a reader at a place: the verdict never depends on it (`#-` is the
// key when nothing is found).
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

/**
 * Reads the three numbers. Returns `value` per number, plus `sites`: for the file count, one entry
 * per file; for the others, one entry per `file#symbol`, with the locations that made it. `sites` is
 * diagnostics only -- it says WHERE when a number rises, and never decides WHETHER the check is red.
 */
function measure() {
    const values = {}
    const sites = {}
    const locations = {}
    for (const number of NUMBERS) {
        const lines = gitGrep(number.flags, number.pattern)
        if (number.countsFiles) {
            const files = lines.filter((entry) => entry !== '' && !isTransactionFile(entry))
            values[number.key] = files.length
            sites[number.key] = Object.fromEntries(files.map((file) => [file, 1]))
            locations[number.key] = Object.fromEntries(files.map((file) => [file, []]))
            continue
        }
        const entries = lines.map(parseLocation).filter((entry) => !isTransactionFile(entry.path))
        const counts = {}
        const where = {}
        for (const entry of entries) {
            const site = `${entry.path}#${symbolAt(entry.path, entry.line) || '-'}`
            counts[site] = (counts[site] || 0) + 1
            const carried = where[site] || []
            carried.push({ ...entry, site })
            where[site] = carried
        }
        values[number.key] = entries.length
        sites[number.key] = counts
        locations[number.key] = where
    }
    return { values, sites, locations }
}

function readBaseline() {
    if (!fs.existsSync(BASELINE_PATH)) return null
    try {
        return JSON.parse(fs.readFileSync(BASELINE_PATH, 'utf8'))
    } catch (error) {
        fail(`baseline.json is not readable JSON (${error.message})`)
    }
}

function sorted(object) {
    return Object.fromEntries(Object.entries(object).sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0)))
}

function summary(values) {
    return NUMBERS.map((number) => `${number.label} ${values[number.key]}`).join(' ')
}

function comparisons(values, baseline) {
    const base = baseline.numbers
    return NUMBERS.map((number) => `${number.label} ${values[number.key]} <= ${base[number.key]}`).join(
        ' '
    )
}

/** Every `file#symbol` whose count rose, with the locations that carry it now. */
function rises(values, sites, locations, baseline) {
    const found = []
    for (const number of NUMBERS) {
        const current = sites[number.key] || {}
        const previous = (baseline.sites || {})[number.key] || {}
        const keys = new Set([...Object.keys(current), ...Object.keys(previous)])
        for (const key of [...keys].sort()) {
            const now = current[key] || 0
            const before = previous[key] || 0
            if (now > before) {
                found.push({
                    number,
                    key,
                    before,
                    now,
                    where: (locations[number.key] || {})[key] || [],
                })
            }
        }
    }
    return found
}

function printRises(list, baseline) {
    for (const rise of list) {
        const delta = `+${rise.now - rise.before}`
        console.log(
            `[LEDGER] RED ${rise.number.label} ${rise.key} rose from ${rise.before} to ${rise.now} (${delta})`
        )
        for (const entry of rise.where) {
            const symbol = entry.site.slice(entry.site.indexOf('#') + 1)
            // `git grep -o` prints the match alone, so the line itself is read from the file: a
            // reader looking at `GraphDraft` learns nothing that the number above did not say.
            const carried = sourceLines(entry.path)
            const text = (carried ? carried[entry.line - 1] : entry.text).trim().slice(0, 160)
            console.log(`[LEDGER] RED   ${entry.path}:${entry.line}#${symbol} ${text}`)
        }
    }
    console.log(`[LEDGER] RED the baseline is ${path.relative(REPO_ROOT, BASELINE_PATH)}`)
}

function numberRises(values, baseline) {
    const base = baseline.numbers
    return NUMBERS.filter((number) => values[number.key] > base[number.key]).map((number) => ({
        number,
        from: base[number.key],
        to: values[number.key],
    }))
}

const measured = measure()
const baseline = readBaseline()

if (RECORD) {
    if (!baseline && !FORCE) {
        console.log(
            `[LEDGER] no baseline at ${path.relative(REPO_ROOT, BASELINE_PATH)}; recording the first one: ${summary(
                measured.values
            )}`
        )
    }
    const rose = baseline ? numberRises(measured.values, baseline) : []
    if (rose.length > 0 && !FORCE) {
        for (const entry of rose) {
            console.log(
                `[LEDGER] RED ${entry.number.label} would rise from ${entry.from} to ${entry.to}`
            )
        }
        printRises(rises(measured.values, measured.sites, measured.locations, baseline), baseline)
        console.log('[LEDGER] refusing to record an increase; --force records it anyway, with the diff')
        process.exit(2)
    }
    if (rose.length > 0) {
        console.log('[LEDGER] --force: recording an increase; the diff of baseline.json says so')
        printRises(rises(measured.values, measured.sites, measured.locations, baseline), baseline)
    }
    const document = {
        comment:
            'The baseline of the direct-connection ledger (graph-store step 2 b): the numbers below ' +
            'may only fall, and the last step is all of them at zero. `sites` records where each ' +
            'number comes from, as `file#symbol` (a line number moves with every edit above it, a ' +
            'symbol does not); it is diagnostics for a red and never decides the verdict. Record a ' +
            'new row after a commit with check.mjs --record.',
        numbers: Object.fromEntries(NUMBERS.map((number) => [number.key, measured.values[number.key]])),
        sites: Object.fromEntries(NUMBERS.map((number) => [number.key, sorted(measured.sites[number.key])])),
    }
    // LF and a trailing newline: this file is read by the check on every platform, and a snapshot of
    // counts should diff as one line per changed number.
    fs.writeFileSync(BASELINE_PATH, `${JSON.stringify(document, null, 2)}\n`, 'utf8')
    console.log(
        `[LEDGER] recorded ${summary(measured.values)} in ${path.relative(REPO_ROOT, BASELINE_PATH)}`
    )
    process.exit(0)
}

if (!baseline) {
    fail(`no baseline at ${path.relative(REPO_ROOT, BASELINE_PATH)}; record one with --record first`)
}

console.log(`[LEDGER] ${comparisons(measured.values, baseline)}`)

const rose = numberRises(measured.values, baseline)
const dropped = NUMBERS.filter((number) => measured.values[number.key] < baseline.numbers[number.key])

if (rose.length > 0) {
    for (const entry of rose) {
        console.log(
            `[LEDGER] RED ${entry.number.label} rose from ${entry.from} to ${entry.to} (+${
                entry.to - entry.from
            })`
        )
    }
    const sites = rises(measured.values, measured.sites, measured.locations, baseline)
    if (sites.length > 0) printRises(sites, baseline)
    else
        console.log(
            '[LEDGER] RED no new file#symbol site: the baseline has no per-site record for these numbers ' +
                '(re-record it), so only the total is named here'
        )
    console.log(
        '[LEDGER] RED these numbers may only fall: the step ends when all three are zero and `GraphDraft` is deleted'
    )
    process.exit(1)
}

for (const number of dropped) {
    console.log(
        `[LEDGER] lower ${number.label} ${baseline.numbers[number.key]} -> ${
            measured.values[number.key]
        }; re-record with check.mjs --record so the gain cannot be spent later`
    )
}

console.log('[LEDGER] ok')
process.exit(0)