#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The plan inventory: the tables that say what layer a plan file belongs to, and the check that
// every plan file of the tree belongs to one.
//
// Every plan file belongs to one of three layers, or to the debug layer: a fast target runs its
// fast tests, a SLOW_PLANS entry runs it in full, FORBIDDEN names it as the milestone layer's --
// a plan whose own file says `tier = "milestone"` -- or the debug layer is out of the release gate
// and says so. A plan outside those would have tests that no layer runs and no line calls UNKNOWN;
// the list would keep printing one number while the tree grew. This used to be unchecked: a
// renamed FORBIDDEN file was printed as skipped, and a new slow plan under feat/ vanished from both
// layers.
//
// The tables and the check live here, in one place, because two readers need the same facts:
// `scripts/release-gate.mjs` imports them, and the merge gate's fast layer runs this file as an
// ordinary plan entry (`test/plans/feat/gate/plan_inventory.plan.toml`). A plan that landed without
// a layer used to redden only a release run, days later; the entry makes the commit that lands it
// red instead. The entry does not restate the check -- it runs this one.
//
//   node scripts/plan-inventory.mjs    print every problem at once, exit 1 when there is one

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
// The runner's own reading of a plan file. The inventory must see the tests a run selects,
// including the copies a `[[sweeps]]` entry generates, so it asks the runner instead of turning the
// same plan file into tests a second time: two readings of one file drift, and the drift is silent.
import { expandSweeps, loadPlanTests, testTier } from '../test/tools/run-tests.mjs'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

// The fast layer. `smoke` + `feat` are what `npm run test` walks; `gpu/profile` and `gpu/reading`
// are fast-tier but under `gpu/`, which neither the fast target nor the slow list reaches -- they
// would otherwise be as invisible as the rest of `gpu/**` was before the slow layer existed.
export const FAST_TARGETS = [
    'smoke',
    'test/plans/feat',
    'test/plans/gpu/profile.plan.toml',
    'test/plans/gpu/reading.plan.toml',
]

// The slow layer, by explicit plan file. Fourteen files: six fast plans whose slow tests
// are runnable, five whole slow plans, and three perf plans the tree keeps gate-able -- the
// `para_speedup` plan the 2026-10-23 rule made one, and the two count guards the 2026-10-25
// rule split out of `perf.step_budget` and `perf.reduce_layout` because they assert counts and
// properties rather than durations. A `slow` test outside these files is a problem this check
// reports: the fast layer would drop it and no other layer would run it.
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

