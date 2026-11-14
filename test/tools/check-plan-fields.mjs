#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A plan field the runner never reads is not a test.
 *
 * `test/plans/gpu/tiles.plan.toml` carried the shape this guard exists for: an entry with
 * `kind = "negative"` and, beside it, a `compare_to` and a `compare_fields` naming a twin entry
 * that had to print the same thing. The text said "this run must equal that one"; the runner
 * compares on `compare_to` only under `kind = "differential"` (run-tests.mjs, the
 * `if (test.kind === 'differential')` block), so the entry checked its exit code and *never*
 * compared a byte -- for seven months, green, with a report that said so.
 *
 * The plan schema is closed (`TEST_FIELDS` in run-tests.mjs): a key that is not a field is an
 * error. That stops a misspelled assertion. It cannot stop a correctly spelled field under a kind
 * that does not read it, and that is the worse half: the entry reads as more than it does, and
 * nothing -- not the gate, not the report, not a review that has not opened the runner -- says so.
 *
 * ## The truth table, as the runner reads it
 *
 * `validateTest` runs one unconditional list of assertions for every kind, and three blocks that
 * are gated on the kind. So "which fields does kind K evaluate" is: the unconditional list for
 * all K, plus K's own block. The rows are the fields a plan can write; the gates are:
 *
 *   field                                   behavior  negative  positive  snapshot  differential  benchmark
 *   expect_exit                                yes       yes      yes       yes        yes          yes
 *   expect_signal                              yes       yes      yes       yes        yes          yes
 *   expect_stdout_contains                     yes       yes      yes       yes        yes          yes
 *   expect_stdout_matches                      yes       yes      yes       yes        yes          yes
 *   expect_stdout_exact / expect_stdout        yes       yes      yes       yes        yes          yes
 *   expect_stdout_starts_with                  yes       yes      yes       yes        yes          yes
 *   expect_stdout_not_contains                 yes       yes      yes       yes        yes          yes
 *   expect_stderr_contains                     yes       yes      yes       yes        yes          yes
 *   expect_output_contains                     yes       yes      yes       yes        yes          yes
 *   expect_output_not_contains                 yes       yes      yes       yes        yes          yes
 *   expect_refusal                             yes       yes      yes       yes        yes          yes
 *   expect_diagnostic_name / _code             yes       yes      yes       yes        yes          yes
 *   expect_diagnostic_line / _column           yes       yes      yes       yes        yes          yes
 *   verify_script / verify_js / _path          yes       yes      yes       yes        yes          yes
 *   normalizers                                yes       yes      yes       yes        yes          yes
 *   golden                                     no        no       no        yes        no           no
 *   snapshot_stream                            no        no       no        yes        no           no
 *   snapshot_compare                           no        no       no        yes        no           no
 *   compare_to                                 no        no       no        no         yes          no
 *   compare_fields                             no        no       no        no         yes          no
 *   perf_budget_mean_ms                        no        no       no        no         no           yes
 *   perf_budget_tolerance_pct                  no        no       no        no         no           yes
 *   backends_agree                             conditional: read only for an entry whose `backends`
 *                                              is a list (each back end after the first must equal the
 *                                              first); with no list there is nothing to compare against
 *   perf_budget_platform                       never an assertion: provenance, copied to meta.json
 *   desc / tier / args / envs / case ...       not assertions (they decide what runs, not what holds)
 *
 * Two consequences a reader should keep: `negative` and `positive` are *labels* -- the runner
 * reads exactly what it reads for `behavior`, so a `negative` entry with no `expect_exit` (or
 * `expect_signal`) asserts nothing about failing; and `normalizers` is not a snapshot field, it
 * is applied to every result before any assertion reads it (which is also why it appears inside
 * the snapshot block: the golden file is normalized with the same list).
 *
 * ## What this guard is, and what it is not
 *
 * It is a reading of the plan tree against that table: every `[[tests]]` entry of every
 * `test/plans/**\/*.plan.toml`, with `[defaults]` merged the way `loadPlanTests` merges it
 * (`{ ...doc.defaults, ...test }`), so an inherited field is reported on the entry that carries
 * it and named as inherited. It reports:
 *
 *   unread-field         a field is written and the entry's kind never evaluates it
 *   half-pair            a `differential` entry without `compare_to`, or without a non-empty
 *                        `compare_fields`: it compares nothing (`for (const field of [] )`), or it
 *                        cannot find its target
 *   label-without-check  `kind = "negative"` with neither `expect_exit` nor `expect_signal`
 *   kind-unread          `kind` is missing, or is a value no block in the runner reads
 *
 * It is **report only** by default: it prints its findings and the line
 * `plan-fields fields=<n> findings=<n> mode=report`, and exits zero, because the finding list is
 * a work item and not yet empty. `--enforce` is the mode it becomes when the list is empty; that
 * is the switch a gate turns on, and it is deliberately absent from every invocation until then.
 *
 * `--verify-runner` is the other half, and the reason this table can be trusted: it reads
 * `test/tools/run-tests.mjs` itself and refuses a table that no longer matches the runner's three
 * kind-gated blocks. A hard-coded table that outlives the code it describes is the same defect one
 * level up -- a guard that asserts nothing -- so the table is checked against the source it came
 * from, and adding a fourth gated kind or moving a field out of a block fails here by name.
 *
 *   node test/tools/check-plan-fields.mjs                 # the tree, report only
 *   node test/tools/check-plan-fields.mjs --enforce       # the tree, findings are a failure
 *   node test/tools/check-plan-fields.mjs --verify-runner # the table against run-tests.mjs
 *   node test/tools/check-plan-fields.mjs --root <checkout>
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'
import { execFileSync } from 'node:child_process'

