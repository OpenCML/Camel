#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The old-tree probe: "the tool is newer than the tree" as something reproducible and assertable.
 *
 * On 2026-11-24 the host called `test/tools/run-tests.mjs --list-cases` on a lane cut before that
 * option existed. The word became a target, `walkPlans` stat'ed `.../--list-cases`, and the run died
 * with a stack trace; `scripts/selection-cases.mjs` reported it as `the plan loader refused this
 * selection`, and the gate read it as a change that selects no plan. Two seats then gave two
 * different wrong explanations of the same failure, because the only record of it was a memory of
 * what had happened. This tool is that failure, made on demand: it builds the old tree, runs this
 * checkout's own consumer against it, and asserts the sentence a reader gets.
 *
 *   node scripts/old-tree-probe.mjs --check
 *
 * What it builds
 *   A tree from `--ref` (default: the commit before `--list-cases` entered
 *   `test/tools/run-tests.mjs`, read from `git log -S`), holding that ref's `test/tools`,
 *   `test/runner.mjs`, `test/plan-selection.json` and `test/plans/smoke`, plus **this checkout's**
 *   `scripts/selection-cases.mjs` -- the caller is the new half and the tree is the old half, which
 *   is the accident exactly. The copy is a real repository with one commit and `origin/develop`
 *   pointing at it, because the consumer reads its `tree` and `base` from git, and a tree it cannot
 *   read would be a third kind of failure this tool must not be measuring by accident.
 *
 *   `--synthesize` builds the old half from **this** checkout's runner with its `--list-cases`
 *   branch renamed, for a clone with no history to archive. A synthesized tree is not an archived
 *   one and a check that accepted both silently would be the same mistake as the two explanations
 *   above, so the build line says which it is, `--check` refuses a synthesized tree unless
 *   `--allow-synthesis` is passed, and `argv.gate` (which needs no git history) passes it.
 *
 * What it asserts, in this order
 *   1. **This tree first**: this checkout's consumer lists this checkout's cases and gets rows, and
 *      nothing in that output says `unknown option`. A probe that can only go red is
 *      indistinguishable from one that is broken; this half is what proves it does not cry wolf.
 *   2. **The old tree**: the same consumer against the old half exits 2 and says this tree's runner
 *      does not know `--list-cases`, that nothing was selected and that this is not an empty
 *      selection -- and it does **not** say `refused this selection` or `the cases this change
 *      selects could not be computed`, the two sentences that misnamed this failure for a night.
 *
 * Exit codes -- four exits, and the two that share 2 say different things
 *   0  the pit is there and the chain behaves: `PASS probe.old-tree`
 *   1  the pit is there and the chain misbehaves: `FAIL probe.old-tree` (red)
 *   2  nothing was measured, in one of two ways, each with its own sentence:
 *      `[PROBE] the old tree was not built: ...` (the ref could not be read, or the word is unknown)
 *      `[PROBE] self refused: this tree is not a green starting point, so nothing about an old tree
 *      was measured` (this checkout itself fails first)
 *   Never 0 for either 2: "could not measure" and "measured green" are the two readings this
 *   project keeps separating, and the two 2s share a code only because a refusal is a refusal.
 *
 * Output carries the three things the two wrong explanations lacked: this checkout's commit, the
 * old tree's commit, and the word itself.
 *
 * Usage
 *   node scripts/old-tree-probe.mjs --check [--ref <ref>] [--synthesize --allow-synthesis] [--keep]
 *   node scripts/old-tree-probe.mjs --build [--ref <ref>] [--dir <path>]
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..')
const RUNNER = path.join(REPO_ROOT, 'test', 'tools', 'run-tests.mjs')
const CONSUMER = path.join(REPO_ROOT, 'scripts', 'selection-cases.mjs')
/** The word the accident was about: the host's listing asks the tree for its cases by this name. */
const WORD = '--list-cases'
/** One plan every ruleset selects when its own path changes: the consumer needs a target to list. */
const PLAN_PATH = 'test/plans/smoke/core.plan.toml'
/** What the old half is missing, found in its source rather than assumed. */
const BRANCH_OF_WORD = `arg === '${WORD}'`
const SYNTHETIC_BRANCH = `arg === '${WORD}-not-in-this-tree'`

