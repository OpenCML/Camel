// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The cases a change selects: the answer `--changed` is held to, and the record that makes it
 * reproducible.
 *
 * `node scripts/test.js --changed --list` answers *which plans* the change reaches. A plan is a
 * directory of cases, so that answer is always wider than the change: a change to one `.cml` case
 * selected the whole plan directory it mirrors, and every case of every plan under it ran because
 * "the plan was selected". This script answers the other question -- which *cases* the change
 * touches -- and it is the one the runner executes: `--changed` hands the run this exact list
 * (`--selection-cases`), so the run cannot expand it again.
 *
 * Three causes, and each case carries the one that put it in:
 *   - `case:<file>`   the changed file is one of the `case` inputs of the plan (or lies under a
 *                     case directory, or is a directory naming cases under it). Only the cases that
 *                     name that input are selected: this is the narrowing.
 *   - `plan:<target>` the plan itself was named: a plan file or a golden was edited, or a rule
 *                     (`also`, a file rule) names the plan because it reads the class of file the
 *                     change belongs to. Every case of that target is the reading the rule asked
 *                     for, so the target is kept whole -- and the record says so per case.
 *   - `source:<dir>`  a source directory mirrors the target (`src/kernel/**` -> `feat/trans` and
 *                     friends). Without a coverage table this is the honest residue: the plan's
 *                     cases at the requested layer, not a claim that each one executes the file.
 *                     This is the cause the coverage step replaces, and its count is the measure of
 *                     what still rides on a plan.
 *
 * The record is the proof of reproducibility: same base, same changed files and same tree produce
 * the same `cases` array and the same digest. `--verify` recomputes and compares, so a reading
 * cannot be quoted without the list it came from.
 *
 * Usage
 *   node scripts/selection-cases.mjs [--base <ref>] [--files <list>] [--tier fast]
 *        [--print] [--write <record.json>] [--verify <record.json>] [--head <ref>] [--quiet]
 *        [--names] [--targets <list>]
 *
 * `--base` is the ref the change is read against (`CAMEL_TEST_BASE` when omitted); `--files` reads
 * the changed paths from a file instead, which is how a merge is replayed: the list is
 * `git diff --name-only <merge>^1 <merge>`, and the record names both the merge (`--head`) and the
 * tree the plan rules came from (`tree`, HEAD of the checkout).
 *
 * `--targets` names the plans **this run** starts, one path per line, and makes the record a
 * description of that run: the case list is computed over those targets, so a plan the run does not
 * start contributes no case, and the record cannot name a case the run would then refuse. The
 * narrowing belongs here, at the step that computes the record, not in the runner: a record and a
 * target list that disagree are two different selections, and only the one the run executes is a
 * reading of the run. `test/runner.mjs` passes this for `--changed`, so the record it hands over is
 * computed for exactly the targets it starts.
 *
 * `--names` writes the case rows even when the selection is larger than `CASES_IN_RECORD_MAX`. A
 * stored reading keeps a count and a digest past that size; a *run* cannot: `--selection-cases`
 * starts the cases by name, so a record it reads must hold the names. `--changed` writes its
 * transient list with `--names` for exactly that reason, and a record without its rows is refused
 * by the runner as a digest rather than read as "nothing was selected".
 *
 * A record written here is computed on the spot, always: the records kept in the tree under
 * `test/selection/` are historical readings of past changes, kept so a replay can be compared with
 * them, and no gate step reads one. A gate that read a record kept in the tree would be a gate whose
 * answer depends on the last commit that wrote it, and any commit adding a case would move it.
 *
 * A change that no rule matches selects **nothing**: it is refused (exit 2) and named with the
 * files that matched no rule, instead of widening to the whole fast layer. The widening read like
 * a correct selection in every reading a gate takes (exit 0, a `[SUMMARY]` with no failure), and it
 * fed the case reader a record with no cases -- an empty selection becoming a plan expansion, which
 * is the one thing the reader refuses.
 *
 * The case list is not computed here twice: `test/tools/run-tests.mjs --list-cases` loads the plan
 * tree, expands `[[sweeps]]`, filters by layer and pulls in companions exactly as a run does, and
 * this script narrows that list by the causes above. A second definition of "which cases are in
 * this plan" would agree today and disagree the first time a plan learned a sweep.
 */
