// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The mechanics of a long job that is never waited on in the foreground.
 *
 * A tool that takes minutes or hours must hand its caller a job id and return at once; the work runs
 * in a detached worker, and the caller reads its progress by polling a directory. Four things keep
 * that from leaving debris behind, and they are the whole of this module:
 *
 *   - a job directory (`job.json`, the worker's log) under one state root, written atomically;
 *   - a worker that is identified by its pid *and its process start time*, so a pid that the system
 *     has handed to some other process is not mistaken for the worker;
 *   - a heartbeat the worker keeps up, so a worker that is alive but wedged is found as surely as
 *     one that is gone;
 *   - one lock per kind of job, held by the worker and reclaimed when its holder is dead, so a crash
 *     never blocks the next job and a live job is never run twice.
 *
 * Every child the worker starts is recorded in `job.json` with its start time, and is ended with its
 * whole process tree (`test/tools/kill-tree.mjs`): by the worker on every way out, and by whoever
 * finds the worker dead or cancels the job, for the case where the worker never got the chance.
 */

import { spawn, spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { DETACH_TREE, killProcessTree } from '../../test/tools/kill-tree.mjs'

const IS_WINDOWS = process.platform === 'win32'

/** Phases after which a job does not change any more. */
export const TERMINAL = new Set(['done', 'failed', 'cancelled', 'dead'])

// ---------------------------------------------------------------- processes

/**
 * When the process `pid` started, as a string that is the same every time it is asked for the same
 * process, or `null` when there is no such process (or the system would not say).
 */
export function startTimeOf(pid) {
    if (!Number.isInteger(pid) || pid <= 0) return null
    const run = IS_WINDOWS
        ? spawnSync(
              'powershell',
              [
                  '-NoProfile',
                  '-NonInteractive',
                  '-Command',
                  `try { (Get-Process -Id ${pid} -ErrorAction Stop).StartTime.ToUniversalTime().ToString('o') } catch { }`,
              ],
              { encoding: 'utf8', windowsHide: true },
          )
        : spawnSync('ps', ['-o', 'lstart=', '-p', String(pid)], {
              encoding: 'utf8',
              env: { ...process.env, LC_ALL: 'C' },
          })
    if (run.error || run.status !== 0) return null
    const text = String(run.stdout || '').trim()
    return text === '' ? null : text
}

/** Whether the process `pid` exists at all (no start time involved). */
export function pidExists(pid) {
    if (!Number.isInteger(pid) || pid <= 0) return false
    try {
        process.kill(pid, 0)
        return true
    } catch (error) {
        return error.code === 'EPERM'
    }
}

/**
 * What `{pid, start}` is now: `'same'` (the pid runs, and its start time is the recorded one),
 * `'other'` (the pid runs but started at another time: it was reused), `'gone'`, or `'unknown'`
 * (the pid runs but the recorded start time was never taken or the system will not say the current
 * one, so nothing tells the two processes apart).
 */
export function identify(who) {
    if (!who || !pidExists(who.pid)) return 'gone'
    if (!who.start) return 'unknown'
    const now = startTimeOf(who.pid)
    if (now === null) return 'unknown'
    return now === who.start ? 'same' : 'other'
}

/** Whether `{pid, start}` is, provably, the process it was recorded as. */
export function isSameProcess(who) {
    return identify(who) === 'same'
}

/**
 * Whether the recorded process may still be running: it is, or it cannot be told from the one that
 * is. For deciding that something is alive; never for deciding to kill it.
 */
export function mayStillRun(who) {
    const state = identify(who)
    return state === 'same' || state === 'unknown'
}

/**
 * Ends the process tree of `who` when it is provably the recorded process. Returns `'killed'`,
 * `'unknown'` (it runs and cannot be told from the recorded one: left alone, for the caller to
 * report) or `'none'` (gone, or a different process).
 */
export function killRecorded(who) {
    const state = identify(who)
    if (state === 'same') {
        killProcessTree({ pid: who.pid })
        return 'killed'
    }
    return state === 'unknown' ? 'unknown' : 'none'
}

const RUN_LIMITED = path.join(path.dirname(fileURLToPath(import.meta.url)), 'run-limited.mjs')

/**
 * Runs a command line to its end for a synchronous caller, with a limit: at `timeoutMs` its whole
 * tree is ended. `shell` runs it through the shell, otherwise it is split by `splitCommand`.
 * Returns `{code, timedOut}`; `code` is null when there is none.
 */
export function runLimitedSync(commandLine, { timeoutMs, cwd, shell = false, stdio = 'inherit' }) {
    const resultFile = path.join(os.tmpdir(), `run-limited-${process.pid}-${Date.now()}.json`)
    const args = [RUN_LIMITED, '--timeout-ms', String(timeoutMs), '--result', resultFile]
    if (shell) args.push('--shell')
    if (cwd) args.push('--cwd', cwd)
    args.push('--', commandLine)
    // The outer limit is only the backstop for the runner itself.
    const outer = spawnSync(process.execPath, args, { stdio, windowsHide: true, timeout: timeoutMs + 60000 })
    let result = null
    try {
        result = JSON.parse(fs.readFileSync(resultFile, 'utf8'))
    } catch {
        /* the runner died before it wrote its result */
    }
    fs.rmSync(resultFile, { force: true })
    if (!result) return { code: null, timedOut: outer.error?.code === 'ETIMEDOUT' || outer.signal !== null }
    return { code: result.code, timedOut: result.timedOut === true }
}

// ---------------------------------------------------------------- job directory

export function jobDirOf(root, id) {
    return path.join(root, id)
}

export function readJob(dir) {
    for (let attempt = 0; attempt < 20; attempt += 1) {
        try {
            return JSON.parse(fs.readFileSync(path.join(dir, 'job.json'), 'utf8'))
        } catch (error) {
            // A file that does not exist is not a job; one that is being replaced is read again.
            if (error.code === 'ENOENT' && attempt >= 2) return null
            pause(25)
        }
    }
    return null
}

function pause(ms) {
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms)
}

// Windows refuses a rename onto a file somebody has open for reading, for as long as they have it;
// the readers here hold it for a moment, so the rename is retried briefly.
function writeAtomic(file, text) {
    const temp = `${file}.${process.pid}.tmp`
    fs.writeFileSync(temp, text)
    for (let attempt = 0; ; attempt += 1) {
        try {
            fs.renameSync(temp, file)
            return
        } catch (error) {
            if (attempt >= 40 || !['EPERM', 'EBUSY', 'EACCES'].includes(error.code)) {
                fs.rmSync(temp, { force: true })
                throw error
            }
            pause(25)
        }
    }
}

/**
 * Merges `patch` into `job.json` and stamps `updatedAt`. A job that already ended is not changed
 * (a late heartbeat must not undo "dead" or "cancelled") unless `force` says so.
 */
export function updateJob(dir, patch, { force = false } = {}) {
    const job = readJob(dir)
    if (!job) return null
    if (!force && TERMINAL.has(job.phase) && patch.phase !== job.phase) return job
    const next = { ...job, ...patch, updatedAt: new Date().toISOString() }
    writeAtomic(path.join(dir, 'job.json'), JSON.stringify(next, null, 2))
    return next
}

export function createJob(root, id, fields) {
    const dir = jobDirOf(root, id)
    fs.mkdirSync(dir, { recursive: true })
    const now = new Date().toISOString()
    const job = { id, startedAt: now, updatedAt: now, ...fields }
    writeAtomic(path.join(dir, 'job.json'), JSON.stringify(job, null, 2))
    return { dir, job }
}

/** The job ids under `root`, oldest first (ids start with a sortable timestamp). */
export function listJobs(root) {
    let names = []
    try {
        names = fs.readdirSync(root, { withFileTypes: true }).filter((e) => e.isDirectory()).map((e) => e.name)
    } catch {
        return []
    }
    return names.filter((name) => readJob(path.join(root, name))).sort()
}

export function appendLog(dir, line) {
    try {
        fs.appendFileSync(path.join(dir, 'worker.log'), `${new Date().toISOString()} ${line}\n`)
    } catch {
        /* a log that cannot be written must not end the job */
    }
}

export function tailLog(dir, count) {
    try {
        const lines = fs.readFileSync(path.join(dir, 'worker.log'), 'utf8').split(/\r?\n/).filter(Boolean)
        return lines.slice(-count)
    } catch {
        return []
    }
}

// ---------------------------------------------------------------- the lock

function readOwner(lockDir) {
    try {
        return JSON.parse(fs.readFileSync(path.join(lockDir, 'owner.json'), 'utf8'))
    } catch {
        return null
    }
}

/**
 * Takes the lock `lockDir` for `owner` (`{jobId, pid, start}`). A lock whose holder is gone is
 * reclaimed and never waited on: `isHolderAlive(owner)` is the caller's reading of "is that holder
 * still running", because only the caller knows what a holder is. Returns `{ok: true}` or
 * `{ok: false, holder}`.
 */
export function acquireLock(lockDir, owner, isHolderAlive) {
    fs.mkdirSync(path.dirname(lockDir), { recursive: true })
    for (let attempt = 0; attempt < 60; attempt += 1) {
        try {
            fs.mkdirSync(lockDir)
            writeAtomic(path.join(lockDir, 'owner.json'), JSON.stringify(owner))
            return { ok: true }
        } catch (error) {
            if (error.code !== 'EEXIST') throw error
        }
        const stale = staleHolder(lockDir, isHolderAlive)
        if (stale.vanished) continue
        if (!stale.stale) return { ok: false, holder: stale.holder }
        // Two contenders can both have read the same dead holder. Whichever removes the lock must
        // not remove one the other has since taken, so removing is done under a claim that only one
        // contender holds, and the holder is judged again once the claim is held.
        const claim = `${lockDir}.reclaim`
        try {
            fs.mkdirSync(claim)
        } catch (error) {
            if (error.code !== 'EEXIST') throw error
            // A claim left by a contender that died is not waited on for ever.
            try {
                if (Date.now() - fs.statSync(claim).mtimeMs > 30000) fs.rmSync(claim, { recursive: true, force: true })
            } catch {
                /* gone already */
            }
            pause(50)
            continue
        }
        try {
            if (staleHolder(lockDir, isHolderAlive).stale) {
                const aside = `${lockDir}.stale-${process.pid}-${Date.now()}`
                try {
                    fs.renameSync(lockDir, aside)
                    fs.rmSync(aside, { recursive: true, force: true })
                } catch (error) {
                    if (error.code !== 'ENOENT') throw error
                }
            }
        } finally {
            fs.rmSync(claim, { recursive: true, force: true })
        }
    }
    return { ok: false, holder: readOwner(lockDir) }
}

/** `{stale: true}` for a lock whose holder is gone, otherwise `{stale: false, holder}`. */
function staleHolder(lockDir, isHolderAlive) {
    const holder = readOwner(lockDir)
    // A lock directory with no owner yet is a holder in the middle of taking it; it is stale only
    // once it has stayed that way for a while.
    if (!holder) {
        let age
        try {
            age = Date.now() - fs.statSync(lockDir).mtimeMs
        } catch {
            return { stale: false, holder: null, vanished: true } // removed while looking
        }
        return age < 10000 ? { stale: false, holder: null } : { stale: true }
    }
    return isHolderAlive(holder) ? { stale: false, holder } : { stale: true }
}

export function lockOwner(lockDir) {
    return readOwner(lockDir)
}

export function reassignLock(lockDir, owner) {
    writeAtomic(path.join(lockDir, 'owner.json'), JSON.stringify(owner))
}

/** Releases the lock when `jobId` holds it (a lock another job took over is left alone). */
export function releaseLock(lockDir, jobId) {
    const holder = readOwner(lockDir)
    if (holder && holder.jobId !== jobId) return false
    fs.rmSync(lockDir, { recursive: true, force: true })
    return true
}

// ---------------------------------------------------------------- the worker and its children

/**
 * Starts `node <args>` detached, with its output in `logFile`, and returns its pid. The caller can
 * exit at once; nothing here waits for the worker.
 */
export function startDetached(args, { logFile, cwd, env }) {
    const fd = fs.openSync(logFile, 'a')
    const child = spawn(process.execPath, args, {
        cwd,
        env: env ?? process.env,
        detached: true,
        stdio: ['ignore', fd, fd],
        windowsHide: true,
    })
    child.unref()
    fs.closeSync(fd)
    return child.pid
}

/**
 * Splits a command line into the program and its arguments: words are separated by spaces, and a
 * double-quoted run is one word (no escapes, no shell syntax). A command is started directly and not
 * through a shell, because the shell is the one process of the chain that a worker killed without
 * warning takes with it, and the pid recorded for the child has to be the program's own.
 */
export function splitCommand(line) {
    const words = []
    const pattern = /"([^"]*)"|([^\s"]+)/g
    let match
    let current = ''
    let last = 0
    // Adjacent pieces with no space between them (--x="a b") are one word.
    while ((match = pattern.exec(line)) !== null) {
        if (match.index > last && /\s/.test(line.slice(last, match.index))) {
            if (current !== '') words.push(current)
            current = ''
        }
        current += match[1] ?? match[2]
        last = match.index + match[0].length
    }
    if (current !== '') words.push(current)
    return words
}

