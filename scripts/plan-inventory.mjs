#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The plan inventory: the tables that say what layer a plan file belongs to, and the check that
// Every plan file belongs to a layer, and the layers are read off the tree rather than written
// down in a list. The fast layer is the whole tree at `--tier fast`; the slow layer is SLOW_PLANS,
// by name, because a slow test is fast to name and long to run; the milestone layer is every test
// whose own tier says `milestone`, which `--tier milestone` selects; the debug layer is out of the
// release gate and says so. A plan file that names a tier nobody knows, a slow test that no slow
// plan names, and a table entry that no longer exists are the problems left, and this check reports
// them at the commit that lands them.
//
// Three ledgers, one question each, all of them "does something read this": a plan file has a layer
// (`planInventoryProblems`, and `slowLayerReading` opens every file the slow list names instead of
// trusting the row), a case has a reader (`caseLedgerProblems`), and a tool's own test is started by
// a plan or recorded with the reason it cannot be (`selfCheckProblems`).
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

// One reading per plan file per run (`readPlan`). The three ledgers below ask about the same tree
// -- the slow list alone would have the same file parsed three times -- and the parse is the
// expensive half of the walk. Reset is not offered: this module runs once per process, and a
// second run in the same process would be a test asking about a different tree.
const PLAN_READING = new Map()

// The fast layer: the whole plan tree at `--tier fast`, which is the command `npm run test` runs
// on a subtree. The author's 01:30 ruling (2026-11-02) put it here: "what we call green" and "what
// runs before a release" get one definition, the tree and each plan's own tier lines. It used to
// be a list (`smoke` + `feat` + two gpu files), and a list is a second definition -- a fast test
// in a file the list did not name was neither run by the gate nor reported as missing, and the
// list had to be edited by hand the day a plan landed somewhere else. Measured before the change:
// the list's own directory target walked all 338 files anyway (a directory is walked, not named),
// so it ran 8440 of the 8441 cases the whole tree has; the difference the change makes is that the
// report now says so instead of holding 25 files back "by policy" while running their fast tests.
export const FAST_TARGETS = ['test/plans']

