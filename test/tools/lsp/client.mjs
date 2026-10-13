// A minimal LSP client for the tests of camel-ls.
//
// Starts the server, speaks JSON-RPC over stdio with `Content-Length` framing, and offers the
// handful of helpers the tests need (open a document, wait for a notification, convert a
// `line:column` marker to a position). No dependencies: the tests run with the Node that runs
// the test runner.
//
// A test file is a script of its own:
//
//     import { withServer, check } from '../../../tools/lsp/client.mjs'
//     await withServer(async (ls) => { ... check(cond, 'what was expected') ... })
//
// It prints one `ok N checks` line and exits 0, or prints the failed checks and exits 1.

import { spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
export const REPO_ROOT = path.resolve(HERE, '..', '..', '..')

function serverPath() {
    const exe = process.platform === 'win32' ? 'camel-ls.exe' : 'camel-ls'
    const home = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
    return path.join(home, 'bin', exe)
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
            console.log(text.split('\n').map((l) => `    ${l}`).join('\n'))
        }
    }
    return cond
}

export function checkEq(actual, expected, what) {
    const a = JSON.stringify(actual)
    const e = JSON.stringify(expected)
    return check(a === e, what, `expected ${e}\nactual   ${a}`)
}

export class LspClient {
    constructor(options = {}) {
        this.proc = spawn(serverPath(), ['--stdio', ...(options.args || [])], {
            stdio: ['pipe', 'pipe', 'pipe'],
            env: { ...process.env, ...(options.env || {}) },
        })
        this.buffer = Buffer.alloc(0)
        this.nextId = 1
        this.pending = new Map() // id -> {resolve}
        this.notifications = [] // received and not yet consumed
        this.waiters = [] // {method, predicate, resolve}
        this.serverRequests = [] // requests the server sent to the client
        this.stderr = ''
        this.exited = new Promise((resolve) => {
            this.proc.on('exit', (code, signal) => {
                this.exitCode = code
                this.exitSignal = signal
                // A request the server never answered must not hang the test.
                for (const { resolve: r } of this.pending.values()) {
                    r({ error: { code: -1, message: `server exited (code ${code}, signal ${signal})` } })
                }
                this.pending.clear()
                for (const w of this.waiters) {
                    w.resolve(null)
                }
                this.waiters = []
                resolve({ code, signal })
            })
        })
        this.proc.stdin.on('error', () => {})
        this.proc.stderr.on('data', (d) => {
            this.stderr += d.toString()
        })
        this.proc.stdout.on('data', (d) => this.#onData(d))
    }

    #onData(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk])
        for (;;) {
            const headerEnd = this.buffer.indexOf('\r\n\r\n')
            if (headerEnd < 0) {
                return
            }
            const header = this.buffer.subarray(0, headerEnd).toString('ascii')
            const m = /Content-Length: (\d+)/i.exec(header)
            if (!m) {
                throw new Error(`frame without Content-Length: ${header}`)
            }
            const length = Number(m[1])
            const start = headerEnd + 4
            if (this.buffer.length < start + length) {
                return
            }
            const body = this.buffer.subarray(start, start + length).toString('utf8')
            this.buffer = this.buffer.subarray(start + length)
            this.#onMessage(JSON.parse(body))
        }
    }

