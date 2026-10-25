#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The release gate: one command that runs the layers a release decision needs, and says out
// loud what it does not run.
//
// Why this exists: `npm run test` is `--tier fast`, and the tier filter
// only applies to a directory target -- so the plan files it walks lose every `tier = "slow"`
// test, and the slow plans under `feat/mm`, `feat/opt` and `feat/tools` never run at all.
// `gpu/**` and `perf/**` are in no gate target either. A "green gate" therefore did not mean
// "the tree is green"; it meant "the fast layer is green". An earlier audit then recommended
// `-Plan test/plans -Tier all` for the release, which would have started the cases
// `agents/test.md` reserves for a milestone on a quiet machine (GC stress and poison, progen,
// the PyTorch comparisons, the benchmarks) on a shared host.
//
// This script owns the gate instead:
//
//   1. fast layer   -- `smoke` + `feat/**` at `--tier fast` (what `npm run test` does), plus
//                      `gpu/profile`, a fast-tier plan that no other gate target reaches;
//   2. slow layer   -- the plan files proved runnable on the shared host, named one by
//                      one. Naming a file makes the runner run the whole plan, slow tests
//                      included, whatever `--tier` says; that is the only honest way to reach
//                      them, and a directory target would silently drop them again;
//   3. static checks-- the seven tree/compiler checks a release has to pass;
//   4. the policy  -- the plans in FORBIDDEN are the milestone layer: each of their own files says
//                      `tier = "milestone"`, so they are never run by an ordinary or a slow run and
//                      `--tier milestone` is what selects them. The gate prints them by name,
//                      with the reason and the policy, and refuses (exit 2) when a caller asks
//                      for one (a positional target, `--include`, `--plan`, `--tier all`,
//                      `--all`, or any directory that contains one).
//
// The gate does not build. It verifies the install it measures -- `--camel <path>`, else this
// tree's `out/latest` -- both that one exists and that its build stamp matches this tree's sources,
// and refuses to start otherwise, because a run against a missing or stale binary measures nothing.
// The choice is made once, here, and handed to every layer as the same `--camel` argument, so no
// layer resolves a default of its own and "checked A, measured B" cannot arise. Build once
// (`npm run build`, or `camel-remote.ps1` without `-Plan`), then run:
//
//   npm run release-gate                     # every layer, the whole gate
//   node scripts/release-gate.mjs --jobs 8
//   node scripts/release-gate.mjs --camel out/debug/bin/camel --only static
//   node scripts/release-gate.mjs --dry-run  # print the commands and the skip list, run nothing
//   node scripts/release-gate.mjs --only slow # PARTIAL: one layer, for re-checking a phase
//   node scripts/release-gate.mjs --include test/plans/feat/tools/lsp.plan.toml
//
// `--only` is for working on the gate, not for a release decision: a partial run prints that it
// is partial, because a gate that reports green while a layer did not run is the defect this
// script exists to remove.
//
// A skipped plan is UNKNOWN, never green. This is the accounting `agents/test.md`
// ask for, printed on every run:
//
//   * the 25 policy-forbidden plan files (22 whole plans + 3 plans whose held-back entries are
//     named one by one) -- the cases the milestone layer would run, counted below through the
//     runner's own reading of those files and printed on every run -- not run by this gate, not
//     claimed. They are the milestone layer, and `--tier milestone` is the layer that runs them;
//   * the debug layer (`--tier debug`, a Debug build) -- never part of a Release gate.
//
// Exit codes: 0 every selected phase passed; 1 a phase failed; 2 refused or not runnable
// (forbidden plan, missing build, unknown argument). A refusal never runs anything.

