// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import path from 'path'
import fs from 'fs'
import { spawnSync } from 'child_process'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
const TEST_ROOT = path.dirname(__filename)
const REPO_ROOT = path.dirname(TEST_ROOT)
const PLANS_ROOT = path.join(TEST_ROOT, 'plans')
const RUNNER = path.join(TEST_ROOT, 'tools', 'run-tests.mjs')
const SELECTION_PATH = path.join(TEST_ROOT, 'plan-selection.json')
// `--changed` answers what this checkout changed relative to the base branch. `origin/develop`
// is the branch work lands on; CAMEL_TEST_BASE overrides it (a release branch, a fork's base).
const DEFAULT_CHANGED_BASE = 'origin/develop'

export function changedBase() {
    return process.env.CAMEL_TEST_BASE || DEFAULT_CHANGED_BASE
}

const TARGETS = {
    smoke: path.join(PLANS_ROOT, 'smoke'),
    feat: path.join(PLANS_ROOT, 'feat'),
    parse: path.join(PLANS_ROOT, 'feat', 'parse'),
    compile: path.join(PLANS_ROOT, 'feat', 'compile'),
    opt: path.join(PLANS_ROOT, 'feat', 'opt'),
    trans: path.join(PLANS_ROOT, 'feat', 'trans'),
    linear: path.join(PLANS_ROOT, 'feat', 'linear'),
    linearNvm: path.join(PLANS_ROOT, 'feat', 'linear'),
    linearJit: path.join(PLANS_ROOT, 'feat', 'linear'),
    para: path.join(PLANS_ROOT, 'feat', 'para'),
    modules: path.join(PLANS_ROOT, 'feat', 'modules'),
    modulesStd: path.join(PLANS_ROOT, 'feat', 'modules', 'std.plan.toml'),
    modulesNn: path.join(PLANS_ROOT, 'feat', 'modules', 'nn.plan.toml'),
    perf: path.join(PLANS_ROOT, 'perf'),
    all: PLANS_ROOT
}

const DEFAULT_TARGETS = [
    path.join(PLANS_ROOT, 'smoke'),
    path.join(PLANS_ROOT, 'feat')
]

// Options that take a value: the value belongs to the option, not to the target list.
const VALUE_FLAGS = new Set(['--tier', '--jobs', '--build', '--camel'])

function resolvePlanPathFromSegments(segments) {
    if (segments.length === 0) return null
    const base = path.join(PLANS_ROOT, ...segments)
    const planPath = `${base}.plan.toml`
    if (fs.existsSync(planPath)) return planPath
    if (fs.existsSync(base)) return base
    return null
}

function resolveNamedTarget(name) {
    const compact = String(name).replace(/^[./\\]+/, '')
    if (compact === 'all') return TARGETS.all
    if (compact === 'smoke') return TARGETS.smoke
    if (compact === 'feat') return TARGETS.feat
    if (compact === 'parse' || compact === 'feat/parse' || compact === 'feat\\parse') return TARGETS.parse
    if (compact === 'compile' || compact === 'feat/compile' || compact === 'feat\\compile') return TARGETS.compile
    if (compact === 'opt' || compact === 'feat/opt' || compact === 'feat\\opt') return TARGETS.opt
    if (compact === 'trans' || compact === 'feat/trans' || compact === 'feat\\trans') return TARGETS.trans
    if (compact === 'linear' || compact === 'feat/linear' || compact === 'feat\\linear') return TARGETS.linear
    if (compact === 'linear/nvm' || compact === 'linear\\nvm' || compact === 'feat/linear/nvm' || compact === 'feat\\linear\\nvm') return TARGETS.linearNvm
    if (compact === 'linear/jit' || compact === 'linear\\jit' || compact === 'feat/linear/jit' || compact === 'feat\\linear\\jit') return TARGETS.linearJit
    if (compact === 'para' || compact === 'feat/para' || compact === 'feat\\para') return TARGETS.para
    if (compact === 'modules') return TARGETS.modules
    if (compact === 'modules/std' || compact === 'modules\\std' || compact === 'feat/modules/std' || compact === 'feat\\modules\\std') return TARGETS.modulesStd
    if (compact === 'modules/nn' || compact === 'modules\\nn' || compact === 'feat/modules/nn' || compact === 'feat\\modules\\nn') return TARGETS.modulesNn
    if (compact === 'perf') return TARGETS.perf

    const dotted = resolvePlanPathFromSegments(compact.split('.').filter(Boolean))
    if (dotted) return dotted
    const dottedFeat = resolvePlanPathFromSegments(['feat', ...compact.split('.').filter(Boolean)])
    if (dottedFeat) return dottedFeat
    const slashed = resolvePlanPathFromSegments(compact.split(/[\\/]+/).filter(Boolean))
    if (slashed) return slashed
    const slashedFeat = resolvePlanPathFromSegments(['feat', ...compact.split(/[\\/]+/).filter(Boolean)])
    if (slashedFeat) return slashedFeat
    return null
}

