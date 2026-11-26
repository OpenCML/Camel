#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The autodiff migration ledger, as a command instead of a morning's arithmetic.
//
// The question the ledger answers is narrow: **which of the old autodiff cases can run on the `vjp`
// library today, and which of them have stopped?** The page that holds the answer
// (internal/archive/m4-autodiff-migration-table.cn.md) is written by hand, and every time it was
// recounted by hand the count came out wrong -- once by one (a probe rewrote a whole import line and
// took the helper names with it), once with a blind spot (measuring every case by exit 0, when some
// of them guard an exit 1 on purpose). So the counting is here, and the page reads the number.
//
//   node scripts/autodiff-migration-ledger.mjs                  the table and the one-line summary
//   node scripts/autodiff-migration-ledger.mjs --quiet           only the `[LEDGER]` line
//   node scripts/autodiff-migration-ledger.mjs --case NAME       one case, with its diagnostic
//   node scripts/autodiff-migration-ledger.mjs --ledger-check    compare this run with the record
//   node scripts/autodiff-migration-ledger.mjs --record          write the record from this run
//   node scripts/autodiff-migration-ledger.mjs --record --force  ... including cases that fell
//
// **The one file this writes is its own record**, and only under `--record`. Nothing here edits a
// case, a plan or a document: the page is a human's, and this prints the numbers the human reads.
//
// --- how a case is measured ---------------------------------------------------------------------
//
// 1. The case is copied to a probe beside itself and its `import { ... } from autodiff` is **split in
//    two**: the transform names the library owns (`vjp`, `grad`, `with_grad`) come from `vjp`, and
//    every other name stays on `autodiff`, which still has them (`add_tangent`, `zero_tangent`, ...).
//    Rewriting the whole line to `from vjp` is what turned `transform_rules` into a false failure.
// 2. The case is run with the pass its plan runs it with, and **passes when the exit code is the one
//    its own plan expects** -- not when it exits 0, and not when a diagnostic sounds like a refusal.
//    `with_grad_old_name` guards that `value_and_grad` does not exist: its expectation is exit 1, so
//    exit 0 would be the failure. When the plan also names output strings, every one must be there.
//
// A case that no `[[tests]]` table under `test/plans` names has no expectation: it is printed with
// `want = none` and **is not counted as passing**. A ledger that guesses an expectation to keep its
// count up is the defect this column exists to avoid.
//
// --- what this ledger no longer does ------------------------------------------------------------
//
// **It does not classify the cases, and it counts no classes.** The three columns -- an array or
// tensor type in the source, a function type carrying a `with` parameter, everything else -- were
// counted by spelling, and a comment is not a spelling: a case that explained itself in a doc comment
// moved class with nothing rewritten and reded the old record, and the sentence then had
// to be reworded to get the reading back. That is the ruler bending the thing it measured, so the
// classifier is deleted rather than tuned. What it was asked is not a run of anything either: the
// array step is judged by `grad(f)([3.0, 2.0])` printing `[2.0, 3.0]` and `skip=body` gone.
//
// --- the record ---------------------------------------------------------------------------------
//
// `--ledger-check` compares this run against the last one recorded, and it compares **names**: the
// record holds the case paths that passed, not a count of them. A count says *how many* fell; a list
// says *which one* fell, and that is the only thing a reader can act on.
//
// **One direction: a case that passed at the last record may not stop passing.** A case that starts
// passing is progress -- it is printed and `--record` puts it in the record, but it is never red.
// `--record` **refuses to drop a name** without `--force`, because a fall is the defect this record
// exists to find: the diff of the record then shows the case that fell, for a reviewer to refuse, and
// the commit that records it has to say why.
//
// This record used to be six numbers in a constant in this file, and every move in any of them was a
// hand edit in the same commit that moved the reading. That shape cannot catch anything: whoever moves
// a reading also moves the thing it is compared with, so the "ratchet" only ever catches the edits
// nobody bothered to make twice, and a number moved by the wrong cause reads exactly like one moved by
// the right cause. The record now lives where a diff can show it
// (`test/cases/tools/autodiff_migration_ledger/baseline.json`) and states case paths.
//
// **The record is a run, so it belongs to a binary.** `pass` is measured by running
// `out/latest/bin/camel`, so the same tree reads differently under a different build of that file: a
// record taken on one host is a floor for that host. `recordedFrom` names the tree and the binary the
// run was, is never compared, and exists for exactly that reason.
//
// Where the expectation comes from. It is the `[[tests]]` table that names the case, wherever under
// `test/plans` it lives. The migration cases' home is `feat/modules`, but some of them are declared in
// `feat/linear`, `feat/compile` and `feat/mm`: a hard-coded list of directories cannot stay complete
// -- it did not, and cases were measured against a default `expect_exit = 0` that nobody had written
// for them. The plan files are read with a small bracket scanner, not a TOML library: the repository
// has no dependency for that, and the keys this needs (`case`, `args`, `expect_exit`, the output
// strings) sit in flat `[[tests]]` tables.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const CASE_DIR = path.join(ROOT, 'test', 'cases', 'modules', 'autodiff')
// Where a case's expectation is read from. **Every** plan under `test/plans`, not one directory: the
// migration cases' home is `feat/modules`, but more than a dozen of them are declared somewhere else
// entirely, and a hard-coded list of directories cannot stay complete.
const PLAN_ROOT = path.join(ROOT, 'test', 'plans')
// The one directory whose plans are read first, so the cases it already covers keep the exact test
// they had before this scan widened: the test to use is "the first one seen, unless a later one is
// named `*.nvm`", and reordering the scan would silently repick tests.
const HOME_PLAN_DIR = path.join(PLAN_ROOT, 'feat', 'modules')
const LIBRARY_NAMES = new Set(['vjp', 'grad', 'with_grad'])
const OFF_PATH_DIR = 'jax_names'
// The record `--ledger-check` compares against, written only by `--record`. It lives under
// `test/cases` so that recording a new baseline selects the plan that runs this check.
const RECORD_PATH = path.join(ROOT, 'test', 'cases', 'tools', 'autodiff_migration_ledger', 'baseline.json')

