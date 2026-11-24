#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The coverage table: for every rule of `test/plan-selection.json`, and for every file no rule
 * covers, the cases a change to that file selects.
 *
 * `node scripts/test.js --changed` answers "which plans" and stops there. A plan is a directory of
 * cases, so the answer is always wider than the change: one header under `src/compile/src/` reaches
 * four plan directories, and a rule that names a plan *directory* prints the same reading as a rule
 * that names one case. This script writes the answer down per rule and per rule-less file, with the
 * case count the run would actually start:
 *
 *   - one row per directory whose files resolve to one selection (kind `dir` or `file`: the rule's
 *     path is a directory or a file), carrying the plan targets, which of them follow `--tier`, and
 *     the number of cases they select at that tier;
 *   - one row per file no rule covers (kind `none`), carrying the group it belongs to. A rule-less
 *     file does not select nothing: `--changed` widens to the whole fast layer when no rule matches
 *     anything, so the row records that widening as a number. (In a change that also touches a
 *     covered file it selects *nothing* instead, and nothing says so; the written page states that
 *     asymmetry.)
 *
 * The number is the point. "`src/compile/src/` selects feat/compile, feat/consistency, feat/trans
 * and four other targets" is a rule name; "it starts 6,184 cases at `--tier fast`" is a reading a
 * reviewer can weigh. Where the rule points at a whole directory the count is that directory's
 * layer, and the row says so by naming the directory as the target it is.
 *
 * The count is not computed here. `loadPlans` in `test/tools/run-tests.mjs` is the runner's own
 * loader -- `[[sweeps]]` copies, back-end expansion, the layer filter and the companion closure --
 * and this script asks it for every distinct selection, exactly as a run would. A second definition
 * of "which cases are in this plan" would agree with a run today and disagree the first time a plan
 * learned a sweep. The matching of a file to its rule is the runner's too: `plansForChanges`, whose
 * reason lines name the rule path that reached the file. Only the widening (`plansForChanges` null)
 * is read from the same rule data the runner loaded, to tell a file that no rule names from a file a
 * rule deliberately excludes (`except`: the perf layer, the GC-stress plans, progen).
 *
 * The tree it reads
 *   Every tracked file, plus every untracked file git would add, minus the table itself. The table
 *   cannot contain itself, and a table generated before its commit must byte-for-byte match one
 *   generated after it: the file's identity does not change when it becomes tracked, so the same
 *   list of paths comes out either way.
 *
 * The inputs it pins
 *   No HEAD is recorded. A table that names the commit it was generated on can never be regenerated
 *   byte-identically from the commit that carries it -- HEAD moves when the table lands. Instead the
 *   three inputs that decide the rows are digests: the selection file, the tracked-file list, and
 *   every plan file's content. A later reader can tell whether the table still describes their tree
 *   without believing a hash that could not have been right.
 *
 * It never writes a plan. The only file it writes is the table (or the `--out` path).
 *
 * Usage
 *   node scripts/coverage-table.mjs                write test/selection/coverage-table.json
 *   node scripts/coverage-table.mjs --check        rebuild and compare bytes, write nothing
 *   node scripts/coverage-table.mjs --print        the headline numbers and the widest rows
 *   node scripts/coverage-table.mjs --explain <path>
 *                                                  one file's selection, with the cases each plan
 *                                                  contributes (why the row is that wide)
 *   node scripts/coverage-table.mjs --tier <name>  the layer the counts are for (default fast)
 *   node scripts/coverage-table.mjs --out <path>   write somewhere else (a probe, not the record)
 */

import fs from 'node:fs'
import path from 'node:path'
import crypto from 'node:crypto'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

import {
    loadDirectoryRules,
    loadSelectionRules,
    plansForChanges,
    resolvedSelection
} from '../test/runner.mjs'
import { loadPlans } from '../test/tools/run-tests.mjs'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.dirname(HERE)
const PLANS_ROOT = path.join(ROOT, 'test', 'plans')
const SELECTION_PATH = path.join(ROOT, 'test', 'plan-selection.json')
const DEFAULT_OUT = path.join(ROOT, 'test', 'selection', 'coverage-table.json')
const VERSION = 1
const TIERS = ['fast', 'slow', 'milestone', 'debug', 'all']

