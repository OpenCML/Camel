// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The coordinator's children counted by slot: the binding table's pure parts in
// internal/tools/camel-oversight/slots.js and the `lanes-below` rule that judges the development slots.

import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))
const plugin = path.resolve(here, '../../internal/tools/camel-oversight')
const { SLOTS, applyEntry, classify } = await import(pathToFileURL(path.join(plugin, 'slots.js')).href)
const { RULES, DEFAULT_LIMITS } = await import(pathToFileURL(path.join(plugin, 'rules.js')).href)

let checks = 0
let failed = 0
const check = (name, ok, detail = '') => {
    checks += 1
    if (!ok) failed += 1
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${ok || !detail ? '' : `: ${detail}`}`)
}

const child = (id, activity = 'running') => ({ id, name: `n-${id}`, activity })
const bind = (table, agent, slot, lane) => applyEntry(table, { kind: 'bind', at: 1, agent, slot, lane })

// ---- classifying children by the table ------------------------------------------------------
{
    const table = new Map()
    bind(table, 'a1', 'dev', 'lane/x')
    bind(table, 'n-a2', 'merge') // bound by name
    bind(table, 'a3', 'misc', 'lane/chores')
    const members = [child('a1'), child('a2'), child('a3', 'idle'), child('a4'), child('a5', 'idle')]
    const out = classify(members, table)
    const slotOf = (id) => out.members.find((m) => m.id === id).slot
    check('a child bound by id lands in its slot', slotOf('a1') === 'dev')
    check('a child bound by name lands in its slot', slotOf('a2') === 'merge')
    check('a child with no row is buffer', slotOf('a4') === 'buffer' && slotOf('a5') === 'buffer')
    check('the lane is carried onto the child', out.members.find((m) => m.id === 'a1').lane === 'lane/x')
    check('running and total are counted per slot', JSON.stringify(out.slots) === JSON.stringify({
        merge: { running: 1, total: 1 },
        dev: { running: 1, total: 1 },
        misc: { running: 0, total: 1 },
        buffer: { running: 1, total: 2 },
    }), JSON.stringify(out.slots))
    check('every slot is reported', SLOTS.every((slot) => slot in out.slots))
}

// ---- rebind, unbind, stale ------------------------------------------------------------------
{
    const table = new Map()
    bind(table, 'a1', 'dev', 'lane/x')
    bind(table, 'a1', 'merge')
    check('binding again replaces the row', table.size === 1 && table.get('a1').slot === 'merge' && table.get('a1').lane === null)
    bind(table, 'gone', 'dev', 'lane/old')
    const out = classify([child('a1')], table)
    check('a row whose child is gone is stale, not dropped', out.stale.length === 1 && out.stale[0].agent === 'gone')
    check('a stale row counts for no slot', out.slots.dev.total === 0)
    applyEntry(table, { kind: 'unbind', agent: 'gone' })
    check('unbind removes the row', !table.has('gone') && classify([child('a1')], table).stale.length === 0)
    // The table is rebuilt from the log in order.
    const rebuilt = new Map()
    for (const entry of [
        { kind: 'bind', agent: 'q', slot: 'dev', lane: 'lane/q', at: 1 },
        { kind: 'bind', agent: 'q', slot: 'buffer', at: 2 },
        { kind: 'bind', agent: 'r', slot: 'dev', lane: 'lane/r', at: 3 },
        { kind: 'unbind', agent: 'r', at: 4 },
    ]) applyEntry(rebuilt, entry)
    check('replaying the log gives the last state', rebuilt.size === 1 && rebuilt.get('q').slot === 'buffer')
}

// ---- the lanes-below rule -------------------------------------------------------------------
const rule = RULES.find((r) => r.id === 'lanes-below')
const verdict = (devRunning, bufferRunning) => {
    const table = new Map()
    const members = []
    for (let i = 0; i < devRunning; i += 1) {
        members.push(child(`d${i}`))
        bind(table, `d${i}`, 'dev', `lane/d${i}`)
    }
    for (let i = 0; i < bufferRunning; i += 1) members.push(child(`b${i}`))
    const placed = classify(members, table)
    const lanes = { running: members.length, total: members.length, slots: placed.slots, lanes: placed.members }
    return rule.check({ m: {}, lanes, limits: DEFAULT_LIMITS })
}
check('the limit is eight development slots and the old names are gone', DEFAULT_LIMITS.devSlots === 8 && !('minLanes' in DEFAULT_LIMITS) && !('maxLanes' in DEFAULT_LIMITS))
check('7 dev slots running with buffer children: fires', verdict(7, 4) !== null)
check('8 dev slots running: does not fire', verdict(8, 0) === null)
check('8 dev slots running with buffer children: does not fire', verdict(8, 3) === null)
check('buffer children do not make up for empty dev slots', verdict(3, 9) !== null)
{
    const v = verdict(7, 4)
    check('the text names the dev count and that the buffer is not counted', v && v.text.includes('7/8') && v.text.includes('缓冲') && v.text.includes('不计入'), v && v.text)
    check('the text names the empty slots', v && v.text.includes('空槽 1 个'), v && v.text)
}
{
    const table = new Map()
    bind(table, 'd0', 'dev', 'lane/a')
    bind(table, 'd1', 'dev', 'lane/b')
    const placed = classify([child('d0'), child('d1', 'idle')], table)
    const v = rule.check({ m: {}, lanes: { running: 1, total: 2, slots: placed.slots, lanes: placed.members }, limits: DEFAULT_LIMITS })
    check('the text names idle dev lanes and the empty count', v && v.text.includes('lane/b') && v.text.includes('空槽 6 个'), v && v.text)
}

console.log(`oversight-binding: ${checks} checks, ${failed} failed`)
process.exit(failed === 0 && checks > 0 ? 0 : 1)