function resolveTarget(input) {
    const named = resolveNamedTarget(input)
    if (named) return named
    const absolute = path.resolve(process.cwd(), input)
    // A plan file's name ends in `.plan.toml`, but saying the whole suffix is noise: the target
    // `test/plans/feat/exemplars` names `exemplars.plan.toml` when no such directory exists.
    // Only a target that is neither a directory nor a file is left alone, so a path that does
    // not exist still reaches the runner as the ENOENT the caller must see.
    if (!fs.existsSync(absolute) && fs.existsSync(`${absolute}.plan.toml`)) return `${absolute}.plan.toml`
    return absolute
}

/**
 * The targets that name no plan: a directory holding no plan file, which is what
 * `test/plans/feat/docs_links` is (the plan's fixture directory; the plan file beside it is
 * `docs_links.plan.toml` and the suffix-less shorthand above does not fire, because the
 * directory exists). `--list` used to print such a directory and exit 0, which reads as "this
 * is what the run would do" when the run would do nothing; the run path refuses the same target
 * (test/tools/run-tests.mjs, `reachedNothing`) and the two entry points must not disagree about
 * what a target means. A path that does not exist is left to the run path's ENOENT.
 */
function targetsHoldingNoPlan(targets) {
    return targets.filter((target) => {
        const absolute = path.resolve(target)
        if (!fs.existsSync(absolute) || !fs.statSync(absolute).isDirectory()) return false
        let plans = 0
        const walk = (directory) => {
            for (const name of fs.readdirSync(directory)) {
                const entry = path.join(directory, name)
                if (fs.statSync(entry).isDirectory()) walk(entry)
                else if (name.endsWith('.plan.toml')) plans += 1
            }
        }
        walk(absolute)
        return plans === 0
    })
}

/** The repository-relative spelling of a path, the way every other line the runner prints names one. */
function relativeToRepo(target) {
    return path.relative(REPO_ROOT, path.resolve(target)).replace(/\\/g, '/')
}

/**
 * The one sentence a target that reaches no plan gets, shared in wording with the run path
 * (test/tools/run-tests.mjs prints the same one to stdout). The self-test asserts both, so a
 * rewording in one place cannot quietly leave the two entry points describing the same mistake
 * differently.
 */
function noPlanSentence(target) {
    const beside = `${path.resolve(target)}.plan.toml`
    const hint = fs.existsSync(beside) ? `; the plan file beside it is ${relativeToRepo(beside)}` : ''
    return (
        `[PLAN ERROR] ${relativeToRepo(target)} holds no *.plan.toml, so this run would start ` +
        `nothing${hint}. A target is a *.plan.toml or a directory that contains one.`
    )
}

