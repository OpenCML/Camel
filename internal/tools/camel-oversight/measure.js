// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The readings the watch rules are stated in, taken from the repository, the backlog, the build
// panel's state files and the build host. Every git call is asynchronous: a synchronous `git fetch`
// stalls the whole DSH process -- every session, the GUI, the routes -- for as long as the network
// takes.
//
// A reading that cannot be taken is an object `{ unreadable: why }`, never zero and never absent:
// "could not read the tree" and "the tree has nothing" are different facts.

import { execFile } from 'node:child_process'
import { readFile, stat } from 'node:fs/promises'
import path from 'node:path'

// The watch reads the coordinator's own checkout while it merges there. `git status` refreshes the
// index under `index.lock` by default, and a merge that runs into that lock fails with "Unable to write
// index" (and a reader killed mid-refresh leaves the lock behind). A background reader must not take
// optional locks: GIT_OPTIONAL_LOCKS=0 makes every read here lock-free.
const run = (command, args, options = {}) =>
    new Promise((resolve, reject) => {
        execFile(
            command,
            args,
            {
                encoding: 'utf8',
                windowsHide: true,
                maxBuffer: 16 * 1024 * 1024,
                timeout: 120_000,
                ...options,
                env: { ...process.env, GIT_OPTIONAL_LOCKS: '0', ...(options.env || {}) },
            },
            (error, stdout) => (error ? reject(error) : resolve(String(stdout).trim())),
        )
    })

const firstLine = (error) => String(error && error.message ? error.message : error).split('\n')[0]

async function attempt(read) {
    try {
        return await read()
    } catch (error) {
        return { unreadable: firstLine(error) }
    }
}

const PROTECTED = ['develop', 'main', 'release', 'fsh-dev', 'integration']
// The branch gate counts lanes waiting to be merged, and lanes live under lane/. Everything else is
// not a lane waiting: a person's branch (the author picking a release commit on tmp-rel-0246, a
// contributor's xxx-dev), the coordinator's docs branch, a merge seat's own branch, the gate's
// baseline branches and measurement probes. Counting those held the gate shut at 31 while far fewer
// lanes were actually waiting, and the coordinator ran two lanes.
const LANE_BRANCH = /^lane\//

/// Lane branches not yet merged, how far each is ahead and behind, and the worst behind. Merged means
/// into integration once that branch exists (lanes land there; develop follows it on promotion), into
/// develop before. A merge seat's own branch (seat/...) is the merging, not a lane waiting for it.
async function branchesOf(git) {
    const base = (await git(['rev-parse', '--verify', '--quiet', 'origin/integration']).then(() => true, () => false))
        ? 'origin/integration'
        : 'origin/develop'
    const names = (await git(['for-each-ref', '--format=%(refname:short)', 'refs/heads']))
        .split('\n')
        .filter((name) => name && !PROTECTED.includes(name) && LANE_BRANCH.test(name))
    const rows = []
    for (const branch of names) {
        const ahead = Number(await git(['rev-list', '--count', `${base}..${branch}`]).catch(() => 'x'))
        if (!Number.isFinite(ahead) || ahead === 0) continue
        const behind = Number(await git(['rev-list', '--count', '--no-merges', `${branch}..${base}`]).catch(() => 'x'))
        rows.push({ branch, ahead, behind: Number.isFinite(behind) ? behind : null })
    }
    const behinds = rows.map((row) => row.behind).filter((value) => value !== null)
    return { base, unmerged: rows.length, maxBehind: behinds.length ? Math.max(...behinds) : 0, rows }
}

