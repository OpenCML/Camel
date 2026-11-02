// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Before treating a red as a new defect: has somebody already fixed it on another branch?
 *
 * Twice in one night the answer was yes and the branch was one `git log` away: `grad_array_sum` died
 * of SIGSEGV and a lane was about to write a probe for it, when the fix had been sitting on
 * `origin/lane-lit2` (`19f0b05e8`, `0aed45eb6`) since before the report; and a `GUARD` question was
 * answered the same way. The rule is in `agents/test.md`; this is the rule as a tool, so it stops
 * depending on who remembers it.
 *
 *   node scripts/find-fix.mjs <plan-file>:<case-name>[,<keyword>...] [--repo <dir>] [--offline]
 *
 * Three readings, one per block:
 *
 *   [1] remote branches that changed the plan file since `origin/develop` -- someone edited this plan
 *   [2] commits on any remote branch whose message carries a keyword (the case name is one when no
 *       keyword is given), each with the branches that contain it and whether it is already merged
 *   [3] the merged question, per commit: `origin/develop` contains it, or it does not
 *
 * Exit codes say which of three situations a reader is in, because "found nothing" and "found
 * something" cannot share a status:
 *
 *   0  at least one candidate (a branch touched the plan, or a message matched)
 *   1  no candidates at all -- this is (so far) a new defect, not someone else's fix
 *   2  no trustworthy reading: not a git repository, bad arguments, or the fetch failed. A stale
 *      `refs/remotes` reports a false "no branch has it", which is the failure that cost a night:
 *      "those four commits are on no remote branch" was true of the cache and false of the server.
 *
 * It writes nothing: no files, no commits, no index or working-tree changes. `git fetch` moves
 * remote-tracking refs, which is the point of block 1 -- pass `--offline` to skip it and be told the
 * reading may be stale.
 */

import { spawnSync } from 'node:child_process'
import path from 'node:path'
import url from 'node:url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..')

function die(code, message) {
    console.error(message)
    process.exit(code)
}

function usage(message) {
    if (message) console.error(`[usage] ${message}`)
    console.error('usage: node scripts/find-fix.mjs <plan-file>:<case-name>[,<keyword>...] [--repo <dir>] [--offline]')
    process.exit(2)
}

function git(repo, args, { allowFailure = false } = {}) {
    const result = spawnSync('git', ['-C', repo, ...args], { encoding: 'utf8', timeout: 120000 })
    if (result.error) {
        if (allowFailure) return null
        die(2, `[error] git ${args.join(' ')} could not run: ${result.error.message}`)
    }
    if (result.status !== 0) {
        if (allowFailure) return null
        die(2, `[error] git ${args.join(' ')} failed: ${(result.stderr || '').trim()}`)
    }
    return result.stdout
}

function parseTarget(raw) {
    // The plan path may be a Windows path (`D:\...`), so the split is at the LAST colon: a case name
    // does not contain one and a drive letter comes first.
    const cut = raw.lastIndexOf(':')
    if (cut <= 0 || cut === raw.length - 1) usage(`expected <plan-file>:<case-name>[,<keyword>...], got '${raw}'`)
    const planFile = raw.slice(0, cut)
    const rest = raw.slice(cut + 1).split(',').map((part) => part.trim()).filter((part) => part !== '')
    if (rest.length === 0) usage(`no case name after '${planFile}:'`)
    const [caseName, ...keywords] = rest
    return { planFile, caseName, keywords }
}

const argv = process.argv.slice(2)
let offline = false
let showAll = false
let repo = REPO_ROOT
let target = null
for (let index = 0; index < argv.length; index++) {
    const arg = argv[index]
    if (arg === '--offline') {
        offline = true
    } else if (arg === '--all') {
        showAll = true
    } else if (arg === '--repo') {
        repo = argv[++index]
        if (!repo) usage('--repo needs a directory')
    } else if (arg.startsWith('--')) {
        usage(`unknown option '${arg}'`)
    } else if (target === null) {
        target = parseTarget(arg)
    } else {
        usage(`unexpected argument '${arg}'`)
    }
}
if (target === null) usage('no <plan-file>:<case-name> given')

