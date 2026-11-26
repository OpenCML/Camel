#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A promotion as one job that is started, polled and never waited on.
 *
 * `integration` takes lane branches optimistically and only builds them. A promotion is the safety
 * net behind that: the whole fast layer measured on the integration tip, every timeout re-run on
 * its own with a doubled limit, the reds compared by plan id and case name with the immediately
 * previous promotion's red table, the three records written (red table, per-case durations, the
 * promotion record the push hook asks for), and, when asked and no red is new, `develop`
 * fast-forwarded to exactly the commit that was measured.
 *
 * The caller never holds a foreground wait. `start` takes the one promotion lock, writes a job
 * directory, launches a detached worker and returns; `status` reads the directory (and finds a
 * dead or wedged worker, ends what it left behind and releases the lock); `cancel` ends the
 * worker's whole process tree and cleans up. The job mechanics are in `scripts/lib/job.mjs`.
 *
 * Usage:
 *   node scripts/promote.mjs start [--tip <ref>] [--publish] [options]
 *   node scripts/promote.mjs status [<id>] [--lines <n>] [--json]
 *   node scripts/promote.mjs cancel [<id>]
 *   node scripts/promote.mjs run --job <id> --state-dir <dir>        (what `start` launches)
 *
 * Options of `start` (kept in `job.json`, so the other subcommands need none of them):
 *   --tip <ref>             what to measure; default origin/integration, else integration
 *   --publish               fast-forward develop to the measured commit when no red is new
 *   --repo <path>           the repository (default: the one this script is in)
 *   --state-dir <dir>       where jobs live; default `stateDir` of internal/tools/camel-gate.local.json
 *   --layer <command>       runs the whole fast layer in {worktree}; must leave {worktree}/summary.json
 *   --rerun <command>       runs one plan ({plan}) in {worktree} with the timeouts doubled (the
 *                           environment has CAMEL_TEST_TIMEOUT_SCALE=2); leaves the same summary.json
 *   --cleanup <command>     drops the remote run of {worktree}; used when a job ends abnormally
 *   --publish-command <c>   publishes {sha} to develop from {worktree}
 *   --publish-worktree <p>  the tree the publish runs in (default: mergeWorktree of the local settings)
 *   --remote <name>         default origin;  --develop <branch> default develop
 *   --reports <dir>         where the promotion records live in the repository (default internal/reports)
 *   --reports-ref <ref>     the tree the previous promotion is read from (default: the measured tip)
 *   --jobs <n>              passed to the layer command as {jobs} (default 8)
 *   --heartbeat-limit <s>   a worker silent for longer than this is dead (default 180)
 *   --heartbeat-interval <s>  how often the worker beats (default 20)
 *   --no-fetch              do not fetch develop before publishing
 *   --no-verify-publish     do not read the remote's develop back after the publish command
 *
 * `summary.json` is the plan runner's own file: `test_timings[]` rows with `plan`, `plan_path`,
 * `test`, `status`, `timed_out`, `child_ms` and `slot_ms`.
 *
 * Exit codes: `start` 0 started, 2 could not start, 3 a live promotion holds the lock; `status` 0, 2
 * no such job; the worker 0 no new red, 1 new reds, 2 an error.
 */

import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import {
    Children,
    TERMINAL,
    acquireLock,
    appendLog,
    createJob,
    jobDirOf,
    killJobProcesses,
    liveness,
    listJobs,
    lockOwner,
    isSameProcess,
    markEnded,
    readJob,
    reassignLock,
    releaseLock,
    startDetached,
    splitCommand,
    startTimeOf,
    tailLog,
    updateJob,
} from './lib/job.mjs'

const SCRIPT = fileURLToPath(import.meta.url)
const SCRIPT_REPO = path.resolve(path.dirname(SCRIPT), '..')

