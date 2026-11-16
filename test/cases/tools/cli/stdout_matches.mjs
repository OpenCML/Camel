#!/usr/bin/env node
/**
 * `expect_stdout_matches` -- the regular-expression assertion over a test's standard output.
 *
 * The semantics, written down so a reader does not have to infer them: the pattern is matched
 * **line by line against the normalized stdout, and any line matching passes**. It is a search, like
 * `expect_stdout_contains`, not a judgement on every line -- so an unanchored `24\\.0` is "some line
 * contains 24.0" and `^24\\.0$` is "some line is exactly 24.0". Both are asserted below, because the
 * difference between them is the whole reason this field can be misread as stricter than it is.
 *
 * And the three ways a plan can be wrong about it are plan errors, not failing tests:
 * a pattern that does not compile (`"["`), an empty pattern (it matches everything and would be
 * green forever), and a second assertion on the same stdout. A run must refuse instead of starting a
 * test whose verdict means nothing.
 *
 * Three runs, not one per claim. Every start of the runner pays a price before it starts anything --
 * it prunes `test/results` and reads the summary of every run directory there
 * (`test/tools/runner-output.mjs#pruneRunnerOutput`) -- and on a host whose tree is large that price
 * is most of the probe's own CPU time: 0.20 s for a bare nested start on a fresh tree, 3.1-3.5 s per
 * start on a checkout holding thousands of run directories. This probe used to start the runner
 * eleven times, once per claim; it starts it three times, once per colour claimed. A run has one
 * exit code and one `[PASS]`/`[FAIL]` line per entry, so the claims group by what they claim:
 *
 *   #  plan            entries  what the run is read for
 *   1  green.plan      4        three patterns that must match (unanchored, anchored, a shape) and a
 *                               list whose every pattern must match: exit 0, one [PASS] per entry
 *   2  red.plan        2        a legal pattern that matches nothing, and a list with one pattern
 *                               missing: exit 1, one [FAIL] per entry, and the message of each
 *   3  refusals.plan   4        an uncompilable pattern, an empty pattern, and one entry for each of
 *                               the two second-stdout assertions: exit 2, one [PLAN ERROR] naming
 *                               plan:line per entry, and no test started
 *
 * What had to stay apart, and why:
 *
 *   * **the refusal run holds nothing else.** Its claim is that *nothing* started -- no `[PASS]`,
 *     no `[FAIL]` -- which any companion test would make false by printing one. All four refusals
 *     are refused at load, before a test starts, so they can share one run: the runner collects
 *     every plan error of the plans it selected and prints them together (that is why four entries
 *     in one file still produce four messages, each named by its own key).
 *   * **green and red cannot share one.** A red entry makes the run exit 1, and then the green
 *     entries' claim "this entry's pattern passed the run" is no longer readable: a run has one
 *     exit code, so it is read once, at the green run. Each colour is then attributed per entry by
 *     its own `[PASS]`/`[FAIL]` line, which is what an attribution reads anyway.
 *
 * Red when: a pattern that does not match passes; a match reports itself as a plan error; a plan
 * error reports itself as a failing test; the diagnostics stop naming the pattern or the head of the
 * output; or a legal pattern is refused.
 *
 * Run: node stdout_matches.mjs
 */

import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const RUNNER = path.join(REPO_ROOT, 'test', 'tools', 'run-tests.mjs')
const SCRATCH = process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(REPO_ROOT, 'tmp', 'stdout-matches-'))

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

// The case every probe plan runs: it prints four lines, so a pattern can be aimed at one of them.
const CASE = path.join(SCRATCH, 'shape.mjs')
fs.writeFileSync(CASE, 'console.log("width 24.0 height 7")\nconsole.log("24.0")\nconsole.log("done")\n')

/**
 * One plan file holding one `[[tests]]` entry per claim. `patterns` is written as the plan's
 * `expect_stdout_matches` (a string, or a list when several are given), and `extra` carries the
 * fields of a refusal -- the second stdout assertion a clash entry exists to collide with.
 */
function writePlan(name, entries) {
    const lines = [`id = "probe.${name}"`, 'tier = "fast"', '']
    for (const entry of entries) {
        lines.push(
            '[[tests]]',
            `name = "probe.${name}.${entry.name}"`,
            'kind = "behavior"',
            'tool = "node"',
            `case = "${CASE.replace(/\\/g, '/')}"`,
            'args = ["${case}"]',
            'expect_exit = 0',
            // `JSON.stringify` of a list of strings is the TOML basic-string form of the same list:
            // both escape `\` and `"`, so the plan holds the pattern this file wrote.
            `expect_stdout_matches = ${JSON.stringify(entry.patterns)}`,
            ...(entry.extra || []),
            'timeout_ms = 30000',
            '',
        )
    }
    const planPath = path.join(SCRATCH, `${name}.plan.toml`)
    const text = lines.join('\n')
    fs.writeFileSync(planPath, text)
    return { planPath, text }
}