import fs from 'node:fs'
import path from 'node:path'
import os from 'node:os'
import crypto from 'node:crypto'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

import { changedFiles, plansForChanges, resolvedSelection } from '../test/runner.mjs'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const REPO_ROOT = path.dirname(HERE)
const RUNNER = path.join(REPO_ROOT, 'test', 'tools', 'run-tests.mjs')
const VERSION = 1
// A record keeps the whole list when it is small enough to read; a whole-layer selection (9,000
// rows, ~1 MB) keeps its count and digest instead, because the list is not a reading anybody reads
// case by case and `--print` reproduces it exactly. The digest is over the same sorted names, so
// the proof does not rest on the rows being stored. `--names` bypasses the cap for the transient
// record a run executes: those rows are not a reading, they are the list the run starts.
const CASES_IN_RECORD_MAX = 1000

function die(why) {
    console.error(`selection-cases: ${why}`)
    process.exit(2)
}

function parseArgv(argv) {
    const options = {
        base: process.env.CAMEL_TEST_BASE || 'origin/develop',
        files: null,
        tier: 'fast',
        print: false,
        write: null,
        verify: null,
        head: null,
        quiet: false,
        names: false,
        targets: null,
    }
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '--base' && i + 1 < argv.length) options.base = argv[++i]
        else if (arg.startsWith('--base=')) options.base = arg.slice('--base='.length)
        else if (arg === '--files' && i + 1 < argv.length) options.files = path.resolve(argv[++i])
        else if (arg.startsWith('--files=')) options.files = path.resolve(arg.slice('--files='.length))
        else if (arg === '--targets' && i + 1 < argv.length) options.targets = path.resolve(argv[++i])
        else if (arg.startsWith('--targets=')) options.targets = path.resolve(arg.slice('--targets='.length))
        else if (arg === '--tier' && i + 1 < argv.length) options.tier = argv[++i]
        else if (arg.startsWith('--tier=')) options.tier = arg.slice('--tier='.length)
        else if (arg === '--print') options.print = true
        else if (arg === '--quiet') options.quiet = true
        else if (arg === '--names') options.names = true
        else if (arg === '--write' && i + 1 < argv.length) options.write = path.resolve(argv[++i])
        else if (arg.startsWith('--write=')) options.write = path.resolve(arg.slice('--write='.length))
        else if (arg === '--verify' && i + 1 < argv.length) options.verify = path.resolve(argv[++i])
        else if (arg.startsWith('--verify=')) options.verify = path.resolve(arg.slice('--verify='.length))
        else if (arg === '--head' && i + 1 < argv.length) options.head = argv[++i]
        else if (arg.startsWith('--head=')) options.head = arg.slice('--head='.length)
        else die(`unknown argument '${arg}'`)
    }
    return options
}

function git(args) {
    const run = spawnSync('git', args, { cwd: REPO_ROOT, encoding: 'utf8', windowsHide: true })
    if (run.error || run.status !== 0) return null
    return `${run.stdout || ''}`.trim()
}

function repoRelative(absolute) {
    return path.relative(REPO_ROOT, absolute).split(path.sep).join('/')
}

function under(absolute, target) {
    const a = path.resolve(absolute)
    const t = path.resolve(target)
    return a === t || a.startsWith(`${t}${path.sep}`)
}

/**
 * The plans a run starts, read from `--targets`. An empty list is refused rather than passed on:
 * `test/tools/run-tests.mjs#loadPlans` reads "no target" as the whole plan tree, so an empty target
 * list would compute a record for 386 plans while the run starts none -- the same empty-selection
 * read as a layer that this whole file exists to keep apart.
 */
