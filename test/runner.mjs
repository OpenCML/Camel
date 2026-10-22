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
const VALUE_FLAGS = new Set(['--tier', '--jobs', '--build'])

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
    // not exist still reaches the runner as the ENOENT the caller must see (H201).
    if (!fs.existsSync(absolute) && fs.existsSync(`${absolute}.plan.toml`)) return `${absolute}.plan.toml`
    return absolute
}

function parseArgs(argv) {
    const runnerArgs = []
    const targets = []
    let changed = false
    let list = false

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

/** Files changed against the base branch, committed or not; null when git cannot answer. */
export function changedFiles() {
    const run = (args) => spawnSync('git', args, { cwd: REPO_ROOT, encoding: 'utf8', windowsHide: true })
    const diff = run(['diff', '--name-only', changedBase()])
    if (diff.error || diff.status !== 0) return null
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
 * a slow plan must not ride a directory into a case it never reads (H205).
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
 * for the plans outside the mirrored tree that a component needs (H161).
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

function planForDirectory(file) {
    for (const rule of loadDirectoryRules()) {
        const prefix = rule.paths.find((entry) => file === entry || file.startsWith(entry))
        if (!prefix) continue
        const rest = file.slice(prefix.length)
        const dir = path.posix.dirname(rest)
        const segments = dir === '.' || dir === '' ? [] : dir.split('/').filter(Boolean)
        for (let length = segments.length; length >= 1; length--) {
            const candidate = path.join(PLANS_ROOT, ...rule.plans, ...segments.slice(0, length))
            if (fs.existsSync(candidate) && fs.statSync(candidate).isDirectory()) return candidate
            const planFile = `${candidate}.plan.toml`
            if (fs.existsSync(planFile) && fs.statSync(planFile).isFile()) return planFile
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
    let matched = false
    for (const file of files) {
        let added = false
        // Directory first: the plan that mirrors the changed path is the narrow selection. The
        // file rules do not replace it, they add the plans that live outside that mirrored tree
        // (a `modules/tiles/` change needs `gpu/**`, which no directory rule can reach).
        const byDirectory = planForDirectory(file)
        if (byDirectory) {
            matched = true
            targets.add(byDirectory)
            added = true
        }
        const rule = rules.find((candidate) =>
            candidate.paths.some((prefix) => file === prefix || file.startsWith(prefix))
        )
        let excluded = false
        if (rule) {
            for (const plan of rule.plans) {
                // An `ownCases` plan is added only for a case it actually names. A slow plan
                // reached through a directory that also holds fast cases would otherwise run in
                // full on an edit that never touches it (H205).
                if (plan.ownCases && !selectionPlanRunsFile(plan.name, file)) continue
                // A layered plan is still a plan to run: it stays in `names` (so it becomes a
                // target) and is listed in `layered` so the runner does not treat it as a named
                // plan that runs in full. Dropping it from `names` would select nothing.
                names.add(plan.name)
                if (plan.layer) layered.add(plan.name)
                added = true
            }
            excluded = rule.except.some((entry) => file === entry || file.startsWith(entry))
            if (rule.self && !excluded) {
                if (file.endsWith('.plan.toml')) {
                    const target = path.join(REPO_ROOT, ...file.split('/'))
                    if (fs.existsSync(target)) {
                        targets.add(target)
                        added = true
                    }
                } else {
                    // A golden or fixture selects the plans of the directory that owns it. It is
                    // not a plan target itself, so this never names a file the tier filter must
                    // leave alone.
                    const target = nearestPlanDirectory(file)
                    if (target) {
                        targets.add(target)
                        added = true
                    }
                }
            }
            if (added) matched = true
        }
        // A file the self rule deliberately excludes (a perf/GC/progen plan) is not a hole: the
    // policy is that an ordinary change does not run it. Everything else is named.
        if (!added && !excluded) unmatched.push(file)
    }
    if (layered.size > 0) matched = true
    return matched
        ? { names: [...names], targets: [...targets], layered: [...layered], unmatched }
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
 * `--changed` runs exactly these, and the self-test asks whether a plan is among them (H201).
 */
export function resolvedSelection(plans) {
    const resolved = new Map()
    for (const target of [...plans.names.map(resolveSelectionTarget), ...plans.targets]) {
        resolved.set(path.resolve(target), target)
    }
    return [...resolved.values()]
}

function selectChangedTargets() {
    const base = changedBase()
    const files = changedFiles()
    if (files === null) {
        console.log(`[INFO] --changed: cannot read ${base}; running the whole fast layer`)
        return { targets: [], layered: [] }
    }
    if (files.length === 0) {
        console.log(`[INFO] --changed: no change against ${base}; running the whole fast layer`)
        return { targets: [], layered: [] }
    }
    const plans = plansForChanges(files)
    if (!plans || (plans.names.length === 0 && plans.targets.length === 0 && plans.layered.length === 0)) {
        console.log(`[INFO] --changed: no rule matches ${files.length} changed file(s); running the whole fast layer`)
        return { targets: [], layered: [] }
    }
    // A file that matches no rule contributes nothing when another file in the same change does
// match: the aggregate fallback never runs, and the class would otherwise be dropped silently.
// Name it instead, so the hole is visible even before the self-test allowlist is updated.
    if (plans.unmatched && plans.unmatched.length > 0) {
        const shown = plans.unmatched.slice(0, 8).join(', ')
        const rest = plans.unmatched.length > 8 ? ` and ${plans.unmatched.length - 8} more` : ''
        console.warn(`[WARN] --changed: ${plans.unmatched.length} changed file(s) match no rule: ${shown}${rest}`)
    }
    const targets = resolvedSelection(plans)
    const layered = [...new Set(plans.layered.map(resolveSelectionTarget).map((target) => path.resolve(target)))]
    const tierNote = layered.length > 0 ? `, ${layered.length} follow --tier` : ''
    console.log(`[INFO] --changed: ${files.length} changed file(s) select ${targets.length} plan(s)${tierNote}`)
    return { targets, layered }
}

export function main(argv = process.argv.slice(2)) {
    const options = parseArgs(argv)
    const selection = options.changed && options.targets.length === 0
        ? selectChangedTargets()
        : { targets: options.targets, layered: [] }
    if (options.list) {
        // `--changed --list`: the selection without the run. It is how a change's plan list is
        // checked (and recorded in a report) without paying for the plans.
        for (const target of selection.targets) console.log(target)
        if (selection.targets.length === 0 && selection.layered.length === 0) {
            console.log('(the whole fast layer: smoke + feat)')
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