function runPlans(plans) {
    const result = spawnSync(process.execPath, [RUNNER, ...plans, '--jobs', '1', '--tier', 'fast'], {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        timeout: 120000,
        env: { ...process.env, CAMEL_TEST_TMPDIR: SCRATCH, CAMEL_TEST_SHOW_OUTPUT: '' },
    })
    return { status: result.status, output: `${result.stdout || ''}${result.stderr || ''}`.replace(/\x1b\[[0-9;]*m/g, '') }
}

// 1. The four claims that a pattern matches. One pattern per entry, because the search semantics is
//    what separates them: unanchored is a substring of a line, anchored is a whole line, and a shape
//    pattern is a whole line too. Each is attributed by its own [PASS]; the run's one exit code says
//    "nothing in this plan went red".
const greenShapes = [
    { name: 'match_plain', patterns: ['24\\.0'], claim: 'a pattern matching a line passes: /24\\.0/ (unanchored is a substring)' },
    { name: 'match_anchored', patterns: ['^24\\.0$'], claim: 'a pattern matching a line passes: /^24\\.0$/ (anchored is a whole line)' },
    { name: 'match_shape', patterns: ['^width \\d+\\.\\d+ height \\d+$'], claim: 'a pattern matching a line passes: a shape pattern' },
    { name: 'match_list', patterns: ['^width', '^done$'], claim: 'a list of patterns that all match passes' },
]
const green = writePlan('green', greenShapes)
const greenRun = runPlans([green.planPath])
check(
    'every entry of the matching plan passed its own pattern (the run exits 0)',
    greenRun.status === 0,
    `status ${greenRun.status}\n${greenRun.output}`,
)
for (const shape of greenShapes) {
    check(
        shape.claim,
        greenRun.output.includes(`[PASS] probe.green.${shape.name}`),
        `no [PASS] probe.green.${shape.name} in\n${greenRun.output}`,
    )
}

// 2. The two claims that a legal pattern can be missing. Both are red, so they share a run: each is
//    attributed by its own [FAIL], and the messages are read off the run's output -- the patterns
//    they quote are different, so a quote cannot come from the neighbour's message.
const redShapes = [
    { name: 'missing', patterns: ['^never printed$'] },
    { name: 'list_miss', patterns: ['^width', '^never$'] },
]
const red = writePlan('red', redShapes)
const redRun = runPlans([red.planPath])
check('the red plan exits 1', redRun.status === 1, `status ${redRun.status}\n${redRun.output}`)
check(
    'an anchored pattern that matches no whole line is red',
    redRun.output.includes('[FAIL] probe.red.missing'),
    redRun.output,
)
check(
    'a legal pattern that matches nothing is red',
    redRun.output.includes('[FAIL] probe.red.list_miss'),
    redRun.output,
)
check('the message quotes the pattern', redRun.output.includes('^never printed$'), redRun.output)
check(
    'the message says the pattern is a legal regular expression',
    /legal regular expression/.test(redRun.output),
    redRun.output,
)
check('the diagnostics print the head of the output', redRun.output.includes('[stdout head]'), redRun.output)
check('the head is the first lines, not the tail', redRun.output.includes('width 24.0 height 7'), redRun.output)
check('the head stops at twelve lines', !/13 of/.test(redRun.output), redRun.output)
check('the failing list entry is the one quoted', redRun.output.includes('^never$'), redRun.output)

// 3. The four claims that a plan is refused. They share one run because all four are refused while
//    the plans are read, before any test starts: the runner collects the plan errors of every plan it
//    selected and prints them together, so one run answers all four. Nothing may have run in it.
const refusals = writePlan('refusals', [
    { name: 'broken', patterns: ['['] },
    { name: 'empty', patterns: [''] },
    {
        name: 'clash_contains',
        patterns: ['done'],
        extra: ['expect_stdout_contains = ["done"]'],
    },
    {
        name: 'clash_exact',
        patterns: ['done'],
        extra: ['expect_stdout_exact = "done\\\\n"'],
    },
])
const refusalRun = runPlans([refusals.planPath])
const refusalLines = refusalRun.output.split('\n')
const planErrorLine = (fragment) =>
    refusalLines.find((line) => line.includes('[PLAN ERROR]') && line.includes(fragment))
// Each refusal has to be reported on its own entry: the line names the plan and the line number it
// was found at (`plan:line`), and the test the entry declares. A message that named neither would be
// a refusal a reader could not fix; one that named the neighbour's test would attribute the defect
// to the wrong entry, which is the reading this whole plan exists to make possible.
const refused = (entry, fragment) => {
    const line = planErrorLine(fragment)
    return Boolean(line) && line.includes(`probe.refusals.${entry}`) && /refusals\.plan\.toml:\d+/.test(line)
}
check(
    'a plan holding an uncompilable pattern, an empty pattern and two second-stdout assertions refuses (exit 2)',
    refusalRun.status === 2,
    `status ${refusalRun.status}\n${refusalRun.output}`,
)
check(
    'the uncompilable pattern is a plan error that quotes the pattern and names plan:line',
    refused('broken', 'expect_stdout_matches: /[/') &&
        /is not a regular expression/.test(planErrorLine('expect_stdout_matches: /[/') || ''),
    refusalRun.output,
)
check('the empty-pattern error is a plan error that says why', refused('empty', 'green forever'), refusalRun.output)
for (const [field, entry] of [
    ['expect_stdout_contains', 'clash_contains'],
    ['expect_stdout_exact', 'clash_exact'],
]) {
    check(
        `expect_stdout_matches with ${field} is a plan error on ${entry}, naming both fields`,
        refused(entry, `expect_stdout_matches with ${field}`),
        refusalRun.output,
    )
}
check('no test ran', !/\[PASS\]|\[FAIL\]/.test(refusalRun.output), refusalRun.output)

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)