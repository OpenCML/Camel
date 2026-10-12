// Test drivers for camel-db: its HTTP API (the gateway and its workers) and its debug adapter.
//
// No dependencies: the tests run with the Node that runs the test runner. A test file is a
// script of its own:
//
//     import { runTest, check, Gateway, DapClient } from '../../../tools/debugger/client.mjs'
//     await runTest(async (t) => {
//         const dap = await t.dap()            // a debug adapter (camel-db --dap), or
//         const gw = await t.gateway()         // a gateway with its HTTP API
//         check(cond, 'what was expected')
//     })
//
// It prints one `ok N checks` line and exits 0, or prints the failed checks and exits 1.
// Every process a test starts is killed when the test ends, however it ends.

import { spawn } from 'node:child_process'
import fs from 'node:fs'
import { request as httpRequest } from 'node:http'
import net from 'node:net'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
export const REPO_ROOT = path.resolve(HERE, '..', '..', '..')
export const FIXTURES = path.join(REPO_ROOT, 'test', 'cases', 'tools', 'debugger', 'fixtures')

export function debuggerPath() {
    const exe = process.platform === 'win32' ? 'camel-db.exe' : 'camel-db'
    const home = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
    return path.join(home, 'bin', exe)
}

/** The absolute path of a fixture program. */
export function fixture(name) {
    return path.join(FIXTURES, name)
}

/** The 1-based line of the `nth` line of a file that contains `needle`. */
export function lineOf(file, needle, nth = 1) {
    const lines = fs.readFileSync(file, 'utf8').split('\n')
    let seen = 0
    for (let i = 0; i < lines.length; i++) {
        if (lines[i].includes(needle) && ++seen === nth) {
            return i + 1
        }
    }
    throw new Error(`occurrence ${nth} of ${JSON.stringify(needle)} not found in ${file}`)
}

let failures = 0
let checks = 0

/** Records one assertion; a failed one is printed and fails the script at the end. */
export function check(cond, what, detail) {
    checks++
    if (!cond) {
        failures++
        console.log(`FAILED: ${what}`)
        if (detail !== undefined) {
            const text = typeof detail === 'string' ? detail : JSON.stringify(detail, null, 2)
            console.log(String(text).split('\n').map((l) => `    ${l}`).join('\n'))
        }
    }
    return Boolean(cond)
}

export function checkEq(actual, expected, what) {
    const a = JSON.stringify(actual)
    const e = JSON.stringify(expected)
    return check(a === e, what, `expected ${e}\nactual   ${a}`)
}

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

/** Polls `probe` until it returns something truthy; null after `timeoutMs`. */
export async function until(probe, timeoutMs = 15000, stepMs = 25) {
    const deadline = Date.now() + timeoutMs
    for (;;) {
        const v = await probe()
        if (v) {
            return v
        }
        if (Date.now() > deadline) {
            return null
        }
        await sleep(stepMs)
    }
}

/** Two adjacent TCP ports on 127.0.0.1 that are free now (the gateway's and its first worker's). */
async function freePortPair() {
    const isFree = (port) =>
        new Promise((resolve) => {
            const s = net.createServer()
            s.once('error', () => resolve(false))
            s.listen(port, '127.0.0.1', () => s.close(() => resolve(true)))
        })
    for (let i = 0; i < 200; i++) {
        const port = 20000 + Math.floor(Math.random() * 20000)
        if ((await isFree(port)) && (await isFree(port + 1)) && (await isFree(port + 2))) {
            return port
        }
    }
    throw new Error('no free port pair')
}

