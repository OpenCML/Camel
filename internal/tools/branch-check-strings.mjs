// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A lane's changed line names a string, and the base no longer spells it in that file.
 *
 * The list a merge was audited by had four cells: the reference count of
 * `GCNodeKind::Copy|::Cast`, the call count of `addCopyNode|addCastNode`, and whether a diagnostic's
 * name was still in `registry.tsv`, `examples.tsv` and `semantic.h`. It passed in all four, before
 * and after the merge, and the merge still broke a reading -- because what the reading depended on
 * was a **string label**, which is in none of those four cells. A seat commit deleted two
 * `return "CAST"` lines from `src/stdlib/std/gir/gir_metadata.cpp`; a lane's first reading asked for
 * `CAST`; git saw no conflict on those lines, took the base's version of the file, and the reading
 * was wrong from the first case on.
 *
 * **No conflict says nobody edited the same line. It says nothing about nobody having deleted what
 * the other side reads.** So the population this check walks is exactly the one the old list had no
 * cell for: every string literal on every line the lane added or changed (prose is counted apart --
 * see "Three answers").
 *
 * --- what it does ---------------------------------------------------------------------------
 *
 *   base        the tree the lane would land on (a seat head, or any ref).
 *   lane        the tree whose lines are read.
 *
 *   git diff -M -U0 <base> <lane>     the lane's side is the `+` side: what the lane has that the
 *                                     base does not. When the lane is older than the base (the
 *                                     older-lane case) that is the lines that were there before
 *                                     the base changed them -- the lines a no-conflict merge keeps
 *                                     only if the base happens not to have moved.
 *   every `+` line                    every `"..."` and `'...'` span, taken as written: escapes are
 *                                     not decoded, so both sides are compared by spelling. Three
 *                                     narrowings, each stated so it can be argued with: `'` is
 *                                     scanned only in code extensions (in prose, `the node's kind`
 *                                     would be read as a string); a one-character `'x'` is a
 *                                     character constant and is skipped; a span that contains
 *                                     another span on the same line (`'type="CAST"'`) is folded into
 *                                     it, because that is one name written inside an expression. A
 *                                     line that is wholly a comment is skipped -- a comment reads
 *                                     nothing.
 *   base's tree                       the same literal, in the *same file*, over the production
 *                                     paths `src/**`, `stdlib/**`, `test/tools/**`; plus the base's
 *                                     copy of every file the lane changed, wherever it lives (the
 *                                     incident's own assertions are in `test/plans/**`, outside the
 *                                     scan set). The scan set also answers "does the base spell it
 *                                     anywhere at all".
 *   git merge-base <base> <lane>      the tree both sides started from ("the fork point"), which is
 *                                     what separates "the base took it away" from "the lane added
 *                                     it".
 *
 * Three answers, one per (lane file, literal) site:
 *
 *   ① safe       the base's copy of that file still spells the literal -- or, marked `elsewhere`,
 *                the base's production code spells it in some other file (the lane's file is new, or
 *                that file never had it), which is safe by this check's standard and says so.
 *   ② danger     the base's copy does not spell it, and it did at the fork point -- i.e. the base
 *                deleted (or moved) that spelling out of that file between the fork and its tip.
 *                The row names the commit that last changed that literal in that file
 *                (`git log -S <literal> -1 <base> -- <file>`), which for a deletion is the commit
 *                that deleted it. `-S` with no result at all is printed as such: it means the base's
 *                file never held that *whole* spelling, and the row is most likely a span inside a
 *                longer string or a regex (limit 4).
 *                Three shapes count as ②, and each says which one it is:
 *                  * `file-lost`: the file is there and the spelling is gone from it;
 *                  * `file-gone`: the file itself is gone from the base (deleted or renamed) -- every
 *                    line the lane has in it is at risk, not only this one, so these rows are
 *                    collapsed to one line per file in the text report and named there for what they
 *                    are: a modify/delete the merge is going to stop on anyway, not a silent one;
 *                  * `tree-gone`: the spelling is gone from the base's **whole** production tree while
 *                    the fork point still had it somewhere -- the producer that spelled it was
 *                    deleted, and the lane's file only ever read it (see limit 2).
 *   ③ new        neither the base's copy of that file, nor the fork point's, nor the base anywhere
 *                spells it: the lane introduced the name.
 *
 * Prose files (`.md`, `.txt`, `.rst`, no extension) are scanned and **counted but not listed**: a
 * quoted sentence is not a name the code reads, and asking the per-file question of yesterday's
 * wording would report every edited paragraph as a dangerous deletion. The count is in the header
 * line, so the narrowing is visible rather than silent.
 *
 * Exit code: 1 when ② is not empty, 0 when it is. A tool whose answer is "these lines read strings
 * the base no longer spells" is a question for the lane's owner, not a verdict -- but it is a
 * question that must be answered before the merge, so it is the one thing that turns the exit code.
 *
 * --- what it cannot answer (each one is a way this check is quiet) ----------------------------
 *
 * 1. **The same spelling with a different meaning.** Comparison is by spelling, so a file that still
 *    writes `"CAST"` for an unrelated purpose answers ① for a line that reads it for this one. On the
 *    reading that motivated this check it is not hypothetical: two of the six `CAST` rows come back ①
 *    because the base's copies of `test/plans/feat/compile/tagged.plan.toml` and
 *    `test/plans/feat/consistency/fail_never.plan.toml` still spell it *in a comment*. Nothing here
 *    reads the producer, so "still spelled" is not "still means the same".
 * 2. **Cross-file indirection.** A golden or a test reads a string the producer writes in another
 *    file. This check follows the file the lane's line sits in; only when the spelling vanished from
 *    the base's whole tree does it report the deletion (②, `tree-gone`). A base that moves `"CAST"`
 *    from `gir_metadata.cpp` to `gir_builder.cpp` *and keeps writing it there* is reported as ② for
 *    the lane's line in `gir_metadata.cpp` (right answer, wrong reason: the file lost it), while
 *    nothing reports a lane line in a test that reads the same moved spelling. The check knows file
 *    boundaries; it does not know who produces what.
 * 3. **A name assembled at run time.** `":op/" + name`, `std::format("{}", ...)`, a TOML `${var}`, a
 *    Python `f"...{x}..."`, a JS template literal (not scanned at all, and named in the constants for
 *    that reason): the whole name never appears as one literal, so neither side can match it. The
 *    fragments it can quote are compared as fragments, and a fragment that survives elsewhere says
 *    nothing about the assembled name.
 * 4. **Text, not syntax.** The scan is textual. A whole comment line is skipped on the lane's side,
 *    but a comment *at the end of a code line* is scanned, and the base's side is scanned with
 *    comments included -- so `// "CAST"` can answer ① for a code spelling that moved. A JS regex
 *    literal is scanned too: `/\(\s*"std::([a-z_][a-z0-9_:]*)"/` yields the span
 *    `std::([a-z_][a-z0-9_:]*)`, which is a pattern and not a string (the `-S` no-result note is
 *    usually how such a row shows itself). A literal inside a raw string (`R"(...)"`) is likewise
 *    just text.
 * 5. **Anything that is not a literal.** An enum (`GCNodeKind::Cast`), a builder call name
 *    (`addCastNode`), a diagnostic id -- the old list's whole population -- is invisible here, and
 *    this check says nothing about it. The two lists are complements, not replacements.
 * 6. **Renames and splits.** The per-file comparison follows the path in the base, using the rename
 *    the diff itself detected (`-M`); a rename the diff did *not* detect makes the file "gone" (②,
 *    `file-gone`), and `git log -S -- <old path>` stops at a rename, so the commit named for a ② row
 *    may be the rename rather than the deletion.
 * 7. **Whether the line mattered.** ② says the file no longer spells the string; it does not say the
 *    reading that depended on it was asserting something true. On the demo below many ② rows are a
 *    version string or a `known_fail_owner` sentence that the base reworded -- true, and not a
 *    defect. That judgement belongs to the lane's owner, and a clean run is not a merge approval.
 *
 * It also does not look at the `-` side (what the lane deleted), at binary files, or at a path
 * containing a newline (skipped, and counted in the report when it happens).
 *
 * --- read-only --------------------------------------------------------------------------------
 *
 * No file is written, no ref is created, moved or deleted, nothing is checked out, nothing is
 * fetched, no index lock is taken (`GIT_OPTIONAL_LOCKS=0`), and the build lock is not touched: the
 * run is `rev-parse`, `merge-base`, `diff`, `ls-tree`, `cat-file` and `log` against local objects.
 * `base`, `lane` and the fork point are resolved again after the run and a sha that moved is exit 4
 * with the ref named, so "read-only" is a reading this program takes rather than a claim it makes.
 *
 * --- usage ------------------------------------------------------------------------------------
 *
 *   node internal/tools/branch-check-strings.mjs --base <ref> --lane <ref>
 *   node internal/tools/branch-check-strings.mjs <base> <lane>            (both positionally)
 *   node internal/tools/branch-check-strings.mjs <base> <lane> --match CAST
 *   node internal/tools/branch-check-strings.mjs <base> <lane> --json
 *
 *   --paths <a,b,c>   the paths base is searched in (default `src,stdlib,test/tools`); the report
 *                     prints how many files each one actually matched, so a path that names nothing
 *                     in this tree is visible instead of silently shrinking the scan
 *   --limit <n>       print at most n rows per bucket (default 0 = every row); totals are always
 *                     printed, and a truncated bucket says how many rows it did not print
 *   --match <text>    only literals containing this text (literal, case-sensitive) -- for asking
 *                     one question of a large diff without hiding the totals
 *   --history <n>     how many ② rows get a `git log -S` lookup (default 200, 0 = none); rows over
 *                     the cap say so instead of printing no commit
 *   --json            the same payload as one JSON document
 *   --repo <dir>      read another checkout (default: the current directory)
 */