function readTargetList(file) {
    let text
    try {
        text = fs.readFileSync(file, 'utf8')
    } catch (error) {
        die(`cannot read the target list ${file} (${error.message})`)
    }
    const targets = text
        .split(/\r?\n/)
        .map((line) => line.trim())
        .filter(Boolean)
        .map((line) => path.resolve(REPO_ROOT, line))
    if (targets.length === 0) die(`the target list ${file} names no plan, so it names no case to compute`)
    return targets
}

/** A plan name from the selection file, as the plan file or directory it resolves to. */
function planTarget(name) {
    const base = path.join(REPO_ROOT, 'test', 'plans', ...String(name).replace(/\\/g, '/').split('/'))
    if (fs.existsSync(base)) return base
    if (fs.existsSync(`${base}.plan.toml`)) return `${base}.plan.toml`
    return null
}

function digestOf(names) {
    const sorted = [...names].sort()
    return `sha256:${crypto.createHash('sha256').update(sorted.join('\n'), 'utf8').digest('hex')}`
}

/** The plan tree's cases, through the run's own loader: `run-tests.mjs --list-cases`. */
function listCases(targets, layered, tier) {
    const args = [RUNNER, ...targets, '--list-cases', '--tier', tier]
    if (layered.length > 0) args.push('--selection-layer', layered.join(','))
    const run = spawnSync(process.execPath, args, {
        cwd: REPO_ROOT,
        encoding: 'utf8',
        windowsHide: true,
        // A whole-layer listing is ~9,000 rows: the default 1 MiB buffer turns that into ENOBUFS,
        // which reads as "the loader could not start" and would have been a wrong answer about a
        // selection this script had already computed.
        maxBuffer: 128 * 1024 * 1024,
    })
    if (run.error) die(`cannot start the plan loader (${run.error.message})`)
    if (run.status !== 0) {
        const text = `${run.stdout || ''}${run.stderr || ''}`
        // An unknown option is not a selection. The runner refuses one in a single line naming the
        // word, and wrapping that in "refused this selection" turns a version mismatch into a
        // statement about this change -- which is how a caller newer than the tree reads as "this
        // change selects no plan". Measured 2026-11-24: a lane cut before `--list-cases` existed in
        // the tree answered the host's listing with a stack trace, and this script reported it as a
        // selection that could not be read.
        const unknown = /^\[ERROR\] unknown option '([^']+)'/m.exec(text)
        if (unknown) {
            const tree = /^\[ARGV\] this tree is [^\n]*/m.exec(text)
            die(
                `this tree's runner does not know ${unknown[1]}: the tree is older than the caller, ` +
                    `so nothing was selected -- this is not an empty selection${tree ? `\n${tree[0]}` : ''}`,
            )
        }
        const why = text.trim().split(/\r?\n/).slice(-4).join(' / ')
        die(`the plan loader refused this selection (exit ${run.status}): ${why}`)
    }
    const rows = []
    let reported = null
    for (const line of `${run.stdout || ''}`.split(/\r?\n/)) {
        const text = line.trim()
        if (text.startsWith('{')) {
            try {
                rows.push(JSON.parse(text))
            } catch {
                die(`the plan loader printed a row this script cannot read: ${text.slice(0, 80)}`)
            }
        } else if (text.startsWith('cases:')) {
            reported = Number(text.slice('cases:'.length).trim())
        }
    }
    if (reported !== null && reported !== rows.length) {
        die(`the plan loader said ${reported} case(s) and printed ${rows.length}; a listing that disagrees with itself is not a reading`)
    }
    return rows
}

