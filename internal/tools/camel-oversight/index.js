// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// camel-oversight: the collaboration engine between the decision-maker (another machine) and the
// coordinator (a session in this DSH process). Three parts:
//
//   the channel  -- the decision-maker reads the coordinator's state and sends it dispatches; the
//                   coordinator acknowledges and speaks back, pushed as it happens;
//   the watch    -- every few minutes the plugin measures the repository, the backlog, the lanes and
//                   the build host itself (rules.js), and reminds the coordinator only of what is
//                   off: a healthy hour costs it no context at all. It also re-states the standing
//                   rules (internal/collab/rules.md) once an hour, against a long context forgetting
//                   them. This replaces the coordinator's own scheduled prompts;
//   the panel    -- client.js draws the readings, the alerts and every reminder in the GUI.
//
// HTTP routes on the web server the GUI already has, and the model's tools:
//   GET  /oversee/state  the target session's status (idle or running, and since when), its live lanes
//                        (subagents, running or inactive), every root session, a summary of each recent
//                        turn, the messages not yet acknowledged, and the build host's load;
//   GET  /oversee/tail   the last n events of a session, each reduced to one line;
//   POST /oversee/say    {id, text, mode} queues a user message into the target session;
//   GET  /oversee/acks   what the model said about each message (taken, declined, done), and what it
//                        sent of its own accord;
//   GET  /oversee/events a server-sent event stream that pushes each acknowledgement, each message
//                        the model sends, and the claimed session ending, as they happen;
//   tools oversight_claim, oversight_ack, oversight_tell, oversight_readings  the model's side.
//
// The routes are raw web-server routes, outside the GUI's launch-token login. The gate is the network:
// the web server stays on loopback and `tailscale serve` publishes it to the tailnet, whose ACL decides
// who reaches it. A delivery records the Tailscale login that `tailscale serve` attached, as a claim.
//
// A message is queued with `agent.followup` (the next turn) or, with mode "steer", `agent.steer` (the
// nearest step). Neither cancels the running turn. Each message carries an id; a second delivery
// of the same id is acknowledged and dropped, so a retried request does not start a second turn.

