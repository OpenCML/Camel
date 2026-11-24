// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import path from 'path'
import fs from 'fs'
import os from 'os'
import { spawnSync } from 'child_process'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
const TEST_ROOT = path.dirname(__filename)
const REPO_ROOT = path.dirname(TEST_ROOT)
const PLANS_ROOT = path.join(TEST_ROOT, 'plans')
const RUNNER = path.join(TEST_ROOT, 'tools', 'run-tests.mjs')
// The layer report `--tier-report` starts: what a layer's entry count is made of (declared entries
// against the copies a `[[sweeps]]` entry derives). It lives beside the plan inventory in `scripts`
// because it reads the tree the way that check does, and it is started through this file so that
// `node scripts/test.js --tier-report` and `npm run test` name one entry point.
const TIER_REPORT = path.join(REPO_ROOT, 'scripts', 'tier-report.mjs')
// The case-level selection: one engine, used both to print the cases a change selects
// (`--changed --list-cases`) and to hand the run the exact list to execute. A second
// implementation of "which cases are in this selection" is the shape this step removes.
const SELECTION_ENGINE = path.join(REPO_ROOT, 'scripts', 'selection-cases.mjs')
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
    // `--list-cases`: print the *cases* a selection resolves to, not only its plans. A plan is a
    // directory of cases and the change touches some of them; the case list is the answer the
    // selection can be held to, and `scripts/selection-cases.mjs` records it.
    let listCases = false
    // `--tier-report` asks what a layer's entry count is made of -- the entries the plan files
    // declare and the entries a `[[sweeps]]` entry derives from them -- instead of running
    // anything. It is a query about the plan tree, so its own script (`scripts/tier-report.mjs`)
    // is started with the layer and nothing else; a target, a job count or a build would name a run
    // that this flag says is not happening.
    let tierReport = false
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
        } else if (arg === '--list-cases') {
            listCases = true
        } else if (arg === '--tier-report') {
            tierReport = true
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
        listCases,
        tierReport,
        // Whether a target was actually named on the command line. `targets` cannot answer it: it
        // defaults to the fast layer, and `--tier-report` must refuse a target, not the default.
        namedTarget: targets.length > 0,
        changedFilesList,
    }
}