/** Whether a case row's input is the changed file, either direction (a directory can be either side). */
function namesFile(row, file) {
    if (!row.case) return false
    const input = row.case.replace(/\/+$/, '')
    const changed = file.replace(/\/+$/, '')
    return input === changed || input.startsWith(`${changed}/`) || changed.startsWith(`${input}/`)
}

/**
 * The selection for one change: the plan-level targets, the cases they expand to, and the cause
 * that keeps each case. Also returns the widened form: a change no rule matches runs the whole fast
 * layer, and that is recorded as the layer it is, never as a case list this script invented.
 *
 * `filesOverride` is the changed-file list a record holds: a record is verified by replaying the
 * files it names, never by asking git for today's diff, because "the same reading" must not depend
 * on the tree having moved.
 */
function select(options, filesOverride = null) {
    const files = filesOverride !== null
        ? filesOverride.map((file) => String(file).replace(/\\/g, '/')).filter(Boolean)
        : options.files
          ? changedFiles(options.files)
          : (() => {
                process.env.CAMEL_TEST_BASE = options.base
                return changedFiles()
            })()
    if (files === null) die(`cannot read the changed files against ${options.base} (${options.files ? 'the list file' : 'git'})`)
    if (files.length === 0) {
        return {
            files: [],
            widened: false,
            empty: true,
            targets: [],
            layered: [],
            planCases: 0,
            cases: [],
            causes: [],
        }
    }
    const plans = plansForChanges(files)
    if (!plans) {
        // No rule matched. This used to widen to the whole fast layer, and that widening is the
        // defect this refusal replaces: it ran 9,894 cases for a change that names no plan, it
        // printed a `[SUMMARY]` no gate could tell from a correct selection, and -- because a
        // whole-layer selection keeps a count and a digest instead of its rows -- the record it
        // left behind held no case at all, so the reader refused it as "an empty selection must
        // not become a plan expansion" *after* the layer had already been chosen. A change that
        // selects nothing is a fact about the change or about the rules, and running everything is
        // not an answer to it: name the plans, or add the rule.
        const shown = files.slice(0, 8).join(', ')
        const rest = files.length > 8 ? ` and ${files.length - 8} more` : ''
        die(
            `no rule in test/plan-selection.json matches the ${files.length} changed file(s): ` +
                `${shown}${rest}, so the selection is empty; an empty selection must not become a ` +
                'plan expansion -- name the plans (node scripts/test.js <plan>) if that is the question'
        )
    }
    // The record describes a run: when the caller names the targets that run starts (`--targets`),
    // those are the targets, and a plan the change selected but the run does not start contributes
    // no case. That is the narrowing this file owes its reader -- a record and a target list that
    // disagree are two selections, and a run can only execute one of them.
    const targets = options.targets === null ? resolvedSelection(plans) : readTargetList(options.targets)
    const layered = [...new Set(plans.layered)]
        .map(planTarget)
        .filter(Boolean)
        .filter((plan) => targets.some((target) => under(plan, target)))
    const rows = listCases(targets, layered, options.tier)
    const causes = plans.causes || []
    const keepWhole = causes.filter((cause) => cause.kind !== 'case')
    const caseFiles = [...new Set(causes.filter((cause) => cause.kind === 'case').map((cause) => cause.file))]
    const byName = new Map(rows.map((row) => [row.name, row]))
    // The layer applies to every case by its own declaration. `--list-cases` keeps a *named* plan's
    // entries whatever the layer (that is right for a person naming a plan), but a plan reached by a
    // change is not a person naming it: a slow case must not run in the fast gate merely because its
    // plan file was selected. The daily layer runs it anyway; the gate's list is the fast cases the
    // change touches.
    const layerKeeps = (row) =>
        options.tier === 'all' ? row.tier !== 'debug' : options.tier === 'debug' ? row.tier === 'debug' : row.tier === options.tier
    const kept = new Set()
    for (const row of rows) {
        if (!layerKeeps(row)) continue
        const planPath = path.resolve(REPO_ROOT, row.plan)
        const whole = keepWhole.find((cause) => under(planPath, cause.target))
        if (whole) {
            kept.add(row.name)
            row.cause = `${whole.kind}:${repoRelative(whole.target)}`
            continue
        }
        const changed = caseFiles.find((file) => namesFile(row, file))
        if (changed) {
            kept.add(row.name)
            row.cause = `case:${changed}`
        }
    }
    // Companions: a differential or back-end case pulls in another case (`refs`). A kept case's
    // companion is part of the reading it needs, so the closure is closed here -- and a companion
    // that is not in the loaded list is a selection this script cannot complete, said out loud
    // rather than silently omitted.
    const missing = []
    const queue = [...kept]
    while (queue.length > 0) {
        const name = queue.pop()
        const row = byName.get(name)
        if (!row) continue
        for (const ref of row.refs || []) {
            if (!byName.has(ref)) {
                missing.push(`${name} -> ${ref}`)
                continue
            }
            if (kept.has(ref)) continue
            kept.add(ref)
            byName.get(ref).cause = `companion:${name}`
            queue.push(ref)
        }
    }
    if (missing.length > 0) {
        die(`a selected case needs a companion the plan set does not hold: ${missing.slice(0, 3).join(', ')}`)
    }
    const cases = rows
        .filter((row) => kept.has(row.name))
        .map((row) => ({ name: row.name, plan: row.plan, case: row.case, tier: row.tier, cause: row.cause }))
        .sort((lhs, rhs) => lhs.name.localeCompare(rhs.name))
    return {
        files,
        widened: false,
        empty: false,
        targets: targets.map(repoRelative),
        layered: layered.map(repoRelative),
        // What the plan-level selection expands to before the case-level causes narrow it: the
        // number the old answer ran. Keeping both in the record is what lets "before/after" be a
        // reading of one replay rather than two runs on different trees.
        planCases: rows.length,
        cases,
        causes,
    }
}

