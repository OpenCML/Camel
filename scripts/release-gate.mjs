#!/usr/bin/env node
// The release gate: one command that runs the layers a release decision needs, and says out
// loud what it does not run.
//
// Why this exists (H154 §6, H147 §3.5): `npm run test` is `--tier fast`, and the tier filter
// only applies to a directory target -- so the plan files it walks lose every `tier = "slow"`
// test, and the slow plans under `feat/mm`, `feat/opt` and `feat/tools` never run at all.
// `gpu/**` and `perf/**` are in no gate target either. A "green gate" therefore did not mean
// "the tree is green"; it meant "the fast layer is green". H147 then recommended
// `-Plan test/plans -Tier all` for the release, which would have started the 200 cases
// `agents/test.md` reserves for a milestone on a quiet machine (GC stress and poison, progen,
// the PyTorch comparisons, the benchmarks) on a shared host.
//
// This script owns the gate instead:
//
//   1. fast layer   -- `smoke` + `feat/**` at `--tier fast` (what `npm run test` does), plus
//                      `gpu/profile`, a fast-tier plan that no other gate target reaches;
//   2. slow layer   -- the plan files H154 §3 proved runnable on the shared host, named one by
//                      one. Naming a file makes the runner run the whole plan, slow tests
//                      included, whatever `--tier` says; that is the only honest way to reach
//                      them, and a directory target would silently drop them again;
//   3. static checks-- the seven tree/compiler checks a release has to pass;
//   4. the policy  -- the 19 plans in FORBIDDEN are never run. The gate prints them by name,
//                      with the reason and the policy, and refuses (exit 2) when a caller asks
//                      for one (a positional target, `--include`, `--plan`, `--tier all`,
//                      `--all`, or any directory that contains one).
//
// The gate does not build. It verifies the install it finds (`CAMEL_HOME`, else `out/latest`) -- both
// that one exists and that its build stamp matches this tree's sources (H149) -- and refuses to
// start otherwise, because a run against a missing or stale binary measures nothing. Build once
// (`npm run build`, or `camel-remote.ps1` without `-Plan`), then run:
//
//   npm run release-gate                     # every layer, the whole gate
//   node scripts/release-gate.mjs --jobs 8
//   node scripts/release-gate.mjs --dry-run  # print the commands and the skip list, run nothing
//   node scripts/release-gate.mjs --only slow # PARTIAL: one layer, for re-checking a phase
//   node scripts/release-gate.mjs --include test/plans/feat/tools/lsp.plan.toml
//
// `--only` is for working on the gate, not for a release decision: a partial run prints that it
// is partial, because a gate that reports green while a layer did not run is the defect this
// script exists to remove.
//
// A skipped plan is UNKNOWN, never green. This is the accounting H154 §6 and `agents/test.md`
// ask for, printed on every run:
//
//   * the 19 policy-forbidden plan files (16 whole plans + 3 plans with forbidden slow entries)
//     -- the 200 cases H154 §2 measured, not run, not claimed;
//   * the debug layer (`--tier debug`, a Debug build) -- never part of a Release gate.
//
// Exit codes: 0 every selected phase passed; 1 a phase failed; 2 refused or not runnable
// (forbidden plan, missing build, unknown argument). A refusal never runs anything.

