#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A plan field the runner never reads is not a test.
 *
 * The shape it exists for: an entry with `kind = "negative"` and, beside it, a `compare_to` and a
 * `compare_fields` naming a twin entry that had to print the same thing. That text says "this run
 * must equal that one"; the runner compares on `compare_to` only under `kind = "differential"`
 * (run-tests.mjs, the `if (test.kind === 'differential')` block), so the entry checks its exit code
 * and never compares a byte. The instance reported against `test/plans/gpu/tiles.plan.toml` on
 * 2026-11-14 was already `differential` in that baseline, and no entry in the tree writes
 * `compare_to` outside one -- so this guard proves itself on a probe plan (the readings are in the
 * commit that added it) rather than on a live entry, and it stays because the class is one field
 * away from coming back.
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
 *   expect_no_diagnostics                      yes       yes      yes       yes        yes          yes
 *                                              (the absence of a diagnostic; when it is set the
 *                                              four rows above are not evaluated for that entry)
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
 *                                              `require_artifacts` is one of these: it fails an entry
 *                                              whose product the build did not produce, so it decides
 *                                              whether there is a reading, not what the reading says
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
 *   mark-without-end-trigger         a `known_fail` mark with no `known_fail_ends_when`: a debt with
 *                        no reading that ends it
 *   end-trigger-names-nothing        a trigger that quotes no reading
 *   end-trigger-names-another-reading  a trigger that does not name this entry's own pinned line
 *   path-without-a-tree  a rule of `test/plan-selection.json` names a path prefix that is not in
 *                        the tree (`path-exception-stale`: recorded in `SELECTION_PATH_EXCEPTIONS`
 *                        and no longer needed). This one is not ratcheted: see below.
 *
 * The last row is the same defect one file over. `test/plan-selection.json` decides which plans a
 * change runs, and a rule is a list of path prefixes; a prefix that is not in the tree matches no
 * file, so the rule selects nothing -- and nothing says so, because `--changed` widens a file no
 * rule matched to the whole fast layer and the run is green. `include/camel/parse/` sat in that
 * state from the layout move (2026-11-22) until this reading existed: its twin,
 * `src/parse/include/camel/parse/`, was already there under the new layout, so one rule of the file
 * matched no file at all and every file under `include/` -- a directory the tree no longer has --
 * was read as "no plan needed". A counting ratchet is the wrong shape for it: a rule that names
 * nothing is a defect to remove, not a debt to spend down, so a finding here fails the run whatever
 * the baseline says.
 *
 * `SELECTION_PATH_EXCEPTIONS` exists for a prefix the tree cannot carry yet, with the reason it
 * will, and an entry that is no longer needed is a finding. It holds nothing today. **An exception
 * table that can hold a broken rule is a table that makes the check stop speaking**: the rule above
 * is that a rule naming nothing is fixed or deleted, never recorded here.
 *
 * It is **ratcheted**: it prints the line `plan-fields fields=<n> findings=<n> mode=report`, then
 * compares the count with `test/tools/plan-fields-baseline.json`. Findings that grow past that file
 * exit non-zero -- by name and by category -- and findings that come down exit zero with the line
 * that says to accept the new number (`--update-baseline` is the only writer). A missing baseline is
 * a red too, never a pass over nothing. This is what makes the check a wall rather than a reading:
 * the gate runs it on every run and its exit code decides, so "may not grow" holds and "came down"
 * is the reward.
 *
 * ## Two states, two reports, because a false red costs what a false green costs
 *
 * The baseline keeping a count means the tree can disagree with it in two different ways, and
 * printing both as the same three hundred lines taught the reader to skip the list -- on the day it
 * is the only useful thing there is.
 *
 *   ratchet broken   the count moved, or a category did while the total did not. Entries were added,
 *                    removed or re-kinded, the baseline has to follow, and the per-entry list is the
 *                    evidence: it is printed, with one line saying how many entries the two disagree
 *                    on, and the exit code is non-zero.
 *   in agreement     the count is exactly the baseline's and no category grew. Nothing is printed
 *                    entry by entry, and nothing is claimed about the entries either: the report is
 *                    the `ratchet=ok` line, and the exit code is zero.
 *
 * The list is therefore not "the report"; it is what a broken ratchet prints. Nor can the quiet
 * case be described entry by entry: the baseline keeps a count and per-category counts, not
 * per-entry identities, so with the total and every category unchanged the check has nothing to say
 * about which entries they are -- and a line that says "N entries do not match" on every green run
 * is a claim about nothing, read as noise, and noise is where the real red hides.
 *
 * `--enforce` is the stricter mode: *any* finding fails, baseline or not. It is what the entry
 * becomes once the list reaches zero, and the two are one edit apart.
 *
 * `--report-only` is the old posture kept for a reader who wants the numbers and not the exit code:
 * it prints the findings and exits zero, ratchet included.
 *
 * `--verify-runner` is the other half, and the reason this table can be trusted: it reads
 * `test/tools/run-tests.mjs` itself and refuses a table that no longer matches the runner -- the
 * three kind-gated blocks by name, and every field listed as evaluated for every kind must be read
 * somewhere in the source. A hard-coded table that outlives the code it describes is the same
 * defect one level up -- a guard that asserts nothing -- so the table is checked against the source
 * it came from, and adding a fourth gated kind or moving a field out of a block fails here by name.
 *
 *   node test/tools/check-plan-fields.mjs                 # the tree, ratcheted against the baseline
 *   node test/tools/check-plan-fields.mjs --enforce       # the tree, findings are a failure
 *   node test/tools/check-plan-fields.mjs --report-only   # the tree, findings and exit zero
 *   node test/tools/check-plan-fields.mjs --verify-runner # the table against run-tests.mjs
 *   node test/tools/check-plan-fields.mjs --update-baseline
 *   node test/tools/check-plan-fields.mjs --root <checkout>
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'
import { execFileSync } from 'node:child_process'