// The slow layer, by explicit plan file: every file that owns a test whose own line says
// `tier = "slow"`, and the whole-plan slow files (the GPU tile plans, the two collection plans, the
// corpus sweep and the perf plans the tree keeps gate-able -- `para_speedup` from the 2026-10-23
// rule, and the two count guards the 2026-10-25 rule split out of `perf.step_budget` and
// `perf.reduce_layout` because they assert counts and properties rather than durations).
//
// The list grew on 2026-11-24 with the 261 cases the first full fast-layer record measured over
// 2 s (internal/reports/2026-11-24-test-suite-inventory.cn.md, appendix B): a case is out of the
// developer loop but still worth reading, so it says `tier = "slow"` and its file is named here,
// because naming the file is what makes the release gate run it. A `slow` test outside these
// files is a problem this check reports: the fast layer would drop it and no other layer would
// run it.
//
// A row here is read, never skipped (see `slowLayerReading`): the check opens every named file and
// counts what it holds per layer, so "slow" is answered by the tests' own `tier` lines and not by
// the fact that a list says so. Before that, the row was a `continue` before any reading -- a file
// named here could be unreadable, empty, or hold nothing but `fast` entries and the check would
// still call the layer covered. The three ways a row can be true and empty are reported by name.
export const SLOW_PLANS = [
    'test/plans/feat/bindings/python_binding.plan.toml',
    'test/plans/feat/compile/collections.plan.toml',
    'test/plans/feat/compile/diag_catalog.plan.toml',
    'test/plans/feat/compile/diag_fixes.plan.toml',
    'test/plans/feat/compile/diag_options.plan.toml',
    'test/plans/feat/compile/generics.plan.toml',
    'test/plans/feat/compile/numeric_permissions.plan.toml',
    'test/plans/feat/compile/return_type.plan.toml',
    'test/plans/feat/compile/type_identity.plan.toml',
    'test/plans/feat/compile/type_text.plan.toml',
    'test/plans/feat/compile/unused_values.plan.toml',
    'test/plans/feat/compile/with_defaults.plan.toml',
    'test/plans/feat/consistency/attempt.plan.toml',
    'test/plans/feat/consistency/match.plan.toml',
    'test/plans/feat/consistency/runner.plan.toml',
    'test/plans/feat/consistency/surface.plan.toml',
    'test/plans/feat/consistency/values.plan.toml',
    'test/plans/feat/docs_links.plan.toml',
    'test/plans/feat/exemplars.plan.toml',
    'test/plans/feat/gate/case_ledger.plan.toml',
    'test/plans/feat/gate/conformance.plan.toml',
    'test/plans/feat/gate/plan_inventory.plan.toml',
    'test/plans/feat/linear/var_reference.plan.toml',
    'test/plans/feat/mm/capacity.plan.toml',
    'test/plans/feat/mm/gc_pacing.plan.toml',
    'test/plans/feat/mm/gc_soak.plan.toml',
    'test/plans/feat/mm/graph_space.plan.toml',
    'test/plans/feat/mm/process_state.plan.toml',
    'test/plans/feat/modules/autodiff.plan.toml',
    'test/plans/feat/modules/checkpoints.plan.toml',
    'test/plans/feat/modules/import_cost.plan.toml',
    'test/plans/feat/modules/nn.plan.toml',
    'test/plans/feat/modules/onnx.plan.toml',
    'test/plans/feat/modules/random.plan.toml',
    'test/plans/feat/modules/rules.plan.toml',
    'test/plans/feat/modules/rules_libraries.plan.toml',
    'test/plans/feat/modules/stdlib_registry.plan.toml',
    'test/plans/feat/modules/tiles.plan.toml',
    'test/plans/feat/opt/algebra.plan.toml',
    'test/plans/feat/opt/control.plan.toml',
    'test/plans/feat/opt/control_data.plan.toml',
    'test/plans/feat/opt/defun.plan.toml',
    'test/plans/feat/opt/effects_kept.plan.toml',
    'test/plans/feat/opt/gate_value.plan.toml',
    'test/plans/feat/opt/generic.plan.toml',
    'test/plans/feat/opt/graph_faces.plan.toml',
    'test/plans/feat/opt/inline.plan.toml',
    'test/plans/feat/opt/inline_cycle.plan.toml',
    'test/plans/feat/opt/inline_use.plan.toml',
    'test/plans/feat/opt/pass_params.plan.toml',
    'test/plans/feat/opt/progen_simplify.plan.toml',
    'test/plans/feat/parse/frontend.plan.toml',
    'test/plans/feat/tools/autodiff_migration_ledger.plan.toml',
    'test/plans/feat/tools/build_stamp.plan.toml',
    'test/plans/feat/tools/clean.plan.toml',
    'test/plans/feat/tools/cli.plan.toml',
    'test/plans/feat/tools/debugger.plan.toml',
    'test/plans/feat/tools/exit_reporting.plan.toml',
    'test/plans/feat/tools/find_fix.plan.toml',
    'test/plans/feat/tools/layer_accounting.plan.toml',
    'test/plans/feat/tools/load_class.plan.toml',
    'test/plans/feat/tools/lsp.plan.toml',
    'test/plans/feat/tools/portability.plan.toml',
    'test/plans/feat/tools/provenance.plan.toml',
    'test/plans/feat/tools/selfchecks.plan.toml',
    'test/plans/feat/trans/canonical_gir.plan.toml',
    'test/plans/feat/trans/cml.plan.toml',
    'test/plans/feat/trans/cml_sweep.plan.toml',
    'test/plans/feat/trans/cpp.plan.toml',
    'test/plans/gpu/tiles.plan.toml',
    'test/plans/gpu/tiles_rows.plan.toml',
    'test/plans/gpu/tiles_transfer.plan.toml',
    'test/plans/perf/para_speedup.plan.toml',
    'test/plans/perf/reduce_layout_counts.plan.toml',
    'test/plans/perf/step_counts.plan.toml',
]

// The milestone layer is not a list here either: it is every test whose own tier line (or its
// plan's) says `milestone`, which `--tier milestone` selects -- `agents/test.md`, General Policy:
// the full suite, the GC poison sweep, the GC stress plans, the progen random-program checks, the
// PyTorch comparisons and the perf/timeit plans are milestone-only, by the coordinator, on a quiet
// machine. Depending on a list meant a plan that gained a milestone case after the list was
// written was never counted; `milestoneLayer()` asks the runner's own reading of the tree instead,
// and the gate prints its numbers on every run.