/** JSON over HTTP to a camel-db process. A failed connection is `{ status: 0 }`. */
export function http(port, method, pathAndQuery, body, timeoutMs = 10000) {
    // node:http, not fetch: fetch loads a WebAssembly parser, which cannot reserve its address
    // space under the virtual-memory limit the test runs are given.
    return new Promise((resolve) => {
        const payload = body === undefined ? undefined : typeof body === 'string' ? body : JSON.stringify(body)
        const req = httpRequest(
            {
                host: '127.0.0.1',
                port,
                method,
                path: pathAndQuery,
                agent: false,
                headers: payload === undefined
                    ? {}
                    : { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(payload) },
                timeout: timeoutMs,
            },
            (res) => {
                const chunks = []
                res.on('data', (c) => chunks.push(c))
                res.on('end', () => {
                    const text = Buffer.concat(chunks).toString('utf8')
                    let json
                    try {
                        json = JSON.parse(text)
                    } catch {
                        json = undefined
                    }
                    resolve({ status: res.statusCode, text, json })
                })
            },
        )
        req.on('timeout', () => req.destroy(new Error(`no response within ${timeoutMs} ms`)))
        req.on('error', (e) => resolve({ status: 0, text: String(e), json: undefined }))
        req.end(payload)
    })
}

/** The gateway: `camel-db --serve <port>`, as the Web UI talks to it. */
export class Gateway {
    static async start(options = {}) {
        const gw = new Gateway()
        gw.port = await freePortPair()
        gw.output = ''
        gw.proc = spawn(debuggerPath(), ['--serve', String(gw.port)], {
            stdio: ['ignore', 'pipe', 'pipe'],
            env: { ...process.env, ...(options.env || {}) },
        })
        gw.proc.stdout.on('data', (d) => (gw.output += d))
        gw.proc.stderr.on('data', (d) => (gw.output += d))
        gw.exited = new Promise((resolve) => gw.proc.on('exit', (code, signal) => resolve({ code, signal })))
        const up = await until(async () => (await gw.get('/api/state')).status === 200, 15000)
        if (!up) {
            throw new Error(`the gateway did not start: ${gw.output}`)
        }
        return gw
    }

    get(pathAndQuery, timeoutMs) {
        return http(this.port, 'GET', pathAndQuery, undefined, timeoutMs)
    }

    post(pathAndQuery, body = {}, timeoutMs) {
        return http(this.port, 'POST', pathAndQuery, body, timeoutMs)
    }

    /** The entry of a task in GET /api/state. */
    async task(id) {
        const st = await this.get('/api/state')
        return (st.json?.tasks || []).find((t) => t.id === id)
    }

    /** Waits until the task satisfies `predicate`; returns its entry, or null on timeout. */
    waitTask(id, predicate, timeoutMs = 15000) {
        return until(async () => {
            const t = await this.task(id)
            return t && predicate(t) ? t : null
        }, timeoutMs)
    }

    async stop() {
        if (this.proc.exitCode === null && this.proc.signalCode === null) {
            this.proc.kill('SIGKILL')
            await this.exited
        }
    }
}

/** How long a test waits for an answer or an event before it calls it missing. Generous: the
 *  test machines run several builds at once, and a wait that is satisfied returns at once. */
export const WAIT_MS = 60000

/**
 * The debug adapter: `camel-db --dap`, spoken to over its standard input and output as an
 * editor does. Requests resolve with the response message (also a failed one: check
 * `success`); events are queued and consumed in order with `waitEvent`.
 */
export class DapClient {
    static async start(options = {}) {
        const dap = new DapClient()
        dap.output = '' // what the adapter wrote to its standard error (it should write nothing)
        dap.proc = spawn(debuggerPath(), ['--dap'], {
            stdio: ['pipe', 'pipe', 'pipe'],
            env: { ...process.env, ...(options.env || {}) },
            cwd: options.cwd,
        })
        dap.seq = 1
        dap.pending = new Map() // request seq -> resolve
        dap.events = [] // every event, in order of arrival
        dap.taken = new Set() // indexes of events a waitEvent has returned
        dap.waiters = [] // { name, predicate, resolve }
        dap.responses = [] // responses that answer no request of this client (request_seq 0)
        dap.protocolErrors = []
        dap.buffer = Buffer.alloc(0)
        dap.proc.stdout.on('data', (d) => dap.#feed(d))
        dap.proc.stderr.on('data', (d) => (dap.output += d))
        dap.proc.stdin.on('error', () => {})
        dap.exited = new Promise((resolve) => dap.proc.on('exit', (code, signal) => resolve({ code, signal })))
        return dap
    }

    #feed(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk])
        for (;;) {
            const headerEnd = this.buffer.indexOf('\r\n\r\n')
            if (headerEnd < 0) {
                return
            }
            const header = this.buffer.subarray(0, headerEnd).toString('latin1')
            const m = /Content-Length: *(\d+)/i.exec(header)
            if (!m) {
                this.protocolErrors.push(`header without Content-Length: ${JSON.stringify(header)}`)
                this.buffer = this.buffer.subarray(headerEnd + 4)
                continue
            }
            const length = Number(m[1])
            if (this.buffer.length < headerEnd + 4 + length) {
                return
            }
            const body = this.buffer.subarray(headerEnd + 4, headerEnd + 4 + length).toString('utf8')
            this.buffer = this.buffer.subarray(headerEnd + 4 + length)
            let message
            try {
                message = JSON.parse(body)
            } catch (e) {
                this.protocolErrors.push(`body is not JSON: ${body.slice(0, 200)}`)
                continue
            }
            this.#dispatch(message)
        }
    }