import { execFileSync } from 'node:child_process'
import process from 'node:process'

const TOOL = 'branch-check-strings'
const DEFAULT_PATHS = ['src', 'stdlib', 'test/tools']
const DEFAULT_HISTORY = 200
const MAX_BUFFER = 512 * 1024 * 1024

const USAGE =
    'usage: node internal/tools/branch-check-strings.mjs --base <ref> --lane <ref> [--paths a,b,c] [--limit <n>] [--match <text>] [--history <n>] [--json] [--repo <dir>]'

// GIT_OPTIONAL_LOCKS=0: a reader must not take the index lock -- `git status` and friends refresh
// the index, and a merge that runs into that lock fails with "Unable to write index". A background
// reader that took one would be a reader that stopped the merging it reports on.
const GIT_ENV = { ...process.env, GIT_OPTIONAL_LOCKS: '0', GIT_TERMINAL_PROMPT: '0', LC_ALL: 'C' }

// A literal as written, one line at a time. Escapes are kept (`\n` stays two characters), so the two
// sides are compared by spelling and never by a re-encoding this program guessed at. A template
// literal is deliberately not scanned: its body is a run-time assembly (`${name}`), and the whole
// name never exists on one line to be compared (see limit 3 in the header).
const DOUBLE_QUOTED = /"(?:[^"\\\n]|\\.)*"/g
const SINGLE_QUOTED = /'(?:[^'\\\n]|\\.)*'/g