// The tree this reading is about, resolved by git, so a relative plan path is resolved the same way
// the runner resolves it.
const top = git(repo, ['rev-parse', '--show-toplevel'], { allowFailure: true })
if (top === null) die(2, `[error] ${repo} is not a git repository, so there are no remote branches to read`)
const root = top.trim()
// A relative plan path belongs to the repository being read, not to the caller's directory: with
// `--repo <somewhere>` the two differ, and resolving against the caller looked for the plan outside
// the tree and reported a cheerful zero.
const planPath = path.isAbsolute(target.planFile)
    ? target.planFile
    : path.relative(root, path.resolve(root, target.planFile)).split(path.sep).join('/')

const develop = 'origin/develop'
if (git(root, ['rev-parse', '--verify', '--quiet', develop], { allowFailure: true }) === null) {
    die(2, `[error] ${develop} does not exist here: without it there is no "since develop" and no "is it merged"`)
}

if (!offline) {
    const fetched = git(root, ['fetch', '--quiet', '--all', '--prune'], { allowFailure: true })
    if (fetched === null) {
        die(
            2,
            '[error] git fetch failed, so refs/remotes may be stale and this reading could report a ' +
                'false "no branch has it" -- the exact failure that cost a night. Fix the remote, or ' +
                'rerun with --offline and read the result as stale.',
        )
    }
} else {
    console.log('[stale] --offline: refs/remotes was not fetched; a branch that exists only on the server looks absent here.')
}

const branches = git(root, ['for-each-ref', '--format=%(refname:short)', 'refs/remotes'])
    .split('\n')
    .map((line) => line.trim())
    .filter((line) => line !== '' && !line.endsWith('/HEAD'))

// The case name is always searched -- a commit that names the case IS a candidate, keyword or no --
// and the extra words widen the net. The name is what ranks first below.
const keywords = [target.caseName, ...target.keywords.filter((word) => word !== target.caseName)]

console.log(`[provenance] repo ${root} | develop ${git(root, ['rev-parse', '--short', develop]).trim()}`)
console.log(`[target] plan ${planPath} | case ${target.caseName} | keywords ${keywords.join(', ')}`)

// ---- block 1: branches that changed the plan file since develop --------------------------------
// `-S` (a literal pickaxe) and not just the path: a branch that edited another entry of the same
// plan file is not a candidate for *this* case, and counting it would answer "someone changed the
// file" to a question about one entry. The file-level count is printed beside the block so the
// context is not lost.
/**
 * The `[[tests]]` blocks of a plan file, keyed by the entry's `name`. Comparing the block before and
 * after a commit answers the question a reader actually has -- "did this commit change *my* entry?"
 * -- which the file path cannot (another entry moved) and a pickaxe over the case name cannot (an
 * added pin or a rewritten note does not repeat the name).
 */
function entryBlocks(text) {
    const blocks = new Map()
    let name = null
    let lines = []
    const flush = () => {
        if (name !== null) blocks.set(name, lines.join('\n'))
        name = null
        lines = []
    }
    for (const line of (text || '').split('\n')) {
        if (/^\s*\[\[tests\]\]/.test(line)) {
            flush()
            lines = [line]
            continue
        }
        if (name === null) {
            const match = /^\s*name\s*=\s*"([^"]*)"/.exec(line)
            if (match) name = match[1]
        }
        lines.push(line)
    }
    flush()
    return blocks
}

function fileAt(repo, revision, file) {
    return git(repo, ['show', `${revision}:${file}`], { allowFailure: true })
}