const DEFAULT_LAYER =
    'pwsh -NoProfile -File "{worktree}/internal/tools/camel-remote.ps1" -Path "{worktree}" -Plan test/plans -Tier fast -Jobs {jobs} -FetchFile summary.json -Latest'
const DEFAULT_RERUN =
    'pwsh -NoProfile -File "{worktree}/internal/tools/camel-remote.ps1" -Path "{worktree}" -Plan "{plan}" -Tier fast -Jobs 4 -FetchFile summary.json -Latest'
const DEFAULT_CLEANUP = 'pwsh -NoProfile -File "{worktree}/internal/tools/camel-remote.ps1" -Path "{worktree}" -Clean'
const DEFAULT_PUBLISH = 'git -C "{worktree}" push {remote} {sha}:refs/heads/{develop}'

function say(line) {
    process.stdout.write(`${line}\n`)
}

function fail(code, why) {
    process.stderr.write(`promote: ${why}\n`)
    process.exit(code)
}

// ---------------------------------------------------------------- arguments and settings

function parseArgs(argv) {
    const out = { positional: [], flags: {} }
    const valued = new Set([
        'tip', 'repo', 'state-dir', 'layer', 'rerun', 'cleanup', 'publish-command', 'publish-worktree', 'remote',
        'develop', 'reports', 'reports-ref', 'jobs', 'heartbeat-limit', 'heartbeat-interval', 'lines', 'job',
    ])
    const bare = new Set(['publish', 'no-fetch', 'no-verify-publish', 'json'])
    for (let i = 0; i < argv.length; i += 1) {
        const word = argv[i]
        if (!word.startsWith('--')) {
            out.positional.push(word)
            continue
        }
        const name = word.slice(2)
        if (bare.has(name)) out.flags[name] = true
        else if (valued.has(name)) {
            if (i + 1 >= argv.length) fail(2, `--${name} needs a value`)
            i += 1
            out.flags[name] = argv[i]
        } else fail(2, `unknown argument '${word}'`)
    }
    return out
}

function git(repo, args, { allowFailure = false } = {}) {
    const run = spawnSync('git', ['-C', repo, ...args], { encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 })
    if (run.status !== 0) {
        if (allowFailure) return null
        throw new Error(`git ${args.join(' ')} failed: ${(run.stderr || run.error?.message || '').trim()}`)
    }
    return run.stdout.replace(/\n+$/, '')
}

function localSettings(repo) {
    try {
        return JSON.parse(fs.readFileSync(path.join(repo, 'internal', 'tools', 'camel-gate.local.json'), 'utf8'))
    } catch {
        return {}
    }
}

function stateRootOf(flags, repo) {
    const dir = flags['state-dir'] || localSettings(repo).stateDir
    if (!dir || String(dir).startsWith('<')) {
        fail(2, 'no state directory: pass --state-dir, or set `stateDir` in internal/tools/camel-gate.local.json')
    }
    return path.join(path.resolve(dir), 'promotion')
}

function fill(template, values) {
    return template.replace(/\{(\w+)\}/g, (whole, key) => (key in values ? String(values[key]) : whole))
}

// ---------------------------------------------------------------- liveness, reaping, cleanup

/** Runs the job's remote cleanup command, bounded, and logs what it said. */
function cleanupRemote(dir, job) {
    const command = job.config?.cleanup
    if (!command) return
    const line = fill(command, { worktree: job.paths.tree })
    appendLog(dir, `[cleanup] ${line}`)
    const [program, ...args] = splitCommand(line)
    const run = spawnSync(program, args, { encoding: 'utf8', timeout: 180000, windowsHide: true })
    appendLog(dir, `[cleanup] exit ${run.status}${run.error ? ` (${run.error.message})` : ''}`)
}

/**
 * Ends everything a job left behind: its processes, its remote run, its lock. Idempotent. The job's
 * phase is set by the caller.
 */