import { parseTomlFile } from './toml-lite.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
// What the last accepted reading was: `test/<subject>-baseline.json`, the shape `test/perf-baseline.json`
// already has. Read by the ratchet below, written only by `--update-baseline`.
const BASELINE_PATH = path.resolve(HERE, '..', '..', 'test', 'plan-fields-baseline.json')

// Every field `validateTest` evaluates whatever the kind is. `expect_stdout` is the legacy name of
// `expect_stdout_exact` and the two are read in one loop; `verify_js` is the legacy name of
// `verify_script`. All three are listed, because a plan may write any of them.
const ALWAYS_EVALUATED = [
    'expect_exit',
    'expect_signal',
    'expect_stdout_contains',
    'expect_stdout_matches',
    'expect_stdout_exact',
    'expect_stdout',
    'expect_stdout_starts_with',
    'expect_stdout_not_contains',
    'expect_stderr_contains',
    'expect_output_contains',
    'expect_output_not_contains',
    'expect_refusal',
    'expect_diagnostic_name',
    'expect_diagnostic_code',
    'expect_diagnostic_line',
    'expect_diagnostic_column',
    'verify_script',
    'verify_js',
    'verify_script_path',
    'normalizers',
]

// The three blocks `validateTest` gates on the kind, and the fields each one reads. Written as
// the runner reads them, not as the documentation lists them: `compareField` also accepts
// `diagnostics`, which docs/test-plans.md does not name.
const GATED = {
    snapshot: ['golden', 'snapshot_stream', 'snapshot_compare'],
    differential: ['compare_to', 'compare_fields'],
    benchmark: ['perf_budget_mean_ms', 'perf_budget_tolerance_pct'],
}

// The kinds the runner distinguishes. `negative` and `positive` are documented labels for
// `behavior` (docs/test-plans.md, "Common Test Fields"): the runner has no block for either, so
// they evaluate exactly what `behavior` evaluates. They are accepted here for that reason and
// reported only where the label claims something no field checks.
const LABEL_KINDS = ['behavior', 'negative', 'positive']
const BLOCK_KINDS = ['snapshot', 'differential', 'benchmark']

// Fields that belong to no assertion. `snapshot_stream` on a benchmark entry is not a missing
// check of the output; `perf_budget_platform` is provenance by its own documentation.
const NOT_AN_ASSERTION = new Set(['perf_budget_platform'])

// The empty value of a string field that the runner tests for truthiness rather than for
// presence, so writing it is writing nothing: `if (test.expect_diagnostic_name || ...)` skips the
// whole diagnostic block, and `verificationConfigOf` reads a script whose `trim()` is empty as no
// script at all. A string that *is* an assertion -- `expect_stdout_exact = ""` says the program
// prints nothing -- is deliberately not in this list.
const EMPTY_STRING_IS_NOOP = new Set([
    'expect_diagnostic_name',
    'expect_diagnostic_code',
    'verify_script',
    'verify_js',
    'verify_script_path',
])

