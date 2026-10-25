#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The plan inventory: the tables that say what happens to a plan file, and the check that every
// plan file of the tree belongs to one of the four fates.
//
// Every plan file belongs to one of four fates: a fast target runs its fast tests, a SLOW_PLANS
// entry runs it in full, FORBIDDEN names it as skipped by policy, or the debug layer is out of the
// release gate and says so. A plan outside those four would have tests that no layer runs and no
// line calls UNKNOWN; the list would keep saying "200 cases skipped" while the tree grew. This used
// to be unchecked (H185): a renamed FORBIDDEN file was printed as skipped, and a new slow plan
// under feat/ vanished from both layers.
//
// The tables and the check live here, in one place, because two readers need the same facts:
// `scripts/release-gate.mjs` imports them, and the merge gate's fast layer runs this file as an
// ordinary plan entry (`test/plans/feat/gate/plan_inventory.plan.toml`). A plan that landed without
// a fate used to redden only a release run, days later; the entry makes the commit that lands it
// red instead. The entry does not restate the check -- it runs this one.
//
//   node scripts/plan-inventory.mjs    print every problem at once, exit 1 when there is one

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { parseTomlFile } from '../test/tools/toml-lite.mjs'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

// The fast layer. `smoke` + `feat` are what `npm run test` walks; `gpu/profile` is fast-tier but
// under `gpu/`, which neither the fast target nor the slow list reaches -- it would otherwise be
// as invisible as the rest of `gpu/**` was before H154.
export const FAST_TARGETS = ['smoke', 'test/plans/feat', 'test/plans/gpu/profile.plan.toml']

// The slow layer, by explicit plan file (H154 §3). Fourteen files: six fast plans whose slow tests
// are runnable, five whole slow plans, and three perf plans the tree keeps gate-able -- the
// `para_speedup` plan the 2026-10-23 ruling made one, and the two count guards the 2026-10-25
// ruling split out of `perf.step_budget` and `perf.reduce_layout` because they assert counts and
// properties rather than durations. Every other slow test is in FORBIDDEN below.
export const SLOW_PLANS = [
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
    'test/plans/perf/para_speedup.plan.toml',
    'test/plans/perf/step_counts.plan.toml',
    'test/plans/perf/reduce_layout_counts.plan.toml',
]