import fs from 'node:fs'
import path from 'node:path'
import { spawn, spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { parseTomlFile } from '../test/tools/toml-lite.mjs'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const PLANS_ROOT = path.join(ROOT, 'test', 'plans')

// H147 and H154 both used 8. `-Jobs 12` makes `gpu_preference.no_device.*` hit spurious 10 s
// timeouts on the shared host; the gate does not expose a way to forget that.
const DEFAULT_JOBS = 8

// The fast layer. `smoke` + `feat` are what `npm run test` walks; `gpu/profile` is fast-tier but
// under `gpu/`, which neither the fast target nor the slow list reaches -- it would otherwise be
// as invisible as the rest of `gpu/**` was before H154.
const FAST_TARGETS = ['smoke', 'test/plans/feat', 'test/plans/gpu/profile.plan.toml']

// The slow layer, by explicit plan file (H154 §3). Eleven files: six fast plans whose slow tests
// are runnable, and five whole slow plans. Every other slow test is in FORBIDDEN below.
const SLOW_PLANS = [
    'test/plans/feat/parse/frontend.plan.toml',
    'test/plans/feat/trans/cpp.plan.toml',
    'test/plans/feat/modules/random.plan.toml',
    'test/plans/feat/tools/lsp.plan.toml',
    'test/plans/feat/linear/var_reference.plan.toml',
    'test/plans/feat/tools/debugger.plan.toml',
    'test/plans/feat/mm/gc_pacing.plan.toml',
    'test/plans/feat/mm/gc_soak.plan.toml',
    'test/plans/gpu/tiles.plan.toml',
    'test/plans/gpu/tiles_rows.plan.toml',
    'test/plans/gpu/tiles_transfer.plan.toml',
]

// Never run. `agents/test.md`, General Policy: the full suite, the GC poison sweep, the GC
// stress plans (`gc.collection_trees_*`, `gc_elder_*`), the progen random-program checks, the
// PyTorch comparisons and the perf/timeit plans are milestone-only -- by the coordinator, on a
// quiet machine -- and the shared host is not one. H154 §2 measured 200 cases here.
//
// A file in this list is named as a whole plan when the whole plan is excluded, and as a plan
// with excluded slow entries when only some of its tests are excluded: the runner cannot select
// a test by name, so naming such a file would start the forbidden tests too (H154 §2, the ONNX
// row). Both kinds are skipped entirely, which is why the list has 19 entries for 200 cases.
const FORBIDDEN = [
    // The benchmark/timeit plans: the machine's load is part of what they measure.
    { file: 'test/plans/perf/fib.plan.toml', reason: 'benchmark (`kind = "benchmark"`): measures the machine load' },
    { file: 'test/plans/perf/para.plan.toml', reason: 'benchmark (`kind = "benchmark"`): measures the machine load' },
    { file: 'test/plans/perf/startup.plan.toml', reason: 'benchmark: measures the machine load' },
    { file: 'test/plans/perf/training_steps.plan.toml', reason: 'GPU training benchmark: measures the machine load' },
    // The other perf plans: minutes-long soaks and fuzzing, or a PyTorch comparison.
    { file: 'test/plans/perf/gc.plan.toml', reason: 'GC soak: minutes long, GB-scale live set (agents/test.md)' },
    { file: 'test/plans/perf/collections_fuzz.plan.toml', reason: 'random-program fuzzing and differential, minutes long (agents/test.md)' },
    { file: 'test/plans/perf/showcase_c1.plan.toml', reason: 'PyTorch comparison (`skip_unless` torch; agents/test.md)' },
    { file: 'test/plans/perf/showcase_c2.plan.toml', reason: 'PyTorch comparison (`skip_unless` torch; agents/test.md)' },
    { file: 'test/plans/perf/showcase_c6.plan.toml', reason: 'PyTorch comparison (`skip_unless` torch; agents/test.md)' },
    { file: 'test/plans/perf/showcase_d5.plan.toml', reason: 'PyTorch comparison (`skip_unless` torch; agents/test.md)' },
    // GC stress and poison: every case sets CAMEL_GC_STRESS_*, CAMEL_GC_POISON or CAMEL_GC_VERIFY.
    { file: 'test/plans/feat/mm/gc.plan.toml', reason: 'GC stress + poison (`CAMEL_GC_STRESS_*`, `CAMEL_GC_POISON=1`)' },
    { file: 'test/plans/feat/mm/gc_collections.plan.toml', reason: 'collection stress (`CAMEL_GC_STRESS_ALLOC` + `CAMEL_GC_VERIFY=both`)' },
    { file: 'test/plans/feat/mm/gc_elder_exhaustion.plan.toml', reason: 'elder-generation exhaustion (`gc_elder_*`; agents/test.md)' },
    { file: 'test/plans/feat/opt/progen.plan.toml', reason: 'progen random-program check (agents/test.md)' },
    { file: 'test/plans/gpu/tiles_regions.plan.toml', reason: 'region run under GC stress (`CAMEL_GC_STRESS_SAFEPOINT=1` + `CAMEL_GC_VERIFY`)' },
    { file: 'test/plans/gpu/tiles_decorator.plan.toml', reason: '`@gpu` decorator: ten GC-stress subprocesses per case' },
    // Files whose slow entries are forbidden; the whole file is skipped (the runner cannot select
    // a test by name, so naming the file would start the forbidden tests).
    { file: 'test/plans/feat/modules/autodiff.plan.toml', reason: 'its 5 slow tests compare against PyTorch (`*_vs_torch64`)' },
    {
        file: 'test/plans/feat/modules/onnx.plan.toml',
        reason: 'its 4 slow tests: 2 PyTorch comparisons + 2 that cannot be selected apart from them (H154 §2)',
    },
    { file: 'test/plans/feat/linear/jit_framed.plan.toml', reason: 'its 2 slow tests are GC stress (`CAMEL_GC_STRESS_SAFEPOINT` + `CAMEL_GC_VERIFY`)' },
]

const POLICY =
    'agents/test.md, General Policy: the full suite, GC poison sweep, GC stress plans, progen, ' +
    'PyTorch comparisons and perf/timeit plans are milestone-only -- run by the coordinator on a ' +
    'quiet machine, never on the shared host'

// The static checks, in the order a release reads them. Each one exits non-zero on a defect;
// the gate's criterion is that exit code, not the text of the last line.
const STATIC_CHECKS = [
    { name: 'stdlib reference: declarations agree with the registered operators', args: ['scripts/stdlib-reference.mjs', '--verify-ops'] },
    { name: 'stdlib reference: docs/stdlib-reference.md is up to date', args: ['scripts/stdlib-reference.mjs', '--verify-doc'] },
    { name: 'diagnostics catalog: the code tables are consistent', args: ['scripts/diagnostics-catalog.mjs', '--check'] },
    { name: 'document links: no broken relative link', args: ['scripts/check-doc-links.mjs', '--all'] },
    { name: 'document snippets: Camel fences compile and print what the page says', args: ['scripts/check-doc-snippets.mjs'] },
    { name: 'stdlib declarations: a migrated module stays migrated', args: ['scripts/stdlib-declarations.mjs', '--check'] },
    { name: 'std includes: no header borrowed through another header', args: ['scripts/check-std-includes.mjs'] },
]

const ANSI = /\u001b\[[0-9;]*[A-Za-z]/g
const LAYERS = ['fast', 'slow', 'static']

function usage() {
    console.log(`Usage: node scripts/release-gate.mjs [options]

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

function rel(abs) {
    return path.relative(ROOT, abs).split(path.sep).join('/')
}

function die(message, code = 2) {
    console.error(`[gate] ${message}`)
    process.exit(code)
}

function parseArgs(argv) {
    const options = { jobs: DEFAULT_JOBS, only: null, include: [], dryRun: false }
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '-h' || arg === '--help') {
            usage()
            process.exit(0)
        } else if (arg === '--dry-run' || arg === '--list') {
            options.dryRun = true
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
            // The exact command H147 §3.5 recommended and H154 §6 warned about. Refuse by name.
            die(
                `REFUSED: '${arg}' selects plans by tier or by the whole tree, which would start ` +
                    `the ${FORBIDDEN.length} plan files the policy forbids.\n[gate] ${POLICY}.\n` +
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

function walkPlans(entry) {
    const abs = path.resolve(ROOT, entry)
    if (!fs.existsSync(abs)) return null
    if (fs.statSync(abs).isFile()) return abs.endsWith('.plan.toml') ? [abs] : null
    const out = []
    const walk = (dir) => {
        for (const name of fs.readdirSync(dir).sort()) {
            const child = path.join(dir, name)
            if (fs.statSync(child).isDirectory()) walk(child)
            else if (name.endsWith('.plan.toml')) out.push(child)
        }
    }
    walk(abs)
    return out
}

const FORBIDDEN_BY_REL = new Map(FORBIDDEN.map((entry) => [entry.file, entry]))

// Every plan file of the tree belongs to one of four fates: a fast target runs its fast tests, a
// SLOW_PLANS entry runs it in full, FORBIDDEN names it as skipped by policy, or the debug layer is
// out of the release gate and says so. A plan outside those four would have tests that no layer
// runs and no line calls UNKNOWN; the list would keep saying "200 cases skipped" while the tree
// grew. This used to be unchecked (H185): a renamed FORBIDDEN file was printed as skipped, and a
// new slow plan under feat/ vanished from both layers.
function planInventoryProblems() {
    const problems = []
    for (const entry of FORBIDDEN) {
        if (!fs.existsSync(path.join(ROOT, entry.file))) {
            problems.push(`the forbidden plan '${entry.file}' does not exist; the skip list is stale`)
        }
    }
    const slowSet = new Set(SLOW_PLANS)
    const forbiddenSet = new Set(FORBIDDEN.map((entry) => entry.file))
    const fastReaches = (key) =>
        FAST_TARGETS.some((target) => {
            if (target === 'smoke') return key.startsWith('test/plans/smoke/')
            if (target.endsWith('.plan.toml')) return key === target
            return key === target || key.startsWith(`${target}/`)
        })
    for (const abs of walkPlans('test/plans') ?? []) {
        const key = rel(abs)
        if (slowSet.has(key) || forbiddenSet.has(key)) continue
        const plan = parseTomlFile(abs)
        const planTier = plan.tier ?? 'fast'
        if (planTier === 'debug') continue
        const slowTests = (plan.tests ?? []).filter((test) => (test.tier ?? planTier) === 'slow').length
        if (!fastReaches(key)) {
            problems.push(
                `${key}: no layer reaches it (not a fast target, a SLOW_PLANS file, FORBIDDEN, or a debug plan); ` +
                    `its ${plan.tests?.length ?? 0} test(s), ${slowTests} slow, are UNKNOWN, not green`,
            )
        } else if (slowTests > 0) {
            problems.push(
                `${key}: ${slowTests} slow test(s) but the fast layer runs only its fast tests and SLOW_PLANS ` +
                    'does not name it; those tests are UNKNOWN, not green',
            )
        }
    }
    return problems
}

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

function findCamel() {
    const homes = [process.env.CAMEL_HOME, path.join(ROOT, 'out', 'latest')].filter(Boolean)
    for (const home of homes) {
        for (const name of ['camel', 'camel.exe']) {
            const candidate = path.join(home, 'bin', name)
            if (fs.existsSync(candidate)) return candidate
        }
    }
    return null
}

// The gate never builds, and it never runs against an install it cannot prove: H149 found
// `-NoBuild` silently reusing another revision's `out/latest`, and a gate that measured that
// binary would report a tree nobody tested. `scripts/build-info.js check` compares the stamp's
// content fingerprint with this tree (docs/, test/, scripts/ and benchmarks/ do not move it, so
// a docs-only commit still passes); exit 4 there is the refusal. An install outside `out/`
// carries no stamp this script can find, and the gate says so instead of pretending.
function checkInstallStamp(camel) {
    const home = path.dirname(path.dirname(camel))
    const relative = path.relative(path.join(ROOT, 'out'), home)
    if (relative.startsWith('..') || path.isAbsolute(relative)) {
        console.log('[gate] build stamp: the install is outside out/, so its stamp cannot be checked; the caller owns staleness')
        return
    }
    const tag = relative.split(path.sep)[0]
    const check = spawnSync(process.execPath, ['scripts/build-info.js', 'check', '--tag', tag], {
        cwd: ROOT,
        encoding: 'utf8',
        windowsHide: true,
    })
    if (check.status === 0) {
        console.log(`[gate] build stamp: out/${tag} was built from this tree's sources`)
        return
    }
    const detail = `${check.stdout || ''}${check.stderr || ''}`.trim()
    console.error(detail ? `[gate] ${detail.replace(/\n/g, '\n[gate] ')}` : '[gate] build-info check failed')
    die(
        `the install at out/${tag} was not built from this tree (build-info exit ${check.status}).\n` +
            '[gate] build once (`npm run build`), then run the gate; it never builds and never assumes.'
    )
}

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

// The slow tests the skip list covers, from the plans themselves: the printed accounting follows
// the tree, so a plan that grows a slow test is counted the day it lands (it used to print the
// literal 200 H154 §2 measured).
function forbiddenSlowTests() {
    let total = 0
    for (const entry of FORBIDDEN) {
        const file = path.join(ROOT, entry.file)
        if (!fs.existsSync(file)) continue
        const plan = parseTomlFile(file)
        const planTier = plan.tier ?? 'fast'
        total += (plan.tests ?? []).filter((test) => (test.tier ?? planTier) === 'slow').length
    }
    return total
}

function printSkipList() {
    console.log(`[gate] skipped by policy: ${FORBIDDEN.length} plan files, ${forbiddenSlowTests()} slow cases`)
    console.log(`[gate]   ${POLICY}`)
    for (const entry of FORBIDDEN) console.log(`[gate]   ${entry.file} -- ${entry.reason}`)
    console.log('[gate]   These cases are UNKNOWN, not green. This gate never runs them; the names are printed')
    console.log('[gate]   so the next reader does not mistake the fast layer for the whole tree.')
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

async function runStaticPhase() {
    console.log(`\n[gate] ===== static checks (${STATIC_CHECKS.length}) =====`)
    const results = []
    for (let i = 0; i < STATIC_CHECKS.length; i++) {
        const check = STATIC_CHECKS[i]
        console.log(`[gate] static ${i + 1}/${STATIC_CHECKS.length}: ${check.name}`)
        console.log(`[gate] $ ${[process.execPath, ...check.args].join(' ')}`)
        const result = await runStreamed([process.execPath, ...check.args])
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
    for (const file of SLOW_PLANS) {
        if (!fs.existsSync(path.join(ROOT, file))) die(`the slow plan '${file}' does not exist; the gate list is stale`, 1)
    }
    const inventoryProblems = planInventoryProblems()
    if (inventoryProblems.length > 0) {
        for (const problem of inventoryProblems) die(problem, 1)
    }

    const identity = treeIdentity()
    console.log('[gate] Camel release gate')
    console.log(`[gate] tree: ${identity.head} (${identity.branch}, ${identity.dirty ? `${identity.dirty} dirty file(s)` : 'clean'})`)
    console.log(`[gate] layers: ${layers.join(', ')}${partial ? ' -- PARTIAL RUN, not a gate result' : ''}`)
    if (partial) console.log('[gate] PARTIAL: a layer did not run; the missing results are unknown, not green.')

    const fastArgv = [...FAST_TARGETS, '--tier', 'fast', '--jobs', String(options.jobs)]
    // A caller may name a plan the H154 list already has; report it as extra only when it is new.
    const extra = extraSlow.filter((file) => !SLOW_PLANS.includes(file))
    const slowPlans = [...SLOW_PLANS, ...extra]
    const slowArgv = [...slowPlans, '--jobs', String(options.jobs)]

    console.log('\n[gate] coverage')
    console.log(`[gate]   fast: ${FAST_TARGETS.join(' ')} (--tier fast: slow tests of those directories are the slow layer's)`)
    console.log(`[gate]   slow: ${slowPlans.length} named plan file(s), in full -- naming the file is what makes a slow test run`)
    if (extra.length > 0) console.log(`[gate]   extra: ${extra.join(', ')} (caller-requested, not in the H154 §3 list)`)
    console.log('[gate]   the slow files that are also fast plans are run in full, so their fast tests appear in both phases')
    printSkipList()

    if (options.dryRun) {
        console.log('\n[gate] dry run: the commands')
        if (layers.includes('fast')) console.log(`[gate] $ ${[process.execPath, 'scripts/test.js', ...fastArgv].join(' ')}`)
        if (layers.includes('slow')) for (const plan of slowPlans) console.log(`[gate] $ ${[process.execPath, 'scripts/test.js', plan].join(' ')}`)
        if (layers.includes('static')) for (const check of STATIC_CHECKS) console.log(`[gate] $ ${[process.execPath, ...check.args].join(' ')}`)
        console.log('[gate] dry run: nothing ran')
        return 0
    }

    const camel = findCamel()
    if (!camel) {
        die(
            'no built Camel: the gate verifies an existing install and never builds.\n' +
                '[gate] build once (`npm run build`, or camel-remote.ps1 without -Plan), then run the gate.'
        )
    }
    console.log(`[gate] compiler: ${camel}`)
    checkInstallStamp(camel)

    const phases = []
    if (layers.includes('fast')) {
        phases.push(await runPlanPhase('fast layer', ['scripts/test.js', ...fastArgv]))
    }
    if (layers.includes('slow')) {
        phases.push(await runPlanPhase('slow layer', ['scripts/test.js', ...slowArgv]))
    }
    if (layers.includes('static')) {
        phases.push(await runStaticPhase())
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
    console.log(`[gate] skipped   ${FORBIDDEN.length} plan files / ${forbiddenSlowTests()} slow cases (policy; UNKNOWN, not green)`)
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
