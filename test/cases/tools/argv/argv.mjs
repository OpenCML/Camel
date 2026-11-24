#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Three ways a run refuses to start, and the three sentences that must not be confused.
 *
 * All three end non-zero and none of them measures anything, so an exit code cannot tell them apart.
 * What can is the line the run prints, and on 2026-11-24 the line for the first of them was a stack
 * trace about a missing directory: the host called `test/tools/run-tests.mjs --list-cases` on a tree
 * cut before that option existed, the parser pushed the word into `targets`, `walkPlans` stat'ed
 * `.../--list-cases`, and `scripts/selection-cases.mjs` reported `the plan loader refused this
 * selection (exit 1)` -- a version mismatch wearing the words of an empty selection. The runner now
 * refuses a word that starts with `-`, names the word, prints the argv it does know and the commit
 * it is, and the consumer passes that reading through instead of renaming it.
 *
 *   `--unknown`    an option this checkout does not have, with a word no tree has (`--list-cases-v0`)
 *                  so the check does not depend on which options this tree happens to carry:
 *                  `[ERROR] unknown option ... (the caller is newer than this tree)`, the argv list,
 *                  this tree's commit, and **none** of the other two sentences.
 *   `--preflight`  a valid argv and an install that is not there: `[FAIL] no Release binary at ...`,
 *                  and not "unknown option" -- a missing build is not a caller from the future.
 *   `--empty`      a selection that is deliberately empty: `--selection-empty`, whose own sentence
 *                  says nothing was started and nothing was measured, and the plan tree was not
 *                  widened to fill it.
 *
 *   `--gate`       the whole chain, not a part of it: a scratch tree is assembled here (this
 *                  checkout's `test/tools`, `test/runner.mjs`, `plan-selection.json` and a plan, plus
 *                  its `scripts/selection-cases.mjs`) and **its runner is made older than the
 *                  caller** by renaming the `--list-cases` branch, which is exactly what a tree cut
 *                  before that option did. The real `selection-cases.mjs` then lists that tree's
 *                  cases, and what a gate would print is asserted: the version-mismatch sentence,
 *                  and neither `refused this selection` nor `the cases this change selects could not
 *                  be computed` -- the line that cost the merge position a round on 2026-11-24.
 *                  The runner's refusal is painted red, and the first version of the consumer's
 *                  branch never fired because of it: this case is why that is now known.
 *
 * Each facet asserts its own sentence **and the absence of the other two**, so the check fails both
 * when a sentence goes missing and when two causes collapse into one sentence again. The exit codes
 * are asserted as they are (`1`, `2`, `2`, `2`): this check is about which words a reader gets, and
 * it does not move a code the gate reads.
 *
 *   `--exits`      the probe tool's four exits, because they are growing to look alike: `0` the pit is
 *                  there and the chain behaves, `1` the pit is there and the chain misbehaves, and two
 *                  ways of measuring nothing that share `2` -- a ref that cannot be read and a
 *                  checkout that is not a green starting point. The two 2s must say different
 *                  things, or "could not measure" and "measured red" collapse again. The last two
 *                  readings come from scratch copies of the tool with a consumer that misnames the
 *                  mismatch adversarially, which is the one place a stand-in is honest: the stand-in
 *                  is the wrong answer, not the thing under test.
 *
 * Usage: node argv.mjs --unknown | --preflight | --empty | --gate | --exits
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const RUNNER = path.join(REPO_ROOT, 'test', 'tools', 'run-tests.mjs')
const MISSING_INSTALL = path.join(REPO_ROOT, 'out', 'argv-probe-no-install')

const UNKNOWN_WORD = '--list-cases-v0'

const SENTENCES = {
    unknown: "unknown option '--list-cases-v0': this runner does not know it (the caller is newer than this tree)",
    preflight: 'no Release binary at',
    empty: 'nothing was started, so nothing was measured',
}

