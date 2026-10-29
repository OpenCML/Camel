#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The conformance gate: every program in `test/conformance/cases.json` has one recorded result --
// what the specification says it prints and returns -- and this script compares the implementation
// with that reading on both sides of the optimizer.
//
// Why it exists: the fast layer proves each plan's cases on the pipeline that plan names, one plan
// at a time. It does not prove that one program's result survives optimization, and no single
// place holds the reading of a program that the optimizer is compared *against*. That is the
// comparison a conformance study makes, and it is what the three columns below state:
//
//   before optimization            the unoptimized implementation matches the recorded reading;
//   across optimization            the reading recorded before optimization is the same as the
//                                  one recorded after it, and every back end's optimized result
//                                  equals its unoptimized result;
//   after optimization             the optimized implementation matches the recorded reading.
//
// A reading is derived from the principle named beside the case -- never copied from what the
// implementation happened to print, or the gate would only prove that it still does what it did.
// The readings, the columns, what each one covers and what it does not are in
// `test/conformance/README.md`.
//
// What a green run does not prove is printed on every run, one line per item: the list is the
// sections of README.md, "What this gate does not cover". A reader of the log has to be able to
// tell the gate's own gaps from its findings without opening the page.
//
// Usage:
//
//   node scripts/check-conformance.mjs                 # this tree's out/latest
//   node scripts/check-conformance.mjs --camel out/debug/bin/camel
//   node scripts/check-conformance.mjs --case order    # cases whose name contains "order"
//
// `--camel <path>` names the binary a run measures, the same parameter and the same stamp rule as
// `scripts/stdlib-reference.mjs`: an install that is not this tree's build is refused (exit 2)
// rather than compared. Exit 0 is every column green; 1 is at least one column red; 2 is a run
// that could not start (no install, an unknown argument).

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { applyNormalizers } from '../test/tools/normalizers.mjs'
import { takeCamel } from '../test/tools/camel-arg.mjs'
import { refuseInstallNotOfThisTree } from './install-stamp.mjs'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const MANIFEST = path.join(ROOT, 'test', 'conformance', 'cases.json')
const NORMALIZERS = ['strip_ansi', 'normalize_newlines', 'trim_trailing_space']

// The three columns, in the order a run reads them. `name` is what a failure prints: the reader of
// a red run has to know which comparison failed without opening this script.
const COLUMNS = [
    {
        key: 'before',
        name: 'before optimization',
        statement: 'the unoptimized implementation matches the recorded reading',
    },
    {
        key: 'across',
        name: 'across optimization',
        statement: 'the reading is the same before and after optimization, and every back end gives the same result on both pipelines',
    },
    {
        key: 'after',
        name: 'after optimization',
        statement: 'the optimized implementation matches the recorded reading',
    },
]

// The seven limits of a green run, one line each, in the order README.md gives them. They are
// printed before the columns so that the reading a log records is never read as more than it is:
// the first is the one that matters most -- a reading stands in for the model's run, so the gate
// proves the implementation matches the record, not that the record matches the model.
const NOT_COVERED = [
    "the model is not re-executed: a reading stands in for its run, so this proves the implementation matches the record, not that the record matches the model",
    "the study's corpus is not here: only the cases in cases.json run, so a defect they would expose is unknown to this gate",
    'programs that must be refused are not cases: parse errors, type errors, refused declarations, faults and error values keep their own plans',
    'non-determinism is not asserted: an order the specification leaves open cannot be written as a reading',
    'graph shape is not compared: where an order edge sits is the linear plans\u2019 snapshots to own',
    'coverage is stdout and the exit code only: fs, the clock, the environment, user-declared resources and handle types are not exercised',
    'device and export paths do not run: GPU execution, ONNX export, the Python SDK and the C++ translation have their own plans',
]

function usage(message) {
    if (message) console.error(`[conformance] ${message}`)
    console.error('usage: node scripts/check-conformance.mjs [--camel <path>] [--case <substring>]')
    process.exit(2)
}

// The install this run measures: `--camel <path>`, else this tree's out/latest. The choice is the
// one `scripts/release-gate.mjs` hands to a check that reads the install, so this script can run
// alone or as that check with no second rule.
let camel = null
let caseFilter = null
const argv = process.argv.slice(2)
const taken = takeCamel(argv, ROOT, usage)
camel = taken.camel
for (let i = 0; i < taken.rest.length; i++) {
    const arg = taken.rest[i]
    if (arg === '--case') {
        if (i + 1 >= taken.rest.length) usage('--case needs a substring of a case name')
        caseFilter = taken.rest[++i]
    } else if (arg.startsWith('--case=')) {
        caseFilter = arg.slice('--case='.length)
    } else if (arg === '-h' || arg === '--help') {
        usage()
    } else {
        usage(`unknown argument '${arg}'`)
    }
}

if (!fs.existsSync(camel)) usage(`no camel binary at ${camel}: --camel names the install a run measures`)
refuseInstallNotOfThisTree(camel, ROOT, 'check-conformance.mjs')

const manifest = JSON.parse(fs.readFileSync(MANIFEST, 'utf8'))
const backends = manifest.backends
const optimized = manifest.optimized
const cases = manifest.cases.filter((entry) => !caseFilter || entry.name.includes(caseFilter))
if (cases.length === 0) usage(`no case matches '${caseFilter}'`)