function* planFiles(dir) {
    let entries
    try {
        entries = fs.readdirSync(dir, { withFileTypes: true })
    } catch {
        return
    }
    for (const entry of entries.sort((a, b) => a.name.localeCompare(b.name))) {
        if (entry.name.startsWith('.') || entry.name === 'node_modules') {
            continue
        }
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) {
            yield* planFiles(full)
        } else if (entry.name.endsWith('.plan.toml')) {
            yield full
        }
    }
}

// `name = "<the entry>"` is where a reader starts, so that is the line a finding names. The same
// name may appear twice in a plan (a back-end-expanded entry and its reference do not, but a
// copy-paste can); the first one wins and the finding still names the entry unambiguously.
function lineOf(file, name, cache) {
    if (typeof name !== 'string' || name.length === 0) {
        return 0
    }
    let text = cache.get(file)
    if (text === undefined) {
        text = fs.readFileSync(file, 'utf8').split('\n')
        cache.set(file, text)
    }
    const needle = `name = "${name}"`
    for (let index = 0; index < text.length; index++) {
        if (text[index].trim() === needle) {
            return index + 1
        }
    }
    return 0
}

/** Everything the entry's kind evaluates, as `validateTest` evaluates it. */
function evaluatedFields(kind, test) {
    const fields = new Set(ALWAYS_EVALUATED)
    for (const field of GATED[kind] || []) {
        fields.add(field)
    }
    // Read for an entry that `backends` expanded; with no list, `__backendReference` is never set
    // and `backends_agree` is never read.
    if (Array.isArray(test.backends)) {
        fields.add('backends_agree')
    }
    return fields
}

function scanTree(rootDir) {
    const findings = []
    const lines = new Map()
    let plansRead = 0
    let entriesRead = 0
    const fieldsRead = new Set(ALWAYS_EVALUATED)
    for (const list of Object.values(GATED)) {
        for (const field of list) {
            fieldsRead.add(field)
        }
    }
    fieldsRead.add('backends_agree')
    for (const file of planFiles(path.join(rootDir, 'test', 'plans'))) {
        let doc
        try {
            doc = parseTomlFile(file)
        } catch {
            // A plan file the parser refuses is the runner's `<invalid plan>` test, not this
            // reading's: it names the file and the key, which this guard cannot improve on.
            continue
        }
        ++plansRead
        const defaults = doc.defaults !== null && typeof doc.defaults === 'object' ? doc.defaults : {}
        const relative = path.relative(rootDir, file).split(path.sep).join('/')
        for (const test of Array.isArray(doc.tests) ? doc.tests : []) {
            if (test === null || typeof test !== 'object' || Array.isArray(test)) {
                continue
            }
            // The merge `loadPlanTests` makes, and the reason an inherited field is reported at all.
            const entry = { ...defaults, ...test }
            ++entriesRead
            const kind = entry.kind
            const evaluated = evaluatedFields(kind, entry)
            const line = lineOf(file, entry.name, lines)
            const add = (category, field, note) =>
                findings.push({
                    category,
                    file: relative,
                    line,
                    name: typeof entry.name === 'string' ? entry.name : `<entry at line ${line}>`,
                    kind,
                    field,
                    note,
                })
            for (const field of Object.keys(entry)) {
                if (NOT_AN_ASSERTION.has(field)) {
                    continue
                }
                if (!fieldsRead.has(field)) {
                    continue
                }
                if (evaluated.has(field)) {
                    continue
                }
                const inherited = !Object.prototype.hasOwnProperty.call(test, field)
                add('unread-field', field, inherited ? 'inherited from [defaults]' : 'written on the entry')
            }
            // Every list field the runner walks is a no-op when it is empty, and every one of them
            // reads as an assertion in the plan. Only the fields this kind evaluates: an empty
            // value on an unread field is already the finding above, once.
            for (const field of evaluated) {
                if (NOT_AN_ASSERTION.has(field)) {
                    continue
                }
                const value = entry[field]
                if (Array.isArray(value) && value.length === 0) {
                    add(
                        'empty-value',
                        field,
                        `an empty list: the runner's loop over it checks nothing${
                            Object.prototype.hasOwnProperty.call(test, field) ? '' : ' (inherited from [defaults])'
                        }`,
                    )
                } else if (EMPTY_STRING_IS_NOOP.has(field) && typeof value === 'string' && value.trim() === '') {
                    add('empty-value', field, "an empty string the runner reads as 'not written'")
                }
            }
            if (kind === 'differential') {
                if (entry.compare_to === undefined || entry.compare_to === '') {
                    add('half-pair', 'compare_to', 'a differential entry with no target compares against nothing')
                }
                if (!Array.isArray(entry.compare_fields) || entry.compare_fields.length === 0) {
                    add(
                        'half-pair',
                        'compare_fields',
                        'an empty field list compares nothing: `for (const field of [])` runs no comparison',
                    )
                }
            }
            if (kind === 'negative' && entry.expect_exit === undefined && entry.expect_signal === undefined) {
                add(
                    'label-without-check',
                    'kind',
                    'the label says the case must fail, and no field asks for a non-zero exit (or a signal)',
                )
            }
            if (!LABEL_KINDS.includes(kind) && !BLOCK_KINDS.includes(kind)) {
                add(
                    'kind-unread',
                    'kind',
                    kind === undefined
                        ? 'no kind is written and the schema does not require one: the entry runs as behavior'
                        : `no block in the runner reads kind '${kind}': the entry runs as behavior`,
                )
            }
        }
    }
    return { findings, plansRead, entriesRead, fields: fieldsRead.size }
}