/** The layer `--tier <name>` names in the forwarded arguments, or null when the run did not name one. */
function tierRequested(runnerArgs) {
    for (let i = 0; i < runnerArgs.length; i++) {
        if (runnerArgs[i] === '--tier' && i + 1 < runnerArgs.length) return runnerArgs[i + 1]
        if (runnerArgs[i].startsWith('--tier=')) return runnerArgs[i].slice('--tier='.length)
    }
    return null
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
 * directory, so `modules/tensor/ops/linalg_ops.cpp` selects `test/plans/feat/modules/tensor` (and
 * `test/cases/compile/enum/shapes.cml` selects `test/plans/feat/compile`) without a per-subsystem
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
        // `also`: plans that read every change under this directory, beside the one its path
        // mirrors. A directory rule names one mirror prefix (`test/cases/` -> `test/plans/feat`), and
        // some plans read a whole class of files the mirror has no room for: the front-end AST
        // snapshot hashes every `.cml` of the repository, so a case that lands without being
        // recorded there is a file that changed and no plan read. The entry follows the rule's
        // `layer` default like any other (`--tier` still filters a slow plan's slow tests).
        also: (rule.also || []).map((entry) => selectionPlan(entry, rule.layer === true)),
        // `except` on a directory rule is the same exclusion a file rule can carry: those paths are
        // kept out of `--changed` on purpose (`test/cases/perf/` is the manual perf layer), so a
        // change there must select nothing -- not the mirrored plan, not an `also` plan.
        except: (rule.except || []).map((entry) => String(entry).replace(/\\/g, '/')),
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
        const also = rule.except.some((entry) => file === entry || file.startsWith(entry)) ? [] : rule.also
        const rest = file.slice(prefix.length)
        const dir = path.posix.dirname(rest)
        const segments = dir === '.' || dir === '' ? [] : dir.split('/').filter(Boolean)
        for (let length = segments.length; length >= 1; length--) {
            const candidate = path.join(PLANS_ROOT, ...rule.plans, ...segments.slice(0, length))
            if (fs.existsSync(candidate) && fs.statSync(candidate).isDirectory()) {
                return { target: candidate, prefix, also }
            }
            const planFile = `${candidate}.plan.toml`
            if (fs.existsSync(planFile) && fs.statSync(planFile).isFile()) {
                return { target: planFile, prefix, also }
            }
        }
        // The directory still matched: its `also` plans read this change even when no plan mirrors
        // the path (`target: null`), and the caller must not read that as "no rule matched".
        return { target: null, prefix, also }
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

let caseInputCache = null

/**
 * Every file a plan names as a case input, read once. A change to one of these is a *case-level*
 * cause: the case whose input changed is the case the change touches, not every case of the plan
 * that happens to mirror the directory the file lives in. This is the table the selection answers
 * from, and it is a reading of the plan files, not a coverage record -- it says which case *names*
 * the file, never which source file a case executed.
 */
export function caseInputFiles() {
    if (caseInputCache) return caseInputCache
    caseInputCache = new Set()
    for (const planPath of walkPlanPaths(PLANS_ROOT)) {
        for (const file of planCaseFiles(planPath)) caseInputCache.add(file)
    }
    return caseInputCache
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
    // Why each selected target is in the selection, one entry per (target, changed file): `case`
    // when the changed file is a case input of a plan under the target, `source` when a source
    // directory mirrors the target, `plan` when a plan file, a golden or an `also` rule named it.
    // A `case` cause narrows the plan to the cases that name the file; the other two keep the
    // target's cases as the plan declares them. The distinction is the answer to "did this case get
    // selected because the change touches it, or because its whole plan was selected": only `case`
    // can say the former.
    const causes = []
    let matched = false
    for (const file of files) {
        let added = false
        const hits = []
        // Is this file an input of some case? Read once per file: the same answer decides the kind
        // of every cause this file contributes.
        const caseInput = caseInputFiles().has(path.resolve(REPO_ROOT, file))
        // Directory first: the plan that mirrors the changed path is the narrow selection. The
        // file rules do not replace it, they add the plans that live outside that mirrored tree
        // (a `modules/tiles/` change needs `gpu/**`, which no directory rule can reach).
        const byDirectory = planForDirectory(file)
        if (byDirectory && byDirectory.target) {
            matched = true
            targets.add(byDirectory.target)
            added = true
            causes.push({
                target: byDirectory.target,
                kind: caseInput ? 'case' : 'source',
                file,
            })
            hits.push(`directory rule ${byDirectory.prefix} -> ${relativeToRepo(byDirectory.target)}`)
        }
        const rule = rules.find((candidate) =>
            candidate.paths.some((prefix) => file === prefix || file.startsWith(prefix))
        )
        let excluded = false
        if (rule) {
            const rulePath = rule.paths.find((prefix) => file === prefix || file.startsWith(prefix))
            // `also`: the plans that read every change under a directory rule's paths. They are
            // added beside the mirrored plan, never instead of it, and a layered one is named the
            // way the file rules name theirs (in `names`, and in `layered` so the runner filters it
            // by --tier). A path a file rule excludes keeps its exclusion: a perf, GC-stress or
            // progen change selects nothing -- not even a plan that reads every `.cml` of the tree
            // -- and the self-test's "these plans stay unselectable" readings are that promise.
            excluded = rule.except.some((entry) => file === entry || file.startsWith(entry))
            if (!excluded && byDirectory) {
                for (const plan of byDirectory.also) {
                    names.add(plan.name)
                    if (plan.layer) layered.add(plan.name)
                    matched = true
                    added = true
                    hits.push(
                        `directory rule ${byDirectory.prefix} -> also ${plan.name}${plan.layer ? ' (follows --tier)' : ''}`
                    )
                }
            }
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
                // The rule names this plan because it can catch the change. When the changed file is a case
                // input, the cause is that case: the plans this rule names are narrowed to the cases
                // that name the file (an `ownCases` entry says the same thing about itself, and a plain
                // entry is no wider a reading than its own cases). Keeping the whole target here would
                // select every case of the plan because the plan was selected.
                causes.push({
                    target: absoluteSelectionTarget(plan.name),
                    kind: caseInput ? 'case' : 'plan',
                    file,
                })
                hits.push(`rule ${rulePath} -> ${plan.name}${plan.layer ? ' (follows --tier)' : ''}`)
            }
            if (rule.self && !excluded) {
                if (file.endsWith('.plan.toml')) {
                    const target = path.join(REPO_ROOT, ...file.split('/'))
                    if (fs.existsSync(target)) {
                        targets.add(target)
                        added = true
                        causes.push({ target, kind: 'plan', file })
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
                        causes.push({ target, kind: 'plan', file })
                        hits.push(`self rule -> ${relativeToRepo(target)} (the directory that owns it)`)
                    }
                }
            }
        }
        // `also`: the plans a directory rule names for every change under its paths, beside the one
        // the path mirrors. They are added beside it, never instead of it, and a layered one is
        // named the way the file rules name theirs (in `names`, and in `layered` so the runner
        // filters it by --tier). A path a file rule excludes keeps its exclusion: a perf,
        // GC-stress or progen change selects nothing -- not even a plan that reads every `.cml` of
        // the tree -- and the self-test's "these plans stay unselectable" readings are that promise.
        if (!excluded && byDirectory) {
            for (const plan of byDirectory.also) {
                names.add(plan.name)
                if (plan.layer) layered.add(plan.name)
                matched = true
                added = true
                // `also` names a plan that reads *every* change under the directory (the front-end
                // snapshot hashes every `.cml`), so its cases are the reading, not an expansion of
                // the changed file's own case: the cause keeps the target whole.
                causes.push({ target: absoluteSelectionTarget(plan.name), kind: 'plan', file })
                hits.push(
                    `directory rule ${byDirectory.prefix} -> also ${plan.name}${plan.layer ? ' (follows --tier)' : ''}`
                )
            }
        }
        // A file the self rule deliberately excludes (a perf/GC/progen plan) is not a hole: the
    // policy is that an ordinary change does not run it. Everything else is named.
        if (added) matched = true
        if (!added && !excluded) unmatched.push(file)
        if (added) reasons.push(`${file}: ${hits.join('; ')}`)
    }
    if (layered.size > 0) matched = true
    return matched
        ? { names: [...names], targets: [...targets], layered: [...layered], unmatched, reasons, causes }
        : null
}