/**
 * The children one worker has running. Each is started so that its tree can be ended, recorded in
 * `job.json` the moment it exists, and removed from the record when it finishes.
 */
export class Children {
    constructor(dir) {
        this.dir = dir
        this.live = new Map()
    }

    #record() {
        updateJob(this.dir, {
            children: [...this.live.values()].map((c) => ({ pid: c.pid, start: c.start, label: c.label })),
        })
    }

    /**
     * Runs a command line (see `splitCommand`) to its end; resolves `{code, signal}`. A command still
     * running after `limitSeconds` is ended with its whole tree and the call rejects, naming the
     * label and the limit: the worker's heartbeat says nothing about a child, so only this limit
     * stops one that hangs.
     */
    run(commandLine, { label, cwd, env, limitSeconds }) {
        return new Promise((resolve, reject) => {
            const fd = fs.openSync(path.join(this.dir, 'worker.log'), 'a')
            fs.writeSync(fd, `${new Date().toISOString()} [${label}] ${commandLine}\n`)
            const [program, ...args] = splitCommand(commandLine)
            const child = spawn(program, args, {
                cwd,
                env: env ?? process.env,
                detached: DETACH_TREE,
                stdio: ['ignore', fd, fd],
                windowsHide: true,
            })
            fs.closeSync(fd)
            const entry = { pid: child.pid, start: null, label, child }
            this.live.set(child.pid, entry)
            // Nothing kills by this pid before its start time is on the record: a record without
            // one cannot tell the child from a process that later has the same pid. (The worker
            // itself ends its children through the handle it holds, not through the record.)
            for (let attempt = 0; attempt < 3 && entry.start === null && pidExists(child.pid); attempt += 1) {
                entry.start = startTimeOf(child.pid)
            }
            this.#record()
            let timer = null
            if (limitSeconds) {
                timer = setTimeout(() => {
                    killProcessTree(child)
                    this.live.delete(child.pid)
                    this.#record()
                    reject(new Error(`the ${label} step ran past its limit of ${limitSeconds} s and its process tree was ended`))
                }, limitSeconds * 1000)
            }
            child.on('error', (error) => {
                clearTimeout(timer)
                this.live.delete(child.pid)
                this.#record()
                resolve({ code: null, signal: null, error })
            })
            child.on('close', (code, signal) => {
                clearTimeout(timer)
                this.live.delete(child.pid)
                this.#record()
                resolve({ code, signal })
            })
        })
    }

    /** Ends every child that is still running, with its whole tree. Safe to call from an exit handler. */
    killAll() {
        for (const entry of this.live.values()) killProcessTree(entry.child)
        this.live.clear()
    }
}