function cleanLeftovers(root, dir, job) {
    const ended = killJobProcesses(job)
    if (ended.length) appendLog(dir, `[cleanup] ended process trees of pid ${ended.join(', ')}`)
    cleanupRemote(dir, job)
    releaseLock(path.join(root, 'lock'), job.id)
}

/**
 * Reads a job and, when its worker is dead or wedged without a final phase, marks it dead and cleans
 * up. Returns the job as it stands afterwards.
 */
function reap(root, id) {
    const dir = jobDirOf(root, id)
    const job = readJob(dir)
    if (!job) return null
    const state = liveness(job, Number(job.config?.heartbeatLimit ?? 180))
    if (state.alive) return job
    if (!state.ended) {
        cleanLeftovers(root, dir, job)
        appendLog(dir, `[status] marked dead: ${state.reason}`)
        return markEnded(dir, 'dead', state.reason)
    }
    // A job that ended by itself but whose lock is still there (the worker died between the two).
    if (lockOwner(path.join(root, 'lock'))?.jobId === job.id) releaseLock(path.join(root, 'lock'), job.id)
    return job
}

function holderAlive(root) {
    return (holder) => {
        if (!holder?.jobId) return false
        const job = reap(root, holder.jobId)
        if (job) return !TERMINAL.has(job.phase)
        // No job directory yet: the holder is the `start` that is still setting the job up.
        return isSameProcess(holder)
    }
}

// ---------------------------------------------------------------- start

function doStart(flags) {
    const repo = path.resolve(flags.repo || SCRIPT_REPO)
    const root = stateRootOf(flags, repo)
    const remote = flags.remote || 'origin'
    const develop = flags.develop || 'develop'

    let tip = null
    const refs = flags.tip ? [flags.tip] : [`${remote}/integration`, 'integration']
    for (const ref of refs) {
        tip = git(repo, ['rev-parse', '--verify', `${ref}^{commit}`], { allowFailure: true })
        if (tip) break
    }
    if (!tip) fail(2, `the tip ${refs.join(' / ')} does not resolve to a commit in ${repo}`)

    const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-')
    const id = `${stamp}-${tip.slice(0, 8)}`
    const mine = { jobId: id, pid: process.pid, start: startTimeOf(process.pid) }
    const lockDir = path.join(root, 'lock')
    const got = acquireLock(lockDir, mine, holderAlive(root))
    if (!got.ok) {
        const holder = got.holder?.jobId
        fail(3, `a promotion is running (${holder ?? 'its lock is being taken'}): node scripts/promote.mjs status ${holder ?? ''}`.trim())
    }

    const dir = jobDirOf(root, id)
    try {
        const config = {
            repo,
            remote,
            develop,
            publish: flags.publish === true,
            layer: flags.layer || DEFAULT_LAYER,
            rerun: flags.rerun || DEFAULT_RERUN,
            cleanup: flags.cleanup === undefined ? DEFAULT_CLEANUP : flags.cleanup,
            publishCommand: flags['publish-command'] || DEFAULT_PUBLISH,
            publishWorktree: flags['publish-worktree'] || null,
            reports: flags.reports || 'internal/reports',
            reportsRef: flags['reports-ref'] || null,
            jobs: Number(flags.jobs || 8),
            heartbeatLimit: Number(flags['heartbeat-limit'] || 180),
            heartbeatInterval: Number(flags['heartbeat-interval'] || 20),
            fetch: flags['no-fetch'] !== true,
            verifyPublish: flags['no-verify-publish'] !== true,
        }
        createJob(root, id, {
            kind: 'promotion',
            tip,
            tipRef: flags.tip || null,
            phase: 'queued',
            worker: null,
            children: [],
            config,
            paths: { root, dir, tree: path.join(root, 'tree') },
        })
        const pid = startDetached([SCRIPT, 'run', '--job', id, '--state-dir', path.dirname(root)], {
            logFile: path.join(dir, 'worker.log'),
            cwd: repo,
        })
        const worker = { pid, start: startTimeOf(pid) }
        updateJob(dir, { worker })
        reassignLock(lockDir, { jobId: id, ...worker })
    } catch (error) {
        releaseLock(lockDir, id)
        fail(2, `could not start: ${error.message}`)
    }
    say(`promotion job ${id} started on ${tip.slice(0, 12)}${flags.publish ? ' (will publish develop)' : ''}`)
    say(`poll:   node scripts/promote.mjs status ${id}`)
    say(`cancel: node scripts/promote.mjs cancel ${id}`)
    say(`log:    ${path.join(dir, 'worker.log')}`)
}