function parseArgs(argv) {
    const runnerArgs = []
    const targets = []
    let changed = false
    let list = false
    // `--changed-files <path>`: the changed paths, listed by a caller that could read the base
    // branch. `internal/tools/camel-remote.ps1` moves a tree to a host that holds the lane's
    // commits but not the base branch, so `git diff origin/develop` there answers "cannot read"
    // rather than "nothing changed"; the list is computed where the base exists and handed over.
    // Same selection either way, so a run here and a run through the gate answer one question.
    let changedFilesList = null

    for (let i = 0; i < argv.length; ++i) {
        const arg = argv[i]
        if (arg === '--changed') {
            changed = true
        } else if (arg === '--list') {
            // Print the plans a selection resolves to instead of running them. `--changed --list`
            // is the answer to "what would this change run?" without paying for the run.
            list = true
        } else if (
            arg === '--all' ||
            arg === '--smoke' ||
            arg === '--feat' ||
            arg === '--parse' ||
            arg === '--compile' ||
            arg === '--opt' ||
            arg === '--trans' ||
            arg === '--linear' ||
            arg === '--linear:nvm' ||
            arg === '--linear:jit' ||
            arg === '--para' ||
            arg === '--modules' ||
            arg === '--modules:std' ||
            arg === '--modules:nn' ||
            arg === '--perf'
        ) {
            targets.push(resolveTarget(arg.slice(2)))
        } else if (arg === '--suite' && i + 1 < argv.length) {
            targets.push(resolveTarget(argv[++i]))
        } else if (arg === '--changed-files' && i + 1 < argv.length) {
            changedFilesList = path.resolve(argv[++i])
        } else if (arg.startsWith('--changed-files=')) {
            changedFilesList = path.resolve(arg.slice('--changed-files='.length))
        } else if (arg.startsWith('-')) {
            runnerArgs.push(arg)
            // `--tier fast` and `--jobs 8` carry their value; without this the value would be
            // read as a plan name.
            if (!arg.includes('=') && VALUE_FLAGS.has(arg) && i + 1 < argv.length) {
                runnerArgs.push(argv[++i])
            }
        } else {
            targets.push(resolveTarget(arg))
        }
    }

    return {
        runnerArgs,
        // The debug layer is opt-in and its abort case lives in test/plans/debug, outside the
        // ordinary smoke + feat default, so a debug run without a target scans the whole plan
        // tree. A named target still narrows it.
        targets: targets.length > 0
            ? targets
            : changed
                ? []
                : debugTierRequested(runnerArgs)
                    ? [PLANS_ROOT]
                    : DEFAULT_TARGETS,
        changed,
        list,
        changedFilesList,
    }
}

/** Whether the forwarded arguments ask for `--tier debug` (as `--tier debug` or `--tier=debug`). */
function debugTierRequested(runnerArgs) {
    for (let i = 0; i < runnerArgs.length; i++) {
        if (runnerArgs[i] === '--tier' && runnerArgs[i + 1] === 'debug') return true
        if (runnerArgs[i] === '--tier=debug') return true
    }
    return false
}

/**
 * Why the last `changedFiles()` answered null, as the sentence the caller must print. The fast
 * selection used to answer this question with one word -- "cannot read" -- and then widen to the
 * whole fast layer, so the three different ways to fail (a base that does not resolve, a base with
 * no merge-base, a diff that failed) all read the same and none of them was visible. A refusal that
 * does not name the step that failed is the next silent form of the same defect.
 */
let changeReadFailure = null

/**
 * Files changed against the base branch, committed or not, plus the untracked ones; null when git
 * cannot answer. `listPath` reads the same list from a file instead, for a run whose base branch
 * lives on another machine (`--changed-files`); a list that cannot be read is null too, because the
 * caller must not read "cannot ask" as "nothing changed".
 */
