// Reads the repository's lane state on a timer and puts it where the agent reads, so the
// agent does not pay for a dozen shell calls every few minutes.
//
// Two surfaces, one measurement:
//   - a runtime context block, refreshed on the timer, which is already in front of the model
//     at every step without anyone asking for it;
//   - a tool, for when the agent wants the numeric form instead of the prose.
//
// Every reading names the repository it was taken from. A reading that cannot be taken is
// reported as unreadable, never as zero: "could not read the tree" and "the tree has nothing"
// are different facts and this file refuses to merge them.
//
// The fetch exit code is reported for the same reason. A local reference that was never
// refreshed agrees with itself, so every reading built on it looks calm whether or not the
// remote moved.

import { execFileSync } from 'node:child_process'
import { readFileSync } from 'node:fs'
import path from 'node:path'

// The row's config is merged over the defaults below, so there is no schema export here:
// a plain object is not one, and the harness calls validate on the schema it is given.
export const name = 'camel-lanes'
export const inject = ['tools', 'systemPrompt', 'timer']

const DEFAULTS = {
    repo: 'D:\\Projects\\Camel',
    intervalSeconds: 300,
    staleMinutes: 120,
    // The lane table has moved once already. Both places are tried, newest first, because a
    // single path that stops existing reports "no lane table" and that reads exactly like
    // "no stalled lane".
    lanesFiles: ['internal/lanes.md', 'docs/handover/lanes.cn.md'],
    fetch: true,
}

// The gates the coordinator works to. Each one counts something that must not exceed a limit;
// crossing one changes what the next hour is allowed to do, so crossing one is the only thing
// in this block that is allowed to shout. A number that is under its limit is not news.
const GATES = [
    { key: 'unmerged', limit: 20, what: 'unmerged lane branches, so no new branch may be opened' },
    { key: 'maxBehind', limit: 100, what: 'commits behind on the worst lane, so merging it is unsafe' },
    { key: 'mergedLocal', limit: 20, what: 'local branches already merged and not deleted' },
    { key: 'mergedRemote', limit: 10, what: 'remote branches already merged and not deleted' },
    {
        key: 'inboxBehind',
        limit: 0,
        what: 'inbox commits on fsh-dev that are not on develop, so the task list being worked from is a copy someone read rather than the one in the tree',
    },
    {
        key: 'claimedNotDeleted',
        limit: 0,
        what: 'items a lane is working from that are still in the inbox, so the inbox says nobody has taken them',
    },
]

function git(repo, args) {
    return execFileSync('git', args, {
        cwd: repo,
        encoding: 'utf8',
        stdio: ['ignore', 'pipe', 'ignore'],
        windowsHide: true,
        maxBuffer: 16 * 1024 * 1024,
        timeout: 120_000,
    }).trim()
}

function readText(file) {
    return readFileSync(file, 'utf8')
}

/// One reading, or an object that says why there is none. Never undefined and never zero.
function attempt(run) {
    try {
        return run()
    } catch (error) {
        return { unreadable: String(error && error.message ? error.message : error).split('\n')[0] }
    }
}

/// How long ago a `2026-11-22 23:30` stamp was, in minutes, or null when the cell is not a stamp.
function ageMinutesOf(stamp, now) {
    const match = /(\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2})/.exec(stamp)
    if (!match) return null
    const at = new Date(
        Number(match[1]),
        Number(match[2]) - 1,
        Number(match[3]),
        Number(match[4]),
        Number(match[5]),
    )
    return Math.round((now - at.getTime()) / 60000)
}

