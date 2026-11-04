#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What a layer's entry count is made of: the entries the plan files declare, the entries a
// `[[sweeps]]` entry derives from them, and how many sweep entries do the deriving.
//
// A layer's size is quoted as one number, and one number cannot say whether a layer grew by 3000
// things somebody wrote or by 3000 copies of one thing a sweep repeats. The milestone layer is the
// case that matters: on 2026-11-05 at 946b6f4d4 it read 3539 entries, of which 246 are written in
// plan files and 3293 are copies one `[[sweeps]]` entry generates. Both are real entries of the
// layer -- a copy runs, and it can fail -- but "3539 entries" and "246 decisions plus one sweep"
// are different facts about the layer, and the run that prints 3539 calls all of them *declared*:
// `test/tools/tier-accounting.mjs` counts what the loader returns, and the loader returns the
// copies. This report answers the question that line does not: of that number, how much is written
// down.
//
// The second pair of readings is what keeps the first from being a conclusion. "All 3293 copies
// come from one sweep entry" is true and proves nothing about whether they are redundant: one
// entry can repeat one program 3293 times (redundant) or run 3293 different programs under one
// setting (the property is checked against the whole corpus). So each sweep entry is also read by
// the commands it launches -- `(tool, case, args, envs)` with the variables resolved the way a
// run resolves them -- and then by `(command, the assertions made about it)`. 3293 copies with
// 3293 distinct commands are 3293 assertions; 3293 copies with 12 distinct commands are 12
// assertions paid for 3293 times.
//
// The variables have to be resolved, and this is the trap the report exists in spite of: read as
// written, the milestone copies came out as 2233 distinct argument lists with 1060 repeats,
// because `${backend}` is a pass list (`run_nvm` in `test/vars.toml`) and `.nvm`/`.fvm`/`.jit`
// copies of one test write it identically. Resolving through the runner's own `expandArgs` and
// `expandTextTokens` turns those 2233 into the number below. A count compared on the written words
// is not a count of what runs.
//
//   node scripts/tier-report.mjs                 every layer, one block each
//   node scripts/tier-report.mjs --tier milestone  the layer `--tier milestone` selects
//   node scripts/tier-report.mjs --tier all --quiet  the totals only, no per-sweep detail
//
// It reads plan files through the runner's own loader (`loadPlanTests`, `expandSweeps`, `testTier`,
// `expandArgs`), so a plan is counted exactly as a run would count it, and it needs no build: it
// starts no `camel` binary and reads no `out/`.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { parseTomlFile } from '../test/tools/toml-lite.mjs'
import { expandArgs, expandSweeps, expandTextTokens, loadPlanTests, testTier } from '../test/tools/run-tests.mjs'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const PLANS_ROOT = path.join(ROOT, 'test', 'plans')
const VARS_PATH = path.join(ROOT, 'test', 'vars.toml')
const TIERS = ['fast', 'slow', 'milestone', 'debug']

// The assertions that decide what a copy claims about the run it launches. Two copies that launch
// the same command and assert the same fields are one assertion run twice; two that launch the
// same command and assert different stdout are two claims about one run.
const ASSERTION_KEYS = new Set([
    'golden', 'compare_to', 'compare_fields', 'backends_agree', 'backends',
    'snapshot_stream', 'snapshot_compare', 'normalizers', 'silenced_warnings',
    'known_fail', 'known_fail_msg', 'skip_unless',
])
const ASSERTION_PREFIXES = ['expect_', 'verify_']

const argv = process.argv.slice(2)
let onlyTier = null
let quiet = false
for (let i = 0; i < argv.length; i++) {
    const arg = argv[i]
    if (arg === '--tier' && i + 1 < argv.length) onlyTier = argv[++i]
    else if (arg.startsWith('--tier=')) onlyTier = arg.slice('--tier='.length)
    else if (arg === '--quiet') quiet = true
    else {
        console.error(`tier-report: unknown argument '${arg}'`)
        process.exit(2)
    }
}
if (onlyTier === 'all') {
    // `--tier all` is how a run asks for every layer the release binary runs; here it is the same
    // question as naming no layer, so it prints all four.
    onlyTier = null
}
if (onlyTier !== null && !TIERS.includes(onlyTier)) {
    // A layer nobody knows is refused, never reported as an empty number: an empty layer and a
    // misspelled one read the same, and neither is the reading anybody asked for.
    console.error(`tier-report: --tier must be one of ${TIERS.join(', ')}, got '${onlyTier}'`)
    process.exit(2)
}