import fs from 'node:fs'
import path from 'node:path'
import { spawn, spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
// The layers and the check that every plan file has one live in `scripts/plan-inventory.mjs`,
// because the merge gate's fast layer runs the same check as a plan entry
// (`test/plans/feat/gate/plan_inventory.plan.toml`); this script imports its tables instead of
// keeping a second copy that could disagree with it.
import {
    FAST_TARGETS,
    SLOW_PLANS,
    FORBIDDEN,
    caseLedgerProblems,
    planInventoryProblems,
    rel,
    walkPlans,
} from './plan-inventory.mjs'
// The layer of a test, by the runner's own rule: the accounting below counts the cases the policy
// holds back, and it must count the same tests the runner would run, from the same rule and the
// same reading of a plan file.
import { expandSweeps, loadPlanTests, testTier } from '../test/tools/run-tests.mjs'
// The stamp rule the two stdlib checkers also apply, so the gate and a checker that reads the
// install cannot disagree about what "this tree's build" means.
import { refuseInstallNotOfThisTree } from './install-stamp.mjs'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

// Earlier audits used 8 too. `-Jobs 12` makes `gpu_preference.no_device.*` hit spurious 10 s
// timeouts on the shared host; the gate does not expose a way to forget that.
const DEFAULT_JOBS = 8

const POLICY =
    'agents/test.md, General Policy: the full suite, GC poison sweep, GC stress plans, progen, ' +
    'PyTorch comparisons and perf/timeit plans are milestone-only -- run by the coordinator on a ' +
    'quiet machine, never on the shared host'

// The static checks, in the order a release reads them. Each one exits non-zero on a defect;
// the gate's criterion is that exit code, not the text of the last line.
//
// `camel: true` marks a check that reads the install: the gate appends `--camel <the install it
// picked>` to its command line, because a check left to resolve its own default would read
// `out/latest` while the gate measured another install -- one run measuring two binaries, the
// defect this gate exists to catch. `--verify-doc` reads the tree only, so it takes no install and
// names none: an argument it never reads would claim a measurement it does not take.
const STATIC_CHECKS = [
    { name: 'stdlib reference: declarations agree with the registered operators', args: ['scripts/stdlib-reference.mjs', '--verify-ops'], camel: true },
    { name: 'stdlib reference: docs/stdlib-reference.md is up to date', args: ['scripts/stdlib-reference.mjs', '--verify-doc'] },
    { name: 'diagnostics registry: the declarations and the generated code tables agree', args: ['scripts/diagnostics-catalog.mjs', '--check'] },
    { name: 'document links: no broken relative link', args: ['scripts/check-doc-links.mjs', '--all'] },
    { name: 'document snippets: Camel fences compile and print what the page says', args: ['scripts/check-doc-snippets.mjs'], camel: true },
    { name: 'stdlib declarations: a migrated module stays migrated', args: ['scripts/stdlib-declarations.mjs', '--check'], camel: true },
    { name: 'std includes: no header borrowed through another header', args: ['scripts/check-std-includes.mjs'] },
]

// The command line of one static check, with the install this gate found when the check reads one.
function staticCheckArgv(check, camel) {
    return check.camel ? [...check.args, '--camel', camel] : check.args
}

const ANSI = /\u001b\[[0-9;]*[A-Za-z]/g
const LAYERS = ['fast', 'slow', 'static']

function usage() {
    console.log(`Usage: node scripts/release-gate.mjs [options]

  --camel PATH    the install every layer measures: its bin/camel (default: this tree's
                  out/latest). The environment does not choose it -- a layer's children get
                  CAMEL_HOME set to this install's root -- and an install that is not this tree's
                  build is refused before anything runs
  --jobs N        parallel width for the plan runs (default ${DEFAULT_JOBS}; 12 is known to make
                  gpu_preference.no_device.* time out on the shared host)
  --only LIST     comma-separated subset of fast,slow,static. A PARTIAL run, not a gate result
  --include PATH  an extra plan file or plan directory for the slow phase (refused when it is,
                  or contains, a plan agents/test.md forbids)
  --dry-run       print the commands, the skip list and the coverage, then exit 0
  -h, --help      this text

The gate runs the fast layer, the slow layer by explicit plan file (never by tier or directory),
the static checks, then exits non-zero if any of them failed. It never builds: build once first.`)
}

function die(message, code = 2) {
    console.error(`[gate] ${message}`)
    process.exit(code)
}

function parseArgs(argv) {
    const options = { jobs: DEFAULT_JOBS, only: null, include: [], dryRun: false, camel: null }
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '-h' || arg === '--help') {
            usage()
            process.exit(0)
        } else if (arg === '--dry-run' || arg === '--list') {
            options.dryRun = true
        } else if (arg === '--camel') {
            const value = argv[++i]
            if (!value) die('--camel needs the path of the install to measure')
            options.camel = path.resolve(value)
        } else if (arg.startsWith('--camel=')) {
            const value = arg.slice('--camel='.length)
            if (!value) die('--camel needs the path of the install to measure')
            options.camel = path.resolve(value)
        } else if (arg === '--jobs') {
            const value = argv[++i]
            if (!value || !/^[1-9][0-9]*$/.test(value)) die('--jobs needs a positive integer')
            options.jobs = Number(value)
        } else if (arg === '--only') {
            const value = argv[++i]
            if (!value) die('--only needs a comma-separated subset of fast,slow,static')
            const layers = value.split(',').map((s) => s.trim()).filter(Boolean)
            for (const layer of layers) {
                if (!LAYERS.includes(layer)) die(`--only does not know the layer '${layer}' (fast, slow, static)`)
            }
            options.only = [...new Set(layers)]
        } else if (arg === '--include' || arg === '--plan') {
            const value = argv[++i]
            if (!value) die(`${arg} needs a plan file or plan directory`)
            options.include.push(value)
        } else if (arg === '--tier' || arg === '--all' || arg.startsWith('--tier=') || arg === '--suite') {
            // The exact command an earlier audit recommended and a later one warned about. Refuse by name.
            die(
                `REFUSED: '${arg}' selects plans by tier or by the whole tree, which would start ` +
                    `the ${FORBIDDEN.length} plan files the policy holds back for the milestone layer.\n[gate] ${POLICY}.\n` +
                    `[gate] The slow layer is an explicit plan file list (H154 §3); see --include for one file.`
            )
        } else if (arg.startsWith('-')) {
            die(`unknown option '${arg}'`)
        } else {
            // A bare target is an include: convenient, and it goes through the same refusal.
            options.include.push(arg)
        }
    }
    return options
}