function report(result) {
    for (const finding of result.findings) {
        const where = finding.line > 0 ? `${finding.file}:${finding.line}` : finding.file
        const kind = finding.kind === undefined ? '<none>' : finding.kind
        console.log(
            `plan-field ${where} ${finding.name} [${kind}] ${finding.category} ${finding.field} (${finding.note})`,
        )
    }
    const mode = ENFORCE ? 'enforce' : 'report'
    console.log(`plan-fields fields=${result.fields} findings=${result.findings.length} mode=${mode}`)
    console.log(
        `plan-fields read ${result.plansRead} plan file(s) and ${result.entriesRead} entries; ` +
            'a field the kind does not evaluate is not a test',
    )
    if (ENFORCE) {
        return result.findings.length > 0 ? 1 : 0
    }
    return ratchet(result, BASELINE_PATH)
}

/**
 * A report-only check with no ratchet is a number nobody has to answer for: it prints the same line
 * on every gate for months, and the day it goes up by ten the ten read like the three hundred that
 * were already there. So the reading is kept in `test/plan-fields-baseline.json`, and the rule is
 * one-way: **findings may not grow**. More than the baseline is a red, by name and by category; the
 * same count is green; fewer is green *and* says so, because a list coming down is the point and a
 * gate that stayed quiet about it would leave the baseline stale and the next growth unnoticed.
 *
 * `--update-baseline` is how the file moves, and it is the only writer: it is what a lane runs after
 * fixing something, so the number in the tree is always the number someone measured and accepted.
 */
function ratchet(result, baselinePath) {
    let baseline
    try {
        baseline = JSON.parse(fs.readFileSync(baselinePath, 'utf8'))
    } catch (error) {
        console.log(
            `FAIL plan-fields no baseline at ${path.relative(path.resolve(HERE, '..', '..'), baselinePath)} ` +
                `(${error.message}): run --update-baseline once and commit it`,
        )
        return 1
    }
    const counted = countByCategory(result.findings)
    const wasCounted = baseline.counted || {}
    const grown = Object.keys(counted)
        .filter((category) => counted[category] > (wasCounted[category] || 0))
        .map((category) => `${category} ${wasCounted[category] || 0} -> ${counted[category]}`)
    const delta = result.findings.length - baseline.count
    const categoryLine = Object.keys(counted)
        .sort()
        .map((category) => `${category}=${counted[category]}`)
        .join(' ')
    const where = path.relative(path.resolve(HERE, '..', '..'), baselinePath).split(path.sep).join('/')
    console.log(
        `plan-fields baseline=${baseline.count} delta=${delta >= 0 ? `+${delta}` : delta} ` +
            `ratchet=${delta > 0 || grown.length > 0 ? 'grown' : delta < 0 ? 'lowered' : 'ok'} ` +
            `counted(${categoryLine})`,
    )
    if (grown.length > 0 && delta <= 0) {
        console.log(
            `FAIL plan-fields a category grew while the total did not (${grown.join(', ')}): ` +
                `a new class of the defect is not the old one being fixed`,
        )
        return 1
    }
    if (delta > 0) {
        console.log(
            `FAIL plan-fields findings grew by ${delta} over ${where} (${baseline.count} -> ` +
                `${result.findings.length}): a field the kind does not read is not a test, so either fix ` +
                'the plan or, when the entry is right and the reading is wrong, fix the reading here',
        )
        return 1
    }
    if (delta < 0) {
        console.log(
            `plan-fields findings came down by ${-delta} (${baseline.count} -> ${result.findings.length}): ` +
                `run \`node test/tools/check-plan-fields.mjs --update-baseline\` to accept it in ${where}`,
        )
    }
    return 0
}

