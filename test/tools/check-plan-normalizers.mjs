#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A Python child writes its text in the platform's line endings: `print` in text mode ends a line
 * with `\r\n` on Windows and `\n` elsewhere. A plan that compares such a child's output byte for
 * byte is therefore red on Windows unless it normalises newlines first, and it is red *before* the
 * value the test exists to pin is ever compared -- the red says nothing about the program, which is
 * what `f8a2da8f4` had to work out for the CUDA-visibility plan.
 *
 * This guard reads every plan and reports the entries that have both halves: a child that is
 * Python, and a comparison that reaches a line end. A Camel run and a Node fixture write `\n` on
 * every platform (`terminal_read_write_roundtrip`'s failure on the Windows leg read `false\n\n`,
 * not `false\r\n\r\n`), so a plan that runs either of those is not reported. A `*_contains` needle
 * that has no line end in it cannot see the difference either, and is not reported; an `*_exact`
 * comparison always can.
 *
 * The comparison is the one the runner makes with the fields it makes it with: the entry's own
 * `normalizers` if it has any, otherwise `[defaults] normalizers`, because a `[[tests]]` entry
 * replaces a default key rather than adding to it (run-tests.mjs merges `{ ...defaults, ...test }`).
 *
 * What this guard cannot see, and does not claim to: a Python child whose text comes back through a
 * Node fixture that normalises it itself (correct, and not visible here); a `[[sweeps]]`
 * expansion; a comparison a `verify_script` makes by hand; and a comparison of Python's *files*
 * rather than its output.
 *
 *   node test/tools/check-plan-normalizers.mjs [--root <checkout>]
 *   node test/tools/check-plan-normalizers.mjs --selftest
 *
 * `--selftest` runs the same guard on the two fixture trees under
 * test/tools/selftest/plan-normalizers/ (one that breaks the rule, one repaired) and checks that it
 * goes red on the first and green on the second: the red this guard exists to give, before it is
 * trusted on the tree.
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

import { parseTomlFile } from './toml-lite.mjs'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..')
const SELFTEST_ROOT = path.join(HERE, 'selftest', 'plan-normalizers')

// The plans the runner runs: `test/plans` is the tree, and the self-test plans under
// `test/tools/selftest` are plan files too -- the red this guard exists for was in one of them.
const PLAN_ROOTS = ['test/plans', path.join('test', 'tools', 'selftest')]

// The fields that compare the child's text, and how far the comparison reaches. An `*_exact` field
// compares the whole text, so a `\r` in it is a difference wherever it sits. A `*_contains` field
// reaches only as far as the text it names: a needle with no line end in it cannot see the
// difference, and one with a line end in it can. A `*_not_contains` field is left out altogether: a
// `\r` that a normaliser would have removed cannot make a forbidden string appear.
const COMPARES_ALL_TEXT = ['expect_stdout_exact', 'expect_stderr_exact', 'expect_output_exact']
const COMPARES_NAMED_TEXT = ['expect_stdout_contains', 'expect_stderr_contains', 'expect_output_contains']

function* planFiles(dir, skipNestedTestTrees = false) {
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
            // A fixture that imitates a checkout (`<fixture>/test/plans/...`) holds plans written as
            // an input to a self-test, not plans the runner runs: `plan-normalizers/bad` is one, and
            // reading it here would report the guard's own red example.
            if (skipNestedTestTrees && entry.name === 'test') {
                continue
            }
            yield* planFiles(full, skipNestedTestTrees)
        } else if (entry.name.endsWith('.plan.toml')) {
            yield full
        }
    }
}