export function changedFiles(listPath = null) {
    changeReadFailure = null
    if (listPath) {
        let text
        try {
            text = fs.readFileSync(listPath, 'utf8')
        } catch (error) {
            changeReadFailure = `cannot read the changed-file list ${listPath} (${error.code || error.message})`
            return null
        }
        return [...new Set(text.split(/\r?\n/).map((line) => line.trim().replace(/\\/g, '/')).filter(Boolean))]
    }
    const base = changedBase()
    const run = (args) => spawnSync('git', args, { cwd: REPO_ROOT, encoding: 'utf8', windowsHide: true })
    // A ref that does not resolve and a diff that failed are different refusals: the message says
    // which, because "cannot read the baseline" alone sent readers to the network for a typo.
    const resolve = run(['rev-parse', '--verify', '--quiet', `${base}^{commit}`])
    if (resolve.error) {
        changeReadFailure = `cannot read ${base}: git cannot run (${resolve.error.message})`
        return null
    }
    if (resolve.status !== 0) {
        changeReadFailure = `cannot read ${base}: it does not resolve to a commit here`
        return null
    }
    // `git diff A` answers across unrelated histories -- and then reports every file that differs,
    // which selects the whole tree and reads exactly like a large change. A shallow clone or a base
    // from another root would take that path, so a base with no merge-base is refused, not diffed.
    const common = run(['merge-base', base, 'HEAD'])
    if (common.error) {
        changeReadFailure = `cannot read ${base}: git cannot run (${common.error.message})`
        return null
    }
    if (common.status !== 0 || !`${common.stdout || ''}`.trim()) {
        changeReadFailure = `cannot read ${base}: it shares no merge-base with HEAD (unrelated history, or a shallow clone), so "the change against it" is undefined`
        return null
    }
    const diff = run(['diff', '--name-only', base])
    if (diff.error || diff.status !== 0) {
        changeReadFailure = `cannot read ${base}: git diff against it failed${diff.error ? ` (${diff.error.message})` : ''}`
        return null
    }
    const untracked = run(['ls-files', '--others', '--exclude-standard'])
    const text = `${diff.stdout || ''}\n${untracked.status === 0 ? untracked.stdout || '' : ''}`
    const files = text
        .split(/\r?\n/)
        .map((line) => line.trim().replace(/\\/g, '/'))
        .filter(Boolean)
    return [...new Set(files)]
}

/**
 * A plan entry of a file rule: `"feat/opt"`, `{ "plan": "...", "layer": true }`, or
 * `{ "plan": "...", "ownCases": true }`. `ownCases` adds the plan only when the changed file is
 * one of the cases the plan names: a case edit cannot invalidate a plan that does not run it, so
 * a slow plan must not ride a directory into a case it never reads.
 */
function selectionPlan(entry, ruleLayer) {
    if (entry !== null && typeof entry === 'object') {
        return {
            name: String(entry.plan || ''),
            layer: entry.layer === undefined ? ruleLayer : entry.layer === true,
            ownCases: entry.ownCases === true,
        }
    }
    return { name: String(entry), layer: ruleLayer, ownCases: false }
}

// The selection is read once per process: `--changed` looks at every changed file, and the
// self-test additionally asks about every tracked file to prove none is rule-less by accident.
let selectionRulesCache = null
let directoryRulesCache = null

export function loadSelectionRules() {
    if (selectionRulesCache) return selectionRulesCache
    const doc = JSON.parse(fs.readFileSync(SELECTION_PATH, 'utf8'))
    selectionRulesCache = (doc.rules || []).map((rule) => {
        // `layer` on the rule is the default for its entries: a layered plan file follows
        // `--tier` even though a named file normally runs in full. A rule about a plan with slow
        // entries that must not run (the PyTorch comparisons of autodiff/onnx) marks them,
        // instead of silently selecting a file whose slow cases then never execute.
        const layer = rule.layer === true
        return {
            paths: (rule.paths || []).map((entry) => String(entry).replace(/\\/g, '/')),
            plans: (rule.plans || []).map((entry) => selectionPlan(entry, layer)).filter((plan) => plan.name),
            // A rule may name no plans but select the changed plan file itself (`test/plans/`).
            self: rule.self === true,
            // Plans the selection must not reach by the self rule (perf, GC stress, progen): a
            // change there falls back to the fast layer instead of running the excluded plan.
            except: (rule.except || []).map((entry) => String(entry).replace(/\\/g, '/')),
        }
    })
    return selectionRulesCache
}

/**
 * The plans a set of changed files selects. For each file the directory rule supplies the plan
 * that mirrors its path, and the first matching file rule adds the plans outside that tree that
 * can catch the same change (the gpu plans of tiles, the exporter of onnx, autodiff for tensor).
 * Their union is run in plan order by the runner. A rule may mark a plan `layer: true`: it stays
 * a named target but follows `--tier` like a directory, so the slow cases that must not run from
 * an ordinary change (the PyTorch comparisons of autodiff/onnx) are filtered while the fast ones
 * still run. Null when no rule matched anything (the caller then runs the whole fast layer).
 */
/**
 * The directory rules: a changed file under one of these roots is first looked up by its
 * directory, so `modules/tensor/ops.cpp` selects `test/plans/feat/modules/tensor` (and
 * `test/cases/compile/enum/x.cml` selects `test/plans/feat/compile`) without a per-subsystem
 * mapping. The deepest plan directory or plan file that matches wins; the file rules below are
 * the complement for a path no plan directory mirrors (a pass, the standard library, docs) and
 * for the plans outside the mirrored tree that a component needs.
 */