function countByCategory(findings) {
    const counted = {}
    for (const finding of findings) {
        counted[finding.category] = (counted[finding.category] || 0) + 1
    }
    return counted
}

function updateBaseline(result, baselinePath) {
    const baseline = {
        comment:
            'The number test/tools/check-plan-fields.mjs reported when it was last accepted: findings may ' +
            'not grow past `count`, and no category may grow past its own entry. Moving it down is the ' +
            'point; moving it up needs the entry to be right and the reading to be wrong. Rewrite this ' +
            'file with `node test/tools/check-plan-fields.mjs --update-baseline`, never by hand.',
        count: result.findings.length,
        counted: countByCategory(result.findings),
        takenAt: new Date().toISOString().slice(0, 10),
        revision: revisionOf(path.resolve(HERE, '..', '..')),
    }
    fs.writeFileSync(baselinePath, `${JSON.stringify(baseline, null, 4)}\n`)
    console.log(`plan-fields baseline written: ${baseline.count} finding(s) at ${baseline.takenAt}`)
    return 0
}

// The revision the reading was taken at, so a stale baseline can be told from a current one. Git may
// be absent (`--root` pointed at an export); the field is then `null`, which the reader can see.
function revisionOf(rootDir) {
    try {
        return execFileSync('git', ['-C', rootDir, 'rev-parse', '--short', 'HEAD'], { encoding: 'utf8' }).trim()
    } catch {
        return null
    }
}

// Comments and literals become spaces, at the same length as the text they came from, so an offset
// in one is an offset in the other: what is left is the code's own braces. Without this, a `{` in a
// template (`\`golden file does not exist: ${path}\``) would be counted as a block.
function stripStringsAndComments(text) {
    const out = text.split('')
    let index = 0
    const blank = (from, to) => {
        for (let at = from; at < to; at++) {
            if (out[at] !== '\n' && out[at] !== '\r') {
                out[at] = ' '
            }
        }
    }
    while (index < text.length) {
        const char = text[index]
        if (char === "'" || char === '"' || char === '`') {
            let end = index + 1
            while (end < text.length && text[end] !== char) {
                end += text[end] === '\\' ? 2 : 1
            }
            blank(index, Math.min(end + 1, text.length))
            index = end + 1
            continue
        }
        if (char === '/' && text[index + 1] === '/') {
            let end = index
            while (end < text.length && text[end] !== '\n') {
                end++
            }
            blank(index, end)
            index = end
            continue
        }
        index++
    }
    return out.join('')
}

// The span of one top-level function's braces, found in the comment- and string-free text.
function functionRange(stripped, signature) {
    const start = stripped.indexOf(signature)
    if (start < 0) {
        return null
    }
    const open = stripped.indexOf('{', start)
    if (open < 0) {
        return null
    }
    let depth = 0
    for (let index = open; index < stripped.length; index++) {
        if (stripped[index] === '{') {
            depth++
        } else if (stripped[index] === '}') {
            depth--
            if (depth === 0) {
                return { start, open, end: index }
            }
        }
    }
    return null
}

