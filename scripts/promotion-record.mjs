#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The check a push to `develop` is held to: the tip being published has a promotion record.
 *
 * A push to `develop` publishes a *tip*, and the reading that covers a tip is the whole layer
 * measured on it, the comparison with the previous promotion case by case, and what the timeouts of
 * that run said once they were run alone. The record is a JSON document committed beside the
 * promotion reports, whose name says which promotion it is, and whose fields are the four things the
 * reading has to be able to answer: which tip, the layer's cells, what it was compared with, and
 * what its timeouts did when they were run alone.
 *
 * It is read from this checkout's `HEAD` and not from the tree of the commit being pushed, and the
 * reason is not convenience: a commit cannot contain a file that names its own sha, so a record that
 * says `tip: <the commit it is in>` cannot exist. The record is therefore always written above the
 * tip it names -- one merge, or one push, after that tip was measured -- and the tree that carries it
 * is the tree the push leaves from. The guard that goes with that is `merge-base --is-ancestor`: the
 * tip being published has to be on this checkout's history, or the record would be about a tree this
 * tree does not have.
 *
 * Usage:
 *   node scripts/promotion-record.mjs <local-sha>
 *
 * Exit codes: 0 the tip has a record that holds, 1 it has none or the record does not hold, 2 it
 * could not be decided (the commit is not on this history, the records cannot be read), which must
 * never read as "this tip is wrong".
 */

import { execFileSync } from 'node:child_process'

const SHA = /^[0-9a-f]{40}$/

function say(line) {
    console.log(line)
}

function refuse(code, why) {
    say(`promotion-record: ${why}`)
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
    const [localSha] = process.argv.slice(2)
    if (!localSha || !SHA.test(localSha)) {
        refuse(2, `a <local-sha> is needed as a full commit sha; it got '${localSha ?? ''}'`)
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
        say(`promotion-record: the tip ${localSha.slice(0, 9)} has no promotion record -- 缺晋级记录`)
        say('  A develop push publishes a tip, and the reading that covers a tip is the promotion record: the')
        say('  whole layer measured on it, the comparison with the previous promotion case by case, and what its')
        say('  timeouts said once they were run alone.')
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
        say(`promotion-record: the promotion record for ${localSha.slice(0, 9)} (${record.file}) does not hold`)
        for (const problem of problems) say(`  ${problem}`)
        say('')
        say('  A develop push is checked against a promotion record, so a record that cannot say which tip,')
        say('  which layer, what it was compared with, or what its timeouts did is not one.')
        process.exit(1)
    }
    const read = record.doc.readings
    say(
        `promotion-record: the tip ${localSha.slice(0, 9)} has a promotion record (${record.file}): ` +
            `promotion ${record.doc.promotion ?? '?'}, ${read.total} case(s) ${read.pass} pass / ${read.fail} fail / ${read.skip} skip, ` +
            `compared with ${String(record.doc.comparison.against)}, ${record.doc.timeouts_rerun.count} timeout(s) re-run`,
    )
    process.exit(0)
}

doVerifyPromotion()