/// Every row of the lane table with the branch it names and how long ago it was touched.
export function readLanes(file, staleMinutes, now = Date.now()) {
    let text
    try {
        text = readFileSync(file, 'utf8')
    } catch {
        return { unreadable: `no lane table at ${file}` }
    }
    const rows = []
    for (const line of text.split(/\r?\n/)) {
        if (!line.startsWith('|')) continue
        const cells = line
            .split('|')
            .slice(1, -1)
            .map((cell) => cell.trim())
        if (cells.length < 6) continue
        const [branch, doing, branchReading, developReading, next, stamp] = cells
        if (!branch.startsWith('lane/')) continue
        const age = ageMinutesOf(stamp, now)
        // A lane is closed only when its stage names an end state. Matching on a leading 已 instead would
// also silence "已开,未提交" -- opened, nothing committed -- which is the row most in need of a
// nudge. The list is of ending words, not of the characters they start with.
        const closed = /^已(合入|收|弃|取代|关闭)/.test(doing)
        rows.push({
            branch,
            doing,
            branchReading,
            developReading,
            next,
            stamp,
            ageMinutes: age,
            closed,
            stalled: !closed && age !== null && age > staleMinutes,
        })
    }
    // Closed rows stay in the table on purpose: a lane that ended is history worth keeping, and
    // dropping it would make the file disagree with the branches it describes.
    return {
        rows,
        closed: rows.filter((row) => row.closed).map((row) => row.branch),
        stalled: rows.filter((row) => row.stalled).map((row) => row.branch),
    }
}

