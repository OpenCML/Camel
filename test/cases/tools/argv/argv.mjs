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
 * Each facet asserts its own sentence **and the absence of the other two**, so the check fails both
 * when a sentence goes missing and when two causes collapse into one sentence again. The exit codes
 * are asserted as they are (`1`, `2`, `2`): this check is about which words a reader gets, and it
 * does not move a code the gate reads.
 *
 * Usage: node argv.mjs --unknown | --preflight | --empty
 */

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
}

const facet = process.argv[2] === undefined ? null : process.argv[2].replace(/^--/, '')
if (!Object.hasOwn(FACETS, facet)) {
    console.error(`usage: node argv.mjs ${Object.keys(FACETS).map((name) => `--${name}`).join(' | ')}`)
    process.exit(2)
}

const spec = FACETS[facet]
// `CAMEL_TEST_RESULTS_ROOT` is not set here on purpose: a refusal that never starts a run must not
// need a place to write one, and the three facets below all refuse before the first test.
const run = spawnSync(process.execPath, [RUNNER, ...spec.args], {
    cwd: REPO_ROOT,
    encoding: 'utf8',
    windowsHide: true,
    env: { ...process.env, CAMEL_TEST_RUN_DIRS_KEEP: '1' },
})
const out = `${run.stdout || ''}${run.stderr || ''}`
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