// ---------------------------------------------------------------- status and cancel

function pickJob(root, wanted) {
    const ids = listJobs(root)
    const id = wanted || ids[ids.length - 1]
    if (!id || !readJob(jobDirOf(root, id))) fail(2, wanted ? `no job '${wanted}' under ${root}` : `no promotion job under ${root}`)
    return id
}

function elapsedOf(job) {
    const end = TERMINAL.has(job.phase) ? Date.parse(job.endedAt || job.updatedAt) : Date.now()
    const seconds = Math.max(0, Math.round((end - Date.parse(job.startedAt)) / 1000))
    return `${Math.floor(seconds / 60)}m${String(seconds % 60).padStart(2, '0')}s`
}

function doStatus(flags, positional) {
    const repo = path.resolve(flags.repo || SCRIPT_REPO)
    const root = stateRootOf(flags, repo)
    const id = pickJob(root, positional[0])
    const job = reap(root, id)
    const dir = jobDirOf(root, id)
    if (flags.json) {
        say(JSON.stringify(job, null, 2))
        return
    }
    say(`job ${job.id}  tip ${job.tip.slice(0, 12)}  phase ${job.phase}  elapsed ${elapsedOf(job)}`)
    if (job.phase === 'dead') say(`dead: ${job.endedBecause}`)
    if (job.phase === 'cancelled') say(`cancelled: ${job.endedBecause}`)
    if (job.phase === 'failed') say(`failed: ${job.error}`)
    if (job.verdict) {
        say(`verdict: ${job.verdict}`)
        if (job.promotion) say(`promotion ${job.promotion}, compared with ${job.comparedWith}`)
        for (const red of job.newReds || []) say(`  new red: ${red}`)
        if ((job.newReds || []).length) {
            say('merges in range (bisect these):')
            for (const merge of job.mergesInRange || []) say(`  ${merge}`)
        }
        if (job.files) for (const file of job.files) say(`  wrote ${file}`)
        if (job.published) say(`published ${job.published.sha.slice(0, 12)} to ${job.config.remote}/${job.config.develop}`)
    }
    for (const line of tailLog(dir, Number(flags.lines || 8))) say(`  | ${line}`)
}

function doCancel(flags, positional) {
    const repo = path.resolve(flags.repo || SCRIPT_REPO)
    const root = stateRootOf(flags, repo)
    const id = pickJob(root, positional[0])
    const dir = jobDirOf(root, id)
    const job = readJob(dir)
    if (TERMINAL.has(job.phase)) {
        say(`job ${id} already ended (${job.phase}); nothing to cancel`)
        releaseLock(path.join(root, 'lock'), id)
        return
    }
    // Mark first: the worker's own exit handler must not write over it, and a heartbeat that lands
    // while the trees are being ended must not look like life.
    markEnded(dir, 'cancelled', 'cancelled by the caller')
    cleanLeftovers(root, dir, job)
    say(`job ${id} cancelled; its processes (${[job.worker, ...(job.children || [])].filter(Boolean).map((p) => p.pid).join(', ')}) were ended and the lock released`)
}

// ---------------------------------------------------------------- the worker

const KEY = (plan, test) => `${plan}\t${test}`

