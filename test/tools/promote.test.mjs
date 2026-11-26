// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * `scripts/promote.mjs` over a throwaway repository, with the layer, the re-run, the cleanup and the
 * publish replaced by stubs. The repository carries a previous promotion's red table (promotion 4)
 * and its record, and a merge after the commit that record names.
 *
 * Read from the result: `start` returns at once while the worker goes on; `status` moves through
 * the phases to the verdict; a timeout that passes on re-run is not a new red and one that fails
 * again is; the records name promotion 4's table; `cancel` and a worker killed without a chance to
 * clean up both leave no process of the job alive (the stub starts a grandchild whose pid is read
 * back) and free the lock; the publish command is handed exactly the measured sha.
 *
 * Exit: 0 when every check holds, 1 when one does not.
 */
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { execFileSync, spawn, spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

import { killJobProcesses, startTimeOf } from '../../scripts/lib/job.mjs'
import { killProcessTree } from './kill-tree.mjs'

const here = path.dirname(fileURLToPath(import.meta.url))
const tool = path.resolve(here, '..', '..', 'scripts', 'promote.mjs')
const IS_WINDOWS = process.platform === 'win32'

const checks = []
function check(name, holds, detail) {
    checks.push({ name, holds: Boolean(holds), detail: detail ?? '' })
}

const slash = (p) => p.replace(/\\/g, '/')
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

function git(dir, args) {
    return execFileSync('git', ['-C', dir, ...args], { encoding: 'utf8' }).replace(/\n+$/, '')
}

function write(file, text) {
    fs.mkdirSync(path.dirname(file), { recursive: true })
    fs.writeFileSync(file, text)
}

function commitAll(dir, message) {
    git(dir, ['add', '-A'])
    git(dir, ['-c', 'user.name=t', '-c', 'user.email=t@example.invalid', '-c', 'commit.gpgsign=false', 'commit', '--quiet', '-m', message])
    return git(dir, ['rev-parse', 'HEAD'])
}

const root = fs.mkdtempSync(path.join(os.tmpdir(), 'promote-'))
const repo = path.join(root, 'repo')
const stubs = path.join(root, 'stubs')
fs.mkdirSync(repo, { recursive: true })
git(repo, ['init', '--quiet', '-b', 'main'])
git(repo, ['config', 'core.autocrlf', 'false'])
write(path.join(repo, 'a.txt'), 'a\n')
const first = commitAll(repo, 'first')
write(path.join(repo, 'internal/reports/2026-11-24-promotion-4-reds.tsv'), 'p\told_red\nq\tother_old_red\n')
write(path.join(repo, 'internal/reports/2026-11-24-promotion-4.record.json'), JSON.stringify({ kind: 'promotion', promotion: 4, tip: first }))
commitAll(repo, 'reports of promotion 4')
git(repo, ['checkout', '--quiet', '-b', 'feature'])
write(path.join(repo, 'b.txt'), 'b\n')
commitAll(repo, 'feature work')
git(repo, ['checkout', '--quiet', 'main'])
git(repo, ['-c', 'user.name=t', '-c', 'user.email=t@example.invalid', '-c', 'commit.gpgsign=false', 'merge', '--quiet', '--no-ff', '-m', 'merge feature', 'feature'])
const tip = git(repo, ['rev-parse', 'HEAD'])

// ---- the stubs: one for the layer, one for the re-run, one for the publish
write(
    path.join(stubs, 'layer.mjs'),
    `import fs from 'node:fs'
import { spawn } from 'node:child_process'
const [worktree, scenarioFile] = process.argv.slice(2)
const scenario = JSON.parse(fs.readFileSync(scenarioFile, 'utf8'))
const grand = spawn(process.execPath, ['-e', 'setTimeout(() => {}, 600000)'], { stdio: 'ignore' })
fs.appendFileSync(scenario.pids, JSON.stringify({ stub: process.pid, grandchild: grand.pid }) + '\\n')
await new Promise((resolve) => setTimeout(resolve, scenario.layerMs))
grand.kill()
fs.writeFileSync(worktree + '/summary.json', JSON.stringify({ test_timings: scenario.rows }))
process.exit(scenario.rows.some((row) => row.status === 'fail') ? 1 : 0)
`,
)
write(
    path.join(stubs, 'rerun.mjs'),
    `import fs from 'node:fs'
const [worktree, scenarioFile, plan] = process.argv.slice(2)
const scenario = JSON.parse(fs.readFileSync(scenarioFile, 'utf8'))
fs.appendFileSync(scenario.reruns, JSON.stringify({ plan, scale: process.env.CAMEL_TEST_TIMEOUT_SCALE }) + '\\n')
const rows = scenario.rows.filter((row) => row.timed_out).map((row) => ({ ...row, status: scenario.rerunStatus, timed_out: scenario.rerunStatus === 'timeout' }))
fs.writeFileSync(worktree + '/summary.json', JSON.stringify({ test_timings: rows.map((row) => ({ ...row, status: row.status === 'timeout' ? 'fail' : row.status })) }))
`,
)
write(
    path.join(stubs, 'publish.mjs'),
    `import fs from 'node:fs'
const [sha, worktree, file] = process.argv.slice(2)
fs.writeFileSync(file, JSON.stringify({ sha, worktree }))
`,
)
write(
    path.join(stubs, 'cleanup.mjs'),
    `import fs from 'node:fs'
fs.appendFileSync(process.argv[2], 'cleaned\\n')
`,
)

const row = (plan, test, status, extra = {}) => ({ plan, plan_path: `test/plans/${plan}.plan.toml`, test, status, timed_out: false, child_ms: 10, slot_ms: 12, ...extra })
const BASE_ROWS = [
    row('p', 'ok', 'pass'),
    row('p', 'old_red', 'fail'),
    row('p', 'known', 'xfail'),
    row('q', 'skipped', 'skip'),
    row('q', 'slow', 'fail', { timed_out: true }),
]

let counter = 0
function scenario(overrides) {
    counter += 1
    const dir = path.join(root, `scenario-${counter}`)
    fs.mkdirSync(dir, { recursive: true })
    const doc = { layerMs: 1500, rerunStatus: 'pass', rows: BASE_ROWS, pids: slash(path.join(dir, 'pids.jsonl')), reruns: slash(path.join(dir, 'reruns.jsonl')), ...overrides }
    write(path.join(dir, 'scenario.json'), JSON.stringify(doc))
    const q = (p) => `"${slash(p)}"`
    const args = [
        '--repo', repo,
        '--state-dir', path.join(dir, 'state'),
        '--tip', tip,
        '--layer', `node ${q(path.join(stubs, 'layer.mjs'))} "{worktree}" ${q(path.join(dir, 'scenario.json'))}`,
        '--rerun', `node ${q(path.join(stubs, 'rerun.mjs'))} "{worktree}" ${q(path.join(dir, 'scenario.json'))} "{plan}"`,
        '--cleanup', `node ${q(path.join(stubs, 'cleanup.mjs'))} ${q(path.join(dir, 'cleanup.log'))}`,
        '--publish-command', `node ${q(path.join(stubs, 'publish.mjs'))} {sha} "{worktree}" ${q(path.join(dir, 'published.json'))}`,
        '--no-fetch',
        '--no-verify-publish',
        '--heartbeat-interval', '1',
    ]
    return { dir, args, state: path.join(dir, 'state'), file: (name) => path.join(dir, name) }
}

function promote(sub, args, extra = []) {
    const started = Date.now()
    const run = spawnSync(process.execPath, [tool, sub, ...extra, ...args], { encoding: 'utf8', timeout: 60000 })
    return { code: run.status, out: String(run.stdout || ''), err: String(run.stderr || ''), ms: Date.now() - started }
}

function status(s) {
    const run = promote('status', ['--repo', repo, '--state-dir', s.state], ['--json'])
    try {
        return { ...JSON.parse(run.out), _code: run.code }
    } catch {
        return { phase: `unreadable: ${run.out}${run.err}` }
    }
}

async function until(s, wanted, ms = 60000) {
    const seen = []
    const stop = Date.now() + ms
    let job = null
    while (Date.now() < stop) {
        job = status(s)
        if (seen[seen.length - 1] !== job.phase) seen.push(job.phase)
        if (wanted(job)) break
        await sleep(300)
    }
    return { job, seen }
}

const ended = (job) => ['done', 'failed', 'cancelled', 'dead'].includes(job.phase)

function alive(pid) {
    try {
        process.kill(pid, 0)
        return true
    } catch (error) {
        return error.code === 'EPERM'
    }
}

function pidsOf(s) {
    let lines = []
    try {
        lines = fs.readFileSync(s.file('pids.jsonl'), 'utf8').split('\n').filter(Boolean).map((l) => JSON.parse(l))
    } catch {
        /* none yet */
    }
    return lines.flatMap((l) => [l.stub, l.grandchild])
}

async function waitForPids(s) {
    for (let i = 0; i < 100 && pidsOf(s).length === 0; i += 1) await sleep(200)
    // The worker has recorded the stub's own process by the time the stub has been running a moment.
    for (let i = 0; i < 50 && !(status(s).children || []).length; i += 1) await sleep(200)
    return pidsOf(s)
}

async function gone(pids) {
    for (let i = 0; i < 25 && pids.some(alive); i += 1) await sleep(200)
    return pids.every((pid) => !alive(pid))
}

function hardKill(pid) {
    if (IS_WINDOWS) spawnSync('taskkill', ['/PID', String(pid), '/F'], { stdio: 'ignore', windowsHide: true })
    else process.kill(pid, 'SIGKILL')
}

const reportsOf = (job) => Object.fromEntries((job.files || []).map((file) => [path.basename(file).replace(/^\d{4}-\d{2}-\d{2}-/, ''), file]))

// ---- 1. a run whose only new red is a timeout that passes alone
{
    const s = scenario({})
    const begun = promote('start', s.args)
    check('start returns in under 3 s', begun.code === 0 && begun.ms < 3000, `${begun.ms} ms, exit ${begun.code}: ${begun.out}${begun.err}`)
    check('start prints the job id and the status command', /promotion job \S+ started/.test(begun.out) && /promote\.mjs status \S+/.test(begun.out), begun.out)
    const early = status(s)
    check('the worker keeps running after start returned', ['queued', 'measure'].includes(early.phase) && early.worker && alive(early.worker.pid), JSON.stringify(early.phase))
    const second = promote('start', s.args)
    check('a second start while one runs is refused with exit 3', second.code === 3, `${second.code}: ${second.err}`)
    const { job, seen } = await until(s, ended)
    const walked = Object.keys(job.phases || {})
    check('status shows the job in flight and ends at done, and job.json lists the phases in order', seen.includes('measure') && seen[seen.length - 1] === 'done' && walked.join() === 'measure,rerun-timeouts,compare,records,done'.replace(',done', ''), `${seen.join(' > ')} | ${walked.join()}`)
    check('the verdict is "no new red" when the only new red timed out and passes on re-run', job.phase === 'done' && job.verdict === 'no new red' && job.newReds.length === 0, `${job.phase} ${job.verdict} ${JSON.stringify(job.newReds)} ${job.error ?? ''}`)
    check('the re-run was given the doubled limit and the plan of the timed-out case', fs.existsSync(s.file('reruns.jsonl')) && /"scale":"2"/.test(fs.readFileSync(s.file('reruns.jsonl'), 'utf8')) && /q\.plan\.toml/.test(fs.readFileSync(s.file('reruns.jsonl'), 'utf8')))
    const files = reportsOf(job)
    check('the three records are written under promotion 5', ['promotion-5-reds.tsv', 'promotion-5-durations.tsv', 'promotion-5.record.json'].every((n) => files[n] && fs.existsSync(files[n])), Object.keys(files).join(', '))
    const record = files['promotion-5.record.json'] ? JSON.parse(fs.readFileSync(files['promotion-5.record.json'], 'utf8')) : {}
    check('the record names promotion 4\'s table, the measured tip and the vanished red', record.comparison?.against === '2026-11-24-promotion-4-reds.tsv' && record.tip === tip && record.comparison.new_reds === 0 && record.comparison.vanished_reds === 1, JSON.stringify(record.comparison))
    check('the record accounts for the timeout: one green, none still red, counts add up', record.timeouts_rerun?.count === 1 && record.timeouts_rerun.green.join() === 'q/slow' && record.timeouts_rerun.still_red_not_timeout.length === 0 && record.timeouts_rerun.run_alone === true, JSON.stringify(record.timeouts_rerun))
    check('the readings count the layer after the re-run', record.readings?.total === 5 && record.readings.pass === 2 && record.readings.fail === 1 && record.readings.skip === 1 && record.readings.known === 1 && record.readings.timed_out_raw_layer === 1, JSON.stringify(record.readings))
    const reds = files['promotion-5-reds.tsv'] ? fs.readFileSync(files['promotion-5-reds.tsv'], 'utf8') : ''
    const durations = files['promotion-5-durations.tsv'] ? fs.readFileSync(files['promotion-5-durations.tsv'], 'utf8') : ''
    check('the red table holds plan id and case, and the durations hold every case', reds === 'p\told_red\n' && durations.split('\n').filter((l) => l && !l.startsWith('#')).length === 5, `${JSON.stringify(reds)} / ${durations.split('\n').length} lines`)
    check('the lock is free and no publish happened', !fs.existsSync(path.join(s.state, 'promotion', 'lock')) && !fs.existsSync(s.file('published.json')))
}

// ---- 2. new reds: a timeout that fails again, and a plain new red
{
    const s = scenario({ rerunStatus: 'timeout', rows: [...BASE_ROWS, row('p', 'brand_new', 'fail')] })
    promote('start', s.args)
    const { job } = await until(s, ended)
    check('a timeout that fails again and a plain new red are both new reds', job.verdict === 'new red' && job.newReds.includes('p brand_new') && job.newReds.includes('q slow'), `${job.verdict} ${JSON.stringify(job.newReds)} ${job.error ?? ''}`)
    check('new reds list the merges in range for bisecting', (job.mergesInRange || []).some((line) => line.includes('merge feature')), JSON.stringify(job.mergesInRange))
    const files = reportsOf(job)
    const record = JSON.parse(fs.readFileSync(files['promotion-5.record.json'], 'utf8'))
    check('the record of a promotion with new reds says so and its timeout is still red', record.comparison.new_reds === 2 && record.timeouts_rerun.still_red_not_timeout[0]?.case === 'q/slow', JSON.stringify(record.comparison))
}

// ---- 3. a --publish with a new red publishes nothing; with none, exactly the measured sha
{
    const s = scenario({ rerunStatus: 'timeout' })
    promote('start', [...s.args, '--publish'])
    const { job } = await until(s, ended)
    check('--publish with a new red publishes nothing', job.verdict === 'new red' && !fs.existsSync(s.file('published.json')), `${job.verdict} ${job.error ?? ''}`)
}
{
    const s = scenario({})
    const begun = promote('start', [...s.args, '--publish'])
    const { job } = await until(s, ended)
    const published = fs.existsSync(s.file('published.json')) ? JSON.parse(fs.readFileSync(s.file('published.json'), 'utf8')) : null
    check('--publish with no new red calls the publish command with exactly the measured sha', job.phase === 'done' && published?.sha === tip, `${job.phase} ${job.error ?? ''} ${JSON.stringify(published)} ${begun.err}`)
    const recordCommit = job.published?.recordCommit
    check('the publish tree carries the record above the tip, and the tip is what was published', recordCommit && recordCommit !== tip && git(repo, ['merge-base', '--is-ancestor', tip, recordCommit]) === '' && git(repo, ['show', '--name-only', '--format=', recordCommit]).includes('promotion-5.record.json'), `${recordCommit}`)
}

// ---- 4. cancel leaves no process of the job alive
{
    const s = scenario({ layerMs: 120000 })
    promote('start', s.args)
    const pids = await waitForPids(s)
    const before = status(s)
    check('the stub layer is running with a grandchild', pids.length === 2 && pids.every(alive) && before.phase === 'measure', `${JSON.stringify(pids)} ${before.phase}`)
    const worker = before.worker.pid
    const cancelled = promote('cancel', ['--repo', repo, '--state-dir', s.state])
    check('cancel ends the stub, its grandchild and the worker', cancelled.code === 0 && (await gone([...pids, worker])), `${cancelled.out}${cancelled.err}`)
    const after = status(s)
    check('a cancelled job is marked cancelled, its remote run is cleaned and its lock is free', after.phase === 'cancelled' && fs.existsSync(s.file('cleanup.log')) && !fs.existsSync(path.join(s.state, 'promotion', 'lock')), `${after.phase}`)
    await sleep(2500)
    check('a cancelled job stays cancelled (no late heartbeat revives it)', status(s).phase === 'cancelled')
    const again = promote('start', s.args)
    check('a new start works after the cancel', again.code === 0, again.err)
    promote('cancel', ['--repo', repo, '--state-dir', s.state])
}

// ---- 5. a worker killed with no chance to clean up
{
    const s = scenario({ layerMs: 120000 })
    promote('start', s.args)
    const pids = await waitForPids(s)
    const job = status(s)
    hardKill(job.worker.pid)
    await sleep(500)
    // Where a child dies with the parent that started it (Windows puts children in a job object that
    // ends them), the hard kill leaves nothing to clean up; elsewhere the stub and its grandchild
    // are orphans, which is the case the recorded pids are for.
    if (!IS_WINDOWS) check('after a hard kill the stub and its grandchild are still running (the worker could not clean up)', pids.every(alive), `alive=${pids.filter(alive)} of ${pids}`)
    check('the lock is still on disk until status looks', fs.existsSync(path.join(s.state, 'promotion', 'lock')))
    const seen = status(s)
    check('status reports the worker dead and says why', seen.phase === 'dead' && /gone/.test(seen.endedBecause || ''), `${seen.phase} ${seen.endedBecause}`)
    const goneNow = await gone(pids)
    check('status ended the processes the dead worker left and freed the lock', goneNow && !fs.existsSync(path.join(s.state, 'promotion', 'lock')) && fs.existsSync(s.file('cleanup.log')), `gone=${goneNow} lock=${fs.existsSync(path.join(s.state, 'promotion', 'lock'))} cleanup=${fs.existsSync(s.file('cleanup.log'))} children=${JSON.stringify(job.children)} alive=${pids.filter(alive)} pids=${pids}`)
    const again = promote('start', s.args)
    check('a new start works after a dead job', again.code === 0, `${again.out}${again.err}`)
    promote('cancel', ['--repo', repo, '--state-dir', s.state])
}

// ---- 6. a start does not wait for a dead holder of the lock
{
    const s = scenario({ layerMs: 120000 })
    promote('start', s.args)
    const pids = await waitForPids(s)
    hardKill(status(s).worker.pid)
    await sleep(500)
    const again = promote('start', s.args)
    check('start reclaims the lock of a dead worker without being asked to look first', again.code === 0, `${again.out}${again.err}`)
    promote('cancel', ['--repo', repo, '--state-dir', s.state])
    await gone(pidsOf(s))
    void pids
}

// ---- 7. a worker that stops beating is dead even though its process is alive
{
    const s = scenario({ layerMs: 120000 })
    const args = s.args.map((a, i, all) => (all[i - 1] === '--heartbeat-interval' ? '60' : a))
    promote('start', [...args, '--heartbeat-limit', '3'])
    const pids = await waitForPids(s)
    await sleep(4500)
    const seen = status(s)
    check('a live worker that has not beaten within the limit is reported dead', seen.phase === 'dead' && /heartbeat/.test(seen.endedBecause || ''), `${seen.phase} ${seen.endedBecause}`)
    check('and its processes are ended', await gone(pids))
}

// ---- 7b. what a dead worker leaves is ended by the recorded pids, whatever started it
{
    const lonely = spawn(process.execPath, ['-e', 'setTimeout(() => {}, 600000)'], { detached: true, stdio: 'ignore' })
    lonely.unref()
    await sleep(500)
    const left = { worker: null, children: [{ pid: lonely.pid, start: startTimeOf(lonely.pid), label: 'layer' }] }
    check('the recorded pid of an orphan is ended with its tree', alive(lonely.pid) && killJobProcesses(left).includes(lonely.pid) && (await gone([lonely.pid])))
    const reused = spawn(process.execPath, ['-e', 'setTimeout(() => {}, 600000)'], { detached: true, stdio: 'ignore' })
    reused.unref()
    await sleep(500)
    const stale = { worker: null, children: [{ pid: reused.pid, start: '1999-01-01T00:00:00.0000000Z', label: 'layer' }] }
    check('a pid whose start time is not the recorded one is a different process and is left alone', killJobProcesses(stale).length === 0 && alive(reused.pid))
    killProcessTree(reused)
}

// ---- 8. the comparison is against the immediately previous promotion
{
    // Promotion 5 has a record but no red table: promotion 4's table is not the previous promotion's.
    git(repo, ['checkout', '--quiet', '-b', 'gap'])
    write(path.join(repo, 'internal/reports/2026-11-26-promotion-5.record.json'), JSON.stringify({ kind: 'promotion', promotion: 5, tip: first }))
    const gapTip = commitAll(repo, 'a promotion with a record and no table')
    git(repo, ['checkout', '--quiet', 'main'])
    const s = scenario({})
    const gapArgs = s.args.map((a, i, all) => (all[i - 1] === '--tip' ? gapTip : a))
    promote('start', gapArgs)
    const { job } = await until(s, ended)
    check('a previous promotion with a record and no red table is an error, not a comparison with an older table', job.phase === 'failed' && /promotion 5 has a record but no red table/.test(job.error || ''), `${job.phase} ${job.error}`)
    check('a failed job frees the lock and cleans the remote run', !fs.existsSync(path.join(s.state, 'promotion', 'lock')) && fs.existsSync(s.file('cleanup.log')))
}

const failed = checks.filter((c) => !c.holds)
for (const c of checks) console.log(`${c.holds ? 'ok  ' : 'FAIL'} ${c.name}${c.holds ? '' : `  [${c.detail}]`}`)
console.log(`promote: ${checks.length} checks, ${failed.length} failed`)
if (process.env.PROMOTE_KEEP) console.log('kept', root)
else try {
    fs.rmSync(root, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 })
} catch {
    /* a temp directory that cannot be removed is not a result */
}
process.exit(failed.length === 0 ? 0 : 1)