import { parseTomlFile } from './toml-lite.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
// What the last accepted reading was, beside the only script that reads it -- and under `test/tools/`
// rather than beside `test/perf-baseline.json`, because a tracked file the selection rules name
// nothing for is a hole in `--changed` (test/tools/selftest/run.mjs). Read by the ratchet below,
// written only by `--update-baseline`.
const BASELINE_PATH = path.join(HERE, 'plan-fields-baseline.json')

// The selection rules' own exceptions, and the rule for this list: **an exception table that can
// hold a broken rule is a table that makes the check stop speaking.** A rule whose prefix is not in
// the tree is a defect -- the plans it was written for are selected by no change, and `--changed`
// widens instead of failing -- so it is fixed or deleted, never recorded here. An entry is for a
// prefix the tree cannot carry yet, with the reason it will; an entry whose path has arrived, or
// whose prefix no rule names any more, is reported so the list cannot outlive what it waited for.
const SELECTION_PATH_EXCEPTIONS = []

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
    // The absence of a diagnostic, which a plan could not say before this field existed
    // (`expect_diagnostic_name = ""` only cancels an inherited name). It stands in place of the four
    // rows above when it is set, so an entry naming it is not also expected to name a diagnostic.
    'expect_no_diagnostics',
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
                    // An empty string here is not always decoration: the entry's own value replaces
                    // the one `[defaults]` gave it, and the runner then skips the block altogether.
                    // That is how an entry cancels an inherited expectation, and it is the one shape
                    // of this finding a reader must not "fix" by deleting the line.
                    const replaced = defaults[field]
                    add(
                        'empty-value',
                        field,
                        replaced === undefined
                            ? "an empty string the runner reads as 'not written'"
                            : `an empty string the runner reads as 'not written': it cancels \`[defaults]\`'s ` +
                                  `${JSON.stringify(replaced)} and asserts nothing itself`,
                    )
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
            // A mark records a red; `known_fail_owner` says who ends it and `known_fail_reason` where
            // it is stuck, and neither says *what* would end it. A debt with no trigger reads as
            // permanent, and the runner can only notice the two moments it already watches (the case
            // starts passing; the refusal changes). `known_fail_ends_when` names the reading that
            // removes the mark, so the next reader -- and the next lane -- can tell a mark that waits
            // on a named wall from one that waits on nothing.
            const marked =
                entry.known_fail === true ||
                (Array.isArray(entry.known_fail) && entry.known_fail.length > 0)
            if (marked) {
                const ends = entry.known_fail_ends_when
                const pinned = Array.isArray(entry.expect_stdout_contains)
                    ? entry.expect_stdout_contains.filter((line) => typeof line === 'string' && line !== '')
                    : []
                if (typeof ends !== 'string' || ends.trim() === '') {
                    add(
                        'mark-without-end-trigger',
                        'known_fail_ends_when',
                        'the mark records a red and no reading that ends it: name the refusal that must go and the line this entry must print',
                    )
                } else if (!/`[^`]+`/.test(ends)) {
                    add(
                        'end-trigger-names-nothing',
                        'known_fail_ends_when',
                        'the trigger quotes no reading: put the sentence that must stop appearing, and the line this entry must then print, in backticks',
                    )
                } else if (pinned.length > 0 && !pinned.some((line) => ends.includes(line))) {
                    add(
                        'end-trigger-names-another-reading',
                        'known_fail_ends_when',
                        `the trigger does not name this entry's own pinned line (${JSON.stringify(pinned[0])}): a mark ends when the entry prints what it pins`,
                    )
                }
            }
        }
    }
    return { findings, plansRead, entriesRead, fields: fieldsRead.size }
}