function parseRedTable(text) {
    const names = new Set()
    for (const raw of text.split(/\r?\n/)) {
        const line = raw.trimEnd()
        if (!line || line.startsWith('#')) continue
        const cells = line.split('\t')
        if (cells.length < 2) continue
        names.add(KEY(cells[0], cells[1]))
    }
    return names
}

/** The previous promotion, read from `ref`: its number, red table and (when it has one) record. */
function previousPromotion(repo, ref, reports) {
    const listing = git(repo, ['ls-tree', '-r', '--name-only', ref, '--', reports], { allowFailure: true })
    if (listing === null) throw new Error(`the tree ${ref} could not be listed`)
    const tables = new Map()
    const records = new Map()
    for (const file of listing.split('\n').map((l) => l.trim()).filter(Boolean)) {
        const base = path.posix.basename(file)
        let m = /^\d{4}-\d{2}-\d{2}-promotion-(\d+)-reds\.tsv$/.exec(base)
        if (m) tables.set(Number(m[1]), { file, base })
        m = /^\d{4}-\d{2}-\d{2}-promotion-(\d+)\.record\.json$/.exec(base)
        if (m) records.set(Number(m[1]), { file, base })
    }
    const highest = (map) => (map.size ? Math.max(...map.keys()) : 0)
    const n = highest(tables)
    const recorded = highest(records)
    if (recorded > n) {
        // The comparison has to be with the promotion immediately before this one: a table of an
        // earlier promotion would call every red of the one in between "new" or hide it.
        throw new Error(
            `promotion ${recorded} has a record but no red table, so the immediately previous promotion cannot be compared against; ` +
                `write its table first (the newest table is promotion ${n})`,
        )
    }
    if (n === 0) return { n: 0, names: new Set(), table: null, tip: null }
    const table = tables.get(n)
    const names = parseRedTable(git(repo, ['show', `${ref}:${table.file}`]))
    let tip = null
    if (records.has(n)) {
        try {
            tip = JSON.parse(git(repo, ['show', `${ref}:${records.get(n).file}`])).tip || null
        } catch {
            tip = null
        }
    }
    return { n, names, table: table.base, tip }
}

function readSummary(file) {
    let doc
    try {
        doc = JSON.parse(fs.readFileSync(file, 'utf8'))
    } catch (error) {
        throw new Error(`the layer left no readable summary.json at ${file}: ${error.message}`)
    }
    if (!Array.isArray(doc.test_timings) || doc.test_timings.length === 0) {
        throw new Error(`${file} holds no test_timings rows, so nothing was measured`)
    }
    return doc.test_timings
}

function localDate() {
    const now = new Date()
    const two = (n) => String(n).padStart(2, '0')
    return `${now.getFullYear()}-${two(now.getMonth() + 1)}-${two(now.getDate())}`
}

