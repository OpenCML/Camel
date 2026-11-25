// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * What the readings cache may answer with, and what it must never answer with.
 *
 * `scripts/merge-readings.mjs --read` stores one plan-set reading of one tree and replays it. The
 * rule is the one the backlog entry (C-1124-131) states: the key contains the identity of the thing
 * cached -- the tree OID -- plus everything else a reading depends on (the plan selection, the jobs
 * width, the tier, and the tool that produced it). So this file pins down:
 *
 *   1. the second call with the same key does NOT run the plans again, and says in its output that
 *      the reading came from the cache, naming the full tree OID;
 *   2. every key component is load-bearing: changing jobs, changing the plan selection, changing one
 *      byte of the tree, or changing the tool makes the next call a miss;
 *   3. identity is the commit, not the directory and not a file mtime: a clone of the same tree
 *      hits, and touching a file without changing its content still hits;
 *   4. a dirty working tree is neither served from the cache nor stored in it.
 *
 * The runner is a stand-in (`--runner` takes a Node script) so that this runs without the build
 * host. It logs every invocation: "the plans ran again" is read as "the runner was started", which
 * is the only evidence that can tell a replay from a measurement.
 */
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { execFileSync, spawnSync } from 'node:child_process'

const here = path.dirname(new URL(import.meta.url).pathname).replace(/^\/([A-Za-z]:)/, '$1')
const tool = path.join(here, '..', '..', 'scripts', 'merge-readings.mjs')
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'merge-readings-cache-'))
const repo = path.join(root, 'tree')
const clone = path.join(root, 'clone')
const cache = path.join(root, 'cache')
const logFile = path.join(root, 'runner.log')
const fake = path.join(root, 'fake-runner.mjs')

let checks = 0
let failures = 0

function check(label, ok, detail = '') {
    checks += 1
    if (ok) {
        console.log(`  PASS  ${label}`)
    } else {
        failures += 1
        console.log(`  FAIL  ${label}`)
        if (detail) console.log(`        ${String(detail).split('\n').slice(0, 10).join('\n        ')}`)
    }
}

function write(file, text) {
    fs.mkdirSync(path.dirname(file), { recursive: true })
    fs.writeFileSync(file, text)
    return file
}

function git(cwd, ...args) {
    return execFileSync('git', ['-C', cwd, ...args], { encoding: 'utf8' }).trim()
}

/** The runner's stand-in: it says what it was asked, prints a reading, and records that it ran. */
write(
    fake,
    `import fs from 'node:fs'
import { execFileSync } from 'node:child_process'
const argv = process.argv.slice(2)
const flag = (name) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : null }
const tree = flag('-Path')
const plans = []
for (let i = 0; i < argv.length; i += 1) if (argv[i] === '-Plan') plans.push(argv[i + 1])
const jobs = Number(flag('-Jobs') || 8)
const tier = flag('-Tier') || 'release'
const oid = execFileSync('git', ['-C', tree, 'rev-parse', 'HEAD'], { encoding: 'utf8' }).trim()
if (process.env.FAKE_RUNNER_LOG) {
    fs.appendFileSync(process.env.FAKE_RUNNER_LOG, JSON.stringify({ oid, plans, jobs, tier }) + '\\n')
}
const rows = plans.map((p) => ({
    name: p.replace(/^test\\/plans\\//, '').replace(/\\//g, '.') + '/only',
    verdict: 'pass',
    reading: null,
    reading_version: 1,
}))
process.stdout.write('[fake-runner] a plan run happened: tree ' + oid + ', jobs ' + jobs + ', tier ' + tier + '\\n')
process.stdout.write('[provenance] tip ' + oid + ' | tier ' + tier + ' | jobs ' + jobs + '\\n')
process.stdout.write('__CAMEL_REMOTE_ROWS_START__\\n' + rows.map((r) => JSON.stringify(r)).join('\\n') + '\\n__CAMEL_REMOTE_ROWS_END__\\n')
process.stdout.write('[SUMMARY] total: ' + rows.length + ', pass: ' + rows.length + ', fail: 0\\n')
process.exit(Number(process.env.FAKE_RUNNER_EXIT || 0))
`,
)

// --- the fixture tree: three plans, one commit -------------------------------
write(path.join(repo, 'README.md'), 'the fixture tree\n')
write(path.join(repo, 'test/plans/alpha/a.plan.toml'), 'id = "alpha.plan"\n\n[[tests]]\nname = "only"\n')
write(path.join(repo, 'test/plans/beta/b.plan.toml'), 'id = "beta.plan"\n\n[[tests]]\nname = "only"\n')
write(path.join(repo, 'test/plans/gamma/g.plan.toml'), 'id = "gamma.plan"\n\n[[tests]]\nname = "only"\n')
git(repo, 'init', '-q')
git(repo, 'config', 'user.email', 'test@example.invalid')
git(repo, 'config', 'user.name', 'cache test')
git(repo, 'add', '-A')
git(repo, 'commit', '-q', '-m', 'fixture')
const firstOid = git(repo, 'rev-parse', 'HEAD')

