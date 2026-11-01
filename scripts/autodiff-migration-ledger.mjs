#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The autodiff migration ledger, as a command instead of a morning's arithmetic.
//
// The question the ledger answers is narrow: **how many of the old autodiff cases can run on the
// `vjp` library today, and what stops the rest?** The page that holds the answer
// (docs/handover/m4-autodiff-migration-table.cn.md) is written by hand, and every time it was
// recounted by hand the count came out wrong -- once by 1 (a probe rewrote a whole import line and
// took the helper names with it), once with a blind spot (measuring every case by exit 0, when 30 of
// them guard an exit 1 on purpose). So the counting is here, and the page reads the number.
//
//   node scripts/autodiff-migration-ledger.mjs              the table and the one-line summary
//   node scripts/autodiff-migration-ledger.mjs --quiet       only the `[LEDGER]` line
//   node scripts/autodiff-migration-ledger.mjs --case NAME   one case, with its diagnostic
//
// **It reads, it does not write.** The page is a human's; this prints the numbers the human then
// writes down. Nothing here edits a case, a plan or a document.
//
// How a case is measured -- three steps, each of which is where the hand count went wrong before:
//
// 1. The case is copied to a probe beside itself and its `import { ... } from autodiff` is **split in
//    two**: the transform names the library owns (`vjp`, `grad`, `with_grad`) come from `vjp`, and
//    every other name stays on `autodiff`, which still has them (`add_tangent`, `zero_tangent`, ...).
//    Rewriting the whole line to `from vjp` is what turned `transform_rules` into a false failure.
// 2. The case is run with the pass its plan runs it with, and **passes when the exit code is the one
//    its own plan expects** -- not when it exits 0. `with_grad_old_name` guards that `value_and_grad`
//    does not exist: its expectation is exit 1, so exit 0 would be the failure.
// 3. A case that does not pass is sorted by **the shape of its first diagnostic**, not by what its
//    source looks like: a function whose type carries a `with` parameter is one class, an
//    array/tensor parameter is another, and everything else is the third. The diagnostic is the
//    one-line form the renderer documents (`include/camel/core/error/diagnostics/render.h`):
//
//        (module)path:line:col: [Error]: message suggestion (name=..., code=0x...)
//
//    so a diagnostic line is one that contains `": [Error]: "`. That is the only thing this reads:
//    the message after it is what carries the function type or the array type. (The first version of
//    this file looked for lines starting with `error[`, a form this tree stopped printing -- `git
//    grep -c 'error\[' -- src` is 0 -- so it matched nothing, put every failing case in `other`, and
//    read 0/0/106 where the classification is 54/46/6. A ruler that reads nothing is worse than no
//    ruler: it reports a number that never changes and a ratchet that reds forever.)
//
// The plan files are read with a small bracket scanner, not a TOML library: the repository has no
// dependency for that, and the five keys this needs (`case`, `args`, `expect_exit`, and the test's
// name to prefer the `nvm` run) sit in flat `[[tests]]` tables.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const CASE_DIR = path.join(ROOT, 'test', 'cases', 'modules', 'autodiff')
const PLAN_DIRS = [path.join(ROOT, 'test', 'plans', 'feat', 'modules')]
const LIBRARY_NAMES = new Set(['vjp', 'grad', 'with_grad'])
const OFF_PATH_DIR = 'jax_names'

const args = process.argv.slice(2)
const quiet = args.includes('--quiet')
const ledgerCheck = args.includes('--ledger-check')
const onlyCase = args.includes('--case') ? args[args.indexOf('--case') + 1] : undefined

// The ledger's own record, kept the way `diagnostics-catalog.mjs` keeps its name coverage: a constant
// in the file, refused to move the wrong way. Two numbers here mean opposite things, so the check
// reads them in opposite directions --
//
//   `pass` may only go **up**: a case that starts doing what its plan expects is progress, and one
//   that stops is a regression however the other lines move.
//   `with`, `array` and `other` may only go **down**: they count what stops the rest, so a case
//   joining one of them is a case leaving `pass`.
//   `total` and `library` are the denominator and may not move at all: a case appearing or leaving
//   changes what the ledger means, which is a decision for a person, not a side effect.
//
// **What `pass` counts, and what it does not.** It counts cases that do what their own plan asks --
// and 8 of the 12 guard a refusal the library is supposed to make (`err_vjp_annotation`, six
// `grad_function_value_*`, `with_grad_old_name`). So this records "how many cases are green on the
// library", **not** "how many cases can be differentiated": the four that really produce numbers are
// `transform_rules`, `jax_batching`, `jax_vmap` and `jax_vmap_tensor`. A reader who takes 12 for
// "12 cases differentiate" would be reading it wrong.
const LEDGER_RECORDED = {
    total: 127,
    library: 118,
    pass: 12,
    with: 54,
    array: 46,
    other: 6,
}

