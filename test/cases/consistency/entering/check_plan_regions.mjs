#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A control region's members are the plan's range for it, and the plan lays out the graph it was
 * handed -- both numbers are read from one run, so what is held is a relation and not a count.
 *
 * The S1 lane moved the executor's liveness onto the regions: the seeds are the results of the
 * regions, and an arm is laid out as the members of its region. That is a mechanism, and a mechanism
 * that only lives in prose is a claim: what is read here is what the passes print. Three facts:
 *
 *   1. every `REGION` line the pass prints says `in-plan == members` -- the members of a control
 *      region are the nodes the plan lays out for it. Significance: the range of an arm is the
 *      members of its region (docs/design/effects/edges.cn.md 5.2). A change that computed the
 *      range from liveness instead would drop a member that nothing reached and print a smaller
 *      `in-plan`, and this line would say so.
 *   2. the plan lays out the graph. `std::stats` prints `GRAPH_STATS` for the graph `test::exec_plan`
 *      is handed, and the `PLAN` lines must count the graph's nodes less the two the root graph holds
 *      and no plan lays out -- a gap of that structural two, so the check is a difference and not a
 *      share of the graph. A count is still the cheapest way to see a plan that lost members; it is
 *      taken against the graph's own size, because how many nodes the compiler builds for a program
 *      is the compiler's business and not the executor's.
 *   3. the reading is not empty: the pass prints control regions, some region has a member, and the
 *      plan has nodes in it. A check that passes because it read nothing is the defect it is meant
 *      to catch.
 *
 * Why the number is not written down here any more. How many nodes the compiler builds for this
 * program is the compiler's business, and it moves with the graph construction: a pinned count then
 * reads as a plan that lost its members, while the plan in fact lays out the whole graph it is
 * handed. What is worth holding is that relation -- the plan lays out the graph's nodes, every
 * control region as its members, and a plan that leaves out more than the root graph's two nodes is
 * a plan that lost members. The program's own output is a separate fact and is held elsewhere, by
 * the golden of the behaviour cases on every back end; the executor is not asked to reproduce it.
 *
 * What this check does NOT hold: that the seeds carry the weight. They do not, while the control
 * edges are still built -- the exit walk reaches every region result through the owner's ports as
 * well, so removing the seeding branch leaves both readings identical (measured on a tree where the
 * chain still carried every call: 552 and `in-plan == members` either way).
 *
 * Nor does it hold that the range is membership and not liveness, on this program: replacing the
 * membership judgement with `live[n]` also leaves the plan byte-identical (measured: `diff` of the
 * two plans is empty, 675 lines either way), because every member this program has is reached while
 * the control edges stand. The check pins `in-plan == members` and "the plan is the graph", and a
 * change that took a member out of the plan would still show here; what it cannot see is the
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

const run = spawnSync(CAMEL, [PROGRAM, 'std::opt', 'std::stats', 'test::exec_plan'], {
    env,
    encoding: 'utf8',
    timeout: 120000
})
if (run.status !== 0) {
    console.error(`[REGIONS] RED the plan pass exited ${run.status}: ${run.stderr.trim()}`)
    process.exit(1)
}
const lines = run.stdout.split(/\r?\n/)

// The graph's own size, from the same run the plan comes from: `std::stats` passes the graph on
// unchanged, so the PLAN lines and this number are two readings of one graph.
const statsLine = lines.find((line) => line.startsWith('GRAPH_STATS '))
let nodes = -1
if (statsLine) {
    try {
        nodes = JSON.parse(statsLine.slice('GRAPH_STATS '.length)).nodes
    } catch {
        nodes = -1
    }
}

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
// to be there: a graph with nodes in it, control regions, members inside them, and a plan with nodes.
if (nodes <= 0) {
    bad.push('[REGIONS] RED no GRAPH_STATS node count: there is no graph for the plan to lay out')
}
if (regions === 0) {
    bad.push('[REGIONS] RED the pass printed no REGION line: the plan is not laid out by regions')
}
if (members === 0) {
    bad.push('[REGIONS] RED no control region has a member: the ranges are empty')
}
// The plan is the graph's nodes less the two the root graph holds and no plan lays out: 546 of 548
// before the graph shrank, 210 of 212 after. The gap is that structural two and not a share of the
// graph, so anything past it is a plan that leaves nodes out.
if (planned > nodes) {
    bad.push(
        `[REGIONS] RED the plan lays out ${planned} nodes and the graph it was handed has ` +
            `${nodes}: a plan cannot hold more nodes than the graph has`
    )
} else if (nodes - planned > 2) {
    bad.push(
        `[REGIONS] RED the plan lays out ${planned} of the graph's ${nodes} nodes: more than the ` +
            `two the root graph holds are left out, so the plan lost members`
    )
}

for (const line of bad) {
    console.error(line)
}
console.log(
    `[REGIONS] graphs ${new Set([...seen].map((k) => k.split('#')[0])).size}, control regions ` +
        `${regions}, members ${members}, planned ${planned}, graph nodes ${nodes}`
)
if (bad.length > 0) {
    console.error(`[REGIONS] RED ${bad.length} reading(s) above`)
    process.exit(1)
}
console.log('[REGIONS] ok every control region is in the plan as its members, and the plan is the graph')