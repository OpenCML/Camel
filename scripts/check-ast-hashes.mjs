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
 * patched the record on the spot (the fifth: `4363 lines, 3 added, 3 removed`,
 * "test: register the three cases whose AST the 198 report changed"). The repair was never wrong.
 * What was missing was a reader that says so before the push: the plan's check runs after a build,
 * beside four hundred other cases, and its red names a target rather than the file the lane just
 * edited. This prints one line per file from a lane's own worktree, in the time the fast layer
 * allows a case (see "the whole corpus in a few processes" below), so the same command is a `fast`
 * test of `feat/parse/frontend.plan.toml` (`frontend.ast_record`) and not only a command a lane has
 * to remember.
 *
 * The plan's check is not replaced and is not narrowed: this is the same reading, taken earlier.
 *
 * --- what is compared, and what is not --------------------------------------
 * A recorded line is `<16 hex> <ok|no> <path>`. The hash is a 64-bit FNV-1a over the normalized AST
 * with every node's token range and origin, or -- for a file the front end rejects -- over the
 * diagnostics' codes and places (`test/tools/parsecheck/snapshot.cpp`, `Diagnostics::dumpIdentity`);
 * `ok` and `no` say which of the two the line is about. So a comment is source text with a range
 * and rewording one moves the hash, while a diagnostic's wording is not recorded at all and
 * rewording a message leaves the line alone.
 *
 * This is not a content-hash check, and it must not become one. A content hash would be red for the
 * one change the record is right to ignore (a message's prose) and would still say nothing about
 * the case the record exists for -- an edit that moves the graph. There is no cheaper approximation
 * of "how does the front end read this file" than the front end.
 *
 * --- the whole corpus in a few processes ------------------------------------
 * The front end reads all 4,546 files the record covers, and one `hashes` process reads them in
 * 1.4-2.1 s on the build host under its usual load (measured 2026-11-25, two runs). 2 s is the line
 * the fast layer draws (`docs/test-plans.md`: a case that costs more goes to `slow`), so the list is
 * read by `--jobs` processes at once -- four by default -- and the answers are merged: the wall
 * clock is the slowest chunk's, 0.5 s measured, and the comparison sees one map as before. Nothing
 * else about the reading depends on the split -- the same command, one list per chunk, one reading
 * per file -- and `--jobs 1` is the whole corpus in one process for a reading that wants to suspect
 * the split.
 *
 * --- the scope, counted rather than assumed ---------------------------------
 * The scope is the `.cml` files the tree holds -- the git index, plus the untracked files that are
 * not ignored, because a lane runs this *before* it commits and a case it has written is exactly
 * the one that lands unrecorded -- with `internal` aside (`test/tools/parsecheck/corpus.py`,
 * `HANDOVER`: the author's drafts, not the repository's surface) and the same skipped directories
 * `corpus.py` walks past. The record's `probes/*` lines are the probes
 * `test/tools/parsecheck/probes.py` generates, so they are generated here too and hashed with the
 * rest; they are not files of the tree and nothing else could compare them.
 *
 * On a clean tree that is 4,256 files plus 227 probes, which is the record's own 4,483 lines. The
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
 * check answers a default instead of taking a reading: `test/tools/parsecheck/main.cpp#readFile` opens a
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
 * stale` is a sentence about 4,483 files read, and a run that read fewer has no sentence to make --
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
 * --- the narrow scope: the .cml files a merge changes ----------------------
 * A merge lane does not want all 4,483 lines; it wants the six or the one it just edited, and it
 * wants the answer in a second. `--base REF` narrows the scope to the corpus `.cml` files this tree
 * changed against `REF` -- `git diff --name-only REF...HEAD` (committed) plus `git diff --name-only
 * HEAD` (staged and unstaged) plus the untracked files, so a lane's uncommitted case is in it -- and
 * the comparison is then made over those rows alone:
 *
 *     node scripts/check-ast-hashes.mjs --base origin/develop     # the seventh gate
 *
 * The narrow run refuses to be mistaken for the wide one: it prints how many `.cml` files the diff
 * named, how many of the record's lines it read, and it does not walk the record for the rows the
 * tree does not hold (that is a claim about 4,483 lines, and this run read six). The `probes/*` rows
 * are not read either, and this is the one deliberate omission: a probe moves when the parser moves,
 * not when a `.cml` moves, so a diff of `.cml` files cannot make one stale -- and hashing them would
 * spend the run's budget on the rows its criterion cannot be about. A diff that names no `.cml` of
 * the corpus is the gate's own pass: a merge that changes no `.cml` cannot leave this record stale,
 * and the run says so in that sentence rather than printing a `0 stale` that would read as a
 * reading of the tree. `--base` with an unreadable REF is a refusal (exit 2), never a silent fall
 * back to the whole tree: a narrowed gate that widens itself on a typo is the one failure a reader
 * cannot see in the output. A remote-tracking name this checkout does not have -- `origin/develop`
 * on a machine whose only remote is a staging repository -- is the one case that is not a typo: the
 * branch part is tried, then `HEAD~1`, and the ref it settled on is printed with its sha before
 * anything is compared, so a fallback is read in the output rather than inferred from a diff that
 * looks short. When nothing resolves, the refusal names what this checkout can resolve and the next
 * step (`--base <the fork point's sha>`, or `git fetch <remote>`) instead of relaying git's
 * `bad revision`.
 *
 * --- --update: the re-record, through `check.py snapshot` -------------------
 * The hash is not computed here and a second implementation of it never will be: `--update` hands
 * the work to `python3 test/tools/parsecheck/check.py snapshot --update --only <glob>` and prints
 * what that prints, then reads the golden back and compares the same scope again, so one command
 * both re-records and shows the result. The globs are the `--only` values given; with `--base` and
 * none given they are the changed files themselves, because "re-record what this diff touched" is
 * the whole intent of the merge-time run. A `.cml` the diff deletes is passed as `--drop`: the
 * record may lose a row for a file the tree no longer has, and that is the deletion `check.py`'s
 * guard allows with a name and counts out loud.
 *
 * `--only <glob>` measurably restricts the *files* it re-hashes and not the run: `check.py snapshot`
 * hashes all 227 probes on every invocation (`probes.py`), so `--update --only
 * test/cases/parse/constructs/enum.cml` re-hashes 228 lines and rewrites the whole table from them
 * -- it replaces one row and keeps the rest, and it does not read one file. Measured on the build
 * host on 2026-11-22: `wrote ... re-hashed 228 line(s), kept 4255 recorded line(s) of the 4483`
 * (228 = the 227 probes + the one file; the row count changed by exactly one, `git diff --numstat`
 * read `1 1` -- the globs narrow what is re-hashed, not what is written).
 * That is the semantics inherited from `check.py`, and it is why `--update` here always names its
 * globs instead of re-recording the tree.
 *
 * --- usage -----------------------------------------------------------------
 *   node scripts/check-ast-hashes.mjs                        # this tree's out/latest, whole corpus
 *   node scripts/check-ast-hashes.mjs --base origin/develop  # the .cml files this tree changed
 *   node scripts/check-ast-hashes.mjs --base origin/develop --update
 *   node scripts/check-ast-hashes.mjs --update --only '<glob>'   # one row, through check.py
 *   node scripts/check-ast-hashes.mjs --camel out/latest/bin/camel
 *   node scripts/check-ast-hashes.mjs --timeout 1800000      # the tool's own clock, in ms
 *   node scripts/check-ast-hashes.mjs --python python3       # the interpreter probes.py needs
 *
 * `--only` is repeatable and needs `--update` (it names what to re-record; the comparison is the
 * whole tree, or the diff with `--base`). The three readings are narrow, wide, and re-record:
 * `--base` is what a merge lane runs before pushing, the whole corpus is what the plan eventually
 * runs, and `--update` is the repair.
 *
 * Exit 0: every line read as recorded (or, with `--base`, no changed `.cml` to read).
 * 1: stale lines, listed with the recorded and the current reading.
 * 2: a run that could not take a reading.
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawn, spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { takeCamel } from '../test/tools/camel-arg.mjs'

const NAME = 'check-ast-hashes'
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const GOLDEN = 'test/plans/feat/parse/golden/ast_hashes.txt'
const PROBES = 'test/tools/parsecheck/probes.py'
/** The probe lines of the record are written under this prefix (`check.py`, `run_hashes`). */
const PROBE_PREFIX = 'probes/'

/** `corpus.HANDOVER`: the language author's drafts, not the repository's language surface. */
const HANDOVER = 'internal/'

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

/** A reading: `<16 hex> <ok|no> <path>` (`test/tools/parsecheck/snapshot.cpp`, `runHashes`). */
const READING = /^([0-9a-f]{16}) (ok|no) (.+)$/

function usage(message) {
    if (message) console.error(`[${NAME}] ${message}`)
    console.error(
        'usage: node scripts/check-ast-hashes.mjs [--base <ref>] [--update [--only <glob>]...]\n' +
            '                                     [--camel <path>] [--timeout MS] [--python <exe>]\n' +
            '                                     [--jobs N]\n' +
            '  --base <ref>   read only the corpus .cml files this tree changed against <ref>\n' +
            '                 (git diff <ref>...HEAD, the working tree, the untracked files)\n' +
            '  --jobs N       read the list in N `hashes` processes at once (default 4, or the\n' +
            '                 machine\'s count when smaller; `--jobs 1` is one process)\n' +
            '  --update       re-record through `check.py snapshot --update --only <glob>`;\n' +
            '                 --only narrows the *files* re-hashed, not the run: check.py hashes\n' +
            '                 all 227 probes every time, so one file re-hashes 228 lines'
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
/**
 * How many `hashes` processes read the corpus at once: four, or the machine's own count when it is
 * smaller. The corpus is 4,319 files, and one process reads them in 1.4-2.1 s on the build host
 * under its usual load -- over the 2 s the fast layer (`docs/test-plans.md`) allows a
 * case of its own, and the reason this check could only be read a layer down. `--jobs 1` is the
 * corpus in one process, and is what a reading that suspects the split should use.
 */
let jobs = Math.max(
    1,
    Math.min(4, os.availableParallelism ? os.availableParallelism() : os.cpus().length)
)
/** The ref this run narrows to, or null for the whole corpus. */
let base = null
let update = false
const only = []
for (let i = 0; i < taken.rest.length; i++) {
    const arg = taken.rest[i]
    if (
        arg === '--timeout' ||
        arg === '--python' ||
        arg === '--base' ||
        arg === '--only' ||
        arg === '--jobs'
    ) {
        const value = taken.rest[++i]
        if (value === undefined || value === '') usage(`${arg} needs a value`)
        if (arg === '--timeout') {
            if (!/^\d+$/.test(value) || Number(value) === 0) usage('--timeout needs milliseconds')
            timeout = Number(value)
        } else if (arg === '--base') base = value
        else if (arg === '--only') only.push(value)
        else if (arg === '--jobs') {
            if (!/^\d+$/.test(value) || Number(value) === 0) usage('--jobs needs a positive integer')
            jobs = Number(value)
        } else python = value
    } else if (arg === '--update') update = true
    else if (arg === '-h' || arg === '--help') usage()
    else usage(`unknown argument '${arg}'`)
}
// `--only` names what to re-record, so it has no meaning without `--update`: reading it as "compare
// these rows" would answer a narrower question than the flag reads, and a comparison against a
// subset of the record is exactly the reading a lane would quote as "the tree is green".
if (only.length > 0 && !update) {
    usage(
        '--only names the files to re-record; give --update, or use --base to narrow the comparison'
    )
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

/**
 * The refs a `--base` that does not resolve here is answered with, in the order they are read:
 * first what this checkout calls the same history (`origin/develop` is `develop` on a machine that
 * has no such remote-tracking ref), then the commit before HEAD, which is where a one-commit lane
 * was cut. The refusal below lists whichever of them resolve, because a reader who cannot resolve
 * one name needs to see the names that work.
 */
const BASE_CANDIDATES = ['origin/develop', 'develop', 'FETCH_HEAD', 'HEAD~1']

/** What `ref` resolves to as a commit in this checkout, or `null` when it does not. */
function refCommit(ref) {
    const result = spawnSync('git', ['rev-parse', '--verify', '--quiet', `${ref}^{commit}`], {
        cwd: ROOT,
        encoding: 'utf8'
    })
    if (result.error || result.status !== 0) return null
    const sha = `${result.stdout || ''}`.trim()
    return sha === '' ? null : sha
}

/**
 * Answers what `--base` names, before any diff is taken, and says which ref it decided on.
 *
 * A checkout that fetches one lane from a staging repository holds that lane's commits and no
 * `origin/develop`, so `git diff origin/develop...HEAD` died inside git as
 * `fatal: bad revision 'origin/develop...HEAD'` -- a sentence about git rather than about the next
 * step. On such a machine the same history is called `develop`, and `HEAD~1` is where a one-commit
 * lane was cut, so a remote-tracking name that does not resolve is tried that way; every fallback
 * is printed with the ref that was asked for, the ref that is used and that ref's sha, so the scope
 * measured below is never a different diff than the run says it read. A name nothing resolves
 * (`--base nosuchref`) is still a refusal -- never a silent widening to the whole corpus -- and it
 * lists what this checkout can resolve plus the two ways on: the fork point's sha, or the fetch
 * that makes the name exist here.
 */
function resolveBase(requested) {
    const asked = refCommit(requested)
    if (asked !== null) return { ref: requested, sha: asked }
    const slash = requested.indexOf('/')
    if (slash > 0 && slash < requested.length - 1) {
        const remote = requested.slice(0, slash)
        const branch = requested.slice(slash + 1)
        for (const fallback of [branch, 'HEAD~1']) {
            const sha = refCommit(fallback)
            if (sha === null) continue
            console.log(
                `[${NAME}] --base ${requested}: this checkout has no ${requested} ` +
                    `(\`git rev-parse --verify --quiet ${requested}^{commit}\` answered nothing)`
            )
            console.log(
                `[${NAME}]   using ${fallback} (${sha.slice(0, 9)}) as the diff base -- ` +
                    `the scope below is against ${fallback}, ` +
                    (fallback === 'HEAD~1'
                        ? 'which holds the last commit only, not the branch this one was cut from'
                        : `not against ${requested}; this fallback is printed, never silent`)
            )
            console.log(
                `[${NAME}]   to narrow against ${requested} itself: \`git fetch ${remote}\` so the ` +
                    'name exists here, or name the fork point by sha: `--base <sha>`'
            )
            return { ref: fallback, sha }
        }
    }
    const here = BASE_CANDIDATES.map((name) => ({ name, sha: refCommit(name) })).filter(
        (each) => each.sha !== null
    )
    refuse([
        `--base ${requested}: this checkout has no ref ${requested} ` +
            `(\`git rev-parse --verify --quiet ${requested}^{commit}\` answered nothing), so there ` +
            'is no diff to narrow to',
        here.length === 0
            ? `nothing here resolves either: ${BASE_CANDIDATES.join(', ')} all answered nothing`
            : 'what this checkout can resolve: ' +
              here.map((each) => `${each.name} ${each.sha.slice(0, 9)}`).join(', '),
        'next: name the commit your branch was cut from (`--base <that sha>`); if ' +
            `${requested} is a remote branch, run \`git fetch <remote>\` first so the name exists ` +
            'here' +
            (here.some((each) => each.name === 'HEAD~1')
                ? '; `--base HEAD~1` reads the last commit only'
                : '')
    ])
}

/**
 * The `.cml` files this tree changed against `ref`: the committed ones (`ref...HEAD`), the working
 * tree's (`HEAD`, which is staged plus unstaged) and the untracked ones -- a lane runs this before
 * it commits, so a case it has just written is exactly the one that is unrecorded, and a diff of
 * committed work alone would miss it. `gitList` refuses on a failed command, so a `--base` naming an
 * unreadable ref stops the run (exit 2) instead of falling back to the whole corpus; `resolveBase`
 * above is what turns that refusal into the ref this machine actually has.
 */
function changedAgainst(ref) {
    const names = new Set()
    const add = (args) => {
        for (const rel of gitList(args)) if (rel.endsWith('.cml')) names.add(rel)
    }
    add(['diff', '--name-only', '-z', `${ref}...HEAD`, '--', '*.cml'])
    add(['diff', '--name-only', '-z', 'HEAD', '--', '*.cml'])
    return [...names].sort()
}

/** The diff this run narrows to, or `null` for the whole corpus. */
if (base !== null) base = resolveBase(base).ref
const changed = base === null ? null : changedAgainst(base)

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
for (const rel of changed === null ? [...listed].sort() : changed) {
    if (!inCorpus(rel)) {
        skipped.push(rel)
        continue
    }
    // The index holds a `.cml` the worktree does not: the file is deleted and the deletion is not
    // committed yet. It is not handed to the tool, because `test/tools/parsecheck/main.cpp#readFile`
    // reads a missing file as an empty string and the parser accepts an empty program -- a
    // reading of nothing that looks exactly like a reading. This is the one default in the chain
    // that would land on "clean", so it is answered here instead: see `gone` below.
    if (fs.existsSync(path.join(ROOT, rel))) scope.push(rel)
    else absent.push(rel)
}
if (scope.length === 0 && changed !== null) {
    // The narrow run's own pass, in its own words: "no changed .cml" is the criterion's answer and
    // not a reading of the record, and the sentence says which of the two this is -- a `0 stale`
    // printed here would be a claim about 4,483 lines that this run never opened.
    console.log(
        `[${NAME}] ${changed.length} .cml file(s) changed against ${base}, ` +
            `0 of them in the snapshot's corpus (${skipped.length} out of it, e.g. the drafts)`
    )
    console.log(
        `[${NAME}] nothing for this gate to read: a merge that changes no .cml of the corpus cannot ` +
            'leave the record stale, and no line of it was compared'
    )
    process.exit(0)
}
if (scope.length === 0) {
    refuse(
        "the tree holds no .cml file in the snapshot's corpus; a check over nothing is not a pass"
    )
}

// --- the record -------------------------------------------------------------
/** The recorded lines, keyed by path; a bad line or a repeated path stops the run. */
function readRecord() {
    let goldenText
    try {
        goldenText = fs.readFileSync(path.join(ROOT, GOLDEN), 'utf8')
    } catch (error) {
        refuse(
            `cannot read ${GOLDEN} (${error.message}): it is the record this check compares against`
        )
    }
    const lines = new Map()
    let lineNumber = 0
    for (const line of goldenText.split('\n')) {
        lineNumber += 1
        if (line === '') continue
        const match = READING.exec(line)
        if (match === null) {
            refuse(`${GOLDEN}:${lineNumber}: not a snapshot line: ${JSON.stringify(line)}`)
        }
        if (lines.has(match[3])) {
            refuse(`${GOLDEN}:${lineNumber}: ${match[3]} is recorded twice; one file has one reading`)
        }
        lines.set(match[3], { hash: match[1], verdict: match[2] })
    }
    if (lines.size === 0) {
        refuse(`${GOLDEN} holds no reading; a comparison against an empty record passes nothing`)
    }
    return lines
}
let recorded = readRecord()

// --- the re-record, and only through `check.py` -----------------------------
// The hash is not computed here: this hands the re-record to the tool that owns it and prints what
// it prints. What it then does by itself is the comparison below, over the same scope, so one
// command both records and reads the result.
const CHECK = 'test/tools/parsecheck/check.py'
if (update) {
    if (changed === null && only.length === 0) {
        // The bulk form, refused at the door. `check.py` allows it only with `--drop`, and it is
        // what took 4,400 lines to 4,393 on 2026-11-17: a tree that is missing another lane's
        // `.cml` files rewrites their rows away, and the loss surfaces a layer later as
        // `frontend.ast_snapshot` red on a tree nobody touched (`agents/test.md`).
        refuse([
            '--update without --only re-records the whole record, which is the bulk form: a tree ' +
                "missing another lane's .cml files rewrites their rows away (agents/test.md).",
            'Name the rows (--only <glob>, repeatable) or narrow the run (--base <ref>, which ' +
                're-records exactly the .cml files the diff names). The whole-record re-record is ' +
                '`camel-remote.ps1 -Plan test/plans/feat/parse -UpdateAstHashes` on the host.'
        ])
    }
    const globs = only.length > 0 ? only : scope
    if (globs.length === 0) {
        refuse('--update has nothing to re-record: the diff names no corpus .cml file')
    }
    // A row whose file the diff deletes is the one loss a re-record is allowed to make, and
    // `check.py` only makes it when the path is named (`--drop`): a wildcard-free glob cannot match
    // a file the tree no longer has, so without this the re-record would keep a row for a case that
    // is gone. Named, printed, and passed on -- never inferred inside `check.py`'s guard.
    const drops = absent.filter((rel) => recorded.has(rel))
    const args = [CHECK, 'snapshot', '--update']
    for (const glob of globs) args.push('--only', glob)
    for (const rel of drops) args.push('--drop', rel)
    console.log(
        `[${NAME}] re-record: ${python} ${args.join(' ')}` +
            (drops.length > 0 ? ` (${drops.length} row(s) the diff deletes)` : '')
    )
    const recordedRun = spawnSync(python, args, {
        cwd: ROOT,
        encoding: 'utf8',
        maxBuffer: 64 << 20
    })
    const text = `${recordedRun.stdout || ''}${recordedRun.stderr || ''}`.trimEnd()
    for (const line of text.split(/\r?\n/)) if (line !== '') console.log(`[${CHECK}] ${line}`)
    if (recordedRun.error) {
        refuse(`\`${python} ${CHECK}\` could not start: ${recordedRun.error.message}`)
    }
    if (recordedRun.status !== 0) {
        refuse(
            `\`${python} ${CHECK} snapshot --update\` exited ${recordedRun.status} and nothing is ` +
                're-recorded; its refusal above is the reading'
        )
    }
    recorded = readRecord()
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

// The probes are read with the whole corpus and not with a diff. They are programs `probes.py`
// writes, so they move when the parser's decisions move -- never because a `.cml` of the tree was
// edited -- and a narrow run that hashed 227 of them would spend a merge lane's second on the rows
// its criterion cannot be about. The count is printed below either way, so a reader of a narrow run
// knows which of the record's 4,483 lines it did not open.
let probeFiles = []
if (changed === null) {
    const probeRun = spawnSync(python, [PROBES, work], { cwd: ROOT, encoding: 'utf8' })
    if (probeRun.error || probeRun.status !== 0) {
        const detail = probeRun.error ? probeRun.error.message : `${probeRun.stderr || ''}`.trim()
        refuse(`\`${python} ${PROBES}\` failed (${probeRun.status || 'no exit code'}): ${detail}`)
    }
    probeFiles = fs
        .readdirSync(work)
        .filter((name) => name.endsWith('.cml'))
        .sort()
        .map((name) => path.join(work, name))
    if (probeFiles.length === 0) {
        refuse(
            `\`${python} ${PROBES}\` wrote no probe, and the record holds ${recordedProbes} of ` +
                'them: the probes are part of the record this check compares against'
        )
    }
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

// The list, split across processes: the tool hashes what its list names and prints one reading per
// file, and the corpus is 4,319 files -- 1.4-2.1 s in one process on the build host under its usual
// load, which is over the 2 s the fast layer allows a case to cost and is why this
// check could only be read a layer down. The split changes only which process reads a file: every
// chunk is the same `hashes` invocation over a smaller list, the answers are merged below, and a
// chunk that fails, dies or answers for a file it was not given stops the run exactly as the one
// process did (`--jobs 1` runs the whole corpus in one process).
const requested = [...scope, ...probeFiles]
const perChunk = Math.ceil(requested.length / Math.min(jobs, requested.length))
const chunks = []
for (let index = 0; index < requested.length; index += perChunk) {
    chunks.push(requested.slice(index, index + perChunk))
}

/**
 * One `hashes` process over one list, resolving to what it said.
 *
 * `spawn` rather than `spawnSync`, because the chunks read at the same time: the wall clock of this
 * check is the slowest chunk's and not the sum of theirs. The clock is still the tool's own
 * (`--timeout`), and a chunk that outlives it resolves with the signal it was killed by, which the
 * caller refuses. A chunk's output that passes the ceiling is stopped and refused rather than
 * truncated: half a line parses as a malformed reading, and a malformed reading is a sentence about
 * this check instead of one about the tree.
 */
function readChunk(chunk, index) {
    const listFile = path.join(work, `files-${index}.txt`)
    fs.writeFileSync(listFile, chunk.map((each) => forward(each)).join('\n') + '\n')
    return new Promise((resolve) => {
        const child = spawn(parsecheck, ['hashes', listFile], { cwd: ROOT, timeout })
        let stdout = ''
        let stderr = ''
        let overflow = false
        child.stdout.setEncoding('utf8')
        child.stderr.setEncoding('utf8')
        child.stdout.on('data', (text) => {
            stdout += text
            if (stdout.length <= (64 << 20) || overflow) return
            overflow = true
            child.kill()
        })
        child.stderr.on('data', (text) => {
            stderr += text
        })
        child.on('error', (error) => resolve({ index, chunk, error }))
        child.on('close', (status, signal) =>
            resolve({ index, chunk, status, signal, stdout, stderr, overflow })
        )
    })
}

const started = Date.now()
const runs = await Promise.all(chunks.map((chunk, index) => readChunk(chunk, index)))
const seconds = ((Date.now() - started) / 1000).toFixed(1)
for (const run of runs) {
    const where = chunks.length === 1 ? '' : ` (chunk ${run.index + 1} of ${chunks.length})`
    if (run.error) {
        refuse(
            `${shown(parsecheck)} hashes could not start${where}: ${run.error.message}`
        )
    }
    if (run.overflow) {
        refuse(
            `${shown(parsecheck)} hashes printed more than 64 MiB${where} after ${seconds} s; ` +
                'a reading that had to be cut is not a reading'
        )
    }
    if (run.signal) {
        const timedOut = run.signal === 'SIGTERM'
        refuse(
            timedOut
                ? `${shown(parsecheck)} hashes did not answer within ${timeout} ms${where}`
                : `${shown(parsecheck)} hashes was killed by ${run.signal} after ${seconds} s${where}`
        )
    }
    if (run.status !== 0) {
        const detail = `${run.stdout || ''}${run.stderr || ''}`.trim()
        refuse([
            `${shown(parsecheck)} hashes exited ${run.status} after ${seconds} s${where}`,
            ...detail.split(/\r?\n/).slice(0, 10).map((line) => `  ${line}`)
        ])
    }
}

// The answer, keyed the way the record spells a path: a probe by `probes/<name>`, everything else
// by its path from the root. A line about a file this run did not name is refused rather than
// dropped: the list and its answers have to describe one set.
const scopeSet = new Set(scope)
const now = new Map()
for (const run of runs) {
    for (const line of (run.stdout || '').split('\n')) {
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
    if (changed !== null) {
        // A narrow run speaks for the rows its diff can have moved, and for no other: a row the
        // tree lost somewhere else is the wide run's reading to make, not this one's.
        if (!absent.includes(file)) continue
        gone.push({ path: file, was, why: 'this diff deletes it' })
        continue
    }
    const why = absent.includes(file)
        ? 'the index holds it, the worktree does not'
        : 'probes.py no longer makes it'
    gone.push({ path: file, was, why })
}
const stale = mismatch.length + missing.length + gone.length
const checked = now.size
const unread = recorded.size - checked

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
if (changed === null) {
    console.log(
        `[${NAME}] scope ${scope.length} .cml file(s) of the tree + ${probeFiles.length} probe(s) ` +
            `from ${PROBES}, read in ${seconds} s (${chunks.length} process(es))`
    )
} else {
    console.log(
        `[${NAME}] scope ${scope.length} of the ${changed.length} .cml file(s) this tree changed ` +
            `against ${base}, and no probe (a probe moves with the parser, not with a .cml); ` +
            `read in ${seconds} s (${chunks.length} process(es))`
    )
    console.log(
        `[${NAME}] not read: ${unread} of the ${recorded.size} recorded line(s) -- a diff run ` +
            'compares the rows its diff can have moved; the wide run is the whole record'
    )
}
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
        `ast hashes: ${checked} file(s) read, ${stale} stale ` +
            `(${mismatch.length} mismatch, ${missing.length} missing, ${gone.length} gone)`
    )
    console.log(
        `${NAME}: ${stale} line(s) of ${GOLDEN} do not describe this tree. If the change is ` +
            'intended, re-record it: `node scripts/check-ast-hashes.mjs ' +
            (changed === null ? '--update --only ' : `--base ${base} --update`) +
            (changed === null ? "'<pattern>'" : '') +
            '` -- which hands it to `python3 test/tools/parsecheck/check.py snapshot --update ' +
            '--only <glob>` -- or, for a whole-record reading, `camel-remote.ps1 -Plan ' +
            'test/plans/feat/parse -UpdateAstHashes` on the host; never a bulk `--update` for one ' +
            'case (agents/test.md).'
    )
    process.exit(1)
}

console.log(
    changed === null
        ? `ast hashes: ${checked} file(s) checked, 0 stale`
        : `ast hashes: ${checked} file(s) read, 0 stale; ${unread} of the ${recorded.size} ` +
              `recorded line(s) were not read (a diff run against ${base})`
)
process.exit(0)