// The plans the gate's own commands cannot start at all. Not "we chose not to run these": the
// author's 01:30 ruling (2026-11-02) leaves this list for a plan the runner refuses to read, or
// whose case needs something the command cannot have, and asks for a reason per entry.
//
// It is empty today, and that is the honest reading: every plan under `test/plans` starts and then
// runs or skips on its own precondition, so nothing belongs here. The 25 rows this list used to
// hold were layer statements -- benchmark, GC soak, PyTorch comparison, "its N milestone tests are
// GC stress" -- and a plan file's own `tier` lines already say all of that. Keeping both is how a
// file with 55 fast tests came to be reported as "UNKNOWN, not green" on a run that executed them.
export const CANNOT_START = []

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
// The milestone layer, counted through the runner's own reading of the tree: every test whose tier
// is `milestone`. A plan whose file says `tier = "milestone"` contributes all of its cases, and the
// three files that hold back only some contribute those tests alone -- which is exactly what
// `--tier milestone` would start, so the number the gate prints is a reading, not a table row.
export function milestoneLayer() {
    const files = new Set()
    let cases = 0
    for (const abs of walkPlans('test/plans') ?? []) {
        const plan = readPlan(abs)
        if (plan.error) continue
        let here = 0
        for (const test of plan.tests) {
            try {
                // Counted as the file declares it, one per entry. A run starts a few more (back ends
                // expand, `compare_to` companions join): measured on 2026-11-02, the files declare
                // 3330 and `--tier milestone` reports `tests: 3404`. Two different subjects, and
                // this line says "test entries" so that it cannot be read as the batch a run starts.
                if (testTier(plan.doc, test) === 'milestone') here += 1
            } catch {
                // an unknown tier is the inventory check's problem, reported there by name
            }
        }
        if (here > 0) {
            files.add(rel(abs))
            cases += here
        }
    }
    return { files: [...files].sort(), cases }
}

// Every problem, not the first: the caller prints the whole list, so a reader sees all the plan
// files that have no layer in one run instead of fixing one per run.
export function planInventoryProblems() {
    const problems = []
    // A root that is not there first, and by name: the rest of this function reads a tree that is
    // not the tree, and every line it would print about the plans of that tree is a line about
    // nothing. `missingRoots` is what the standalone run turns into exit 2.
    for (const { root, path: abs } of missingRoots()) {
        problems.push(`${root} does not exist (expected at ${abs}): a root that is not there is not a tree with no plan in it`)
    }
    for (const entry of CANNOT_START) {
        if (!fs.existsSync(path.join(ROOT, entry.file))) {
            problems.push(`the plan the gate cannot start, '${entry.file}', does not exist; the list is stale`)
        }
    }
    // The slow layer, read rather than skipped. A row of SLOW_PLANS is a claim about a file: that
    // the runner can open it, that it holds tests, and that at least one of them belongs to the
    // slow or the milestone layer. Each claim is checked here, by name, because a row that is
    // false is indistinguishable from a row that is true in every count the gate prints: the file
    // is run either way, and the number of entries the layer gained is the thing nobody reads.
    const slow = slowLayerReading()
    LAST_READING.slow = slow
    for (const row of slow.files) {
        if (row.missing) {
            problems.push(`the slow plan '${row.file}' does not exist; the slow list is stale`)
            continue
        }
        if (row.error) {
            problems.push(
                `the slow plan '${row.file}': the runner cannot read it (${row.error}); a named file with no ` +
                    'reading has no layer, and the gate would fail on it a release later instead of here',
            )
            continue
        }
        if (row.entries === 0) {
            problems.push(
                `the slow plan '${row.file}' holds no test entry: naming it runs nothing, so the row states a ` +
                    'layer the file does not have',
            )
            continue
        }
        if (row.slow + row.milestone === 0) {
            problems.push(
                `the slow plan '${row.file}' holds ${row.fast} test(s) and no slow or milestone entry: the row ` +
                    'moves nothing into the slow layer and only runs the file a second time',
            )
        }
    }
    const planFiles = walkPlans('test/plans') ?? []
    if (planFiles.length === 0) {
        problems.push('test/plans holds no *.plan.toml: the inventory examined nothing, which is not a pass')
        return problems
    }
    const slowSet = new Set(SLOW_PLANS)
    const cannotStartSet = new Set(CANNOT_START.map((entry) => entry.file))
    const fastReaches = (key) =>
        FAST_TARGETS.some((target) => {
            if (target === 'smoke') return key.startsWith('test/plans/smoke/')
            if (target.endsWith('.plan.toml')) return key === target
            return key === target || key.startsWith(`${target}/`)
        })
    let slowOutside = 0
    for (const abs of planFiles) {
        const key = rel(abs)
        if (cannotStartSet.has(key)) continue
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
        // A named file has been read above, entry by entry. Its own slow tests are the slow
        // layer's by their own `tier` lines, and the gate runs the whole file; there is nothing
        // left for this loop to decide about it.
        if (slowSet.has(key)) continue
        slowOutside += slowTests
        if (!fastReaches(key)) {
            problems.push(
                `${key}: no layer reaches it (not a fast target, a SLOW_PLANS file, a plan the gate ` +
                    `cannot start, or a debug or milestone plan); its ${plan.tests.length} test(s), ` +
                    `${slowTests} slow, are UNKNOWN, not green`,
            )
        } else if (slowTests > 0) {
            problems.push(
                `${key}: ${slowTests} slow test(s) but the fast layer runs only its fast tests and SLOW_PLANS ` +
                    'does not name it; those tests are UNKNOWN, not green',
            )
        }
    }
    // A slow test whose file is not named here is the same hole seen from the side of the test:
    // `--tier slow` selects it, the merge gate never asks for that layer, and the file the release
    // gate runs by name is a different list. The line above reports each such file; this is the
    // number the whole tree reads, so a run that reports none of them can be told from one that
    // examined none.
    LAST_READING.slowOutside = slowOutside
    return problems
}

