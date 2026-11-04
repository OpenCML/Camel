#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Checks the one disagreement between a plan and its tree that nothing else sees: prose that says
// a case is red today, on a case that runs green.
//
// `lane-stale` (8d0de4e3e) cleaned six such sentences out of `test/plans/`. The two plans were
// written on 2026-11-04 at 21:52 and 22:29 as *work owed* (`7cde424bd`, `4ed4ee6af`), the fixes
// landed hours later in the passes and not in the tests (`aa157702a`, `0add4cd3c`, `060fbffaa`),
// and the plan text was never revised against them, so it went on describing the world before the
// fix: "RED TODAY, expected to be fixed" over five cases that had been green since. Cleaning those
// six sentences fixes the leaves and not the mechanism -- the next plan written as debt has the
// same fate, because nothing reads the words back.
//
// So this checks them. A `desc`, a `known_fail_note` or a `#` comment under `test/plans/` that
// claims its case is red today must (a) be attributable to exactly one plan entry and (b) find that
// entry actually running red in a recorded run of *this* revision.
//
// The judgement is the run's, never the plan's own words. In particular an entry that carries a
// `known_fail` mark *and* runs green is refused here: the mark is the claim, the run is the
// reading, and reading the mark back would pass exactly the shape the six sentences had -- a mark
// that had outlived its red. The same distinction decides the two flavours of a red cell: a cell
// whose status is `fail` because the runner wrote "marked known_fail ... but passes, so the mark is
// out of date" is a case that *passed*, so it is green evidence and turns this check red.
//
// What counts as a claim is the fixed lexicon below. It is a word list and not a parser because a
// reader greps the same words; the one exception it carries is named in the lexicon itself
// (`still fails a run whose work has grown`, which is a sentence about a perf budget and not about
// a case). A claim the lexicon cannot attribute to an entry -- a file-header comment, a
// `[defaults]` block -- is an error and not a pass: an unattributable claim is one that no reading
// can ever contradict.
//
// Where the reading comes from: the record a plan run writes,
// `test/results/log/<timestamp>_<pid>/` -- `run.json` at the run root names the revision it was
// taken at, and each `<status>/<plan>/<test>/meta.json` carries that cell's own status. No reading
// is borrowed from another revision: the six sentences are the proof that a case can go green under
// a sentence without the sentence moving, so a run taken before the last commit says nothing about
// this tree. A claim whose entry has no cell at this revision is printed as `unmeasured` and
// counted -- never dropped.
//
//   node scripts/check-plan-debt.mjs                 the checkout this script sits in
//   node scripts/check-plan-debt.mjs --root DIR      a fixture tree (DIR/test/plans, DIR/test/results)
//   node scripts/check-plan-debt.mjs --head SHA      judge against SHA instead of git's HEAD
//   node scripts/check-plan-debt.mjs --selftest      the judgement itself, on fixtures it writes
//
// Exit 1 when a claim is contradicted by the run (its case is green, or its cells disagree) or
// cannot be attributed to one entry; 2 when there is nothing to judge against (no run at this
// revision, no `test/plans`) -- a check that could not read is never reported as a pass; 0
// otherwise, with every unmeasured claim named on its own line.

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

// --- the lexicon ------------------------------------------------------------
//
// `claim` is what a reader would read as debt; `prose` names the working it is *not*, and is cut
// out of the line before the claim is looked for. The performance-bound sentence is the only
// exception the tree needs today: "the budget ... still fails a run whose work has grown by two
// thirds" says a ceiling discriminates, which is a fact about a green test, and it lives in four
// comments. Anything else that reads like debt is a claim -- reword it, do not widen this.
const CLAIMS = [
    { claim: 'RED TODAY', re: /\bred today\b/gi },
    { claim: 'work owed', re: /\bwork owed\b/gi },
    { claim: 'works owed', re: /\bworks owed\b/gi },
    { claim: 'still owed', re: /\bstill owed\b/gi },
    { claim: 'owes work', re: /\bowes? work\b/gi },
    { claim: 'still red', re: /\bstill red\b/gi },
    { claim: 'currently fails', re: /\bcurrently (?:fails?|failing|red)\b/gi },
    { claim: 'still fail', re: /\bstill fails?\b/gi, prose: [/\bstill fails? a run\b/gi] },
    { claim: 'not yet', re: /\bnot yet\b/gi },
]