// A child that is Python: named as the tool, or reached through the file it runs (the python
// runner `test/tools/run.py`, or a `*.py` case).
function runsPython(test) {
    if (test.tool === 'python') {
        return true
    }
    const words = []
    if (typeof test.case === 'string') {
        words.push(test.case)
    }
    if (Array.isArray(test.args)) {
        for (const arg of test.args) {
            if (typeof arg === 'string') {
                words.push(arg)
            }
        }
    }
    return words.some((word) => /\.py(?:[#:].*)?$/.test(word))
}

function normalisersOf(test, defaults) {
    if (Array.isArray(test.normalizers)) {
        return test.normalizers
    }
    if (defaults !== null && typeof defaults === 'object' && Array.isArray(defaults.normalizers)) {
        return defaults.normalizers
    }
    return []
}

// Which of the entry's fields can see a `\r` the normaliser did not remove.
function comparisonsOf(test) {
    const fields = COMPARES_ALL_TEXT.filter((field) => test[field] !== undefined)
    for (const field of COMPARES_NAMED_TEXT) {
        const value = test[field]
        if (value === undefined) {
            continue
        }
        const needles = Array.isArray(value) ? value : [value]
        if (needles.some((needle) => typeof needle === 'string' && needle.includes('\n'))) {
            fields.push(field)
        }
    }
    return fields
}

function scan(rootDir) {
    const problems = []
    let plansRead = 0
    let pythonEntries = 0
    let comparingEntries = 0
    for (const planRoot of PLAN_ROOTS) {
        const isSelftestRoot = planRoot !== PLAN_ROOTS[0]
        for (const file of planFiles(path.join(rootDir, planRoot), isSelftestRoot)) {
            let doc
            try {
                doc = parseTomlFile(file)
            } catch {
                // A fixture that exists to be rejected by the schema is not a plan the runner runs.
                continue
            }
            ++plansRead
            for (const test of Array.isArray(doc.tests) ? doc.tests : []) {
                if (!runsPython(test)) {
                    continue
                }
                ++pythonEntries
                const compared = comparisonsOf(test)
                if (compared.length === 0) {
                    continue
                }
                ++comparingEntries
                if (!normalisersOf(test, doc.defaults).includes('normalize_newlines')) {
                    problems.push(
                        `${path.relative(rootDir, file)}: ${test.name || '<unnamed>'}: a Python child's ` +
                            `text is compared by ${compared.join(', ')} without \`normalize_newlines\`, so ` +
                            `the Windows leg sees the child's \`\\r\\n\``,
                    )
                }
            }
        }
    }
    return { problems, plansRead, pythonEntries, comparingEntries }
}

function report(result) {
    for (const problem of result.problems) {
        console.log(`FAIL ${problem}`)
    }
    if (result.problems.length > 0) {
        console.log(
            `FAIL ${result.problems.length} entry(ies) compare a Python child's text without ` +
                'normalising newlines; add `normalize_newlines` to the entry or to `[defaults] normalizers`',
        )
        return 1
    }
    console.log(
        `PASS every Python child's text is compared after the newlines are normalised ` +
            `(${result.plansRead} plan(s), ${result.pythonEntries} Python entr(ies), ` +
            `${result.comparingEntries} of them compare text)`,
    )
    return 0
}

// The guard on its own fixtures: the red one must produce the message, the repaired one must be
// silent. A guard that cannot be made to fail is a guard nobody knows is still connected.
function selftest() {
    const failures = []
    const bad = scan(path.join(SELFTEST_ROOT, 'bad'))
    if (bad.problems.length === 0) {
        failures.push('the fixture that breaks the rule was not reported')
    } else if (!bad.problems.some((problem) => problem.includes('compares.plan.toml'))) {
        failures.push(`the reported problem does not name the fixture's entry: ${bad.problems[0]}`)
    }
    const good = scan(path.join(SELFTEST_ROOT, 'good'))
    if (good.problems.length > 0) {
        failures.push(`the repaired fixture was reported: ${good.problems[0]}`)
    }
    if (good.comparingEntries === 0) {
        failures.push('the repaired fixture compares no Python text, so it proves nothing')
    }
    for (const failure of failures) {
        console.log(`FAIL ${failure}`)
    }
    if (failures.length > 0) {
        console.log(`FAIL ${failures.length} of the guard's own expectations did not hold`)
        return 1
    }
    console.log(
        `PASS plan-normalizer self-test ok (${bad.problems.length} problem(s) on the broken fixture, ` +
            `none on the repaired one, whose ${good.comparingEntries} comparing entr(ies) are covered)`,
    )
    return 0
}

let root = REPO_ROOT
let runSelftest = false
for (let i = 2; i < process.argv.length; i++) {
    if (process.argv[i] === '--root' && i + 1 < process.argv.length) {
        root = path.resolve(process.argv[++i])
    } else if (process.argv[i] === '--selftest') {
        runSelftest = true
    } else {
        console.log(`FAIL unknown argument: ${process.argv[i]}`)
        process.exit(2)
    }
}

process.exit(runSelftest ? selftest() : report(scan(root)))