/// Local branches with no unmerged work, and remote branches already contained in develop.
///
/// The remote side is written as `origin/<name>` on purpose. A `refs/heads/<name>` string does
/// not resolve locally, so every comparison returns non-zero, and the count comes out as "all
/// of them" while looking perfectly reasonable. A count whose list cannot be printed is not a
/// reading, so both lists are kept.
function branchesAlreadyMerged(repo) {
    const merged = (candidate, reference) => {
        try {
            git(repo, ['merge-base', '--is-ancestor', candidate, reference])
            return true
        } catch {
            return false
        }
    }

    const local = git(repo, ['for-each-ref', '--format=%(refname:short)', 'refs/heads'])
        .split('\n')
        .filter((name) => name && !['develop', 'main', 'fsh-dev'].includes(name))
    const mergedLocal = local.filter((name) => merged(name, 'origin/develop'))

    const heads = git(repo, ['for-each-ref', '--format=%(refname:short)', 'refs/remotes/origin'])
        .split('\n')
        .map((name) => name.replace(/^origin\//, ''))
        .filter((name) => name && name !== 'HEAD' && !['develop', 'main', 'fsh-dev'].includes(name))
    const mergedRemote = heads.filter((name) => merged(`origin/${name}`, 'origin/develop'))

    return { mergedLocal, mergedRemote }
}

/// Everything one beat measures. Each entry is a reading or an object saying it was unreadable.
export function measure({ repo, lanesFiles, staleMinutes, fetch }) {
    const now = new Date()
    const at = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(
        now.getDate(),
    ).padStart(2, '0')} ${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}`

    // The exit code is part of the reading. Swallowing it is how a run of "nothing changed"
    // becomes blind: the values below are then read from references nobody refreshed.
    let fetched
    if (fetch === false) {
        fetched = 'skipped'
    } else {
        fetched = attempt(() => {
            git(repo, ['fetch', 'origin', '--prune'])
            return 0
        })
    }

    return {
        repo,
        at,
        fetched,
        develop: attempt(() => ({
            head: git(repo, ['rev-parse', '--short=9', 'origin/develop']),
            age: git(repo, ['log', '-1', '--format=%cr', 'origin/develop']),
        })),
        head: attempt(() => git(repo, ['rev-parse', '--abbrev-ref', 'HEAD'])),
        dirty: attempt(() => {
            const porcelain = git(repo, ['status', '--porcelain', '--untracked-files=all'])
            return porcelain === '' ? 0 : porcelain.split('\n').length
        }),
        worktrees: attempt(() => git(repo, ['worktree', 'list']).split('\n').length - 1),
        branches: attempt(() => {
            const names = git(repo, ['for-each-ref', '--format=%(refname:short)', 'refs/heads'])
                .split('\n')
                .filter((name) => name && !['develop', 'main', 'release', 'fsh-dev'].includes(name))
            const rows = []
            for (const branch of names) {
                let ahead
                try {
                    ahead = Number(git(repo, ['rev-list', '--count', `origin/develop..${branch}`]))
                } catch {
                    continue
                }
                if (ahead === 0) continue
                let behind = null
                try {
                    behind = Number(
                        git(repo, ['rev-list', '--count', '--no-merges', `${branch}..origin/develop`]),
                    )
                } catch {
                    behind = null
                }
                rows.push({ branch, ahead, behind })
            }
            const behindValues = rows.map((row) => row.behind).filter((value) => value !== null)
            return {
                unmerged: rows.length,
                maxBehind: behindValues.length === 0 ? 0 : Math.max(...behindValues),
                rows,
            }
        }),
        merged: attempt(() => branchesAlreadyMerged(repo)),
        fshDev: attempt(() => git(repo, ['rev-parse', '--short=9', 'origin/fsh-dev'])),
        // An item is claimed by deleting it from the inbox and entering it in the backlog under the
        // same number. Both halves are needed: the inbox is what nobody has taken, the backlog is
        // what someone is doing. A number that appears in both is an item that was taken and not
        // removed, and the inbox then reports it as waiting.
        inbox: attempt(() => {
            const behind = Number(
                git(repo, ['rev-list', '--count', 'origin/develop..origin/fsh-dev', '--', 'internal/inbox.md']),
            )
            let claimedNotDeleted = 0
            let unreadable = null
            try {
                const inbox = readText(path.join(repo, 'internal', 'inbox.md'))
                const backlog = readText(path.join(repo, 'internal', 'backlog.md'))
                const inInbox = new Set((inbox.match(/\bD-\d{4}-\d+\b/g) || []))
                const doing = new Set(
                    (backlog.match(/^\|\s*(D-\d{4}-\d+)\s*\|[^\n]*\|\s*doing\s*\|/gm) || []).map(
                        (row) => row.match(/^\|\s*(D-\d{4}-\d+)/)[1],
                    ),
                )
                for (const number of doing) if (inInbox.has(number)) claimedNotDeleted += 1
            } catch (error) {
                unreadable = String(error && error.message ? error.message : error)
            }
            if (unreadable !== null) return { unreadable: `cannot read the inbox or the backlog: ${unreadable}` }
            return { behind, claimedNotDeleted }
        }),
        lanes: (() => {
            const tried = []
            for (const candidate of lanesFiles) {
                const table = readLanes(path.join(repo, candidate), staleMinutes, now.getTime())
                if (!table.unreadable) return table
                tried.push(candidate)
            }
            return { unreadable: `no lane table; tried ${tried.join(' and ')}` }
        })(),
    }
}

/// The numbers the gates are stated in, pulled out of wherever they were measured. A gate whose
/// reading failed is null, which is neither over nor under its limit.
export function gateValues(state) {
    const value = (f) => (f && !f.unreadable ? f : null)
    const branches = value(state.branches)
    const mergedBranches = value(state.merged)
    const inbox = value(state.inbox)
    return {
        unmerged: branches ? branches.unmerged : null,
        maxBehind: branches ? branches.maxBehind : null,
        worktrees: typeof state.worktrees === 'number' ? state.worktrees : null,
        mergedLocal: mergedBranches ? mergedBranches.mergedLocal.length : null,
        mergedRemote: mergedBranches ? mergedBranches.mergedRemote.length : null,
        inboxBehind: inbox ? inbox.behind : null,
        claimedNotDeleted: inbox ? inbox.claimedNotDeleted : null,
    }
}

/// The gates that are over their limit.
export function breaches(state) {
    const values = gateValues(state)
    return GATES.filter((gate) => values[gate.key] !== null && values[gate.key] > gate.limit).map(
        (gate) => ({ ...gate, value: values[gate.key] }),
    )
}

/// The block the model sees. Short on purpose: it is read at every step.
export function render(state, silenced = new Map()) {
    const lines = []
    const over = breaches(state).filter((gate) => !silenced.has(gate.key))
    if (over.length > 0) {
        lines.push(`Camel lane state — ${over.length} gate(s) over the limit, act on these first:`)
        for (const gate of over) {
            lines.push(`- OVER ${gate.key}=${gate.value} (limit ${gate.limit}): ${gate.what}`)
        }
    } else {
        lines.push(`Camel lane state, every gate under its limit, read at ${state.at}.`)
    }
    lines.push(`From ${state.repo}.`)

    const show = (label, value) => {
        if (value === undefined) return
        if (typeof value === 'object' && value !== null && 'unreadable' in value) {
            lines.push(`- ${label}: unreadable — ${value.unreadable}`)
            return
        }
        lines.push(`- ${label}: ${typeof value === 'object' ? JSON.stringify(value) : value}`)
    }

    if (state.fetched === 'skipped') {
        lines.push('- fetch: skipped by config, so the readings below come from local references and may be stale')
    } else if (typeof state.fetched === 'object' && state.fetched !== null && 'unreadable' in state.fetched) {
        lines.push(`- fetch: failed — ${state.fetched.unreadable}. Every reading below may be stale.`)
    } else if (state.fetched !== 0) {
        lines.push(`- fetch: exit ${state.fetched}. Every reading below may be stale.`)
    }

    if (state.develop && !state.develop.unreadable) {
        lines.push(`- origin/develop: ${state.develop.head} (${state.develop.age})`)
    } else {
        show('origin/develop', state.develop)
    }
    show('HEAD', state.head)
    show('working tree changes', state.dirty)
    // Live lanes occupy worktrees, so this count alone is not litter: it is only news beside
    // the number of lanes actually running, which this plugin cannot see.
    if (typeof state.worktrees === 'number') {
        lines.push(
            `- worktrees besides the main checkout: ${state.worktrees} ` +
                '(live lanes occupy these, so this count alone is not litter)',
        )
    } else {
        show('worktrees besides the main checkout', state.worktrees)
    }

    if (state.branches && !state.branches.unreadable) {
        const { unmerged, maxBehind, rows } = state.branches
        lines.push(`- unmerged lane branches: ${unmerged}, worst ${maxBehind} commits behind`)
        for (const row of rows) {
            lines.push(
                `  - ${row.branch}: ${row.ahead} ahead, ${row.behind === null ? 'unknown' : row.behind} behind`,
            )
        }
    } else {
        show('lane branches', state.branches)
    }

    if (state.merged && !state.merged.unreadable) {
        const { mergedLocal, mergedRemote } = state.merged
        const names = mergedLocal.length > 0 && mergedLocal.length <= 6 ? ` (${mergedLocal.join(', ')})` : ''
        lines.push(
            `- already merged, still present: local ${mergedLocal.length}, remote ${mergedRemote.length}${names}`,
        )
    } else {
        show('already merged', state.merged)
    }

    if (typeof state.liveAgents === 'number') {
        lines.push(
            `- agents live in this process: ${state.liveAgents} ` +
                '(finished agents stay live, so this is not the number of lanes at work)',
        )
    } else {
        show('agents live in this process', state.liveAgents)
    }

    show('origin/fsh-dev', state.fshDev)

    if (state.lanes && !state.lanes.unreadable && state.lanes.rows) {
        const stalled = state.lanes.stalled
        lines.push(
            `- lane table: ${state.lanes.rows.length} open row(s)` +
                (stalled.length === 0 ? ', none stalled' : `, stalled: ${stalled.join(', ')}`),
        )
    } else {
        show('lane table', state.lanes)
    }

    lines.push(
        'These readings are refreshed on a timer and are not a substitute for measuring again before acting on them.',
    )
    return lines.join('\n')
}

export function apply(ctx, rawConfig) {
    const config = { ...DEFAULTS, ...(rawConfig || {}) }

    // Gates the reader has asked to be quiet about, and until when. Silence is per gate and it
    // expires: a gate that is genuinely over its limit comes back without anyone remembering to
    // bring it back, which is the only version of this that does not end in a gate nobody sees.
    const silenced = new Map()
    const silence = (gate, minutes) => {
        if (minutes <= 0) silenced.delete(gate)
        else silenced.set(gate, Date.now() + minutes * 60_000)
    }
    const pruneSilence = () => {
        const now = Date.now()
        for (const [gate, until] of silenced) if (until <= now) silenced.delete(gate)
    }

    // Counts the live agents in this process. This is not the number of lanes at work: an
    // agent that has finished its turn stays live, and a lane run by another process is not
    // here at all. It is reported as what it is, because "how many windows are open" and "how
    // many lanes are working" are different questions and only one of them has a reading.
    const liveAgents = () => {
        try {
            const service = ctx.get('agents')
            const list = service && typeof service.list === 'function' ? service.list() : null
            return list ? list.length : { unreadable: 'no agents service' }
        } catch (error) {
            return { unreadable: String(error && error.message ? error.message : error) }
        }
    }

    const read = () => {
        pruneSilence()
        return { ...measure(config), liveAgents: liveAgents() }
    }

    let state = read()
    let text = render(state, silenced)

    ctx.effect(() =>
        ctx.tools.register({
            name: 'camel_lanes_silence',
            description:
                'Silence one lane-state gate for a while, so it stops appearing in the prompt until the time is up. Use it when a gate is over its limit for a reason that is already understood and being worked on. Pass zero minutes to bring it back immediately.',
            parameters: {
                type: 'object',
                properties: {
                    gate: {
                        type: 'string',
                        enum: GATES.map((gate) => gate.key),
                        description: 'Which gate to silence.',
                    },
                    minutes: {
                        type: 'integer',
                        minimum: 0,
                        maximum: 1440,
                        description: 'How long to stay quiet, in minutes. Zero lifts the silence.',
                    },
                },
                required: ['gate', 'minutes'],
                additionalProperties: false,
            },
            output: {
                schema: {
                    type: 'object',
                    properties: { result: { type: 'string' } },
                    required: ['result'],
                    additionalProperties: false,
                },
                render: (value) => value.result,
            },
            async execute({ gate, minutes }) {
                silence(gate, minutes)
                state = read()
                text = render(state, silenced)
                const gateDefinition = GATES.find((entry) => entry.key === gate)
                const value = gateValues(state)[gate]
                return {
                    result:
                        minutes <= 0
                            ? `${gate} is no longer silenced. Its reading is ${value === null ? 'unreadable' : value}.`
                            : `${gate} is silenced for ${minutes} minute(s); it was ${value === null ? 'unreadable' : value} against a limit of ${gateDefinition.limit}.`,
                }
            },
        }),
    )

    ctx.effect(() =>
        ctx.tools.register({
            name: 'camel_lanes',
            description:
                'Read the Camel repository lane state in one call: whether the fetch succeeded, origin/develop head and age, HEAD, working-tree changes, worktree count, every local lane branch with how far ahead and behind it is, local and remote branches already merged but not deleted, origin/fsh-dev, the lane table rows older than the stale limit, how many agents are live in this process, and which gates are over their limits.',
            parameters: { type: 'object', properties: {}, additionalProperties: false },
            output: {
                // The tool answers with the reading as text. The object keeps the numbers
                // addressable for a caller that wants them and gives the renderer one field
                // to print, so a reading is never re-formatted by two places.
                schema: {
                    type: 'object',
                    properties: { readings: { type: 'string' } },
                    required: ['readings'],
                    additionalProperties: false,
                },
                render: (value) => value.readings,
            },
            async execute() {
                state = read()
                text = render(state, silenced)
                return { readings: text + '\n\n' + JSON.stringify(state, null, 2) }
            },
        }),
    )

    ctx.effect(() =>
        ctx.systemPrompt.context({
            name: 'camel-lanes',
            order: 900,
            text: () => text,
        }),
    )

    // One context per gate, each with its own order and its own paragraph, so a gate that is
    // over its limit states what that limit forbids instead of being one line inside a table.
    // A gate that is under its limit, or that has been silenced, contributes nothing at all:
    // an empty section in front of the model at every step is a cost with no reading in it.
    GATES.forEach((gate, index) => {
        ctx.effect(() =>
            ctx.systemPrompt.context({
                name: `camel-lanes-gate-${gate.key}`,
                order: 901 + index,
                text: () => {
                    if (silenced.has(gate.key)) return ''
                    const value = gateValues(state)[gate.key]
                    if (value === null || value <= gate.limit) return ''
                    return (
                        `Gate ${gate.key} is over its limit: ${value} against ${gate.limit}. ` +
                        `${gate.what}. Act on this before starting anything new.`
                    )
                },
            }),
        )
    })

    const beat = () => {
        try {
            state = read()
            text = render(state, silenced)
        } catch (error) {
            const why = String(error && error.message ? error.message : error).split('\n')[0]
            text = `Camel lane state: unreadable this beat — ${why}. The previous reading is stale, not current.`
        }
    }

    ctx.effect(() => ctx.timer.interval(beat, Math.max(30, config.intervalSeconds) * 1000))
}