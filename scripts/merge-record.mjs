#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The record a merge leaves when a gate passed it, and the check that refuses a merge commit that
 * has none.
 *
 * A gate refuses a *push*, not a commit. That distinction is the hole this file exists to close: a
 * merge made in the checkout that had `develop` checked out stayed on the local branch when the gate
 * refused it, and the next push of anything at all -- one backlog row -- carried it. The gate then
 * measured that row, said yes, and the refused merge became public with no reading behind it.
 *
 * So the judgement moves to the commit. `write` is called by the merge tool after every gate has
 * read the merged tree, and stores what those gates read under `.git/camel-passed/<sha>.json`;
 * `verify` is called by the pre-push hook with the two ends of the push, walks the first-parent
 * chain it is about to publish, and refuses when a merge commit in it has no record.
 *
 * The record says more than "passed" on purpose. A record that named only the commit would leave
 * the next reader unable to tell a merge that was read from one that was waved through:
 *
 *   both commits     the merge and the first parent it lands on, both checked against git here
 *   the plan set     the plan names and a hash of the set, so "the same plans" is checkable
 *   the baseline     which commit the comparison was measured on, and where that reading came from
 *   the three counts fixed, new, changed -- and the cases in each
 *
 * Usage:
 *   node scripts/merge-record.mjs write --diff <diff.json> --merge <sha> --first-parent <sha>
 *        --second-parent <sha> --branch <name> --tree <path> --out <dir>
 *        [--baseline-source <text>] [--gate <name> ...]
 *   node scripts/merge-record.mjs verify <remote-sha> <local-sha>
 *   node scripts/merge-record.mjs verify-promotion <local-sha>
 *
 * `verify` is the per-merge question and `verify-promotion` is the per-tip question a develop push
 * asks instead; they are two checks and neither substitutes for the other (see `verify-promotion`
 * below).
 *
 * Exit codes, the same three-way convention the gates use: 0 the claim holds, 1 it does not (a
 * merge with no record, or a reading that is not a pass), 2 it could not be decided (the range or
 * the record directory cannot be read), which must never read as "this merge is wrong".
 */
import { execFileSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { planSetHash } from './plan-set.mjs'

const ZERO = '0'.repeat(40)
const SHA = /^[0-9a-f]{40}$/

function say(line) {
    console.log(line)
}

function refuse(code, why) {
    say(`merge-record: ${why}`)
    process.exit(code)
}

function git(cwd, args, allowFailure = false) {
    try {
        return execFileSync('git', ['-C', cwd, ...args], { encoding: 'utf8' }).replace(/\n+$/, '')
    } catch (error) {
        if (allowFailure) return null
        refuse(2, `git ${args.join(' ')} failed in ${cwd}: ${error.stderr?.trim() || error.message}`)
    }
}

function arg(name) {
    const at = process.argv.indexOf(name)
    return at >= 0 ? process.argv[at + 1] : null
}

function argAll(name) {
    const out = []
    for (let i = 0; i < process.argv.length; i += 1) {
        if (process.argv[i] === name && i + 1 < process.argv.length) out.push(process.argv[i + 1])
    }
    return out
}

function passedDir(root) {
    return path.join(git(root, ['rev-parse', '--path-format=absolute', '--git-common-dir']), 'camel-passed')
}

function recordPath(root, sha) {
    return path.join(passedDir(root), `${sha}.json`)
}

// --- write -------------------------------------------------------------------
function doWrite() {
    const diffFile = arg('--diff')
    const merge = arg('--merge')
    const firstParent = arg('--first-parent')
    const secondParent = arg('--second-parent')
    const branch = arg('--branch') || ''
    const tree = arg('--tree') || process.cwd()
    const outDir = arg('--out')
    const baselineSource = arg('--baseline-source') || 'unknown'
    const gates = argAll('--gate')
    // What the measured run's own exit was. Absent means `0`: every record written before this field
    // existed was written on a path that had a green gate, and rewriting that history is not worth a
    // migration. `unknown` is refused nowhere -- it is a real state -- but `verify` will not treat it
    // as a pass (see below).
    const exit = (arg('--exit') || '0').trim()
    // The cases that were red at the snapshot this commit is and green on the tip a backfill ends at.
    // Written only by `merge-lane.ps1 -MergeCommit -KnownAtSnapshot`, and only after that tool has
    // checked each entry against a reading of the tip (`run.json`'s revision and the case's status).
    // This file stores the claim with its evidence; `verify` refuses the record again when a `new`
    // case is not one of them, so the allowance cannot widen a record written without the list.
    const knownFile = arg('--known-at-snapshot') || ''
    // The same shape one layer up: a file that has no two-line header in this tree and was given one
    // by a later commit. `merge-lane.ps1 -MergeCommit -KnownHeaderAtSnapshot` reads the three facts
    // out of the repository before it writes this; here they are stored as their own field so a
    // reader can tell a head allowance from a reading allowance without opening either.
    const knownHeaderFile = arg('--known-header-at-snapshot') || ''
    if (exit !== '0' && exit !== 'unknown') {
        refuse(2, `--exit takes '0' or 'unknown'; it got '${exit}'`)
    }
    // What this record's numbers are about. `rows` is a comparison of two trees' readings; `none` is a
    // merge with nothing to measure -- every file it touches is documentation, no plan is selected,
    // and running the suite anyway would be a reading about some other change. The two are written as
    // two different claims rather than one claim with zeros in it, because a reader (and the verify
    // path a push runs) has to be able to tell `measured and green` from `nothing to measure`, and a
    // zero cannot say which. `none` is refused unless it says why and names the gate that decided it.
    const measured = arg('--measured') || 'rows'
    const why = (arg('--why') || '').trim()
    if (measured !== 'rows' && measured !== 'none') {
        refuse(2, `--measured takes 'rows' or 'none'; it got '${measured}'`)
    }
    const docsOnly = measured === 'none'
    if (docsOnly) {
        if (!outDir) refuse(2, 'write needs --out <dir>')
        if (!why) refuse(2, 'write --measured none needs --why: a record that says nothing was measured has to say why nothing was')
        if (!gates.includes('docs-only')) {
            refuse(2, "write --measured none needs --gate docs-only: the gate that decided there was nothing to measure is part of the claim")
        }
    }
    if ((!diffFile && !docsOnly) || !outDir) refuse(2, 'write needs --diff <diff.json> and --out <dir>')
    for (const [name, value] of [['--merge', merge], ['--first-parent', firstParent], ['--second-parent', secondParent]]) {
        if (!value || !SHA.test(value)) refuse(2, `write needs ${name} <full commit sha>; it was '${value ?? ''}'`)
    }

    if (!fs.existsSync(diffFile) && !docsOnly) {
        refuse(2, `no comparison at ${diffFile}: a record of a merge nobody compared would be a claim with no reading`)
    }
    let diff
    if (docsOnly) {
        // The empty plan set, stated rather than left out: `verify` recomputes the hash of the names it
        // is given, so a document that named no set at all could not be told from one whose set was
        // dropped. The baseline is named by its sha and by the sentence that says it was not measured.
        diff = {
            planSet: { names: [], hash: planSetHash([]) },
            baseline: { sha: firstParent, source: 'none (documentation only)', file: '', cases: null },
            fixed: [],
            new: [],
            changed: [],
        }
    } else {
        try {
            diff = JSON.parse(fs.readFileSync(diffFile, 'utf8'))
        } catch (error) {
            refuse(2, `${diffFile} is not JSON: ${error.message}`)
        }
    }
    const fixed = Array.isArray(diff.fixed) ? diff.fixed : null
    const regressed = Array.isArray(diff.new) ? diff.new : null
    const changed = Array.isArray(diff.changed) ? diff.changed : null
    if (!fixed || !regressed || !changed) refuse(2, `${diffFile} does not hold the three sets (fixed, new, changed)`)

    // The allowance is read here and checked against the comparison that was just written, not
    // against a sentence: a list that names a case the comparison calls `new` and nothing else is a
    // list; one that names a case which is not new says nothing and would let the next one through.
    let knownAtSnapshot = null
    if (knownFile) {
        let list
        try {
            list = JSON.parse(fs.readFileSync(knownFile, 'utf8'))
        } catch (error) {
            refuse(2, `--known-at-snapshot ${knownFile} is not JSON: ${error.message}`)
        }
        const entries = Array.isArray(list.entries) ? list.entries : []
        if (!SHA.test(String(list.tip || ''))) refuse(2, '--known-at-snapshot needs "tip": the commit the evidence was read on')
        if (entries.length === 0) refuse(2, '--known-at-snapshot names no entry: an empty allowance is a refusal with extra steps')
        for (const entry of entries) {
            const removed = Boolean(entry.removedAt)
            const ok = removed
                ? Boolean(entry.case) && SHA.test(String(entry.snapshot || '')) && SHA.test(String(entry.removedAt || '')) && Boolean(entry.note)
                : Boolean(entry.case) && SHA.test(String(entry.snapshot || '')) && SHA.test(String(entry.fixedAt || ''))
            if (!ok) {
                refuse(2, `--known-at-snapshot entry ${JSON.stringify(entry)} needs ${removed ? 'case, snapshot, removedAt and note' : 'case, snapshot and fixedAt'}`)
            }
            if (entry.snapshot !== merge) {
                refuse(2, `--known-at-snapshot entry ${entry.case} is red at ${entry.snapshot.slice(0, 9)}, and this record is about ${merge.slice(0, 9)}`)
            }
        }
        const names = new Set(entries.map((entry) => entry.case))
        // A comparison names a case as `plan/test`; the `new` set carries that name beside the
        // reading that moved, so the comparison is on the name and not on the object that holds it.
        const regressedNames = regressed.map((row) => (typeof row === 'string' ? row : row?.name)).filter(Boolean)
        // The same fact in its second shape: a case this merge adds and that is red in its own tree.
        // The first parent never had it, so it is not a regression; it is still red where it stands,
        // and the allowance covers it on the same three facts.
        const addedRedNames = (Array.isArray(diff.addedRed) ? diff.addedRed : []).map((row) => (typeof row === 'string' ? row : row?.name)).filter(Boolean)
        const candidates = [...new Set([...regressedNames, ...addedRedNames])]
        const extra = candidates.filter((name) => !names.has(name))
        if (extra.length > 0) {
            refuse(2, `--known-at-snapshot does not cover ${extra.join(', ')}: the comparison calls it new or adds it red and the list does not name it`)
        }
        const missing = entries.filter((entry) => !candidates.includes(entry.case))
        if (missing.length > 0) {
            refuse(2, `--known-at-snapshot names ${missing.map((entry) => entry.case).join(', ')}, which the comparison neither calls new nor adds red: a stale allowance is refused rather than carried`)
        }
        // Which of the two each entry is, written down: "red where it stood" and "added red" are
        // different claims about a tree, and a report that counts them has to be able to split them.
        const withKind = entries.map((entry) => (entry.removedAt
            ? { case: entry.case, kind: 'removed-red', snapshot: entry.snapshot, removedAt: entry.removedAt, note: String(entry.note || '') }
            : {
                case: entry.case,
                kind: regressedNames.includes(entry.case) ? 'new' : 'added-red',
                snapshot: entry.snapshot,
                fixedAt: entry.fixedAt,
            }))
        knownAtSnapshot = { tip: list.tip, evidence: list.run ? String(list.run) : '', entries: withKind }
    }
    let knownHeaderAtSnapshot = null
    if (knownHeaderFile) {
        let list
        try {
            list = JSON.parse(fs.readFileSync(knownHeaderFile, 'utf8'))
        } catch (error) {
            refuse(2, `--known-header-at-snapshot ${knownHeaderFile} is not JSON: ${error.message}`)
        }
        const entries = Array.isArray(list.entries) ? list.entries : []
        if (!SHA.test(String(list.tip || ''))) refuse(2, '--known-header-at-snapshot needs "tip": a commit the fix has to be an ancestor of')
        if (entries.length === 0) refuse(2, '--known-header-at-snapshot names no entry: an empty allowance is a refusal with extra steps')
        for (const entry of entries) {
            if (!entry.file || !SHA.test(String(entry.snapshot || '')) || !SHA.test(String(entry.fixedAt || ''))) {
                refuse(2, `--known-header-at-snapshot entry ${JSON.stringify(entry)} needs file, snapshot and fixedAt`)
            }
            if (entry.snapshot !== merge) {
                refuse(2, `--known-header-at-snapshot entry ${entry.file} has no header at ${entry.snapshot.slice(0, 9)}, and this record is about ${merge.slice(0, 9)}`)
            }
        }
        knownHeaderAtSnapshot = { tip: list.tip, entries }
    }
    if (!diff.planSet || !Array.isArray(diff.planSet.names) || !diff.planSet.hash) {
        refuse(2, `${diffFile} does not name the plan set: a record that cannot say which plans were read is not checkable`)
    }
    // The hash is recomputed from the names rather than copied through. A field this file only
    // carries is a field nobody checks: the document would say "these 5 plans" beside a hash of some
    // other set, both from the same writer, and the record would look checkable while not being so.
    // The algorithm is `plan-set.mjs`, the one `merge-readings.mjs` keys a baseline by -- shared, so
    // the two cannot answer this question differently. A disagreement is exit 2 and not 1: the
    // reading is not refuted here, it is unreadable as a claim about a plan set.
    const planHash = planSetHash(diff.planSet.names)
    if (planHash !== diff.planSet.hash) {
        refuse(
            2,
            `${diffFile} names ${diff.planSet.names.length} plan(s) that hash to ${planHash}, but it says ${diff.planSet.hash}: ` +
                'the plan set and its hash have to be the same set, and only the writer that produced both can repair it',
        )
    }

    // The commit this claim is about, read from git and not from the caller: the parents are what a
    // later reader checks the record against, so the record must not be able to disagree with them.
    const parents = git(tree, ['rev-list', '--parents', '-n', '1', merge], true)
    if (parents === null) refuse(1, `${merge} is not a commit in ${tree}`)
    const fields = parents.trim().split(/\s+/)
    if (fields.length < 3) refuse(1, `${merge} is not a merge commit (${fields.length - 1} parent(s)); nothing to record`)
    if (fields[1] !== firstParent) {
        refuse(1, `${merge}'s first parent is ${fields[1]}, not ${firstParent}: the record would describe a different merge`)
    }

    // A record is a claim that the merge passed. The tool refuses a merge before it gets here, so a
    // call that arrives with a non-empty `new` or `changed` is a caller that took a different path
    // to this write -- and the one thing this file must never do is certify it. The one exception is
    // a case the allowance above named, verified and put in the record with the commit that fixed it;
    // `changed` has no such path and stays a refusal.
    const allowedCases = new Set(knownAtSnapshot ? knownAtSnapshot.entries.map((entry) => entry.case) : [])
    const unallowed = regressed.filter((row) => !allowedCases.has(typeof row === 'string' ? row : row?.name))
    if (unallowed.length > 0 || changed.length > 0) {
        refuse(1, `refusing to record a pass for ${merge.slice(0, 9)}: ${unallowed.length} newly red and ${changed.length} changed, and a pass record with those in it would be read as a pass`)
    }

    // A batch writes one record per merge commit, because the hook asks about every merge commit on
    // the first-parent chain and a batch is a run of them. The rows those records carry were read
    // once, off the batch's TIP: that is the only tree that was measured, and a record that let a
    // reader believe each commit was measured on its own tree would be the quieter lie. So the batch
    // is named in the record -- how many lanes, which ones, and the tip whose tree the reading came
    // off -- and a single-lane merge has no `batch` field at all rather than a batch of one.
    const batchSize = Number(arg('--batch-size') || 0)
    const batchLanes = argAll('--batch-lane')
    const batchTip = arg('--batch-tip') || merge
    if (batchSize > 0) {
        if (batchLanes.length !== batchSize) {
            refuse(2, `--batch-size ${batchSize} and ${batchLanes.length} --batch-lane: a batch record that cannot name its lanes cannot say what it measured`)
        }
        if (!SHA.test(batchTip)) refuse(2, `--batch-tip must be a full commit sha; it was '${batchTip}'`)
        if (batchTip === merge && batchSize === 1 && batchLanes[0] === branch) {
            refuse(2, 'a batch of one lane is a single merge: write it without the batch fields')
        }
    }

    const record = {
        sha: merge,
        firstParent,
        secondParent,
        laneBranch: branch,
        tree,
        ...(batchSize > 0
            ? {
                  batch: {
                      size: batchSize,
                      lanes: batchLanes,
                      tip: batchTip,
                      // What the numbers below describe, said in the record rather than inferred from
                      // `sha === tip`: the reading is the batch's, taken once on the tip's tree.
                      measured: batchTip === merge ? 'this commit is the batch tip' : `the batch tip ${batchTip.slice(0, 9)}; this commit was not measured on its own tree`,
                  },
              }
            : {}),
        planSet: { names: diff.planSet.names, hash: diff.planSet.hash, count: diff.planSet.names.length },
        baseline: {
            sha: diff.baseline?.sha ?? firstParent,
            source: baselineSource,
            file: diff.baseline?.file ?? '',
            cases: diff.baseline?.cases ?? null,
        },
        // cases is the number of rows the baseline held. For a docs-only merge there is no baseline and
        // no rows, so it is 0 and not null: a document that says `nothing was measured` beside a null
        // cannot be told from one whose baseline went missing.
        readings: { cases: docsOnly ? 0 : (diff.baseline?.cases ?? null), fixed: fixed.length, new: regressed.length, changed: changed.length },
        cases: { fixed, new: regressed, changed },
        // Only on a backfill: the cases a historical merge could not pass because the tree has since
        // been fixed forward, each with the commit that fixed it and the tip the evidence was read on.
        // Absent from every record written on the ordinary path, so a reader can tell a merge that was
        // green where it stood from one that was red there and is green now.
        ...(knownAtSnapshot ? { knownAtSnapshot } : {}),
        // The other layer of the same backfill: a file this tree added without its two-line header,
        // and the commit that wrote it. `header` below says the check passed on the pushed tree; this
        // says which files it passed because a later commit had given them their header. Absent from
        // every record written on the ordinary path.
        ...(knownHeaderAtSnapshot ? { knownHeaderAtSnapshot } : {}),
        gates,
        // The exit code the measured run ended with, as the gate reported it. `unknown` is the third
        // state and not a zero in disguise: the gate printed its readings and could not obtain the
        // run's exit code (its closing session did not return; the gate cuts it and exits 7). A
        // record that says `unknown` may exist -- the reading is real and worth keeping -- but it is
        // not a pass: `verify` refuses it exactly like a record that is missing.
        exit,
        // Only present when there was nothing to measure: a record that always carried `measured` would
        // make every row record claim a mode it never chose, and the reader of a file on disk cannot
        // ask the writer what it meant.
        ...(docsOnly ? { measured: 'none', why } : {}),
        measuredAt: new Date().toISOString(),
        host: os.hostname(),
        // Which merge position wrote this: one lane, or a batch. A reader needs it to know whether the
        // reading beside `sha` is that commit's own or its batch tip's -- the batch fields say the
        // second, and this says who asked.
        writtenBy: arg('--written-by') || 'internal/tools/merge-lane.ps1',
    }
    fs.mkdirSync(outDir, { recursive: true })
    const target = path.join(outDir, `${merge}.json`)
    fs.writeFileSync(target, `${JSON.stringify(record, null, 2)}\n`)
    say(`merge-record: wrote ${target}`)
    say(
        `merge-record: ${merge.slice(0, 9)} first parent ${firstParent.slice(0, 9)}, ${record.planSet.count} plan(s), ` +
            `baseline ${String(record.baseline.sha).slice(0, 12)} (${baselineSource}), ` +
            `fixed=${fixed.length} new=${regressed.length} changed=${changed.length}, exit=${exit}`,
    )
    process.exit(0)
}

// --- verify ------------------------------------------------------------------
function doVerify() {
    const [remoteSha, localSha] = process.argv.slice(3)
    if (!remoteSha || !localSha || !SHA.test(localSha) || !(SHA.test(remoteSha) || remoteSha === ZERO)) {
        refuse(2, `verify needs <remote-sha> <local-sha> as full commit shas; it got '${remoteSha ?? ''}' and '${localSha ?? ''}'`)
    }
    const root = process.cwd()
    const dir = passedDir(root)

    // The commits this push adds to the branch. `--first-parent` is the walk that matters: a merge
    // is published as one commit on that chain, and a commit reachable only through a side branch is
    // part of a merge that already had its reading.
    let range
    if (remoteSha === ZERO) {
        // The branch does not exist on the far side, so the whole of its first-parent history is
        // being published. Every merge in it needs a record for the same reason a single one does.
        range = git(root, ['rev-list', '--first-parent', localSha], true)
    } else {
        range = git(root, ['rev-list', '--first-parent', `${remoteSha}..${localSha}`], true)
    }
    if (range === null) {
        refuse(
            2,
            `the commits between ${remoteSha.slice(0, 9)} and ${localSha.slice(0, 9)} cannot be listed here; ` +
                'a range this checkout cannot read is not an empty one -- fetch and push again',
        )
    }
    const commits = range.split('\n').map((line) => line.trim()).filter(Boolean)

    const problems = []
    let merges = 0
    for (const sha of commits) {
        const parents = (git(root, ['rev-list', '--parents', '-n', '1', sha], true) || '').trim().split(/\s+/)
        if (parents.length < 3) continue
        merges += 1
        const file = path.join(dir, `${sha}.json`)
        const subject = (git(root, ['log', '-1', '--format=%s', sha], true) || '').trim()
        if (!fs.existsSync(file)) {
            problems.push(`${sha.slice(0, 9)}  ${subject}  -- no pass record at ${file}`)
            continue
        }
        let record
        try {
            record = JSON.parse(fs.readFileSync(file, 'utf8'))
        } catch (error) {
            problems.push(`${sha.slice(0, 9)}  ${subject}  -- the record at ${file} does not parse: ${error.message}`)
            continue
        }
        if (record.sha !== sha) {
            problems.push(`${sha.slice(0, 9)}  ${subject}  -- the record at ${file} is about ${String(record.sha).slice(0, 9)}`)
            continue
        }
        if (record.firstParent !== parents[1]) {
            problems.push(
                `${sha.slice(0, 9)}  ${subject}  -- the record says it landed on ${String(record.firstParent).slice(0, 9)}, ` +
                    `git says ${parents[1].slice(0, 9)}`,
            )
            continue
        }
        // `measured: none` is a claim of its own, and it is checked as one: it has to say why nothing
        // was measured, it has to name the gate that decided that, and it cannot carry rows. The
        // alternative -- letting it through on its zeros -- would make `nothing to measure` and `read
        // nothing` the same record, which is the confusion this field exists to end.
        if (record.measured === 'none') {
            const reason = String(record.why || '').trim()
            if (!reason) {
                problems.push(`${sha.slice(0, 9)}  ${subject}  -- the record says nothing was measured and does not say why`)
                continue
            }
            if (!Array.isArray(record.gates) || !record.gates.includes('docs-only')) {
                problems.push(
                    `${sha.slice(0, 9)}  ${subject}  -- the record says nothing was measured, and no gate in it says ` +
                        `'docs-only' (it lists: ${(Array.isArray(record.gates) ? record.gates : []).join(', ') || 'none'})`,
                )
                continue
            }
            if (Number(record.readings?.cases) !== 0) {
                problems.push(
                    `${sha.slice(0, 9)}  ${subject}  -- the record says nothing was measured and reports ` +
                        `${String(record.readings?.cases)} case(s)`,
                )
                continue
            }
        }
        // The plan set the record claims, checked against itself. `write` already refused a document
        // whose hash did not describe its names, so a record that reached the disk through this
        // program holds a coherent pair -- which means a disagreeing pair was not written by it. That
        // is exactly why the check belongs here as well as there: the record is a file on disk, and a
        // file on disk is what the hook reads, not the code path that meant to write it. A hand-made
        // record can still state a coherent set; it cannot any longer state an incoherent one and be
        // waved through, which is the difference between a checkable field and a field with a name.
        const plan = record.planSet
        if (!plan || !Array.isArray(plan.names) || !plan.hash) {
            problems.push(
                `${sha.slice(0, 9)}  ${subject}  -- the record does not name the plan set, so it cannot say which plans were read`,
            )
            continue
        }
        const recomputed = planSetHash(plan.names)
        if (recomputed !== plan.hash) {
            problems.push(
                `${sha.slice(0, 9)}  ${subject}  -- the record's ${plan.names.length} plan name(s) hash to ${recomputed}, ` +
                    `but it says ${plan.hash}: the set and its hash are not the same set`,
            )
            continue
        }
        if (plan.count !== plan.names.length) {
            problems.push(
                `${sha.slice(0, 9)}  ${subject}  -- the record says ${String(plan.count)} plan(s) and names ${plan.names.length}`,
            )
            continue
        }
        const read = record.readings || {}
        if (Number(read.new) > 0 || Number(read.changed) > 0) {
            // The one way a record with a newly red case is still a pass: the case was red at the
            // snapshot the merge commit *is* and green on the tip this push ends at, and the writer
            // verified both from a reading rather than from a sentence. `knownAtSnapshot` says which
            // cases and at which two commits; a `new` set it does not cover is refused exactly as
            // before, and so is a covered one that names a case the record does not carry.
            const known = Array.isArray(record.knownAtSnapshot?.entries) ? record.knownAtSnapshot.entries : []
            const covered = new Set(known.map((entry) => entry.case))
            const uncovered = (Array.isArray(record.cases?.new) ? record.cases.new : [])
                .map((row) => (typeof row === 'string' ? row : row?.name))
                .filter(Boolean)
                .filter((name) => !covered.has(name))
            const wrongCount = Number(read.changed) > 0
            if (known.length === 0 || uncovered.length > 0 || wrongCount) {
                problems.push(
                    `${sha.slice(0, 9)}  ${subject}  -- the record is not a pass: new=${read.new} changed=${read.changed}` +
                        (uncovered.length > 0 ? ` (${uncovered.slice(0, 3).join(', ')}${uncovered.length > 3 ? ', ...' : ''} not known at the snapshot)` : ''),
                )
                continue
            }
            for (const entry of known) {
                // A case that was removed is a different claim from one that was fixed, and the
                // record has to be able to say which without a reader guessing from the fields.
                if (entry.kind === 'removed-red') {
                    if (!entry.removedAt || !SHA.test(String(entry.removedAt)) || !SHA.test(String(entry.snapshot)) || !entry.note) {
                        problems.push(
                            `${sha.slice(0, 9)}  ${subject}  -- the record allows ${entry.case} red at a snapshot and names no commit that removed it, or does not quote it`,
                        )
                        continue
                    }
                    continue
                }
                if (!entry.fixedAt || !SHA.test(String(entry.fixedAt)) || !SHA.test(String(entry.snapshot))) {
                    problems.push(
                        `${sha.slice(0, 9)}  ${subject}  -- the record allows ${entry.case} red at a snapshot and names no commit that fixed it`,
                    )
                    continue
                }
            }
        }
        // The exit code the measured run ended with. `unknown` means the gate printed its readings and
        // could not say what the run exited with (its closing session did not return). That is not a
        // pass, and the whole point of keeping such a record is that a reader can tell it apart from
        // one that is missing -- so it is refused here, in those words, rather than counted as green.
        if (record.exit !== undefined && String(record.exit) !== '0') {
            problems.push(
                `${sha.slice(0, 9)}  ${subject}  -- the record says exit=${record.exit}: the run's exit code was never obtained`,
            )
        }
    }

    if (problems.length > 0) {
        say(`merge-record: ${problems.length} merge commit(s) in this push have no reading that holds (${merges} merge(s) in range)`)
        for (const problem of problems) say(`  ${problem}`)
        say('')
        say('A gate refuses a push and not a commit, so a merge that was refused is still on the branch and the')
        say('next push carries it. These commits never passed: merge them again through the merge tool, or drop')
        say('them from the branch before pushing.')
        process.exit(1)
    }
    say(`merge-record: ${merges} merge commit(s) in this push, every one recorded`)
    process.exit(0)
}

// --- verify-promotion --------------------------------------------------------
//
// `verify` above answers a per-merge question: did the tree this merge lands on read the same as the
// merged tree, case by case. A push to `develop` publishes a *tip*, and the reading that covers a tip
// is a different one: the whole layer measured on that tip, the comparison against the previous
// promotion case by case, and what the timeouts of that run said once they were run alone. Neither
// substitutes for the other -- one pass record per merge says nothing about a tip, and a layer's
// counts say nothing about one merge -- so this is a second check with its own refusal, not a
// stricter `verify`.
//
// The record is a JSON document committed beside the promotion reports, whose name says which
// promotion it is, and whose fields are the four things the reading has to be able to answer: which
// tip, the layer's cells, what it was compared with, and what its timeouts did when they were run
// alone.
//
// It is read from this checkout's `HEAD` and not from the tree of the commit being pushed, and the
// reason is not convenience: a commit cannot contain a file that names its own sha, so a record that
// says `tip: <the commit it is in>` cannot exist. The record is therefore always written above the
// tip it names -- one merge, or one push, after that tip was measured -- and the tree that carries it
// is the tree the push leaves from. The guard that goes with that is `merge-base --is-ancestor`: the
// tip being published has to be on this checkout's history, or the record would be about a tree this
// tree does not have.
function promotionRecords(root, ref) {
    const listed = git(root, ['ls-tree', '-r', '--name-only', ref, '--', 'internal/reports'], true)
    if (listed === null) return { problem: `its tree could not be listed (${ref})` }
    const records = []
    for (const file of listed.split('\n').map((line) => line.trim()).filter(Boolean)) {
        if (!file.endsWith('.record.json')) continue
        let text
        try {
            text = execFileSync('git', ['-C', root, 'show', `${ref}:${file}`], { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 })
        } catch (error) {
            records.push({ file, problem: `it could not be read from ${ref.slice(0, 9)}: ${error.message}` })
            continue
        }
        let doc
        try {
            doc = JSON.parse(text)
        } catch (error) {
            records.push({ file, problem: `it does not parse at ${ref.slice(0, 9)}: ${error.message}` })
            continue
        }
        if (doc?.kind !== 'promotion') continue
        records.push({ file, doc, from: ref })
    }
    return { records }
}

function count(value) {
    return typeof value === 'number' && Number.isInteger(value) && value >= 0 ? value : null
}

// One record's own four answers. Every refusal names the field and what a pass needs it to say, so
// the repair is in the record and not in this file.
function promotionProblems(doc, tip) {
    const problems = []
    const which = String(doc.promotion ?? '?')
    if (String(doc.tip || '') !== tip) {
        problems.push(`promotion ${which}: it records tip ${String(doc.tip || 'nothing').slice(0, 9)}, and this push publishes ${tip.slice(0, 9)}`)
    }
    const read = doc.readings || {}
    const cells = ['total', 'pass', 'fail', 'skip'].map((name) => count(read[name]))
    if (cells.some((one) => one === null)) {
        problems.push(`promotion ${which}: readings.total/pass/fail/skip have to be four counts; it has ${JSON.stringify(read)}`)
    } else if (cells[0] === 0) {
        problems.push(`promotion ${which}: readings.total is 0, so the layer it claims to have measured is empty`)
    } else if (cells[1] + cells[2] + cells[3] > cells[0]) {
        problems.push(`promotion ${which}: ${cells[1]} pass + ${cells[2]} fail + ${cells[3]} skip is more than ${cells[0]} cases`)
    }
    for (const field of ['tier', 'scope']) {
        if (!String(doc[field] || '').trim()) {
            problems.push(`promotion ${which}: it does not say ${field === 'tier' ? 'which layer' : 'what was measured'} (${field})`)
        }
    }
    const cmp = doc.comparison || {}
    if (!String(cmp.against || '').trim()) {
        problems.push(`promotion ${which}: comparison.against names no earlier promotion, so nothing was compared case by case`)
    }
    if (count(cmp.new_reds) === null || count(cmp.vanished_reds) === null) {
        problems.push(`promotion ${which}: comparison.new_reds and comparison.vanished_reds have to be counts`)
    } else if (count(cmp.new_reds) > 0) {
        problems.push(`promotion ${which}: comparison.new_reds is ${cmp.new_reds}, and a tip with new reds is not a basis to publish`)
    }
    // A timeout is not a red until it was run alone and failed again: that is the one thing this
    // field has to show, and the two lists are what makes "every timeout accounted for" checkable
    // instead of a sentence.
    const rerun = doc.timeouts_rerun || {}
    const timedOut = count(rerun.count)
    const green = Array.isArray(rerun.green) ? rerun.green : null
    const stillRed = Array.isArray(rerun.still_red_not_timeout) ? rerun.still_red_not_timeout : null
    if (timedOut === null || green === null || stillRed === null) {
        problems.push(
            `promotion ${which}: timeouts_rerun needs count, green[] and still_red_not_timeout[] -- a timeout counts as red only ` +
                'after it was run alone and failed again',
        )
    } else if (green.length + stillRed.length !== timedOut) {
        problems.push(
            `promotion ${which}: timeouts_rerun.count is ${timedOut}, but ${green.length} green and ${stillRed.length} still red ` +
                `account for ${green.length + stillRed.length}: every timeout has to be run alone and land in one of the two`,
        )
    } else if (timedOut > 0 && rerun.run_alone !== true) {
        problems.push(`promotion ${which}: timeouts_rerun.run_alone is not true, so its ${timedOut} timeout(s) were never re-run alone`)
    }
    return problems
}

function doVerifyPromotion() {
    const [localSha] = process.argv.slice(3)
    if (!localSha || !SHA.test(localSha)) {
        refuse(2, `verify-promotion needs <local-sha> as a full commit sha; it got '${localSha ?? ''}'`)
    }
    const root = process.cwd()

    // The pushed tip has to be on this checkout's history: a record read here is about a tree that
    // contains this one, and a push of a commit from somewhere else is not a tip this tree can have
    // measured. This is exit 2 and not 1 -- a checkout that cannot see the commit has answered
    // nothing about it.
    if (git(root, ['merge-base', '--is-ancestor', localSha, 'HEAD'], true) === null) {
        refuse(
            2,
            `the commit being pushed as develop (${localSha.slice(0, 9)}) is not on this checkout's history, so a ` +
                `promotion record read here cannot be about it; push from the checkout that carries the tip being published`,
        )
    }

    // One place: this checkout's tree. The record cannot live inside the commit it names (a commit
    // cannot contain its own sha), so it is written above that tip and travels with the tree the push
    // leaves from.
    const found = []
    const unreadable = []
    const listed = promotionRecords(root, 'HEAD')
    if (listed.problem) unreadable.push(`HEAD: ${listed.problem}`)
    for (const one of listed.records || []) {
        if (one.problem) unreadable.push(`${one.file}: ${one.problem}`)
        else found.push(one)
    }

    const mine = found.filter((one) => String(one.doc.tip || '') === localSha)
    if (mine.length === 0) {
        say(`merge-record: the tip ${localSha.slice(0, 9)} has no promotion record -- 缺的是晋级记录，不是合并记录`)
        say('  A develop push publishes a tip, and the reading that covers a tip is the promotion record: the')
        say('  whole layer measured on it, the comparison with the previous promotion case by case, and what its')
        say('  timeouts said once they were run alone. A pass record per merge (in .git/camel-passed) answers')
        say('  the per-merge question and does not stand in for this one.')
        const named = found.map((one) => `${one.file} (tip ${String(one.doc.tip || 'nothing').slice(0, 9)})`)
        say(`  promotion record(s) the tree being pushed from has: ${named.length ? named.join(', ') : 'none'}`)
        for (const one of unreadable) say(`  also unreadable: ${one}`)
        say('  what repairs it: write internal/reports/<date>-promotion-<n>.record.json with kind "promotion",')
        say("  tip = this sha, the layer's four counts and tier/scope, comparison.against, and timeouts_rerun,")
        say('  and commit it on the tree this push leaves from.')
        // A checkout that could read nothing has not answered the question; one that read records and
        // found none naming this tip has answered it.
        process.exit(found.length === 0 && unreadable.length > 0 ? 2 : 1)
    }
    if (mine.length > 1) {
        const tips = mine.map((one) => one.file).join(', ')
        refuse(2, `two promotion records claim the tip ${localSha.slice(0, 9)} (${tips}); one tip has one reading`)
    }

    const record = mine[0]
    const problems = promotionProblems(record.doc, localSha)
    if (problems.length > 0) {
        say(`merge-record: the promotion record for ${localSha.slice(0, 9)} (${record.file}) does not hold`)
        for (const problem of problems) say(`  ${problem}`)
        say('')
        say('  A develop push is checked against a promotion record, so a record that cannot say which tip,')
        say('  which layer, what it was compared with, or what its timeouts did is not one.')
        process.exit(1)
    }
    const read = record.doc.readings
    say(
        `merge-record: the tip ${localSha.slice(0, 9)} has a promotion record (${record.file}): ` +
            `promotion ${record.doc.promotion ?? '?'}, ${read.total} case(s) ${read.pass} pass / ${read.fail} fail / ${read.skip} skip, ` +
            `compared with ${String(record.doc.comparison.against)}, ${record.doc.timeouts_rerun.count} timeout(s) re-run`,
    )
    process.exit(0)
}

const command = process.argv[2]
if (command === 'write') doWrite()
else if (command === 'verify') doVerify()
else if (command === 'verify-promotion') doVerifyPromotion()
else {
    say('usage: merge-record.mjs write --diff <diff.json> --merge <sha> --first-parent <sha> --second-parent <sha> --branch <name> --tree <path> --out <dir>')
    say('       merge-record.mjs verify <remote-sha> <local-sha>')
    say('       merge-record.mjs verify-promotion <local-sha>')
    process.exit(2)
}