function recordOf(options, selection) {
    const head = options.head || git(['rev-parse', 'HEAD'])
    const tree = git(['rev-parse', 'HEAD'])
    const files = [...selection.files].sort()
    const base = {
        version: VERSION,
        base: options.base,
        head,
        tree,
        tier: options.tier,
        files,
        files_digest: digestOf(files),
        widened: selection.widened === true,
        empty: selection.empty === true,
        plans: selection.targets,
        layered: selection.layered,
        // The plan-level expansion (what `--changed --list` + a run used to execute) and the
        // case-level selection beside it: one replay, two answers, so the difference is a number
        // this record carries rather than a comparison of two runs on two trees.
        plan_case_count: selection.planCases,
        // A record holds a selection, never a verdict. A merge whose reading was never taken stays
        // without one: the field is null and the note says why, because a blank is not "no reds"
        // and an invented pass would be the one thing the readings store cannot recover from.
        verdict: null,
        verdict_note:
            'selection only: no test verdict was recorded for this change set, and none is inferred from it',
    }
    // `widened` is still written (as `false`): every record taken before the widening was removed
    // carries the field, and `--verify` compares it.
    const names = selection.cases.map((row) => row.name)
    const recorded = options.names === true || names.length <= CASES_IN_RECORD_MAX
    // The summary of why, kept even when the rows are not: a count per cause is the reading that
    // says whether a change narrowed a plan or rode it whole, and it costs three numbers.
    const causeCounts = {}
    for (const row of selection.cases) {
        const kind = row.cause.split(':')[0]
        causeCounts[kind] = (causeCounts[kind] ?? 0) + 1
    }
    return {
        ...base,
        case_count: names.length,
        case_digest: digestOf(names),
        cause_counts: causeCounts,
        // The list and the cause that keeps each case: the digest is a key, the list is the
        // reading. A verifier compares both, so a case cannot move from one cause to another and
        // still read as the same selection.
        cases: recorded ? selection.cases.map((row) => ({ name: row.name, cause: row.cause })) : [],
        cases_recorded: recorded,
    }
}

