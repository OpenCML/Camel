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
import path from 'node:path'
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
 * Whether `{pid, start}` is still the process it was recorded as. A recorded start time that the
 * pid no longer has means the pid was reused: that process is gone.
 */
export function isSameProcess(who) {
    if (!who || !pidExists(who.pid)) return false
    if (!who.start) return true
    const now = startTimeOf(who.pid)
    return now === null ? true : now === who.start
}

/** Ends the process tree of `who` when it is still the recorded process. Returns whether it was. */
export function killRecorded(who) {
    if (!isSameProcess(who)) return false
    killProcessTree({ pid: who.pid })
    return true
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
    for (let attempt = 0; attempt < 3; attempt += 1) {
        try {
            fs.mkdirSync(lockDir)
            writeAtomic(path.join(lockDir, 'owner.json'), JSON.stringify(owner))
            return { ok: true }
        } catch (error) {
            if (error.code !== 'EEXIST') throw error
        }
        const holder = readOwner(lockDir)
        // A lock directory with no owner yet is a holder in the middle of taking it; it is stale
        // only once it has stayed that way for a while.
        let ownerless = false
        if (!holder) {
            const age = Date.now() - fs.statSync(lockDir).mtimeMs
            if (age < 10000) return { ok: false, holder: null }
            ownerless = true
        }
        if (!ownerless && isHolderAlive(holder)) return { ok: false, holder }
        fs.rmSync(lockDir, { recursive: true, force: true })
    }
    return { ok: false, holder: readOwner(lockDir) }
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

    /** Runs a command line (see `splitCommand`) to its end; resolves `{code, signal}`. */
    run(commandLine, { label, cwd, env }) {
        return new Promise((resolve) => {
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
            // Recorded before its start time is read, so that a worker killed in between still
            // leaves a pid for whoever cleans up; the start time is added when it is known.
            this.#record()
            entry.start = startTimeOf(child.pid)
            this.#record()
            child.on('error', (error) => {
                this.live.delete(child.pid)
                this.#record()
                resolve({ code: null, signal: null, error })
            })
            child.on('close', (code, signal) => {
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
 * Ends what a job left running: the worker's tree (when it is still the worker) and every recorded
 * child's tree. Used when the worker could not do it itself. Returns the pids that were ended.
 */
export function killJobProcesses(job) {
    const ended = []
    const targets = [job.worker, ...(Array.isArray(job.children) ? job.children : [])]
    for (const who of targets) {
        if (who && killRecorded(who)) ended.push(who.pid)
    }
    return ended
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
    if (!isSameProcess(job.worker)) {
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