/** A plan named by the selection file, as the absolute target it resolves to (for the causes). */
function absoluteSelectionTarget(name) {
    try {
        return resolveSelectionTarget(name)
    } catch {
        return path.join(PLANS_ROOT, ...String(name).replace(/\\/g, '/').split('/'))
    }
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
 * The line a **measured empty** selection prints: what it counted, what it selected, and that the
 * layer is a different question. `changed: 0 target(s)` is the reading a gate can grep for, because
 * a run that starts nothing and a run of the whole layer end the same way -- exit 0 and a
 * `[SUMMARY]` with no failure -- so the count has to be a line of its own.
 */
function announceEmptySelection(against) {
    return (
        `changed: 0 target(s) -- no file differs from ${against}, so no plan is selected and no plan ` +
        `starts. The whole fast layer (${fastLayerInventory()}) is a layer and not this change: name a ` +
        `plan, or pass --all, to run it`
    )
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
        // A change with no file in it selects no plan, and the listing was read, so this zero is a
        // measured zero -- and a measured zero runs no plan. It used to hand the empty target list
        // to `test/tools/run-tests.mjs`, whose `loadPlans` reads "no target" as the whole plan tree:
        // 0 changed files ran 336 plan files and 8,000-odd tests, and every reading a gate takes --
        // exit 0, a `[SUMMARY]` with no failure -- read the same as a correct selection of one plan.
        // Naming that widening and then doing it is not a fix; the fallback is what the number was.
        // So an empty selection now selects nothing and starts nothing (`--selection-empty` keeps the
        // runner from reading "no target" as a layer), and the layer is run only when it is asked
        // for: a plan name, or `--all`.
        console.log(`[INFO] ${announceEmptySelection(against)}`)
        return { targets: [], layered: [], widened: false, empty: true }
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
        // No rule matched, so the selection is empty -- and an empty selection is refused here, not
        // widened to the whole fast layer. The widening used to be the policy for a change that
        // only touches a class the project keeps out of `--changed` (perf, GC stress, progen), and
        // it is the path this refusal replaces: it ran 9,894 cases for a change that names no plan,
        // it printed a `[SUMMARY]` no gate can tell from a correct selection, and the record it left
        // held no case at all (a whole-layer selection keeps a count and a digest, not its rows), so
        // the run it was handed to refused it as "an empty selection must not become a plan
        // expansion" -- after the layer had already been chosen. `--all` and a plan name are how the
        // layer is asked for deliberately.
        const shown = files.slice(0, 8).join(', ')
        const rest = files.length > 8 ? ` and ${files.length - 8} more` : ''
        refuseChanged(
            `no rule in ${relativeToRepo(SELECTION_PATH)} matches the ${files.length} changed ` +
                `file(s): ${shown}${rest}, so the selection is empty; an empty selection must not ` +
                'become a plan expansion'
        )
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
    if (options.tierReport) {
        // A query about the plan tree, not a run: `scripts/tier-report.mjs` reads the plans through
        // this runner's own loader and starts no test, so it needs no build, no install and no
        // target. A target or `--changed` beside it would name a run this flag says is not
        // happening, and silently ignoring one is how a flag stops meaning what it says.
        if (options.changed || options.namedTarget || options.list) {
            console.error(
                '[ERROR] --tier-report names a layer and nothing else: it reads the plan tree and ' +
                    'runs no plan, so a target, --all/--suite or --changed has no run to narrow',
            )
            process.exit(2)
        }
        const tier = tierRequested(options.runnerArgs)
        const quiet = options.runnerArgs.includes('--quiet') ? ['--quiet'] : []
        const proc = spawnSync(process.execPath, [TIER_REPORT, ...(tier ? ['--tier', tier] : []), ...quiet], {
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
    const selection = options.changed && options.targets.length === 0
        ? selectChangedTargets({ changedFilesList: options.changedFilesList })
        : { targets: options.targets, layered: [], widened: false }
    // The arguments that describe *this* change to the case engine: the same base (through
    // `CAMEL_TEST_BASE`/`--files`, never recomputed differently) and the same layer, plus the target
    // list below (`--targets`).
    const engineArgs = (extra) => {
        const args = [SELECTION_ENGINE]
        const tier = tierRequested(options.runnerArgs)
        if (tier) args.push('--tier', tier)
        if (options.changedFilesList) args.push('--files', options.changedFilesList)
        return args.concat(extra)
    }
    if (options.list && options.listCases) {
        console.error('[ERROR] --list names plans and --list-cases names cases: ask one question')
        process.exit(2)
    }
    if (options.listCases) {
        // `--changed --list-cases`: the cases this change selects, printed as JSON rows and never
        // started. It is the selection the record holds and the reading a second run is compared
        // with -- same change, same tree, same list. `--names` because the answer *is* the rows: a
        // capped record would print `cases: 5765` beside nothing, which reads as a list nobody has.
        const proc = spawnSync(process.execPath, engineArgs(['--print', '--names']), {
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
            // A zero that is a zero -- no changed file, no plan -- gets its own sentence. It used to
            // share one with a widening, which is what made the widening invisible; `--changed` no
            // longer widens (a change no rule matches is refused, see selectChangedTargets).
            console.log('(no plan: nothing was selected)')
        }
        return
    }
    const layeredArgs = selection.layered.length > 0
        ? ['--selection-layer', selection.layered.join(',')]
        : []
    // A measured empty selection travels as an explicit flag, not as an absent target list: "no
    // target" is the whole plan tree to `loadPlans`, so the two readings have to differ before they
    // reach it. It is the only way `--changed` starts nothing, and it is a measured zero, not a
    // failed selection -- an unreadable base still refuses with exit 2 above.
    // The case list, before the run: `--changed` hands over the exact cases this change selects, so
    // the run is the list the record can be checked against. It is not a widening and not a second
    // selection -- the run still loads the same targets; it only stops expanding them past the
    // cases that name the changed files. A case list that cannot be computed is a refusal: running
    // the plans anyway would answer a wider question under this change's name.
    let caseArgs = []
    // A case selection that is empty is a *measured* zero for this layer (a change whose plan has
    // no case in it, or whose cases all belong to another layer), not a plan expansion: it travels
    // as `--selection-empty` with no target, and the run starts nothing and says which zero it is.
    let runTargets = selection.targets
    let runEmpty = selection.empty === true
    // The record is a *change's* list, so only a change run has one. `--changed` hands over the cases
    // this change selects, and there the narrowing is the selection: the run is the list a second run
    // of the same change can be checked against. A layer run (`-Plan all --tier fast`) asks for the
    // layer and nothing else, and `scripts/selection-cases.mjs` narrows every list it computes to the
    // files that differ from the base -- so the record written for a layer run names a change's
    // cases. Handing that over made the layer run start a subset of the layer while printing the
    // layer's name: on 2026-11-24 `-Plan all --tier fast` read 9279 cases over 355 plans while the
    // tree's own `scripts/tier-report.mjs` said 9919, and nothing in the output said which it was.
    // `run-tests.mjs#refuseRecordThatNarrowsLayer` refuses such a record now; a layer run is given
    // none at all, so `keep` stays null and the run starts the layer it names.
    if (options.changed && selection.targets.length > 0) {
        const caseFile = path.join(os.tmpdir(), `camel-selection-${process.pid}-${Date.now()}.json`)
        // The targets this run is about to start, in the form the engine narrows its list to. The record
        // is computed for exactly this list, so it names the cases of *this* run and not a case the
        // run would refuse -- the narrowing belongs where the record is computed, which is why
        // `--selection-cases` stays a guard against a record from somewhere else rather than the
        // mechanism the gate depends on. Passed as a file rather than on the command line because a
        // target is a path: one with a space or a comma in it would be read as two targets or one
        // broken one.
        const targetFile = path.join(os.tmpdir(), `camel-targets-${process.pid}-${Date.now()}.txt`)
        fs.writeFileSync(targetFile, `${selection.targets.join('\n')}\n`)
        // `--names`: this record is not a reading to keep, it is the list the run starts, so it must
        // hold the names even when the selection is larger than a stored reading keeps. Without it a
        // wide selection stored a count and a digest, the run was handed a record with no cases, and
        // `--selection-cases` refused it -- exit 2 and 0 rows for every merge that selected more
        // than a thousand cases.
        const engine = spawnSync(
            process.execPath,
            engineArgs(['--write', caseFile, '--quiet', '--names', '--targets', targetFile]),
            {
                cwd: REPO_ROOT,
                stdio: 'inherit',
                env: process.env
            }
        )
        try {
            fs.rmSync(targetFile, { force: true })
        } catch {
            /* already gone */
        }
        if (engine.error || engine.status !== 0) {
            console.error(
                `[ERROR] --changed: the cases this change selects could not be computed` +
                    `${engine.error ? ` (${engine.error.message})` : ''}; the plans are not run in their ` +
                    'place, because "many cases" is not the same answer as "these cases".'
            )
            process.exit(2)
        }
        let record = null
        try {
            record = JSON.parse(fs.readFileSync(caseFile, 'utf8'))
        } catch (error) {
            console.error(`[ERROR] --changed: the case list could not be read back (${error.message})`)
            process.exit(2)
        }
        if (record.case_count === 0) {
            console.log(
                `[INFO] --changed: ${record.plan_case_count} case(s) would run at the plan level, but this ` +
                    `change selects none of them at --tier ${record.tier}; nothing is started`
            )
            runTargets = []
            runEmpty = true
            try {
                fs.rmSync(caseFile, { force: true })
            } catch {
                /* already gone */
            }
        } else {
            caseArgs = ['--selection-cases', caseFile]
        }
    }
    const proc = spawnSync(
        process.execPath,
        [RUNNER, ...options.runnerArgs, ...runTargets, ...layeredArgs, ...(runEmpty ? ['--selection-empty'] : []), ...caseArgs],
        {
            cwd: REPO_ROOT,
            stdio: 'inherit',
            env: process.env
        }
    )
    if (caseArgs.length > 0) {
        // The run has read the list by now (it exits after loading and starting the cases); the
        // file is this process's own, named by its pid, so removing it cannot take another run's.
        try {
            fs.rmSync(caseArgs[1], { force: true })
        } catch {
            /* already gone */
        }
    }
    if (proc.error) {
        console.error(proc.error.message)
        process.exit(1)
    }
    process.exit(typeof proc.status === 'number' ? proc.status : 1)
}

if (process.argv[1] && path.resolve(process.argv[1]) === __filename) {
    main()
}