const args = process.argv.slice(2)
const quiet = args.includes('--quiet')
const ledgerCheck = args.includes('--ledger-check')
const record = args.includes('--record')
const force = args.includes('--force')
const onlyCase = args.includes('--case') ? args[args.indexOf('--case') + 1] : undefined
let loaderFailure = ''

// `--case` narrows the run to one case, so the set it passes is not the set the record holds: a
// comparison would report every other case as having fallen, and a record would replace the floor with
// one name. Refused rather than narrowed, because both of those would look like an answer.
if ((record || ledgerCheck) && onlyCase) {
    console.error(
        'ledger: --case runs one case, so it cannot be compared with the record or written into it; ' +
            'drop one of the two.',
    )
    process.exit(2)
}

function relativeToRoot(file) {
    return path.relative(ROOT, file).split(path.sep).join('/')
}

function camelBinary() {
    const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
    const exe = process.platform === 'win32' ? 'camel.exe' : 'camel'
    return path.join(home, 'bin', exe)
}

function binaryStamp() {
    try {
        return fs.statSync(camelBinary()).mtime.toISOString()
    } catch {
        return 'absent'
    }
}

// --- reading the expectations out of the plans ---------------------------------------------------
//
// Each `[[tests]]` table is a flat run of `key = value` lines; `args` is the only list. A test's pass
// is the first word of `args` that is not the case itself, with `${run_nvm}` and friends spelled out.
const PASS_WORDS = {
    '${run_nvm}': 'std::nvm',
    '${run_fvm}': 'std::fvm',
    '${run_jit}': 'std::jit',
    '${run_prl}': 'std::prl',
    '${run_macro}': 'std::macro',
}

