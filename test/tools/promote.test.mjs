// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * `scripts/promote.mjs` over a throwaway repository, with the layer, the cleanup and the publish
 * replaced by stubs. The repository carries a previous promotion's red table (promotion 4)
 * and its record, and a merge after the commit that record names.
 *
 * Read from the result: `start` returns at once while the worker goes on; `status` moves through
 * the phases to the verdict; a case the runner re-ran after a timeout is not red when the re-run
 * passed and is when it timed out again; a layer that was cut short, or whose reading lacks a case
 * of the previous reading, publishes nothing and names the cases; a child past its limit ends the
 * job in its phase; the records name promotion 4's table; `cancel` and a worker killed without a chance to
 * clean up both leave no process of the job alive (the stub starts a grandchild whose pid is read
 * back) and free the lock; the publish command is handed exactly the measured sha.
 *
 * The tree-maker is a stub too: it makes the worktree and writes the machine-local settings file, as the
 * real one does, and the layer stub refuses a tree without that file, as the host tooling does. A
 * tree-maker that fails ends the job in the tree phase with what it said.
 *
 * Exit: 0 when every check holds, 1 when one does not.
 */
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { execFileSync, spawn, spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

import { acquireLock, killJobProcesses, startTimeOf } from '../../scripts/lib/job.mjs'
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
write(path.join(repo, '.gitignore'), 'internal/tools/camel-gate.local.json\n')
const first = commitAll(repo, 'first')
write(path.join(repo, 'internal/reports/2026-11-24-promotion-4-reds.tsv'), 'p\told_red\nq\tother_old_red\n')
write(path.join(repo, 'internal/reports/2026-11-24-promotion-4-durations.tsv'), "# plan_id\tcase\tstatus\tchild_ms\tslot_ms\np\tok\tpass\t1\t1\np\told_red\tfail\t1\t1\np\tknown\txfail\t1\t1\nq\tskipped\tskip\t1\t1\nq\tslow\tpass\t1\t1\n")
write(path.join(repo, 'internal/reports/2026-11-24-promotion-4.record.json'), JSON.stringify({ kind: 'promotion', promotion: 4, tip: first }))
commitAll(repo, 'reports of promotion 4')
git(repo, ['checkout', '--quiet', '-b', 'feature'])
write(path.join(repo, 'b.txt'), 'b\n')
commitAll(repo, 'feature work')
git(repo, ['checkout', '--quiet', 'main'])
git(repo, ['-c', 'user.name=t', '-c', 'user.email=t@example.invalid', '-c', 'commit.gpgsign=false', 'merge', '--quiet', '--no-ff', '-m', 'merge feature', 'feature'])
const tip = git(repo, ['rev-parse', 'HEAD'])

// ---- the stubs: one for the layer, one for the publish
write(
    path.join(stubs, 'treemaker.mjs'),
    `import fs from 'node:fs'
import { execFileSync } from 'node:child_process'
const [repo, tree, sha, scenarioFile, ...hooks] = process.argv.slice(2)
const scenario = JSON.parse(fs.readFileSync(scenarioFile, 'utf8'))
fs.appendFileSync(scenario.trees, JSON.stringify({ tree, sha, hooks }) + '\\n')
if (scenario.treeHangs) await new Promise((resolve) => setTimeout(resolve, 600000))
if (scenario.treeFails) {
    console.log(scenario.treeFails)
    process.exit(3)
}
execFileSync('git', ['-C', repo, 'worktree', 'add', '--detach', tree, sha], { stdio: 'ignore' })
fs.mkdirSync(tree + '/internal/tools', { recursive: true })
fs.writeFileSync(tree + '/internal/tools/camel-gate.local.json', '{"sshHost":"stub"}')
`,
)
write(
    path.join(stubs, 'layer.mjs'),
    `import fs from 'node:fs'
import { spawn } from 'node:child_process'
const [worktree, scenarioFile] = process.argv.slice(2)
const scenario = JSON.parse(fs.readFileSync(scenarioFile, 'utf8'))
if (!fs.existsSync(worktree + '/internal/tools/camel-gate.local.json')) {
    console.log("the local settings file has no 'sshHost'")
    process.exit(2)
}
const grand = spawn(process.execPath, ['-e', 'setTimeout(() => {}, 600000)'], { stdio: 'ignore' })
fs.appendFileSync(scenario.pids, JSON.stringify({ stub: process.pid, grandchild: grand.pid }) + '\\n')
await new Promise((resolve) => setTimeout(resolve, scenario.layerMs))
grand.kill()
fs.writeFileSync(worktree + '/summary.json', JSON.stringify({ test_timings: scenario.rows }))
process.exit(scenario.layerExit ?? (scenario.rows.some((row) => row.status === 'fail') ? 1 : 0))
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
    // timed out in the layer, passed on the runner's own re-run: the row carries the final verdict
    row('q', 'slow', 'pass', { rerun: { first_timed_out: true, status: 'pass' } }),
]
const TWICE = [...BASE_ROWS.slice(0, 4), row('q', 'slow', 'fail', { timed_out: true, rerun: { first_timed_out: true, status: 'fail' } })]

let counter = 0
function scenario(overrides) {
    counter += 1
    const dir = path.join(root, `scenario-${counter}`)
    fs.mkdirSync(dir, { recursive: true })
    const doc = { layerMs: 1500, rows: BASE_ROWS, pids: slash(path.join(dir, 'pids.jsonl')), trees: slash(path.join(dir, 'trees.jsonl')), ...overrides }
    write(path.join(dir, 'scenario.json'), JSON.stringify(doc))
    const q = (p) => `"${slash(p)}"`
    const args = [
        '--repo', repo,
        '--state-dir', path.join(dir, 'state'),
        '--tip', tip,
        '--layer', `node ${q(path.join(stubs, 'layer.mjs'))} "{worktree}" ${q(path.join(dir, 'scenario.json'))}`,
        '--tree-maker', `node ${q(path.join(stubs, 'treemaker.mjs'))} "{repo}" "{tree}" {sha} ${q(path.join(dir, 'scenario.json'))} {hooks}`,
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
    check('the worker keeps running after start returned', ['queued', 'tree', 'measure'].includes(early.phase) && early.worker && alive(early.worker.pid), JSON.stringify(early.phase))
    const second = promote('start', s.args)
    check('a second start while one runs is refused with exit 3', second.code === 3, `${second.code}: ${second.err}`)
    const { job, seen } = await until(s, ended)
    const walked = Object.keys(job.phases || {})
    check('status shows the job in flight and ends at done, and job.json lists the phases in order', seen.includes('measure') && seen[seen.length - 1] === 'done' && walked.join() === 'tree,measure,compare,records', `${seen.join(' > ')} | ${walked.join()}`)
    check('the verdict is "no new red" when the only new red timed out and passes on re-run', job.phase === 'done' && job.verdict === 'no new red' && job.newReds.length === 0, `${job.phase} ${job.verdict} ${JSON.stringify(job.newReds)} ${job.error ?? ''}`)
    check('the promotion re-runs nothing: the layer stub ran once', pidsOf(s).length === 2, JSON.stringify(pidsOf(s)))
    const files = reportsOf(job)
    check('the three records are written under promotion 5', ['promotion-5-reds.tsv', 'promotion-5-durations.tsv', 'promotion-5.record.json'].every((n) => files[n] && fs.existsSync(files[n])), Object.keys(files).join(', '))
    const record = files['promotion-5.record.json'] ? JSON.parse(fs.readFileSync(files['promotion-5.record.json'], 'utf8')) : {}
    check('the record names promotion 4\'s table, the measured tip and the vanished red', record.comparison?.against === '2026-11-24-promotion-4-reds.tsv' && record.tip === tip && record.comparison.new_reds === 0 && record.comparison.vanished_reds === 1, JSON.stringify(record.comparison))
    check('the record accounts for the timeout: one green, none still red, counts add up', record.timeouts_rerun?.count === 1 && record.timeouts_rerun.green.join() === 'q/slow' && record.timeouts_rerun.still_red_not_timeout.length === 0 && record.timeouts_rerun.run_alone === true && record.timeouts_rerun.promotion_reran_plans === false && /plan runner/.test(record.timeouts_rerun.rerun_by), JSON.stringify(record.timeouts_rerun))
    check('the readings count the final verdicts of the layer', record.readings?.total === 5 && record.readings.pass === 2 && record.readings.fail === 1 && record.readings.skip === 1 && record.readings.known === 1, JSON.stringify(record.readings))
    const reds = files['promotion-5-reds.tsv'] ? fs.readFileSync(files['promotion-5-reds.tsv'], 'utf8') : ''
    const durations = files['promotion-5-durations.tsv'] ? fs.readFileSync(files['promotion-5-durations.tsv'], 'utf8') : ''
    check('the red table holds plan id and case, and the durations hold every case', reds === 'p\told_red\n' && durations.split('\n').filter((l) => l && !l.startsWith('#')).length === 5, `${JSON.stringify(reds)} / ${durations.split('\n').length} lines`)
    check('the lock is free and no publish happened', !fs.existsSync(path.join(s.state, 'promotion', 'lock')) && !fs.existsSync(s.file('published.json')))
}

// ---- 1b. the trees come from the tree-maker, with the hooks only where something is pushed from
{
    const s = scenario({})
    promote('start', [...s.args, '--publish'])
    const { job } = await until(s, ended)
    const made = fs.existsSync(s.file('trees.jsonl')) ? fs.readFileSync(s.file('trees.jsonl'), 'utf8').split(/\r?\n/).filter(Boolean).map((l) => JSON.parse(l)) : []
    check('a layer that needs the settings file in its tree runs, because the tree-maker wrote it', job.phase === 'done' && job.verdict === 'no new red', `${job.phase} ${job.verdict} ${job.error ?? ''}`)
    check('the measuring tree is made without hooks and the publish tree with them, both at the tip', made.length === 2 && made[0].hooks.join() === '-NoHusky' && made[1].hooks.length === 0 && made.every((m) => m.sha === tip), JSON.stringify(made))
}
{
    const s = scenario({ treeFails: "the local settings file has no 'sshHost'" })
    promote('start', s.args)
    const { job } = await until(s, ended)
    check('a tree-maker that fails ends the job in the tree phase with its message, not as a layer failure', job.phase === 'failed' && job.failedPhase === 'tree' && /sshHost/.test(job.error || '') && /measuring tree/.test(job.error || ''), `${job.phase} ${job.failedPhase} ${job.error}`)
    check('and nothing ran after it, the lock is free and the remote run is cleaned', !fs.existsSync(s.file('pids.jsonl')) && !job.phases?.measure && !fs.existsSync(path.join(s.state, 'promotion', 'lock')) && fs.existsSync(s.file('cleanup.log')))
}

// ---- 2. new reds: a timeout that fails again, and a plain new red
{
    const s = scenario({ rows: [...TWICE, row('p', 'brand_new', 'fail')] })
    promote('start', s.args)
    const { job } = await until(s, ended)
    check('a case that timed out twice (the runner re-ran it) and a plain new red are both new reds', job.verdict === 'new red' && job.newReds.includes('p brand_new') && job.newReds.includes('q slow'), `${job.verdict} ${JSON.stringify(job.newReds)} ${job.error ?? ''}`)
    check('new reds list the merges in range for bisecting', (job.mergesInRange || []).some((line) => line.includes('merge feature')), JSON.stringify(job.mergesInRange))
    const files = reportsOf(job)
    const record = files['promotion-5.record.json'] ? JSON.parse(fs.readFileSync(files['promotion-5.record.json'], 'utf8')) : { comparison: {}, timeouts_rerun: { still_red_not_timeout: [] } }
    check('the record of a promotion with new reds says so and its timeout is still red', record.comparison.new_reds === 2 && record.timeouts_rerun.still_red_not_timeout[0]?.case === 'q/slow' && /timed out again/.test(record.timeouts_rerun.still_red_not_timeout[0].reason), JSON.stringify(record.comparison))
}

// ---- 3. a --publish with a new red publishes nothing; with none, exactly the measured sha
{
    const s = scenario({ rows: TWICE })
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
    check('the recorded pid of an orphan is ended with its tree', alive(lonely.pid) && killJobProcesses(left).ended.includes(lonely.pid) && (await gone([lonely.pid])))
    const reused = spawn(process.execPath, ['-e', 'setTimeout(() => {}, 600000)'], { detached: true, stdio: 'ignore' })
    reused.unref()
    await sleep(500)
    const stale = { worker: null, children: [{ pid: reused.pid, start: '1999-01-01T00:00:00.0000000Z', label: 'layer' }] }
    check('a pid whose start time is not the recorded one is a different process and is left alone', killJobProcesses(stale).ended.length === 0 && alive(reused.pid))
    killProcessTree(reused)
}

// ---- 2b. every status but pass, skip and known is red
{
    const s = scenario({ rows: [...BASE_ROWS, row('p', 'odd', 'error'), row('p', 'crashed', 'crash')] })
    promote('start', s.args)
    const { job } = await until(s, ended)
    check('a status other than pass, skip and known counts as red', job.verdict === 'new red' && job.newReds.includes('p odd') && job.newReds.includes('p crashed'), `${job.verdict} ${JSON.stringify(job.newReds)} ${job.error ?? ''}`)
}

// ---- 2c. a reading that is not complete publishes nothing and names what is missing
{
    // The layer is cut short (exit 2: nothing started or the transcript is short) after two rows.
    const s = scenario({ rows: BASE_ROWS.slice(0, 2), layerExit: 2 })
    promote('start', [...s.args, '--publish'])
    const { job } = await until(s, ended)
    check('a layer that exits as cut short fails the job in the measure phase, with its exit code', job.phase === 'failed' && job.failedPhase === 'measure' && /exited 2/.test(job.error || ''), `${job.phase} ${job.failedPhase} ${job.error}`)
    check('and names every case of the previous reading that the partial summary lacks', ['p/known', 'q/skipped', 'q/slow'].every((name) => (job.error || '').includes(name)) && !(job.error || '').includes('p/ok,'), job.error)
    check('and records and publishes nothing, frees the lock and cleans the remote run', !job.files && !job.verdict && !fs.existsSync(s.file('published.json')) && !fs.existsSync(path.join(s.state, 'promotion', 'lock')) && fs.existsSync(s.file('cleanup.log')))
}
{
    // The layer exits 0 or 1 like a finished run, but a case of the previous reading is not in it.
    const s = scenario({ rows: BASE_ROWS.filter((r) => r.test !== 'skipped') })
    promote('start', [...s.args, '--publish'])
    const { job } = await until(s, ended)
    check('a finished layer whose reading lacks a case of the previous reading fails and names it', job.phase === 'failed' && /q\/skipped/.test(job.error || '') && !fs.existsSync(s.file('published.json')), `${job.phase} ${job.error}`)
    const t = scenario({ rows: BASE_ROWS.filter((r) => r.test !== 'skipped') })
    promote('start', [...t.args, '--accept-missing', 'q/skipped'])
    const accepted = await until(t, ended)
    const files = reportsOf(accepted.job)
    const record = files['promotion-5.record.json'] ? JSON.parse(fs.readFileSync(files['promotion-5.record.json'], 'utf8')) : {}
    check('a case deleted on purpose is let through when it is named, and the record lists it', accepted.job.phase === 'done' && (record.accepted_missing_cases || []).join() === 'q/skipped', `${accepted.job.phase} ${accepted.job.error}`)
}

// ---- 2d. a child that hangs past its limit ends the job in that phase, with no process left
{
    const s = scenario({ layerMs: 120000 })
    promote('start', [...s.args, '--layer-timeout', '3'])
    const pids = await waitForPids(s)
    const { job } = await until(s, ended)
    check('a layer past its limit fails the job in the measure phase, naming the limit', job.phase === 'failed' && job.failedPhase === 'measure' && /layer step ran past its limit of 3 s/.test(job.error || ''), `${job.phase} ${job.failedPhase} ${job.error}`)
    check('and the layer, its grandchild and the worker are gone, the lock is free', pids.length === 2 && (await gone([...pids, job.worker.pid])) && !fs.existsSync(path.join(s.state, 'promotion', 'lock')), `${JSON.stringify(pids)}`)
}
{
    const s = scenario({ treeHangs: true })
    promote('start', [...s.args, '--short-timeout', '3'])
    const { job } = await until(s, ended)
    check('a tree-maker past its limit fails the job in the tree phase, naming the limit', job.phase === 'failed' && job.failedPhase === 'tree' && /tree step ran past its limit of 3 s/.test(job.error || ''), `${job.phase} ${job.failedPhase} ${job.error}`)
    check('and nothing ran after it and the lock is free', !job.phases?.measure && !fs.existsSync(path.join(s.state, 'promotion', 'lock')))
}

// ---- 2e. a process whose start time was never recorded is not killed
{
    const lonely = spawn(process.execPath, ['-e', 'setTimeout(() => {}, 600000)'], { detached: true, stdio: 'ignore' })
    lonely.unref()
    await sleep(500)
    const unrecorded = { worker: null, children: [{ pid: lonely.pid, start: null, label: 'layer' }] }
    const outcome = killJobProcesses(unrecorded)
    check('a recorded pid with no start time is reported and left running', outcome.ended.length === 0 && outcome.unknown.includes(lonely.pid) && alive(lonely.pid), JSON.stringify(outcome))
    killProcessTree(lonely)
}

// ---- 2f. two contenders that both saw the same dead holder: exactly one takes the lock
{
    const lockDir = path.join(root, 'race-lock')
    fs.mkdirSync(lockDir, { recursive: true })
    fs.writeFileSync(path.join(lockDir, 'owner.json'), JSON.stringify({ jobId: 'dead', pid: 1 }))
    const holds = (holder) => holder.jobId !== 'dead'
    let second = null
    let nested = false
    // The first contender has read the dead holder; before it removes anything, the second
    // contender runs through its whole acquisition.
    const first = acquireLock(lockDir, { jobId: 'a', pid: 2 }, (holder) => {
        if (!nested) {
            nested = true
            second = acquireLock(lockDir, { jobId: 'b', pid: 3 }, holds)
        }
        return holds(holder)
    })
    const holder = JSON.parse(fs.readFileSync(path.join(lockDir, 'owner.json'), 'utf8'))
    check("of two contenders for a dead holder's lock only the one that removed it first holds it", second?.ok === true && first.ok === false && holder.jobId === 'b', `${JSON.stringify(first)} ${JSON.stringify(second)} ${JSON.stringify(holder)}`)
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