// Where a single quote is a string delimiter rather than an apostrophe in prose. Outside this set
// (`*.md`, `*.dot`, `*.txt`) `'` is not scanned at all: `the node's kind` would otherwise be read as
// the literal `s kind, and a conversion` and every docs line would become a finding.
const SINGLE_QUOTE_CODES = new Set([
    '.c',
    '.h',
    '.cc',
    '.cpp',
    '.cxx',
    '.hpp',
    '.hh',
    '.hxx',
    '.cu',
    '.m',
    '.mm',
    '.py',
    '.js',
    '.mjs',
    '.cjs',
    '.ts',
    '.mts',
    '.jsx',
    '.tsx',
    '.toml',
    '.cml',
    '.sh',
    '.ps1',
    '.json',
])

const TEMPLATE_LITERAL_CODES = new Set(['.js', '.mjs', '.cjs', '.ts', '.mts', '.jsx', '.tsx'])

// Prose is scanned like everything else on the lane's side -- a sentence is a span between two
// quotes -- but it is not asked the per-file question: a quoted sentence in a `.md` is not a name
// the code reads, and comparing yesterday's wording to today's would report every edited paragraph
// as a dangerous deletion. Its literals are counted and not listed.
const PROSE = new Set(['.md', '.txt', '.rst', '.adoc', ''])

const isProse = (file) => PROSE.has(extensionOf(file))

function git(repo, args, { raw = false } = {}) {
    try {
        const out = execFileSync('git', ['-C', repo, ...args], {
            encoding: 'utf8',
            env: GIT_ENV,
            windowsHide: true,
            maxBuffer: MAX_BUFFER,
            stdio: ['ignore', 'pipe', 'pipe'],
        })
        return { code: 0, out: raw ? String(out) : String(out).replace(/\r\n/g, '\n').trim(), err: '' }
    } catch (error) {
        return {
            code: typeof error.status === 'number' ? error.status : 1,
            out: String(error.stdout || '').replace(/\r\n/g, '\n').trim(),
            err: String(error.stderr || '').replace(/\r\n/g, '\n').trim(),
        }
    }
}

const short = (sha) => String(sha).slice(0, 9)

function fail(message, code = 2) {
    process.stderr.write(`${TOOL}: ${message}\n${USAGE}\n`)
    process.exit(code)
}

function parseArgs(argv) {
    const options = {
        base: null,
        lane: null,
        paths: DEFAULT_PATHS,
        limit: 0,
        match: null,
        history: DEFAULT_HISTORY,
        json: false,
        repo: process.cwd(),
    }
    const positional = []
    for (let index = 0; index < argv.length; index += 1) {
        const arg = argv[index]
        const value = () => {
            const next = argv[index + 1]
            if (next === undefined || next.startsWith('--')) fail(`${arg} needs a value`)
            index += 1
            return next
        }
        const whole = (name, minimum) => {
            const raw = value()
            const parsed = Number(raw)
            if (!Number.isInteger(parsed) || parsed < minimum)
                fail(`${name} takes a whole number of at least ${minimum}, not '${raw}'`)
            return parsed
        }
        switch (arg) {
            case '--base':
                options.base = value()
                break
            case '--lane':
                options.lane = value()
                break
            case '--paths':
                options.paths = value()
                    .split(',')
                    .map((item) => item.trim())
                    .filter(Boolean)
                if (options.paths.length === 0) fail('--paths names no path')
                break
            case '--limit':
                options.limit = whole('--limit', 0)
                break
            case '--history':
                options.history = whole('--history', 0)
                break
            case '--match':
                options.match = value()
                break
            case '--repo':
                options.repo = value()
                break
            case '--json':
                options.json = true
                break
            case '--help':
            case '-h':
                process.stdout.write(`${USAGE}\n`)
                process.exit(0)
                break
            default:
                if (arg.startsWith('--')) fail(`unknown argument '${arg}'`)
                positional.push(arg)
        }
    }
    if (positional.length > 0) {
        if (options.base === null) options.base = positional.shift()
        if (positional.length > 0 && options.lane === null) options.lane = positional.shift()
    }
    if (positional.length > 0) fail(`unexpected argument '${positional[0]}'`)
    if (options.base === null || options.lane === null)
        fail('two refs are needed: a base and a lane (see --base/--lane)')
    return options
}

function resolveCommit(repo, ref) {
    const resolved = git(repo, ['rev-parse', '--verify', '--quiet', `${ref}^{commit}`])
    if (resolved.code !== 0 || !resolved.out) return null
    return resolved.out.split('\n')[0]
}

function extensionOf(file) {
    const slash = Math.max(file.lastIndexOf('/'), file.lastIndexOf('\\'))
    const dot = file.lastIndexOf('.')
    return dot > slash ? file.slice(dot).toLowerCase() : ''
}