function readTests(file) {
    const text = fs.readFileSync(file, 'utf8')
    const tests = []
    for (const block of text.split(/^\[\[tests\]\]$/m).slice(1)) {
        const test = { name: '', case: '', args: [], expectExit: 0, contains: [] }
        const name = /^name = "([^"]*)"/m.exec(block)
        if (name) test.name = name[1]
        const kase = /^case = "([^"]*)"/m.exec(block)
        if (!kase) continue
        test.case = kase[1]
        const run = /^args = \[(.*)\]/m.exec(block)
        if (run) {
            test.args = [...run[1].matchAll(/"([^"]*)"/g)].map((m) => m[1].replace('${case}', test.case))
        }
        const exit = /^expect_exit = (-?\d+)/m.exec(block)
        if (exit) test.expectExit = Number(exit[1])
        const contains = /^expect_output_contains = \[(.*)\]/m.exec(block)
        test.contains = contains ? [...contains[1].matchAll(/"([^"]*)"/g)].map((m) => m[1]) : []
        tests.push(test)
    }
    return tests
}

// Every case file the plan tree names, mapped to the test that describes it, together with the plan
// file it came from (the provenance is printed for the cases whose expectation is not in the home
// directory, so a reader can check that the reading is the case's own). A case with several tests
// (one per backend) is represented by its `nvm` test when it has one -- that is the run this ledger
// makes; when it has none, by its first test, so a case whose only reading is a `macro` run is still
// measured the way its plan measures it.
const planFiles = []
function collectPlans(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) collectPlans(full)
        else if (entry.name.endsWith('.plan.toml')) planFiles.push(full)
    }
}
collectPlans(PLAN_ROOT)
const homePlans = planFiles.filter((file) => path.dirname(file) === HOME_PLAN_DIR)
const otherPlans = planFiles
    .filter((file) => path.dirname(file) !== HOME_PLAN_DIR)
    .sort()
// Read the home directory first, in readdir order (this keeps every pick it already made), then the
// rest by path. The scan is the whole tree, so a case declared by a plan anywhere is found; there is
// no directory list left for a new case to fall out of.
const orderedPlans = [...homePlans, ...otherPlans]

const expectations = new Map()
for (const file of orderedPlans) {
    for (const test of readTests(file)) {
        const target = path.resolve(path.dirname(file), test.case)
        const seen = expectations.get(target)
        if (!seen) expectations.set(target, { test, plan: file })
        else if (test.name.endsWith('.nvm') && !seen.test.name.endsWith('.nvm'))
            expectations.set(target, { test, plan: file })
    }
}

function passOf(test) {
    for (const word of test.args.slice(1)) {
        if (PASS_WORDS[word]) return PASS_WORDS[word]
        if (word.startsWith('std::')) return word
    }
    return 'std::nvm'
}

// --- the probe ------------------------------------------------------------------------------------
//
// The two-line import is the whole rewrite; a case that names none of the library's transforms (the
// six that only use helpers, the JAX ones) is run untouched, because there is nothing to migrate.
function rewriteImports(source) {
    const line = /^import\s*\{([^}]*)\}\s*from\s*autodiff\s*$/m.exec(source)
    if (!line) return { source, migrated: false }
    const names = line[1]
        .split(',')
        .map((n) => n.trim())
        .filter(Boolean)
    const mine = names.filter((n) => LIBRARY_NAMES.has(n))
    const theirs = names.filter((n) => !LIBRARY_NAMES.has(n))
    const replacement = []
    if (mine.length) replacement.push(`import { ${mine.join(', ')} } from vjp`)
    if (theirs.length) replacement.push(`import { ${theirs.join(', ')} } from autodiff`)
    return { source: source.replace(line[0], replacement.join('\n')), migrated: mine.length > 0 }
}