const FORBIDDEN_BY_REL = new Map(FORBIDDEN.map((entry) => [entry.file, entry]))

function checkIncludes(include) {
    const extra = []
    const refused = []
    for (const entry of include) {
        const files = walkPlans(entry)
        if (!files || files.length === 0) die(`'${entry}' is not a plan file, a plan directory, or a directory of them`)
        for (const abs of files) {
            const key = rel(abs)
            const forbidden = FORBIDDEN_BY_REL.get(key)
            if (forbidden) refused.push({ key, reason: forbidden.reason })
            else extra.push(key)
        }
    }
    if (refused.length > 0) {
        console.error(`[gate] REFUSED: ${refused.length} of the requested plan file(s) are in the set the policy forbids.`)
        for (const { key, reason } of refused) console.error(`[gate]   ${key} -- ${reason}`)
        console.error(`[gate] ${POLICY}.`)
        console.error('[gate] The gate will not start them, not even when asked: their tests are UNKNOWN, not green.')
        process.exit(2)
    }
    return [...new Set(extra)]
}

// The one install this run measures: the binary `--camel <path>` names, else this tree's
// `out/latest/bin/camel`. Picked once, here. The environment does not choose it: `CAMEL_HOME` is
// the install location's convention -- what the binary and an SDK user read to find the runtime
// library -- and letting it pick would make the measured install depend on the shell. What the
// chosen install's children read as `CAMEL_HOME` is its own root, set by each layer.
function findCamel(named) {
    if (named) return fs.existsSync(named) ? named : null
    for (const name of ['camel', 'camel.exe']) {
        const candidate = path.join(ROOT, 'out', 'latest', 'bin', name)
        if (fs.existsSync(candidate)) return candidate
    }
    return null
}

// The gate never builds, and it never runs against an install it cannot prove: `-NoBuild` once
// silently reused another revision's `out/latest`, and a gate that measured that binary would
// report a tree nobody tested. The check is `refuseInstallNotOfThisTree`, the same rule the stdlib
// checkers apply to an install they are pointed at: the stamp travels inside the install, so an
// install outside this tree's `out/` is compared with this tree's sources and refused the same way
// rather than exempted.

function git(...args) {
    const proc = spawnSync('git', args, { cwd: ROOT, encoding: 'utf8', windowsHide: true })
    if (proc.error || proc.status !== 0) return null
    return proc.stdout.trim()
}

function treeIdentity() {
    const head = git('rev-parse', 'HEAD') || 'unknown'
    const branch = git('rev-parse', '--abbrev-ref', 'HEAD') || 'unknown'
    const dirty = git('status', '--porcelain')
    return { head, branch, dirty: dirty === null ? null : dirty.split('\n').filter(Boolean).length }
}