/**
 * Ends what a job left running: the worker's tree (when it is provably still the worker) and every
 * recorded child's tree. Used when the worker could not do it itself. Returns `{ended, unknown}`:
 * the pids that were ended, and the pids that still run but cannot be told from the recorded process
 * (no start time was recorded, or none can be read now), which are left alone.
 */
export function killJobProcesses(job) {
    const ended = []
    const unknown = []
    const targets = [job.worker, ...(Array.isArray(job.children) ? job.children : [])]
    for (const who of targets) {
        if (!who) continue
        const outcome = killRecorded(who)
        if (outcome === 'killed') ended.push(who.pid)
        else if (outcome === 'unknown') unknown.push(who.pid)
    }
    return { ended, unknown }
}

/**
 * What `status` and `start` need to know about a job: `{alive: true}` or `{alive: false, reason}`.
 * A worker that is gone, or is a different process than the one recorded, is dead; so is one whose
 * `updatedAt` is older than `limitSeconds` (a wedged worker holds the lock as surely as a running
 * one). A job that ended by itself is not alive and says so with `ended: true`.
 */
export function liveness(job, limitSeconds) {
    if (TERMINAL.has(job.phase)) return { alive: false, reason: `ended (${job.phase})`, ended: true }
    if (!job.worker) {
        const age = (Date.now() - Date.parse(job.startedAt)) / 1000
        return age > 30 ? { alive: false, reason: 'the worker never started' } : { alive: true }
    }
    if (!mayStillRun(job.worker)) {
        return { alive: false, reason: `the worker (pid ${job.worker.pid}) is gone` }
    }
    const silent = (Date.now() - Date.parse(job.updatedAt)) / 1000
    if (silent > limitSeconds) {
        return { alive: false, reason: `no heartbeat for ${Math.round(silent)} s (limit ${limitSeconds} s)`, wedged: true }
    }
    return { alive: true }
}

/** Marks a job as ended (`dead` or `cancelled`) and records why. */
export function markEnded(dir, phase, why) {
    return updateJob(dir, { phase, endedAt: new Date().toISOString(), endedBecause: why, children: [] }, { force: true })
}