import { randomUUID } from 'node:crypto'
import { execFile } from 'node:child_process'
import { appendFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { readFile } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import { pathToFileURL } from 'node:url'
import * as bootMeasure from './measure.js'
import * as bootRules from './rules.js'

// The parts that change most often -- the rules, their limits and texts, and what is measured --
// are reloaded when they change, at the next beat, without restarting the host: a restart stops
// every agent that is working. They are read from the line merges land on -- origin/integration, or
// origin/develop where there is no integration branch -- the content that has passed the merge gate
// and been pushed, never from the checkout's working tree: a merge in progress there holds content
// that has not been measured yet, and a lane's half-done edit may sit there too. Develop moves only
// when integration is promoted, hours apart, and a fix to these files should not wait for that. A
// file that fails to load leaves the version in use in place and is reported. The routes, the tools
// and the session wiring in this file still need a restart.
const HOT_REFS = ['origin/integration', 'origin/develop']
async function hotRef(repo) {
    for (const ref of HOT_REFS) {
        if (await gitIn(repo, ['rev-parse', '--verify', '--quiet', ref]).then(() => true, () => false)) return ref
    }
    return HOT_REFS[HOT_REFS.length - 1]
}
const HOT_DIR = path.join(os.homedir(), '.camel-oversight', 'hot')
const hot = {
    rules: { module: bootRules, blob: null, error: null },
    measure: { module: bootMeasure, blob: null, error: null },
}
const gitIn = (repo, args) =>
    new Promise((resolve, reject) =>
        execFile('git', ['-C', repo, ...args], { env: { ...process.env, GIT_OPTIONAL_LOCKS: '0' }, maxBuffer: 16 << 20 },
            (error, stdout) => (error ? reject(error) : resolve(stdout))))
// The text of a file as the merged line has it (hotRef), or null when it cannot be read.
async function pushedText(repo, file) {
    const ref = await hotRef(repo)
    return gitIn(repo, ['show', `${ref}:${file}`]).catch(() => null)
}
async function reloadHot(repo, onChange) {
    if (!repo) return
    const HOT_REF = await hotRef(repo)
    for (const [name, file] of [['rules', 'rules.js'], ['measure', 'measure.js']]) {
        const slot = hot[name]
        const spec = `internal/tools/camel-oversight/${file}`
        let blob
        try {
            blob = (await gitIn(repo, ['rev-parse', `${HOT_REF}:${spec}`])).trim()
        } catch (error) {
            slot.error = `${spec} at ${HOT_REF}: ${String(error.message).split(/\r?\n/)[0]}`
            continue
        }
        if (slot.blob === null) {
            slot.blob = blob // the version imported at start stands until the pushed one changes
            continue
        }
        if (blob === slot.blob) continue
        slot.blob = blob // a broken version is not retried every beat
        try {
            const text = await gitIn(repo, ['show', `${HOT_REF}:${spec}`])
            mkdirSync(HOT_DIR, { recursive: true })
            const copy = path.join(HOT_DIR, `${name}-${blob.slice(0, 12)}.mjs`)
            if (!existsSync(copy)) writeFileSync(copy, text)
            slot.module = await import(pathToFileURL(copy).href)
            slot.error = null
            onChange && onChange({ file, blob: blob.slice(0, 12), ok: true })
        } catch (error) {
            slot.error = `${file}: ${error.message}`
            onChange && onChange({ file, blob: blob.slice(0, 12), ok: false, error: error.message })
        }
    }
}

export const name = 'camel-oversight'
export const inject = ['agents', 'webServer', 'tools', 'systemPrompt', 'timer']

const DEFAULTS = {
    logFile: path.join(os.homedir(), '.camel-oversight', 'log.jsonl'),
    sender: 'decision-maker',
    tailLimit: 400,
    turnsInState: 8,
    // The repository the watch measures; empty turns the watch off (the channel still works).
    repo: '',
    watchSeconds: 300,
    stateDir: '',
    buildHost: '',
    buildProbe: 'cat /proc/loadavg; nproc; pgrep -fc "ctest|camel" || true',
    rulesFile: 'internal/collab/rules.md',
    rulesRefreshMinutes: 30,
    // Unread dispatches the coordinator may have queued before a new one is refused with 'reorganize'.
    queueLimit: 3,
    // A tool call in the claimed session that has not returned after blockWarnMinutes is reported;
    // after blockCancelMinutes the turn is cancelled (queued messages kept) and the coordinator told why.
    blockWarnMinutes: 10,
    blockCancelMinutes: 30,
    blockExempt: [],
    limits: {},
}

const clip = (text, n) => {
    const flat = String(text ?? '').replace(/\s+/g, ' ').trim()
    return flat.length > n ? flat.slice(0, n - 1) + '…' : flat
}

const textOf = (content) =>
    Array.isArray(content)
        ? content.filter((block) => block && block.type === 'text').map((block) => block.text).join(' ')
        : ''

/// One line per session event; the kinds that say nothing about progress are left out.
function summarize(event) {
    const d = event.data || {}
    switch (event.type) {
        case 'turn/start':
            return `turn ${d.turn} started`
        case 'turn/end':
            return `turn ${d.turn} ended (${d.reason && d.reason.kind ? d.reason.kind : 'done'})`
        case 'user/message':
            return `user: ${clip(textOf(d.content), 300)}`
        case 'assistant/message':
            return `assistant: ${clip(textOf(d.message && d.message.content), 300)}${d.interrupted ? ' [interrupted]' : ''}`
        case 'tool/call':
            return `call ${d.name}(${clip(d.arguments, 200)})`
        case 'tool/result':
            return d.error
                ? `result: error ${d.error.code}${d.error.reason ? ' ' + clip(d.error.reason, 160) : ''}`
                : `result: ${clip(textOf(d.message && d.message.content), 160)}`
        default:
            return null
    }
}

// Values that belong to one machine (the build host's ssh name, where the log goes) live in a file on
// that machine, not in the repository's patch: two layers that both insert this plugin leave only one
// of them mounted, and the one that wins need not be the one that carries the values.
const LOCAL_CONFIG = path.join(os.homedir(), '.camel-oversight', 'config.json')

export function apply(ctx, rawConfig) {
    let local = {}
    try {
        if (existsSync(LOCAL_CONFIG)) local = JSON.parse(readFileSync(LOCAL_CONFIG, 'utf8'))
    } catch (error) {
        ctx.logger && ctx.logger.warn(`camel-oversight: ${LOCAL_CONFIG} is not JSON: ${error.message}`)
    }
    const config = { ...DEFAULTS, ...(rawConfig || {}), ...local }

    // ---- durable log: deliveries and acknowledgements survive a restart ----------------------
    const delivered = new Map() // id -> {id, at, mode, text}
    const acks = new Map() // id -> {id, at, status, note}
    const superseded = new Map() // id -> the id of the dispatch that replaced it before it was read
    let claimed = null // the session that called oversight_claim last
    let dmLastSeen = null // the last request the decision-maker's client made
    const promptReach = { target: 0, other: 0, none: 0 } // whom the prompt context was assembled for
    const tells = [] // what the model sent the decision-maker with oversight_tell
    const record = (entry) => {
        try {
            mkdirSync(path.dirname(config.logFile), { recursive: true })
            appendFileSync(config.logFile, JSON.stringify(entry) + '\n')
        } catch (error) {
            ctx.logger && ctx.logger.warn(`camel-oversight: could not write the log: ${error.message}`)
        }
    }
    if (existsSync(config.logFile)) {
        for (const line of readFileSync(config.logFile, 'utf8').split('\n')) {
            if (!line.trim()) continue
            try {
                const entry = JSON.parse(line)
                if (entry.kind === 'say') delivered.set(entry.id, entry)
                if (entry.kind === 'ack') acks.set(entry.id, entry)
                if (entry.kind === 'superseded') superseded.set(entry.id, entry.by)
                if (entry.kind === 'claim') claimed = entry.session
                if (entry.kind === 'tell') tells.push(entry)
            } catch {
                // A torn last line from a crash is skipped; the rest of the log still holds.
            }
        }
    }

    // ---- what the sessions are doing ---------------------------------------------------------
    const tails = new Map() // session id -> [{seq, time, type, line}]
    const since = new Map() // agent id -> time its status last changed
    // The claimed session's tool calls that have not returned, and its last assistant text: what the
    // drift checks below read (a call that blocks the session, a turn that ends by asking the owner).
    const pendingCalls = new Map() // callId -> {name, at, warned}
    let lastAssistantText = ''
    let onTargetEvent = () => {}
    ctx.effect(() =>
        ctx.on('session/event', (session, event) => {
            const id = String(session.id)
            if (id === claimed) onTargetEvent(event)
            const line = summarize(event)
            if (line === null) return
            const tail = tails.get(id) || []
            tail.push({ seq: event.seq, time: event.time, type: event.type, line })
            if (tail.length > config.tailLimit) tail.splice(0, tail.length - config.tailLimit)
            tails.set(id, tail)
        }),
    )
    ctx.effect(() =>
        ctx.on('agent/status', ({ agent }) => {
            since.set(String(agent.id), Date.now())
        }),
    )

    const headerOf = (agent) => (agent && agent.session && agent.session.header) || {}
    // The subagent runtime is not a dependency of the whole plugin (the channel works without it); it is
    // bound when present, the way DSH's own plugins bind an optional service, and released with it.
    let subagentsRef = null
    if (typeof ctx.inject === 'function') ctx.inject(['subagents'], (inner) => {
        const bound = inner.subagents
        subagentsRef = bound
        inner.effect(() => () => {
            if (subagentsRef === bound) subagentsRef = null
        })
    })
    const subagentService = () => subagentsRef || ctx.get('subagents')
    // A root session is one no other session spawned: the coordinator's own, never a lane.
    const isRoot = (agent) => {
        if (!agent) return false
        const subagents = subagentService()
        try {
            if (subagents && typeof subagents.rootOf === 'function') return subagents.rootOf(agent) === agent
        } catch {
            // fall through to the session header
        }
        return !headerOf(agent).parentSession
    }
    const lastEventAt = (id) => {
        const tail = tails.get(id)
        return tail && tail.length ? tail[tail.length - 1].time : null
    }

    // A root session is one that no other session spawned. Every one is listed in the state, with
    // its directory, so the reader can see which sessions exist and which one is the target.
    const roots = () => ctx.agents.list().filter(isRoot)
    const sessionsNow = () =>
        roots().map((agent) => {
            const id = String(agent.id)
            return {
                id,
                cwd: headerOf(agent).cwd ?? null,
                createdAt: headerOf(agent).createdAt ?? null,
                status: agent.status,
                since: since.get(id) ?? null,
                lastEventAt: lastEventAt(id),
                claimed: claimed === id,
            }
        })

    // The session a message lands in: the one a request names, else the one that last called
    // oversight_claim. A process holds many sessions and none is guessed at: with no claim, or a
    // claimed session that has ended, there is no target and a delivery is refused.
    const target = (named) => ctx.agents.get(named || claimed || '') || null

    // One line per recent turn: its start, the tools it called, and how it ended.
    const recentTurns = (id, count) => {
        const tail = tails.get(id) || []
        const turns = []
        let current = null
        for (const entry of tail) {
            if (entry.type === 'turn/start') {
                current = { start: entry.time, lines: [], end: null }
                turns.push(current)
            } else if (current) {
                if (entry.type === 'turn/end') current.end = entry.line
                else if (entry.type === 'tool/call') current.lines.push(entry.line.slice(5, 60))
            }
        }
        return turns.slice(-count).map((turn) => ({
            started: turn.start,
            ended: turn.end,
            calls: turn.lines.length,
            tools: [...new Set(turn.lines.map((line) => line.split('(')[0]))],
        }))
    }

    const answer = (res, status, value) => {
        const body = JSON.stringify(value)
        res.writeHead(status, {
            'content-type': 'application/json; charset=utf-8',
            'content-length': Buffer.byteLength(body),
            'cache-control': 'no-store',
        })
        res.end(body)
    }

    const readBody = (req, limit = 64 * 1024) =>
        new Promise((resolve, reject) => {
            const chunks = []
            let size = 0
            req.on('data', (chunk) => {
                size += chunk.length
                if (size > limit) {
                    reject(new Error(`body longer than ${limit} bytes`))
                    req.destroy()
                    return
                }
                chunks.push(chunk)
            })
            req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')))
            req.on('error', reject)
        })

    const route = (pathname, method, handler) =>
        ctx.effect(() =>
            ctx.webServer.register({
                kind: 'exact',
                path: pathname,
                handler: async (req, res) => {
                    if (req.method !== method) return answer(res, 405, { error: `use ${method}` })
                    const url = new URL(req.url, 'http://local')
                    // The decision-maker's client names itself, so its absence can be read: the GUI
                    // polls the same routes and must not count as the decision-maker being there.
                    if (url.searchParams.get('client') === 'dm') dmLastSeen = Date.now()
                    try {
                        await handler(req, res, url)
                    } catch (error) {
                        answer(res, 500, { error: clip(error && error.message ? error.message : error, 300) })
                    }
                },
            }),
        )

    // A dispatch is owed an answer once the coordinator has read it. One still waiting in its queue is
    // listed by `queuedDispatches`, not here: naming its id before its text arrives asks for an
    // answer to a message the coordinator cannot see.
    const pending = () => {
        const agent = target()
        return [...delivered.values()]
            .filter((entry) => !acks.has(entry.id) && !superseded.has(entry.id))
            .filter((entry) => !(agent && entry.messageId && stillQueued(agent, entry.messageId)))
            .map((entry) => entry.id)
    }

    // Whether a message is still waiting in the agent's queue, unread.
    const stillQueued = (agent, messageId) => {
        const inbox = agent && agent.inbox
        if (!inbox) return false
        return [...(inbox.nextTurn || []), ...(inbox.nextStep || [])].some((message) => message && message.id === messageId)
    }
    // The dispatches the coordinator has not read yet, oldest first.
    const queuedDispatches = (agent) =>
        [...delivered.values()]
            .filter((entry) => entry.messageId && !superseded.has(entry.id) && stillQueued(agent, entry.messageId))
            .sort((a, b) => a.at - b.at)
            .map((entry) => entry.id)

    // ---- the push half: a server-sent event stream ----------------------------------------------
    // What the coordinator says back (an acknowledgement, a message of its own, the claimed session
    // ending) is pushed to every open /oversee/events stream as it happens, so the reader learns it
    // without polling. A comment line every 25 seconds keeps proxies from closing an idle stream.
    // Every event worth replaying is kept (and rebuilt from the log at load), so a reader that lost
    // the stream -- a tailnet hiccup, a restart on either side -- asks for what it missed with
    // `?since=<ms>` instead of losing it.
    const streams = new Set()
    const replayable = []
    const remember = (event) => {
        if (event.kind === 'beat') return
        replayable.push(event)
        if (replayable.length > 500) replayable.splice(0, replayable.length - 500)
    }
    for (const entry of [...acks.values(), ...tells]) remember(entry)
    replayable.sort((a, b) => a.at - b.at)
    const emit = (event) => {
        remember(event)
        const frame = `data: ${JSON.stringify(event)}\n\n`
        for (const res of streams) res.write(frame)
    }
    ctx.effect(() =>
        ctx.on('agent/disposed', ({ agent }) => {
            if (String(agent.id) === claimed) emit({ kind: 'session-ended', session: claimed, at: Date.now() })
        }),
    )

    // The lanes are the live agents of the target session's subagent tree, read from the process every
    // time they are asked for, so the count cannot go stale the way a hand-kept table does. It is the
    // live set, not the durable catalog: the catalog read (`listDescendants`) walks every child's
    // history and does not return within seconds, and a lane that is not live is not a lane at work.
    // A lane run by another process is not in this process and is not counted.
    // Tree membership by the sessions' own headers: a child session records its parent, so following
    // `parentSession` upwards from a live agent reaches the root it belongs to. This needs nothing but
    // the agents list; the subagent runtime, when it is bound, only supplies the lanes' names.
    const rootIdOf = (agent) => {
        let current = agent
        for (let depth = 0; current && depth < 16; depth += 1) {
            const parent = headerOf(current).parentSession
            if (!parent) return String(current.id)
            const next = ctx.agents.get(parent)
            if (!next) return String(parent)
            current = next
        }
        return null
    }
    const lanesOf = async (id) => {
        const subagents = subagentService()
        const root = id ? ctx.agents.get(id) : null
        if (!root) return { unreadable: 'no claimed session' }
        const members = []
        for (const agent of ctx.agents.list()) {
            if (agent === root) continue
            let inTree = rootIdOf(agent) === String(root.id)
            if (!inTree && subagents && typeof subagents.rootOf === 'function') {
                try {
                    inTree = subagents.rootOf(agent) === root
                } catch {
                    inTree = false
                }
            }
            if (!inTree) continue
            let name = String(agent.id)
            try {
                if (subagents && typeof subagents.namesOf === 'function') name = subagents.namesOf(agent).self
            } catch {
                // an agent whose tree is being torn down keeps its id as its name
            }
            members.push({ id: String(agent.id), name, activity: agent.status === 'running' ? 'running' : 'idle', since: since.get(String(agent.id)) ?? null })
        }
        return { running: members.filter((m) => m.activity === 'running').length, total: members.length, lanes: members }
    }

    // ---- the watch ----------------------------------------------------------------------------------
    // Every watchSeconds the plugin measures and runs the rules. An alert becomes a reminder once its
    // condition has held for the rule's `hold` and the same alert was not delivered within its
    // `cooldown` (an alert whose reading changed is new). All reminders due in one beat are delivered
    // as one message, at the loudest level among them.
    // Limits are read afresh at each beat: the rules file's defaults, then the plugin config, then
    // the local config file, so a threshold changes without a restart.
    const limitsNow = () => {
        let local = {}
        try {
            if (existsSync(LOCAL_CONFIG)) local = JSON.parse(readFileSync(LOCAL_CONFIG, 'utf8')).limits || {}
        } catch {
            local = {}
        }
        return { ...hot.rules.module.DEFAULT_LIMITS, ...(config.limits || {}), ...local }
    }
    let reading = null
    let lanesNow = { unreadable: 'not measured yet' }
    const alerts = new Map() // rule id -> {since, key, text, level, lastDelivered, lastKey}
    const reminders = [] // the most recent deliveries, newest last
    let lastRefresh = 0
    let beating = false

    const unackedOlderThan = (minutes) =>
        [...delivered.values()]
            // A dispatch another one replaced (`supersedes`) is answered by the answer to the replacement.
            .filter((entry) => !acks.has(entry.id) && !superseded.has(entry.id) && Date.now() - entry.at > minutes * 60_000)
            .map((entry) => entry.id)
    // Only dispatch-shaped ids are items; an id a session made up for itself can never have a backlog row.
    const takenIds = () =>
        [...acks.values()]
            .filter((entry) => entry.status === 'taken' && /^[DC]-\d+-\d+$/.test(entry.id) && Date.now() - entry.at > 60 * 60_000)
            .map((entry) => entry.id)
    const backlogOpen = () =>
        reading && reading.backlog instanceof Map ? [...reading.backlog.values()].filter((row) => !row.done).length : 0

    // The plugin's own messages never pile up: a reminder or a rules refresh that is still unread when
    // the next one of its kind is sent is replaced by it, since the newer one is computed from fresher
    // readings and says everything the older one did.
    const lastOfKind = new Map() // 'reminder' | 'rules' -> message id still possibly queued
    const sendToTarget = (agent, level, text, kind = 'reminder') => {
        const message = Object.freeze({
            id: randomUUID(),
            role: 'user',
            source: Object.freeze({ kind: 'user' }),
            content: Object.freeze([Object.freeze({ type: 'text', text })]),
        })
        const previous = lastOfKind.get(kind)
        if (previous && stillQueued(agent, previous)) agent.inbox.remove(previous)
        lastOfKind.set(kind, message.id)
        // DSH has three ways in. inject adds to the running turn's context and starts nothing; steer goes
        // in at the running turn's next step and, with no turn running, waits for one; followup is a new
        // message after the current turn and starts one when the session is idle. So only followup wakes
        // an idle session: urgent and wake reminders use it then, steer and inject only while a turn runs.
        if (agent.status !== 'running' && (level === 'urgent' || level === 'wake')) return agent.followup(message), 'followup'
        if (level === 'urgent') return agent.steer(message), 'steer'
        return agent.inject(message), 'inject'
    }

    const deliver = (due, now) => {
        const agent = target()
        const LEVELS = hot.rules.module.LEVELS
        const level = due.reduce((top, alert) => (LEVELS[alert.level] > LEVELS[top] ? alert.level : top), 'quiet')
        const text = `[oversight · 提醒]\n${due.map((alert) => `- ${alert.text}`).join('\n')}`
        const mode = agent ? sendToTarget(agent, level, text) : 'held (no claimed session)'
        const entry = { kind: 'reminder', at: now, level, mode, rules: due.map((alert) => alert.id), text }
        record(entry)
        emit(entry)
        reminders.push(entry)
        if (reminders.length > 50) reminders.splice(0, reminders.length - 50)
        if (!agent) return
        for (const alert of due) {
            const state = alerts.get(alert.id)
            state.lastDelivered = now
            state.lastKey = alert.key
        }
    }

    // ---- keeping the standing rules in front of the coordinator --------------------------------------
    // A long-running session forgets what its skill said and drifts: it asks the owner, it runs a long
    // job in the foreground and cannot answer. Three layers, each measurable:
    //   always      the rules text is part of the claimed session's prompt context at every step
    //               (`promptReach` counts which sessions the prompt actually reached);
    //   periodic    every rulesRefreshMinutes, and right after a claim, the rules are injected into the
    //               session's context -- this path does not depend on the prompt reaching it;
    //   on sight    a turn that ends by asking the owner, or a call to a question tool, gets a reminder at
    //               once; a tool call that blocks the session is reported, and cancelled when it lasts.
    let rulesText = ''
    const loadRules = async () => {
        if (!config.repo) return
        // As pushed, not as the checkout's working tree has it mid-merge (see hotRef).
        const pushed = await pushedText(config.repo, config.rulesFile)
        const text = pushed !== null ? pushed : await readFile(path.join(config.repo, config.rulesFile), 'utf8').catch(() => '')
        if (text.trim()) rulesText = text.trim()
    }
    void loadRules()

    const nudge = (id, level, text, how) => {
        const agent = target()
        const now = Date.now()
        const mode = agent ? (how ? how(agent) : sendToTarget(agent, level, text)) : 'held (no claimed session)'
        const entry = { kind: 'reminder', at: now, level, mode, rules: [id], text }
        record(entry)
        emit(entry)
        reminders.push(entry)
        if (reminders.length > 50) reminders.splice(0, reminders.length - 50)
    }

    const refreshRules = async (now, force = false) => {
        const agent = target()
        if (!agent || !rulesText) return
        if (!force && now - lastRefresh < config.rulesRefreshMinutes * 60_000) return
        const tail = tails.get(String(agent.id)) || []
        const active = tail.length > 0 && now - tail[tail.length - 1].time < 60 * 60_000
        if (!force && !active) return
        lastRefresh = now
        sendToTarget(agent, 'quiet', `[oversight · 铁律复读]\n${rulesText}`, 'rules')
        const entry = { kind: 'reminder', at: now, level: 'quiet', mode: 'inject', rules: ['rules-refresh'], text: '铁律复读' }
        record(entry)
        reminders.push(entry)
    }

    // A turn that ends by asking the owner: the last assistant text ends in a question mark, or asks for
    // a decision in so many words. The owner is not there to answer; the rule is to go on with the default.
    const ASKS = /(请问|要不要|你希望|是否需要|需要我|确认一下|等你|请你决定|你来定|你看呢|行吗|可以吗|你说一声|不说我就|你点头|你同意的话|你若同意|由你定|你定|你否决|Should I|Would you like|Let me know|shall I)/i
    const asksOwner = (text) => {
        const tailText = String(text || '').trim().slice(-400)
        return tailText !== '' && (/[?？]\s*$/.test(tailText) || ASKS.test(tailText))
    }
    let lastAskNudge = 0
    let lastTurn = null
    const QUESTION_TOOLS = new Set(['ask_user_question', 'ask_user'])
    onTargetEvent = (event) => {
        const d = event.data || {}
        const at = event.time || Date.now()
        if (event.type === 'tool/call') {
            pendingCalls.set(String(d.callId), { name: d.name, at, warned: false })
            if (QUESTION_TOOLS.has(d.name) && Date.now() - lastAskNudge > 10 * 60_000) {
                lastAskNudge = Date.now()
                nudge('asked-owner', 'urgent', '[oversight · 纠偏] 你在向负责人提问。铁律 1：不问负责人、不等——按默认做法继续；真有问题就用 oversight_tell 直接问决策者（问句、读数、默认做法）。')
            }
        } else if (event.type === 'tool/result') {
            const callId = d.message && d.message.source ? d.message.source.callId : d.callId
            pendingCalls.delete(String(callId))
        } else if (event.type === 'assistant/message') {
            lastAssistantText = textOf(d.message && d.message.content)
        } else if (event.type === 'turn/start') {
            lastAssistantText = ''
        } else if (event.type === 'turn/end') {
            pendingCalls.clear()
            const aborted = d.reason && d.reason.kind === 'aborted'
            // What the turn ended on, for the rules: a turn that ends on a promise ("I will ... after ...")
            // leaves the work to a message that may never come.
            lastTurn = { at, text: String(lastAssistantText || ''), aborted: Boolean(aborted) }
            if (!aborted && asksOwner(lastAssistantText) && Date.now() - lastAskNudge > 10 * 60_000) {
                lastAskNudge = Date.now()
                nudge('asked-owner', 'wake', '[oversight · 纠偏] 你上一轮以问句结束，在等负责人回答。铁律 1：不问负责人、不等——按默认做法继续干；真有问题就用 oversight_tell 直接问决策者（问句、读数、两三条路各带代价、默认做法）。')
            }
        }
    }

    // A call that does not return blocks the session: dispatches queue behind it, reminders cannot reach
    // it, and nobody can talk to it. Reported at blockWarnMinutes; at blockCancelMinutes the turn is
    // cancelled with its queued messages kept, and the coordinator is told why in the next turn.
    const checkBlocking = () => {
        const agent = target()
        if (!agent) return
        const now = Date.now()
        // A call that only waits on a background job loses nothing when interrupted: the job keeps
        // running. While such a wait holds dispatches behind it, the turn is ended at once, not after
        // blockCancelMinutes, since the dispatches cannot arrive any other way.
        const held = [...delivered.values()].filter(
            (entry) => entry.messageId && !superseded.has(entry.id) && stillQueued(agent, entry.messageId) &&
                now - entry.at >= (config.heldDispatchMinutes ?? 10) * 60_000,
        )
        for (const [callId, call] of pendingCalls) {
            if ((config.blockExempt || []).includes(call.name)) continue
            if (held.length && (config.waitTools || ['job_output']).includes(call.name)) {
                pendingCalls.delete(callId)
                const text = `[oversight · 纠偏] 你在 ${call.name} 上等后台任务，而 ${held.length} 条派工（${held.map((e) => e.id).join('、')}）排在这一轮之后到不了。插件结束了这一轮，后台任务照常在跑。先读派工，读数好了推送会叫醒你（铁律：你只做判断，不在会话里久等）。`
                nudge('wait-ended', 'urgent', text, (a) => {
                    a.cancel({ kind: 'user' }, { keepInbox: true })
                    a.followup(Object.freeze({
                        id: randomUUID(), role: 'user', source: Object.freeze({ kind: 'user' }),
                        content: Object.freeze([Object.freeze({ type: 'text', text })]),
                    }))
                    return 'cancel+followup'
                })
                continue
            }
            const minutes = (now - call.at) / 60_000
            if (minutes >= config.blockCancelMinutes) {
                pendingCalls.delete(callId)
                nudge('blocked-cancelled', 'urgent',
                    `[oversight · 纠偏] 工具调用 ${call.name} 阻塞会话 ${Math.round(minutes)} 分钟，插件已中断那一轮（排队的消息都保留）。铁律"你只做判断"：会话只做判断；长活交车道或后台运行，不占住会话——否则派工与提醒都进不来。`,
                    (a) => {
                        a.cancel({ kind: 'user' }, { keepInbox: true })
                        a.followup(Object.freeze({
                            id: randomUUID(), role: 'user', source: Object.freeze({ kind: 'user' }),
                            content: Object.freeze([Object.freeze({ type: 'text', text: `[oversight · 纠偏] 工具调用 ${call.name} 阻塞会话 ${Math.round(minutes)} 分钟，插件已中断那一轮（排队的消息都保留）。铁律"你只做判断"：会话只做判断；长活交车道或后台运行，不占住会话。接着干，别重跑那条阻塞命令。` })]),
                        }))
                        return 'cancel+followup'
                    })
            } else if (minutes >= config.blockWarnMinutes && !call.warned) {
                call.warned = true
                nudge('blocking', 'wake', `协调者的工具调用 ${call.name} 已阻塞会话 ${Math.round(minutes)} 分钟；到 ${config.blockCancelMinutes} 分钟插件会中断那一轮。`, () => 'reported')
            }
        }
    }
    ctx.effect(() => ctx.timer.interval(checkBlocking, 60_000))

    const beat = async () => {
        if (beating || !config.repo) return
        beating = true
        try {
            const now = Date.now()
            await reloadHot(config.repo, (change) => {
                const entry = { kind: 'hot-reload', at: now, ...change }
                record(entry)
                emit(entry)
            })
            const limits = limitsNow()
            reading = await hot.measure.module.measure({
                repo: config.repo,
                fetch: config.fetch !== false,
                stateDir: config.stateDir,
                buildHost: config.buildHost,
                buildProbe: config.buildProbe,
            })
            const agent = target()
            lanesNow = await lanesOf(agent ? String(agent.id) : null)
            const facts = {
                m: reading,
                lanes: lanesNow,
                limits,
                now,
                unacked: unackedOlderThan(20),
                stuck: agent
                    ? [...delivered.values()]
                          .filter((entry) => entry.messageId && !superseded.has(entry.id) && stillQueued(agent, entry.messageId))
                          .map((entry) => ({ id: entry.id, minutes: Math.floor((now - entry.at) / 60_000) }))
                    : [],
                taken: takenIds(),
                backlogOpen: backlogOpen(),
                target: agent ? { status: agent.status, since: since.get(String(agent.id)) ?? null } : null,
                dmLastSeen,
                lastTurn,
            }
            const due = []
            for (const rule of hot.rules.module.RULES) {
                let found = null
                try {
                    found = rule.check(facts)
                } catch (error) {
                    found = null
                    ctx.logger && ctx.logger.warn(`camel-oversight: rule ${rule.id} failed: ${error.message}`)
                }
                if (rule.id === 'coordinator-idle' && found && facts.target && facts.target.since !== null) {
                    // The idle rule's hold runs from when the session went idle, not from when the
                    // watch first noticed it.
                    if (now - facts.target.since < rule.hold * 60_000) found = null
                }
                if (!found) {
                    alerts.delete(rule.id)
                    continue
                }
                const state = alerts.get(rule.id) || { since: now, lastDelivered: null, lastKey: null }
                Object.assign(state, { key: found.key, text: found.text, level: rule.level })
                alerts.set(rule.id, state)
                const held = rule.id === 'coordinator-idle' || now - state.since >= rule.hold * 60_000
                const cooled = state.lastDelivered === null || now - state.lastDelivered >= rule.cooldown * 60_000 || state.lastKey !== found.key
                if (held && cooled) due.push({ id: rule.id, level: rule.level, key: found.key, text: found.text })
            }
            if (due.length > 0) deliver(due, now)
            await loadRules()
            await refreshRules(now)
            emit({ kind: 'beat', at: now, alerts: [...alerts.keys()] })
        } finally {
            beating = false
        }
    }
    ctx.effect(() => ctx.timer.interval(() => void beat(), Math.max(60, config.watchSeconds) * 1000))
    ctx.effect(() => ctx.timer.timeout(() => void beat(), 20_000))

    const readingSummary = () => {
        if (!reading) return null
        const value = (v) => (v && v.unreadable ? { unreadable: v.unreadable } : v)
        const backlog = reading.backlog instanceof Map ? reading.backlog : null
        return {
            at: reading.at,
            fetched: reading.fetched,
            develop: value(reading.develop),
            fshDev: value(reading.fshDev),
            checkout: value(reading.checkout),
            worktrees: value(reading.worktrees),
            branches: reading.branches && !reading.branches.unreadable
                ? { unmerged: reading.branches.unmerged, maxBehind: reading.branches.maxBehind }
                : value(reading.branches),
            merged: reading.merged && !reading.merged.unreadable
                ? { local: reading.merged.mergedLocal.length, remote: reading.merged.mergedRemote.length }
                : value(reading.merged),
            backlog: backlog
                ? {
                      rows: backlog.size,
                      doing: [...backlog.values()].filter((row) => row.doing).length,
                      blocked: [...backlog.values()].filter((row) => row.blocked).length,
                      open: [...backlog.values()].filter((row) => !row.done).length,
                  }
                : value(reading.backlog),
            panel: value(reading.panel),
            host: value(reading.host),
        }
    }
    const alertsNow = () =>
        [...alerts.entries()].map(([id, state]) => ({
            id,
            level: state.level,
            text: state.text,
            since: state.since,
            lastDelivered: state.lastDelivered,
        }))

    route('/oversee/state', 'GET', async (req, res) => {
        const agent = target()
        const id = agent ? String(agent.id) : null
        answer(res, 200, {
            at: Date.now(),
            target: agent ? { id, cwd: headerOf(agent).cwd ?? null, status: agent.status, since: since.get(id) ?? null } : null,
            lanes: id ? await lanesOf(id) : lanesNow,
            alerts: alertsNow(),
            reminders: reminders.slice(-10),
            reading: readingSummary(),
            sessions: sessionsNow(),
            recentTurns: id ? recentTurns(id, config.turnsInState) : [],
            unacknowledged: pending(),
            queuedDispatches: agent ? queuedDispatches(agent) : [],
            decisionMaker: { lastSeen: dmLastSeen },
            promptReach,
            blocking: [...pendingCalls.values()].map((call) => ({ name: call.name, minutes: Math.round((Date.now() - call.at) / 60_000) })),
            watch: config.repo ? { repo: config.repo, everySeconds: config.watchSeconds } : { off: 'no repo in config' },
            hotLoaded: Object.fromEntries(Object.entries(hot).map(([name, slot]) => [name, { blob: slot.blob && slot.blob.slice(0, 12), error: slot.error }])),
        })
    })

    route('/oversee/tail', 'GET', (req, res, url) => {
        const agent = target()
        const id = url.searchParams.get('session') || (agent ? String(agent.id) : '')
        const n = Math.min(Math.max(Number(url.searchParams.get('n')) || 50, 1), config.tailLimit)
        answer(res, 200, { session: id, events: (tails.get(id) || []).slice(-n) })
    })

    route('/oversee/acks', 'GET', (req, res) => {
        answer(res, 200, { acks: [...acks.values()], tells: tells.slice(-50), unacknowledged: pending() })
    })

    route('/oversee/events', 'GET', (req, res, url) => {
        res.writeHead(200, {
            'content-type': 'text/event-stream; charset=utf-8',
            'cache-control': 'no-store',
            connection: 'keep-alive',
        })
        res.write(`: open, target ${claimed ?? 'none'}\n\n`)
        const after = Number(url.searchParams.get('since'))
        if (Number.isFinite(after) && after > 0) {
            const missed = replayable.filter((event) => event.at > after)
            if (missed.length > 5) {
                // A reader back from a long absence gets one summary, not a flood it would work through
                // one by one; the details are a read of /oversee/acks away.
                const kinds = {}
                for (const event of missed) kinds[event.kind] = (kinds[event.kind] || 0) + 1
                res.write(`data: ${JSON.stringify({ kind: 'missed', at: missed[missed.length - 1].at, count: missed.length, kinds, read: '/oversee/acks' })}\n\n`)
            } else {
                for (const event of missed) res.write(`data: ${JSON.stringify({ ...event, replayed: true })}\n\n`)
            }
        }
        streams.add(res)
        const beat = setInterval(() => res.write(': ping\n\n'), 25_000)
        req.on('close', () => {
            clearInterval(beat)
            streams.delete(res)
        })
    })

    route('/oversee/say', 'POST', async (req, res) => {
        const raw = await readBody(req)
        const body = raw === '' ? {} : JSON.parse(raw)
        const id = typeof body.id === 'string' ? body.id.trim() : ''
        const text = typeof body.text === 'string' ? body.text.trim() : ''
        const mode = body.mode === 'steer' ? 'steer' : 'queue'
        if (!id || !text) return answer(res, 400, { error: 'body must be {"id": "...", "text": "...", "mode"?: "queue"|"steer"}' })
        if (delivered.has(id)) return answer(res, 200, { id, duplicate: true, deliveredAt: delivered.get(id).at })
        const agent = target(typeof body.session === 'string' ? body.session : '')
        if (!agent) return answer(res, 409, { error: 'no session has claimed the channel (oversight_claim), and the request names none', sessions: sessionsNow() })
        // A newer dispatch may replace older ones the coordinator has not read yet: they are taken out of
        // its queue before it reaches them, so it acts on the reorganized message, not on a backlog of
        // stale ones. One already read cannot be taken back; it stays and is reported as such.
        const supersedes = Array.isArray(body.supersedes) ? body.supersedes.filter((x) => typeof x === 'string') : []
        const withdrawn = []
        const alreadyRead = []
        for (const old of supersedes) {
            const entry = delivered.get(old)
            if (!entry || superseded.has(old)) continue
            if (entry.messageId && stillQueued(agent, entry.messageId) && agent.inbox.remove(entry.messageId)) {
                superseded.set(old, id)
                record({ kind: 'superseded', id: old, by: id, at: Date.now() })
                withdrawn.push(old)
            } else {
                alreadyRead.push(old)
            }
        }
        // Back-pressure: past queueLimit unread dispatches the coordinator would work through a backlog
        // in order, behind which anything newer or more urgent waits. The sender is told to reorganize
        // them into one message (with `supersedes`), not to keep adding.
        const queued = queuedDispatches(agent)
        if (mode !== 'steer' && queued.length >= config.queueLimit) {
            return answer(res, 429, {
                error: `the coordinator has ${queued.length} unread dispatches queued (limit ${config.queueLimit}); reorganize them into one message and send it with "supersedes"`,
                queued,
            })
        }
        const message = Object.freeze({
            id: randomUUID(),
            role: 'user',
            source: Object.freeze({ kind: 'user' }),
            content: Object.freeze([Object.freeze({ type: 'text', text: `[${config.sender} · ${id}] ${text}` })]),
        })
        if (mode === 'steer') agent.steer(message)
        else agent.followup(message)
        const from = req.headers['tailscale-user-login'] || null
        const entry = { kind: 'say', id, at: Date.now(), mode, text, session: String(agent.id), from, messageId: message.id, supersedes: withdrawn }
        delivered.set(id, entry)
        record(entry)
        answer(res, 202, { id, mode, session: String(agent.id), status: agent.status, withdrawn, alreadyRead, queued: queued.length + 1 })
    })

    // ---- the model's side ------------------------------------------------------------------------
    ctx.effect(() =>
        ctx.tools.register({
            name: 'oversight_claim',
            description:
                'Make this session the one the oversight channel delivers to and reports on. Call it once at the start of a coordinating session; a later claim by another session takes over.',
            parameters: { type: 'object', properties: {}, additionalProperties: false },
            output: {
                schema: { type: 'object', properties: { result: { type: 'string' } }, required: ['result'], additionalProperties: false },
                render: (_args, value) => [{ type: 'text', text: value.result }],
            },
            async execute() {
                const agent = ctx.agents.currentInitiator()
                if (!agent) return { result: 'no initiating session; nothing claimed' }
                // Only a root session can be the coordinator. A lane is a subagent of it, and a lane
                // that claimed would take the channel, the reminders and the rules away from it.
                if (!isRoot(agent)) {
                    return { result: 'refused: this session is a subagent; only the coordinator\'s own (root) session claims the channel' }
                }
                claimed = String(agent.id)
                pendingCalls.clear()
                void refreshRules(Date.now(), true)
                record({ kind: 'claim', session: claimed, at: Date.now() })
                return { result: `the oversight channel now targets session ${claimed}` }
            },
        }),
    )

    ctx.effect(() =>
        ctx.tools.register({
            name: 'oversight_ack',
            description:
                `Acknowledge a message that arrived prefixed "[${config.sender} · <id>]". Say whether you took it, declined it (give the reason), or finished it.`,
            parameters: {
                type: 'object',
                properties: {
                    id: { type: 'string', description: 'The id in the message prefix.' },
                    status: { type: 'string', enum: ['taken', 'declined', 'done'] },
                    note: { type: 'string', description: 'One or two sentences: what you will do, why not, or what was done.' },
                },
                required: ['id', 'status'],
                additionalProperties: false,
            },
            output: {
                schema: { type: 'object', properties: { result: { type: 'string' } }, required: ['result'], additionalProperties: false },
                render: (_args, value) => [{ type: 'text', text: value.result }],
            },
            async execute({ id, status, note }) {
                const entry = { kind: 'ack', id, at: Date.now(), status, note: note || '' }
                acks.set(id, entry)
                record(entry)
                emit(entry)
                return { result: `${id}: ${status}` }
            },
        }),
    )

    ctx.effect(() =>
        ctx.tools.register({
            name: 'oversight_tell',
            description:
                'Send the decision-maker a message of your own, pushed to it at once: a blocker, a reading it must see now, a result that changes what it would dispatch next. Not for acknowledging its messages (use oversight_ack) and not for questions of principle (write a page under internal/questions/ and tell it the page).',
            parameters: {
                type: 'object',
                properties: {
                    text: { type: 'string', description: 'What to tell it, self-contained.' },
                    urgent: { type: 'boolean', description: 'True only when it should act before its next scheduled check.' },
                },
                required: ['text'],
                additionalProperties: false,
            },
            output: {
                schema: { type: 'object', properties: { result: { type: 'string' } }, required: ['result'], additionalProperties: false },
                render: (_args, value) => [{ type: 'text', text: value.result }],
            },
            async execute({ text, urgent }) {
                const entry = { kind: 'tell', at: Date.now(), urgent: urgent === true, text: String(text), session: claimed }
                tells.push(entry)
                if (tells.length > 200) tells.splice(0, tells.length - 200)
                record(entry)
                emit(entry)
                return { result: streams.size > 0 ? 'pushed to the decision-maker' : 'logged; the decision-maker reads it at its next check' }
            },
        }),
    )

    ctx.effect(() =>
        ctx.systemPrompt.context({
            name: 'camel-oversight',
            order: 950,
            text: () => {
                // Only the claimed session gets the rules and the channel's state; another session in
                // this process has nothing to do with them. With no claim, a session working in the
                // repository is reminded that the coordinator claims first.
                const aimed = target()
                const initiator = ctx.agents.currentInitiator()
                if (!initiator) promptReach.none += 1
                else if (aimed && initiator === aimed) promptReach.target += 1
                else promptReach.other += 1
                if (!aimed) {
                    const cwd = String((headerOf(initiator) || {}).cwd || '').toLowerCase().replace(/\//g, '\\')
                    const repo = String(config.repo || '').toLowerCase().replace(/\//g, '\\')
                    // A root session working in the repository itself; a lane's worktree beside it
                    // (`Camel-wt\…`) shares the prefix but is not "in" the repository, and a subagent
                    // is never the coordinator.
                    return initiator && repo && isRoot(initiator) && (cwd === repo || cwd.startsWith(repo.replace(/\\+$/, '') + '\\'))
                        ? '没有会话认领 camel-oversight 通道。若你是 Camel 的协调者，第一件事调 oversight_claim，然后按 internal/collab/SKILL.md 与 coordinator.md 工作。'
                        : ''
                }
                if (initiator !== aimed) return ''
                // Only stable text goes here: the rules (they change when rules.md does) and how the
                // channel works. This is assembled into every request ahead of the conversation, so text
                // that changed with each measurement -- unanswered ids, open alerts -- broke the prompt
                // cache for the whole conversation behind it. Those reach the session as messages (a
                // newer one of the same kind replaces an unread older one) and on the band.
                const head =
                    `Messages prefixed "[${config.sender} · <id>]" come from the decision-maker through the oversight channel. ` +
                    'Each is a dispatch: acknowledge it with oversight_ack in the same turn (taken, and record it in internal/backlog.md under the same id; declined with a reason; or done with the commit). ' +
                    'Messages prefixed "[oversight · …]" are the plugin\'s own reminders from its measurements; act on them, no acknowledgement needed. oversight_readings gives the measurements on demand.'
                return (rulesText ? [rulesText, head] : [head]).join(' ')
            },
        }),
    )

    ctx.effect(() =>
        ctx.tools.register({
            name: 'oversight_readings',
            description:
                'The latest measurements of the repository, backlog, lanes and build host, and the open alerts, in one call. Use it instead of running git and shell commands to count lanes, branches or backlog rows.',
            parameters: { type: 'object', properties: {}, additionalProperties: false },
            output: {
                schema: { type: 'object', properties: { result: { type: 'string' } }, required: ['result'], additionalProperties: false },
                render: (_args, value) => [{ type: 'text', text: value.result }],
            },
            async execute() {
                return { result: JSON.stringify({ reading: readingSummary(), lanes: lanesNow, alerts: alertsNow(), unacknowledged: pending() }, null, 2) }
            },
        }),
    )
}