async function runWorker(flags) {
    const root = path.join(path.resolve(flags['state-dir']), 'promotion')
    const id = flags.job
    const dir = jobDirOf(root, id)
    const first = readJob(dir)
    if (!first) fail(2, `no job '${id}' under ${root}`)
    const { config, paths, tip } = first
    const repo = config.repo
    const tree = paths.tree
    const lockDir = path.join(root, 'lock')
    const children = new Children(dir)
    updateJob(dir, { worker: { pid: process.pid, start: startTimeOf(process.pid) } })

    let ended = false
    const abnormal = (why) => {
        if (ended) return
        ended = true
        const job = readJob(dir)
        children.killAll()
        if (job && !TERMINAL.has(job.phase)) {
            cleanupRemote(dir, job)
            updateJob(dir, { phase: 'failed', error: why, endedAt: new Date().toISOString(), children: [] })
        }
        releaseLock(lockDir, id)
    }
    for (const signal of ['SIGTERM', 'SIGINT', 'SIGHUP']) {
        process.on(signal, () => {
            abnormal(`worker received ${signal}`)
            process.exit(2)
        })
    }
    process.on('uncaughtException', (error) => {
        appendLog(dir, `uncaught: ${error.stack || error}`)
        abnormal(`uncaught exception: ${error.message}`)
        process.exit(2)
    })
    process.on('exit', () => abnormal('the worker exited without a final phase'))

    const beat = setInterval(() => updateJob(dir, {}), config.heartbeatInterval * 1000)
    const setPhase = (phase) => {
        appendLog(dir, `[phase] ${phase}`)
        const job = readJob(dir)
        updateJob(dir, { phase, phases: { ...(job?.phases || {}), [phase]: { startedAt: new Date().toISOString() } } })
    }
    const summaryFile = path.join(tree, 'summary.json')
    const values = { worktree: tree, tip, jobs: config.jobs, repo, remote: config.remote, develop: config.develop, sha: tip }

    try {
        // ---- measure
        setPhase('measure')
        if (fs.existsSync(path.join(tree, '.git'))) {
            git(tree, ['checkout', '--quiet', '--detach', '--force', tip])
            git(tree, ['clean', '-fdq'])
        } else {
            fs.mkdirSync(path.dirname(tree), { recursive: true })
            git(repo, ['worktree', 'add', '--detach', tree, tip])
        }
        const at = () => git(tree, ['rev-parse', 'HEAD'])
        if (at() !== tip) throw new Error(`the measuring tree is at ${at()}, not at the tip ${tip}`)
        fs.rmSync(summaryFile, { force: true })
        const layer = await children.run(fill(config.layer, values), { label: 'layer', cwd: tree })
        if (layer.error) throw new Error(`the layer command could not run: ${layer.error.message}`)
        appendLog(dir, `[layer] exit ${layer.code}`)
        if (at() !== tip) throw new Error('the measuring tree moved while the layer ran, so the reading is not about the tip')
        const rows = readSummary(summaryFile)
        fs.copyFileSync(summaryFile, path.join(dir, 'layer-summary.json'))

        // ---- rerun-timeouts
        setPhase('rerun-timeouts')
        const timedOut = rows.filter((row) => row.timed_out === true)
        const reran = new Map()
        const byPlan = new Map()
        for (const row of timedOut) {
            if (!byPlan.has(row.plan_path)) byPlan.set(row.plan_path, [])
            byPlan.get(row.plan_path).push(row)
        }
        for (const [planPath, cases] of byPlan) {
            const relative = path.isAbsolute(planPath) && planPath.startsWith(tree) ? path.relative(tree, planPath) : planPath
            fs.rmSync(summaryFile, { force: true })
            const again = await children.run(fill(config.rerun, { ...values, plan: relative.replace(/\\/g, '/') }), {
                label: `rerun ${relative}`,
                cwd: tree,
                env: { ...process.env, CAMEL_TEST_TIMEOUT_SCALE: '2' },
            })
            if (again.error) throw new Error(`the re-run of ${relative} could not run: ${again.error.message}`)
            const second = readSummary(summaryFile)
            for (const row of cases) {
                const found = second.find((other) => other.plan === row.plan && other.test === row.test)
                reran.set(KEY(row.plan, row.test), found || null)
            }
        }

        // ---- compare
        setPhase('compare')
        const reportsRef = config.reportsRef || tip
        const previous = previousPromotion(repo, reportsRef, config.reports)
        const red = new Set()
        for (const row of rows) if (row.status === 'fail') red.add(KEY(row.plan, row.test))
        const green = []
        const stillRed = []
        for (const row of timedOut) {
            const key = KEY(row.plan, row.test)
            const second = reran.get(key)
            const name = `${row.plan}/${row.test}`
            if (second && second.status === 'pass') {
                red.delete(key)
                green.push(name)
            } else {
                red.add(key)
                stillRed.push({
                    case: name,
                    reason: !second
                        ? 'the case was not in the re-run of its plan'
                        : second.timed_out === true
                          ? 'timed out again at the doubled limit'
                          : `the re-run ended ${second.status}`,
                })
            }
        }
        const newReds = [...red].filter((key) => !previous.names.has(key)).sort()
        const vanished = [...previous.names].filter((key) => !red.has(key)).sort()
        const count = (status) => rows.filter((row) => row.status === status).length
        const readings = {
            total: rows.length,
            pass: count('pass') + green.length,
            fail: red.size,
            skip: count('skip'),
            known: count('xfail'),
            timed_out_raw_layer: timedOut.length,
        }
        const merges = previous.tip
            ? (git(repo, ['log', '--merges', '--first-parent', '--format=%h %s', `${previous.tip}..${tip}`], { allowFailure: true }) || '')
                  .split('\n').filter(Boolean)
            : []
        const display = (key) => key.replace('\t', ' ')
        const verdict = newReds.length === 0 ? 'no new red' : 'new red'
        const promotion = previous.n + 1
        updateJob(dir, {
            verdict,
            promotion,
            comparedWith: previous.table || 'nothing (no earlier promotion)',
            newReds: newReds.map(display),
            mergesInRange: merges,
        })

        // ---- records
        setPhase('records')
        const prefix = `${localDate()}-promotion-${promotion}`
        const out = path.join(dir, 'reports')
        fs.mkdirSync(out, { recursive: true })
        const redsName = `${prefix}-reds.tsv`
        const durationsName = `${prefix}-durations.tsv`
        const recordName = `${prefix}.record.json`
        fs.writeFileSync(path.join(out, redsName), [...red].sort().join('\n') + (red.size ? '\n' : ''))
        const numeric = (value) => (typeof value === 'number' ? value : '')
        fs.writeFileSync(
            path.join(out, durationsName),
            '# plan_id\tcase\tstatus\tchild_ms\tslot_ms\n' +
                rows
                    .map((row) => [row.plan, row.test, row.status, numeric(row.child_ms), numeric(row.slot_ms)].join('\t'))
                    .sort()
                    .join('\n') + '\n',
        )
        const willPublish = config.publish && newReds.length === 0
        let developBefore = null
        if (willPublish) {
            if (config.fetch) git(repo, ['fetch', '--quiet', config.remote, config.develop], { allowFailure: true })
            developBefore = git(repo, ['rev-parse', '--verify', `${config.remote}/${config.develop}^{commit}`], { allowFailure: true })
        }
        const record = {
            kind: 'promotion',
            promotion,
            tip,
            tier: 'fast',
            scope: 'test/plans (the whole plan tree)',
            jobs: config.jobs,
            measured_at: new Date().toISOString(),
            readings,
            comparison: {
                against: previous.table || 'nothing (no earlier promotion)',
                new_reds: newReds.length,
                vanished_reds: vanished.length,
                red_names_file: `${config.reports}/${redsName}`,
            },
            timeouts_rerun: {
                count: timedOut.length,
                run_alone: true,
                timeout_scale: 2,
                green,
                still_red_not_timeout: stillRed,
            },
            red_table: `${config.reports}/${redsName}`,
            red_table_rows: red.size,
            durations_file: `${config.reports}/${durationsName}`,
            next_promotion_compares_against: `${config.reports}/${redsName}`,
            ...(newReds.length ? { new_red_names: newReds.map(display), merges_in_range: merges } : {}),
            ...(willPublish ? { pushed_as: config.develop, develop_before: developBefore, pushed_develop_sha: tip } : {}),
        }
        fs.writeFileSync(path.join(out, recordName), JSON.stringify(record, null, 2) + '\n')
        updateJob(dir, { files: [redsName, durationsName, recordName].map((name) => path.join(out, name)) })
        appendLog(dir, `[records] ${prefix}: ${readings.total} cases, ${red.size} red, ${newReds.length} new, ${green.length}/${timedOut.length} timeouts green on re-run`)

        // ---- publish
        if (willPublish) {
            setPhase('publish')
            if (developBefore && git(repo, ['merge-base', '--is-ancestor', developBefore, tip], { allowFailure: true }) === null) {
                throw new Error(`${config.remote}/${config.develop} (${developBefore.slice(0, 9)}) is not an ancestor of the measured tip, so it would not be a fast-forward`)
            }
            await publish({ config, repo, tip, dir, out, names: [redsName, durationsName, recordName], promotion, children, values })
        }
        clearInterval(beat)
        const final = readJob(dir)
        if (!TERMINAL.has(final.phase)) {
            updateJob(dir, { phase: 'done', endedAt: new Date().toISOString(), children: [] })
        }
        ended = true
        releaseLock(lockDir, id)
        appendLog(dir, `[done] ${verdict}`)
        process.exit(newReds.length ? 1 : 0)
    } catch (error) {
        clearInterval(beat)
        appendLog(dir, `[error] ${error.message}`)
        abnormal(error.message)
        updateJob(dir, { error: error.message })
        process.exit(2)
    }
}