const STATUSES = ['pass', 'fail', 'xfail', 'skip']

// A cell's status alone does not say whether the case failed: the runner turns a marked case that
// starts passing into a `fail` whose only failure is this sentence, and that case passed.
const STALE_MARK = /marked known_fail/

/** A line with its working phrases blanked, so a claim cannot be found inside one of them. */
function stripProse(text, prose) {
    let out = text
    for (const re of prose || []) out = out.replace(re, (match) => ' '.repeat(match.length))
    return out
}

/** Every claim in one piece of text, as `{claim, phrase}`; one per phrase per piece. */
function findClaims(text, prose) {
    const pool = stripProse(text, prose)
    const found = []
    for (const entry of CLAIMS) {
        entry.re.lastIndex = 0
        const match = entry.re.exec(pool)
        if (match) found.push({ phrase: entry.claim, at: match.index, text: match[0] })
    }
    return found
}

// --- reading a plan ---------------------------------------------------------

/**
 * One plan file's entries and every claim in its prose. An entry is a `[[tests]]` or `[[sweeps]]`
 * block; `[defaults]` and the file header belong to no entry and are `null` here, which is what
 * makes a claim in them unattributable.
 *
 * The scan is line-oriented because the file has to be read the way a person reads it -- a claim in
 * a `#` comment four lines above a `desc` is the style this repository writes in. A multi-line
 * TOML value is followed to its closing `"""` so a claim on its second line is still that entry's.
 */
function scanPlan(relPath, text) {
    const entries = []
    const claims = []
    let current = null
    let open = null
    const lines = text.split(/\r?\n/)

    const record = (lineNumber, key, body, prose) => {
        for (const found of findClaims(body, prose)) {
            claims.push({
                planPath: relPath,
                line: lineNumber,
                where: key,
                phrase: found.phrase,
                text: found.text,
                entry: current,
            })
        }
    }

    for (const [index, line] of lines.entries()) {
        const lineNumber = index + 1
        if (open) {
            const close = line.indexOf('"""')
            if (close < 0) {
                record(lineNumber, open.key, line, open.prose)
                continue
            }
            record(lineNumber, open.key, line.slice(0, close), open.prose)
            open = null
            continue
        }
        const block = /^\s*\[\[\s*([A-Za-z_][A-Za-z0-9_]*)\s*\]\]\s*$/.exec(line)
        if (block) {
            current = { name: null, line: lineNumber, kind: block[1] }
            entries.push(current)
            continue
        }
        if (/^\s*\[[A-Za-z_]/.test(line)) {
            current = null
            continue
        }
        const comment = /^\s*#(.*)$/.exec(line)
        if (comment) {
            record(lineNumber, 'comment', comment[1], proseOf('comment'))
            continue
        }
        const field = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$/.exec(line)
        if (!field) continue
        const key = field[1]
        if (key === 'name' && current) {
            const quoted = /"((?:[^"\\]|\\.)*)"/.exec(field[2])
            current.name = quoted ? JSON.parse(`"${quoted[1]}"`) : null
        }
        if (field[2].startsWith('"""')) {
            const rest = field[2].slice(3)
            const close = rest.indexOf('"""')
            if (close < 0) open = { key, prose: proseOf(key) }
            record(lineNumber, key, close < 0 ? rest : rest.slice(0, close), proseOf(key))
            continue
        }
        record(lineNumber, key, field[2], proseOf(key))
    }
    return { entries, claims }
}

/** The working phrases that are prose in one kind of field. */
function proseOf(key) {
    return key === 'desc' || key === 'known_fail_note' || key === 'comment'
        ? CLAIMS.flatMap((entry) => entry.prose || [])
        : []
}

function planFiles(root) {
    const plansRoot = path.join(root, 'test', 'plans')
    const out = []
    const walk = (dir) => {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            const full = path.join(dir, entry.name)
            if (entry.isDirectory()) walk(full)
            else if (entry.isFile() && entry.name.endsWith('.plan.toml')) out.push(full)
        }
    }
    if (fs.existsSync(plansRoot)) walk(plansRoot)
    return out.sort()
}