// Never run. `agents/test.md`, General Policy: the full suite, the GC poison sweep, the GC
// stress plans (`gc.collection_trees_*`, `gc_elder_*`), the progen random-program checks, the
// PyTorch comparisons and the perf/timeit plans are milestone-only -- by the coordinator, on a
// quiet machine -- and the shared host is not one. H154 §2 measured 200 cases here.
//
// A file in this list is named as a whole plan when the whole plan is excluded, and as a plan
// with excluded slow entries when only some of its tests are excluded: the runner cannot select
// a test by name, so naming such a file would start the forbidden tests too (H154 §2, the ONNX
// row). Both kinds are skipped entirely, which is why the list holds 26 entries (23 whole plans
// and 3 plans with forbidden slow entries) for those 200 cases plus the perf cases that landed
// after that measurement.
export const FORBIDDEN = [
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
    // The remaining perf plans: a measurement whose numbers only mean anything beside the whole
    // sweep, recorded rather than gated (the 2026-10-25 ruling disposed of the eight that had no
    // fate: one to the slow layer, two split into a gate-able count guard and a measurement, five
    // here).
    { file: 'test/plans/perf/gemm_canonical_shapes.plan.toml', reason: 'a rate curve (GFLOP/s per shape and round): measured before a release, not in a gate' },
    { file: 'test/plans/perf/gemm_threads.plan.toml', reason: 'a thread-rate curve (GFLOP/s per thread count): measured before a release, not in a gate' },
    { file: 'test/plans/perf/instruction_counts.plan.toml', reason: 'needs a privileged container and `perf stat` (`kernel.perf_event_paranoid` refuses an unprivileged perf)' },
    { file: 'test/plans/perf/instruction_counts_load.plan.toml', reason: 'the same privileged `perf` measurement, and it induces the load it reads (96 spinners, 600 s)' },
    { file: 'test/plans/perf/showcase_d4.plan.toml', reason: 'a line-by-line comparison with the Python counterpart: a measurement, like the showcase plans above' },
    { file: 'test/plans/perf/step_budget.plan.toml', reason: 'the measured half: the step budget and its profile (the count guards are perf.step_counts, in SLOW_PLANS)' },
    { file: 'test/plans/perf/reduce_layout.plan.toml', reason: 'the measured half: the per-operator profile (the count guard is perf.reduce_layout_counts, in SLOW_PLANS)' },
    // GC stress and poison: every case sets CAMEL_GC_STRESS_*, CAMEL_GC_POISON or CAMEL_GC_VERIFY.
    { file: 'test/plans/feat/mm/gc.plan.toml', reason: 'GC stress + poison (`CAMEL_GC_STRESS_*`, `CAMEL_GC_POISON=1`)' },
    { file: 'test/plans/feat/mm/gc_collections.plan.toml', reason: 'collection stress (`CAMEL_GC_STRESS_ALLOC` + `CAMEL_GC_VERIFY=both`)' },
    { file: 'test/plans/feat/mm/gc_elder_exhaustion.plan.toml', reason: 'elder-generation exhaustion (`gc_elder_*`; agents/test.md)' },
    { file: 'test/plans/feat/opt/progen.plan.toml', reason: 'progen random-program check (agents/test.md)' },
    { file: 'test/plans/gpu/tiles_regions.plan.toml', reason: 'region run under GC stress (`CAMEL_GC_STRESS_SAFEPOINT=1` + `CAMEL_GC_VERIFY`)' },
    // Files whose slow entries are forbidden; the whole file is skipped (the runner cannot select
    // a test by name, so naming the file would start the forbidden tests).
    { file: 'test/plans/feat/modules/autodiff.plan.toml', reason: 'its 5 slow tests compare against PyTorch (`*_vs_torch64`)' },
    {
        file: 'test/plans/feat/modules/onnx.plan.toml',
        reason: 'its 4 slow tests: 2 PyTorch comparisons + 2 that cannot be selected apart from them (H154 §2)',
    },
    { file: 'test/plans/feat/linear/jit_framed.plan.toml', reason: 'its 2 slow tests are GC stress (`CAMEL_GC_STRESS_SAFEPOINT` + `CAMEL_GC_VERIFY`)' },
]

export function rel(abs) {
    return path.relative(ROOT, abs).split(path.sep).join('/')
}

