// camel-oversight: a narrow channel through which a supervising agent on another machine reads what
// this DSH process is doing and leaves it messages.
//
// Four HTTP routes on the web server the GUI already has, and one tool for the model:
//   GET  /oversee/state  the target session's status (idle or running, and since when), every live
//                        agent's status, a one-line summary of each recent turn, the messages sent
//                        through this channel that are not yet acknowledged, and the build host's load;
//   GET  /oversee/tail   the last n events of a session, each reduced to one line;
//   POST /oversee/say    {id, text, mode} queues a user message into the target session;
//   GET  /oversee/acks   what the model said about each message (taken, declined, done);
//   tool oversight_ack   how the model says it.
//
// The routes are raw web-server routes, outside the GUI's launch-token login. The gate is the network:
// the web server stays on loopback and `tailscale serve` publishes it to the tailnet, whose ACL decides
// who reaches it. A delivery records the Tailscale login that `tailscale serve` attached, as a claim.
//
// A message is queued with `agent.followup` (the next turn) or, with mode "steer", `agent.steer` (the
// nearest step). Neither cancels the running turn. Each message carries an id; a second delivery
// of the same id is acknowledged and dropped, so a retried request does not start a second turn.

import { execFile } from 'node:child_process'
import { randomUUID } from 'node:crypto'
import { appendFileSync, existsSync, mkdirSync, readFileSync } from 'node:fs'
import os from 'node:os'
import path from 'node:path'

export const name = 'camel-oversight'
export const inject = ['agents', 'webServer', 'tools', 'systemPrompt']