function stripAnsi(text) {
    return text.replace(ANSI, '')
}

// Tee a child's stdout/stderr to the console while keeping the text, so the phase can report the
// runner's own `[SUMMARY]` line instead of the gate re-counting tests. A gate that paraphrases a
// summary can be wrong about its own numbers; this one only quotes them.
function runStreamed(argv) {
    return new Promise((resolve) => {
        const started = Date.now()
        let text = ''
        const child = spawn(argv[0], argv.slice(1), { cwd: ROOT, env: process.env })
        const tee = (stream, sink) => {
            stream.on('data', (chunk) => {
                const value = chunk.toString()
                text += value
                sink.write(value)
            })
        }
        tee(child.stdout, process.stdout)
        tee(child.stderr, process.stderr)
        child.on('error', (error) => {
            text += `\n${error.message}\n`
            resolve({ code: 127, text, seconds: (Date.now() - started) / 1000 })
        })
        child.on('close', (code) => resolve({ code: code ?? 1, text, seconds: (Date.now() - started) / 1000 }))
    })
}

function readSummary(text) {
    const clean = stripAnsi(text)
    let summary = null
    for (const match of clean.matchAll(
        /^\[SUMMARY\] total: (\d+), pass: (\d+), fail: (\d+)(?:, skip: (\d+))?(?:, known failures: (\d+))?$/gm
    )) {
        summary = {
            total: Number(match[1]),
            pass: Number(match[2]),
            fail: Number(match[3]),
            skip: Number(match[4] || 0),
            xfail: Number(match[5] || 0),
        }
    }
    return summary
}

// The cases the policy holds back, read through the runner's own loaders: the printed accounting
// follows the tree, so a plan that grows a case is counted the day it lands, and no number is
// written down here to go stale. A whole plan the policy forbids carries `tier = "milestone"` in its own
// file, so all of its cases count; in the three files only some tests are held back, and those say
// `tier = "milestone"` themselves. Counting the runner's tests, not the raw table rows, is what
// makes the number the number of cases `--tier milestone` would run from these files.
function forbiddenMilestoneTests() {
    let total = 0
    for (const entry of FORBIDDEN) {
        const file = path.join(ROOT, entry.file)
        if (!fs.existsSync(file)) continue
        let read
        try {
            read = loadPlanTests(file)
        } catch {
            // a plan the runner cannot read fails the inventory check above, by name
            continue
        }
        const plan = { ...read.doc, __planPath: file }
        for (const test of [...read.tests, ...expandSweeps(read.doc, file)]) {
            try {
                if (testTier(plan, test) === 'milestone') total += 1
            } catch {
                // a plan whose tier the runner does not know fails the inventory check above
            }
        }
    }
    return total
}

function printSkipList() {
    console.log(`[gate] held by policy for the milestone layer: ${FORBIDDEN.length} plan files, ${forbiddenMilestoneTests()} cases`)
    console.log(`[gate]   ${POLICY}`)
    for (const entry of FORBIDDEN) console.log(`[gate]   ${entry.file} -- ${entry.reason}`)
    console.log('[gate]   These cases are UNKNOWN, not green. This gate never runs them; the names are printed')
    console.log('[gate]   so the next reader does not mistake the fast layer for the whole tree.')
    console.log('[gate]   They are selected by `--tier milestone`, which a milestone run names on purpose.')
    console.log('[gate] not run here: the debug layer (`--tier debug`, a Debug build) -- never part of a Release gate')
}

// One phase runs one or more commands; the phase fails on the first non-mergeable command exit
// (for the plans that is the runner's own exit 1) and keeps the numbers the runner printed.
async function runPlanPhase(label, argv) {
    console.log(`\n[gate] ===== ${label} =====`)
    console.log(`[gate] $ ${[process.execPath, ...argv].join(' ')}`)
    const result = await runStreamed([process.execPath, ...argv])
    const summary = readSummary(result.text)
    if (!summary) {
        console.log(`[gate] ${label}: FAILED (exit ${result.code}, ${result.seconds.toFixed(1)} s; the runner printed no [SUMMARY])`)
        return { label, ok: false, code: result.code, seconds: result.seconds, summary: null }
    }
    const ok = result.code === 0 && summary.fail === 0
    const details = `total ${summary.total}, pass ${summary.pass}, fail ${summary.fail}, skip ${summary.skip}` +
        (summary.xfail ? `, known failures ${summary.xfail}` : '')
    console.log(`[gate] ${label}: ${ok ? 'PASS' : 'FAIL'} (${details}; ${result.seconds.toFixed(1)} s)`)
    return { label, ok, code: result.code, seconds: result.seconds, summary }
}