/// A line that is wholly a comment. Asked of the lane's side only: a comment reads nothing, so it is
/// not a question to ask ("a comment that names a URI explains the code instead of depending on it",
/// as `scripts/check-operator-uri-literals.mjs` puts it). The base's side stays textual, comments
/// included, because a spelling that survives in a comment is one this program will not call deleted
/// -- the quieter direction. An inline comment after code is still scanned on both sides.
const HASH_COMMENT = new Set(['.py', '.sh', '.ps1', '.toml', '.yml', '.yaml', '.rb'])

function isCommentLine(text, file) {
    const trimmed = text.trimStart()
    if (trimmed.startsWith('//') || trimmed.startsWith('/*') || trimmed.startsWith('*')) return true
    return HASH_COMMENT.has(extensionOf(file)) && trimmed.startsWith('#')
}

/// The literals one line spells, in the order they appear. Raw bodies: `"a\nb"` yields `a\nb`.
/// A span that contains another span on the same line is folded into it: `'type="CAST"'` and
/// `"CAST"` are one name written inside an expression, and two rows for it would be one finding
/// counted twice.
function literalsIn(text, file, { skipCommentLines = false } = {}) {
    if (skipCommentLines && isCommentLine(text, file)) return []
    const extension = extensionOf(file)
    const spans = []
    const keep = (whole) => {
        const body = whole.slice(1, -1)
        if (body.length > 0) spans.push({ whole, body })
    }
    for (const match of text.matchAll(DOUBLE_QUOTED)) keep(match[0])
    if (SINGLE_QUOTE_CODES.has(extension)) {
        for (const match of text.matchAll(SINGLE_QUOTED)) {
            // A one-character `'x'` is a character constant, not a name; `'\0'` and longer are kept.
            if (match[0].length > 2) keep(match[0])
        }
    }
    if (TEMPLATE_LITERAL_CODES.has(extension)) {
        // Template literals are deliberately not scanned: their bodies are run-time assemblies
        // (`${name}`) and no whole name exists on one line to be compared. Kept as a named set so
        // the next reader does not have to guess whether `.js` was forgotten or excluded.
    }
    if (spans.length < 2) return spans.map((span) => span.body)
    const kept = spans.filter(
        (span) =>
            !spans.some(
                (other) =>
                    other.whole.length < span.whole.length && span.whole.includes(other.whole),
            ),
    )
    return kept.map((span) => span.body)
}

/// Every blob of `<sha>` under `paths`. One `cat-file --batch` for the whole set: the per-literal
/// `git grep` this replaces costs one process per literal, and a lane's diff has hundreds.
function readBlobs(repo, sha, paths) {
    const result = new Map()
    if (paths.length === 0) return result
    const input = `${paths.map((path) => `${sha}:${path}`).join('\n')}\n`
    let out
    try {
        out = execFileSync('git', ['-C', repo, 'cat-file', '--batch'], {
            input,
            env: GIT_ENV,
            windowsHide: true,
            maxBuffer: MAX_BUFFER,
            stdio: ['pipe', 'pipe', 'pipe'],
        })
    } catch (error) {
        fail(`git cat-file --batch failed for ${short(sha)}: ${String(error.message || error)}`)
    }
    let position = 0
    let index = 0
    while (index < paths.length && position < out.length) {
        const newline = out.indexOf(0x0a, position)
        if (newline < 0) break
        const header = out.toString('utf8', position, newline)
        position = newline + 1
        const path = paths[index]
        index += 1
        if (header.endsWith(' missing')) {
            result.set(path, { text: null, missing: true })
            continue
        }
        const fields = header.split(' ')
        const size = Number(fields[2])
        if (!Number.isFinite(size)) {
            result.set(path, { text: null, missing: true })
            continue
        }
        const blob = out.subarray(position, position + size)
        position += size + 1
        // A NUL byte is this program's test for "not text"; such a file exists and spells nothing.
        result.set(path, { text: blob.includes(0) ? null : blob.toString('utf8'), missing: false })
    }
    return result
}

/// The spelling index of a tree: which paths exist, and which literals each text file spells.
/// `spellings` is the *scan set's* union only -- the whole-tree answer below is a statement about the
/// production paths, so a file read in because the lane touched it must not widen it.
function readTree(sha) {
    return { sha, paths: new Set(), files: new Map(), spellings: new Set(), total: 0, perPrefix: new Map(), skipped: 0 }
}

function fillTree(repo, tree, prefixes) {
    const listed = git(repo, ['ls-tree', '-r', '--name-only', '-z', tree.sha, '--', ...prefixes], {
        raw: true,
    })
    if (listed.code !== 0) fail(`git ls-tree ${tree.sha} failed: ${listed.err || listed.out}`)
    const all = listed.out.split('\0').filter((path) => path.length > 0)
    const readable = all.filter((path) => !path.includes('\n'))
    const blobs = readBlobs(repo, tree.sha, readable)
    for (const path of all) tree.paths.add(path)
    for (const [path, blob] of blobs) {
        if (blob.text === null) continue
        const set = new Set(literalsIn(blob.text, path))
        tree.files.set(path, set)
        for (const literal of set) tree.spellings.add(literal)
    }
    for (const prefix of prefixes) {
        let count = 0
        for (const path of all) if (path === prefix || path.startsWith(`${prefix}/`)) count += 1
        tree.perPrefix.set(prefix, count)
    }
    tree.total = all.length
    tree.skipped = all.length - readable.length
    return tree
}

