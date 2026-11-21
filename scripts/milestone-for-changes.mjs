#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Which milestone-layer plans a change touches: the merge lane's missing step, as a lister.
//
// Why this exists: `npm run test` is `--tier fast`, the merge lane runs `smoke` + `feat` at that
// same layer, and the release gate says `not run here: the milestone layer` in so many words. So
// a change to a file whose plan keeps a `tier = "milestone"` entry runs nothing that reads it,
// and a defect there reaches `develop` unseen -- `feat/opt/progen.plan.toml` is the measured
// case: two entries, both milestone, and the `GATE` misread that reached `develop` sat in the
// layer no gate runs.
//
// What it does NOT do is run the layer. It answers one question -- the plan files the change
// selects that contribute at least one milestone entry -- and names the two ways that answer can
// be blind, so a reader gets a reading rather than an empty list:
//
//   * a changed file no rule in `test/plan-selection.json` matches (`--changed` falls back to the
//     whole fast layer, which starts no milestone entry);
//   * a changed plan file whose own entries are milestone but which the selection deliberately
//     drops (the `except` list of the self rule: perf, GC stress, progen, tiles_regions). That is
//     the policy's ruling -- an ordinary change does not start those -- and it is exactly why the
//     merge lane needs this lister rather than `--changed` alone.
//
// The judgement is not restated here: the plan set comes from `plansForChanges`, the layer comes
// from `testTier` and `milestoneLayer`. Two copies of either rule would drift.
//
//   node scripts/milestone-for-changes.mjs                  # git diff --name-only against the base
//   node scripts/milestone-for-changes.mjs --base origin/develop
//   node scripts/milestone-for-changes.mjs src/parse/syntax/parser.cpp test/cases/...
//   node scripts/milestone-for-changes.mjs --plan           # print one `node scripts/test.js <file>` per line
//
// Exit 0 lists at least one milestone plan and found no hole; 1 found a hole (named); 2 could not
// read the change.

import { execFileSync } from 'node:child_process'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

import { plansForChanges, resolvedSelection } from '../test/runner.mjs'
import { loadPlanTests, expandSweeps, testTier } from '../test/tools/run-tests.mjs'
import { milestoneLayer, walkPlans, rel } from './plan-inventory.mjs'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

function parseArgs(argv) {
    const options = { base: process.env.CAMEL_TEST_BASE || 'origin/develop', files: [], plan: false }
    for (let i = 0; i < argv.length; ++i) {
        const arg = argv[i]
        if (arg === '--base' && i + 1 < argv.length) options.base = argv[++i]
        else if (arg.startsWith('--base=')) options.base = arg.slice('--base='.length)
        else if (arg === '--plan') options.plan = true
        else if (arg === '--help' || arg === '-h') options.help = true
        else if (arg.startsWith('-')) throw new Error(`unknown option '${arg}'`)
        else options.files.push(arg)
    }
    return options
}

/** Every plan file under `test/plans`, with its milestone entry count and its whole entry count. */
function inventory() {
    const counts = new Map()
    for (const abs of walkPlans(path.join(REPO_ROOT, 'test/plans')) ?? []) {
        let tests = []
        let doc = {}
        try {
            const loaded = loadPlanTests(abs)
            doc = loaded.doc
            tests = [...loaded.tests, ...expandSweeps(loaded.doc, abs)]
        } catch {
            // A plan the runner cannot read is the inventory check's subject, not this one's.
            continue
        }
        let milestone = 0
        for (const test of tests) {
            try {
                if (testTier(doc, test) === 'milestone') milestone += 1
            } catch {
                // an unknown tier is reported by feat/gate/plan_inventory
            }
        }
        counts.set(rel(abs), { doc, tests, milestone })
    }
    return counts
}

function changedFiles(base) {
    try {
        const out = execFileSync('git', ['-C', REPO_ROOT, 'diff', '--name-only', base], {
            encoding: 'utf8',
            maxBuffer: 64 * 1024 * 1024,
        })
        return out.split(/\r?\n/).filter((line) => line.trim() !== '')
    } catch (error) {
        console.error(`[ms] cannot read the change against '${base}': ${error.message.split('\n')[0]}`)
        return null
    }
}

function main(argv) {
    const options = parseArgs(argv)
    if (options.help) {
        console.log('usage: node scripts/milestone-for-changes.mjs [--base <ref>] [--plan] [file...]')
        return 0
    }
    const files = options.files.length > 0 ? options.files : changedFiles(options.base)
    if (files === null) return 2
    if (files.length === 0) {
        console.log(`[ms] no change against '${options.base}' (or none given): nothing to select`)
        return 0
    }

    const layer = milestoneLayer()
    const inventory_ = inventory()
    const plans = plansForChanges(files)
    const targets = plans ? resolvedSelection(plans) : []
    const selected = new Set()
    for (const target of targets) {
        for (const abs of walkPlans(target) ?? []) selected.add(rel(abs))
    }
    const hits = [...selected].filter((key) => (inventory_.get(key)?.milestone ?? 0) > 0).sort()

    const entries = hits.reduce((sum, key) => sum + inventory_.get(key).milestone, 0)
    console.log(
        `[ms] ${files.length} changed file(s) -> ${selected.size} plan file(s) selected -> ` +
            `${hits.length} with a milestone entry (${entries} of the layer's ${layer.cases} entries)`
    )
    for (const key of hits) {
        const row = inventory_.get(key)
        console.log(`[ms]   ${key}: ${row.milestone} milestone of ${row.tests.length} entries (tier ${row.doc.tier ?? 'unset'})`)
    }
    if (options.plan) {
        for (const key of hits) console.log(`node scripts/test.js ${key}`)
    }
    if (plans && plans.unmatched.length > 0) {
        console.log(`[ms] ${plans.unmatched.length} changed file(s) match no rule: ${plans.unmatched.join(', ')}`)
    }

    // The two ways this list can be empty while the change does touch a milestone entry. Both are
    // named rather than passed over: an empty list that means "nothing to do" and one that means
    // "the rules cannot see it" read the same, and only one of them is a pass.
    const holes = []
    for (const file of files) {
        if (!file.endsWith('.plan.toml')) continue
        const key = file.split('\\').join('/')
        const row = inventory_.get(key)
        if (!row || row.milestone === 0) continue
        if (!selected.has(key)) holes.push(`${key} (${row.milestone} milestone entries, not selected by the rules)`)
    }
    if (!plans && files.length > 0) {
        holes.push(`no rule matches any of the ${files.length} changed file(s): --changed would run the whole fast layer`)
    }
    if (hits.length === 0) {
        console.log('[ms] no milestone plan is selected: this change runs no milestone entry')
    }
    for (const hole of holes) console.log(`[ms] hole: ${hole}`)
    return holes.length > 0 ? 1 : 0
}

process.exit(main(process.argv.slice(2)))