    #onMessage(msg) {
        if (msg.id !== undefined && msg.method === undefined) {
            const p = this.pending.get(msg.id)
            if (p) {
                this.pending.delete(msg.id)
                p.resolve(msg)
            }
            return
        }
        if (msg.id !== undefined) {
            // A request from the server: answer with null so that it never waits for us.
            this.serverRequests.push(msg)
            this.sendRaw({ jsonrpc: '2.0', id: msg.id, result: null })
            return
        }
        const i = this.waiters.findIndex(
            (w) => w.method === msg.method && (!w.predicate || w.predicate(msg.params)),
        )
        if (i >= 0) {
            const [w] = this.waiters.splice(i, 1)
            w.resolve(msg.params)
        } else {
            this.notifications.push(msg)
        }
    }

    /** Writes bytes as they are: for the tests of malformed input. */
    writeBytes(data) {
        this.proc.stdin.write(data)
    }

    sendRaw(msg) {
        const body = Buffer.from(JSON.stringify(msg), 'utf8')
        this.proc.stdin.write(`Content-Length: ${body.length}\r\n\r\n`)
        this.proc.stdin.write(body)
    }

    /** Sends a request and resolves with the whole response (`result` or `error`). */
    requestRaw(method, params, timeoutMs = 20000) {
        const id = this.nextId++
        if (this.exitCode !== undefined || this.exitSignal !== undefined) {
            // The server is gone: say so at once instead of waiting for the timeout.
            return Promise.resolve({
                error: { code: -1, message: `server exited (code ${this.exitCode}, signal ${this.exitSignal})` },
            })
        }
        return new Promise((resolve) => {
            const timer = setTimeout(() => {
                this.pending.delete(id)
                resolve({ error: { code: -2, message: `no response to ${method} within ${timeoutMs} ms` } })
            }, timeoutMs)
            this.pending.set(id, {
                resolve: (msg) => {
                    clearTimeout(timer)
                    resolve(msg)
                },
            })
            this.sendRaw({ jsonrpc: '2.0', id, method, params })
        })
    }

    /** Sends a request and resolves with its result; a response with an error throws. */
    async request(method, params, timeoutMs) {
        const msg = await this.requestRaw(method, params, timeoutMs)
        if (msg.error) {
            throw new Error(`${method}: ${msg.error.message} (${msg.error.code})`)
        }
        return msg.result
    }

    notify(method, params) {
        this.sendRaw({ jsonrpc: '2.0', method, params })
    }

    /** The next notification of `method` that satisfies `predicate`; null on timeout or exit. */
    waitFor(method, predicate, timeoutMs = 20000) {
        const i = this.notifications.findIndex(
            (n) => n.method === method && (!predicate || predicate(n.params)),
        )
        if (i >= 0) {
            const [n] = this.notifications.splice(i, 1)
            return Promise.resolve(n.params)
        }
        if (this.exitCode !== undefined || this.exitSignal !== undefined) {
            return Promise.resolve(null)
        }
        return new Promise((resolve) => {
            const waiter = { method, predicate, resolve: undefined }
            const timer = setTimeout(() => {
                const at = this.waiters.indexOf(waiter)
                if (at >= 0) {
                    this.waiters.splice(at, 1)
                }
                resolve(null)
            }, timeoutMs)
            waiter.resolve = (params) => {
                clearTimeout(timer)
                resolve(params)
            }
            this.waiters.push(waiter)
        })
    }

    async initialize(extra = {}) {
        const result = await this.request('initialize', {
            processId: process.pid,
            rootUri: null,
            capabilities: {},
            ...extra,
        })
        this.notify('initialized', {})
        return result
    }

    /** Opens a document and returns its URI. `name` is a file name inside the scratch folder. */
    open(name, text, version = 1) {
        const uri = this.uriOf(name)
        this.notify('textDocument/didOpen', {
            textDocument: { uri, languageId: 'camel', version, text },
        })
        return uri
    }

    /** Replaces the whole text of a document. */
    change(uri, text, version) {
        this.notify('textDocument/didChange', {
            textDocument: { uri, version },
            contentChanges: [{ text }],
        })
    }

    /** The diagnostics the server publishes for `uri` next (for a given version when stated). */
    diagnosticsFor(uri, version, timeoutMs) {
        return this.waitFor(
            'textDocument/publishDiagnostics',
            (p) => p.uri === uri && (version === undefined || p.version === version),
            timeoutMs,
        )
    }

    uriOf(name) {
        return pathToFileURL(path.join(this.dir(), name)).href
    }

    /** A scratch folder of this client, removed when the server is shut down. */
    dir() {
        if (!this.scratch) {
            // The real path: on Windows the temporary folder may be given in its short form
            // (`RUNNER~1`), which a file URL made here spells `%7E` and one made by the server
            // spells `~`; the tests compare the URIs of files as text.
            const tmp = fs.realpathSync.native(os.tmpdir())
            this.scratch = fs.mkdtempSync(path.join(tmp, 'camel-ls-test-'))
        }
        return this.scratch
    }

    /** Writes a file into the scratch folder (for modules that a document imports). */
    writeFile(name, text) {
        const file = path.join(this.dir(), name)
        fs.mkdirSync(path.dirname(file), { recursive: true })
        fs.writeFileSync(file, text)
        return pathToFileURL(file).href
    }

    async shutdown() {
        if (this.exitCode === undefined && this.exitSignal === undefined) {
            await this.requestRaw('shutdown', null, 5000)
            this.notify('exit', null)
            const timer = setTimeout(() => this.proc.kill(), 5000)
            await this.exited
            clearTimeout(timer)
        }
        if (this.scratch) {
            fs.rmSync(this.scratch, { recursive: true, force: true })
        }
        return { code: this.exitCode, signal: this.exitSignal }
    }
}

