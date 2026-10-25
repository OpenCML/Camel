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
// to be unchecked: a renamed FORBIDDEN file was printed as skipped, and a new slow plan under feat/
// vanished from both layers.
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
import { fileURLToPath } from 'node:url'
import { parseTomlFile } from '../test/tools/toml-lite.mjs'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

// The fast layer. `smoke` + `feat` are what `npm run test` walks; `gpu/profile` is fast-tier but
// under `gpu/`, which neither the fast target nor the slow list reaches -- it would otherwise be
// as invisible as the rest of `gpu/**` was before the slow layer existed.
export const FAST_TARGETS = ['smoke', 'test/plans/feat', 'test/plans/gpu/profile.plan.toml']

// The slow layer, by explicit plan file. Fourteen files: six fast plans whose slow tests are
// runnable, five whole slow plans, and three perf plans the tree keeps gate-able -- the
// `para_speedup` plan, and the two count guards split out of `perf.step_budget` and
// `perf.reduce_layout` because they assert counts and properties rather than durations. Every
// other slow test is in FORBIDDEN below.
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
// quiet machine -- and the shared host is not one. measured 200 cases here.
//
// A file in this list is named as a whole plan when the whole plan is excluded, and as a plan
// with excluded slow entries when only some of its tests are excluded: the runner cannot select
// a test by name, so naming such a file would start the forbidden tests too (the ONNX row).
// Both kinds are skipped entirely, which is why the list holds 26 entries (23 whole plans and 3
// plans with forbidden slow entries) for those 200 cases plus the perf cases that landed after
// that measurement.
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
    // sweep, recorded rather than gated (the eight that had no fate: one to the slow layer, two
    // split into a gate-able count guard and a measurement, five here).
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
// files that have no fate in one run instead of fixing one per run.
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

// The merge gate's fast layer runs this file as a plan entry, and a reader can run it by hand: the
// same check the release gate starts with, without an install and without a layer.
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const problems = planInventoryProblems()
    const examined = (walkPlans('test/plans') ?? []).length
    for (const problem of problems) console.error(`plan-inventory: ${problem}`)
    if (problems.length > 0) {
        console.error(
            `plan-inventory: ${problems.length} problem(s): every plan file must be reached by a fast target, ` +
                'named in SLOW_PLANS, named in FORBIDDEN, or be a debug plan',
        )
        process.exit(1)
    }
    console.log(`plan-inventory: ${examined} plan files, every one of them has a fate`)
}