/// Branches already merged into develop and not deleted, local and on origin.
async function mergedOf(git) {
    const merged = (ref) => git(['merge-base', '--is-ancestor', ref, 'origin/develop']).then(() => true, () => false)
    const local = (await git(['for-each-ref', '--format=%(refname:short)', 'refs/heads']))
        .split('\n')
        .filter((name) => name && !PROTECTED.includes(name))
    const remote = (await git(['for-each-ref', '--format=%(refname:short)', 'refs/remotes/origin']))
        .split('\n')
        .map((name) => name.replace(/^origin\//, ''))
        .filter((name) => name && name !== 'HEAD' && !PROTECTED.includes(name))
    const mergedLocal = []
    for (const name of local) if (await merged(name)) mergedLocal.push(name)
    const mergedRemote = []
    for (const name of remote) if (await merged(`origin/${name}`)) mergedRemote.push(name)
    return { mergedLocal, mergedRemote }
}

/// The backlog's rows, by id: status and the stamp of the last update. Read as origin/develop has
/// it, like the rules and the hot-reloaded files: the main checkout is the author's workspace and may
/// stand on another branch, or hold an edit nobody pushed.
async function backlogOf(git) {
    // The coordinator records rows on its docs branch and pushes them between merges; until then that
    // branch is the newer record. Without it, origin/develop.
    const text = await git(['show', 'docs/coordinator:internal/backlog.md']).catch(() => git(['show', 'origin/develop:internal/backlog.md']))
    const rows = new Map()
    const ownRows = new Set()
    for (const line of text.split('\n')) {
        if (!line.trim().startsWith('|')) continue
        // The id is in the first cell, possibly in code or bold marks; a table of any width counts.
        const cells = line.split('|').map((cell) => cell.trim().replace(/[`*]/g, ''))
        // The whole first word is the id: a sub-item such as `D-1123-50④` is its own row and must not
        // overwrite the item it was split from.
        const id = /^([DC]-\d+-\d+\S*)/.exec(cells[1] || '')
        if (!id) continue
        // The documented table has seven columns (status fifth, stamp sixth); a narrower one is read
        // by content: the status is the cell that names one, the stamp the cell that holds one.
        const wide = cells.length >= 9
        const statusCell = wide ? cells[5] : cells.slice(2).find((cell) => /待做|在做|卡住|完成|open|doing|todo|blocked|done|resolved/i.test(cell)) || ''
        const status = statusCell
        const stampCell = wide ? cells[6] : cells.slice(2).find((cell) => /\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}/.test(cell)) || ''
        const stamp = /(\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2})/.exec(stampCell)
        const updated = stamp ? new Date(+stamp[1], +stamp[2] - 1, +stamp[3], +stamp[4], +stamp[5]).getTime() : null
        const row = {
            status,
            doing: /在做|doing|progress/i.test(status),
            blocked: /卡住|blocked/i.test(status),
            done: /完成|done|resolved/i.test(status),
            updated,
        }
        rows.set(id[0], row)
        // A row for a sub-item or a group (`D-1123-11/12/13`) also records the item it belongs to,
        // unless that item has a row of its own, which wins wherever it stands.
        const base = /^[DC]-\d+-\d+/.exec(id[0])[0]
        if (base !== id[0] && !ownRows.has(base)) rows.set(base, row)
        if (base === id[0]) ownRows.add(base)
    }
    return rows
}

/// What the build panel's state files say, with each file's age: a queue that stopped being written
/// and a machine with nothing to do produce the same numbers otherwise.
async function buildPanelOf(repo, stateDir) {
    const dir =
        stateDir ||
        (await readFile(path.join(repo, 'internal', 'tools', 'camel-gate.local.json'), 'utf8')
            .then((text) => JSON.parse(text).stateDir)
            .catch(() => '')) ||
        path.resolve(repo, '..', 'camel-gate-state')
    const runsFile = path.join(dir, 'run-registry.jsonl')
    const info = await stat(runsFile)
    const latest = new Map()
    for (const line of (await readFile(runsFile, 'utf8')).split('\n')) {
        if (!line.trim()) continue
        try {
            const row = JSON.parse(line)
            latest.set(row.runId || row.startedAt || `${latest.size}`, row)
        } catch {
            // a torn line is skipped
        }
    }
    const runs = [...latest.values()]
    // A run with no finish whose record has not been touched for a while is one whose process ended
    // without writing it (a crash, a closed window), not one that is still going: counting it would
    // show a busy host that is idle. The writer refreshes `updatedAt` as it goes.
    const touched = (row) => Date.parse(row.updatedAt || row.startedAt || '') || 0
    const open = runs.filter((row) => !row.finishedAt && Date.now() - touched(row) < 20 * 60_000)
    const abandoned = runs.filter((row) => !row.finishedAt && Date.now() - touched(row) >= 20 * 60_000).length
    // The two queues the build-queue panel draws, read from the same files it reads: who holds the
    // build or test slot on this machine and who is waiting, with how long.
    const queue = async (name) => {
        const file = path.join(dir, name)
        try {
            const doc = JSON.parse(await readFile(file, 'utf8'))
            const holder = doc.holder && typeof doc.holder.worktree === 'string' && doc.holder.worktree.trim()
                ? { worktree: doc.holder.worktree.trim(), phase: doc.holder.phase || '', startedAt: doc.holder.startedAt || null }
                : null
            const waiters = Array.isArray(doc.waiters)
                ? doc.waiters
                      .filter((item) => item && typeof item.worktree === 'string' && item.worktree.trim())
                      .map((item) => ({
                          worktree: item.worktree.trim(),
                          waitedSeconds: item.since ? Math.round((Date.now() - Date.parse(item.since)) / 1000) : null,
                      }))
                : []
            return { holder, waiters }
        } catch (error) {
            return { unreadable: firstLine(error) }
        }
    }
    return {
        ageSeconds: Math.round((Date.now() - info.mtimeMs) / 1000),
        buildQueue: await queue('build-state.json'),
        testQueue: await queue('test-state.json'),
        unfinished: open.length,
        abandoned,
        total: runs.length,
        // Each run still going: where it is, which slot it holds, and how long it has waited for one.
        // A queue that is long, or two runs whose phases overlap, is read here rather than assumed.
        running: open.map((row) => ({
            worktree: row.worktree ?? null,
            phase: row.phase ?? null,
            slot: row.slot ?? null,
            // The width the run's slot granted (see camel-remote.ps1): what a reader compares the
            // host's load against. Absent `jobs` is a run old enough to predate the budget, not a
            // run at width zero.
            jobs: row.jobs ?? null,
            jobsRequested: row.jobsRequested ?? null,
            buildJobs: row.buildJobs ?? null,
            queuedAt: row.queuedAt ?? null,
            queueSeconds: row.queueSeconds ?? (row.queuedAt && !row.buildStartedAt ? Math.round((Date.now() - Date.parse(row.queuedAt)) / 1000) : null),
            startedAt: row.startedAt ?? null,
        })),
    }
}

/// The build host's load over ssh (key-based, no prompt).
async function buildHostOf(host, probe) {
    const out = await run('ssh', ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5', host, probe], { timeout: 15_000 })
    const [loadavg = '', cores = '', jobs = ''] = out.split('\n')
    const [one, five, fifteen] = loadavg.split(' ').map(Number)
    return { load: [one, five, fifteen], cores: Number(cores), testProcesses: Number(jobs) }
}

/// One full reading. `fetch` refreshes origin first; its outcome is part of the reading, because
/// every other number is read from the references it refreshes.
export async function measure({ repo, fetch = true, stateDir = '', buildHost = '', buildProbe = '' }) {
    const git = (args) => run('git', args, { cwd: repo })
    const fetched = fetch ? await attempt(async () => (await git(['fetch', 'origin', '--prune']), 'ok')) : 'skipped'
    return {
        at: Date.now(),
        repo,
        fetched,
        develop: await attempt(async () => ({
            head: await git(['rev-parse', '--short=9', 'origin/develop']),
            ageMinutes: Math.round((Date.now() / 1000 - Number(await git(['log', '-1', '--format=%ct', 'origin/develop']))) / 60),
        })),
        // The integration branch, when there is one: how old its tip is, and how far develop trails it.
        integration: await attempt(async () => {
            const has = await git(['rev-parse', '--verify', '--quiet', 'origin/integration']).then(() => true, () => false)
            if (!has) return { absent: true }
            return {
                head: await git(['rev-parse', '--short=9', 'origin/integration']),
                ageMinutes: Math.round((Date.now() / 1000 - Number(await git(['log', '-1', '--format=%ct', 'origin/integration']))) / 60),
                developBehind: Number(await git(['rev-list', '--count', 'origin/develop..origin/integration'])),
            }
        }),
        fshDev: await attempt(async () => ({
            head: await git(['rev-parse', '--short=9', 'origin/fsh-dev']),
            notOnDevelop: Number(await git(['rev-list', '--count', '--no-merges', 'origin/develop..origin/fsh-dev'])),
        })),
        checkout: await attempt(async () => {
            const porcelain = await git(['status', '--porcelain'])
            return {
                head: await git(['rev-parse', '--abbrev-ref', 'HEAD']),
                dirty: porcelain === '' ? 0 : porcelain.split('\n').length,
                behind: Number(await git(['rev-list', '--count', 'HEAD..origin/develop'])),
            }
        }),
        worktrees: await attempt(async () => (await git(['worktree', 'list'])).split('\n').length - 1),
        // How long the main checkout's index.lock has existed, or null when there is none. A single git
        // command holds it for seconds; minutes means something left it or keeps taking it, and every
        // merge in the checkout fails until it goes.
        indexLock: await stat(path.join(repo, '.git', 'index.lock')).then(
            (info) => ({ ageSeconds: Math.round((Date.now() - info.mtimeMs) / 1000) }),
            (error) => (error && error.code === 'ENOENT' ? null : { unreadable: firstLine(error) }),
        ),
        branches: await attempt(() => branchesOf(git)),
        merged: await attempt(() => mergedOf(git)),
        backlog: await attempt(() => backlogOf(git)),
        panel: await attempt(() => buildPanelOf(repo, stateDir)),
        host: buildHost ? await attempt(() => buildHostOf(buildHost, buildProbe)) : { unconfigured: true },
    }
}