    #dispatch(message) {
        if (message.type === 'response') {
            const resolve = this.pending.get(message.request_seq)
            if (resolve) {
                this.pending.delete(message.request_seq)
                resolve(message)
            } else {
                this.responses.push(message)
            }
        } else if (message.type === 'event') {
            this.events.push(message)
            this.#serveWaiters()
        } else {
            this.protocolErrors.push(`neither a response nor an event: ${JSON.stringify(message).slice(0, 200)}`)
        }
    }

    #take(name, predicate) {
        for (let i = 0; i < this.events.length; i++) {
            const e = this.events[i]
            if (!this.taken.has(i) && e.event === name && (!predicate || predicate(e.body || {}))) {
                this.taken.add(i)
                return e
            }
        }
        return null
    }

    #serveWaiters() {
        this.waiters = this.waiters.filter((w) => {
            const e = this.#take(w.name, w.predicate)
            if (e) {
                w.resolve(e)
                return false
            }
            return true
        })
    }

    /** Sends raw bytes: for what no well-behaved client sends. */
    sendRaw(text) {
        this.proc.stdin.write(text)
    }

    sendMessage(message) {
        const body = Buffer.from(JSON.stringify(message), 'utf8')
        this.proc.stdin.write(Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body]))
    }

    /** One request; resolves with its response, or with `{ success: false, timeout: true }`. */
    request(command, args, timeoutMs = WAIT_MS) {
        const seq = this.seq++
        return new Promise((resolve) => {
            const timer = setTimeout(() => {
                this.pending.delete(seq)
                resolve({ success: false, timeout: true, command, message: `no response to ${command} within ${timeoutMs} ms` })
            }, timeoutMs)
            this.pending.set(seq, (response) => {
                clearTimeout(timer)
                resolve(response)
            })
            const message = { seq, type: 'request', command }
            if (args !== undefined) {
                message.arguments = args
            }
            this.sendMessage(message)
        })
    }

    /** The next event `name` (that satisfies `predicate`) no earlier wait has returned; null
     *  when none arrives in time. Events of one name are consumed in the order they were sent. */
    waitEvent(name, predicate, timeoutMs = WAIT_MS) {
        if (typeof predicate === 'number') {
            timeoutMs = predicate
            predicate = undefined
        }
        const now = this.#take(name, predicate)
        if (now) {
            return Promise.resolve(now)
        }
        return new Promise((resolve) => {
            const waiter = { name, predicate, resolve: (e) => (clearTimeout(timer), resolve(e)) }
            const timer = setTimeout(() => {
                this.waiters = this.waiters.filter((w) => w !== waiter)
                resolve(null)
            }, timeoutMs)
            this.waiters.push(waiter)
        })
    }

    /** The text of the `output` events of a category so far. */
    outputOf(category) {
        return this.events
            .filter((e) => e.event === 'output' && e.body?.category === category)
            .map((e) => e.body.output)
            .join('')
    }

    /** Waits until the output of `category` contains `text`; false when it does not in time. */
    async waitOutput(category, text, timeoutMs = WAIT_MS) {
        return Boolean(await until(() => this.outputOf(category).includes(text), timeoutMs))
    }

    /**
     * initialize, launch, the breakpoints, configurationDone: a session up to the point where
     * the program runs. `breakpoints` maps a file to its lines. Returns what each step answered.
     */
    async launch(program, config = {}, breakpoints = {}, initializeArgs = {}) {
        const out = {}
        out.initialize = await this.request('initialize', { clientID: 'test', adapterID: 'camel', ...initializeArgs })
        out.launch = await this.request('launch', { program, ...config })
        if (!out.launch.success) {
            return out
        }
        out.initialized = await this.waitEvent('initialized')
        out.breakpoints = {}
        for (const [file, lines] of Object.entries(breakpoints)) {
            const r = await this.request('setBreakpoints', {
                source: { path: file },
                breakpoints: lines.map((line) => ({ line })),
            })
            out.breakpoints[file] = r.body?.breakpoints || []
            out.setBreakpoints = r
        }
        out.configurationDone = await this.request('configurationDone')
        return out
    }

    /** The next `stopped` event with where it is: `{ reason, body, frames, top, line, name }`. */
    async waitStop(timeoutMs = WAIT_MS) {
        const e = await this.waitEvent('stopped', timeoutMs)
        if (!e) {
            return null
        }
        const st = await this.request('stackTrace', { threadId: e.body.threadId })
        const frames = st.body?.stackFrames || []
        return {
            reason: e.body.reason,
            body: e.body,
            frames,
            top: frames[0],
            line: frames[0]?.line,
            name: frames[0]?.name,
            names: frames.map((f) => f.name),
        }
    }

    /** The variables of a scope of a frame as `{ name: { value, type, variablesReference } }`. */
    async variables(frameId, scopeName = 'Locals') {
        const scopes = await this.request('scopes', { frameId })
        const scope = (scopes.body?.scopes || []).find((s) => s.name === scopeName)
        if (!scope) {
            return null
        }
        return this.children(scope.variablesReference)
    }

    async children(variablesReference) {
        const r = await this.request('variables', { variablesReference })
        const out = {}
        for (const v of r.body?.variables || []) {
            out[v.name] = v
        }
        return out
    }

    /** Ends the session: disconnect, then the adapter process by its pid if it is still there.
     *  The worker dies with the adapter (it is killed on disconnect, and by the system when its
     *  parent is gone). */
    async stop() {
        if (this.proc.exitCode === null && this.proc.signalCode === null) {
            const gone = Promise.race([this.exited, sleep(3000)])
            try {
                this.sendMessage({ seq: this.seq++, type: 'request', command: 'disconnect', arguments: { terminateDebuggee: true } })
            } catch {
                // the pipe is closed
            }
            await gone
        }
        if (this.proc.exitCode === null && this.proc.signalCode === null) {
            this.proc.kill('SIGKILL')
            await this.exited
        }
    }
}