// One run of one program on one pipeline. `passes` is the whole pass list, spelled out: a back end
// named on its own is the unoptimized pipeline, and `std::opt` in front of it is the optimized one.
// `CAMEL_HOME` is the install this script measures, never whatever the shell exports.
function run(program, passes) {
    const install = path.dirname(path.dirname(camel))
    const proc = spawnSync(camel, [program, ...passes], {
        cwd: ROOT,
        encoding: 'utf8',
        timeout: 60000,
        env: { ...process.env, CAMEL_HOME: install },
    })
    if (proc.error) {
        return { code: null, stdout: '', stderr: '', failed: proc.error.message }
    }
    return {
        code: proc.status,
        stdout: applyNormalizers(proc.stdout || '', NORMALIZERS),
        stderr: applyNormalizers(proc.stderr || '', NORMALIZERS),
    }
}

function json(text) {
    return JSON.stringify(text)
}

// A failure line carries both results, but a result that is a printed graph (a pass that does not
// run the program, which is itself the defect) would bury the line: the first part and the length
// name it without drowning the reading.
function clip(text, limit = 160) {
    return text.length <= limit ? text : `${text.slice(0, limit)}... (${text.length} chars)`
}

// What a run produced, in the words a failure reports it with.
function actual(result) {
    if (result.failed) return `did not start (${result.failed})`
    return `exit ${result.code}, stdout ${json(clip(result.stdout))}`
}

function expected(text, code) {
    return `exit ${code}, stdout ${json(text)}`
}

const failures = new Map(COLUMNS.map((column) => [column.key, []]))
// The cases each column failed on, distinct: four back ends failing on one case is one case.
const failedCases = new Map(COLUMNS.map((column) => [column.key, new Set()]))
let runs = 0

function fail(column, caseName, line) {
    failures.get(column).push(line)
    failedCases.get(column).add(caseName)
}

for (const entry of cases) {
    const program = path.join(ROOT, entry.case)
    const reading = entry.reading
    const readingAfter = entry.reading_after === undefined ? reading : entry.reading_after

    // The two halves of the cross-optimization column that do not depend on a back end: the two
    // recorded readings, and then the same reading on both sides.
    if (reading !== readingAfter) {
        fail(
            'across',
            entry.name,
            `case '${entry.name}': the reading recorded before optimization ${json(reading)} differs from the one recorded after it ${json(readingAfter)} (${entry.principle})`,
        )
    }

    for (const backend of backends) {
        const unoptimized = run(program, [`std::${backend}`])
        // A case may name its own optimized pipeline (a study case that belongs to one pass); the
        // manifest's `optimized` is the default for the cases that do not.
        const optimizedRun = run(program, [...(entry.optimized || optimized), `std::${backend}`])
        runs += 2

        if (unoptimized.failed) {
            fail('before', entry.name, `case '${entry.name}', ${backend}: ${actual(unoptimized)}; ${entry.principle}`)
        } else if (unoptimized.code !== entry.exit || unoptimized.stdout !== reading) {
            fail(
                'before',
                entry.name,
                `case '${entry.name}', ${backend}: expected ${expected(reading, entry.exit)}, actual ${actual(unoptimized)} (${entry.principle})`,
            )
        }

        if (optimizedRun.failed || unoptimized.failed) {
            // Reported once, under the column whose run did not start.
            if (optimizedRun.failed) {
                fail('after', entry.name, `case '${entry.name}', ${backend}: ${actual(optimizedRun)}`)
            }
            continue
        }

        if (optimizedRun.code !== unoptimized.code || optimizedRun.stdout !== unoptimized.stdout) {
            fail(
                'across',
                entry.name,
                `case '${entry.name}', ${backend}: optimization changed the result: unoptimized ${actual(unoptimized)}, optimized ${actual(optimizedRun)} (${entry.principle})`,
            )
        }

        if (optimizedRun.code !== entry.exit || optimizedRun.stdout !== readingAfter) {
            fail(
                'after',
                entry.name,
                `case '${entry.name}', ${backend}: expected ${expected(readingAfter, entry.exit)}, actual ${actual(optimizedRun)} (${entry.principle})`,
            )
        }
    }
}

// The run's own report: what it measured, then the seven limits of a green run -- one line each,
// before any column, so a log cannot record a green reading as more than it is -- then one line per
// column with the count it measured, then every failure under the column that owns it.
console.log(
    `[conformance] ${cases.length} case(s) from test/conformance/cases.json; back ends ${backends.join(', ')}; ` +
        `optimized pipeline ${optimized.join(' ')}; ${runs} program run(s)`,
)
console.log(
    `[conformance] a reading is the result the named principle gives the program; what a green run does not ` +
        `prove, ${NOT_COVERED.length} items (README.md, "What this gate does not cover"):`,
)
for (const item of NOT_COVERED) console.log(`[conformance]   not covered -- ${item}`)

let failedColumns = 0
for (const column of COLUMNS) {
    const columnFailures = failures.get(column.key)
    if (columnFailures.length === 0) {
        console.log(`[conformance] ${column.name}: ${cases.length}/${cases.length} cases -- ${column.statement}`)
        continue
    }
    failedColumns++
    const failed = failedCases.get(column.key).size
    console.log(
        `[conformance] FAIL ${column.name}: ${failed} of ${cases.length} case(s) -- ${column.statement}`,
    )
    for (const line of columnFailures) console.log(`[conformance]   ${line}`)
}

if (failedColumns > 0) {
    console.log(`[conformance] ${failedColumns} of ${COLUMNS.length} columns red`)
    process.exit(1)
}
console.log(`[conformance] ${COLUMNS.length}/${COLUMNS.length} columns green`)