/** A path as the run record writes it, comparable across checkouts: `test/plans/...`. */
function planKey(p) {
    const slashed = p.replace(/\\/g, '/')
    const at = slashed.lastIndexOf('/test/plans/')
    if (at >= 0) return slashed.slice(at + 1)
    return slashed
}

/** The longest entry name of this plan that a cell name belongs to (`x` owns `x`, `x.nvm`). */
function ownerOf(cellName, names) {
    let best = null
    for (const name of names) {
        if (cellName === name || cellName.startsWith(`${name}.`)) {
            if (!best || name.length > best.length) best = name
        }
    }
    return best
}

// --- reading the runs -------------------------------------------------------

function readJson(file) {
    try {
        return JSON.parse(fs.readFileSync(file, 'utf8'))
    } catch {
        return null
    }
}

/**
 * Every run directory under `<results>/log`, newest first, with the manifest it wrote. A directory
 * whose `run.json` cannot be read is kept with `manifest: null` and reported, because a run whose
 * revision is unreadable is the one thing that must not quietly answer "no run at this revision".
 */
function readRuns(resultsRoot) {
    const logRoot = path.join(resultsRoot, 'log')
    if (!fs.existsSync(logRoot)) return []
    const runs = []
    for (const entry of fs.readdirSync(logRoot, { withFileTypes: true })) {
        if (!entry.isDirectory()) continue
        const dir = path.join(logRoot, entry.name)
        const manifest = readJson(path.join(dir, 'run.json'))
        runs.push({
            name: entry.name,
            dir,
            manifest,
            revision: manifest && typeof manifest.revision === 'string' ? manifest.revision : null,
            startedAt: manifest && typeof manifest.started_at === 'string' ? manifest.started_at : '',
        })
    }
    runs.sort((lhs, rhs) => (lhs.startedAt < rhs.startedAt ? 1 : lhs.startedAt > rhs.startedAt ? -1 : lhs.name < rhs.name ? 1 : -1))
    return runs
}

/** One run's cells, keyed by `<plan>::<test>`; the newest run wins a cell it shares with an older one. */
function readCells(run, cells, notes) {
    for (const status of STATUSES) {
        const statusDir = path.join(run.dir, status)
        if (!fs.existsSync(statusDir)) continue
        for (const planDir of fs.readdirSync(statusDir, { withFileTypes: true })) {
            if (!planDir.isDirectory()) continue
            for (const testDir of fs.readdirSync(path.join(statusDir, planDir.name), { withFileTypes: true })) {
                if (!testDir.isDirectory()) continue
                const meta = readJson(path.join(statusDir, planDir.name, testDir.name, 'meta.json'))
                if (!meta || typeof meta.test !== 'string' || typeof meta.plan_path !== 'string') {
                    notes.push(`${run.name}: ${status}/${planDir.name}/${testDir.name}/meta.json is unreadable`)
                    continue
                }
                const key = `${planKey(meta.plan_path)}::${meta.test}`
                if (cells.has(key)) continue
                cells.set(key, {
                    status: meta.status || status,
                    failures: Array.isArray(meta.failures) ? meta.failures : [],
                    run: run.name,
                })
            }
        }
    }
}

/**
 * The tree's reading: the cells of every run taken at `head`, newest first, plus the plans those
 * runs selected. A run at another revision is not merged in -- its cells describe another tree.
 */
function readReading(resultsRoot, head) {
    const runs = readRuns(resultsRoot)
    const atHead = runs.filter((run) => run.revision === head)
    const cells = new Map()
    const selected = new Set()
    const notes = []
    for (const run of atHead) {
        for (const plan of (run.manifest && run.manifest.plans) || []) selected.add(planKey(plan))
        readCells(run, cells, notes)
    }
    const unreadable = runs.filter((run) => run.revision === null).length
    return { head, runs: atHead, runsWithoutRevision: unreadable, cells, selected, notes }
}

// --- the judgement ----------------------------------------------------------

/**
 * The verdict for one red claim: `red` when at least one of its cells ran red and none ran green,
 * `green` when a cell passed, `mixed` when both happened, `unmeasured` when the revision has no
 * cell for it. A cell's red is `xfail`, or a `fail` the stale-mark sentence did not write.
 */