const touching = []
const fileTouchers = []
for (const branch of branches) {
    if (branch === develop) continue
    const commits = (git(root, ['log', '--format=%H %h %s', `${develop}..${branch}`, '--', planPath], { allowFailure: true }) || '')
        .split('\n')
        .map((line) => line.trim())
        .filter(Boolean)
    if (commits.length > 0) fileTouchers.push(branch)
    const changed = []
    for (const line of commits) {
        const [sha, short, ...subject] = line.split(' ')
        const after = fileAt(root, sha, planPath)
        if (after === null) continue
        const before = fileAt(root, `${sha}^`, planPath)
        const afterBlocks = entryBlocks(after)
        const beforeBlocks = entryBlocks(before || '')
        if (!afterBlocks.has(target.caseName) && !beforeBlocks.has(target.caseName)) continue
        if (afterBlocks.get(target.caseName) === beforeBlocks.get(target.caseName)) continue
        changed.push(`${short} ${subject.join(' ')}`)
    }
    if (changed.length > 0) touching.push({ branch, count: changed.length, commits: changed.slice(0, 5) })
}touching.sort((a, b) => b.count - a.count || a.branch.localeCompare(b.branch))

console.log('')
console.log(
    `[1] remote branches that changed this entry in the plan file since ${develop}: ${touching.length}` +
        (fileTouchers.length > 0
            ? `  (branches that touched the file at all: ${fileTouchers.length} -- ${fileTouchers.join(', ')})`
            : '  (and no branch touched the file at all)'),
)
for (const entry of touching) {
    console.log(`    ${entry.branch}  (${entry.count} commit(s), not merged)`)
    for (const commit of entry.commits) console.log(`        ${commit}`)
    if (entry.count > entry.commits.length) console.log(`        ... ${entry.count - entry.commits.length} more`)
}

// ---- block 2: commits whose message carries a keyword -------------------------------------------
const seen = new Map()
for (const keyword of keywords) {
    const output = git(root, ['log', '--remotes', '--no-merges', '-i', `--grep=${keyword}`, '--format=%H\t%h\t%ct\t%s', '-n', '400'])
    for (const line of output.split('\n')) {
        const [sha, short, when, ...subject] = line.split('\t')
        if (!sha || !short) continue
        const entry = seen.get(sha) || { sha, short, at: Number(when) || 0, subject: subject.join('\t'), keywords: [] }
        if (!entry.keywords.includes(keyword)) entry.keywords.push(keyword)
        seen.set(sha, entry)
    }
}

const hits = []
for (const entry of seen.values()) {
    const containing = git(root, ['branch', '-r', '--contains', entry.sha], { allowFailure: true })
    const holderBranches = (containing || '')
        .split('\n')
        .map((line) => line.replace(/^\*/, '').trim())
        .filter((line) => line !== '' && !line.endsWith('/HEAD'))
    const merged = git(root, ['merge-base', '--is-ancestor', entry.sha, develop], { allowFailure: true }) !== null
    hits.push({ ...entry, branches: holderBranches, merged })
}

console.log('')
console.log(`[2] commits whose message carries ${keywords.map((k) => `'${k}'`).join(' or ')} (merged commits included): ${hits.length}`)
for (const hit of hits) {
    const where = hit.branches.length > 0 ? hit.branches.join(', ') : '(no remote branch holds it)'
    console.log(`    ${where} | ${hit.short} | ${hit.merged ? 'merged' : 'not merged'} | ${hit.subject}`)
}

// ---- block 3: the merged question, as a count --------------------------------------------------
const mergedHits = hits.filter((hit) => hit.merged).length
console.log('')
console.log(`[3] already on ${develop}: ${mergedHits} of ${hits.length} matching commit(s), ${hits.length - mergedHits} not merged`)

const candidates = touching.length + hits.length
if (candidates === 0) {
    console.log('')
    console.log(
        `[no candidates] no remote branch changed ${planPath} since ${develop} and no commit message carries ` +
            `${keywords.map((k) => `'${k}'`).join(' or ')}: so far this is a new defect, not someone else's fix ` +
            '(exit 1 -- an empty reading is not a pass)',
    )
    process.exit(1)
}
console.log('')
console.log(
    `[candidates] ${touching.length} branch(es) changed the plan, ${hits.length} commit(s) matched; ` +
        `${mergedHits} already merged into ${develop} (exit 0)`,
)
process.exit(0)