function die(why) {
    console.error(`coverage-table: ${why}`)
    process.exit(2)
}

function parseArgv(argv) {
    const options = { tier: 'fast', out: DEFAULT_OUT, check: false, print: false, explain: null, quiet: false }
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '--tier' && i + 1 < argv.length) options.tier = argv[++i]
        else if (arg.startsWith('--tier=')) options.tier = arg.slice('--tier='.length)
        else if (arg === '--out' && i + 1 < argv.length) options.out = path.resolve(argv[++i])
        else if (arg.startsWith('--out=')) options.out = path.resolve(arg.slice('--out='.length))
        else if (arg === '--explain' && i + 1 < argv.length) options.explain = argv[++i].replace(/\\/g, '/')
        else if (arg.startsWith('--explain=')) options.explain = arg.slice('--explain='.length).replace(/\\/g, '/')
        else if (arg === '--check') options.check = true
        else if (arg === '--print') options.print = true
        else if (arg === '--quiet') options.quiet = true
        else die(`unknown argument '${arg}'`)
    }
    if (!TIERS.includes(options.tier)) die(`--tier must be one of ${TIERS.join(', ')}, got '${options.tier}'`)
    return options
}

/** A repository-relative spelling, the way every line the runner prints names a path. */
function rel(absolute) {
    return path.relative(ROOT, absolute).split(path.sep).join('/')
}

function sha256(text) {
    return `sha256:${crypto.createHash('sha256').update(text).digest('hex')}`
}

function git(args) {
    return spawnSync('git', args, { cwd: ROOT, encoding: 'utf8', windowsHide: true, maxBuffer: 256 * 1024 * 1024 })
}

/**
 * The files a change can touch: the index, plus what git would add (an uncommitted file is still a
 * file a change touches; the self-test walks the same two sets). Ignored paths stay out, so build
 * output and scratch directories never become rows.
 */
function repositoryFiles() {
    const run = git(['ls-files', '-z', '--cached', '--others', '--exclude-standard'])
    if (run.error || run.status !== 0) {
        die(`cannot list the tree: git ls-files failed${run.error ? ` (${run.error.message})` : ''}`)
    }
    return [...new Set(`${run.stdout || ''}`.split('\0').filter(Boolean).map((file) => file.replace(/\\/g, '/')))]
}

/**
 * The rule-less classes of the repository, as path shapes rather than as an allowlist: the point of
 * this table is that a class of files selects nothing, so the class is derived every time and a
 * file that fits no shape is printed as `unclassified` instead of being absorbed.
 *
 *   internal/{reports,reviews,archive,plans,questions,collab}  the notes and pages of the project
 *   internal/*                                                 the two pages at the root of internal
 *   benchmarks, showcase, promo, third_party                   the manual perf layer, vendored code
 *   docs, src                                                  a documentation or source file no
 *                                                              rule reaches -- empty today, kept so
 *                                                              the class is named the day it is not
 *   pypi, .github, .vscode, .husky, repo metadata, test        the classes the self-test already
 *   scaffolding                                                names. `packaging/` is NOT one of
 *                                                              them (`pypi/` is), and neither is
 *                                                              `CHANGELOG.md` (`changes.log` is)
 *
 * `allowlisted` says whether `test/tools/selftest/run.mjs` already names the class (that file's
 * `RULELESS_ALLOWED` is the authority); the finding is the rest.
 */
