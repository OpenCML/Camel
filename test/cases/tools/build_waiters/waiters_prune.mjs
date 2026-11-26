#!/usr/bin/env node
/**
 * The waiters list of `build-state.json` shows only processes that exist.
 *
 * The list is written by the process that waits and removed by it, but a process that is killed
 * skips every `finally`: the entry stayed, and a panel showed a wait of more than a day for a process
 * that no longer existed. Every touch of the file now passes the list through one function,
 * `internal/tools/camel-gate-state.ps1#Select-LiveCamelWaiters`. Two checks:
 *
 *   1. the function, given one waiter that is this test's own process, one whose pid does not exist,
 *      and one whose pid exists but whose process started after the entry's `since` (a reused pid),
 *      keeps only the first;
 *   2. a real `camel-build.ps1` waiting for a lock a helper holds is killed (its `finally` never
 *      runs, and the entry is still in the file -- the premise), and the next `camel-build.ps1` to
 *      touch the file leaves no entry for the dead pid.
 *
 * The builds here name their own lock (`-LockScope`) and their own state directory, so no real build
 * on the machine is made to wait.
 *
 * Usage: node waiters_prune.mjs
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import url from 'node:url'
import { spawn, spawnSync } from 'node:child_process'

const here = path.dirname(url.fileURLToPath(import.meta.url))
const tools = path.resolve(here, '..', '..', '..', '..', 'internal', 'tools')
const repo = path.resolve(tools, '..', '..')
const powershell = 'powershell.exe'
const base = ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass']

const failures = []
const check = (ok, what) => {
    if (!ok) failures.push(what)
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`)
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'build-waiters-'))
const scope = `Test${process.pid}`
const children = []

// A pid that does not exist: a process that was started and has ended.
const deadPid = () => {
    const child = spawnSync(process.execPath, ['-e', ''])
    return child.pid
}

function pruneUnit() {
    const live = { worktree: 'live', pid: process.pid, since: new Date().toISOString() }
    const dead = { worktree: 'dead', pid: deadPid(), since: new Date().toISOString() }
    // This test's own pid, with a `since` from before the process existed: not the process that wrote it.
    const reused = { worktree: 'reused', pid: process.pid, since: new Date(Date.now() - 30 * 86400_000).toISOString() }
    const file = path.join(dir, 'unit.json')
    fs.writeFileSync(file, JSON.stringify({ waiters: [live, dead, reused] }))
    const script = [
        `. '${path.join(tools, 'camel-gate-state.ps1')}'`,
        `$doc = Get-Content -Raw -LiteralPath '${file}' | ConvertFrom-Json`,
        `$kept = @(Select-LiveCamelWaiters $doc.waiters)`,
        `Write-Output ('KEPT=' + (($kept | ForEach-Object { $_.worktree }) -join ','))`,
    ].join('; ')
    const run = spawnSync(powershell, [...base, '-Command', script], { encoding: 'utf8' })
    const line = (run.stdout || '').split(/\r?\n/).find((l) => l.startsWith('KEPT='))
    check(line === 'KEPT=live', `pruning keeps the live waiter only (read: ${line ?? run.stderr})`)
}

const waitersOf = (file) => {
    try {
        return JSON.parse(fs.readFileSync(file, 'utf8').replace(/^﻿/, '')).waiters ?? []
    } catch {
        return []
    }
}

async function killedWaiter() {
    const state = path.join(dir, 'build-state.json')
    const holderReady = path.join(dir, 'holder.ready')
    const holder = spawn(
        powershell,
        [
            ...base,
            '-Command',
            `$m = New-Object System.Threading.Mutex($false, 'Global\\CamelBuildSerialized${scope}'); ` +
                `[void]$m.WaitOne(); Set-Content -Path '${holderReady}' -Value held; Start-Sleep -Seconds 120`,
        ],
        { stdio: 'ignore' },
    )
    children.push(holder)
    for (let i = 0; i < 100 && !fs.existsSync(holderReady); i++) await sleep(100)
    check(fs.existsSync(holderReady), 'the helper holds the build lock')

    const build = (extra = []) =>
        [...base, '-File', path.join(tools, 'camel-build.ps1'), '-Path', repo, '-StateDir', dir, '-LockScope', scope, '-Check', '1', ...extra]
    const waiter = spawn(powershell, build(), { stdio: 'ignore' })
    children.push(waiter)
    let listed = false
    for (let i = 0; i < 200 && !listed; i++) {
        listed = waitersOf(state).some((w) => w.pid === waiter.pid)
        if (!listed) await sleep(100)
    }
    check(listed, 'the waiting build is listed in waiters')

    spawnSync('taskkill', ['/F', '/T', '/PID', String(waiter.pid)], { stdio: 'ignore' })
    await sleep(500)
    check(
        waitersOf(state).some((w) => w.pid === waiter.pid),
        'premise: a killed waiter skipped its cleanup and is still in the file',
    )

    spawnSync('taskkill', ['/F', '/T', '/PID', String(holder.pid)], { stdio: 'ignore' })
    await sleep(500)
    const next = spawnSync(powershell, build(), { encoding: 'utf8', timeout: 120000 })
    check(next.status === 0, `the next build touches the file and exits 0 (exit ${next.status})`)
    check(
        !waitersOf(state).some((w) => w.pid === waiter.pid),
        'after that touch the dead waiter is gone',
    )
}

try {
    pruneUnit()
    await killedWaiter()
} finally {
    for (const child of children) {
        try {
            spawnSync('taskkill', ['/F', '/T', '/PID', String(child.pid)], { stdio: 'ignore' })
        } catch {}
    }
    fs.rmSync(dir, { recursive: true, force: true })
}

if (failures.length) {
    console.log(`FAIL ${failures.length} check(s)`)
    process.exit(1)
}
console.log('PASS')
