// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The mirror image of the reachability check: a plan may be selected by a change, but that
// must not cost more than the change can invalidate. A costly plan -- a `slow` one, which the
// release gate runs, or a `milestone` one, which only a milestone runs -- selected for a fast case
// path runs in full (a named plan file ignores `--tier`), so a one-line edit to a common case
// directory would drag the whole costly plan into the developer loop. The facts are derived from
// the tree (tier, the tests' tiers and timeouts, the `case` paths the plans name, and the
// selection `--changed` itself returns); no plan or path is written down here.
//
// A selected costly plan is accepted only when it names the changed case and is the only owner
// of it, or when `test/plan-selection.json` records the shared case in `slowCaseOwners` with a
// reason. An exception that excuses nothing (the plan is not costly, or it shares no case with
// a fast plan) is a failure too, so the list cannot rot.
//
// Run directly (`node test/tools/selftest/over-selection.mjs`) to check only this property; the
// runner self-test imports `overSelectionProblems` and adds the result to its own problems.
import fs from 'fs'
import path from 'path'
import { execFileSync } from 'child_process'
import { fileURLToPath } from 'url'
import { parseTomlFile } from '../toml-lite.mjs'
import { plansForChanges, resolvedSelection } from '../../runner.mjs'

// `docs/test-plans.md` calls a test of about five seconds or more slow, and the runner's default
// timeout is ten seconds; a ceiling of five minutes is thirty times that and is the point where a
// timeout can no longer be read as a safety margin around a fast test.
const LONG_TIMEOUT_MS = 300000
const CASE_ENTRY = /^[ \t]*case[ \t]*=[ \t]*"([^"]*)"/gm

const here = path.dirname(fileURLToPath(import.meta.url))
const defaultRepoRoot = path.resolve(here, '..', '..', '..')
const relFromRepo = (repoRoot, candidate) =>
    path.relative(repoRoot, candidate).split(path.sep).join('/')

function walkPlanFiles(dir, out = []) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) walkPlanFiles(full, out)
        else if (entry.name.endsWith('.plan.toml')) out.push(full)
    }
    return out
}

/**
 * A plan's cost class, from the facts the plan states: its tier, its tests' tiers, timeouts. The
 * costly layers are `slow` (the release gate's batch) and `milestone` (what only runs at a
 * milestone): a file target runs such a plan in full, which is what the mirror below is about.
 */
function planCost(planFile) {
    const doc = parseTomlFile(planFile)
    const costly = (tier) => tier === 'slow' || tier === 'milestone'
    const topTier = costly(doc.tier) || doc.tier === 'debug' ? doc.tier : 'fast'
    const tests = (doc.tests || []).map((test) => ({ ...(doc.defaults || {}), ...test }))
    const testTier = (test) =>
        costly(test.tier) || test.tier === 'debug' ? test.tier : costly(topTier) ? topTier : 'fast'
    const maxTimeout = Math.max(10000, ...tests.map((test) => Number(test.timeout_ms) || 0))
    return {
        tier: topTier,
        slow: costly(topTier) || tests.some((test) => costly(testTier(test))),
        maxTimeout,
    }
}

function caseFilesOf(planFile) {
    const files = []
    for (const match of fs.readFileSync(planFile, 'utf8').matchAll(CASE_ENTRY)) {
        if (match[1].includes('${')) continue // the runner's variables, not a path in the tree
        files.push(path.resolve(path.dirname(planFile), match[1].replace(/\\/g, '/')))
    }
    return files
}

function selectionTargetPath(repoRoot, name) {
    const base = path.join(repoRoot, 'test', 'plans', ...String(name).replace(/\\/g, '/').split('/'))
    if (fs.existsSync(base) && fs.statSync(base).isDirectory()) return base
    if (fs.existsSync(`${base}.plan.toml`)) return `${base}.plan.toml`
    return base
}