// The table against the source it came from. `validateTest` is the one function that decides what a
// kind reads, so the reading is: its `if (test.kind === '...')` guards, and the `test.<field>` names
// inside each. A field that moves out of a block, a fourth gated kind, or a block that stops being
// gated is a change to this table, and it fails here by name instead of letting the guard keep
// passing over a runner that no longer works that way.
function verifyRunner(runnerPath) {
    let source
    try {
        source = fs.readFileSync(runnerPath, 'utf8')
    } catch (error) {
        console.log(`FAIL plan-fields cannot read the runner: ${error.message}`)
        return 1
    }
    const stripped = stripStringsAndComments(source)
    const range = functionRange(stripped, 'async function validateTest(')
    if (range === null) {
        console.log('FAIL plan-fields validateTest is not in test/tools/run-tests.mjs: this table has no source')
        return 1
    }
    // The guards are matched in the source with its literals (the kind is a string literal); the
    // block ends are counted in the stripped text, where the braces are the code's own.
    const body = source.slice(range.start, range.end + 1)
    const bodyStripped = stripped.slice(range.start, range.end + 1)
    const failures = []
    const gatedKinds = []
    const guard = /if\s*\(\s*test\.kind\s*===\s*'([a-z_]+)'\s*\)\s*\{/g
    let match
    while ((match = guard.exec(body)) !== null) {
        const kind = match[1]
        const blockStart = bodyStripped.indexOf('{', match.index)
        let depth = 0
        let blockEnd = blockStart
        for (let index = blockStart; index < bodyStripped.length; index++) {
            if (bodyStripped[index] === '{') {
                depth++
            } else if (bodyStripped[index] === '}') {
                depth--
                if (depth === 0) {
                    blockEnd = index
                    break
                }
            }
        }
        const block = body.slice(blockStart, blockEnd + 1)
        const fields = new Set()
        const read = /\btest\.([A-Za-z_][A-Za-z0-9_]*)/g
        let field
        while ((field = read.exec(block)) !== null) {
            fields.add(field[1])
        }
        gatedKinds.push(kind)
        const expected = GATED[kind]
        if (expected === undefined) {
            failures.push(
                `the runner gates on kind '${kind}', which this table does not know: add it to GATED, ` +
                    'with the fields its block reads',
            )
            continue
        }
        // `normalizers` is read inside the snapshot block and for every other kind as well (the
        // result is normalized before any assertion reads it), so it is not a snapshot field;
        // `__planPath` and the other `__` names are the runner's own bookkeeping on the entry, not
        // a key a plan writes.
        const crossCutting = new Set(['normalizers'])
        const actual = [...fields].filter((name) => !crossCutting.has(name) && !name.startsWith('__')).sort()
        const want = [...expected].sort()
        if (actual.join(',') !== want.join(',')) {
            failures.push(
                `the ${kind} block reads [${actual.join(', ')}] and the table says [${want.join(', ')}]: ` +
                    'update GATED here and the docs/test-plans.md table together',
            )
        }
    }
    for (const kind of Object.keys(GATED)) {
        if (!gatedKinds.includes(kind)) {
            failures.push(`the table has a ${kind} block and validateTest has none any more`)
        }
    }
    for (const failure of failures) {
        console.log(`FAIL plan-fields ${failure}`)
    }
    if (failures.length > 0) {
        console.log(`FAIL plan-fields ${failures.length} of the table's rows no longer match the runner`)
        return 1
    }
    console.log(
        `plan-fields table ok: ${Object.keys(GATED).length} gated kind(s) ` +
            `(${gatedKinds.join(', ')}) match validateTest, and ${ALWAYS_EVALUATED.length} field(s) ` +
            'are evaluated for every kind',
    )
    return 0
}

let root = path.resolve(HERE, '..', '..')
let ENFORCE = false
let mode = 'tree'
let UPDATE_BASELINE = false
for (let index = 2; index < process.argv.length; index++) {
    const argument = process.argv[index]
    if (argument === '--root' && index + 1 < process.argv.length) {
        root = path.resolve(process.argv[++index])
    } else if (argument === '--enforce') {
        ENFORCE = true
    } else if (argument === '--report-only') {
        ENFORCE = false
    } else if (argument === '--verify-runner') {
        mode = 'runner'
    } else if (argument === '--update-baseline') {
        UPDATE_BASELINE = true
    } else {
        console.log(`FAIL plan-fields unknown argument: ${argument}`)
        process.exit(2)
    }
}

if (mode === 'runner') {
    process.exit(verifyRunner(path.join(HERE, 'run-tests.mjs')))
}
const result = scanTree(root)
process.exit(UPDATE_BASELINE ? updateBaseline(result, BASELINE_PATH) : report(result))