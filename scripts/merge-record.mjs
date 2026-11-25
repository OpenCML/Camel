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
            if (!entry.case || !SHA.test(String(entry.snapshot || '')) || !SHA.test(String(entry.fixedAt || ''))) {
                refuse(2, `--known-at-snapshot entry ${JSON.stringify(entry)} needs case, snapshot and fixedAt`)
            }
            if (entry.snapshot !== merge) {
                refuse(2, `--known-at-snapshot entry ${entry.case} is red at ${entry.snapshot.slice(0, 9)}, and this record is about ${merge.slice(0, 9)}`)
            }
        }
        const names = new Set(entries.map((entry) => entry.case))
        // A comparison names a case as `plan/test`; the `new` set carries that name beside the
        // reading that moved, so the comparison is on the name and not on the object that holds it.
        const regressedNames = regressed.map((row) => (typeof row === 'string' ? row : row?.name)).filter(Boolean)
        const extra = regressedNames.filter((name) => !names.has(name))
        if (extra.length > 0) {
            refuse(2, `--known-at-snapshot does not cover ${extra.join(', ')}: the comparison calls it new and the list does not name it`)
        }
        const missing = entries.filter((entry) => !regressedNames.includes(entry.case))
        if (missing.length > 0) {
            refuse(2, `--known-at-snapshot names ${missing.map((entry) => entry.case).join(', ')}, which the comparison does not call new: a stale allowance is refused rather than carried`)
        }
        knownAtSnapshot = { tip: list.tip, evidence: list.run ? String(list.run) : '', entries }
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

const command = process.argv[2]
if (command === 'write') doWrite()
else if (command === 'verify') doVerify()
else {
    say('usage: merge-record.mjs write --diff <diff.json> --merge <sha> --first-parent <sha> --second-parent <sha> --branch <name> --tree <path> --out <dir>')
    say('       merge-record.mjs verify <remote-sha> <local-sha>')
    process.exit(2)
}