/**
 * Publishes exactly `tip`. The push hook reads the promotion record from the tree it runs in, and a
 * record cannot be inside the commit it names, so the records are committed on top of the tip in the
 * publish tree and what is pushed is the tip itself, not that commit.
 */
async function publish({ config, repo, tip, dir, out, names, promotion, children, values }) {
    let tree = config.publishWorktree
    if (!tree) tree = localSettings(repo).mergeWorktree
    if (Array.isArray(tree)) tree = tree[0]
    if (typeof tree === 'string' && tree.includes(';')) tree = tree.split(';')[0]
    if (!tree || tree.startsWith('<')) tree = path.join(path.dirname(path.dirname(dir)), 'publish-tree')
    if (!fs.existsSync(path.join(tree, '.git'))) git(repo, ['worktree', 'add', '--detach', tree, tip])
    if (git(tree, ['status', '--porcelain']) !== '') throw new Error(`the publish tree ${tree} is not clean`)
    git(tree, ['checkout', '--quiet', '--detach', tip])
    const reportsDir = path.join(tree, config.reports)
    fs.mkdirSync(reportsDir, { recursive: true })
    for (const name of names) fs.copyFileSync(path.join(out, name), path.join(reportsDir, name))
    git(tree, ['add', '--', config.reports])
    git(tree, ['-c', 'commit.gpgsign=false', 'commit', '--quiet', '-m', `promotion ${promotion}: records of the whole fast layer on ${tip.slice(0, 12)}`])
    const recordCommit = git(tree, ['rev-parse', 'HEAD'])
    git(repo, ['branch', '-f', `record/promotion-${promotion}`, recordCommit])
    appendLog(dir, `[publish] records committed as ${recordCommit.slice(0, 12)} on record/promotion-${promotion}; publishing ${tip}`)
    const run = await children.run(fill(config.publishCommand, { ...values, worktree: tree, sha: tip }), { label: 'publish', cwd: tree })
    if (run.error || run.code !== 0) throw new Error(`the publish command ${run.error ? `could not run: ${run.error.message}` : `exited ${run.code}`}`)
    if (config.verifyPublish) {
        const listed = git(repo, ['ls-remote', config.remote, `refs/heads/${config.develop}`], { allowFailure: true }) || ''
        const there = listed.split(/\s+/)[0]
        if (there !== tip) throw new Error(`after the publish ${config.remote}/${config.develop} is ${there || 'unreadable'}, not the measured ${tip}`)
    }
    updateJob(dir, { published: { sha: tip, recordCommit } })
}

// ---------------------------------------------------------------- dispatch

const [command, ...rest] = process.argv.slice(2)
const { positional, flags } = parseArgs(rest)
if (command === 'start') doStart(flags)
else if (command === 'status') doStatus(flags, positional)
else if (command === 'cancel') doCancel(flags, positional)
else if (command === 'run') await runWorker(flags)
else fail(2, 'usage: promote.mjs start|status|cancel|run (see the header of this file)')
