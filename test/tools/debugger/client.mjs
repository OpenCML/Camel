// Test drivers for camel-db: its HTTP API (the gateway and its workers) and its debug adapter.
//
// No dependencies: the tests run with the Node that runs the test runner. A test file is a
// script of its own:
//
//     import { runTest, check, Gateway, DapClient } from '../../../tools/debugger/client.mjs'
//     await runTest(async (t) => {
//         const dap = await t.dap()            // an initialised adapter, or
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