/**
 * A source text with markers. `/*name*\/` marks the position of the character after it; the
 * markers are removed from the text. Returns the text and `at(name)`, the LSP position
 * (UTF-16 code units) of a marker.
 */
export function marked(source) {
    const marks = new Map()
    let text = ''
    let last = 0
    const re = /\/\*([A-Za-z0-9_]+)\*\//g
    for (let m; (m = re.exec(source)); ) {
        text += source.slice(last, m.index)
        last = m.index + m[0].length
        marks.set(m[1], text.length)
    }
    text += source.slice(last)
    const positionOf = (offset) => {
        const before = text.slice(0, offset)
        const line = before.split('\n').length - 1
        const character = offset - (before.lastIndexOf('\n') + 1)
        return { line, character }
    }
    return {
        text,
        at(name) {
            if (!marks.has(name)) {
                throw new Error(`no marker ${name}`)
            }
            return positionOf(marks.get(name))
        },
        /** The position of the `nth` (1-based) occurrence of `needle`, plus `delta` units. */
        find(needle, nth = 1, delta = 0) {
            let from = -1
            for (let i = 0; i < nth; i++) {
                from = text.indexOf(needle, from + 1)
                if (from < 0) {
                    throw new Error(`occurrence ${nth} of ${JSON.stringify(needle)} not found`)
                }
            }
            return positionOf(from + delta)
        },
        /** The range of the `nth` occurrence of `needle`. */
        range(needle, nth = 1) {
            return { start: this.find(needle, nth), end: this.find(needle, nth, needle.length) }
        },
    }
}

/** `l:c-l:c` for a range, `l:c` for a position: compact text for assertions. */
export function fmtRange(r) {
    return `${r.start.line}:${r.start.character}-${r.end.line}:${r.end.character}`
}

/** Runs `body` with an initialised server, shuts it down, prints the verdict and exits. */
export async function withServer(body, options = {}) {
    const ls = new LspClient(options)
    let crashed = null
    try {
        if (!options.noInitialize) {
            ls.initializeResult = await ls.initialize(options.initialize)
        }
        await body(ls)
    } catch (e) {
        crashed = e
    }
    const { code, signal } = await ls.shutdown()
    if (crashed) {
        failures++
        console.log(`FAILED: the test threw: ${crashed.stack || crashed}`)
    }
    if (!options.expectAbnormalExit) {
        check(code === 0, 'the server exits with 0 after shutdown/exit', `code ${code}, signal ${signal}`)
    }
    if (failures > 0) {
        if (ls.stderr.trim()) {
            console.log(`server stderr:\n${ls.stderr}`)
        }
        console.log(`${failures} of ${checks} checks failed`)
        process.exit(1)
    }
    console.log(`ok ${checks} checks`)
    process.exit(0)
}
