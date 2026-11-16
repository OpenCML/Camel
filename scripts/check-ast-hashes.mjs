#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The AST snapshot against the tree, before the plan that reports it: every `.cml` the tree holds,
 * hashed by `camel-parsecheck hashes` -- the tool `frontend.ast_snapshot` itself runs -- with the
 * line `test/plans/feat/parse/golden/ast_hashes.txt` records for it compared with the reading the
 * file gives now.
 *
 * Why it exists: five merges have changed a `.cml` file and left that record stale, and every one
 * was found the same way. The twelve-target gate turned
 * `feat.parse.frontend | frontend.ast_snapshot` red on the base branch, and the merge position
 * patched the record on the spot (`1dad3104f` is the fifth: `4363 lines, 3 added, 3 removed`,
 * "test: register the three cases whose AST the 198 report changed"). The repair was never wrong.
 * What was missing was a reader that says so before the push: the plan's check runs after a build,
 * beside four hundred other cases, and its red names a target rather than the file the lane just
 * edited. This prints one line per file from a lane's own worktree, and it costs one process.
 *
 * The plan's check is not replaced and is not narrowed: this is the same reading, taken earlier.
 *
 * --- what is compared, and what is not --------------------------------------
 * A recorded line is `<16 hex> <ok|no> <path>`. The hash is a 64-bit FNV-1a over the normalized AST
 * with every node's token range and origin, or -- for a file the front end rejects -- over the
 * diagnostics' codes and places (`tools/parsecheck/snapshot.cpp`, `Diagnostics::dumpIdentity`);
 * `ok` and `no` say which of the two the line is about. So a comment is source text with a range
 * and rewording one moves the hash, while a diagnostic's wording is not recorded at all and
 * rewording a message leaves the line alone.
 *
 * This is not a content-hash check, and it must not become one. A content hash would be red for the
 * one change the record is right to ignore (a message's prose) and would still say nothing about
 * the case the record exists for -- an edit that moves the graph. There is no cheaper approximation
 * of "how does the front end read this file" than the front end, and the front end reads all 4,363
 * files the record covers in one process.
 *
 * --- the scope, counted rather than assumed ---------------------------------
 * The scope is the `.cml` files the tree holds -- the git index, plus the untracked files that are
 * not ignored, because a lane runs this *before* it commits and a case it has written is exactly
 * the one that lands unrecorded -- with `docs/handover` aside (`test/tools/parsecheck/corpus.py`,
 * `HANDOVER`: the author's drafts, not the repository's surface) and the same skipped directories
 * `corpus.py` walks past. The record's `probes/*` lines are the probes
 * `test/tools/parsecheck/probes.py` generates, so they are generated here too and hashed with the
 * rest; they are not files of the tree and nothing else could compare them.
 *
 * On a clean tree that is 4,136 files plus 227 probes, which is the record's own 4,363 lines. The
 * two definitions are not assumed to agree: a path the record holds and this scope does not is
 * reported (`gone`), and so is the reverse (`missing`), so a drift between `corpus.py`'s walk and
 * this one is a line a reader sees. A tracked `.cml` inside a skipped directory is counted and
 * named on its own line rather than dropped: a file that leaves the scope silently is a file
 * nobody checks.
 *
 * --- three kinds of stale, apart, because the repair differs ----------------
 *   mismatch  the file is in the tree and its recorded line differs from its reading now.
 *             `python3 test/tools/parsecheck/check.py snapshot --update --only '<pattern>'` for
 *             that file, never a bulk `--update` (agents/test.md).
 *   missing   the file is in the tree and the record holds no line for it: a new `.cml`, or one
 *             whose line `--update` never added. `snapshot --update` adds it. The plan is red on
 *             this too -- it asserts `0 not in the snapshot yet` -- but as a count, not as a path.
 *   gone      the record holds a line the tree does not: a `.cml` that was deleted, or a probe
 *             `probes.py` no longer makes. `snapshot --update` drops it, and until then the record
 *             claims a reading of a file the tree no longer has.
 *
 * A file the index holds and the worktree does not is *not* hashed, and it is the one place this
 * check answers a default instead of taking a reading: `tools/parsecheck/main.cpp#readFile` opens a
 * missing path and gets an empty string, the parser accepts an empty program, and the hash that
 * comes back describes nothing -- it would read as `mismatch` only because an empty program's hash
 * is unlikely to be the recorded one. So the scope is filtered by what is on disk, and the missing
 * paths are printed (`not on disk`). The recorded ones then land in `gone`, which is exactly what
 * the plan's own `check.py` calls `recorded file no longer exists`.
 *
 * --- a reading that could not be taken is never a green one ----------------
 * Exit 2, with what stopped it, for: no `camel-parsecheck` at the install named; a golden that is
 * not in the tree or holds a line that is not a snapshot line, or a path recorded twice; a
 * `probes.py` that fails or makes no probe; a tool that crashes, ends by a signal or outlives
 * `--timeout`; a line of its output that is not a reading, or an answer about a file this run did
 * not ask about; a scope that came out empty; and a file the tool could not read (it prints
 * `error <path>: ...` and goes on, and this refuses rather than reading the rest as clean). `0
 * stale` is a sentence about 4,363 files read, and a run that read fewer has no sentence to make --
 * the same rule `scripts/check-capture-census.mjs` states for its census, and the reason neither
 * tool accepts an empty scope.
 *
 * --- the install is named, not stamped -------------------------------------
 * `scripts/install-stamp.mjs` (`refuseInstallNotOfThisTree`) is the rule for a checker that reads
 * an install's operator registry, and it does not apply here. A build's fingerprint covers every
 * `.cml` of the tree (`scripts/common.js`, `sourceFingerprint`), so the one change this check exists
 * for is the one that makes an install "not this tree's build" -- the stamp cannot express "a
 * `.cml` edit moves the parser's reading, not its binary". The substitute is provenance, which is
 * what a reading that cannot be seen to come from the tree is missing: the first lines name the
 * golden, the `camel-parsecheck` that answered and when it was built. Two parsers that differ print
 * the same kind of line, so the line is what a reader compares when a reading looks wrong.
 *
 * --- usage -----------------------------------------------------------------
 *   node scripts/check-ast-hashes.mjs                      # this tree's out/latest
 *   node scripts/check-ast-hashes.mjs --camel out/latest/bin/camel
 *   node scripts/check-ast-hashes.mjs --timeout 1800000    # the tool's own clock, in ms
 *   node scripts/check-ast-hashes.mjs --python python3     # the interpreter probes.py needs
 *
 * Exit 0: every line as recorded. 1: stale lines, listed with the recorded and the current reading.
 * 2: a run that could not take a reading.
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { takeCamel } from '../test/tools/camel-arg.mjs'

const NAME = 'check-ast-hashes'
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const GOLDEN = 'test/plans/feat/parse/golden/ast_hashes.txt'
const PROBES = 'test/tools/parsecheck/probes.py'
/** The probe lines of the record are written under this prefix (`check.py`, `run_hashes`). */
const PROBE_PREFIX = 'probes/'

/** `corpus.HANDOVER`: the language author's drafts, not the repository's language surface. */
const HANDOVER = 'docs/handover/'

/**
 * `corpus.SKIP_DIRS` and `corpus.walk`'s `out_lr` prefix rule, kept with the same names: this scope
 * is only the snapshot's corpus if both walks agree on what that corpus is, and the `skipped` line
 * below is what makes a disagreement visible instead of silent.
 */
const SKIP_DIRS = new Set([
    '.git',
    'node_modules',
    'build',
    'out',
    'tmp',
    'third_party',
    'dist',
    'vendor',
    '.cache',
    '__pycache__',
    'tmp_q',
    'results'
])

/** A reading: `<16 hex> <ok|no> <path>` (`tools/parsecheck/snapshot.cpp`, `runHashes`). */
const READING = /^([0-9a-f]{16}) (ok|no) (.+)$/

function usage(message) {
    if (message) console.error(`[${NAME}] ${message}`)
    console.error(
        'usage: node scripts/check-ast-hashes.mjs [--camel <path>] [--timeout MS] [--python <exe>]'
    )
    process.exit(2)
}

/**
 * The run that could not take a reading: what stopped it, and exit 2 rather than a green.
 *
 * On stdout, like every other line this check prints. `internal/tools/camel-remote.ps1` merges a
 * plan's stderr on the host (`$runPlan 2>&1`, its header) because a plan's reason for being red
 * arrives there; a checker's is read from stdout, and a refusal written on stderr reaches a lane as
 * an exit code with no sentence after it -- which is the reading of a tool that broke, not the
 * reading of a run that refused.
 */
function refuse(lines) {
    for (const line of [].concat(lines)) console.log(`[${NAME}] ${line}`)
    console.log(`[${NAME}] refusing: a run that did not read the tree has no stale count to report`)
    process.exit(2)
}

const argv = process.argv.slice(2)
const taken = takeCamel(argv, ROOT, usage)
const camel = taken.camel
let timeout = 900000
let python = 'python3'
for (let i = 0; i < taken.rest.length; i++) {
    const arg = taken.rest[i]
    if (arg === '--timeout' || arg === '--python') {
        const value = taken.rest[++i]
        if (value === undefined || value === '') usage(`${arg} needs a value`)
        if (arg === '--timeout') {
            if (!/^\d+$/.test(value) || Number(value) === 0) usage('--timeout needs milliseconds')
            timeout = Number(value)
        } else python = value
    } else if (arg === '-h' || arg === '--help') usage()
    else usage(`unknown argument '${arg}'`)
}

/** The path as the record spells it: relative to the root, forward slashes. */
const shown = (file) => path.relative(ROOT, file).split(path.sep).join('/')

/** A path as the tool prints it back: forward slashes, absolute paths included. */
const forward = (file) => file.split(path.sep).join('/')

// --- the scope --------------------------------------------------------------
// `git ls-files` is the definition of "the files the tree holds": the index answers for what is
// committed and for what is staged, and `--others --exclude-standard` adds what a lane has written
// but not added yet. A failure to list is a refusal, not an empty scope.
function gitList(args) {
    const result = spawnSync('git', args, { cwd: ROOT, encoding: 'utf8', maxBuffer: 64 << 20 })
    if (result.error || result.status !== 0) {
        const detail = result.error ? result.error.message : `${result.stderr || ''}`.trim()
        refuse(`git ${args.join(' ')} failed (${result.status || 'no exit code'}): ${detail}`)
    }
    return result.stdout.split('\0').filter((each) => each !== '')
}

const listed = new Set([
    ...gitList(['ls-files', '-z', '--', '*.cml']),
    ...gitList(['ls-files', '-z', '--others', '--exclude-standard', '--', '*.cml'])
])

/** Is this path part of the corpus the snapshot records (`corpus.py`'s walk)? */
function inCorpus(rel) {
    if (rel.startsWith(HANDOVER)) return false
    return !rel
        .split('/')
        .some((part) => SKIP_DIRS.has(part) || part.startsWith('out_lr'))
}

const scope = []
const skipped = []
const absent = []
for (const rel of [...listed].sort()) {
    if (!inCorpus(rel)) {
        skipped.push(rel)
        continue
    }
    // The index holds a `.cml` the worktree does not: the file is deleted and the deletion is not
    // committed yet. It is not handed to the tool, because `tools/parsecheck/main.cpp#readFile`
    // reads a missing file as an empty string and the parser accepts an empty program -- a
    // reading of nothing that looks exactly like a reading. This is the one default in the chain
    // that would land on "clean", so it is answered here instead: see `gone` below.
    if (fs.existsSync(path.join(ROOT, rel))) scope.push(rel)
    else absent.push(rel)
}
if (scope.length === 0) {
    refuse(
        "the tree holds no .cml file in the snapshot's corpus; a check over nothing is not a pass"
    )
}

// --- the record -------------------------------------------------------------
let goldenText
try {
    goldenText = fs.readFileSync(path.join(ROOT, GOLDEN), 'utf8')
} catch (error) {
    refuse(`cannot read ${GOLDEN} (${error.message}): it is the record this check compares against`)
}
const recorded = new Map()
let lineNumber = 0
for (const line of goldenText.split('\n')) {
    lineNumber += 1
    if (line === '') continue
    const match = READING.exec(line)
    if (match === null) {
        refuse(`${GOLDEN}:${lineNumber}: not a snapshot line: ${JSON.stringify(line)}`)
    }
    if (recorded.has(match[3])) {
        refuse(`${GOLDEN}:${lineNumber}: ${match[3]} is recorded twice; one file has one reading`)
    }
    recorded.set(match[3], { hash: match[1], verdict: match[2] })
}
if (recorded.size === 0) {
    refuse(`${GOLDEN} holds no reading; a comparison against an empty record passes nothing`)
}
const recordedProbes = [...recorded.keys()].filter((each) => each.startsWith(PROBE_PREFIX)).length

// --- the readings -----------------------------------------------------------
// The probes are generated, not committed: `probes.py` writes them and this hashes what it wrote.
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-ast-hashes-'))
process.on('exit', () => {
    try {
        fs.rmSync(work, { recursive: true, force: true })
    } catch {
        // A temporary directory this run could not remove is the system's to clean; it decides
        // nothing about the reading.
    }
})

const probeRun = spawnSync(python, [PROBES, work], { cwd: ROOT, encoding: 'utf8' })
if (probeRun.error || probeRun.status !== 0) {
    const detail = probeRun.error ? probeRun.error.message : `${probeRun.stderr || ''}`.trim()
    refuse(`\`${python} ${PROBES}\` failed (${probeRun.status || 'no exit code'}): ${detail}`)
}
const probeFiles = fs
    .readdirSync(work)
    .filter((name) => name.endsWith('.cml'))
    .sort()
    .map((name) => path.join(work, name))
if (probeFiles.length === 0) {
    refuse(
        `\`${python} ${PROBES}\` wrote no probe, and the record holds ${recordedProbes} of them: ` +
            'the probes are part of the record this check compares against'
    )
}
// The probes are named by `probes/<file>` in the record and by an absolute path on disk; both come
// from the same list, so the two spellings are held together here and never recomputed.
const probeNames = new Map(
    probeFiles.map((file) => [file, `${PROBE_PREFIX}${path.basename(file)}`])
)

const parsecheck = path.join(
    path.dirname(camel),
    process.platform === 'win32' ? 'camel-parsecheck.exe' : 'camel-parsecheck'
)
if (!fs.existsSync(parsecheck)) {
    refuse(
        `no camel-parsecheck beside ${shown(camel)} (looked for ${shown(parsecheck)}): ` +
            'that binary is what reads a .cml here, and this install does not have it'
    )
}

// One list, one process: the tool hashes what the list names and prints one reading per file.
const listFile = path.join(work, 'files.txt')
const requested = [...scope, ...probeFiles]
fs.writeFileSync(listFile, requested.map((each) => forward(each)).join('\n') + '\n')

const started = Date.now()
const hashesRun = spawnSync(parsecheck, ['hashes', listFile], {
    cwd: ROOT,
    encoding: 'utf8',
    maxBuffer: 64 << 20,
    timeout
})
const seconds = ((Date.now() - started) / 1000).toFixed(1)
if (hashesRun.error) {
    const timedOut = hashesRun.error.code === 'ETIMEDOUT' || hashesRun.signal === 'SIGTERM'
    refuse(
        timedOut
            ? `${shown(parsecheck)} hashes did not answer within ${timeout} ms`
            : `${shown(parsecheck)} hashes could not start: ${hashesRun.error.message}`
    )
}
if (hashesRun.signal) {
    refuse(`${shown(parsecheck)} hashes was killed by ${hashesRun.signal} after ${seconds} s`)
}
if (hashesRun.status !== 0) {
    const detail = `${hashesRun.stdout || ''}${hashesRun.stderr || ''}`.trim()
    refuse([
        `${shown(parsecheck)} hashes exited ${hashesRun.status} after ${seconds} s`,
        ...detail.split(/\r?\n/).slice(0, 10).map((line) => `  ${line}`)
    ])
}

// The answer, keyed the way the record spells a path: a probe by `probes/<name>`, everything else
// by its path from the root. A line about a file this run did not name is refused rather than
// dropped: the list and its answers have to describe one set.
const scopeSet = new Set(scope)
const now = new Map()
for (const line of (hashesRun.stdout || '').split('\n')) {
    if (line === '') continue
    const match = READING.exec(line)
    if (match === null) {
        refuse(
            `${shown(parsecheck)} hashes printed a line that is not a reading: ` +
                JSON.stringify(line)
        )
    }
    const key = probeNames.get(path.resolve(match[3])) || match[3]
    if (!scopeSet.has(key) && !key.startsWith(PROBE_PREFIX)) {
        refuse(
            `hashes answered for ${match[3]}, which this run did not ask about; ` +
                'the list and its answers disagree'
        )
    }
    now.set(key, { hash: match[1], verdict: match[2] })
}
const unanswered = [...probeFiles.map((file) => probeNames.get(file)), ...scope].filter(
    (key) => !now.has(key)
)
if (unanswered.length > 0) {
    refuse([
        `${unanswered.length} of the ${requested.length} file(s) this run named got no reading: ` +
            'a comparison over a set it lost files from is not a comparison',
        ...unanswered.slice(0, 10).map((each) => `  ${each}`)
    ])
}

// --- the comparison ---------------------------------------------------------
const mismatch = []
const missing = []
for (const file of [...scope, ...probeFiles.map((each) => probeNames.get(each))]) {
    const was = recorded.get(file)
    const is = now.get(file)
    if (was === undefined) missing.push({ path: file, now: is })
    else if (was.hash !== is.hash || was.verdict !== is.verdict) {
        mismatch.push({ path: file, was, now: is })
    }
}
const gone = []
for (const [file, was] of recorded) {
    if (now.has(file)) continue
    const why = absent.includes(file)
        ? 'the index holds it, the worktree does not'
        : 'probes.py no longer makes it'
    gone.push({ path: file, was, why })
}
const stale = mismatch.length + missing.length + gone.length
const checked = now.size

// --- the reading ------------------------------------------------------------
const buildTime = (() => {
    try {
        return fs.statSync(parsecheck).mtime.toISOString().replace('T', ' ').slice(0, 19)
    } catch {
        return 'at an unknown time'
    }
})()
console.log(`[${NAME}] golden ${GOLDEN}: ${recorded.size} recorded line(s)`)
console.log(`[${NAME}] install ${shown(parsecheck)}, built ${buildTime}`)
console.log(
    `[${NAME}] scope ${scope.length} .cml file(s) of the tree + ${probeFiles.length} probe(s) ` +
        `from ${PROBES}`
)
// Two kinds of leftover path, and only one of them is expected: the handover directory is the
// documented exclusion (`corpus.HANDOVER`) and its count is enough, while a `.cml` the index holds
// and this scope leaves out for any other reason is a surprise and is named.
const handover = skipped.filter((file) => file.startsWith(HANDOVER))
const outside = skipped.filter((file) => !file.startsWith(HANDOVER))
if (handover.length > 0) {
    console.log(
        `[${NAME}] out of the corpus: ${handover.length} path(s) under ${HANDOVER} (the drafts)`
    )
}
if (outside.length > 0) {
    console.log(`[${NAME}] out of the corpus: ${outside.length} path(s) a skipped directory holds`)
    for (const file of outside.slice(0, 5)) console.log(`[${NAME}]   ${file}`)
    if (outside.length > 5) console.log(`[${NAME}]   ... and ${outside.length - 5} more`)
}
// Named and counted, and never hashed: a deleted file must not turn into an empty program's
// reading. A recorded one is `gone` below, which is the line that stops the run.
if (absent.length > 0) {
    console.log(
        `[${NAME}] not on disk: ${absent.length} tracked .cml path(s) the worktree does not have`
    )
    for (const file of absent.slice(0, 5)) console.log(`[${NAME}]   ${file}`)
    if (absent.length > 5) console.log(`[${NAME}]   ... and ${absent.length - 5} more`)
}

if (stale > 0) {
    // Three columns, class first: a reader pasting one of these lines into a report says both what
    // is wrong and what the repair is.
    const rows = [mismatch, missing, gone].flat()
    const width = Math.min(Math.max(...rows.map((each) => each.path.length)), 72)
    const row = (kind, each) => {
        const was = each.was ? `${each.was.hash} ${each.was.verdict}` : '-'
        const is = each.now ? `${each.now.hash} ${each.now.verdict}` : `- (${each.why})`
        return (
            `${kind.padEnd(8)} ${each.path.padEnd(width)}  ` +
            `recorded ${was.padEnd(19)}  now ${is}`
        )
    }
    for (const each of mismatch) console.log(row('mismatch', each))
    for (const each of missing) console.log(row('missing', each))
    for (const each of gone) console.log(row('gone', each))
    console.log(
        `ast hashes: ${checked} file(s) checked, ${stale} stale ` +
            `(${mismatch.length} mismatch, ${missing.length} missing, ${gone.length} gone)`
    )
    console.log(
        `${NAME}: ${stale} line(s) of ${GOLDEN} do not describe this tree. If the change is ` +
            'intended, re-record it: `python3 test/tools/parsecheck/check.py snapshot --update ' +
            "--only '<pattern>'` for a file, or `camel-remote.ps1 -Plan test/plans/feat/parse " +
            '-UpdateAstHashes` on the host for the whole record -- and never a bulk `--update` ' +
            'for one case (agents/test.md).'
    )
    process.exit(1)
}

console.log(`ast hashes: ${checked} file(s) checked, 0 stale`)
process.exit(0)