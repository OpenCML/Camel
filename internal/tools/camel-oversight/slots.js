// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The binding table: which slot each child of the coordinator fills, and which lane it works.
//
// The coordinator's children are counted by slot, not all together: merge seats (counted apart),
// development slots (the standing shape; "full" is judged against these), the standing misc lane for
// chores, and a buffer that is not kept full. A child with no row counts as buffer. The table is
// the one place a lane is bound to a child: the coordinator writes it with `oversight_bind` and
// `oversight_unbind`, every call is a log entry, and the table is rebuilt from the log at load.
//
// Pure functions only, so the counting can be tested without the host.

export const SLOTS = ['merge', 'dev', 'misc', 'buffer']

// Apply one log entry (`bind` or `unbind`) to the table, a Map from the agent as the coordinator
// named it (an id or a name) to its row. Binding the same agent again replaces its row.
export function applyEntry(table, entry) {
    if (entry.kind === 'bind') {
        table.set(entry.agent, { agent: entry.agent, slot: entry.slot, lane: entry.lane || null, worktree: entry.worktree || null, at: entry.at })
    } else if (entry.kind === 'unbind') {
        table.delete(entry.agent)
    }
    return table
}

// A row names a child by its id or by its name.
const matches = (row, member) => row.agent === member.id || row.agent === member.name

// Put each child in its slot and report the rows that name no live child. `members` are
// `{ id, name, activity, ... }`; the result's `members` carry `slot`, `lane`, `worktree` as well.
export function classify(members, table) {
    const rows = [...table.values()]
    const placed = members.map((member) => {
        const row = rows.find((r) => matches(r, member))
        return { ...member, slot: row ? row.slot : 'buffer', lane: row ? row.lane : null, worktree: row ? row.worktree : null }
    })
    const stale = rows.filter((row) => !members.some((member) => matches(row, member)))
    const slots = {}
    for (const slot of SLOTS) {
        const inSlot = placed.filter((member) => member.slot === slot)
        slots[slot] = { running: inSlot.filter((member) => member.activity === 'running').length, total: inSlot.length }
    }
    return { members: placed, slots, stale }
}