function printSelection(options, selection, record) {
    if (options.quiet) return
    console.log(
        `selection: base=${record.base} tree=${(record.tree || '?').slice(0, 9)} files=${record.files.length} ` +
            `plans=${record.plans.length} widened=${record.widened} tier=${record.tier}`
    )
    for (const row of record.cases) console.log(JSON.stringify(row))
    console.log(`plan-cases: ${record.plan_case_count}`)
    console.log(`cases: ${record.case_count}`)
    console.log(`case-digest: ${record.case_digest}`)
}

/** Same input, same tree, same list: the record is compared field by field, not by its hash alone. */
function verify(file, options) {
    let held
    try {
        held = JSON.parse(fs.readFileSync(file, 'utf8'))
    } catch (error) {
        die(`cannot read the record ${file} (${error.message})`)
    }
    // A replay narrows nothing: the record's own file list decides its targets, on this tree.
    // `--targets` cannot arrive here -- the dispatch refuses the pair -- so the comparison is always
    // one record against its own change, never against another run.
    const replay = { ...options, base: held.base, tier: held.tier || options.tier }
    const selection = select(replay, held.files)
    const fresh = recordOf(replay, selection)
    const problems = []
    if (fresh.files_digest !== held.files_digest) problems.push(`changed files differ: ${held.files_digest} vs ${fresh.files_digest}`)
    if (fresh.widened !== held.widened) problems.push(`widened differs: ${held.widened} vs ${fresh.widened}`)
    if (fresh.case_count !== held.case_count) problems.push(`case count differs: ${held.case_count} vs ${fresh.case_count}`)
    if (fresh.plan_case_count !== held.plan_case_count) {
        problems.push(`plan-level count differs: ${held.plan_case_count} vs ${fresh.plan_case_count}`)
    }
    if (fresh.case_digest !== held.case_digest) problems.push(`case digest differs: ${held.case_digest} vs ${fresh.case_digest}`)
    if (!held.widened && held.cases_recorded === true && Array.isArray(held.cases)) {
        const heldByName = new Map(held.cases.map((row) => [row.name, row]))
        const freshByName = new Map(fresh.cases.map((row) => [row.name, row]))
        for (const [name, row] of freshByName) {
            const was = heldByName.get(name)
            if (!was) problems.push(`selected now, not in the record: ${name}`)
            else if ((was.cause || '') !== (row.cause || '')) problems.push(`cause differs for ${name}: ${was.cause} vs ${row.cause}`)
        }
        for (const name of heldByName.keys()) if (!freshByName.has(name)) problems.push(`in the record, not selected now: ${name}`)
    }
    if (problems.length > 0) {
        for (const problem of problems.slice(0, 20)) console.error(`  ${problem}`)
        console.error(`selection-cases: ${problems.length} difference(s); the record is not reproducible on this tree`)
        process.exit(1)
    }
    console.log(
        `selection-cases: identical -- ${held.case_count} case(s), ${held.case_digest}, ` +
            `tree ${(held.tree || '?').slice(0, 9)}, base ${held.base}`
    )
    return 0
}

const options = parseArgv(process.argv.slice(2))
if (options.verify) {
    // A record is replayed against the targets its own file list selects; a target list beside it
    // would compare one record with another run's selection, which is two questions in one answer.
    if (options.targets !== null) die('--verify replays the record\'s own targets; --targets names a run\'s, and the two disagree by definition')
    process.exit(verify(options.verify, options))
}
const selection = select(options)
const record = recordOf(options, selection)
if (options.write) {
    fs.mkdirSync(path.dirname(options.write), { recursive: true })
    fs.writeFileSync(options.write, `${JSON.stringify(record, null, 2)}\n`)
}
if (options.print) printSelection(options, selection, record)
if (!options.write && !options.print) printSelection(options, selection, record)