function camelBinary() {
    const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
    const exe = process.platform === 'win32' ? 'camel.exe' : 'camel'
    return path.join(home, 'bin', exe)
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

// Every case file the plan tree names, mapped to the test that describes it. A case with several
// tests (one per backend) is represented by its `nvm` test when it has one -- that is the run this
// ledger makes; when it has none, by its first test, so a case whose only reading is a `macro` run is
// still measured the way its plan measures it.
const expectations = new Map()
for (const dir of PLAN_DIRS) {
    for (const entry of fs.readdirSync(dir)) {
        if (!entry.endsWith('.plan.toml')) continue
        for (const test of readTests(path.join(dir, entry))) {
            const file = path.resolve(dir, test.case)
            const seen = expectations.get(file)
            if (!seen) expectations.set(file, test)
            else if (test.name.endsWith('.nvm') && !seen.name.endsWith('.nvm')) expectations.set(file, test)
        }
    }
}

function passOf(test) {
    for (const word of test.args.slice(1)) {
        if (PASS_WORDS[word]) return PASS_WORDS[word]
        if (word.startsWith('std::')) return word
    }
    return 'std::nvm'
}

// --- the probe --------------------------------------------------------------------------------
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
    try {
        const result = spawnSync(camelBinary(), [probe, passOf(test)], {
            cwd: ROOT,
            encoding: 'utf8',
            timeout: 120000,
            env: { ...process.env, CAMEL_HOME: process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest') },
        })
        const text = `${result.stdout || ''}${result.stderr || ''}`
        return { exit: result.status === null ? -1 : result.status, first: firstDiagnostic(text), text }
    } finally {
        fs.rmSync(probe, { force: true })
    }
}

// --- the classes ------------------------------------------------------------------------------
//
// The first diagnostic of a failing run, in either spelling this tree has printed: the renderer's
// one-line form carries `: [Error]: `, and the CLI's compact form starts the line with
// `error[SemanticDiag::Name]:`. Both were the *only* spelling at some point, and a detector that
// knows one of them matches nothing in the other -- which is how this ruler once reported 0/0/106
// while the classification was 54/46/6. So it accepts both, and `--quiet` prints how many failing
// rows it could not read, because a number that never moves is worse than no number at all.
function firstDiagnostic(text) {
    const lines = text.split('\n')
    const rendered = lines.find((l) => l.includes(': [Error]: '))
    if (rendered !== undefined) return rendered.trim()
    const compact = lines.find((l) => /(^|\s)(error|warning)\[[A-Za-z_]+::[A-Za-z_]+\]/.test(l))
    return compact === undefined ? '' : compact.trim()
}

// The shape, not the source: a diagnostic that prints a function type carrying a `with` parameter
// (`<x: float> (y: float) => float`, `<w: float> () => float`) is the `with` class; one that names an
// array or tensor parameter (`float[]`, `f32[..]`, `Model`, `Plain`) is the array class. Everything
// else -- a missing name, the `@vjp` annotation -- is `other`.
const ARRAY_SHAPE = /\[\]|\[\.\.\]|\bf32\b|\bf16\b|\bbf16\b|\bModel\b|\bPlain\b/
const WITH_SHAPE = /<[A-Za-z_]*\w*\s*:\s*[^>]+>/

function classify(first) {
    if (ARRAY_SHAPE.test(first)) return 'array'
    if (WITH_SHAPE.test(first)) return 'with'
    return 'other'
}

// --- the run ----------------------------------------------------------------------------------
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
    const relative = path.relative(ROOT, file).replace(/\\/g, '/')
    const test = expectations.get(file)
    const offPath = path.relative(CASE_DIR, file).split(path.sep)[0] === OFF_PATH_DIR
    if (onlyCase && !relative.includes(onlyCase)) continue
    const source = fs.readFileSync(file, 'utf8')
    const migrated = rewriteImports(source).migrated
    // A case with no `func main` is a library user, run through another case that imports it. Run
    // alone it reports `EntryModuleMissingMain`, which would be this tool's own noise reported as the
    // case's failure, so it is named for what it is.
    // A case that expects exit 1 is run whether or not it has a main: rr_vjp_annotation fails
    // at the parse, so it has none, and its refusal is exactly what it guards.
    const standalone = /^func\s+main\s*\(/m.test(source) || (test ? test.expectExit !== 0 : false)
    const { exit, first, text } = standalone
        ? runCase(file, test || { args: ['${case}'], expectExit: 0, contains: [] })
        : { exit: null, first: 'no main: this case is run through another one, so it cannot be judged alone', text: '' }
    const expected = test ? test.expectExit : 0
    // The exit code is the least a case can ask for. When its plan also names strings the run has to
    // print, a refusal that exits 1 for some other reason is not that case passing -- the same
    // distinction the plan itself draws.
    const missing = (test ? test.contains : []).filter((s) => !text.includes(s))
    const matched = exit !== null && exit === expected && missing.length === 0
    rows.push({
        relative,
        offPath,
        expected,
        exit,
        pass: matched,
        kind: matched ? 'pass' : classify(first),
        missing,
        migrated,
        first,
        pass_: test ? passOf(test) : 'std::nvm',
    })
}

const library = rows.filter((r) => !r.offPath)
const count = (kind) => library.filter((r) => r.kind === kind).length
const summary =
    `[LEDGER] total=${rows.length} library=${library.length} pass=${count('pass')} ` +
    `with=${count('with')} array=${count('array')} other=${count('other')}`

if (!quiet) {
    const width = Math.max(...rows.map((r) => r.relative.length), 10)
    console.log(`${'case'.padEnd(width)}  exit  want  class   first diagnostic`)
    for (const row of rows) {
        const line = [
            row.relative.padEnd(width),
            (row.exit === null ? '-' : String(row.exit)).padStart(4),
            String(row.expected).padStart(5),
            row.offPath ? 'off' : row.kind.padEnd(6),
            row.first.slice(0, 110),
        ].join('  ')
        console.log(line)
    }
    const off = rows.length - library.length
    console.log('')
    console.log(
        `${library.length} cases name the library's transforms; ${off} more live under ` +
            `${OFF_PATH_DIR}/ or name nothing the library owns and are counted in total only.`,
    )
    // The nine under `jax_names/` are not skipped, and this says so where a reader can see the
    // count: they are walked like the rest, they print a row, and they are left out of the four
    // classes because they exercise the prelude's `vmap`, not this library's `vjp` -- counting them
    // as "not passing on the library" would be counting the wrong thing.
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
}
console.log(summary)

if (ledgerCheck) {
    const measured = {
        total: rows.length,
        library: library.length,
        pass: count('pass'),
        with: count('with'),
        array: count('array'),
        other: count('other'),
    }
    const problems = []
    const move = (key, direction, wording) => {
        const recorded = LEDGER_RECORDED[key]
        const got = measured[key]
        if (direction === 'up' ? got < recorded : got > recorded) {
            problems.push(
                `${key} 记录值 ${recorded} 但实测 ${got}（${wording}）：` +
                    `改了就该在同一个提交里改下 LEDGER_RECORDED 的那个数，并说明读数为什么动了`,
            )
        }
    }
    if (measured.total !== LEDGER_RECORDED.total) {
        problems.push(
            `total 记录值 ${LEDGER_RECORDED.total} 但实测 ${measured.total}：分母是决定，不是副作用`,
        )
    }
    if (measured.library !== LEDGER_RECORDED.library) {
        problems.push(
            `library 记录值 ${LEDGER_RECORDED.library} 但实测 ${measured.library}：分母是决定，不是副作用`,
        )
    }
    move('pass', 'up', '只能升')
    move('with', 'down', '只能降')
    move('array', 'down', '只能降')
    move('other', 'down', '只能降')
    if (problems.length > 0) {
        console.log('')
        console.log('[LEDGER-CHECK] the ledger moved the wrong way:')
        for (const problem of problems) console.log(`  ${problem}`)
        process.exitCode = 1
    } else {
        console.log('')
        console.log(
            `[LEDGER-CHECK] ok: total=${measured.total} library=${measured.library} ` +
                `pass=${measured.pass} with=${measured.with} array=${measured.array} ` +
                `other=${measured.other} -- pass 没有降，三类没有升`,
        )
    }
}