/** A plan file's path as a reader names it, relative to the repository root. */
function rel(abs) {
    return path.relative(ROOT, abs).split(path.sep).join('/')
}

function walkPlans(entry, out = []) {
    if (!fs.existsSync(entry)) return out
    if (fs.statSync(entry).isFile()) {
        if (entry.endsWith('.plan.toml')) out.push(entry)
        return out
    }
    for (const name of fs.readdirSync(entry).sort()) walkPlans(path.join(entry, name), out)
    return out
}

// The variables a run gives a test (`runOneTest` in test/tools/run-tests.mjs): every `test/vars.toml`
// value, the case the entry names resolved against its own plan file, the per-test scratch path and
// the back end's pass list. The three values that name an *install* rather than this test -- the
// binary, the interpreter, the scratch directory -- are held at a fixed placeholder: they are the
// same for every copy of a sweep, so reading them from this checkout would put an absolute path
// from this machine into the identity of a program.
const VARS = fs.existsSync(VARS_PATH) ? parseTomlFile(VARS_PATH) : {}
const INSTALL_INDEPENDENT = {
    camel: '<camel>',
    python: '<python>',
    test_tmp: '<test_tmp>',
}

/**
 * The command a test launches, as one string. Not its name: a sweep names each copy
 * `<sweep>:<source plan>:<source test>`, unique by construction, so counting distinct names would
 * answer "all copies differ" for any sweep whatever it repeats.
 */
function commandOf(test) {
    const casePath = typeof test.case === 'string'
        ? path.resolve(path.dirname(test.__planPath), test.case).split(path.sep).join('/')
        : ''
    const scope = { ...VARS, ...INSTALL_INDEPENDENT, case: casePath }
    if (test.__backend) scope.backend = VARS[`run_${test.__backend}`]
    let args
    let envs
    try {
        args = expandArgs(test.args || [], scope)
        envs = (test.envs || []).map((entry) => expandTextTokens(entry, scope)).sort()
    } catch (error) {
        // A variable the runner would refuse is a defect of the plan file, and the runner reports it
        // when the test runs. Here it is one unresolvable command, never a silent empty one.
        return `unresolvable: ${error.message}`
    }
    return JSON.stringify([test.tool || 'camel', casePath, args, envs])
}

/** What a test asserts about the run it launches, as one string: the same command twice is one assertion only if this matches. */
function assertionsOf(test) {
    const picked = {}
    for (const key of Object.keys(test).sort()) {
        if (ASSERTION_KEYS.has(key) || ASSERTION_PREFIXES.some((prefix) => key.startsWith(prefix))) {
            picked[key] = test[key]
        }
    }
    return JSON.stringify(picked)
}

const planFiles = walkPlans(PLANS_ROOT)
if (planFiles.length === 0) {
    // A report over no plan file is not a zero-sized layer, it is a report of nothing.
    console.error(`tier-report: ${rel(PLANS_ROOT)} holds no *.plan.toml`)
    process.exit(2)
}

// One pass over the tree: every declared entry and every derived entry, tagged with its layer and
// -- for a derived one -- the sweep entry it came from.
const layers = new Map(TIERS.map((tier) => [tier, { declared: 0, derived: 0, sweeps: new Map(), declaredCommands: new Map() }]))
const problems = []
for (const planPath of planFiles) {
    let plan
    let ownTests
    let derived
    try {
        const { doc, tests } = loadPlanTests(planPath)
        plan = { ...doc, __planPath: planPath }
        ownTests = tests
        derived = expandSweeps(doc, planPath)
    } catch (error) {
        // A plan the runner cannot read has no layer to report. Naming it is the whole answer; a
        // report that skipped it would print a total that is short by an unknown amount.
        problems.push(`${rel(planPath)}: ${error.message}`)
        continue
    }
    for (const test of ownTests) {
        const tier = testTier(plan, test)
        const bucket = layers.get(tier)
        bucket.declared += 1
        // The same reading for what is written down, so the copies' repeat count has something to
        // be compared with: a layer whose declared entries already run one command twice is not
        // made redundant by a sweep that copies both of them.
        const command = commandOf(test)
        bucket.declaredCommands.set(command, (bucket.declaredCommands.get(command) || 0) + 1)
    }
    for (const test of derived) {
        const tier = testTier(plan, test)
        const key = `${rel(planPath)}#${test.__sweep}`
        const bucket = layers.get(tier)
        bucket.derived += 1
        if (!bucket.sweeps.has(key)) {
            bucket.sweeps.set(key, {
                key,
                copies: 0,
                commands: new Map(),
                assertions: new Map(),
                sources: new Set(),
            })
        }
        const sweep = bucket.sweeps.get(key)
        sweep.copies += 1
        const command = commandOf(test)
        sweep.commands.set(command, (sweep.commands.get(command) || 0) + 1)
        const assertion = `${command}\u0000${assertionsOf(test)}`
        sweep.assertions.set(assertion, (sweep.assertions.get(assertion) || 0) + 1)
        sweep.sources.add(test.__planId || (test.__planPath ? path.basename(test.__planPath, '.plan.toml') : '?'))
    }
}