/**
 * The slow layer as a reading: every file SLOW_PLANS names, opened and counted by the layer each
 * of its entries lands in. `--tier slow` selects the entries whose own line says `slow`; this list
 * says which files those entries live in, so the two numbers answer two questions and neither is
 * inferred from the other:
 *
 *   * `named` -- rows of the list, i.e. files the release gate starts in full;
 *   * `read` -- rows that were opened; the difference is what "skipped before reading" used to hide;
 *   * `entries`/`byLayer` -- what those files hold, by the runner's own `testTier`, including the
 *     copies their `[[sweeps]]` entries derive.
 *
 * The report therefore says where each entry went. A file whose entries are all `fast` adds
 * nothing to the slow layer: the gate runs it twice and the slow layer is not one test larger.
 */
export function slowLayerReading() {
    const files = []
    const byLayer = { fast: 0, slow: 0, milestone: 0, debug: 0 }
    let read = 0
    for (const file of SLOW_PLANS) {
        const abs = path.join(ROOT, file)
        if (!fs.existsSync(abs)) {
            files.push({ file, missing: true })
            continue
        }
        const plan = readPlan(abs)
        if (plan.error) {
            files.push({ file, error: plan.error })
            continue
        }
        read += 1
        const counts = { fast: 0, slow: 0, milestone: 0, debug: 0 }
        for (const test of plan.tests) {
            try {
                counts[testTier(plan.doc, test)] += 1
            } catch {
                // an unknown tier is the plan loop's problem, reported there by name
            }
        }
        for (const tier of Object.keys(byLayer)) byLayer[tier] += counts[tier]
        files.push({ file, entries: plan.tests.length, ...counts })
    }
    const entries = files.reduce((sum, row) => sum + (row.entries || 0), 0)
    return { named: SLOW_PLANS.length, read, files, byLayer, entries }
}

/**
 * A plan file as the runner reads it: its own tests plus the copies its `[[sweeps]]` entries
 * generate. A plan the schema rejects, and a malformed sweep, redden the run itself; here they are
 * one problem carrying the reason, so the rest of the inventory still prints.
 *
 * Read once per file per run, because three readers now ask for the same file: the layer check,
 * the slow layer's own reading and the case ledger. Parsing a plan is the expensive half of this
 * check, and the walk is the reason the plan entry that runs it carries a CPU bound rather than a
 * wall one.
 */
export function readPlan(abs) {
    // One key per file: the callers below spell a path with `/` or with the platform's separator
    // (the walk joins with it), and two spellings of one plan would be two parses.
    const key = path.resolve(abs)
    if (PLAN_READING.has(key)) return PLAN_READING.get(key)
    const reading = readPlanUncached(abs)
    PLAN_READING.set(key, reading)
    return reading
}