const plansAlpha = write(path.join(root, 'plans-alpha.txt'), 'test/plans/alpha\n')
const plansBeta = write(path.join(root, 'plans-beta.txt'), 'test/plans/beta\n')

function runs() {
    return fs.existsSync(logFile) ? fs.readFileSync(logFile, 'utf8').split('\n').filter(Boolean).length : 0
}

function read(args, extraEnv = {}) {
    const res = spawnSync(process.execPath, [tool, '--read', '--runner', fake, ...args], {
        cwd: repo,
        encoding: 'utf8',
        env: { ...process.env, FAKE_RUNNER_LOG: logFile, ...extraEnv },
    })
    return { code: res.status ?? -1, text: `${res.stdout ?? ''}${res.stderr ?? ''}` }
}

function entries() {
    return fs.existsSync(cache) ? fs.readdirSync(cache).filter((n) => n.endsWith('.json')) : []
}

const base = ['--tree-path', repo, '--plans', plansAlpha, '--jobs', '8', '--readings-cache', cache]

// --- 1: the cold reading runs the plans; the warm one does not --------------
const cold = read(base)
check('a cold --read is a miss and measures', cold.code === 0 && /readings-cache: MISS/.test(cold.text), cold.text)
check('the cold reading carries the runner output and its row block', /\[SUMMARY\] total: 1, pass: 1, fail: 0/.test(cold.text) && /__CAMEL_REMOTE_ROWS_START__/.test(cold.text), cold.text)
check('the cold reading names the tree it measured', cold.text.includes(firstOid), cold.text)
check('the runner was started exactly once', runs() === 1, `runs=${runs()}`)
check('the cold reading is stored', entries().length === 1 && fs.existsSync(path.join(cache, 'MANIFEST.md')), entries().join(', '))

const warm = read(base)
check('the same key is a hit', warm.code === 0 && /readings-cache: HIT/.test(warm.text), warm.text)
check('the hit says the reading came from the cache', /this reading came from the cache, not from a run now/.test(warm.text), warm.text)
check('the hit names the cached tree OID in full', warm.text.includes(firstOid) && /\[readings-cache\] hit tree=[0-9a-f]{40}/.test(warm.text), warm.text)
check('the hit replays the same reading', /alpha\/only/.test(warm.text) && /\[SUMMARY\] total: 1, pass: 1, fail: 0/.test(warm.text), warm.text)
check('the plans did NOT run again on a hit', runs() === 1, `runs=${runs()}`)

// --- 2: identity is the commit, not the mtime and not the path --------------
const readme = path.join(repo, 'README.md')
const now = new Date()
fs.utimesSync(readme, now, now)
const touched = read(base)
check('touching a file (mtime only) still hits', touched.code === 0 && /readings-cache: HIT/.test(touched.text) && runs() === 1, touched.text)

git(root, 'clone', '-q', repo, clone)
const cloned = read(['--tree-path', clone, '--plans', plansAlpha, '--jobs', '8', '--readings-cache', cache])
check('the same commit at another path is the same tree', cloned.code === 0 && /readings-cache: HIT/.test(cloned.text) && runs() === 1, cloned.text)

// --- 3: every key component is load-bearing ---------------------------------
const jobs4 = read(['--tree-path', repo, '--plans', plansAlpha, '--jobs', '4', '--readings-cache', cache])
check('changing jobs misses', jobs4.code === 0 && /readings-cache: MISS/.test(jobs4.text) && runs() === 2, jobs4.text)
const jobs4Again = read(['--tree-path', repo, '--plans', plansAlpha, '--jobs', '4', '--readings-cache', cache])
check('the jobs-4 reading is then a hit of its own', /readings-cache: HIT/.test(jobs4Again.text) && runs() === 2, jobs4Again.text)

const beta = read(['--tree-path', repo, '--plans', plansBeta, '--jobs', '8', '--readings-cache', cache])
check('changing the plan selection misses', beta.code === 0 && /readings-cache: MISS/.test(beta.text) && runs() === 3, beta.text)
const betaAgain = read(['--tree-path', repo, '--plans', plansBeta, '--jobs', '8', '--readings-cache', cache])
check('the beta reading is then a hit of its own', /readings-cache: HIT/.test(betaAgain.text) && runs() === 3, betaAgain.text)