function runCase(file, test) {
    const source = fs.readFileSync(file, 'utf8')
    const { source: rewritten } = rewriteImports(source)
    const probe = file.replace(/\.cml$/, '.ledger-probe.cml')
    fs.writeFileSync(probe, rewritten, 'utf8')
    if (!fs.existsSync(camelBinary())) {
        loaderFailure = `no binary at ${camelBinary()}`
    }
    try {
        const result = spawnSync(camelBinary(), [probe, passOf(test)], {
            cwd: ROOT,
            encoding: 'utf8',
            timeout: 120000,
            env: { ...process.env, CAMEL_HOME: process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest') },
        })
        const text = `${result.stdout || ''}${result.stderr || ''}`
        const first = text.split('\n').find((l) => l.includes(': [Error]: ')) || ''
        // A binary that cannot start is not a case that failed. On the host the compiler needs the
        // newer `libstdc++` on `LD_LIBRARY_PATH`; without it every run exits 1 with a loader message
        // and no diagnostic, and the ledger would report a pass count that measures the shell, not the
        // tree. One such run marks the whole measure unusable.
        if (/error while loading shared libraries|GLIBCXX_|version `GLIBC/.test(text)) {
            loaderFailure =
                text.split('\n').find((l) => /error while loading shared libraries|GLIBCXX_/.test(l)) ||
                text.split('\n')[0]
        }
        return { exit: result.status === null ? -1 : result.status, first: first.trim(), text }
    } finally {
        fs.rmSync(probe, { force: true })
    }
}

// --- the run --------------------------------------------------------------------------------------
const files = []
function walk(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) walk(full)
        else if (entry.name.endsWith('.cml') && !entry.name.endsWith('.probe.cml')) files.push(full)
    }
}
walk(CASE_DIR)
files.sort()

const rows = []
for (const file of files) {
    const relative = relativeToRoot(file)
    const entry = expectations.get(file)
    const test = entry ? entry.test : undefined
    const planFile = entry ? relativeToRoot(entry.plan) : null
    const offPath = path.relative(CASE_DIR, file).split(path.sep)[0] === OFF_PATH_DIR
    if (onlyCase && !relative.includes(onlyCase)) continue
    const source = fs.readFileSync(file, 'utf8')
    const migrated = rewriteImports(source).migrated
    // A case with no `func main` is a library user, run through another case that imports it. Run
    // alone it reports `EntryModuleMissingMain`, which would be this tool's own noise reported as the
    // case's failure, so it is named for what it is.
    // A case that expects exit 1 is run whether or not it has a main: `err_vjp_annotation` fails at
    // the parse, so it has none, and its refusal is exactly what it guards.
    const standalone = /^func\s+main\s*\(/m.test(source) || (test ? test.expectExit !== 0 : false)
    const { exit, first, text } = standalone
        ? runCase(file, test || { args: ['${case}'], expectExit: 0, contains: [] })
        : { exit: null, first: 'no main: this case is run through another one, so it cannot be judged alone', text: '' }
    // `null`, not 0: a case no plan names has no expectation, and reading it against a zero written
    // here is how this ledger used to invent one -- `probe_library_grad` has a `main`, is named by no
    // plan, and used to pass on an exit code nobody had asked for.
    const expected = test ? test.expectExit : null
    // The exit code is the least a case can ask for. When its plan also names strings the run has to
    // print, a refusal that exits 1 for some other reason is not that case passing -- the same
    // distinction the plan itself draws.
    const missing = (test ? test.contains : []).filter((s) => !text.includes(s))
    const matched = Boolean(test) && exit !== null && exit === expected && missing.length === 0
    rows.push({
        relative,
        offPath,
        expected,
        exit,
        pass: matched,
        missing,
        migrated,
        first,
        pass_: test ? passOf(test) : 'std::nvm',
        planFile,
        planInHome: entry ? path.dirname(entry.plan) === HOME_PLAN_DIR : false,
        declared: Boolean(test),
    })
}

const library = rows.filter((r) => !r.offPath)
// `pass` is what the run did; it is a run of one binary and the record is a run of it.
const passed = library.filter((r) => r.pass).length
// The record's members: the case paths this run passes. `relative`, never a line number -- a line
// moves with every edit above it, and a record that renames its own evidence on a one-line insertion
// is a record people learn to ignore.
const passingNow = new Set(library.filter((r) => r.pass).map((r) => r.relative))
const rowByName = new Map(rows.map((row) => [row.relative, row]))

// A run of nothing is not a run where nothing passes, and a dead binary looks exactly like a clean
// refusal -- six cases that expect exit 1 and pin no output "pass" against a compiler that never
// started (measured 2026-11-02, a shell without LD_LIBRARY_PATH). So the detector is not the count:
// if no case produced an exit code, and if any run failed to start the binary, the instrument is not
// moving, and an instrument that is not moving does not report a number. `pass` is printed as `n/a`,
// the reason is said out loud, and the process exits non-zero so a gate cannot carry the number on.
const ran = rows.filter((r) => r.exit !== null && r.exit !== -1).length
const blind = ran === 0 || Boolean(loaderFailure)
const passCell = blind ? 'n/a' : String(passed)
if (blind) {
    console.error(
        'ledger: the instrument is not running, so `pass` is printed as `n/a` rather than a number. ' +
            (loaderFailure
                ? `The binary did not start: ${loaderFailure}. `
                : `Nothing ran at all: there is no built tree at ` +
                  `${relativeToRoot(path.dirname(camelBinary())) || 'out/latest'}. `) +
            'A run of nothing is not a run where nothing passes, and a binary that cannot start reads ' +
            'like a clean refusal. `total` and `library` are unaffected: they are counted from the tree.',
    )
    process.exitCode = 1
}

const summary = `[LEDGER] total=${rows.length} library=${library.length} pass=${passCell}`

if (!quiet) {
    const width = Math.max(...rows.map((r) => r.relative.length), 10)
    console.log(`${'case'.padEnd(width)}  exit  want  off  first diagnostic`)
    for (const row of rows) {
        const line = [
            row.relative.padEnd(width),
            (row.exit === null ? '-' : String(row.exit)).padStart(4),
            (row.expected === null ? 'none' : String(row.expected)).padStart(5),
            (row.offPath ? 'off' : '').padEnd(3),
            row.first.slice(0, 110),
        ].join('  ')
        console.log(line)
    }
    const off = rows.length - library.length
    console.log('')
    // Where each expectation came from, and what has none. A widened scan is only worth trusting if
    // it says, out loud, which cases it found somewhere other than the home directory and which it
    // found nowhere -- the second kind used to be read against an invented `expect_exit = 0` and
    // counted as passing, which is a number that answers nobody's question.
    const fromElsewhere = rows.filter((r) => r.declared && !r.planInHome)
    if (fromElsewhere.length > 0) {
        console.log(
            `${fromElsewhere.length} cases have their expectation outside ${relativeToRoot(HOME_PLAN_DIR)}:`,
        )
        for (const row of fromElsewhere) console.log(`  ${row.relative} -> ${row.planFile} want=${row.expected}`)
    }
    const undeclared = rows.filter((r) => !r.declared)
    if (undeclared.length > 0) {
        console.log(
            `${undeclared.length} cases are named by no [[tests]] table under ` +
                `${relativeToRoot(PLAN_ROOT)}: they are listed with \`want = none\`, ` +
                'they are **not** counted as passing, and a case with no `func main` among them is run ' +
                'through another case rather than alone:',
        )
        for (const row of undeclared)
            console.log(`  ${row.relative}  exit=${row.exit === null ? '-' : row.exit}`)
    }
    console.log(
        `${library.length} cases name the library's transforms; ${off} more live under ` +
            `${OFF_PATH_DIR}/ or name nothing the library owns and are counted in total only.`,
    )
    // The nine under `jax_names/` are not skipped, and this says so where a reader can see the
    // count: they are walked like the rest, they print a row, and they are left out of `pass` because
    // they exercise the prelude's `vmap`, not this library's `vjp` -- counting them as "not passing on
    // the library" would be counting the wrong thing.
    const offPathRows = rows.filter((r) => r.offPath)
    if (offPathRows.length > 0) {
        console.log(
            `Of the ${off}, ${offPathRows.length} live under ${OFF_PATH_DIR}/ and are run and listed, ` +
                'but they are off this lane: they test the prelude, so they count in total only.',
        )
    }
    const noNames = library.filter((r) => !r.migrated)
    console.log(
        'A case passes when its exit code is the one its own plan expects and it prints every string ' +
            `the plan names; ${noNames.length} of the ${library.length} name none of the library's ` +
            'transforms, so they pass without it.',
    )
    console.log(
        `pass is a run of ${relativeToRoot(camelBinary())} (${binaryStamp()}): it moves with that ` +
            'binary and is not comparable across machines. The record is a run too, which is why it ' +
            'names the tree and the binary it came from.',
    )
}
console.log(summary)

// --- the record -----------------------------------------------------------------------------------
//
// True when the file on disk was written by the counted form of this check (six numbers, one of them
// a classification). A number is not a case, so it is not read as a list -- reading it as one would
// report every case in the tree as having entered -- and `--record` replaces it while the gate
// refuses to compare against it until somebody does.
let countedForm = false

function readRecord() {
    if (!fs.existsSync(RECORD_PATH)) return null
    let document
    try {
        document = JSON.parse(fs.readFileSync(RECORD_PATH, 'utf8'))
    } catch {
        countedForm = true
        return null
    }
    if (!Array.isArray(document.passing)) {
        countedForm = true
        return null
    }
    return document
}

function describeFall(name) {
    const row = rowByName.get(name)
    if (!row) return `  ${name}  is not in the tree any more`
    const want = row.expected === null ? 'none' : row.expected
    const got = row.exit === null ? '-' : row.exit
    return `  ${name}  want=${want}  got=${got}  ${row.first.slice(0, 110)}`
}

if (record) {
    if (blind) {
        console.error(
            'ledger: not recording: the instrument is not running (see above). A record written from a ' +
                'run of nothing would be a floor of zero, and every later reading would be compared ' +
                'against it.',
        )
        process.exit(1)
    }
    const previous = readRecord()
    const recorded = previous ? new Set(previous.passing) : new Set()
    const fell = [...recorded].filter((name) => !passingNow.has(name)).sort()
    const rose = [...passingNow].filter((name) => !recorded.has(name)).sort()
    if (countedForm) {
        console.log(
            `[LEDGER] the record on disk is the counted form of this check (numbers, not case paths); ` +
                `replacing it with the ${passingNow.size} case(s) this run passes`,
        )
    }
    if (rose.length > 0) {
        console.log('[LEDGER] cases this run passes that the record did not name:')
        for (const name of rose) console.log(`  ${name}`)
    }
    if (fell.length > 0 && !force) {
        console.log('[LEDGER] refusing to record these cases as no longer passing:')
        for (const name of fell) console.log(describeFall(name))
        console.log(
            '[LEDGER] a fall is the defect this record exists to find; --force records it anyway, so ' +
                'the diff of the record shows it for a reviewer to refuse, and the commit says why',
        )
        process.exit(2)
    }
    if (fell.length > 0) {
        console.log('[LEDGER] --force: recording cases that stopped passing; the diff says so')
        for (const name of fell) console.log(describeFall(name))
    }
    const document = {
        comment:
            'The baseline of the autodiff migration ledger: the case paths that passed, as the run that ' +
            'recorded it read them. `--ledger-check` compares names, and one direction is red -- a case ' +
            'listed here that no longer passes. A case that starts passing is progress and is put here ' +
            'by `--record`, which refuses to drop a name without `--force`, because a fall is the defect ' +
            'this list exists to find and the diff has to show it. There is no count here: a count says ' +
            'how many fell, a list says which one. `recordedFrom` says which tree and binary the run was ' +
            'and is never compared. Record it with node ' +
            'scripts/autodiff-migration-ledger.mjs --record.',
        recordedFrom: `tree ${treeState()} / ${relativeToRoot(camelBinary())} built ${binaryStamp()}`,
        passing: [...passingNow].sort(),
    }
    fs.mkdirSync(path.dirname(RECORD_PATH), { recursive: true })
    // LF and a trailing newline: this file is read on the host and on this machine, and a change to it
    // should diff as one line per case that entered or left.
    fs.writeFileSync(RECORD_PATH, `${JSON.stringify(document, null, 2)}\n`, 'utf8')
    console.log(`[LEDGER] recorded ${document.passing.length} passing case(s) in ${relativeToRoot(RECORD_PATH)}`)
    console.log('[LEDGER] ok: the record is this run')
    process.exit(0)
}

if (ledgerCheck) {
    if (blind) {
        console.log('')
        console.log(
            '[LEDGER-CHECK] not run: the instrument is not running (see above), so this run is not ' +
                'compared with the record -- a comparison against a record of nothing would send a ' +
                'reader hunting for a fall that happened because nothing started.',
        )
        process.exit(1)
    }
    const previous = readRecord()
    if (!previous) {
        console.error(
            countedForm
                ? `ledger: ${relativeToRoot(RECORD_PATH)} is the counted form of this check (numbers, ` +
                      'not case paths): re-record it with `node scripts/autodiff-migration-ledger.mjs ' +
                      '--record`, which writes the case paths'
                : `ledger: no record at ${relativeToRoot(RECORD_PATH)}; record one with ` +
                      '`node scripts/autodiff-migration-ledger.mjs --record`',
        )
        process.exit(2)
    }
    const recorded = new Set(previous.passing)
    const fell = [...recorded].filter((name) => !passingNow.has(name)).sort()
    const rose = [...passingNow].filter((name) => !recorded.has(name)).sort()
    if (fell.length > 0) {
        console.log('')
        console.log('[LEDGER-CHECK] these cases passed at the last record and do not pass now:')
        for (const name of fell) console.log(describeFall(name))
        console.log(
            `[LEDGER-CHECK] a fall is the one direction this record has: fix the case, or record the ` +
                `fall with --record --force in a commit that says why it stopped passing. ` +
                `${fell.length} of the ${recorded.size} recorded case(s) fell.`,
        )
        process.exitCode = 1
    } else {
        console.log('')
        console.log('[LEDGER-CHECK] ok: every case the record names still passes')
        if (rose.length > 0) {
            console.log(
                `[LEDGER-CHECK] ${rose.length} case(s) pass that the record does not name yet, so a fall ` +
                    'among them would not be seen; --record raises the floor to this run:',
            )
            for (const name of rose) console.log(`  ${name}`)
        }
    }
}

// The tree the record is taken on, for `recordedFrom` only: a reading with no tree named cannot be
// quoted, and a record is a reading. The vocabulary is the build stamp's (`scripts/common.js#gitState`:
// revision, then whether the tree was clean, and how many files it was not), for the same reason --
// `revision` alone names a commit whose files are not the ones the run read. A tree that cannot be
// read is named as one, never guessed.
function treeState() {
    const head = spawnSync('git', ['rev-parse', 'HEAD'], { cwd: ROOT, encoding: 'utf8' })
    const revision = head.status === 0 ? (head.stdout || '').trim().slice(0, 9) : ''
    if (revision === '') return 'unknown'
    const status = spawnSync('git', ['status', '--porcelain'], { cwd: ROOT, encoding: 'utf8' })
    if (status.status !== 0) return `${revision} (no status)`
    const dirty = (status.stdout || '').split(/\r?\n/).filter((line) => line !== '').length
    return dirty > 0 ? `${revision} (dirty: ${dirty} file(s))` : `${revision} (clean)`
}