export function overSelectionProblems(repoRoot = defaultRepoRoot) {
    const problems = []
    const plansRoot = path.join(repoRoot, 'test', 'plans')
    const selectionDoc = JSON.parse(fs.readFileSync(path.join(repoRoot, 'test', 'plan-selection.json'), 'utf8'))

    const costCache = new Map()
    const costOf = (planFile) => {
        if (!costCache.has(planFile)) costCache.set(planFile, planCost(planFile))
        return costCache.get(planFile)
    }
    const planFiles = walkPlanFiles(plansRoot)
    const planCosts = new Map(planFiles.map((file) => [relFromRepo(repoRoot, file), costOf(file)]))
    // `slow` here names the cost class, not the tier: a `slow` test and a `milestone` test are
    // both plans a developer change may not drag into the fast loop.
    const isSlowCost = (relative) => {
        const cost = planCosts.get(relative)
        return cost !== undefined && (cost.slow || cost.maxTimeout >= LONG_TIMEOUT_MS)
    }

    // Case path -> the plans that name it. This is the only definition of "the plan runs that
    // case" available to the tree: a plan's `case` entries, resolved the way the runner does.
    const owners = new Map()
    for (const planFile of planFiles) {
        for (const caseFile of caseFilesOf(planFile)) {
            if (!fs.existsSync(caseFile) || !fs.statSync(caseFile).isFile()) continue
            const key = relFromRepo(repoRoot, caseFile)
            if (!owners.has(key)) owners.set(key, new Set())
            owners.get(key).add(relFromRepo(repoRoot, planFile))
        }
    }

    // The recorded exceptions: a slow plan may run a fast case when a fast plan also owns it,
    // if the selection file says why. Entries are validated against the tree, not trusted.
    const exceptions = new Set()
    const exceptionEntries = (selectionDoc.slowCaseOwners && selectionDoc.slowCaseOwners.entries) || []
    for (const entry of exceptionEntries) {
        const name = String(entry.plan || '').replace(/\\/g, '/')
        const why = typeof entry.why === 'string' ? entry.why.trim() : ''
        if (!name || !why) {
            problems.push(`plan-selection: a slowCaseOwners entry needs a plan and a why`)
            continue
        }
        const planFile = selectionTargetPath(repoRoot, name)
        if (!fs.existsSync(planFile) || !fs.statSync(planFile).isFile()) {
            problems.push(`plan-selection: slowCaseOwners names '${name}', which is not a plan file under test/plans`)
            continue
        }
        const relative = relFromRepo(repoRoot, planFile)
        if (!isSlowCost(relative)) {
            problems.push(
                `plan-selection: slowCaseOwners names '${name}', which is not slow-cost, so it excuses nothing`
            )
            continue
        }
        const shares = [...owners.entries()].some(
            ([, plans]) => plans.has(relative) && [...plans].some((other) => other !== relative && !isSlowCost(other))
        )
        if (!shares) {
            problems.push(
                `plan-selection: slowCaseOwners names '${name}', which shares no case with a fast plan, so it excuses nothing`
            )
            continue
        }
        exceptions.add(relative)
    }

    const tracked = execFileSync('git', ['ls-files', '-z', '--cached', '--others', '--exclude-standard'], {
        cwd: repoRoot,
        encoding: 'utf8',
    })
        .split('\0')
        .filter(Boolean)
    const casePaths = tracked.filter((file) => file.startsWith('test/cases/'))
    const violations = []
    for (const file of casePaths) {
        const plans = owners.get(file)
        if (!plans || plans.size === 0) continue
        // A "fast case path": at least one plan runs it as a fast test (a plan with a slow or a milestone
        // test is costly, so this is the same fact as the plan's cost class, derived, not listed).
        if (![...plans].some((plan) => !isSlowCost(plan))) continue
        const selection = plansForChanges([file])
        if (!selection) continue
        const layered = new Set(selection.layered.map((name) => path.resolve(selectionTargetPath(repoRoot, name))))
        for (const target of resolvedSelection(selection)) {
            const stat = fs.statSync(target, { throwIfNoEntry: false })
            if (!stat || !stat.isFile() || !target.endsWith('.plan.toml')) continue
            if (layered.has(path.resolve(target))) continue // follows --tier; the slow tests are filtered
            const plan = relFromRepo(repoRoot, target)
            if (!isSlowCost(plan)) continue
            const owner = plans.has(plan)
            const onlyOwner = owner && plans.size === 1
            const excused = owner && exceptions.has(plan)
            if (onlyOwner || excused) continue
            const cost = planCosts.get(plan)
            const facts = `tier ${cost.tier}${cost.maxTimeout > 10000 ? `, longest timeout ${cost.maxTimeout} ms` : ''}`
            violations.push(
                `${file} selects the costly plan ${plan} (${facts}) and a file target runs in full: `
                + (owner
                    ? 'the plan runs the case but is not its only owner, so the fast plan already covers the change; '
                    : 'the plan does not run that case, so the change cannot invalidate it; ')
                + 'name the costly plan only for the cases it runs (`ownCases`), or record the shared case in '
                + "plan-selection.json's `slowCaseOwners` with a reason"
            )
        }
    }
    if (violations.length > 0) {
        problems.push(
            `plan-selection: ${violations.length} case path(s) run a slow or milestone plan the change cannot invalidate:\n  `
            + [...new Set(violations)].join('\n  ')
        )
    }
    return problems
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const problems = overSelectionProblems()
    if (problems.length > 0) {
        console.error(problems.join('\n'))
        process.exit(1)
    }
    console.log('over-selection self-test ok')
}