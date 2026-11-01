#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * `:op/keep` is reachable today, and this is the number that says so.
 *
 * The operator was added for a pass that takes the last read of an effect it has no chain place
 * for, and the question "is it ever written?" cannot be answered by reading the pass: it is
 * answered by counting the graphs that hold one. `std::opt` over the cases of
 * `test/cases/opt/effects` writes three of them today -- two nodes in each of
 * `branch_keeps_outer_effect.cml`, `dce_effect_under_pure_arithmetic.cml` and
 * `fold_keeps_arm_effect.cml` -- and two witnesses outside that directory hold two more.
 *
 * The count is pinned, with the names, because a change in it is a decision and not a drift: one
 * more case means a rule started writing the operator where it did not before, one fewer means a
 * rule stopped, and either way someone has to look at which case moved and why. A count with no
 * names would go red without saying where.
 *
 * Usage: node check.mjs
 */

import { spawnSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import url from 'url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..', '..')
const CASES = path.join(REPO_ROOT, 'test', 'cases', 'opt')
const CAMEL = path.join(
    REPO_ROOT,
    'out',
    'latest',
    'bin',
    process.platform === 'win32' ? 'camel.exe' : 'camel'
)
const env = { ...process.env, CAMEL_HOME: process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest') }

/// What the sweep found on 2026-11-01, and what the plan pins.
const EFFECTS_WITH_KEEP = {
    'branch_keeps_outer_effect.cml': 2,
    'dce_effect_under_pure_arithmetic.cml': 2,
    'fold_keeps_arm_effect.cml': 2
}
const WITNESSES_WITH_KEEP = {
    'generic/control_chain.cml': 2,
    'generic/unread_gate.cml': 2
}

const nodeCount = (program) => {
    const run = spawnSync(CAMEL, [program, 'std::opt', 'std::jgir'], { env, encoding: 'utf8' })
    if (run.status !== 0) {
        throw new Error(`camel ${program} std::opt std::jgir exited ${run.status}: ${run.stderr}`)
    }
    return (run.stdout.match(/"label": ":op\/keep"/g) || []).length
}

const sweepDirectory = (relative) => {
    const found = {}
    for (const name of fs.readdirSync(path.join(CASES, relative)).sort()) {
        if (!name.endsWith('.cml')) {
            continue
        }
        const nodes = nodeCount(path.join(CASES, relative, name))
        if (nodes > 0) {
            found[name] = nodes
        }
    }
    return found
}

const sweepWitnesses = (names) => {
    const found = {}
    for (const name of names) {
        const nodes = nodeCount(path.join(CASES, name))
        if (nodes > 0) {
            found[name] = nodes
        }
    }
    return found
}

/// The two sides of a pin: what the sweep found against what was measured, named either way.
const compare = (label, found, expected) => {
    const lines = []
    const names = (map) => Object.keys(map).sort()
    for (const name of names(found)) {
        lines.push(`[KEEP]   ${label} ${name} nodes=${found[name]}`)
    }
    for (const name of names(expected)) {
        if (!(name in found)) {
            lines.push(`[KEEP] RED ${label} ${name} no longer holds an :op/keep node (a rule stopped)`)
        } else if (found[name] !== expected[name]) {
            lines.push(
                `[KEEP] RED ${label} ${name} holds ${found[name]} :op/keep node(s), pinned ${expected[name]}`
            )
        }
    }
    for (const name of names(found)) {
        if (!(name in expected)) {
            lines.push(
                `[KEEP] RED ${label} ${name} holds an :op/keep node and is not pinned (a rule started)`
            )
        }
    }
    return lines
}

const effects = sweepDirectory('effects')
const witnesses = sweepWitnesses(Object.keys(WITNESSES_WITH_KEEP))
const red = [
    ...compare('effects', effects, EFFECTS_WITH_KEEP),
    ...compare('witness', witnesses, WITNESSES_WITH_KEEP)
].filter((line) => line.startsWith('[KEEP] RED'))
const counted = (map) => Object.values(map).reduce((sum, n) => sum + n, 0)
const lines = [
    `[KEEP] effects cases=${Object.keys(effects).length} nodes=${counted(effects)} of ${fs
        .readdirSync(path.join(CASES, 'effects'))
        .filter((name) => name.endsWith('.cml')).length}`,
    ...compare('effects', effects, EFFECTS_WITH_KEEP),
    ...compare('witness', witnesses, WITNESSES_WITH_KEEP),
    red.length === 0 ? '[KEEP] ok' : `[KEEP] RED ${red.length} pinned number(s) moved`
]
process.stdout.write(lines.join('\n') + '\n')
process.exit(red.length === 0 ? 0 : 1)