function verdictOf(cells) {
    if (cells.length === 0) return { verdict: 'unmeasured', red: [], green: [] }
    const red = cells.filter((cell) => cell.status === 'xfail' || (cell.status === 'fail' && !STALE_MARK.test(cell.failures.join('\n'))))
    const green = cells.filter((cell) => cell.status === 'pass' || (cell.status === 'fail' && STALE_MARK.test(cell.failures.join('\n'))))
    const skipped = cells.filter((cell) => cell.status === 'skip')
    if (red.length > 0 && green.length > 0) return { verdict: 'mixed', red, green }
    if (green.length > 0) return { verdict: 'green', red, green }
    if (red.length > 0) return { verdict: 'red', red, green }
    if (skipped.length > 0) return { verdict: 'unmeasured', red, green }
    return { verdict: 'unmeasured', red, green }
}

/**
 * Judge every claim of the tree against one revision's reading. Returns the claims with their
 * verdicts and the ones that fail the check: contradicted (`green`, `mixed`), unattributable, or
 * ambiguous. `unmeasured` never fails -- the run that did not select the plan is named beside it.
 */
function judge({ root, head, resultsRoot }) {
    const files = planFiles(root)
    const reading = readReading(resultsRoot, head)
    const byPlan = new Map()
    for (const file of files) {
        const rel = planKey(path.relative(root, file).replace(/\\/g, '/'))
        const scanned = scanPlan(rel, fs.readFileSync(file, 'utf8'))
        byPlan.set(rel, { entries: scanned.entries.filter((entry) => entry.name), claims: scanned.claims })
    }
    const judged = []
    const failures = []
    for (const [rel, plan] of byPlan) {
        for (const claim of plan.claims) {
            const base = {
                planPath: rel,
                line: claim.line,
                where: claim.where,
                phrase: claim.phrase,
                entry: claim.entry ? claim.entry.name : null,
            }
            if (!claim.entry || !claim.entry.name) {
                judged.push({ ...base, verdict: 'unattributable', cells: [] })
                failures.push({ ...base, verdict: 'unattributable' })
                continue
            }
            const names = plan.entries.map((entry) => entry.name)
            const cells = []
            for (const [key, cell] of reading.cells) {
                const [cellPlan, cellTest] = key.split('::')
                if (cellPlan !== rel) continue
                if (ownerOf(cellTest, names) !== claim.entry.name) continue
                cells.push({ name: cellTest, ...cell })
            }
            const { verdict, red, green } = verdictOf(cells)
            const row = { ...base, verdict, cells, red, green }
            judged.push(row)
            if (verdict !== 'red' && verdict !== 'unmeasured') failures.push(row)
        }
    }
    return { judged, failures, reading, files: files.length, plans: byPlan.size }
}

// --- reporting --------------------------------------------------------------

function describe(row) {
    const cells = row.cells || []
    const detail = cells.length === 0
        ? 'no cell at this revision'
        : cells.map((cell) => `${cell.name}=${cell.status}${cell.status === 'fail' && STALE_MARK.test(cell.failures.join('\n')) ? '(marked, but passed)' : ''}`).join(' ')
    const state = {
        red: 'red, as the prose says',
        green: 'GREEN, but the prose says red',
        mixed: 'its cells disagree, so the prose is not true of the entry',
        unmeasured: 'unmeasured at this revision',
        unattributable: 'not attributable to one entry, so nothing can check it',
    }[row.verdict] || row.verdict
    return `${row.planPath}:${row.line} ${row.entry || '(no entry)'} ${row.where} "${row.phrase}": ${state} -- ${detail}`
}

function report(result) {
    const { judged, failures, reading, files } = result
    const counts = { red: 0, green: 0, mixed: 0, unmeasured: 0, unattributable: 0 }
    for (const row of judged) counts[row.verdict] = (counts[row.verdict] || 0) + 1
    console.log(
        `[plan-debt] ${judged.length} claim(s) in ${files} plan file(s), judged at ${reading.head}: ` +
            `${reading.runs.length} run(s) at this revision, ${reading.selected.size} plan(s) selected`,
    )
    for (const note of reading.notes) console.log(`[plan-debt] ${note}`)
    if (reading.runsWithoutRevision > 0) {
        console.log(`[plan-debt] ${reading.runsWithoutRevision} run directory(ies) record no revision and were not merged in`)
    }
    for (const row of judged) console.log(`[plan-debt] ${describe(row)}`)
    const unmeasured = judged.filter((row) => row.verdict === 'unmeasured')
    for (const row of unmeasured) {
        const why = reading.selected.has(row.planPath)
            ? 'the plan ran, but this run selected no cell for that entry (its tier or a precondition)'
            : 'no run at this revision selected that plan'
        console.log(`[plan-debt] unmeasured: ${row.planPath}:${row.line} ${row.entry} -- ${why}`)
    }
    console.log(
        `[plan-debt] judged: red ${counts.red || 0}, green ${counts.green || 0}, mixed ${counts.mixed || 0}, ` +
            `unmeasured ${counts.unmeasured || 0}, unattributable ${counts.unattributable || 0}`,
    )
    return failures
}