export function loadDirectoryRules() {
    if (directoryRulesCache) return directoryRulesCache
    const doc = JSON.parse(fs.readFileSync(SELECTION_PATH, 'utf8'))
    directoryRulesCache = (doc.directoryRules || []).map((rule) => ({
        paths: (rule.paths || []).map((entry) => String(entry).replace(/\\/g, '/')),
        plans: String(rule.plans || '').replace(/\\/g, '/').split('/').filter(Boolean),
    }))
    return directoryRulesCache
}

/**
 * The plan directory or plan file that mirrors the changed path, and the directory rule's own
 * prefix that reached it. Both travel: `--changed` prints the rule beside the file, so a selection
 * can be read as the rule match it is and not only as a count of plans.
 */
function planForDirectory(file) {
    for (const rule of loadDirectoryRules()) {
        const prefix = rule.paths.find((entry) => file === entry || file.startsWith(entry))
        if (!prefix) continue
        const rest = file.slice(prefix.length)
        const dir = path.posix.dirname(rest)
        const segments = dir === '.' || dir === '' ? [] : dir.split('/').filter(Boolean)
        for (let length = segments.length; length >= 1; length--) {
            const candidate = path.join(PLANS_ROOT, ...rule.plans, ...segments.slice(0, length))
            if (fs.existsSync(candidate) && fs.statSync(candidate).isDirectory()) {
                return { target: candidate, prefix }
            }
            const planFile = `${candidate}.plan.toml`
            if (fs.existsSync(planFile) && fs.statSync(planFile).isFile()) {
                return { target: planFile, prefix }
            }
        }
        return null
    }
    return null
}

/**
 * The nearest directory under test/plans that holds a plan file, for a non-plan file under
 * test/plans (a golden, a fixture). A golden has no plan of its own; the plans in the directory
 * that owns it are the ones that read it. Null when no ancestor holds a plan.
 */
function nearestPlanDirectory(file) {
    let dir = path.posix.dirname(file)
    while (dir.startsWith('test/plans/') && dir !== 'test/plans') {
        const absolute = path.join(REPO_ROOT, ...dir.split('/'))
        if (fs.existsSync(absolute) && fs.statSync(absolute).isDirectory()) {
            const hasPlan = fs.readdirSync(absolute).some((entry) => {
                const candidate = path.join(absolute, entry)
                return entry.endsWith('.plan.toml') && fs.statSync(candidate).isFile()
            })
            if (hasPlan) return absolute
        }
        const parent = path.posix.dirname(dir)
        if (parent === dir || parent === 'test') return null
        dir = parent
    }
    return null
}

/**
 * The plans an `ownCases` entry may stand for: the plan file itself, or every plan file under a
 * plan directory (the selection file names both forms).
 */
function walkPlanPaths(entry, out = []) {
    if (fs.statSync(entry).isDirectory()) {
        for (const name of fs.readdirSync(entry)) {
            const full = path.join(entry, name)
            if (fs.statSync(full).isDirectory()) walkPlanPaths(full, out)
            else if (name.endsWith('.plan.toml')) out.push(full)
        }
    } else {
        out.push(entry)
    }
    return out
}

const CASE_ENTRY = /^[ \t]*case[ \t]*=[ \t]*"([^"]*)"/gm
const planCaseCache = new Map()

/** The case files a plan names, resolved the way the test runner resolves them. */
function planCaseFiles(planPath) {
    if (!planCaseCache.has(planPath)) {
        const files = new Set()
        for (const match of fs.readFileSync(planPath, 'utf8').matchAll(CASE_ENTRY)) {
            // `${case}` and the other variables are the runner's, not a path in this tree.
            if (match[1].includes('${')) continue
            files.add(path.resolve(path.dirname(planPath), match[1].replace(/\\/g, '/')))
        }
        planCaseCache.set(planPath, files)
    }
    return planCaseCache.get(planPath)
}

