#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// `test/plans/<dir>/D` and `test/plans/<dir>/D.plan.toml` sit beside each other when a plan's
// fixtures live in a directory of the plan's own name. The short name still works -- the runner
// tries `${base}.plan.toml` before the directory -- but the full path `test/plans/<dir>/D` resolves
// to the directory, which holds no plan file at any depth, so the run selects nothing. On
// 2026-11-03 `-Plan test/plans/feat/docs_links` was reported as "0 plans" for exactly this reason.
//
// The runner now refuses a target that reaches no plan file, so the hole is loud when it is hit.
// This check keeps the pairing documented before anyone hits it: a directory that has a plan file
// beside it must say in its own README that this is the fixtures and the plan is next door.
//
//   node scripts/plan-dir-markers.mjs --check    report the pairs whose directory lacks the marker
//
// The population is the pairs, not every directory: a golden or fixture directory with no plan of
// its own name (`feat/compile/golden`, `feat/format/verify`) is not a name anyone types as a target
// and is deliberately out of scope. A check that matched nothing is refused (exit 2): this rule is
// small enough that an empty population means it looked in the wrong place, not that the tree is
// clean.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const PLANS = path.join(ROOT, 'test', 'plans')

// The sentence a marker README has to contain. One phrase rather than a shape: a README that says
// something else about the directory is not the statement this check is about.
const MARKER = 'This directory holds no plan file'

function directoriesUnder(dir, out = []) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        if (!entry.isDirectory()) continue
        const full = path.join(dir, entry.name)
        out.push(full)
        directoriesUnder(full, out)
    }
    return out
}

const shown = (target) => path.relative(ROOT, target).split(path.sep).join('/')

const pairs = directoriesUnder(PLANS).filter((dir) => fs.existsSync(`${dir}.plan.toml`))
if (pairs.length === 0) {
    console.error(
        `plan-dir-markers: no directory under ${shown(PLANS)} has a plan file beside it, so this ` +
            'check matched nothing and proves nothing',
    )
    process.exit(2)
}

// A pair is only a trap when the directory holds no plan of its own: `modules/tensor/` sits beside
// `tensor.plan.toml` and also holds plans, so both names reach something and there is nothing to
// warn about. The trap is the directory that holds the fixtures and nothing else.
const holdsPlans = (dir) => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        if (entry.isDirectory()) {
            if (holdsPlans(path.join(dir, entry.name))) return true
        } else if (entry.name.endsWith('.plan.toml')) {
            return true
        }
    }
    return false
}

const traps = pairs.filter((dir) => !holdsPlans(dir))
if (traps.length === 0) {
    console.error(
        'plan-dir-markers: every directory with a plan file beside it also holds plans of its ' +
            'own, so this check matched no trap and proves nothing',
    )
    process.exit(2)
}

const missing = []
for (const dir of traps) {
    const readme = path.join(dir, 'README.md')
    const text = fs.existsSync(readme) ? fs.readFileSync(readme, 'utf8') : ''
    if (!text.includes(MARKER)) missing.push({ dir, readme, hasReadme: fs.existsSync(readme) })
}

for (const { dir, readme, hasReadme } of missing) {
    const why = hasReadme
        ? `its README.md does not say "${MARKER}"`
        : 'it has no README.md'
    console.error(
        `plan-dir-markers: ${shown(dir)}: ${why}, and ${path.basename(dir)}.plan.toml is beside it: ` +
            'the short name reaches the plan, the full path reaches this directory and selects nothing',
    )
}

console.log(
    `plan-dir-markers: ${traps.length} fixtures-only directory/plan pair(s) of ${pairs.length}, ` +
        `${traps.length - missing.length} marked, ${missing.length} unmarked`,
)
process.exit(missing.length === 0 ? 0 : 1)