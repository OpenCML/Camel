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
    if (!diffFile || !outDir) refuse(2, 'write needs --diff <diff.json> and --out <dir>')
    for (const [name, value] of [['--merge', merge], ['--first-parent', firstParent], ['--second-parent', secondParent]]) {
        if (!value || !SHA.test(value)) refuse(2, `write needs ${name} <full commit sha>; it was '${value ?? ''}'`)
    }

    if (!fs.existsSync(diffFile)) {
        refuse(2, `no comparison at ${diffFile}: a record of a merge nobody compared would be a claim with no reading`)
    }
    let diff
    try {
        diff = JSON.parse(fs.readFileSync(diffFile, 'utf8'))
    } catch (error) {
        refuse(2, `${diffFile} is not JSON: ${error.message}`)
    }
    const fixed = Array.isArray(diff.fixed) ? diff.fixed : null
    const regressed = Array.isArray(diff.new) ? diff.new : null
    const changed = Array.isArray(diff.changed) ? diff.changed : null
    if (!fixed || !regressed || !changed) refuse(2, `${diffFile} does not hold the three sets (fixed, new, changed)`)
    if (!diff.planSet || !Array.isArray(diff.planSet.names) || !diff.planSet.hash) {
        refuse(2, `${diffFile} does not name the plan set: a record that cannot say which plans were read is not checkable`)
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
    // to this write -- and the one thing this file must never do is certify it.
    if (regressed.length > 0 || changed.length > 0) {
        refuse(1, `refusing to record a pass for ${merge.slice(0, 9)}: ${regressed.length} newly red and ${changed.length} changed, and a pass record with those in it would be read as a pass`)
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
        readings: { cases: diff.baseline?.cases ?? null, fixed: fixed.length, new: regressed.length, changed: changed.length },
        cases: { fixed, new: regressed, changed },
        gates,
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
            `fixed=${fixed.length} new=${regressed.length} changed=${changed.length}`,
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
        const read = record.readings || {}
        if (Number(read.new) > 0 || Number(read.changed) > 0) {
            problems.push(
                `${sha.slice(0, 9)}  ${subject}  -- the record is not a pass: new=${read.new} changed=${read.changed}`,
            )
        }
    }

    if (problems.length > 0) {
        say(`merge-record: ${problems.length} merge commit(s) in this push have no reading (${merges} merge(s) in range)`)
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