/** Whether a named plan (file or directory) runs the changed file as one of its cases. */
function selectionPlanRunsFile(name, file) {
    let target
    try {
        target = resolveSelectionTarget(name)
    } catch {
        return false
    }
    const absolute = path.resolve(REPO_ROOT, file)
    return walkPlanPaths(target).some((planPath) => planCaseFiles(planPath).has(absolute))
}

export function plansForChanges(files) {
    const rules = loadSelectionRules()
    const names = new Set()
    const layered = new Set()
    const targets = new Set()
    const unmatched = []
    // One line per changed file: the rule that reached it and the plan(s) that rule selected. A
    // count of plans is what a correct selection and a wrong one both print; the rule match is the
    // part that can disagree with the intent, so `--changed` prints it beside the count.
    const reasons = []
    let matched = false
    for (const file of files) {
        let added = false
        const hits = []
        // Directory first: the plan that mirrors the changed path is the narrow selection. The
        // file rules do not replace it, they add the plans that live outside that mirrored tree
        // (a `modules/tiles/` change needs `gpu/**`, which no directory rule can reach).
        const byDirectory = planForDirectory(file)
        if (byDirectory) {
            matched = true
            targets.add(byDirectory.target)
            added = true
            hits.push(`directory rule ${byDirectory.prefix} -> ${relativeToRepo(byDirectory.target)}`)
        }
        const rule = rules.find((candidate) =>
            candidate.paths.some((prefix) => file === prefix || file.startsWith(prefix))
        )
        let excluded = false
        if (rule) {
            const rulePath = rule.paths.find((prefix) => file === prefix || file.startsWith(prefix))
            for (const plan of rule.plans) {
                // An `ownCases` plan is added only for a case it actually names. A slow plan
                // reached through a directory that also holds fast cases would otherwise run in
                // full on an edit that never touches it.
                if (plan.ownCases && !selectionPlanRunsFile(plan.name, file)) continue
                // A layered plan is still a plan to run: it stays in `names` (so it becomes a
                // target) and is listed in `layered` so the runner does not treat it as a named
                // plan that runs in full. Dropping it from `names` would select nothing.
                names.add(plan.name)
                if (plan.layer) layered.add(plan.name)
                added = true
                hits.push(`rule ${rulePath} -> ${plan.name}${plan.layer ? ' (follows --tier)' : ''}`)
            }
            excluded = rule.except.some((entry) => file === entry || file.startsWith(entry))
            if (rule.self && !excluded) {
                if (file.endsWith('.plan.toml')) {
                    const target = path.join(REPO_ROOT, ...file.split('/'))
                    if (fs.existsSync(target)) {
                        targets.add(target)
                        added = true
                        hits.push(`self rule -> ${relativeToRepo(target)}`)
                    }
                } else {
                    // A golden or fixture selects the plans of the directory that owns it. It is
                    // not a plan target itself, so this never names a file the tier filter must
                    // leave alone.
                    const target = nearestPlanDirectory(file)
                    if (target) {
                        targets.add(target)
                        added = true
                        hits.push(`self rule -> ${relativeToRepo(target)} (the directory that owns it)`)
                    }
                }
            }
            if (added) matched = true
        }
        // A file the self rule deliberately excludes (a perf/GC/progen plan) is not a hole: the
    // policy is that an ordinary change does not run it. Everything else is named.
        if (!added && !excluded) unmatched.push(file)
        if (added) reasons.push(`${file}: ${hits.join('; ')}`)
    }
    if (layered.size > 0) matched = true
    return matched
        ? { names: [...names], targets: [...targets], layered: [...layered], unmatched, reasons }
        : null
}

/** A plan named by the selection file, relative to test/plans (a directory or a *.plan.toml). */
function resolveSelectionTarget(name) {
    const base = path.join(PLANS_ROOT, ...name.replace(/\\/g, '/').split('/'))
    for (const candidate of [base, `${base}.plan.toml`]) {
        if (fs.existsSync(candidate)) return candidate
    }
    throw new Error(`plan-selection.json names '${name}', which is not under ${PLANS_ROOT}`)
}

/**
 * The absolute targets a selection resolves to: the named plans (resolved to a file or a
 * directory under test/plans) plus the targets a directory rule or the self rule supplied.
 * `--changed` runs exactly these, and the self-test asks whether a plan is among them.
 */