const DEFAULTS = {
    logFile: path.join(os.homedir(), '.camel-oversight', 'log.jsonl'),
    sessionId: '',
    sender: 'decision-maker',
    tailLimit: 400,
    turnsInState: 8,
    buildHost: '',
    buildProbe: 'cat /proc/loadavg; nproc; pgrep -fc "ctest|camel" || true',
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

export function apply(ctx, rawConfig) {
    const config = { ...DEFAULTS, ...(rawConfig || {}) }

    // ---- durable log: deliveries and acknowledgements survive a restart ----------------------
    const delivered = new Map() // id -> {id, at, mode, text}
    const acks = new Map() // id -> {id, at, status, note}
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
            } catch {
                // A torn last line from a crash is skipped; the rest of the log still holds.
            }
        }
    }

    // ---- what the sessions are doing ---------------------------------------------------------
    const tails = new Map() // session id -> [{seq, time, type, line}]
    const since = new Map() // agent id -> time its status last changed
    ctx.effect(() =>
        ctx.on('session/event', (session, event) => {
            const line = summarize(event)
            if (line === null) return
            const id = String(session.id)
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

    const agentsNow = () =>
        ctx.agents.list().map((agent) => ({
            id: String(agent.id),
            status: agent.status,
            since: since.get(String(agent.id)) ?? null,
        }))

    // The session a message lands in: the configured one, or the only root agent. Several roots
    // are refused rather than guessed between.
    const target = () => {
        if (config.sessionId) return ctx.agents.get(config.sessionId) || null
        const subagents = ctx.get('subagents')
        const roots = ctx.agents
            .list()
            .filter((agent) => !subagents || typeof subagents.rootOf !== 'function' || subagents.rootOf(agent) === agent)
        return roots.length === 1 ? roots[0] : null
    }

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

    // ---- the build host ------------------------------------------------------------------------
    let build = { unconfigured: true }
    let buildAt = 0
    const probeBuild = () => {
        if (!config.buildHost) return
        if (Date.now() - buildAt < 60_000) return
        buildAt = Date.now()
        execFile(
            'ssh',
            ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5', config.buildHost, config.buildProbe],
            { timeout: 10_000 },
            (error, stdout) => {
                if (error) {
                    build = { unreadable: clip(error.message, 160), at: Date.now() }
                    return
                }
                const [loadavg = '', cores = '', jobs = ''] = stdout.split('\n')
                const [one, five, fifteen] = loadavg.split(' ').map(Number)
                build = { load: [one, five, fifteen], cores: Number(cores), testProcesses: Number(jobs), at: Date.now() }
            },
        )
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
                    try {
                        await handler(req, res, new URL(req.url, 'http://local'))
                    } catch (error) {
                        answer(res, 500, { error: clip(error && error.message ? error.message : error, 300) })
                    }
                },
            }),
        )

    const pending = () => [...delivered.values()].filter((entry) => !acks.has(entry.id)).map((entry) => entry.id)

    route('/oversee/state', 'GET', (req, res) => {
        probeBuild()
        const agent = target()
        const id = agent ? String(agent.id) : null
        answer(res, 200, {
            at: Date.now(),
            target: agent ? { id, status: agent.status, since: since.get(id) ?? null } : null,
            agents: agentsNow(),
            recentTurns: id ? recentTurns(id, config.turnsInState) : [],
            unacknowledged: pending(),
            build,
        })
    })

    route('/oversee/tail', 'GET', (req, res, url) => {
        const agent = target()
        const id = url.searchParams.get('session') || (agent ? String(agent.id) : '')
        const n = Math.min(Math.max(Number(url.searchParams.get('n')) || 50, 1), config.tailLimit)
        answer(res, 200, { session: id, events: (tails.get(id) || []).slice(-n) })
    })

    route('/oversee/acks', 'GET', (req, res) => {
        answer(res, 200, { acks: [...acks.values()], unacknowledged: pending() })
    })

    route('/oversee/say', 'POST', async (req, res) => {
        const raw = await readBody(req)
        const body = raw === '' ? {} : JSON.parse(raw)
        const id = typeof body.id === 'string' ? body.id.trim() : ''
        const text = typeof body.text === 'string' ? body.text.trim() : ''
        const mode = body.mode === 'steer' ? 'steer' : 'queue'
        if (!id || !text) return answer(res, 400, { error: 'body must be {"id": "...", "text": "...", "mode"?: "queue"|"steer"}' })
        if (delivered.has(id)) return answer(res, 200, { id, duplicate: true, deliveredAt: delivered.get(id).at })
        const agent = target()
        if (!agent) return answer(res, 409, { error: 'no single root session; set sessionId in the plugin config' })
        const message = Object.freeze({
            id: randomUUID(),
            role: 'user',
            source: Object.freeze({ kind: 'user' }),
            content: Object.freeze([Object.freeze({ type: 'text', text: `[${config.sender} · ${id}] ${text}` })]),
        })
        if (mode === 'steer') agent.steer(message)
        else agent.followup(message)
        const from = req.headers['tailscale-user-login'] || null
        const entry = { kind: 'say', id, at: Date.now(), mode, text, session: String(agent.id), from }
        delivered.set(id, entry)
        record(entry)
        answer(res, 202, { id, mode, session: String(agent.id), status: agent.status })
    })

    // ---- the model's side ------------------------------------------------------------------------
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
                render: (value) => value.result,
            },
            async execute({ id, status, note }) {
                const entry = { kind: 'ack', id, at: Date.now(), status, note: note || '' }
                acks.set(id, entry)
                record(entry)
                return { result: `${id}: ${status}` }
            },
        }),
    )

    ctx.effect(() =>
        ctx.systemPrompt.context({
            name: 'camel-oversight',
            order: 950,
            text: () => {
                const waiting = pending()
                const head =
                    `Messages prefixed "[${config.sender} · <id>]" come from the decision-maker through the oversight channel. ` +
                    'Treat each as an inbox item: act on it as you would on one in internal/inbox.md, and acknowledge it with oversight_ack (taken, declined with a reason, or done).'
                return waiting.length === 0 ? head : `${head} Not yet acknowledged: ${waiting.join(', ')}.`
            },
        }),
    )
}