// --- the self-test ----------------------------------------------------------
//
// The judgement is four lines of code and five verdicts, and the one that matters cannot be checked
// against the tree: "a claim of red over a case that passes" is the shape this repository actually
// had, and the tree no longer has it. So the fixture writes it, along with the two flavours of a
// `fail` cell -- an ordinary one (the case is red) and the stale-mark one (the case passed, and the
// runner said so). Every claim below is asserted by verdict, and the fixture's own header comment
// is asserted unattributable: a claim no entry owns is the one kind of pass this check must never
// give.

function fixture(root) {
    const plans = path.join(root, 'test', 'plans')
    const run = path.join(root, 'test', 'results', 'log', '20260101_000000_1')
    fs.mkdirSync(plans, { recursive: true })
    const plan = [
        'id = "probe"',
        '# work owed: two of these are not done',
        '',
        '[[tests]]',
        'name = "probe.green"',
        'desc = "RED TODAY, expected to be fixed: the arm is handed the wrong place."',
        'case = "green.cml"',
        '',
        '[[tests]]',
        'name = "probe.red"',
        'desc = "The case still fails: the fold leaves a DATA between BRCH and its JOIN."',
        'case = "red.cml"',
        '',
        '[[tests]]',
        'name = "probe.marked"',
        'known_fail = true',
        'known_fail_note = "The mark stands because the case still fails."',
        'case = "marked.cml"',
        '',
        '[[tests]]',
        'name = "probe.unmeasured"',
        'desc = "not yet: this one has no reading here."',
        'case = "unmeasured.cml"',
        '',
    ].join('\n')
    fs.mkdirSync(plans, { recursive: true })
    fs.writeFileSync(path.join(plans, 'probe.plan.toml'), plan)
    const meta = (status, test, failures) => {
        const dir = path.join(run, status, 'probe', test)
        fs.mkdirSync(dir, { recursive: true })
        fs.writeFileSync(
            path.join(dir, 'meta.json'),
            JSON.stringify({ plan_path: 'test/plans/probe.plan.toml', plan_id: 'probe', test, status, failures: failures || [] }),
        )
    }
    meta('pass', 'probe.green')
    meta('fail', 'probe.red', ['expected exit 0, got 1'])
    meta('fail', 'probe.marked', ['marked known_fail but passes, so the mark is out of date and must be removed'])
    fs.mkdirSync(run, { recursive: true })
    fs.writeFileSync(
        path.join(run, 'run.json'),
        JSON.stringify({
            started_at: '2026-01-01T00:00:00.000Z',
            revision: 'selftest',
            plans: ['test/plans/probe.plan.toml'],
            targets: ['probe'],
        }),
    )
    return root
}

