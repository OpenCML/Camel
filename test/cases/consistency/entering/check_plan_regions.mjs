#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A control region's members are the plan's range for it, and the plan of `positions.cml` is 552
 * nodes -- both as numbers, on the plan pass, so that narrowing either one is red.
 *
 * The S1 lane moved the executor's liveness onto the regions: the seeds are the results of the
 * regions, and an arm is laid out as the members of its region. That is a mechanism, and a mechanism
 * that only lives in prose is a claim: what is read here is what the pass prints. Two facts:
 *
 *   1. every `REGION` line the pass prints says `in-plan == members` -- the members of a control
 *      region are the nodes the plan lays out for it. Significance: the range of an arm is the
 *      members of its region (docs/design/effects/edges.cn.md 5.2). A change that computed the
 *      range from liveness instead would drop a member that nothing reached and print a smaller
 *      `in-plan`, and this line would say so.
 *   2. the plan has 552 nodes. A change that narrows liveness or a range takes nodes out of the
 *      plan; the count is the cheapest way to see it, and it is written here as an exact number
 *      rather than a shape, because "no fewer than" would not see a plan that lost the members.
 *
 * What this check does NOT hold: that the seeds carry the weight. They do not, while the control
 * edges are still built -- the exit walk reaches every region result through the owner's ports as
 * well, so removing the seeding branch leaves both readings identical (measured: 552 and `in-plan ==
 * members` either way).
 *
 * Nor does it hold that the range is membership and not liveness, on this program: replacing the
 * membership judgement with `live[n]` also leaves the plan byte-identical (measured: `diff` of the
 * two plans is empty, 675 lines either way), because every member this program has is reached while
 * the control edges stand. The check pins today's numbers -- 552 nodes, `in-plan == members` -- and
 * a change that took a member out of the plan would still show here; what it cannot see is the
 * difference between "a member because its region says so" and "a member because something reached
 * it", which becomes observable only once the edge from a branch to its arm is gone. The seed being
 * load-bearing has to be read by a case of that shape, and there is none today.
 *
 * Usage: node check_plan_regions.mjs
 */

import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const CAMEL = path.join(
    REPO_ROOT,
    'out',
    'latest',
    'bin',
    process.platform === 'win32' ? 'camel.exe' : 'camel'
)
const env = {
    ...process.env,
    CAMEL_HOME: process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
}
const PROGRAM = path.join(REPO_ROOT, 'test', 'cases', 'consistency', 'entering', 'positions.cml')

/** The number of nodes the plan lays out for this program, as a value: a plan that lost a member
 *  prints fewer, and the message names both numbers rather than "the plan changed". */
const PLANNED_NODES = 552

const run = spawnSync(CAMEL, [PROGRAM, 'std::opt', 'test::exec_plan'], {
    env,
    encoding: 'utf8',
    timeout: 120000
})
if (run.status !== 0) {
    console.error(`[REGIONS] RED the plan pass exited ${run.status}: ${run.stderr.trim()}`)
    process.exit(1)
}
const lines = run.stdout.split(/\r?\n/)

const region = /^REGION (\S+) (\d+) kind (\d+) owner (\S+) result (\S+) members (\d+) in-plan (\d+)$/
let regions = 0
let members = 0
let planned = 0
const bad = []
const seen = new Set()
for (const line of lines) {
    if (line.startsWith('PLAN ')) {
        ++planned
        continue
    }
    const m = region.exec(line)
    if (!m) {
        continue
    }
    ++regions
    const [graph, id, kind, owner, result, count, inPlan] = [
        m[1],
        m[2],
        Number(m[3]),
        m[4],
        m[5],
        Number(m[6]),
        Number(m[7])
    ]
    seen.add(`${graph}#${id}`)
    members += count
    if (count !== inPlan) {
        bad.push(
            `[REGIONS] RED ${graph} region ${id} (kind ${kind}, owner ${owner}, result ${result}) ` +
                `has ${count} members and ${inPlan} of them in the plan`
        )
    }
}

// A check that passes because it read nothing is the defect it is meant to catch, so the reading has
// to be there: control regions, members inside them, and a plan with nodes in it.
if (regions === 0) {
    bad.push('[REGIONS] RED the pass printed no REGION line: the plan is not laid out by regions')
}
if (members === 0) {
    bad.push('[REGIONS] RED no control region has a member: the ranges are empty')
}
if (planned !== PLANNED_NODES) {
    bad.push(`[REGIONS] RED the plan lays out ${planned} nodes, not ${PLANNED_NODES}`)
}

for (const line of bad) {
    console.error(line)
}
console.log(
    `[REGIONS] graphs ${new Set([...seen].map((k) => k.split('#')[0])).size}, control regions ` +
        `${regions}, members ${members}, planned ${planned}`
)
if (bad.length > 0) {
    console.error(`[REGIONS] RED ${bad.length} reading(s) above`)
    process.exit(1)
}
console.log('[REGIONS] ok every control region is in the plan as its members')