const FACETS = {
    unknown: {
        args: [UNKNOWN_WORD, '--tier', 'fast'],
        exit: 1,
        contains: [SENTENCES.unknown, '[ARGV] this runner knows:', '[ARGV] this tree is', '--list-cases'],
        absent: [SENTENCES.preflight, SENTENCES.empty, 'refused this selection'],
    },
    preflight: {
        args: ['--camel', path.join(MISSING_INSTALL, 'bin', 'camel.exe'), '--tier', 'fast'],
        exit: 2,
        contains: [SENTENCES.preflight, 'argv-probe-no-install'],
        absent: ['unknown option', SENTENCES.empty, 'refused this selection'],
    },
    empty: {
        args: ['--selection-empty', '--tier', 'fast'],
        exit: 2,
        contains: ['--selection-empty', SENTENCES.empty],
        absent: ['unknown option', SENTENCES.preflight, 'refused this selection'],
    },
    exits: {
        // Five readings, four exits: the tool prints its own verdict line in each.
        exit: 0,
        contains: [],
        absent: [],
    },
    gate: {
        // `scripts/old-tree-probe.mjs --check` exits 0 when the chain behaves: it has already
        // asserted the consumer's exit 2 and the words, and it prints what it read.
        exit: 0,
        contains: [
            'PASS probe.old-tree',
            "this tree's runner does not know --list-cases",
            'this is not an empty selection',
            '[ARGV] this tree is',
            'built by synthesis, not by archive',
        ],
        // The two sentences a gate printed before this lane, one about a selection and one about the
        // cases it could not compute: neither may come back for a version mismatch.
        absent: ['refused this selection', 'the cases this change selects could not be computed'],
    },
}

const facet = process.argv[2] === undefined ? null : process.argv[2].replace(/^--/, '')
if (!Object.hasOwn(FACETS, facet)) {
    console.error(`usage: node argv.mjs ${Object.keys(FACETS).map((name) => `--${name}`).join(' | ')}`)
    process.exit(2)
}

const spec = FACETS[facet]
if (facet === 'exits') {
    process.exit(runExits())
}
let run
let out
if (facet === 'gate') {
    // The chain lives in `scripts/old-tree-probe.mjs`, which builds the old half and asserts the
    // sentence; this case is what keeps that tool from rotting, and the tool is what a seat runs
    // when it wants the pit reproduced rather than remembered.
    run = spawnSync(process.execPath, [path.join(REPO_ROOT, 'scripts', 'old-tree-probe.mjs'), '--check'], {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        windowsHide: true,
    })
    out = `${run.stdout || ''}${run.stderr || ''}`
} else {
    // `CAMEL_TEST_RESULTS_ROOT` is not set on purpose: a refusal that never starts a run must not
    // need a place to write one, and these facets all refuse before the first test.
    run = spawnSync(process.execPath, [RUNNER, ...spec.args], {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        windowsHide: true,
        env: { ...process.env, CAMEL_TEST_RUN_DIRS_KEEP: '1' },
    })
    out = `${run.stdout || ''}${run.stderr || ''}`
}
const problems = []

if (run.error) {
    problems.push(`the runner could not be started: ${run.error.message}`)
} else {
    if (run.status !== spec.exit) {
        problems.push(`expected exit ${spec.exit}, got ${run.status}`)
    }
    for (const needle of spec.contains) {
        if (!out.includes(needle)) problems.push(`the output does not say '${needle}'`)
    }
    for (const needle of spec.absent) {
        if (out.includes(needle)) problems.push(`the output says '${needle}', which belongs to another cause`)
    }
    // The argv list is only worth printing if it is the tree's own: the option this accident was
    // about must appear in it, or the list is a decoration that will drift from the parser.
    if (facet === 'unknown' && !out.includes('  --tier')) {
        problems.push('the argv list does not read as a list of options')
    }
}

if (problems.length > 0) {
    for (const problem of problems) console.log(`FAIL ${problem}`)
    console.log('--- the run said:')
    for (const line of out.split(/\r?\n/)) if (line.trim() !== '') console.log(`    ${line}`)
    console.log(`FAIL argv.${facet}`)
    process.exit(1)
}
console.log(`PASS argv.${facet}`)

/**
 * The tool's exits, one reading each, because `0` and `1` and the two faces of `2` are all just
 * numbers and only the sentences tell them apart.
 *
 * The red and the second `2` need a consumer that answers badly, and there is no way to make the
 * real one do that on demand: so they run against a scratch copy of the tool whose consumer is a
 * stand-in. That is the one place a stand-in is honest -- it is the *wrong answer* being fed in, not
 * the thing under test -- and the tool's own words are what the assertions land on.
 */