/**
 * Every `paths` entry of `test/plan-selection.json` against the tree it selects in.
 *
 * A rule is a list of prefixes, and `--changed` looks a changed file up under one: a prefix that is
 * not in the tree matches no file, and a file no rule matched is dropped from the union or widens
 * the run to the whole fast layer -- a green run either way. So the rule reads as coverage and is
 * none, which is why this asks the tree instead of trusting the rule's own text.
 */
function scanSelection(rootDir) {
    const relative = 'test/plan-selection.json'
    const file = path.join(rootDir, 'test', 'plan-selection.json')
    const unreadable = (note) => ({
        findings: [
            {
                category: 'selection-unreadable',
                file: relative,
                line: 0,
                name: relative,
                kind: 'selection',
                field: 'paths',
                note: `${note}: the selection rules were not checked`,
            },
        ],
        rules: 0,
        paths: 0,
        exceptions: SELECTION_PATH_EXCEPTIONS.length,
    })
    let text
    try {
        text = fs.readFileSync(file, 'utf8')
    } catch (error) {
        return unreadable(`cannot be read (${error.code || error.message})`)
    }
    let doc
    try {
        doc = JSON.parse(text)
    } catch (error) {
        return unreadable(`is not JSON (${error.message})`)
    }
    const lines = text.split('\n')
    // Where a reader starts: the first line naming the prefix, quoted as JSON quotes it. A shorter
    // prefix does not match inside a longer one (`include/camel/parse/` is not found inside
    // `src/parse/include/camel/parse/`, because the quote in front of it belongs to `src/parse/`).
    const lineOf = (prefix) => {
        const needle = JSON.stringify(prefix)
        const at = lines.findIndex((line) => line.includes(needle))
        return at < 0 ? 0 : at + 1
    }
    const add = (category, prefix, note) =>
        findings.push({
            category,
            file: relative,
            line: lineOf(prefix),
            name: prefix,
            kind: 'selection',
            field: 'paths',
            note,
        })
    const findings = []
    const exceptions = new Map(SELECTION_PATH_EXCEPTIONS.map((entry) => [entry.path, entry]))
    const named = new Set()
    let rules = 0
    let paths = 0
    for (const list of [doc.directoryRules || [], doc.rules || []]) {
        for (const rule of list) {
            if (!Array.isArray(rule.paths) || rule.paths.length === 0) continue
            rules += 1
            for (const prefix of rule.paths) {
                if (typeof prefix !== 'string' || prefix === '') continue
                paths += 1
                named.add(prefix)
                const present = fs.existsSync(path.join(rootDir, prefix))
                const exception = exceptions.get(prefix)
                if (present && exception) {
                    add(
                        'path-exception-stale',
                        prefix,
                        `recorded as "${exception.why}", and the tree has the path now: remove the entry`,
                    )
                } else if (!present && !exception) {
                    add(
                        'path-without-a-tree',
                        prefix,
                        `the rule selects ${JSON.stringify(rule.plans)} and the prefix is not in the tree: ` +
                            'it matches no file, so a change under it runs the widened fast layer and not these plans',
                    )
                }
            }
        }
    }
    for (const [prefix, exception] of exceptions) {
        if (!named.has(prefix)) {
            add(
                'path-exception-stale',
                prefix,
                `recorded as "${exception.why}", and no rule names the prefix any more: remove the entry`,
            )
        }
    }
    return { findings, rules, paths, exceptions: SELECTION_PATH_EXCEPTIONS.length }
}