function readPlanUncached(abs) {
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
// The list held the reduced programs of the E9 crash campaign whose shape was not fixed -- each
// reproduces a crash the 2026-11-17 full run reported, and no plan entry can name one without
// either stating the crash as the expected answer (`known_fail` over a process that ends in a
// guarantee) or going red until its shape is fixed. Two of those shapes are fixed now and their
// programs are plan entries (`rc12_seed165`, `rc13_seed202`, and `rc17_seed193` together with its
// line in `test/tools/cppexec/expect.txt`). The five that are left, `rc14_seed176`, `rc15_seed52`,
// `rc16_seed153`, `rc18_seed429` and `rc19_seed70`, are no longer recorded here, and the reason is
// the readership rule rather than the files: `test/cases/opt/pass_written/check.mjs` builds
// `test/cases/opt` out of path segments before it sweeps two of its corpora, and this check counts
// a directory named that way as reading every file it holds, so it reports those five as reached
// and asks for the entries to go. What each one reproduces -- and the command that fires it by hand
// -- is in the plan's own comment (`test/plans/feat/opt/e9.plan.toml`), which is where the record
// of an unfixed shape has to live while the ledger cannot see it.
//
// The list was empty before those, and the one entry it held earlier is recorded here instead
// of being forgotten: the trap fixture `test/cases/compile/module_imports/import_order/roots_trap/
// pkg/lib.cml` stays in the tree on purpose -- its whole function is to be there, so a regression
// that searches a directory it must not search finds it and prints its value, which the pins above
// never allow -- and the entry said nothing opens it in a correct run. That stopped being true when
// the AST snapshot began to cover every `.cml` file of the repository: `test/tools/parsecheck/
// check.py` reads that file now, which is what the `reached` map reports, so the file is reached by
// code and the exception it needed is gone. The trap itself is unchanged, and a regression that
// loads it still fails the pins.
export const CASE_EXCEPTIONS = []

const CASE_ROOT = 'test/cases'
// The numbers the standalone run prints, kept from the walk it already takes: reading the tree a
// second time for the count would cost the fast layer the same eleven seconds the first walk does,
// and a second walk can also disagree with the first about a tree that changed between them.
const LAST_READING = {
    cases: 0,
    readers: 0,
    // the three ledgers' own numbers, printed before the verdict and on a failing run too
    slow: null,
    slowOutside: null,
    selfChecks: null,
}
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
// benchmark pipeline, which is a harness of its own outside the plans. The 2026-11-22 move put every
// block under `src/` (`src/kernel/`, `src/parse/`, `src/compile/`, `src/stdlib/`, `src/tools/`); the
// layout move before it split `src/` and `include/` into four blocks and the library (`lib/`, which
// also took `stdlib/` and `modules/`). The old names stay in the list so a checkout of any of the
// three layouts is read the same way. `src/` is one root and not five: it holds every block since the
// 2026-11-22 move, and listing `src/` beside `src/kernel/` would count every file of the compiler
// twice (the walk returns a list per root, and the same file under two roots is two entries).
// The exemplars moved from the top level to `internal/exemplars/`: the root is the moved directory
// itself and not `internal/`, because the other blocks under `internal/` are notes and drafts, and
// `exemplars/` stays in the legacy set so a checkout from before the move is read the same way.
const NAMING_ROOTS = ['test/', 'scripts/', 'src/', 'lib/', 'tools/', 'include/', 'stdlib/', 'modules/', 'examples/', 'internal/exemplars/']

// The names of the layouts before the move. They stay in the list so a checkout of an older layout is
// read the same way, and they are the only roots whose absence is expected: every root the tree of
// today has is under `REQUIRED_ROOTS`, and a run that cannot find one refuses (exit 2) instead of
// walking around it.
const LEGACY_NAMING_ROOTS = new Set(['lib/', 'tools/', 'include/', 'stdlib/', 'modules/', 'exemplars/'])

// The directories a reading cannot be taken without: the naming roots of the tree this checkout has,
// and the two the ledger reads by name. This list exists because of the shape `walkTreeFiles` used to
// have -- `readdir` on a directory that is not there throws `ENOENT`, the catch turned that into an
// empty list, and an empty list is exactly what a directory holding no matching file returns too.
// Measured 2026-11-22 with `exemplars/` moved away: `412 plan files, every one of them has a
// destination` and `4216 case files, every one of them is read by code`, exit 0 -- the same two
// lines, in the same order, about a tree the run had not read. The walk now separates the two
// readings (see the catch below), and this list names a root that is not there before any of them.
const REQUIRED_ROOTS = [
    'test',
    'test/plans',
    'test/cases',
    'scripts',
    'src',
    'src/kernel',
    'src/parse',
    'src/compile',
    'src/stdlib',
    'src/tools',
    'examples',
    'internal/exemplars',
]

/** The roots this checkout does not have and the check cannot be taken without. */
export function missingRoots() {
    const missing = []
    for (const root of REQUIRED_ROOTS) {
        const abs = path.join(ROOT, root)
        if (!fs.existsSync(abs)) missing.push({ root, path: abs })
    }
    return missing
}

// What the runner writes while it runs (a million files after a few weeks of runs) is not part of
// the tree the ledger judges: the walk would read it all, and the check would grow with the
// history of the checkout instead of with the repository.
const RUN_OUTPUT_DIRS = new Set(['test/results', 'test/tmp', 'node_modules'])

function walkTreeFiles(dir) {
    const out = []
    const walk = (abs, prefix) => {
        let entries
        try {
            entries = fs.readdirSync(abs).sort()
        } catch (error) {
            // A directory the walk cannot read is not a directory that holds no file. At a root the
            // two are the whole subject of this check: a name of the layout before the move may be
            // absent (`LEGACY_NAMING_ROOTS`, which is why this catch exists at all), and every other
            // root is named by `missingRoots()` before the walk starts. Below a root the walk stops
            // and says where -- a directory that changed under a running tree is a reading nobody
            // can take, not a tree with one directory less.
            if (prefix === dir && error.code === 'ENOENT' && LEGACY_NAMING_ROOTS.has(`${dir}/`)) return
            throw new Error(
                `${prefix}: cannot read ${abs} (${error.code || error.message}): the tree cannot be ` +
                    'read there, and that is not a tree with no file in it',
            )
        }
        for (const name of entries) {
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
    LAST_READING.readers = tracked.length

    const files = caseTreeFiles()
    const caseFiles = files.filter((file) => !file.endsWith('.plan.toml'))
    const treePlans = files.filter((file) => file.endsWith('.plan.toml'))
    LAST_READING.cases = caseFiles.length
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
    // is prose about the plan, not a read of anything. `known_fail_owner` and `known_fail_reason`
    // are prose of the same kind: they name a lane and a wall, never a file the plan runs, and the
    // reason quotes a page (`docs/...`) that the plan does not read.
    const PROSE_KEYS = new Set([
        'desc', 'known_fail_owner', 'known_fail_reason', 'known_fail_note', 'known_fail_ends_when',
        'failure_note', 'comment',
    ])
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

// --- the self-check ledger: the tools' own tests, and whether a plan runs one ----------------
//
// The two ledgers above answer for plan files and for cases. Both were already answering when a
// reader noticed the third subject: the tools' own tests -- the files whose name says they are a
// test (`*.test.mjs`, `*.test.ps1`) and the two entry points of the runner's self-test -- were
// named by no plan, no checker and no workflow, so a red there was invisible for as long as the
// tool kept being used. That is the same defect the case ledger exists for, one subject down: a
// file nothing runs cannot fail, so a change that breaks it is silent while the tree pays for it.
//
// The rule is the case ledger's: a self-check is either named by a plan -- a value of a plan file
// that resolves to it, which is what makes a run start it -- or recorded in
// SELF_CHECK_EXCEPTIONS with the reason it cannot be. A name in a comment, in a report or in this
// file is prose about a test, not a run of it.
//
// `test/tools/selftest/` is the one directory where the name does not decide: the two files that
// are checks in their own right are listed in SELF_CHECK_ENTRY_POINTS, and the fixtures beside
// them (`over-selection.mjs`, `scratch_probe.mjs`, `selftest_probe.mjs`, `backend_echo.mjs`, the
// fixture plans) are inputs of those two rather than checks that stand alone.
const SELF_CHECK_ROOTS = ['test/tools', 'internal/tools', 'scripts']
const SELF_CHECK_ENTRY_POINTS = new Set([
    'test/tools/selftest/run.mjs',
    'test/tools/selftest/selection-cases.mjs',
])

// The one reason a self-check of this tree cannot be started by a plan, with the two measurements
// it rests on. It is written once because the nineteen files below are one class, and the class is
// the thing that has to be revisited if either measurement changes.
const NO_PWSH =
    'a plan cannot start it: the build host is Linux and has no pwsh (measured with `command -v pwsh` on ' +
    'the build host: no output), and the runner resolves a tool that is neither `node` nor `python` ' +
    'inside the install (`<install>/bin/pwsh`), so an entry would fail before the script ran instead of ' +
    'skipping with a reason. The suite the test covers runs on the workstation under PowerShell, which ' +
    'no gate command reaches; the `internal/tools/` rule of test/plan-selection.json records the same ' +
    'fact about the scripts themselves. Running them needs a pwsh on the host or a PATH lookup in the ' +
    "runner's tool resolution, and neither is this ledger's to decide."

export const SELF_CHECK_EXCEPTIONS = [
    'internal/tools/camel-cleanup.test.ps1',
    'internal/tools/camel-gate-state.test.ps1',
    'internal/tools/camel-remote-args.test.ps1',
    'internal/tools/camel-remote-cleanup.test.ps1',
    'internal/tools/camel-remote-fetch.test.ps1',
    'internal/tools/camel-remote-targets.test.ps1',
    'internal/tools/hook-integration.test.ps1',
    'internal/tools/merge-batch-sets.test.ps1',
    'internal/tools/merge-lane-baseline.test.ps1',
    'internal/tools/merge-lane-checkout.test.ps1',
    'internal/tools/merge-lane-docs-only.test.ps1',
    'internal/tools/merge-lane-docs.test.ps1',
    'internal/tools/merge-lane-firstparent.test.ps1',
    'internal/tools/merge-lane-push.test.ps1',
    'internal/tools/merge-lane-resolved.test.ps1',
    'internal/tools/merge-lane-start.test.ps1',
    'internal/tools/merge-record.test.ps1',
    'internal/tools/run-registry.test.ps1',
    'internal/tools/run-tests-diagnostic-file.test.ps1',
].map((file) => ({ file, reason: NO_PWSH }))

// The files the ledger judges: a name that says "test", and the two self-test entry points. Read
// from the tree, so a new self-check appears here by being written, not by being listed.
function selfCheckFiles() {
    const out = []
    for (const root of SELF_CHECK_ROOTS) {
        for (const file of walkTreeFiles(root)) {
            const name = baseOf(file)
            if (name.endsWith('.test.mjs') || name.endsWith('.test.ps1') || SELF_CHECK_ENTRY_POINTS.has(file)) {
                out.push(file)
            }
        }
    }
    return out.sort()
}

/** The files of this tree that a plan value resolves to: what a run of some plan would start. */
function planNamedFiles() {
    const named = new Set()
    for (const abs of walkPlans('test/plans') ?? []) {
        const plan = readPlan(abs)
        if (plan.error) continue
        const base = dirOf(rel(abs))
        for (const value of referenceStrings(plan.doc)) {
            if (typeof value !== 'string' || value.includes('${') || value.startsWith('http')) continue
            // an install path or an absolute path names a file of another tree
            if (value.includes(':') || value.startsWith('/')) continue
            // the hand-written joiner the case ledger uses: a plan value is already relative to
            // the plan's directory, so `/` and `..` are the whole resolution it needs.
            named.add(underCases(base, value))
        }
    }
    return named
}

// Every problem, not the first, for the same reason as the two ledgers above.
export function selfCheckProblems() {
    const problems = []
    const files = selfCheckFiles()
    if (files.length === 0) {
        return ['no self-check under test/tools, internal/tools or scripts: the ledger examined nothing, which is not a pass']
    }
    const recorded = new Map(SELF_CHECK_EXCEPTIONS.map((entry) => [entry.file, entry]))
    for (const entry of SELF_CHECK_EXCEPTIONS) {
        if (!fs.existsSync(path.join(ROOT, entry.file))) {
            problems.push(`the recorded self-check '${entry.file}' does not exist; the exception list is stale`)
        } else if (!files.includes(entry.file)) {
            problems.push(
                `the recorded self-check '${entry.file}' is not one of the files this ledger judges (its name ` +
                    'does not say test and it is not a self-test entry point); remove the entry',
            )
        }
    }
    const named = planNamedFiles()
    let covered = 0
    let recordedCount = 0
    for (const file of files) {
        const startable = named.has(file)
        const entry = recorded.get(file)
        if (startable) {
            covered += 1
            if (entry) {
                problems.push(
                    `${file}: recorded in SELF_CHECK_EXCEPTIONS, but a plan names it now; remove the entry`,
                )
            }
            continue
        }
        if (entry) {
            recordedCount += 1
            continue
        }
        problems.push(
            `${file}: no plan names it (a plan's value is what starts a file) and SELF_CHECK_EXCEPTIONS does ` +
                'not record why it cannot be started; give it a plan entry, or say what stops one',
        )
    }
    LAST_READING.selfChecks = { files: files.length, covered, recorded: recordedCount }
    return problems
}

// The merge gate's fast layer runs this file as a plan entry, and a reader can run it by hand: the
// same checks the release gate starts with, without an install and without a layer.
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    // The roots are read first and before anything is walked. A run that cannot read a root has no
    // reading to give, and exit 2 is how it says that: printing the lines of a tree it never read is
    // the defect this block was changed for (see REQUIRED_ROOTS).
    const missing = missingRoots()
    if (missing.length > 0) {
        console.log('plan-inventory: read 0 plan file(s), 0 case file(s) and 0 file(s) under the naming roots')
        for (const { root, path: abs } of missing) {
            console.error(`plan-inventory: no such directory: ${root} (expected at ${abs})`)
        }
        console.error(
            `plan-inventory: ${missing.length} required root(s) missing: the plan layers and the case ` +
                'ledger examined nothing, which is not a pass',
        )
        process.exitCode = 2
    } else {
        const planProblems = planInventoryProblems()
        const caseProblems = caseLedgerProblems()
        const selfProblems = selfCheckProblems()
        const problems = [...planProblems, ...caseProblems, ...selfProblems]
        const examined = (walkPlans('test/plans') ?? []).length
        // What was read, printed before the verdict and on a failing run too: the number a reader
        // compares across runs, and the sentence a run has to answer for when it shrinks.
        console.log(
            `plan-inventory: read ${examined} plan file(s) under test/plans, ${LAST_READING.cases} case ` +
                `file(s) under ${CASE_ROOT} and ${LAST_READING.readers} file(s) under the naming roots`,
        )
        // The two layer readings, in the words of the question each answers. The slow line is the
        // one that stopped being a `continue`: every named file is opened, and what it holds is
        // counted by the layer its own tests belong to.
        const slow = LAST_READING.slow
        if (slow) {
            console.log(
                `plan-inventory: slow layer: ${slow.named} named plan file(s), ${slow.read} read, ` +
                    `${slow.entries} test entries in them (${slow.byLayer.fast} fast, ${slow.byLayer.slow} slow, ` +
                    `${slow.byLayer.milestone} milestone, ${slow.byLayer.debug} debug); ${LAST_READING.slowOutside} ` +
                    'slow test(s) in files the list does not name',
            )
        }
        const selfChecks = LAST_READING.selfChecks
        if (selfChecks) {
            console.log(
                `plan-inventory: self-checks: ${selfChecks.files} file(s) under test/tools, internal/tools and ` +
                    `scripts; ${selfChecks.covered} started by a plan, ${selfChecks.recorded} recorded with a reason`,
            )
        }
        for (const problem of problems) console.error(`plan-inventory: ${problem}`)
        if (problems.length > 0) {
            console.error(
                `plan-inventory: ${problems.length} problem(s): every plan file must be reached by a fast target, ` +
                    'named in SLOW_PLANS, cannot be started by the gate, or be a debug or milestone plan; a slow ' +
                    'plan must be readable and hold a slow or milestone entry; every file ' +
                    'under test/cases must be read by code -- a plan, an import, a harness that opens it -- ' +
                    'or be recorded in CASE_EXCEPTIONS with the reason it is kept; and every self-check must be ' +
                    'started by a plan or recorded in SELF_CHECK_EXCEPTIONS',
            )
            process.exitCode = 1
        } else {
            console.log(`plan-inventory: ${examined} plan files, every one of them has a destination`)
            console.log(
                `plan-inventory: ${LAST_READING.cases} case files, every one of them is read by code or recorded with a reason ` +
                    `(${CASE_EXCEPTIONS.length} recorded)`,
            )
            console.log(
                `plan-inventory: ${selfChecks ? selfChecks.files : 0} self-check(s), every one of them is started by a ` +
                    `plan or recorded with a reason (${SELF_CHECK_EXCEPTIONS.length} recorded)`,
            )
        }
    }
}