function runExits() {
    const scratch = []
    const readings = []
    const read = (label, tree, args) =>
        readings.push({
            label,
            ...spawnSync(process.execPath, [path.join(tree, 'scripts', 'old-tree-probe.mjs'), ...args], {
                cwd: tree,
                encoding: 'utf8',
                windowsHide: true,
            }),
        })
    read('green', REPO_ROOT, ['--check'])
    read('no tree', REPO_ROOT, ['--build', '--ref', '0'.repeat(40)])
    read('no such option', REPO_ROOT, ['--nope'])
    for (const kind of ['broken-words', 'always-refuses']) {
        const tree = scratchTree(kind)
        scratch.push(tree)
        read(kind === 'broken-words' ? 'red' : 'self refused', tree, ['--check'])
    }
    const expected = [
        ['green', 0, 'PASS probe.old-tree'],
        ['no tree', 2, '[PROBE] the old tree was not built:'],
        ['no such option', 2, "old-tree-probe: unknown option '--nope'"],
        ['red', 1, 'FAIL probe.old-tree'],
        ['self refused', 2, '[PROBE] self refused:'],
    ]
    const problems = []
    const lines = []
    for (const [label, code, says] of expected) {
        const reading = readings.find((candidate) => candidate.label === label)
        const text = `${reading.stdout || ''}${reading.stderr || ''}`
        const said = text.split(/\r?\n/).find((line) => line.includes(says)) || ''
        lines.push(`[EXITS] ${label}: exit ${reading.status} :: ${said.trim()}`)
        if (reading.status !== code) problems.push(`${label}: expected exit ${code}, got ${reading.status}`)
        if (said === '') problems.push(`${label}: the output does not say '${says}'`)
    }
    // The two refusals share a code; they must not share a sentence.
    const refusals = expected.filter((entry) => entry[1] === 2).map((entry) => entry[0])
    const said = refusals.map((label) => (lines.find((line) => line.startsWith(`[EXITS] ${label}:`)) || '').split(':: ')[1])
    if (said[0] === said[1]) problems.push('the two exits that are both 2 say the same thing')
    for (const tree of scratch) fs.rmSync(tree, { recursive: true, force: true })
    for (const line of lines) console.log(line)
    if (problems.length > 0) {
        for (const problem of problems) console.log(`FAIL ${problem}`)
        console.log('FAIL argv.exits')
        return 1
    }
    console.log('PASS argv.exits')
    return 0
}

/**
 * A tree that only exists to feed the tool a bad consumer: a runner carrying the branch the tool
 * removes, this checkout's `test/runner.mjs` and `plan-selection.json` (the synthesis copies them),
 * a plan, a copy of the tool, and a consumer whose answer depends on `kind`.
 */
function scratchTree(kind) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-probe-exits-'))
    for (const rel of ['test/tools', 'test/plans/smoke', 'scripts']) fs.mkdirSync(path.join(dir, rel), { recursive: true })
    fs.writeFileSync(path.join(dir, 'test', 'tools', 'run-tests.mjs'), "const BRANCH = \"arg === '--list-cases'\"\n")
    fs.writeFileSync(path.join(dir, 'test', 'plans', 'smoke', 'core.plan.toml'), '')
    for (const rel of ['test/runner.mjs', 'test/plan-selection.json', 'scripts/old-tree-probe.mjs']) {
        fs.copyFileSync(path.join(REPO_ROOT, rel), path.join(dir, rel))
    }
    const always = kind === 'always-refuses'
    fs.writeFileSync(
        path.join(dir, 'scripts', 'selection-cases.mjs'),
        [
            '#!/usr/bin/env node',
            "import fs from 'node:fs'",
            "import path from 'node:path'",
            "import url from 'node:url'",
            'const HERE = path.dirname(url.fileURLToPath(import.meta.url))',
            "const root = path.resolve(HERE, '..')",
            `const runner = fs.readFileSync(path.join(root, 'test', 'tools', 'run-tests.mjs'), 'utf8')`,
            `const knows = runner.includes("arg === '--list-cases'")`,
            `if (${always ? 'false' : 'knows'}) {`,
            `    console.log(JSON.stringify({ name: 'stub.one', verdict: 'pass', reading: 'stub' }))`,
            '    process.exit(0)',
            '}',
            `console.log("selection-cases: the plan loader refused this selection (exit 2): [ERROR] unknown option '--list-cases'")`,
            'process.exit(2)',
            '',
        ].join('\n'),
    )
    return dir
}