function selfTest() {
    const root = fixture(fs.mkdtempSync(path.join(os.tmpdir(), 'camel-plan-debt-')))
    const expected = [
        ['test/plans/probe.plan.toml', 2, '(no entry)', 'work owed', 'unattributable'],
        ['test/plans/probe.plan.toml', 6, 'probe.green', 'RED TODAY', 'green'],
        ['test/plans/probe.plan.toml', 11, 'probe.red', 'still fail', 'red'],
        ['test/plans/probe.plan.toml', 17, 'probe.marked', 'still fail', 'green'],
        ['test/plans/probe.plan.toml', 22, 'probe.unmeasured', 'not yet', 'unmeasured'],
    ]
    const result = judge({ root, head: 'selftest', resultsRoot: path.join(root, 'test', 'results') })
    let bad = 0
    for (const [planPath, line, entry, phrase, verdict] of expected) {
        const row = result.judged.find(
            (candidate) => candidate.planPath === planPath && candidate.line === line && candidate.phrase === phrase,
        )
        if (!row) {
            console.log(`selftest: no claim at ${planPath}:${line} "${phrase}"`)
            bad++
            continue
        }
        if (row.verdict !== verdict) {
            console.log(`selftest: ${planPath}:${line} "${phrase}": expected ${verdict}, got ${row.verdict}`)
            bad++
        }
        if (row.entry !== (entry === '(no entry)' ? null : entry)) {
            console.log(`selftest: ${planPath}:${line} "${phrase}": expected entry ${entry}, got ${row.entry}`)
            bad++
        }
    }
    // The stale-mark cell must *not* count as red evidence: it is the one shape this check exists
    // for, and a judgement that read `fail` as red would pass it.
    const marked = result.judged.find((row) => row.entry === 'probe.marked')
    if (!marked || marked.verdict !== 'green') {
        console.log('selftest: a known_fail mark whose case passed was not read as green')
        bad++
    }
    const failing = result.failures.map((row) => `${row.entry}:${row.verdict}`).sort().join(' ')
    if (failing !== 'null:unattributable probe.green:green probe.marked:green') {
        console.log(`selftest: failures are '${failing}', expected the green and the unattributable claim`)
        bad++
    }
    fs.rmSync(root, { recursive: true, force: true })
    if (bad > 0) return 1
    console.log(`plan-debt self-test ok: ${result.judged.length} claim(s), 5 verdict(s)`)
    return 0
}

// --- entry point ------------------------------------------------------------

function headOf(root) {
    const proc = spawnSync('git', ['-C', root, 'rev-parse', 'HEAD'], { encoding: 'utf8', windowsHide: true })
    if (proc.error || proc.status !== 0) return null
    const sha = proc.stdout.trim()
    return sha || null
}

function main(argv) {
    const scriptRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
    const valueOf = (flag) => {
        const at = argv.indexOf(flag)
        if (at < 0) return undefined
        if (argv[at + 1] === undefined || argv[at + 1].startsWith('--')) return ''
        return argv[at + 1]
    }
    if (argv.includes('--selftest')) return selfTest()
    const rootFlag = valueOf('--root')
    const root = rootFlag === undefined ? scriptRoot : path.resolve(rootFlag)
    if (rootFlag !== undefined && !fs.existsSync(root)) {
        console.log(`--root ${rootFlag}: no such directory`)
        return 2
    }
    const headFlag = valueOf('--head')
    const head = headFlag === undefined ? headOf(root) : headFlag
    if (!head) {
        console.log('[plan-debt] cannot read this tree\'s revision (git rev-parse HEAD failed): no reading can be judged against it')
        return 2
    }
    const resultsFlag = valueOf('--results')
    const resultsRoot = resultsFlag === undefined
        ? path.join(root, 'test', 'results')
        : path.resolve(resultsFlag)
    if (planFiles(root).length === 0) {
        console.log(`[plan-debt] no *.plan.toml under ${path.join(root, 'test', 'plans')}: refusing to report a pass over nothing`)
        return 2
    }
    const result = judge({ root, head, resultsRoot })
    const failures = report(result)
    const contradicted = failures.length
    if (contradicted > 0) {
        // A contradicted claim is the defect this check exists for; an unattributable one is a claim
        // no reading can ever contradict, which is the same defect one step earlier. Neither is a
        // pass, and they are counted apart so the two are not read as one number.
        const unattributable = failures.filter((row) => row.verdict === 'unattributable').length
        console.log(
            `[plan-debt] ${contradicted - unattributable} claim(s) contradicted by the run, ` +
                `${unattributable} not attributable to an entry`,
        )
        return 1
    }
    if (result.reading.runs.length === 0 && result.judged.length > 0) {
        console.log(
            `[plan-debt] no plan run at ${head} recorded under ${resultsRoot}: nothing here could be judged. ` +
                `Run the plans, then run this check in the same gate (-Plan <target> -Check plan-debt).`,
        )
        return 2
    }
    console.log('[plan-debt] every claim is backed by a case that runs red at this revision')
    return 0
}

process.exit(main(process.argv.slice(2)))