export function resolvedSelection(plans) {
    const resolved = new Map()
    for (const target of [...plans.names.map(resolveSelectionTarget), ...plans.targets]) {
        resolved.set(path.resolve(target), target)
    }
    return [...resolved.values()]
}

function countPlanFiles(directory) {
    if (!fs.existsSync(directory)) return 0
    let count = 0
    for (const name of fs.readdirSync(directory)) {
        const entry = path.join(directory, name)
        if (fs.statSync(entry).isDirectory()) count += countPlanFiles(entry)
        else if (name.endsWith('.plan.toml')) count += 1
    }
    return count
}

/**
 * The plan files of the fast layer, counted the way a run of it would read them. Every refusal names
 * this number, because the cost of the silent widening was never what it ran: it was that "3 plans"
 * and "the whole layer" printed the same reading, so the widening had no symptom. Naming the size is
 * what makes the refusal a reading of what the widening would have cost.
 */
function fastLayerInventory() {
    const smoke = countPlanFiles(path.join(PLANS_ROOT, 'smoke'))
    const feat = countPlanFiles(path.join(PLANS_ROOT, 'feat'))
    return `${smoke + feat} plan file(s) (smoke ${smoke}, feat ${feat})`
}

/**
 * Refuse a selection that could not be made. The exit code is 2, the same one the milestone stage
 * gives the same question (an unknown layer is never reported as a number), and the two readings a
 * gate cannot tell apart are both printed: what failed, and what the old fallback would have run
 * instead. "Cannot read" alone is the second-worst check; the worst is the one that then runs a
 * layer, which is why the refusal names the layer it declines and how to ask for it deliberately.
 */
function refuseChanged(reason) {
    console.error(`[ERROR] --changed: ${reason}`)
    console.error(
        `[ERROR] --changed: the plans this change touches are unknown, so none is selected and this ` +
        `run fails. The whole fast layer -- ${fastLayerInventory()} -- is what the fallback here used ` +
        `to run, and it reads exactly like a correct selection in every reading a gate takes. ` +
        `Name the layer deliberately with --all (or name a plan) if that is the question.`
    )
    process.exit(2)
}

/**
 * The selection rules, read once and refused like the base: a rules file that cannot be parsed, or
 * that names no rule at all, is not "no rule matched this change" -- it is a selection nobody made,
 * and every change would widen to the whole fast layer behind it.
 */
function loadRulesForSelection() {
    try {
        const rules = loadSelectionRules()
        const directories = loadDirectoryRules()
        if (rules.length === 0 && directories.length === 0) {
            refuseChanged(
                `the selection rules ${relativeToRepo(SELECTION_PATH)} name no rule and no directory ` +
                `rule, so no change can select a plan`
            )
        }
    } catch (error) {
        refuseChanged(`cannot read the selection rules ${relativeToRepo(SELECTION_PATH)} (${error.message})`)
    }
}