// The per-entry list, and only the per-entry list: who calls it decides whether it is the report
// (a broken ratchet, `--enforce`, `--report-only`) or noise (a baseline that is merely stale).
function printFindings(findings) {
    for (const finding of findings) {
        const where = finding.line > 0 ? `${finding.file}:${finding.line}` : finding.file
        const kind = finding.kind === undefined ? '<none>' : finding.kind
        console.log(
            `plan-field ${where} ${finding.name} [${kind}] ${finding.category} ${finding.field} (${finding.note})`,
        )
    }
}

// What was read, in two lines, whatever the verdict: the counts a reader compares across runs.
function printReading(result) {
    const mode = ENFORCE ? 'enforce' : 'report'
    console.log(`plan-fields fields=${result.fields} findings=${result.findings.length} mode=${mode}`)
    console.log(
        `plan-fields read ${result.plansRead} plan file(s) and ${result.entriesRead} entries; ` +
            'a field the kind does not evaluate is not a test',
    )
}

function report(result, selection) {
    // The selection reading first, and on every verdict: `exceptions=0` is the line that says the
    // rules were counted and nothing was let through an exception.
    console.log(
        `plan-fields selection paths=${selection.paths} rules=${selection.rules} exceptions=${selection.exceptions}`,
    )
    if (ENFORCE) {
        printFindings([...result.findings, ...selection.findings])
        printReading(result)
        return result.findings.length + selection.findings.length > 0 ? 1 : 0
    }
    if (REPORT_ONLY) {
        // The posture this check had before it became a wall: the same lines, no exit code.
        printFindings(result.findings)
        printFindings(selection.findings)
        printReading(result)
        console.log('plan-fields report only: the count is not compared with the baseline')
        return 0
    }
    const code = ratchet(result, BASELINE_PATH)
    // A rule that names nothing is a defect and not a debt: the ratchet above may come down and this
    // still fails, which is the difference between a count and a wall.
    if (selection.findings.length > 0) {
        printFindings(selection.findings)
        console.log(
            `FAIL plan-fields ${selection.findings.length} path(s) of test/plan-selection.json name no file in ` +
                'the tree: a rule that matches no file selects nothing, and the change that should have ' +
                'run its plans runs the widened fast layer instead',
        )
        return 1
    }
    return code
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
    // Which of the two disagreements this is decides whether the list is printed, and it is decided
    // before anything is. `broken` is a ratchet that grew: the set of entries moved (the count, or a
    // category under an unchanged total), so the baseline has to follow and the list is the evidence
    // -- it names the entries. `lowered` also prints it, for the same reason: entries changed. Only a
    // tree whose count is exactly the baseline's and whose categories are all within it is the other
    // state -- nothing broke, the baseline is stale, and 300 lines of the findings it already
    // accepted are what made a reader stop reading them.
    const broken = delta > 0 || grown.length > 0
    if (broken || delta < 0) {
        printFindings(result.findings)
    }
    printReading(result)
    console.log(
        `plan-fields baseline=${baseline.count} delta=${delta >= 0 ? `+${delta}` : delta} ` +
            `ratchet=${broken ? 'grown' : delta < 0 ? 'lowered' : 'ok'} ` +
            `counted(${categoryLine})`,
    )
    if (broken) {
        // The set of entries moved, so the baseline no longer describes the tree: say in one line
        // how many entries the two disagree on, then the rows themselves. This line is printed
        // *only* here. An earlier revision printed it whenever the count happened to equal the
        // baseline's as well -- "333 existing entries do not match, ratchet unbroken" beside
        // `delta=+0 ratchet=ok` -- which is a claim about nothing: the baseline keeps a count and
        // per-category counts, not per-entry identities, so with the total and every category
        // unchanged there is no evidence of a disagreement to report. A line that appears on every
        // green run says "something is wrong" every day and is read as noise, which costs exactly
        // what the false green costs.
        const moved =
            delta === 0
                ? `${result.findings.length} entries, a category moved under an unchanged total`
                : `${Math.abs(delta)} entr${Math.abs(delta) === 1 ? 'y' : 'ies'} ${
                      delta > 0 ? 'the baseline does not record' : 'the tree no longer carries'
                  } (${baseline.count} -> ${result.findings.length})`
        console.log(`baseline not in sync with the working tree: ${moved}, ratchet broken`)
    }
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
    // The other half of the table: a field listed as evaluated for every kind has to be read by the
    // runner at all. A row added for a field nobody reads is the same defect one level up -- the
    // table would then certify an entry the runner ignores -- so the name is looked for in the
    // source, not only counted here. Two fields cannot be found that way and say why.
    const readIndirectly = new Map([
        [
            'expect_stdout_exact',
            'read as `test[field]` by the loop over [expect_stdout_exact, expect_stdout] in validateTest',
        ],
        ['expect_stdout', 'the legacy name of the same loop'],
    ])
    for (const field of ALWAYS_EVALUATED) {
        if (readIndirectly.has(field)) {
            continue
        }
        if (!new RegExp(`\\btest\\.${field}\\b`).test(source)) {
            failures.push(
                `the table says '${field}' is evaluated for every kind and run-tests.mjs never reads ` +
                    `test.${field}: either the table is wrong or the runner dropped the field`,
            )
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

let ENFORCE = false
let REPORT_ONLY = false

/**
 * Everything the command line decides, in one function and with one exit code at the end.
 *
 * The code is *set*, never `process.exit`ed. This check's reading is three hundred-odd findings and
 * then the two lines that say what they amount to (`plan-fields read ...`, `ratchet=...`), and the
 * gate reads stdout through a pipe (`spawn(..., stdio: ['ignore', 'pipe', 'pipe'])`,
 * run-tests.mjs). A write to a pipe is queued and returns; `process.exit` leaves with the queue
 * still full, so the reading arrived as 43,283 bytes ending inside a finding and
 * `feat/tools/plan_fields.tree` went red on a tree that passes -- the child's own exit code 0 and
 * empty stderr say it had printed the whole thing. Ending at the end of the script writes the last
 * line first. `writing-freedom.mjs` carries the same note for the same reason: it reported an empty
 * stdout under the plan while printing its report on a terminal.
 */
function main(argv) {
    let root = path.resolve(HERE, '..', '..')
    let mode = 'tree'
    let UPDATE_BASELINE = false
    for (let index = 0; index < argv.length; index++) {
        const argument = argv[index]
        if (argument === '--root' && index + 1 < argv.length) {
            root = path.resolve(argv[++index])
        } else if (argument === '--enforce') {
            ENFORCE = true
        } else if (argument === '--report-only') {
            ENFORCE = false
            REPORT_ONLY = true
        } else if (argument === '--verify-runner') {
            mode = 'runner'
        } else if (argument === '--update-baseline') {
            UPDATE_BASELINE = true
        } else {
            console.log(`FAIL plan-fields unknown argument: ${argument}`)
            return 2
        }
    }
    if (mode === 'runner') {
        return verifyRunner(path.join(HERE, 'run-tests.mjs'))
    }
    const result = scanTree(root)
    const selection = scanSelection(root)
    return UPDATE_BASELINE ? updateBaseline(result, BASELINE_PATH) : report(result, selection)
}

process.exitCode = main(process.argv.slice(2))