async function runStaticPhase(camel) {
    console.log(`\n[gate] ===== static checks (${STATIC_CHECKS.length}) =====`)
    const results = []
    for (let i = 0; i < STATIC_CHECKS.length; i++) {
        const check = STATIC_CHECKS[i]
        const argv = staticCheckArgv(check, camel)
        console.log(`[gate] static ${i + 1}/${STATIC_CHECKS.length}: ${check.name}`)
        console.log(`[gate] $ ${[process.execPath, ...argv].join(' ')}`)
        const result = await runStreamed([process.execPath, ...argv])
        const ok = result.code === 0
        console.log(`[gate] static ${i + 1}/${STATIC_CHECKS.length}: ${ok ? 'PASS' : `FAIL (exit ${result.code})`}`)
        results.push({ name: check.name, ok, code: result.code })
    }
    const failed = results.filter((r) => !r.ok)
    console.log(`[gate] static: ${results.length - failed.length}/${results.length} passed`)
    return { label: 'static', ok: failed.length === 0, failed }
}

async function main() {
    const options = parseArgs(process.argv.slice(2))
    const layers = options.only ?? LAYERS
    const partial = layers.length !== LAYERS.length
    const extraSlow = checkIncludes(options.include)

    // The gate's own table may never overlap the policy list: a gate that runs a forbidden plan
    // is the defect it exists to catch, so fail on the contradiction instead of trusting review.
    for (const file of [...SLOW_PLANS, ...FAST_TARGETS]) {
        if (FORBIDDEN_BY_REL.has(file)) die(`internal error: '${file}' is both gated and forbidden; refusing to run`, 1)
    }
    // Every problem at once, from the one implementation of the inventory: a stale list entry (a
    // slow plan that does not exist, a forbidden one that does not), every plan file that has no
    // layer, and every file under test/cases that nothing reads. Printing the first one and dying
    // hid the rest: eight perf plans had no layer, and the run named one of them, so the reader
    // fixed one file per run and the list stayed a moving target.
    const inventoryProblems = [...planInventoryProblems(), ...caseLedgerProblems()]
    if (inventoryProblems.length > 0) {
        for (const problem of inventoryProblems) console.error(`[gate] ${problem}`)
        die(
            `the inventory has ${inventoryProblems.length} problem(s); every plan file must be reached by a ` +
                'fast target, named in SLOW_PLANS, named in FORBIDDEN, or be a debug or milestone plan, and ' +
                'every file under test/cases must be read by code (a plan, an import, a tool that opens it) ' +
                'or recorded in CASE_EXCEPTIONS with the reason it is kept',
            1,
        )
    }

    const identity = treeIdentity()
    console.log('[gate] Camel release gate')
    console.log(`[gate] tree: ${identity.head} (${identity.branch}, ${identity.dirty ? `${identity.dirty} dirty file(s)` : 'clean'})`)
    console.log(`[gate] layers: ${layers.join(', ')}${partial ? ' -- PARTIAL RUN, not a gate result' : ''}`)
    if (partial) console.log('[gate] PARTIAL: a layer did not run; the missing results are unknown, not green.')

    // The install is picked once, before any command line is built, and every layer gets the same
    // `--camel` argument: the plan layers set each test child's CAMEL_HOME to this install's root,
    // the static checks read it, and nothing resolves a second default behind the gate's back.
    const camel = findCamel(options.camel)
    const camelArg = camel ? ['--camel', camel] : []
    const fastArgv = [...FAST_TARGETS, '--tier', 'fast', '--jobs', String(options.jobs), ...camelArg]
    // A caller may name a plan the slow list already has; report it as extra only when it is new.
    const extra = extraSlow.filter((file) => !SLOW_PLANS.includes(file))
    const slowPlans = [...SLOW_PLANS, ...extra]
    const slowArgv = [...slowPlans, '--jobs', String(options.jobs), ...camelArg]

    console.log('\n[gate] coverage')
    console.log(`[gate]   fast: ${FAST_TARGETS.join(' ')} (--tier fast: slow tests of those directories are the slow layer's)`)
    console.log(`[gate]   slow: ${slowPlans.length} named plan file(s), in full -- naming the file is what makes a slow test run`)
    if (extra.length > 0) console.log(`[gate]   extra: ${extra.join(', ')} (caller-requested, not in the H154 §3 list)`)
    console.log('[gate]   the slow files that are also fast plans are run in full, so their fast tests appear in both phases')
    printSkipList()

    if (options.dryRun) {
        console.log('\n[gate] dry run: the commands')
        if (layers.includes('fast')) console.log(`[gate] $ ${[process.execPath, 'scripts/test.js', ...fastArgv].join(' ')}`)
        if (layers.includes('slow')) for (const plan of slowPlans) console.log(`[gate] $ ${[process.execPath, 'scripts/test.js', plan, ...camelArg].join(' ')}`)
        if (layers.includes('static')) {
            // The install is resolved at run time; a dry run has none, and says which checks wait
            // for one rather than printing a command line that differs from the one that runs.
            for (const check of STATIC_CHECKS) {
                const argv = check.camel && camel ? staticCheckArgv(check, camel) : check.args
                console.log(`[gate] $ ${[process.execPath, ...argv].join(' ')}`)
                if (check.camel && !camel) console.log('[gate]   (+ `--camel <install>` at run time; no install found now)')
            }
        }
        if (!camel) console.log('[gate]   (+ `--camel <install>` on the plan layers at run time; no install found now)')
        console.log('[gate] dry run: nothing ran')
        return 0
    }

    if (!camel) {
        die(
            options.camel
                ? `no camel binary at ${options.camel}: --camel names the install every layer measures`
                : 'no built Camel: the gate verifies an existing install and never builds.\n' +
                  '[gate] build once (`npm run build`, or camel-remote.ps1 without -Plan), then run the gate.'
        )
    }
    console.log(`[gate] compiler: ${camel}`)
    refuseInstallNotOfThisTree(camel, ROOT, 'release-gate.mjs')
    console.log(`[gate] build stamp: ${path.dirname(path.dirname(camel))} was built from this tree's sources`)

    const phases = []
    if (layers.includes('fast')) {
        phases.push(await runPlanPhase('fast layer', ['scripts/test.js', ...fastArgv]))
    }
    if (layers.includes('slow')) {
        phases.push(await runPlanPhase('slow layer', ['scripts/test.js', ...slowArgv]))
    }
    if (layers.includes('static')) {
        phases.push(await runStaticPhase(camel))
    }

    console.log('\n[gate] ===== summary =====')
    let failedPhases = 0
    for (const phase of phases) {
        if (phase.label === 'static') {
            console.log(`[gate] static      ${phase.ok ? 'PASS' : 'FAIL'} (${phase.failed.length} of ${STATIC_CHECKS.length} checks failed)`)
            if (!phase.ok) for (const check of phase.failed) console.log(`[gate]   failed: ${check.name}`)
        } else if (phase.summary) {
            const s = phase.summary
            console.log(
                `[gate] ${phase.label.padEnd(11)} ${phase.ok ? 'PASS' : 'FAIL'} ` +
                    `(total ${s.total}, pass ${s.pass}, fail ${s.fail}, skip ${s.skip}; ${phase.seconds.toFixed(1)} s)`
            )
        } else {
            console.log(`[gate] ${phase.label.padEnd(11)} FAIL (exit ${phase.code}; no summary)`)
        }
        if (!phase.ok) failedPhases++
    }
    console.log(`[gate] held back   ${FORBIDDEN.length} plan files / ${forbiddenMilestoneTests()} milestone cases (policy; UNKNOWN, not green)`)
    if (partial) console.log('[gate] PARTIAL   this run did not cover every layer; it is not a gate result')
    console.log(`[gate] GATE: ${failedPhases === 0 ? 'all selected phases passed' : `FAILED (${failedPhases} phase(s))`}`)
    if (failedPhases === 0 && !partial) {
        console.log('[gate] Release is a decision from here: the version, the notes and the verdict are the author\'s.')
    }
    return failedPhases === 0 ? 0 : 1
}

try {
    process.exitCode = await main()
} catch (error) {
    console.error(`[gate] ${error && error.stack ? error.stack : error}`)
    process.exitCode = 1
}