export function walkPlans(entry) {
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

// Every problem, not the first: the caller prints the whole list, so a reader sees all the plan
// files that have no fate in one run instead of fixing one per run (H500).
export function planInventoryProblems() {
    const problems = []
    for (const entry of FORBIDDEN) {
        if (!fs.existsSync(path.join(ROOT, entry.file))) {
            problems.push(`the forbidden plan '${entry.file}' does not exist; the skip list is stale`)
        }
    }
    for (const file of SLOW_PLANS) {
        if (!fs.existsSync(path.join(ROOT, file))) {
            problems.push(`the slow plan '${file}' does not exist; the slow list is stale`)
        }
    }
    const planFiles = walkPlans('test/plans') ?? []
    if (planFiles.length === 0) {
        problems.push('test/plans holds no *.plan.toml: the inventory examined nothing, which is not a pass (H185)')
        return problems
    }
    const slowSet = new Set(SLOW_PLANS)
    const forbiddenSet = new Set(FORBIDDEN.map((entry) => entry.file))
    const fastReaches = (key) =>
        FAST_TARGETS.some((target) => {
            if (target === 'smoke') return key.startsWith('test/plans/smoke/')
            if (target.endsWith('.plan.toml')) return key === target
            return key === target || key.startsWith(`${target}/`)
        })
    for (const abs of planFiles) {
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

// --- the case ledger: the same accounting, extended from plan files to cases ------------------
//
// The fates above say what happens to every plan file; this says what happens to every file under
// `test/cases`. A case no plan reaches is a file nothing reads: it cannot fail, so a change that
// breaks it is invisible while the tree keeps paying for it. That is not hypothetical -- a
// one-off measurement workload stayed in the tree for months, was never run, and had gone wrong.
//
// A file under `test/cases` counts as read when one of these holds:
//
//   * a plan names it: a test's `case`, `verify_script_path`, `compare_to`, `golden`, an argument
//     or an environment value resolves to the file, or to a directory holding it. A value also
//     resolves against the test's own case directory, because a module root (`-I ../roots/one`)
//     is written relative to the program that reads it;
//   * a plan names the module of the file rather than its path: a pass or a definition library is
//     loaded by name (`no_pass::run`), and the search starts at the entry program's directory, so
//     `<case directory>/<module>.cml` is that file;
//   * some file of the tree names the path: a fragment of two or more segments that ends at the
//     file (`trans/cml_canon_name_a.cml` in a harness, the whole path in a golden or a document),
//     or a directory two or more levels below `test/cases` read as a fixture
//     (`test/cases/modules/nn/fixtures/spatial`);
//   * a case reads it: an `import` of another case, transitively, over the module roots an import
//     searches; a harness beside its program, or in the directory above it, names the program
//     (`fixture('fstring.cml')`); or a harness and a case share a stem (`port_fill_metadata.py`
//     reads `port_fill_metadata.cml`, the same convention `profile_alloc.py` states outright).
//
// A file the check cannot reach and the tree keeps anyway is recorded in CASE_EXCEPTIONS with the
// reason, and the run prints how many were recorded, so an exception is visible in the gate's
// output instead of being a silent pass. A plan file under `test/cases` is not a case: it must be
// recorded too, or the check asks for it to move under `test/plans`.
export const CASE_EXCEPTIONS = [
    {
        file: 'test/cases/compile/array_alias/array_alias.plan.toml',
        reason:
            'a plan file, not a case: the file itself says it belongs in test/plans/feat/compile/ ' +
            'and lives here only until the tensor-alias flip moves it. No plan can name a plan, so ' +
            'no reference can reach it; until it moves, no layer runs its 8 tests and they are ' +
            'known failures, not green',
    },
    {
        file: 'test/cases/perf/gc/build_tree.cml',
        reason:
            'a workload of the manual GC instrument (test/tools/gcbench/gcbench.py, ' +
            'test/tools/perfbaseline/baseline.py): no plan and no case reads it, the instrument is ' +
            'what runs it, and its measurement is written up in docs/performance/graph-store-baseline.md',
    },
    {
        file: 'test/cases/perf/gc/fib.cml',
        reason:
            'a workload of the manual GC instrument (test/tools/gcbench/gcbench.py): no plan and no ' +
            'case reads it; benchmarks/pipeline/exclusions.json keeps it out of the benchmark pipeline too',
    },
    {
        file: 'test/cases/perf/gc/jit_const.cml',
        reason:
            'a workload of the manual GC instrument (test/tools/gcbench/gcbench.py): no plan and no ' +
            'case reads it, and its measurement is written up in docs/performance/graph-store-baseline.md',
    },
    {
        file: 'test/cases/perf/gc/map_chain.cml',
        reason:
            'a workload of the manual GC instrument (test/tools/gcbench/gcbench.py): no plan and no ' +
            'case reads it, and its measurement is written up in docs/performance/graph-store-baseline.md',
    },
    {
        file: 'test/cases/para/core/block_sort_merge.cml',
        reason:
            'a workload of the manual GC instrument (test/tools/gcbench/gcbench.py): no plan and no ' +
            'case reads it, the instrument is what runs it',
    },
]

const CASE_ROOT = 'test/cases'
const CASE_REFERENCE_KEYS = new Set(['case', 'verify_script_path', 'compare_to', 'golden', 'args', 'envs', 'plans'])

// Where a reference counts: the compiler, the standard library, the modules, the tools, the tests
// and the scripts -- the paths a gate or a plan reads a case through. A document or a report that
// *describes* a measurement is not a reader of the workload it measured, and neither is the
// benchmark pipeline, which is a harness of its own outside the plans.
const NAMING_ROOTS = ['test/', 'scripts/', 'src/', 'include/', 'stdlib/', 'modules/', 'tools/', 'examples/', 'exemplars/']

function walkTreeFiles(dir) {
    const out = []
    const walk = (abs, prefix) => {
        for (const name of fs.readdirSync(abs).sort()) {
            const child = `${abs}/${name}`
            if (fs.statSync(child).isDirectory()) walk(child, `${prefix}/${name}`)
            else out.push(`${prefix}/${name}`)
        }
    }
    walk(path.join(ROOT, dir), dir)
    return out
}

// A path under `test/cases`, written the way the plans write it: `path.resolve` and
// `path.relative` are the two slowest calls in the check on Windows (they normalize, check
// reserved names and split drives for every candidate), and a plan value is already relative to
// the plan's directory, so `/` and `..` can be resolved by hand. The same holds for the
// directory and the name of a path, which this file needs a few hundred thousand times.
function underCases(base, value) {
    const out = []
    for (const part of `${base}/${value}`.split('/')) {
        if (part === '' || part === '.') continue
        if (part === '..') out.pop()
        else out.push(part)
    }
    return out.join('/')
}

const dirOf = (p) => {
    const i = p.lastIndexOf('/')
    return i < 0 ? '' : p.slice(0, i)
}

const baseOf = (p) => p.slice(p.lastIndexOf('/') + 1)

function readText(file) {
    const buf = fs.readFileSync(file)
    return buf.includes(0) ? null : buf.toString('utf8')
}

// A case tree carries tensors, bytecode images and models beside its source, and the check reads
// text: a name inside a binary is not a reference to anything, and reading megabytes of them per
// run costs the fast layer more than the whole check.
const BINARY_SUFFIX =
    /\.(npy|npz|npyc|cmo|onnx|pb|pt|pth|pkl|pickle|h5|safetensors|tflite|caffemodel|bin|dat|so|dll|exe|o|a|lib|wasm|zip|gz|tar|pdf|png|jpe?g|gif|bmp|tiff|ico|woff2?|mp3|wav|mp4|mov|arrow|parquet)$/i

function referenceStrings(node, out = []) {
    if (Array.isArray(node)) {
        for (const value of node) referenceStrings(value, out)
        return out
    }
    if (node && typeof node === 'object') {
        for (const [key, value] of Object.entries(node)) {
            if (CASE_REFERENCE_KEYS.has(key)) {
                if (typeof value === 'string') out.push(value)
                else if (Array.isArray(value)) for (const item of value) if (typeof item === 'string') out.push(item)
            } else referenceStrings(value, out)
        }
    }
    return out
}

// Every problem, not the first, for the same reason as the fates above: one run, the whole list.
export function caseLedgerProblems() {
    const problems = []
    const caseRootAbs = path.join(ROOT, CASE_ROOT)
    if (!fs.existsSync(caseRootAbs)) {
        return [`${CASE_ROOT} does not exist: the case ledger examined nothing, which is not a pass`]
    }
    const recorded = new Set(CASE_EXCEPTIONS.map((entry) => entry.file))
    for (const entry of CASE_EXCEPTIONS) {
        if (!fs.existsSync(path.join(ROOT, entry.file))) {
            problems.push(`the recorded case '${entry.file}' does not exist; the exception list is stale`)
        }
    }

    const listed = spawnSync('git', ['ls-files', '-z'], { cwd: ROOT, encoding: 'utf8', maxBuffer: 1 << 28 })
    if (listed.error || listed.status !== 0) {
        return [
            'the case ledger could not list the tree (git ls-files failed): the check examined nothing, ' +
                'which is not a pass',
        ]
    }
    const tracked = listed.stdout.split('\0').filter(Boolean)

    const files = walkTreeFiles(CASE_ROOT).sort()
    const caseFiles = files.filter((file) => !file.endsWith('.plan.toml'))
    const treePlans = files.filter((file) => file.endsWith('.plan.toml'))
    if (caseFiles.length === 0) {
        return [`${CASE_ROOT} holds no case file: the case ledger examined nothing, which is not a pass`]
    }
    const caseSet = new Set(caseFiles)
    // The tree is read once, from this file list, and never from the disk again: a stat per plan
    // value per base per candidate is hundreds of thousands of system calls, and on Windows that
    // alone costs the fast layer half a minute.
    const fileSet = new Set(files)
    const below = new Map()
    for (const file of files) {
        let dir = dirOf(file)
        while (dir.includes('/')) {
            if (!below.has(dir)) below.set(dir, [])
            below.get(dir).push(file)
            dir = dirOf(dir)
        }
    }

    // --- what a plan names ------------------------------------------------------------------
    const reached = new Map()
    const mark = (file, who) => {
        if (caseSet.has(file) && !reached.has(file)) reached.set(file, who)
    }
    for (const plan of tracked.filter((file) => file.endsWith('.plan.toml'))) {
        const abs = `${ROOT}/${plan}`
        let doc
        try {
            doc = parseTomlFile(abs)
        } catch {
            continue
        }
        const planDir = dirOf(plan)
        const tests = Array.isArray(doc.tests) ? doc.tests : []
        const caseDirs = []
        for (const test of tests) {
            if (typeof test?.case !== 'string' || test.case.includes('${')) continue
            const target = underCases(planDir, test.case)
            caseDirs.push(fileSet.has(target) ? dirOf(target) : target)
        }
        for (const value of referenceStrings(doc)) {
            if (!value || value.includes('${') || value.startsWith('http')) continue
            for (const base of [planDir, ...caseDirs]) {
                for (const candidate of [value, `${value}.cml`]) {
                    const key = underCases(base, candidate)
                    if (!key.startsWith(`${CASE_ROOT}/`)) continue
                    // naming a directory reads every file it holds (a fixture directory, a module
                    // root the test compiles against)
                    if (below.has(key)) {
                        for (const child of below.get(key)) mark(child, plan)
                    } else mark(key, plan)
                }
            }
        }
        // a pass or a definition library named as `module::member`, or quoted by name (`'broken_definitions'
        // does not compile`), and the module sits beside the program the test runs
        const text = readText(abs)
        if (text === null) continue
        const modules = new Set([
            ...[...text.matchAll(/([A-Za-z_][A-Za-z0-9_]*)\s*::/g)].map((match) => match[1]),
            ...[...text.matchAll(/['"]([A-Za-z_][A-Za-z0-9_]*)['"]/g)].map((match) => match[1]),
        ])
        for (const dir of caseDirs) {
            for (const module of modules) mark(`${dir}/${module}.cml`, `${plan} (a module it names)`)
        }
    }

    // --- what the rest of the tree names, and what a harness beside a case reads -------------
    const suffix = new Map()
    const dirSuffix = new Map()
    const near = new Map()
    const push = (map, key, value) => {
        if (!map.has(key)) map.set(key, [])
        map.get(key).push(value)
    }
    for (const file of caseFiles) {
        const parts = file.split('/')
        for (let i = 0; i < parts.length; i++) push(suffix, parts.slice(i).join('/'), file)
        for (let i = 0; i < parts.length - 1; i++) {
            for (let j = i + 1; j < parts.length - 1; j++) push(dirSuffix, parts.slice(i, j + 1).join('/'), file)
        }
        const dir = dirOf(file)
        push(near, dir, file)
        push(near, dirOf(dir), file)
    }
    const TOKEN = /[A-Za-z0-9_.\-]+(?:\/[A-Za-z0-9_.\-]+)+/g
    for (const file of tracked) {
        if (!NAMING_ROOTS.some((root) => file.startsWith(root))) continue
        // the exception table below names the files it records; the ledger is not a reader of them
        if (file === 'scripts/plan-inventory.mjs') continue
        if (BINARY_SUFFIX.test(file)) continue
        const text = readText(`${ROOT}/${file}`)
        if (text === null) continue
        for (const candidate of near.get(dirOf(file)) ?? []) {
            if (text.includes(baseOf(candidate))) mark(candidate, file)
        }
        for (const match of text.matchAll(TOKEN)) {
            const token = match[0].replace(/^[.\-/]+/, '').replace(/[.\-/]+$/, '')
            for (const candidate of suffix.get(token) ?? []) mark(candidate, file)
            // a fixture directory a harness reads: naming the whole case tree, or one of its
            // top-level directories, is not a reference to any particular case
            if (token.startsWith(`${CASE_ROOT}/`) && token.split('/').length >= 4) {
                for (const candidate of dirSuffix.get(token) ?? []) mark(candidate, file)
            }
        }
    }

    // --- what a reached case reads: imports, and a harness that shares its stem --------------
    const edges = new Map()
    const addImport = (from, target) => {
        if (target === from || !caseSet.has(target)) return
        if (!edges.has(from)) edges.set(from, new Set())
        edges.get(from).add(target)
    }
    for (const file of caseFiles) {
        if (!file.endsWith('.cml')) continue
        const text = readText(`${ROOT}/${file}`)
        if (text === null) continue
        for (const line of text.split('\n')) {
            const match = /^\s*import\s+(?:\{[^}]*\}\s+from\s+)?([A-Za-z0-9_./]+)/.exec(line)
            if (!match) continue
            let base = dirOf(file)
            const rest = []
            for (const segment of match[1].split('/')) {
                if (/^\.+$/.test(segment)) {
                    for (let i = 1; i < segment.length; i++) base = dirOf(base)
                } else rest.push(segment.replace(/\./g, '/'))
            }
            if (!rest.length) continue
            for (const root of moduleRoots(base)) {
                // a dot inside a name is a separator (`import a.one`), so the joined form can hold
                // an empty segment: `import .x` is the name `x` in the program's own directory
                const stem = `${root}/${rest.join('/')}`.split('/').filter(Boolean).join('/')
                addImport(file, `${stem}.cml`)
                addImport(file, `${stem}/index.cml`)
            }
        }
    }
    const stemOf = (file) => file.replace(/\.[A-Za-z0-9]+$/, '.cml')
    let grew = true
    while (grew) {
        grew = false
        for (const [from, targets] of edges) {
            if (!reached.has(from)) continue
            for (const target of targets) {
                if (!reached.has(target)) {
                    mark(target, `${from} imports it`)
                    grew = true
                }
            }
        }
        for (const file of [...reached.keys()]) {
            if (file.endsWith('.cml')) continue
            const stem = stemOf(file)
            if (caseSet.has(stem) && !reached.has(stem)) {
                mark(stem, `${file} is the harness beside it`)
                grew = true
            }
        }
    }

    // --- the verdict ------------------------------------------------------------------------
    for (const file of [...treePlans, ...caseFiles]) {
        if (reached.has(file)) {
            if (recorded.has(file)) {
                problems.push(
                    `${file}: recorded in CASE_EXCEPTIONS, but ${reached.get(file)} reads it now; remove the entry`,
                )
            }
            continue
        }
        if (recorded.has(file)) continue
        if (file.endsWith('.plan.toml')) {
            problems.push(
                `${file}: a plan file under ${CASE_ROOT} belongs under test/plans (no plan can name a plan, ` +
                    'so nothing reaches it); move it, or record it in CASE_EXCEPTIONS with the reason',
            )
        } else {
            problems.push(
                `${file}: no plan and no other file of the tree reads it (not a test's case, not a module a ` +
                    'plan loads by name, not imported by a reached case, not named by a harness or a fixture ' +
                    'directory), and CASE_EXCEPTIONS does not record it; delete it, or say what it locks',
            )
        }
    }
    return problems
}

// A module name is looked up in the importing file's directory and then in each directory above it
// (the module roots): `import ../lib`, `import a.one` and a bare `import lib` all land somewhere.
function moduleRoots(from) {
    const roots = [from]
    let up = from
    while (up.includes('/')) {
        up = dirOf(up)
        roots.push(up)
    }
    return roots
}

// The merge gate's fast layer runs this file as a plan entry, and a reader can run it by hand: the
// same checks the release gate starts with, without an install and without a layer.
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const fateProblems = planInventoryProblems()
    const caseProblems = caseLedgerProblems()
    const problems = [...fateProblems, ...caseProblems]
    const examined = (walkPlans('test/plans') ?? []).length
    for (const problem of problems) console.error(`plan-inventory: ${problem}`)
    if (problems.length > 0) {
        console.error(
            `plan-inventory: ${problems.length} problem(s): every plan file must be reached by a fast target, ` +
                'named in SLOW_PLANS, named in FORBIDDEN, or be a debug plan; and every file under test/cases ' +
                'must be read by a plan either directly or through a module, an import or a harness, or be ' +
                'recorded in CASE_EXCEPTIONS with the reason it is kept',
        )
        process.exit(1)
    }
    console.log(`plan-inventory: ${examined} plan files, every one of them has a fate`)
    const cases = walkTreeFiles(CASE_ROOT).filter((file) => !file.endsWith('.plan.toml')).length
    console.log(
        `plan-inventory: ${cases} case files, every one of them is read by a plan or recorded with a reason ` +
            `(${CASE_EXCEPTIONS.length} recorded)`,
    )
}