if (problems.length > 0) {
    for (const problem of problems) console.error(`tier-report: ${problem}`)
    console.error(
        `tier-report: ${problems.length} plan file(s) could not be read, so every number below would ` +
            'be short by an unknown amount: none is printed',
    )
    process.exit(1)
}

/**
 * The commit these numbers belong to, printed before them. A layer's size is a reading of a tree,
 * and the sweep it is mostly made of names plans rather than tests: the number changes the day
 * anyone adds a test to one of those plans. Without the tip, two readings of two commits look like
 * a disagreement, and the reader cannot tell which one is theirs.
 */
function treeProvenance(planCount) {
    const git = (args) => spawnSync('git', args, { cwd: ROOT, encoding: 'utf8', windowsHide: true })
    const tip = git(['rev-parse', 'HEAD'])
    if (tip.error || tip.status !== 0) return `tree unknown | ${planCount} plan files`
    const status = git(['status', '--porcelain', '--untracked-files=no'])
    // A dirty tree has no tip: what would be read is that commit plus edits nobody recorded.
    const dirty = status.status === 0 ? `${status.stdout || ''}`.trim().split('\n').filter(Boolean).length : null
    const tree = dirty === null
        ? tip.stdout.trim()
        : dirty > 0
            ? `(dirty: ${dirty} files)`
            : tip.stdout.trim()
    return `tree ${tree} | ${planCount} plan files`
}

console.log(`[tier-report] ${treeProvenance(planFiles.length)}`)

/** How many of a sweep's copies repeat a command another copy already launches. */
function repeats(counts) {
    let repeated = 0
    let worst = 0
    for (const count of counts.values()) {
        repeated += count - 1
        worst = Math.max(worst, count)
    }
    return { repeated, worst }
}

const shown = onlyTier === null ? TIERS : [onlyTier]
for (const tier of shown) {
    const bucket = layers.get(tier)
    const sweeps = [...bucket.sweeps.values()].sort((lhs, rhs) => rhs.copies - lhs.copies || lhs.key.localeCompare(rhs.key))
    const total = bucket.declared + bucket.derived
    const share = total === 0 ? 0 : Math.round((bucket.derived / total) * 100)
    console.log(
        `[tier-report] ${tier}: ${bucket.declared} declared test entries / ${bucket.derived} derived / ` +
            `${sweeps.length} [[sweeps]] ${sweeps.length === 1 ? 'entry' : 'entries'} ` +
            `= ${total} total (${share}% derived)`,
    )
    if (quiet) continue
    const declaredRepeats = repeats(bucket.declaredCommands)
    console.log(
        `[tier-report]   declared: ${bucket.declaredCommands.size} distinct commands ` +
            `(${declaredRepeats.repeated} repeated, most launched ${declaredRepeats.worst} time(s))`,
    )
    for (const sweep of sweeps) {
        const commandRepeats = repeats(sweep.commands)
        const assertionRepeats = repeats(sweep.assertions)
        console.log(
            `[tier-report]   ${sweep.key}: ${sweep.copies} copies, ` +
                `${sweep.commands.size} distinct (tool, case, args, envs) commands ` +
                `(${commandRepeats.repeated} repeated, most launched ${commandRepeats.worst} time(s)), ` +
                `${sweep.assertions.size} distinct command+assertion pairs ` +
                `(${assertionRepeats.repeated} repeated), ` +
                `from ${sweep.sources.size} source plan(s)`,
        )
    }
}