// The milestone layer. `agents/test.md`, General Policy: the full suite, the GC poison sweep, the
// GC stress plans (`gc.collection_trees_*`, `gc_elder_*`), the progen random-program checks, the
// PyTorch comparisons and the perf/timeit plans are milestone-only -- by the coordinator, on a
// quiet machine -- and the shared host is not one. Each plan file named here says
// `tier = "milestone"` in its own file (the tests it holds back do too, in the three files that
// hold back only some), so one word names the layer: `--tier milestone` selects exactly this batch
// plus the unsampled copies of a sweep, and `--tier slow` never touches it.
//
// A file in this list is named as a whole plan when the whole plan is excluded, and as a plan
// whose milestone entries are excluded when only some of its tests are: the runner cannot select
// a test by name, so naming such a file would start the excluded tests too (the ONNX row). The
// gate skips the file entirely either way, which is why the list holds 25 entries (22 whole plans
// and 3 plans with milestone entries). How many cases that is, the gate counts through the runner's
// own reading of these files (`loadPlanTests`, `expandSweeps` and `testTier`, in its
// `forbiddenMilestoneTests`) and prints on every run; a count read off the table rows would be a
// different number, because a row with `backends` is one case there and one test per back end to
// the runner, and only the runner's number is the number of cases `--tier milestone` would run.
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
    // sweep, recorded rather than gated (the 2026-10-25 rule disposed of the eight that had no
    // layer: one to the slow layer, two split into a gate-able count guard and a measurement, five
    // here).
    { file: 'test/plans/perf/gemm_canonical_shapes.plan.toml', reason: 'a rate curve (GFLOP/s per shape and round): measured before a release, not in a gate' },
    { file: 'test/plans/perf/gemm_threads.plan.toml', reason: 'a thread-rate curve (GFLOP/s per thread count): measured before a release, not in a gate' },
    { file: 'test/plans/perf/instruction_counts.plan.toml', reason: 'needs a privileged container and `perf stat` (`kernel.perf_event_paranoid` refuses an unprivileged perf)' },
    { file: 'test/plans/perf/instruction_counts_load.plan.toml', reason: 'the same privileged `perf` measurement, and it induces the load it reads (96 spinners, 600 s)' },
    { file: 'test/plans/perf/showcase_d4.plan.toml', reason: 'a line-by-line comparison with the Python counterpart: a measurement, like the showcase plans above' },
    { file: 'test/plans/perf/step_budget.plan.toml', reason: 'the measured half: the step budget and its profile (the count guards are perf.step_counts, in SLOW_PLANS)' },
    { file: 'test/plans/perf/reduce_layout.plan.toml', reason: 'the measured half: the per-operator profile (the count guard is perf.reduce_layout_counts, in SLOW_PLANS)' },
    // GC stress and poison: every case passes --gc-stress, --gc-check poison or --gc-check verify.
    { file: 'test/plans/feat/mm/gc.plan.toml', reason: 'GC stress + poison (`--gc-stress`, `--gc-check poison`)' },
    { file: 'test/plans/feat/mm/gc_collections.plan.toml', reason: 'collection stress (`--gc-stress alloc` + `--gc-check verify=both`)' },
    { file: 'test/plans/feat/mm/gc_elder_exhaustion.plan.toml', reason: 'elder-generation exhaustion (`gc_elder_*`; agents/test.md)' },
    { file: 'test/plans/feat/opt/progen.plan.toml', reason: 'progen random-program check (agents/test.md)' },
    { file: 'test/plans/gpu/tiles_regions.plan.toml', reason: 'region run under GC stress (`--gc-stress safepoint=1` + `--gc-check verify`)' },
    // Files whose milestone entries are held back; the whole file is skipped (the runner cannot
    // select a test by name, so naming the file would start the held-back tests).
    { file: 'test/plans/feat/modules/autodiff.plan.toml', reason: 'its 5 milestone tests compare against PyTorch (`*_vs_torch64`)' },
    {
        file: 'test/plans/feat/modules/onnx.plan.toml',
        reason: 'its 4 milestone tests: 2 PyTorch comparisons + 2 that cannot be selected apart from them',
    },
    { file: 'test/plans/feat/linear/jit_framed.plan.toml', reason: 'its 2 milestone tests are GC stress (`--gc-stress safepoint` + `--gc-check verify`)' },
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
// files that have no layer in one run instead of fixing one per run.
export function planInventoryProblems() {
    const problems = []
    for (const entry of FORBIDDEN) {
        if (!fs.existsSync(path.join(ROOT, entry.file))) {
            problems.push(`the milestone plan '${entry.file}' does not exist; the list is stale`)
        }
    }
    for (const file of SLOW_PLANS) {
        if (!fs.existsSync(path.join(ROOT, file))) {
            problems.push(`the slow plan '${file}' does not exist; the slow list is stale`)
        }
    }
    const planFiles = walkPlans('test/plans') ?? []
    if (planFiles.length === 0) {
        problems.push('test/plans holds no *.plan.toml: the inventory examined nothing, which is not a pass')
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
        const plan = readPlan(abs)
        if (plan.error) {
            problems.push(`${key}: the runner cannot read it (${plan.error}); a plan file with no reading has no layer`)
            continue
        }
        const planTier = plan.doc.tier ?? 'fast'
        // A plan whose own file names a layer only that layer walks -- the debug layer, the
        // milestone layer -- has its destination: the layer that selects it.
        if (planTier === 'debug' || planTier === 'milestone') continue
        // The runner's own rule for a test's layer, so a plan file that names a layer nobody knows
        // is a problem here, at the commit that lands it, and not a stack trace in a release run.
        let slowTests = 0
        let unknownTier = null
        for (const test of plan.tests) {
            try {
                if (testTier(plan.doc, test) === 'slow') slowTests += 1
            } catch (error) {
                unknownTier = error && error.message ? error.message : String(error)
            }
        }
        if (unknownTier) {
            problems.push(`${key}: ${unknownTier}`)
            continue
        }
        if (!fastReaches(key)) {
            problems.push(
                `${key}: no layer reaches it (not a fast target, a SLOW_PLANS file, FORBIDDEN, or a debug or ` +
                    `milestone plan); its ${plan.tests.length} test(s), ${slowTests} slow, are UNKNOWN, not green`,
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

/**
 * A plan file as the runner reads it: its own tests plus the copies its `[[sweeps]]` entries
 * generate. A plan the schema rejects, and a malformed sweep, redden the run itself; here they are
 * one problem carrying the reason, so the rest of the inventory still prints.
 */
function readPlan(abs) {
    try {
        const { doc, tests } = loadPlanTests(abs)
        return { doc, tests: [...tests, ...expandSweeps(doc, abs)] }
    } catch (error) {
        return { error: error && error.message ? error.message : String(error) }
    }
}

// --- the case ledger: the same accounting, extended from plan files to cases ------------------
//
// The layers above say what happens to every plan file; this says what happens to every file under
// `test/cases`. A case no plan reaches is a file nothing reads: it cannot fail, so a change that
// breaks it is invisible while the tree keeps paying for it. That is not hypothetical -- a
// one-off measurement workload stayed in the tree for months, was never run, and had gone wrong.
//
// A file under `test/cases` counts as read when one of these holds. Only code counts, and a file
// and a directory are judged by the same rule: a plan reads its values, an `import` reads another
// case, and tool code that runs opens what it names. A name in a comment, in a plan's `desc` or in
// an index's own listing is prose about a file, not a read of it -- the old rule counted a file
// named anywhere, comments included, while a directory needed code that opened it, which judged
// the same information two ways and kept files alive on a comment.
//
//   * a plan names it: a test's `case`, `verify_script_path`, `compare_to`, `golden`, an argument
//     or an environment value resolves to the file, or to a directory holding it. A value also
//     resolves against the test's own case directory, because a module root (`-I ../roots/one`)
//     is written relative to the program that reads it;
//   * a plan names the module of the file rather than its path: a pass or a definition library is
//     loaded by name (`no_pass::run`), and the search starts at the entry program's directory, so
//     `<case directory>/<module>.cml` is that file;
//   * code of the tree names the path: a fragment of two or more segments that ends at the file
//     (`trans/cml_canon_name_a.cml` in a harness), or a directory below `test/cases` that code
//     builds out of path segments and reads as a fixture
//     (`path.join(ROOT, 'test', 'cases', 'modules', 'std', 'env', 'unicode')`), which includes a
//     tool that holds `test/cases` in a variable and joins its workload onto that variable, and a
//     harness that hands the compiler a directory beside itself (`path.join(HERE, 'model')`);
//   * a case reads it: an `import` of another case, transitively, over the module roots an import
//     searches; a harness beside its program, or in the directory above it, names the program
//     (`fixture('fstring.cml')`); or a harness and a case share a stem (`port_fill_metadata.py`
//     reads `port_fill_metadata.cml`, the same convention `profile_alloc.py` states outright).
//
// A file the check cannot reach and the tree keeps anyway is recorded in CASE_EXCEPTIONS with the
// reason, and the run prints how many were recorded, so an exception is visible in the gate's
// output instead of being a silent pass. A plan file under `test/cases` is not a case: it must be
// recorded too, or the check asks for it to move under `test/plans`.
//
// The list holds one file, and one kind of file is left in the tree on purpose: a trap fixture,
// which a correct compiler never opens. Its whole function is to be there -- a regression that
// searches a directory it must not search finds it and prints its value, and the pins above never
// allow that value -- so no plan and no code reads it, and deleting it would remove the guard.
export const CASE_EXCEPTIONS = [
    {
        file: 'test/cases/compile/module_imports/import_order/roots_trap/pkg/lib.cml',
        reason:
            'a trap fixture: the entry directory is not a module root, and this file is what a ' +
            'regression that treats it as one would load (it returns 9, which the pins above never ' +
            'allow). Nothing opens it in a correct run, and its presence is the check',
    },
]

const CASE_ROOT = 'test/cases'
// A case is a file of the repository, and a bytecode cache is not: the interpreter writes
// `__pycache__` beside a Python module it imports, it is in `.gitignore` (so no commit carries
// one), and it appears and disappears with whoever last imported the module -- including in a
// checkout a gate reuses, where it survives `git clean -fd` because it is ignored. The walk below
// reads the filesystem, so it sees such a file; the thing that reads it is the interpreter that
// wrote it, which is not a plan. Interpreter caches that are hidden directories (`.pytest_cache`,
// `.mypy_cache`) are already skipped with the hidden files, and no other directory of this kind
// exists in the tree.
// A `tmp` directory is a test's scratch space (a dataset a case downloads, a plot it writes): no
// commit carries one (`git ls-files` holds no path with a `tmp` segment), so it is not a case.
const PYTHON_CACHE = /(^|\/)(__pycache__|tmp)\//
const CASE_REFERENCE_KEYS = new Set(['case', 'verify_script_path', 'compare_to', 'golden', 'args', 'envs', 'plans'])

// A plan is an index of the cases it runs, and `test/plan-selection.json` is an index of the
// directories a change touches: neither reads a directory by naming it, only a harness, a program
// or a data file of one does (a run list that names the directory its programs live in, a test
// that names the fixture tree it loads).
const INDEX_FILES = new Set(['test/plan-selection.json'])

// Where a reference counts: the compiler, the standard library, the modules, the tools, the tests
// and the scripts -- the paths a gate or a plan reads a case through. A document or a report that
// *describes* a measurement is not a reader of the workload it measured, and neither is the
// benchmark pipeline, which is a harness of its own outside the plans.
const NAMING_ROOTS = ['test/', 'scripts/', 'src/', 'include/', 'stdlib/', 'modules/', 'tools/', 'examples/', 'exemplars/']

// What the runner writes while it runs (a million files after a few weeks of runs) is not part of
// the tree the ledger judges: the walk would read it all, and the check would grow with the
// history of the checkout instead of with the repository.
const RUN_OUTPUT_DIRS = new Set(['test/results', 'test/tmp', 'node_modules'])

function walkTreeFiles(dir) {
    const out = []
    const walk = (abs, prefix) => {
        for (const name of fs.readdirSync(abs).sort()) {
            const child = `${abs}/${name}`
            if (RUN_OUTPUT_DIRS.has(`${prefix}/${name}`) || name === 'node_modules') continue
            if (fs.statSync(child).isDirectory()) walk(child, `${prefix}/${name}`)
            else out.push(`${prefix}/${name}`)
        }
    }
    walk(path.join(ROOT, dir), dir)
    return out
}

// The files of `test/cases` that are cases: the walk above reads what the filesystem holds, and the
// ledger judges what the repository holds, so a hidden file (the directory's own bookkeeping) and a
// bytecode cache (the interpreter's) are not cases.
function caseTreeFiles() {
    return walkTreeFiles(CASE_ROOT)
        .filter((file) => !PYTHON_CACHE.test(file) && !baseOf(file).startsWith('.'))
        .sort()
}

// The files that can read a case: the tree under the naming roots, read from the filesystem. The
// repository's index is not the source. A change reaches a test host as a patch over the last
// commit, so there the index still names a moved file at the path it left and does not name a new
// one at all, and reading it either crashes on a path the tree no longer has or misses the file
// that now reads the cases. The ledger judges the tree, so the tree is what it walks.
function namingRootFiles() {
    return NAMING_ROOTS.flatMap((root) => walkTreeFiles(root.slice(0, -1)))
        .filter((file) => !PYTHON_CACHE.test(file) && !baseOf(file).startsWith('.'))
        .sort()
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

/**
 * The paths under `test/cases` that code builds out of segments: `CASES = path.join(ROOT, 'test',
 * 'cases')` and then `path.join(CASES, 'perf', 'gc', 'fib.cml')`. A tool that opens a workload
 * names it in exactly this way and nowhere else, so the literal segments of a join are followed
 * through the variable that holds `test/cases`. An identifier the walk cannot resolve (the
 * repository root itself) contributes no segment, which is what the single-call reading did: only
 * the part of the path that is written out as literals can be judged.
 */
function joinedCasePaths(code) {
    const held = new Map()
    const found = new Set()
    const segments = (args) => {
        const parts = []
        for (const raw of args.split(',')) {
            const arg = raw.trim().replace(/[)\];]+$/, '')
            const quoted = /^['"]([^'"]*)['"]$/.exec(arg)
            if (quoted) {
                parts.push(quoted[1])
                continue
            }
            const known = held.get(arg)
            if (known) parts.push(...known)
        }
        return parts
    }
    // The root itself reads nothing by being named: only a path below it does.
    const under = (parts) => {
        const joined = parts.join('/')
        if (joined === CASE_ROOT) return null
        const at = joined.indexOf(`${CASE_ROOT}/`)
        return at < 0 ? null : joined.slice(at)
    }
    for (const line of code.split('\n')) {
        for (const call of line.matchAll(/\.join\(([^)]*)\)/g)) {
            const joined = under(segments(call[1]))
            if (joined) found.add(joined)
        }
        const assign = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$/.exec(line)
        if (!assign) continue
        const call = /\.join\(([^)]*)\)/.exec(assign[2])
        if (!call) continue
        const parts = segments(call[1])
        const joined = parts.join('/')
        if (joined === CASE_ROOT) held.set(assign[1], ['test', 'cases'])
        else if (under(parts)) held.set(assign[1], joined.slice(joined.indexOf(`${CASE_ROOT}/`)).split('/'))
    }
    return found
}

// Every problem, not the first, for the same reason as the plan layers above: one run, the whole list.
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

    // Reading the tree is the only list this check needs: a path that is not in it is not a case
    // file and not a reader of one, and a file the tree has and no commit lists yet is a reader the
    // moment it is written (`namingRootFiles`).
    const tracked = namingRootFiles()

    const files = caseTreeFiles()
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
    // A plan's code: its values, without the prose fields. A plan reads a file through a value --
    // its `case`, an argument, an environment entry, a golden -- and a name inside `desc` or a note
    // is prose about the plan, not a read of anything.
    const PROSE_KEYS = new Set(['desc', 'known_fail_note', 'failure_note', 'comment'])
    const valueText = (node, out) => {
        if (typeof node === 'string') out.push(node)
        else if (Array.isArray(node)) for (const item of node) valueText(item, out)
        else if (node && typeof node === 'object') {
            for (const [key, value] of Object.entries(node)) if (!PROSE_KEYS.has(key)) valueText(value, out)
        }
        return out
    }
    const planCode = new Map()
    for (const plan of tracked.filter((file) => file.endsWith('.plan.toml'))) {
        const read = readPlan(`${ROOT}/${plan}`)
        if (read.error) continue
        planCode.set(plan, valueText(read.doc, []).join('\n'))
        const planDir = dirOf(plan)
        const dirsOfTest = (test, base) => {
            if (typeof test?.case !== 'string' || test.case.includes('${')) return []
            const target = underCases(base, test.case)
            return [fileSet.has(target) ? dirOf(target) : target]
        }
        const ownDirs = []
        for (const test of read.tests) {
            if (test.__planPath && rel(test.__planPath) !== plan) continue
            ownDirs.push(...dirsOfTest(test, planDir))
        }
        const resolve = (base, value, dirs) => {
            if (!value || value.includes('${') || value.startsWith('http')) return
            // an environment entry or a `key=value` argument names what the program reads in the
            // part after the `=` (`CAMEL_PACKAGES=../packages` is how a case finds a package root)
            const spellings = [value]
            const assigned = /^[A-Za-z_][A-Za-z0-9_]*=(.+)$/.exec(value)
            if (assigned) spellings.push(assigned[1])
            for (const from of [base, ...dirs]) {
                for (const spelling of spellings) {
                    for (const candidate of [spelling, `${spelling}.cml`]) {
                        const key = underCases(from, candidate)
                        if (!key.startsWith(`${CASE_ROOT}/`)) continue
                        // naming a directory reads every file it holds (a fixture directory, a module
                        // root the test compiles against)
                        if (below.has(key)) {
                            for (const child of below.get(key)) mark(child, plan)
                        } else mark(key, plan)
                    }
                }
            }
        }
        for (const value of referenceStrings(read.doc)) resolve(planDir, value, ownDirs)
        // A copy a `[[sweeps]]` entry generates names the same cases as the plan it was copied
        // from, and its paths are relative to that plan's directory, not to this one.
        for (const test of read.tests) {
            const base = test.__planPath ? dirOf(rel(test.__planPath)) : planDir
            if (base === planDir) continue
            const dirs = dirsOfTest(test, base)
            for (const value of referenceStrings(test)) resolve(base, value, dirs)
        }
        // a pass or a definition library named as `module::member`, or quoted by name (`'broken_definitions'
        // does not compile`), and the module sits beside the program the test runs
        const modules = new Set([
            ...[...planCode.get(plan).matchAll(/([A-Za-z_][A-Za-z0-9_]*)\s*::/g)].map((match) => match[1]),
            ...[...planCode.get(plan).matchAll(/['"]([A-Za-z_][A-Za-z0-9_]*)['"]/g)].map((match) => match[1]),
        ])
        for (const dir of ownDirs) {
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
    // A name counts when code names it, and the rule is the same for a file and for a directory: a
    // comment, a plan's `desc` or an index's own listing is prose about a file, not a read of it.
    // What is left of a file is its code -- comments stripped (`#` is a comment in a script or a
    // data list, not in Camel, C++ or JavaScript, where it is an attribute or an operator), and for
    // a plan its values without the prose fields. `test/plan-selection.json` is an index of the
    // directories a change touches rather than a reader of them, so its `comment` and `why` fields
    // are stripped too, and it is left out of the broadening rule below.
    const HASH_COMMENT = /\.(py|toml|txt|sh|ya?ml|cfg|ini)$/
    const stripComments = (text, hash) => {
        let out = text.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, ' ')
        if (hash) out = out.replace(/#[^\n]*/g, ' ')
        return out
    }
    const stripJsonProse = (text) => text.replace(/"(comment|why|desc|note|notes)"\s*:\s*"(?:[^"\\]|\\.)*"/g, ' ')
    for (const file of tracked) {
        if (!NAMING_ROOTS.some((root) => file.startsWith(root))) continue
        // the exception table below names the files it records; the ledger is not a reader of them
        if (file === 'scripts/plan-inventory.mjs') continue
        if (BINARY_SUFFIX.test(file)) continue
        const isPlan = file.endsWith('.plan.toml')
        const text = readText(`${ROOT}/${file}`)
        if (text === null) continue
        const code = isPlan
            ? planCode.get(file) ?? ''
            : stripComments(stripJsonProse(text), HASH_COMMENT.test(file))
        if (code === '') continue
        for (const candidate of near.get(dirOf(file)) ?? []) {
            // a file that writes its own name in a comment does not read itself
            if (candidate === file) continue
            if (code.includes(baseOf(candidate))) mark(candidate, file)
        }
        // a harness that builds the path of its fixture tree out of segments
        // (`path.join(ROOT, 'test', 'cases', 'modules', 'std', 'env', 'unicode')`), or a tool that
        // holds `test/cases` in a variable and joins its workload onto it, reads what it builds and
        // no single token in its text says so
        for (const joined of joinedCasePaths(code)) {
            if (caseSet.has(joined)) {
                mark(joined, file)
            } else if (below.has(joined)) {
                for (const child of below.get(joined)) mark(child, file)
            }
        }
        // A harness that names its own directory and then a segment of it
        // (`--arg p=${path.join(HERE, 'model')}`) reads that directory: a struct parameter is a
        // directory of `.npy` files, and no token of the text names one of them. `HERE` is the name
        // this tree gives the directory of the file that is running, and the segment has to name a
        // directory that holds cases under `test/cases` for the rule to say anything.
        if (!isPlan) {
            for (const call of code.matchAll(/\.join\(([^)]*)\)/g)) {
                const args = call[1].split(',').map((piece) => piece.trim())
                if (args.length < 2 || !/^(HERE|__dirname)$/.test(args[0])) continue
                const first = /^['"]([A-Za-z0-9_.\-]+)['"]$/.exec(args[1])
                if (!first) continue
                for (const base of [dirOf(file), dirOf(dirOf(file))]) {
                    const key = `${base}/${first[1]}`
                    if (below.has(key)) for (const child of below.get(key)) mark(child, file)
                }
            }
        }
        for (const match of code.matchAll(TOKEN)) {
            const token = match[0].replace(/^[.\-/]+/, '').replace(/[.\-/]+$/, '')
            for (const candidate of suffix.get(token) ?? []) mark(candidate, file)
            // a fixture directory a harness reads: naming a directory reads every file under it.
            // A plan's own value pointing at the directory its cases live in is resolved above with
            // the plan's own directory as the base, so it is not read here a second time.
            if (!isPlan && !INDEX_FILES.has(file) && token.startsWith(`${CASE_ROOT}/`) &&
                token.split('/').length >= 4) {
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
    const planProblems = planInventoryProblems()
    const caseProblems = caseLedgerProblems()
    const problems = [...planProblems, ...caseProblems]
    const examined = (walkPlans('test/plans') ?? []).length
    for (const problem of problems) console.error(`plan-inventory: ${problem}`)
    if (problems.length > 0) {
        console.error(
            `plan-inventory: ${problems.length} problem(s): every plan file must be reached by a fast target, ` +
                'named in SLOW_PLANS, named in FORBIDDEN, or be a debug or milestone plan; and every file ' +
                'under test/cases must be read by code -- a plan, an import, a harness that opens it -- ' +
                'or be recorded in CASE_EXCEPTIONS with the reason it is kept',
        )
        process.exit(1)
    }
    console.log(`plan-inventory: ${examined} plan files, every one of them has a destination`)
    const cases = caseTreeFiles().filter((file) => !file.endsWith('.plan.toml')).length
    console.log(
        `plan-inventory: ${cases} case files, every one of them is read by code or recorded with a reason ` +
            `(${CASE_EXCEPTIONS.length} recorded)`,
    )
}