function selectChangedTargets(options = {}) {
    const base = changedBase()
    const listPath = options.changedFilesList || null
    const against = listPath ? `the list ${listPath} (against ${base})` : base
    const files = changedFiles(listPath)
    if (files === null) {
        // A base that cannot be read and a list that cannot be read are the same defect: the
        // selection is unknown, and an unknown selection must not become a layer.
        refuseChanged(changeReadFailure || `cannot read ${against}`)
    }
    if (files.length === 0) {
        // A change with no file in it selects no plan. The listing was read, so this zero is
        // measured: it is printed as the zero it is instead of widening to the layer. The run then
        // starts nothing, which test/tools/run-tests.mjs reports as a no-op and not as a green.
        console.log(
            `[INFO] --changed: 0 changed file(s) against ${against}; 0 plan(s) selected, ` +
            `and the whole fast layer -- ${fastLayerInventory()} -- is not run`
        )
        return { targets: [], layered: [], widened: false }
    }
    loadRulesForSelection()
    let plans
    try {
        plans = plansForChanges(files)
    } catch (error) {
        // A rule that names a plan the tree does not hold is a rule nobody can run: it is refused
        // here, naming the plan, instead of becoming a stack trace out of `resolvedSelection`.
        refuseChanged(`${error.message}`)
    }
    if (!plans) {
        // No rule matched. That is the documented policy for a change that only touches a class the
        // project keeps out of `--changed` (perf, GC stress, progen), so the widening stays -- but it
        // is printed as the widening it is, with the files that caused it and what it costs.
        const shown = files.slice(0, 8).join(', ')
        const rest = files.length > 8 ? ` and ${files.length - 8} more` : ''
        console.warn(
            `[WARN] --changed: no rule matches the ${files.length} changed file(s); widening to the ` +
            `whole fast layer (${fastLayerInventory()}): ${shown}${rest}`
        )
        return { targets: [], layered: [], widened: true }
    }
    // A file that matches no rule contributes nothing when another file in the same change does
// match: the aggregate fallback never runs, and the class would otherwise be dropped silently.
// Name it instead, so the hole is visible even before the self-test allowlist is updated.
    if (plans.unmatched && plans.unmatched.length > 0) {
        const shown = plans.unmatched.slice(0, 8).join(', ')
        const rest = plans.unmatched.length > 8 ? ` and ${plans.unmatched.length - 8} more` : ''
        console.warn(`[WARN] --changed: ${plans.unmatched.length} changed file(s) match no rule: ${shown}${rest}`)
    }
    let targets
    let layered
    try {
        targets = resolvedSelection(plans)
        layered = [...new Set(plans.layered.map(resolveSelectionTarget).map((target) => path.resolve(target)))]
    } catch (error) {
        refuseChanged(`${error.message}`)
    }
    const tierNote = layered.length > 0 ? `, ${layered.length} follow --tier` : ''
    console.log(`[INFO] --changed: ${files.length} changed file(s) against ${against} select ${targets.length} plan(s)${tierNote}`)
    for (const reason of (plans.reasons || []).slice(0, 12)) console.log(`[INFO] --changed:   ${reason}`)
    if ((plans.reasons || []).length > 12) {
        console.log(`[INFO] --changed:   and ${plans.reasons.length - 12} more changed file(s) with a rule`)
    }
    return { targets, layered, widened: false }
}

export function main(argv = process.argv.slice(2)) {
    const options = parseArgs(argv)
    if (options.changedFilesList && !options.changed) {
        // The list only ever answers `--changed`; accepting it silently would be a run that reads a
        // flag and does nothing with it.
        console.error('[ERROR] --changed-files is only read together with --changed')
        process.exit(2)
    }
    const selection = options.changed && options.targets.length === 0
        ? selectChangedTargets({ changedFilesList: options.changedFilesList })
        : { targets: options.targets, layered: [], widened: false }
    if (options.list) {
        // `--changed --list`: the selection without the run. It is how a change's plan list is
        // checked (and recorded in a report) without paying for the plans.
        // A target that holds no plan is refused here too, and with the same exit code the run
        // path gives it: printing the directory and exit 0 would answer "what would this run?"
        // with a path no plan is behind, and the two entry points would disagree about a target
        // that means nothing.
        const barren = targetsHoldingNoPlan(selection.targets)
        if (barren.length > 0) {
            for (const target of barren) console.log(noPlanSentence(target))
            process.exit(2)
        }
        for (const target of selection.targets) console.log(target)
        if (selection.targets.length === 0 && selection.layered.length === 0) {
            // The layer is printed only when this selection really is the layer. A zero that is a
            // zero -- no changed file, no plan -- printed the same sentence as a widening, and that
            // is the reading that made the widening invisible in the first place.
            console.log(selection.widened
                ? '(the whole fast layer: smoke + feat)'
                : '(no plan: nothing was selected)')
        }
        return
    }
    const layeredArgs = selection.layered.length > 0
        ? ['--selection-layer', selection.layered.join(',')]
        : []
    const proc = spawnSync(process.execPath, [RUNNER, ...options.runnerArgs, ...selection.targets, ...layeredArgs], {
        cwd: REPO_ROOT,
        stdio: 'inherit',
        env: process.env
    })
    if (proc.error) {
        console.error(proc.error.message)
        process.exit(1)
    }
    process.exit(typeof proc.status === 'number' ? proc.status : 1)
}

if (process.argv[1] && path.resolve(process.argv[1]) === __filename) {
    main()
}