/// The base's and the fork's copies of the files the lane changed, for the ones the scan set does not
/// hold. Without this a lane's line in `test/plans/**` -- where that incident's assertions sit --
/// could only be answered by the whole-tree question, and "the base still spells
/// `CAST` somewhere in `src`" would answer ① for an assertion that reads the base's GIR dump.
/// Existence and spelling are added; the scan set's `spellings` is not.
function augmentTree(repo, tree, paths) {
    const extra = paths.filter((path) => !tree.paths.has(path))
    if (extra.length === 0) return 0
    const blobs = readBlobs(repo, tree.sha, extra)
    let read = 0
    for (const [path, blob] of blobs) {
        if (blob.missing) continue
        tree.paths.add(path)
        if (blob.text === null) continue
        if (!tree.files.has(path)) tree.files.set(path, new Set(literalsIn(blob.text, path)))
        read += 1
    }
    return read
}

function unquotePath(text) {
    if (text.length < 2 || !text.startsWith('"') || !text.endsWith('"')) return text
    return text.slice(1, -1).replace(/\\(x[0-9a-fA-F]{2}|[0-7]{3}|.)/g, (whole, escape) => {
        if (escape.length === 1) {
            const named = {
                n: '\n',
                t: '\t',
                r: '\r',
                a: '\x07',
                b: '\b',
                f: '\f',
                v: '\v',
                '\\': '\\',
                '"': '"',
            }
            return named[escape] ?? escape
        }
        if (escape.startsWith('x')) return String.fromCharCode(parseInt(escape.slice(1), 16))
        return String.fromCharCode(parseInt(escape, 8))
    })
}