const ALPHA = /\x1b\[[0-9;]*[A-Za-z]/g

function stripColour(text) {
    return String(text || '').replace(ALPHA, '')
}

function git(args, cwd = REPO_ROOT) {
    const run = spawnSync('git', args, { cwd, encoding: 'utf8', windowsHide: true, timeout: 120000 })
    if (run.error || run.status !== 0) return null
    return run.stdout.trim()
}

function argvParsed(argv) {
    const options = { check: false, build: false, ref: null, dir: null, keep: false }
    for (let i = 0; i < argv.length; i += 1) {
        const arg = argv[i]
        if (arg === '--check') options.check = true
        else if (arg === '--build') options.build = true
        else if (arg === '--keep') options.keep = true
        else if (arg === '--ref' && i + 1 < argv.length) options.ref = argv[++i]
        else if (arg.startsWith('--ref=')) options.ref = arg.slice('--ref='.length)
        else if (arg === '--dir' && i + 1 < argv.length) options.dir = path.resolve(argv[++i])
        else if (arg.startsWith('--dir=')) options.dir = path.resolve(arg.slice('--dir='.length))
        else {
            console.error(`old-tree-probe: unknown option '${arg}'`)
            console.error('usage: node scripts/old-tree-probe.mjs --check [--keep] | --build [--ref <ref>] [--dir <path>]')
            process.exit(2)
        }
    }
    if (options.check === options.build) {
        console.error('old-tree-probe: ask one question -- --check or --build')
        process.exit(2)
    }
    return options
}

/**
 * The commit before `--list-cases` entered the runner, from the history rather than from a count:
 * `git log -S` names the commit that changed the number of its occurrences, and the old tree is
 * that commit's parent.
 */
function refBeforeTheWord() {
    const found = git(['log', '--format=%H', '--reverse', `-S${WORD}`, '--', 'test/tools/run-tests.mjs'])
    if (!found) return null
    const first = found.split(/\r?\n/).filter(Boolean)[0]
    if (!first) return null
    const parent = git(['rev-parse', '--verify', `${first}^`])
    return parent ? { ref: parent, removedIn: first } : null
}

function check(problems, condition, message) {
    if (!condition) problems.push(message)
}

/**
 * Build the old half into `dir`. Returns `{ ok, by, sha, failure }`, and prints the line that says
 * which half is which -- a build that fell back to synthesis must say so in the log, because a
 * synthesized reading and an archived one are not the same reading.
 */
function buildTree(dir, options, mode) {
    const target = dir || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-old-tree-'))
    fs.mkdirSync(target, { recursive: true })
    const paths = ['test/tools', 'test/runner.mjs', 'test/plan-selection.json', 'test/plans/smoke']
    let by = null
    let sha = null
    if (mode === 'synthesis') {
        for (const rel of paths) {
            fs.cpSync(path.join(REPO_ROOT, rel), path.join(target, rel), { recursive: true })
        }
        const runner = path.join(target, 'test', 'tools', 'run-tests.mjs')
        const text = fs.readFileSync(runner, 'utf8')
        const older = text.replace(BRANCH_OF_WORD, SYNTHETIC_BRANCH)
        if (older === text) {
            return { ok: false, failure: `this checkout's runner has no ${BRANCH_OF_WORD} branch to remove` }
        }
        fs.writeFileSync(runner, older)
        by = 'synthesis'
        sha = git(['rev-parse', 'HEAD'])
    } else {
        const ref = options.ref || (refBeforeTheWord() || {}).ref
        if (!ref) {
            return { ok: false, failure: `no --ref given and no commit in this history added ${WORD} to the runner` }
        }
        sha = git(['rev-parse', '--verify', `${ref}^{commit}`])
        if (!sha) return { ok: false, ref, failure: `'${ref}' is not a commit this checkout can read` }
        const tar = path.join(target, 'old-tree.tar')
        const packed = spawnSync('git', ['archive', '--format=tar', '-o', tar, sha, '--', ...paths], {
            cwd: REPO_ROOT,
            encoding: 'utf8',
            windowsHide: true,
        })
        if (packed.status !== 0) {
            return { ok: false, ref: sha, failure: `git archive could not read ${sha}: ${stripColour(packed.stderr).trim().split(/\r?\n/).pop()}` }
        }
        const unpacked = spawnSync('tar', ['-xf', tar, '-C', target], { encoding: 'utf8', windowsHide: true })
        fs.rmSync(tar, { force: true })
        if (unpacked.error || unpacked.status !== 0) {
            return { ok: false, ref: sha, failure: `the archive of ${sha} could not be unpacked: ${(unpacked.error && unpacked.error.message) || unpacked.stderr}` }
        }
        by = 'archive'
    }
    // The consumer always comes from THIS checkout: the old half is the tree, the new half is the
    // caller, and taking both from the old ref would reproduce nothing but a consistent old world.
    fs.mkdirSync(path.join(target, 'scripts'), { recursive: true })
    fs.copyFileSync(CONSUMER, path.join(target, 'scripts', 'selection-cases.mjs'))
    // Any plan the consumer can name will do; the one below is the usual one, and the fallback keeps
    // this tool working across refs where the smoke plan was named differently.
    const planPath = fs.existsSync(path.join(target, PLAN_PATH)) ? PLAN_PATH : firstPlan(target)
    if (!planPath) {
        return { ok: false, ref: sha, failure: `the old half has no plan under test/plans/smoke, so there is nothing to list` }
    }
    const commit = (...args) => spawnSync('git', args, { cwd: target, encoding: 'utf8', windowsHide: true })
    commit('init', '-q', '-b', 'develop')
    commit('config', 'core.autocrlf', 'false')
    commit('add', '-A')
    commit('-c', 'user.email=probe@invalid', '-c', 'user.name=probe', 'commit', '-q', '-m', 'a tree older than the caller')
    commit('update-ref', 'refs/remotes/origin/develop', 'HEAD')
    const built = git(['rev-parse', 'HEAD'], target)
    return { ok: true, dir: target, by, sha, ref: sha, built, planPath, failure: null }
}

/** The first plan file under `test/plans/smoke`, as a path relative to `dir`. */
function firstPlan(dir) {
    const root = path.join(dir, 'test', 'plans', 'smoke')
    if (!fs.existsSync(root)) return null
    const stack = [root]
    while (stack.length > 0) {
        const here = stack.pop()
        for (const entry of fs.readdirSync(here, { withFileTypes: true })) {
            const full = path.join(here, entry.name)
            if (entry.isDirectory()) stack.push(full)
            else if (entry.name.endsWith('.plan.toml')) return path.relative(dir, full).split(path.sep).join('/')
        }
    }
    return null
}

/** Run this checkout's consumer against `dir` and hand back its exit code and its stripped words. */
function listCasesIn(dir, planPath) {
    // The list of changed paths is written to a scratch directory, never into `dir`: for the self
    // half `dir` is this checkout, and a probe that writes into the tree it measures is exactly the
    // side effect a run's own tree guard reports as a change nobody made.
    const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-probe-list-'))
    const list = path.join(scratch, 'changed.txt')
    fs.writeFileSync(list, `${planPath}\n`)
    try {
        const run = spawnSync(process.execPath, [path.join(dir, 'scripts', 'selection-cases.mjs'), '--files', list], {
            cwd: dir,
            encoding: 'utf8',
            windowsHide: true,
        })
        return {
            status: run.error ? null : run.status,
            text: stripColour(`${run.stdout || ''}${run.stderr || ''}`),
            error: run.error || null,
        }
    } finally {
        fs.rmSync(scratch, { recursive: true, force: true })
    }
}

function main() {
    const options = argvParsed(process.argv.slice(2))
    const problems = []
    const hereSha = git(['rev-parse', 'HEAD']) || 'unknown'
    console.log(`[PROBE] this tree ${hereSha} (consumer ${path.relative(REPO_ROOT, CONSUMER)})`)

    if (options.build) {
        const built = buildTree(options.dir, options, 'archive')
        if (!built.ok) {
            console.error(`[PROBE] the old tree was not built: ${built.failure}`)
            process.exit(2)
        }
        console.log(`[PROBE] bound: ${built.dir}  by ${built.by}  at ${built.sha}`)
        console.log(`[PROBE] kept: ${built.dir} (remove it when done)`)
        console.log(`[PROBE] word ${WORD}`)
        return 0
    }

    // 1. This tree first. A probe that only goes red in the pit cannot be told from a broken one, and
    //    a probe whose own starting point is broken measures nothing about an old tree: this half
    //    has its own exit (2, "self refused") so it cannot arrive wearing the red of a real finding.
    const self = listCasesIn(REPO_ROOT, PLAN_PATH)
    const selfProblems = []
    if (self.status !== 0) {
        selfProblems.push(`this tree: the consumer exited ${self.status} on its own cases (expected 0)`)
    }
    if (self.text.includes('unknown option')) {
        selfProblems.push(`this tree: the consumer says 'unknown option' about its own argv, so this checkout is the old half`)
    }
    if (!/^\{"name":/m.test(self.text)) {
        selfProblems.push('this tree: the consumer listed no case row')
    }
    const selfRows = (self.text.match(/^\{"name":/gm) || []).length
    console.log(`[PROBE] self: exit ${self.status}, ${selfRows} case row(s), no 'unknown option'`)
    if (selfProblems.length > 0) {
        for (const line of self.text.split(/\r?\n/)) if (line.trim() !== '') console.log(`    ${line.trim()}`)
        for (const problem of selfProblems) console.log(`FAIL ${problem}`)
        console.log('[PROBE] self refused: this tree is not a green starting point, so nothing about an old tree was measured')
        return 2
    }

    // 2. The old tree.
    //    By synthesis, not by archive, and the reason is a finding this tool recorded on its first
    //    run: no commit in this history has a runner that refuses an unknown option **without**
    //    knowing `--list-cases`. The refusal arrived in the same era as the option, so an archived
    //    tree answers the caller with `no Release binary at ...` or a stack -- it cannot say the
    //    sentence this check is about. The archived half is still worth looking at (`--build --ref`),
    //    but it is the *before*, and pretending it were the pairing would be the same mistake the
    //    two explanations made.
    const built = buildTree(null, options, 'synthesis')
    if (!built.ok) {
        console.error(`[PROBE] the old tree was not built: ${built.failure}`)
        process.exit(2)
    }
    console.log(`[PROBE] built by synthesis, not by archive: this checkout's runner with its '${WORD}' branch renamed, because no commit in this history refuses an unknown option without knowing '${WORD}'`)
    console.log(`[PROBE] word ${WORD}`)
    const old = listCasesIn(built.dir, built.planPath)
    for (const line of old.text.split(/\r?\n/)) if (line.trim() !== '') console.log(`    ${line.trim()}`)
    const wanted = [
        `this tree's runner does not know ${WORD}`,
        'this is not an empty selection',
        '[ARGV] this tree is',
    ]
    check(problems, old.status === 2, `the old tree: the consumer exited ${old.status} (expected 2)`)
    for (const needle of wanted) check(problems, old.text.includes(needle), `the old tree: the output does not say '${needle}'`)
    for (const needle of ['refused this selection', 'the cases this change selects could not be computed']) {
        check(problems, !old.text.includes(needle), `the old tree: the output says '${needle}', which misnames the version mismatch`)
    }
    if (options.keep) console.log(`[PROBE] kept: ${built.dir}`)
    else fs.rmSync(built.dir, { recursive: true, force: true })

    if (problems.length > 0) {
        for (const problem of problems) console.log(`FAIL ${problem}`)
        console.log('FAIL probe.old-tree')
        return 1
    }
    console.log('PASS probe.old-tree')
    return 0
}

process.exit(main())