const RULELESS_GROUPS = [
    { group: 'report-page', allowlisted: false, match: /^internal\/reports\// },
    { group: 'review-page', allowlisted: false, match: /^internal\/reviews\// },
    { group: 'archived-page', allowlisted: false, match: /^internal\/archive\// },
    { group: 'plan-page', allowlisted: false, match: /^internal\/plans\// },
    { group: 'question-page', allowlisted: false, match: /^internal\/questions\// },
    { group: 'collab-page', allowlisted: false, match: /^internal\/collab\// },
    { group: 'internal-page', allowlisted: false, match: /^internal\// },
    { group: 'docs-page', allowlisted: false, match: /^docs\// },
    { group: 'source-no-test', allowlisted: false, match: /^src\// },
    { group: 'benchmark', allowlisted: true, match: /^benchmarks\// },
    { group: 'showcase', allowlisted: true, match: /^showcase\// },
    { group: 'marketing', allowlisted: true, match: /^promo\// },
    { group: 'pypi', allowlisted: true, match: /^pypi\// },
    { group: 'packaging-tree', allowlisted: false, match: /^packaging\// },
    { group: 'vendored', allowlisted: true, match: /^third_party\// },
    { group: 'ci-config', allowlisted: true, match: /^\.github\// },
    { group: 'editor-config', allowlisted: true, match: /^\.vscode\// },
    { group: 'git-hook', allowlisted: true, match: /^\.husky\// },
    { group: 'release-notes', allowlisted: false, match: /^CHANGELOG\.md$/ },
    {
        group: 'repo-metadata',
        allowlisted: true,
        match: /^(LICENSE|ABOUT|ZEN|changes\.log|conanfile\.txt|package-lock\.json|export_matrix\.(csv|md))$|^\.(clang-format|gitattributes|gitignore|prettierrc)$/
    },
    {
        group: 'test-scaffolding',
        allowlisted: true,
        match: /^test\/\.gitignore$|^test\/README\.md$|^test\/plans\/\.gitkeep$|^test\/perf-baseline\.json$|^test\/cases\/opt\/progen\/(README\.md|findings\.json)$/
    }
]

/**
 * The classes a rule excludes on purpose (`except`): the perf layer, the GC-stress plans, the
 * progen plan and the two GPU stress plans. A file here is one the project does not want a change
 * to select, which is a different intent from a file no rule names -- the row records which, and
 * the class is derived from the `except` entry that matched, not from a second list of paths.
 */
function excludedGroup(except) {
    if (/^test\/(cases|plans)\/perf\//.test(except)) return { group: 'perf-layer', allowlisted: true }
    if (/^test\/plans\/feat\/mm\/gc/.test(except)) return { group: 'gc-stress-plan', allowlisted: true }
    if (/^test\/plans\/feat\/opt\/progen\.plan\.toml$/.test(except)) return { group: 'progen-plan', allowlisted: true }
    if (/^test\/plans\/gpu\/tiles_(regions|decorator)\.plan\.toml$/.test(except)) return { group: 'gpu-stress-plan', allowlisted: true }
    return { group: 'excluded-by-rule', allowlisted: false }
}

/** The class of a file that selects nothing, and whether that silence is one the project recorded. */
function groupOf(file) {
    return RULELESS_GROUPS.find((entry) => entry.match.test(file)) || { group: 'unclassified', allowlisted: false }
}

function directoryOf(file) {
    const dir = path.posix.dirname(file)
    return dir === '.' ? '' : dir
}

function main() {
    const options = parseArgv(process.argv.slice(2))
    const outRelative = rel(options.out)

    // The rule data the runner loaded, not a second parse of the file: `paths` and `except` are
    // what the runner matches with, so a rule added tomorrow is a row tomorrow.
    const fileRules = loadSelectionRules()
    const dirRules = loadDirectoryRules()

    const files = repositoryFiles()
        .filter((file) => file !== outRelative)
        .sort()
    if (files.length === 0) die(`${rel(ROOT)} holds no file git would list`)
    const fileSet = new Set(files)

    const layer = loadPlans([], options.tier)
    const layerCases = layer.plans.reduce((sum, plan) => sum + plan.tests.length, 0)
    if (layer.reachedNothing.length > 0) {
        die(`the plan tree ${layer.reachedNothing.join(', ')} holds no plan, so the layer has no size`)
    }

    // One `loadPlans` per distinct selection: the file walk can produce thousands of rows that
    // answer the same question, and the loader is the expensive half.
    const selections = new Map()
    function casesOf(targets, layered) {
        const key = `${targets.join('\n')}\u0000${layered.join('\n')}`
        if (!selections.has(key)) {
            const loaded = loadPlans(targets, options.tier, new Set(layered))
            const perPlan = loaded.plans.map((plan) => ({
                plan: rel(plan.__planPath),
                cases: plan.tests.length
            }))
            selections.set(key, {
                cases: perPlan.reduce((sum, entry) => sum + entry.cases, 0),
                perPlan
            })
        }
        return selections.get(key)
    }

    const rows = []
    // Rows for directory/file rules are grouped by directory: the runner resolves a directory
    // rule by the file's own directory, so two files in one directory answer the same question and
    // a row per file would repeat one number a thousand times.
    const resolvedRows = new Map()

    for (const file of files) {
        const selection = plansForChanges([file])
        if (selection === null) {
            // Two shapes answer null: a file a rule deliberately keeps out of `--changed` (the
            // `except` lists: perf, GC stress, progen, the tiles_regions GPU plan), and a file no
            // rule names at all. The first is a policy, the second is the hole this table exists
            // to measure, so the row says which.
            const fileRule = fileRules.find((rule) => rule.paths.some((entry) => file === entry || file.startsWith(entry)))
            const dirRule = dirRules.find((rule) => rule.paths.some((entry) => file === entry || file.startsWith(entry)))
            const except =
                (fileRule && fileRule.except.find((entry) => file === entry || file.startsWith(entry))) ||
                (dirRule && dirRule.except.find((entry) => file === entry || file.startsWith(entry))) ||
                null
            const group = except === null ? groupOf(file) : excludedGroup(except)
            // Both shapes cost the same alone: `plansForChanges` answers null for a file no rule
            // names and for a file a rule excludes, and `--changed` reads null as "nothing matched"
            // and widens to the whole layer. The `excluded` flag says the silence was intended; it
            // does not make the run smaller. (Measured: a list holding only
            // `test/cases/perf/fib_recursive.cml` prints "falling back to the whole fast layer".)
            rows.push({
                kind: 'none',
                pattern: file,
                group: group.group,
                allowlisted: group.allowlisted,
                excluded: except !== null,
                except,
                cases: layerCases
            })
            continue
        }

        const reason = (selection.reasons && selection.reasons[0]) || ''
        const dirPrefix = (/directory rule (\S+) ->/.exec(reason) || [])[1] || null
        const rulePath = (/(?:^|: |; )rule (\S+) ->/.exec(reason) || [])[1] || null
        // The self rule (`test/plans/`) names the changed file itself, not a path pattern: editing a
        // plan runs that plan, editing a golden runs the plans of the directory that owns it.
        const pattern = /self rule ->/.test(reason) ? file : rulePath || dirPrefix
        if (!pattern) die(`${file} is selected (${reason}) but its reason names no rule path`)
        const targets = resolvedSelection(selection).map((target) => path.resolve(target)).sort()
        const layered = [...new Set(selection.layered.map((name) => path.resolve(PLANS_ROOT, ...name.split('/'))))].sort()
        const dir = directoryOf(file)
        const key = `${dir}\u0000${pattern}\u0000${targets.join(',')}\u0000${layered.join(',')}`
        if (!resolvedRows.has(key)) {
            resolvedRows.set(key, {
                kind: fileSet.has(pattern) ? 'file' : 'dir',
                pattern,
                scope: dir,
                files: 0,
                targets,
                layered
            })
        }
        resolvedRows.get(key).files += 1
    }

    for (const row of resolvedRows.values()) {
        const counted = casesOf(row.targets, row.layered)
        rows.push({
            kind: row.kind,
            pattern: row.pattern,
            scope: row.scope,
            files: row.files,
            plans: row.targets.map((target) => rel(target)),
            layered: row.layered.map((target) => rel(target)),
            cases: counted.cases
        })
    }

    const kindOrder = { dir: 0, file: 1, none: 2 }
    rows.sort(
        (lhs, rhs) =>
            kindOrder[lhs.kind] - kindOrder[rhs.kind] ||
            (lhs.pattern || '').localeCompare(rhs.pattern || '') ||
            (lhs.scope || '').localeCompare(rhs.scope || '')
    )
    // Every file answers exactly one row. A file counted twice (a pattern that resolves to one
    // directory twice) or not at all (a file dropped between the match and the row) would make the
    // totals a reading of the writer rather than of the tree, so the sum is checked here.
    const countedFiles = rows.reduce((sum, row) => sum + (row.kind === 'none' ? 1 : row.files), 0)
    if (countedFiles !== files.length) {
        die(`${files.length} file(s) but the rows count ${countedFiles}: a file is in ${countedFiles > files.length ? 'two rows' : 'no row'}`)
    }

    // The groups, counted from the rows: a class of rule-less files is a row count, never a
    // written-down total.
    const groupCounts = new Map()
    for (const row of rows) {
        if (row.kind !== 'none') continue
        const entry = groupCounts.get(row.group) || { group: row.group, files: 0, excluded: 0, allowlisted: row.allowlisted }
        entry.files += 1
        if (row.excluded) entry.excluded += 1
        groupCounts.set(row.group, entry)
    }
    const groups = [...groupCounts.values()]
        .map((entry) => ({
            group: entry.group,
            files: entry.files,
            excludedByRule: entry.excluded,
            allowlisted: entry.allowlisted
        }))
        .sort((lhs, rhs) => rhs.files - lhs.files || lhs.group.localeCompare(rhs.group))

    const planInput = fs
        .readdirSync(PLANS_ROOT, { recursive: true, withFileTypes: true })
        .filter((entry) => entry.isFile() && entry.name.endsWith('.plan.toml'))
        .map((entry) => rel(path.join(entry.parentPath || entry.path, entry.name)))
        .sort()
        .map((plan) => `${plan}\t${sha256(fs.readFileSync(path.join(ROOT, plan), 'utf8'))}`)
        .join('\n')

    const noneRows = rows.filter((row) => row.kind === 'none')
    const uncoveredRows = noneRows.filter((row) => !row.excluded)
    const table = {
        version: VERSION,
        tool: 'scripts/coverage-table.mjs',
        tier: options.tier,
        inputs: {
            selection: sha256(fs.readFileSync(SELECTION_PATH, 'utf8')),
            files: sha256(files.join('\n')),
            plans: sha256(planInput)
        },
        totals: {
            files: files.length,
            rows: rows.length,
            dirRows: rows.filter((row) => row.kind === 'dir').length,
            fileRows: rows.filter((row) => row.kind === 'file').length,
            noneRows: noneRows.length,
            uncoveredRows: uncoveredRows.length,
            excludedRows: noneRows.length - uncoveredRows.length,
            uncoveredNotAllowlisted: uncoveredRows.filter((row) => !row.allowlisted).length,
            planFiles: planInput.length === 0 ? 0 : planInput.split('\n').length,
            casesInLayer: layerCases
        },
        groups,
        rows
    }

    const text = serialize(table)

    if (options.explain) return explain(options, rows, casesOf, layerCases, fileSet)
    if (options.check) return check(text, options)
    if (options.print) print(table)
    if (!options.print) {
        fs.mkdirSync(path.dirname(options.out), { recursive: true })
        fs.writeFileSync(options.out, text)
        if (!options.quiet) {
            console.log(
                `coverage-table: wrote ${rel(options.out)} -- ${table.totals.rows} row(s) ` +
                    `(${table.totals.dirRows} dir, ${table.totals.fileRows} file, ${table.totals.noneRows} none), ` +
                    `${table.totals.files} file(s), ${table.totals.casesInLayer} case(s) at --tier ${table.tier}`
            )
        }
    }
}

/**
 * The bytes of the table. Rows are one line each: a table read by a person and diffed by a gate
 * should not spend four lines on one row, and a stable key order means a regenerated table differs
 * only where a reading moved.
 */
function serialize(table) {
    const head = { ...table }
    delete head.rows
    delete head.groups
    const lines = ['{']
    const body = [
        ...Object.entries(head).map(([key, value]) => `    ${JSON.stringify(key)}: ${JSON.stringify(value)},`),
        `    "groups": [`,
        ...table.groups.map((group, index) => `        ${JSON.stringify(group)}${index === table.groups.length - 1 ? '' : ','}`),
        `    ],`,
        `    "rows": [`,
        ...table.rows.map((row, index) => `        ${JSON.stringify(row)}${index === table.rows.length - 1 ? '' : ','}`),
        `    ]`
    ]
    lines.push(...body)
    lines.push('}')
    return `${lines.join('\n')}\n`
}

function check(text, options) {
    let current = null
    try {
        current = fs.readFileSync(options.out, 'utf8')
    } catch (error) {
        console.error(`coverage-table: cannot read ${rel(options.out)} (${error.code || error.message}): nothing to compare`)
        process.exit(1)
    }
    const digest = sha256(text)
    if (current === text) {
        console.log(`coverage-table: identical ${digest} (${rel(options.out)}, --tier ${options.tier})`)
        return
    }
    const before = current.split('\n')
    const after = text.split('\n')
    let line = 0
    while (line < before.length && line < after.length && before[line] === after[line]) line++
    console.error(`coverage-table: ${rel(options.out)} is not what this tree generates`)
    console.error(`coverage-table: first difference at line ${line + 1}`)
    console.error(`coverage-table:   recorded: ${(before[line] || '<end of file>').slice(0, 200)}`)
    console.error(`coverage-table:   now:      ${(after[line] || '<end of file>').slice(0, 200)}`)
    console.error(`coverage-table: regenerate with \`node scripts/coverage-table.mjs --tier ${options.tier}\``)
    process.exit(1)
}

/** The longest common directory of a set of labels, so rows that answer one question read as one. */
function commonDirectory(labels) {
    if (labels.length === 1) return labels[0]
    const split = labels.map((label) => label.split('/').filter(Boolean))
    const common = []
    for (let index = 0; ; index++) {
        const segment = split[0][index]
        if (segment === undefined) break
        if (!split.every((parts) => parts[index] === segment)) break
        common.push(segment)
    }
    return common.length === 0 ? '(the tree)' : `${common.join('/')}/`
}

/**
 * The widest answers, with rows that resolve to one selection read as one line: ten fixtures under
 * one plan directory answer the same question, and printing that number ten times hides the other
 * rows rather than making the table more precise.
 */
function print(table) {
    const aggregated = new Map()
    for (const row of table.rows) {
        if (row.kind === 'none') continue
        const key = `${row.kind}\u0000${row.plans.join(',')}\u0000${row.cases}`
        if (!aggregated.has(key)) {
            aggregated.set(key, { kind: row.kind, plans: row.plans, cases: row.cases, files: 0, rows: 0, labels: [] })
        }
        const entry = aggregated.get(key)
        entry.files += row.files
        entry.rows += 1
        entry.labels.push(row.kind === 'dir' ? (row.scope ? `${row.scope}/` : row.pattern) : row.pattern)
    }
    const widest = [...aggregated.values()].sort(
        (lhs, rhs) =>
            rhs.cases - lhs.cases ||
            rhs.files - lhs.files ||
            commonDirectory(lhs.labels).localeCompare(commonDirectory(rhs.labels))
    )
    const totals = table.totals
    console.log(`[coverage-table] tier ${table.tier} | ${totals.files} file(s) | ${totals.rows} row(s)`)
    console.log(
        `[coverage-table] rows: ${totals.dirRows} directory, ${totals.fileRows} file, ${totals.noneRows} rule-less ` +
            `(${totals.excludedRows} excluded by a rule, ${totals.uncoveredRows} no rule at all, ` +
            `${totals.uncoveredNotAllowlisted} of those outside the self-test's classes)`
    )
    console.log(
        `[coverage-table] the widening: a change that no rule selects -- or that a rule excludes -- starts ${totals.casesInLayer} case(s) at --tier ${table.tier}`
    )
    console.log('[coverage-table] widest rows:')
    for (const entry of widest.slice(0, 8)) {
        const targets =
            entry.plans.length === 0
                ? 'none'
                : `${entry.plans.slice(0, 3).join(', ')}${entry.plans.length > 3 ? ` and ${entry.plans.length - 3} more` : ''}`
        console.log(
            `[coverage-table]   ${String(entry.cases).padStart(6)}  ${entry.kind}  ${commonDirectory(entry.labels)}  ` +
                `(${entry.files} file(s) in ${entry.rows} row(s)) -> ${targets}`
        )
    }
    console.log('[coverage-table] rule-less groups:')
    for (const group of table.groups) {
        console.log(
            `[coverage-table]   ${String(group.files).padStart(5)}  ${group.group}${group.allowlisted ? ' (named by the self-test)' : ''}${group.excludedByRule > 0 ? `, ${group.excludedByRule} by a rule's except` : ''}`
        )
    }
}

/**
 * One file's answer, with the cases each plan contributes: the table records the total, and the
 * decomposition is what says whether a rule points at a whole directory or at one plan.
 */
function explain(options, rows, casesOf, layerCases, fileSet) {
    const asked = options.explain.replace(/^\.\//, '').replace(/\/$/, '')
    // The most specific row that answers the path: the row of the file's own directory, else the
    // row of the longest rule path that reaches it (a rule and a scope both name a directory, and
    // the narrower one is the answer for a path inside it).
    const directory = directoryOf(asked)
    const reached = rows.filter(
        (row) => row.kind !== 'none' && (row.scope === asked || row.scope === directory || asked === row.pattern || asked.startsWith(row.pattern))
    )
    const matches = reached.some((row) => row.scope === directory || row.scope === asked)
        ? reached.filter((row) => row.scope === directory || row.scope === asked)
        : reached.sort((lhs, rhs) => rhs.pattern.length - lhs.pattern.length).slice(0, 1)
    if (matches.length === 0) {
        const ruleLess = rows.find((row) => row.kind === 'none' && row.pattern === asked)
        if (ruleLess) {
            console.log(`[coverage-table] ${asked}: kind none, group ${ruleLess.group}`)
            console.log(
                ruleLess.excluded
                    ? `[coverage-table]   rule \`${ruleLess.except}\` excludes it on purpose, but alone it still widens to the whole layer: ${layerCases} case(s)`
                    : `[coverage-table]   no rule names it, so a change alone widens to the whole layer: ${layerCases} case(s)`
            )
            return
        }
        console.error(`coverage-table: no row of ${rel(options.out)} answers '${asked}'`)
        process.exit(1)
    }
    for (const row of matches) {
        const targets = row.plans.map((plan) => path.resolve(ROOT, plan))
        const counted = casesOf(targets, row.layered)
        console.log(
            `[coverage-table] ${asked}: ${row.kind} rule ${row.pattern}${row.scope ? ` (scope ${row.scope}/)` : ''} -> ${counted.cases} case(s)`
        )
        // Which target the cases come from: a rule that names a whole directory buys that
        // directory's layer, a named plan file buys every case it has. The split is the reading a
        // reviewer needs to say whether the rule is the right width.
        const byTarget = new Map()
        for (const entry of counted.perPlan) {
            const absolute = path.resolve(ROOT, entry.plan)
            const owner = targets.find((target) => target === absolute || absolute.startsWith(`${target}${path.sep}`))
            const key = owner ? rel(owner) : '(unattributed)'
            byTarget.set(key, (byTarget.get(key) || 0) + entry.cases)
        }
        const ordered = [...byTarget.entries()].sort((lhs, rhs) => rhs[1] - lhs[1] || lhs[0].localeCompare(rhs[0]))
        for (const [target, cases] of ordered) {
            const isLayered = row.layered.includes(target)
            const isFile = fileSet.has(target)
            const note = isFile ? (isLayered ? 'named, follows --tier' : 'named, runs in full') : '--tier applies'
            console.log(`[coverage-table]   ${String(cases).padStart(5)}  ${target}  (${note})`)
        }
        const perPlan = [...counted.perPlan].sort((lhs, rhs) => rhs.cases - lhs.cases || lhs.plan.localeCompare(rhs.plan))
        console.log('[coverage-table]   widest plans:')
        for (const entry of perPlan.slice(0, 5)) {
            console.log(`[coverage-table]   ${String(entry.cases).padStart(5)}  ${entry.plan}`)
        }
        if (perPlan.length > 5) console.log(`[coverage-table]   and ${perPlan.length - 5} more plan(s)`)
    }
}

main()