/** Runs `body`, cleans up what it started, prints the verdict and exits. */
export async function runTest(body) {
    const started = []
    const scratch = []
    const t = {
        async gateway(options) {
            const gw = await Gateway.start(options)
            started.push(gw)
            return gw
        },
        /** A debug adapter, not yet initialised. */
        async dap(options) {
            const dap = await DapClient.start(options)
            started.push(dap)
            return dap
        },
        /** A scratch folder removed when the test ends. */
        dir() {
            const d = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-db-test-'))
            scratch.push(d)
            return d
        },
        /** Writes a program into a scratch folder and returns its path. */
        program(name, text) {
            const file = path.join(t.dir(), name)
            fs.writeFileSync(file, text)
            return file
        },
        track(resource) {
            started.push(resource)
            return resource
        },
    }
    let crashed = null
    try {
        await body(t)
    } catch (e) {
        crashed = e
    }
    for (const s of started.reverse()) {
        try {
            await s.stop()
        } catch {
            // already gone
        }
    }
    for (const d of scratch) {
        fs.rmSync(d, { recursive: true, force: true })
    }
    if (crashed) {
        failures++
        console.log(`FAILED: the test threw: ${crashed.stack || crashed}`)
    }
    if (failures > 0) {
        for (const s of started) {
            if (s.output && s.output.trim()) {
                console.log(`process output:\n${s.output}`)
            }
        }
        console.log(`${failures} of ${checks} checks failed`)
        process.exit(1)
    }
    console.log(`ok ${checks} checks`)
    process.exit(0)
}