// one byte in the tree: a new commit, so a new tree OID
fs.appendFileSync(path.join(repo, 'README.md'), 'x')
git(repo, 'commit', '-q', '-am', 'one byte')
const movedOid = git(repo, 'rev-parse', 'HEAD')
const sizeBefore = Number(git(repo, 'cat-file', '-s', `${firstOid}:README.md`))
const sizeAfter = Number(git(repo, 'cat-file', '-s', `${movedOid}:README.md`))
check('the fixture really moved by one byte', movedOid !== firstOid && sizeAfter - sizeBefore === 1, `${movedOid} ${sizeBefore} -> ${sizeAfter}`)
const moved = read(base)
check('a tree one byte different misses', moved.code === 0 && /readings-cache: MISS/.test(moved.text) && runs() === 4, moved.text)
check('the miss is keyed by the new OID, not the old one', moved.text.includes(movedOid), moved.text)
const movedAgain = read(base)
check('the moved reading is then a hit of its own', /readings-cache: HIT/.test(movedAgain.text) && runs() === 4, movedAgain.text)

git(repo, 'reset', '--hard', '-q', firstOid)
const back = read(base)
check('the tree the first reading was of hits again after the reset', /readings-cache: HIT/.test(back.text) && runs() === 4, back.text)

// --- 4: a dirty tree is neither served nor stored ---------------------------
const before = entries().length
fs.appendFileSync(path.join(repo, 'README.md'), 'uncommitted')
const dirty = read(base)
check('a dirty tree bypasses the cache', /readings-cache: bypass/.test(dirty.text) && /dirty/.test(dirty.text), dirty.text)
check('a dirty tree is measured, not served', runs() === 5 && /a plan run happened/.test(dirty.text), `runs=${runs()}\n${dirty.text}`)
check('a dirty tree stores nothing', /nothing stored/.test(dirty.text) && entries().length === before, `${entries().length} vs ${before}`)
git(repo, 'checkout', '--', 'README.md')
const clean = read(base)
check('the clean tree hits the entry it had before the dirty read', /readings-cache: HIT/.test(clean.text) && runs() === 5, clean.text)

// --- 5: a red reading is cached with its exit code --------------------------
const redArgs = ['--tree-path', repo, '--plans', plansAlpha, '--jobs', '3', '--readings-cache', cache]
const red = read(redArgs, { FAKE_RUNNER_EXIT: '1' })
check('a red reading exits with the runner exit code', red.code === 1 && /readings-cache: MISS/.test(red.text), red.text)
check('the red reading is stored', runs() === 6 && entries().length === before + 1, `runs=${runs()}, entries=${entries().length}`)
const redAgain = read(redArgs, { FAKE_RUNNER_EXIT: '1' })
check('a red reading is replayed with the same exit code', redAgain.code === 1 && /readings-cache: HIT/.test(redAgain.text) && runs() === 6, redAgain.text)

// --- 6: the tool that produced the reading is part of the key ---------------
fs.appendFileSync(fake, '\n// one byte of tool change\n')
const newerTool = read(base)
check('a changed runner is a changed tool, so a miss', /readings-cache: MISS/.test(newerTool.text) && runs() === 7, newerTool.text)
const newerToolAgain = read(base)
check('the new tool then hits its own entry', /readings-cache: HIT/.test(newerToolAgain.text) && runs() === 7, newerToolAgain.text)

// --- 7: the tier is a key component too -------------------------------------
// A debug reading is a reading of another install; the same tree, plans and jobs do not make it the
// same reading.
const debug = read(['--tree-path', repo, '--plans', plansAlpha, '--jobs', '8', '--tier', 'debug', '--readings-cache', cache])
check('changing the tier misses', debug.code === 0 && /readings-cache: MISS/.test(debug.text) && runs() === 8, debug.text)
check('the debug reading was taken with the debug tier', /tier debug/.test(debug.text) && /\[provenance\].*tier debug/.test(debug.text), debug.text)
const debugAgain = read(['--tree-path', repo, '--plans', plansAlpha, '--jobs', '8', '--tier', 'debug', '--readings-cache', cache])
check('the debug reading is then a hit of its own', /readings-cache: HIT/.test(debugAgain.text) && runs() === 8, debugAgain.text)

// --- 8: a cache inside the tree is refused ----------------------------------
const insideRun = read(['--tree-path', repo, '--plans', plansAlpha, '--jobs', '8', '--readings-cache', path.join(repo, '.cache')])
check('a cache directory inside the measured tree is refused', insideRun.code === 2 && /inside this work tree/.test(insideRun.text), insideRun.text)

console.log('')
if (failures > 0) {
    console.log(`merge-readings cache test: ${checks} checks, ${failures} failed`)
    process.exit(1)
}
console.log(`merge-readings cache test: ${checks} checks, 0 failed`)