/// The lane's side of `git diff <base> <lane>`: every `+` line, with the line number it has in the
/// lane's copy of the file. `-U0` so the hunk header gives the number directly and no context line
/// can be mistaken for a change.
function laneSide(repo, baseSha, laneSha) {
    const diff = git(repo, [
        'diff',
        '--no-color',
        '--no-ext-diff',
        '-M',
        '-U0',
        '--src-prefix=a/',
        '--dst-prefix=b/',
        baseSha,
        laneSha,
    ])
    if (diff.code !== 0) fail(`git diff ${short(baseSha)} ${short(laneSha)} failed: ${diff.err}`)
    const rows = []
    const files = new Set()
    const baseFiles = new Set()
    // `header` until the first `@@` of a file, `hunk` after it. The two have to be told apart: a `-`
    // line whose content begins `-- ` is emitted as `--- ...`, and inside a hunk that is a removed
    // line, not a file header. Reading it as a header would attribute the next file's `+` lines to
    // the file before it -- one diff, silently off by one file.
    let stage = 'header'
    let binary = false
    let file = null
    let baseFile = null
    let renameFrom = null
    let lineNumber = 0
    for (const raw of diff.out.split('\n')) {
        if (raw.startsWith('diff --git ')) {
            file = null
            baseFile = null
            renameFrom = null
            stage = 'header'
            binary = false
            continue
        }
        if (raw.startsWith('@@')) {
            const parsed = /^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@/.exec(raw)
            if (!parsed) {
                stage = 'header'
                continue
            }
            stage = 'hunk'
            lineNumber = Number(parsed[1])
            continue
        }
        if (stage === 'header') {
            if (raw.startsWith('rename from ')) {
                // The base's path for this file: a base that renamed it still holds the content, and
                // the per-file question must be asked of the file the base actually has.
                renameFrom = unquotePath(raw.slice('rename from '.length).trim())
            } else if (raw.startsWith('+++ ')) {
                const path = raw.slice(4).trim()
                file = path === '/dev/null' ? null : unquotePath(path).replace(/^b\//, '')
                baseFile = file === null ? null : (renameFrom ?? file)
            } else if (raw.startsWith('Binary files ')) {
                binary = true
            }
            continue
        }
        if (file === null || binary) continue
        if (raw.startsWith('\\')) continue
        if (raw.startsWith('+')) {
            rows.push({ file, baseFile, line: lineNumber, text: raw.slice(1) })
            files.add(file)
            baseFiles.add(baseFile)
            lineNumber += 1
            continue
        }
        if (raw.startsWith('-')) continue
        // No context lines with -U0; anything else that is not a header is not part of the change.
    }
    return { rows, files, baseFiles, binary: binary && file !== null }
}

/// The commit that last changed how many times `literal` appears at or before `sha` -- in `file`, or
/// across the whole scan set when `file` is null (a spelling the base's tree lost somewhere else).
/// For a deletion that is the deletion; `git log` stops at a rename, which is why the report says
/// "last changed" rather than "deleted".
function lastChangeOf(repo, sha, file, literal, paths = []) {
    const found = git(repo, [
        'log',
        '-1',
        '--format=%h|%cs|%s',
        `-S${literal}`,
        sha,
        '--',
        ...(file === null ? paths : [file]),
    ])
    if (found.code !== 0 || !found.out) return null
    const [commit, date, subject] = found.out.split('|')
    if (!commit) return null
    return { commit, date: date || '', subject: subject || '' }
}

const pad = (text, width) => String(text).padEnd(width)

/// The last commit that touched `file` at or before `sha`. Used for a file the base no longer has,
/// where the question is not which spelling went but which commit took the file.
function lastCommitForPath(repo, sha, file) {
    const found = git(repo, ['log', '-1', '--format=%h|%cs|%s', sha, '--', file])
    if (found.code !== 0 || !found.out) return null
    const [commit, date, subject] = found.out.split('|')
    if (!commit) return null
    return { commit, date: date || '', subject: subject || '' }
}

function classify(repo, groups, baseTree, forkTree, options, stats) {
    const rows = []
    const fileGoneHistory = new Map()
    const take = (lookup) => {
        stats.historyUsed += 1
        const found = lookup()
        if (found === null) stats.historyMissed += 1
        return found
    }
    for (const group of groups) {
        const { file, baseFile, literal, sites } = group
        const baseSet = baseTree.files.get(baseFile)
        const forkSet = forkTree.files.get(baseFile) ?? forkTree.files.get(file)
        const baseHas = baseSet ? baseSet.has(literal) : false
        const forkHas = forkSet ? forkSet.has(literal) : false
        const baseHasFile = baseTree.paths.has(baseFile)
        const forkHasFile = forkTree.paths.has(file) || forkTree.paths.has(baseFile)
        const elsewhere = baseTree.spellings.has(literal)

        let bucket
        let reason
        let shape
        if (isProse(file)) {
            // Not a name the code reads, and not one of the three answers: counted, not listed. A
            // quoted sentence in a `.md` moves with the prose that holds it, and comparing yesterday's
            // wording to today's would report every edited paragraph as a deletion.
            bucket = 0
            shape = 'prose'
            reason = '散文文件里的引文，不当名字看'
        } else if (baseHas) {
            bucket = 1
            shape = 'file-safe'
            reason =
                baseFile === file
                    ? 'base 的这份文件里还写着它'
                    : `base 把这一文件叫 ${baseFile}，那里还写着它`
        } else if (forkHasFile && !baseHasFile) {
            bucket = 2
            shape = 'file-gone'
            reason = '这一文件在 base 里已不存在（被删或改名）'
        } else if (forkHas) {
            bucket = 2
            shape = 'file-lost'
            reason =
                baseFile === file
                    ? 'fork 时这一文件写着它，base 的这份文件里没有了'
                    : `fork 时 ${baseFile} 写着它，base 的这份文件里没有了（diff 认出的改名对应）`
        } else if (!elsewhere && forkTree.spellings.has(literal)) {
            bucket = 2
            shape = 'tree-gone'
            reason = 'base 整棵生产代码树都没有它了，而 fork 时还有（写下它的地方被删了）'
        } else if (elsewhere) {
            // The base still spells it, in some other file. Safe by this check's standard, and marked
            // so a reader can see the answer came from another file rather than from this one.
            bucket = 1
            shape = 'elsewhere-safe'
            reason = 'base 的这份文件里没有它，但 base 生产代码的别处还写着它'
        } else {
            bucket = 3
            shape = 'new'
            reason = 'base 与 fork 都从来没有这一写法'
        }

        const row = {
            file,
            baseFile,
            literal,
            sites,
            bucket,
            shape,
            reason,
            lastChange: null,
            lastChangeKind: null,
            historySkipped: false,
            elsewhere,
        }
        if (bucket === 2) {
            if (shape === 'file-gone') {
                // One lookup per file, not per literal in it: the question is which commit took the
                // file, and a file can carry hundreds of literals.
                if (!fileGoneHistory.has(file)) {
                    fileGoneHistory.set(
                        file,
                        options.history > 0 && stats.historyUsed < options.history
                            ? take(() => lastCommitForPath(repo, baseTree.sha, baseFile))
                            : null,
                    )
                }
                row.lastChange = fileGoneHistory.get(file)
                row.lastChangeKind = 'file'
            } else if (options.history > 0 && stats.historyUsed < options.history) {
                row.lastChange = take(() =>
                    lastChangeOf(
                        repo,
                        baseTree.sha,
                        shape === 'tree-gone' ? null : baseFile,
                        literal,
                        options.paths,
                    ),
                )
                row.lastChangeKind = 'spelling'
            } else if (options.history > 0) {
                stats.historyTruncated += 1
                row.historySkipped = true
            }
        }
        rows.push(row)
    }
    return rows
}

function reportJson(payload) {
    process.stdout.write(`${JSON.stringify(payload, null, 2)}\n`)
}

function reportText(payload) {
    const lines = []
    const { base, lane, fork, scanned, counts } = payload
    lines.push(
        `branch-check-strings · 读数来自本地 refs，未 fetch · 只读：rev-parse / merge-base / diff / ls-tree / cat-file / log`,
    )
    lines.push(`base  ${base.ref} @ ${short(base.sha)}`)
    lines.push(`lane  ${lane.ref} @ ${short(lane.sha)}`)
    const forkNote =
        fork.sha === base.sha ? '   （= base：lane 是 base 的后代，② 在结构上只能为空）' : fork.sha === lane.sha ? '   （= lane：lane 是 base 的祖先，这是「旧树对后来的头」的方向）' : ''
    lines.push(`fork  merge-base @ ${short(fork.sha)}${forkNote}`)
    lines.push(
        `扫描  ${scanned.prefixes.map((prefix) => `${prefix.prefix}/** ${prefix.files}`).join(' · ')}  ⇒ 合计 ${scanned.files} 个文件（fork 侧 ${scanned.forkFiles} 个）`,
    )
    lines.push(
        `另读  lane 改到 ${scanned.changedFiles} 个文件，其中 ${scanned.laneFilesRead} 个不在上面三处，按同一文件在 base 的副本比对（不参与「整棵树都没有了」那条）`,
    )
    lines.push(
        `lane 侧 + 行 ${scanned.addedLines} 行 / ${scanned.changedFiles} 个文件 ⇒ ${counts.sites} 个（文件,字面量）位置${counts.prose > 0 ? `，其中 ${counts.prose} 个在散文文件里（.md/.txt，不当名字看，不计入三类）` : ''}${payload.match ? ` · 只看了含 '${payload.match}' 的` : ''}`,
    )
    lines.push('')

    const listBucket = (title, rows, render) => {
        lines.push(`${title} —— ${rows.length} 条`)
        if (rows.length === 0) {
            lines.push('   （无）')
            return
        }
        const shown = payload.limit > 0 ? rows.slice(0, payload.limit) : rows
        for (const row of shown) render(row)
        if (shown.length < rows.length)
            lines.push(`   …（只印了 ${shown.length} 条，共 ${rows.length} 条；--limit 0 印全部）`)
    }

    const sitesOf = (row) => {
        const shown = row.sites.slice(0, 3)
        const more = row.sites.length > shown.length ? ` …(+${row.sites.length - shown.length})` : ''
        return shown.map((site) => `${site.line}`).join(',') + more
    }

    listBucket(`① 仍存在（安全）`, payload.safe, (row) => {
        const note = row.shape === 'elsewhere-safe' ? `   ← ${row.reason}` : ''
        lines.push(`   ${row.file}:${sitesOf(row)}  "${row.literal}"${note}`)
    })
    lines.push('')

    const lastLine = (row, kind = 'spelling') => {
        const label =
            kind === 'file'
                ? 'base 最后一次动这个文件'
                : 'base 最后一次动它（-S：删掉它的那次一般就是这一条）'
        if (row.lastChange)
            return `${label}：${row.lastChange.commit} ${row.lastChange.date} ${row.lastChange.subject}`
        if (row.historySkipped) return `${label}：（未查，超过 --history 上限）`
        return `${label}：（-S 无结果 —— base 的这一文件里从没有过这一整段写法，多半只是更长字符串或正则里的一段）`
    }

    const sharp = payload.danger.filter((row) => row.shape !== 'file-gone')
    const collapsed = new Map()
    for (const row of payload.danger.filter((row) => row.shape === 'file-gone')) {
        if (!collapsed.has(row.file)) collapsed.set(row.file, [])
        collapsed.get(row.file).push(row)
    }

    lines.push(
        `② base 里已不存在 —— 危险：lane 读的东西被人删了 —— ${payload.danger.length} 条` +
            (collapsed.size > 0
                ? `（其中 ${payload.danger.length - sharp.length} 条是 ${collapsed.size} 个文件整个没了，按文件折叠在下面）`
                : ''),
    )
    if (payload.danger.length === 0) {
        lines.push('   （无）')
    } else {
        const shown = payload.limit > 0 ? sharp.slice(0, payload.limit) : sharp
        for (const row of shown) {
            lines.push(`   ${row.file}:${sitesOf(row)}  "${row.literal}"   ← ${row.reason}`)
            lines.push(`      ${lastLine(row)}`)
        }
        if (shown.length < sharp.length)
            lines.push(`   …（只印了 ${shown.length} 条，共 ${sharp.length} 条；--limit 0 印全部）`)
        if (collapsed.size > 0) {
            lines.push(
                '   --- 整个文件在 base 里已不存在（它不是「无冲突」那一类：lane 改过、base 删了，合并在这些文件上本来就会报 modify/delete）---',
            )
            const fileRows = [...collapsed.entries()]
            const fileShown = payload.limit > 0 ? fileRows.slice(0, payload.limit) : fileRows
            for (const [file, rows] of fileShown) {
                const sample = rows
                    .slice(0, 3)
                    .map((row) => `"${row.literal}"`)
                    .join(' ')
                lines.push(`   ${file}   ← 这个文件里 ${rows.length} 个字面量，例如 ${sample}`)
                lines.push(`      ${lastLine(rows[0], 'file')}`)
            }
            if (fileShown.length < fileRows.length)
                lines.push(`   …（只印了 ${fileShown.length} 个文件，共 ${fileRows.length} 个）`)
        }
    }
    lines.push('')

    listBucket(`③ 只在 lane 里出现（新增，正常）`, payload.newNames, (row) => {
        lines.push(`   ${row.file}:${sitesOf(row)}  "${row.literal}"`)
    })
    lines.push('')

    lines.push(`refs 不变：base / lane / fork 跑前跑后 sha 相同 ${payload.refsUnchanged ? '✔' : '✘'}（本工具只读的断言）`)
    lines.push('')
    lines.push('它不能回答什么（判断前先读这几条，脚本头部注释与 internal/tools/README.md 是同一份）：')
    lines.push('  · 同名不同义：只按拼写比。base 的同一文件里这个词用于别处（甚至只在注释里）即算「还在」，它不读产生它的代码。')
    lines.push('  · 跨文件间接引用：它跟着 lane 那一行所在的文件走；只有整个 base 生产代码树都没有这一写法时才报「被删」，它不知道谁产生谁。')
    lines.push('  · 动态拼出来的名字（":op/" + name、std::format、TOML ${var}、JS 模板串——模板串整个不扫）两侧都匹配不到。')
    lines.push('  · 文本不是语法：行尾注释、JS 正则里的引号也会被当成拼写（`-S 无结果` 一般就是这种行的样子）。')
    lines.push('  · 枚举 / 构造器调用 / 诊断名不在它的视野里 —— 那是另一张清单，两张是互补不是替代。')
    lines.push('  · 改名与拆分：只认 diff 自己检出的改名；没检出的，文件即算「不在了」，且 `git log -S -- 老路径` 停在改名那一步。')
    lines.push('  · 它不判断这一行重不重要：② 也可能是被改写的版本号或一句 known_fail_owner，真不真是车道主人的判断。')
    process.stdout.write(`${lines.join('\n')}\n`)
}

function main() {
    const options = parseArgs(process.argv.slice(2))

    const topLevel = git(options.repo, ['rev-parse', '--show-toplevel'])
    if (topLevel.code !== 0 || !topLevel.out) fail(`not a git repository: ${options.repo}`)
    const repo = topLevel.out

    const baseSha = resolveCommit(repo, options.base)
    if (!baseSha) fail(`base '${options.base}' does not resolve to a commit in ${repo}`)
    const laneSha = resolveCommit(repo, options.lane)
    if (!laneSha) fail(`lane '${options.lane}' does not resolve to a commit in ${repo}`)

    const mergeBase = git(repo, ['merge-base', baseSha, laneSha])
    if (mergeBase.code !== 0 || !mergeBase.out)
        fail(`no merge base for ${options.base} and ${options.lane}: ${mergeBase.err}`)
    const forkSha = mergeBase.out.split('\n')[0]

    const baseTree = fillTree(repo, readTree(baseSha), options.paths)
    const forkTree =
        forkSha === baseSha ? baseTree : fillTree(repo, readTree(forkSha), options.paths)
    if (options.paths.length > 0 && baseTree.total === 0)
        fail(`no file under ${options.paths.join(', ')} at ${options.base}`)

    const lane_ = laneSide(repo, baseSha, laneSha)

    // The lane's own files, read at both trees: the per-file question has to be asked about a line in
    // `test/plans/**` too, and those paths are outside the scan set. The base side is read at the
    // path the base has (`baseFile`, which follows a rename the diff detected); the fork side at the
    // lane's path, because the fork is on the lane's side of that rename.
    const extraRead = augmentTree(repo, baseTree, [...lane_.baseFiles])
    if (forkTree !== baseTree) augmentTree(repo, forkTree, [...lane_.files])

    // Grouped by (file, literal): one classification per file's spelling, every line that reads it
    // listed under it. A literal repeated on forty golden lines is one row, not forty.
    const groups = new Map()
    let addedLines = 0
    for (const row of lane_.rows) {
        addedLines += 1
        for (const literal of literalsIn(row.text, row.file, { skipCommentLines: true })) {
            if (options.match && !literal.includes(options.match)) continue
            const key = `${row.file}\u0000${literal}`
            let group = groups.get(key)
            if (!group) {
                group = { file: row.file, baseFile: row.baseFile, literal, sites: [] }
                groups.set(key, group)
            }
            if (group.sites.length < 64) group.sites.push({ line: row.line, text: row.text.trim() })
        }
    }

    const stats = { historyUsed: 0, historyMissed: 0, historyTruncated: 0 }
    const rows = classify(repo, [...groups.values()], baseTree, forkTree, options, stats)

    const refsAfter = [resolveCommit(repo, options.base), resolveCommit(repo, options.lane)]
    const refsUnchanged = refsAfter[0] === baseSha && refsAfter[1] === laneSha

    const safe = rows.filter((row) => row.bucket === 1)
    const danger = rows.filter((row) => row.bucket === 2)
    const newNames = rows.filter((row) => row.bucket === 3)
    const prose = rows.filter((row) => row.bucket === 0)

    const payload = {
        tool: TOOL,
        version: 1,
        readOnly: true,
        fetched: false,
        localRefsOnly: true,
        base: { ref: options.base, sha: baseSha },
        lane: { ref: options.lane, sha: laneSha },
        fork: { sha: forkSha },
        limit: options.limit,
        match: options.match,
        history: options.history,
        historyUsed: stats.historyUsed,
        historyTruncated: stats.historyTruncated,
        scanned: {
            prefixes: options.paths.map((prefix) => ({
                prefix,
                files: baseTree.perPrefix.get(prefix) ?? 0,
            })),
            files: baseTree.total,
            forkFiles: forkTree.total,
            skippedPaths: baseTree.skipped,
            changedFiles: lane_.files.size,
            addedLines,
            laneFilesRead: extraRead,
        },
        counts: {
            sites: groups.size,
            safe: safe.length,
            danger: danger.length,
            newNames: newNames.length,
            prose: prose.length,
        },
        refsUnchanged,
        safe,
        danger,
        newNames,
        prose,
    }

    if (options.json) reportJson(payload)
    else reportText(payload)

    if (!refsUnchanged) {
        process.stderr.write(
            `${TOOL}: a ref moved while this run was reading it; the table is not a reading of one state\n`,
        )
        process.exitCode = 4
        return
    }
    process.exitCode = danger.length > 0 ? 1 : 0
}

main()