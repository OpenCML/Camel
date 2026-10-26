#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fails when a reader-facing page shows a Camel snippet that the current language does not
// accept, or whose printed output is not what the page says it is.
//
//   node scripts/check-doc-snippets.mjs              check the reader-facing pages
//   node scripts/check-doc-snippets.mjs --default-roots  the same, ignoring a script path argument
//   node scripts/check-doc-snippets.mjs FILE DIR  check the given files and directories
//   --camel PATH                                  the compiler the snippets are run with;
//                                                 this tree's `out/latest/bin/camel` without it
//
// A Camel fence is a fenced block tagged `camel` or `cml` (both tags are used in the tree).
// The page itself is the source of truth for what the snippet means:
//
//   * A `text` fence directly after the Camel fence, or after a `bash`/`sh` command fence that
//     runs it, is the output the page declares (the convention of `docs/language/README.md`:
//     "a `text` code block is its output"). The snippet is run and its stdout must match. A
//     command fence chooses the passes and `--arg` values, exactly as the page does.
//   * Otherwise the snippet only has to compile: parse, GCT and GIR, asserted with
//     `camel <file> std::gir`. A fence with no entry point is compiled as a module with a
//     generated `func main(): int sync { return 0 }`; the fence itself is not changed.
//   * Adjacent Camel fences whose first lines name files (`// shapes.cml`, then `// main.cml`)
//     are one example: the files are written into one directory and the entry is the fence
//     with `func main`.
//
// A fence that is deliberately not a program is marked, never guessed at. The marker is an
// HTML comment on the line before the opening fence, so it does not appear on the page:
//
//   <!-- camel-snippet: fragment: continues the function above -->
//   <!-- camel-snippet: counter-example: the two returns do not leave the function -->
//   <!-- camel-snippet: graph-dump: the text block is one excerpt of std::rgir's DOT -->
//   <!-- camel-snippet: compile-only: the text block is a grammar, not output -->
//
// `fragment` and `counter-example` are not compiled. A `counter-example` must also say so on
// the page: the fence or the paragraph before it has to contain `counter-example`, `wrong`,
// `反例`, `错误`, `does not compile` or `is an error`. A marker without a reason is an error.
//
// A `graph-dump` fence is a text block like any other: the graph pass prints the dump on stdout,
// and the block is compared with it (a dump is the assertion that the reader's program is drawn
// this way, so it must be able to fail). Two normalisations make that comparison a comparison of
// the graph and not of the machine it was drawn on: the attributes that carry where the file was
// and where it was built (`origin`, `span`, `srcfile`; see normalizeDump) lose their values, so
// the checker's temporary file and the page's own example file compare equal, and the *presence*
// of each attribute is still compared. A block that does not open the dump (`digraph ...`) is one
// excerpt of it and has to appear in the dump as a contiguous run of lines, up to renaming the node
// and cluster numbers of one side one-to-one onto the other's (see DUMP_NUMBER; the prose names a
// node by what it is, so a page whose graph is numbered differently still holds); a whole dump is
// compared line by line. A `compile-only` fence is the marker for a text block that is not this
// run's output at all, and is still compiled without comparing anything.
//
// A page is written against the target language, so a snippet that the tree cannot compile yet is
// not marked on the page: it is registered in PENDING below, keyed by the page and the snippet's
// own text. A pending snippet that fails is exempted and named in the output; one that starts to
// compile, and to print its text block when it has one, must be removed from the list.
//
// On a `docs/design/` or `docs/technical/` page a fence without an entry point is an excerpt,
// not a program: those pages quote fragments, proposed syntax and measurements. Only their
// fences that contain `func main` are compiled. The rule is in EXCERPTS below, with the reason.
// The record directories (docs/handover, docs/reviews) are not pages and are not scanned; three
// generated or dated references are allowlisted because their `camel` fences are signatures,
// diagnostics or excerpts of an earlier state, not programs.
//
// The run is parallel (one camel process per snippet, up to JOBS at a time) because every
// snippet is its own entry file and its own temp directory; batching them into one process
// would mix module names and entries. The summary prints the wall time.
//
// Exit status 1 when a snippet is stale, 2 when the scope holds no document or no Camel fence: a
// check of nothing is refused, never reported as a pass.

import crypto from 'node:crypto'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { takeCamel } from '../test/tools/camel-arg.mjs'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const JOBS = Math.max(1, Number(process.env.CAMEL_SNIPPET_JOBS || 8))
const TIMEOUT_MS = Number(process.env.CAMEL_SNIPPET_TIMEOUT_MS || 15000)

function usage(message) {
    if (message) console.error(`check-doc-snippets: ${message}`)
    console.error(
        'usage: node scripts/check-doc-snippets.mjs [--default-roots] [--camel PATH] [FILE|DIR ...]',
    )
    process.exit(2)
}

// The compiler every snippet is run with: `--camel <path>` names it, else this tree's
// `out/latest/bin/camel`. One rule, in one place: test/tools/camel-arg.mjs.
const { camel: CAMEL, rest: ARGV } = takeCamel(process.argv.slice(2), ROOT, usage)

// Directories that are records, not pages: they quote the old spelling to say what happened.
const RECORDS = ['docs/handover/', 'docs/reviews/']

// A generated reference is not a teaching page: its `camel` fences are signatures (one per
// overload) and the triggering examples of diagnostics, deliberately wrong.
const ALLOWED_FILES = [
    ['docs/stdlib-reference.md', 'generated: signature lines, one per overload, not programs'],
    ['docs/diagnostics.md', 'generated: each example is a program that raises the diagnostic on purpose'],
    ['docs/design-principles-log.cn.md', 'a dated implementation log: the fences are excerpts of the state then'],
]

// Design and technical notes are working records: they quote fragments, proposed syntax and
// measurements. Their fences with an entry point are programs and are checked; the others
// are excerpts, with this reason.
const EXCERPTS = [
    ['docs/design/', 'design note: excerpts and proposed syntax; only a fence with func main is a program'],
    ['docs/technical/', 'technical record: excerpts and measurements; only a fence with func main is a program'],
]

// ---- The pending list ------------------------------------------------------------------------
// A reader-facing page is written against the target language: a rule that is decided is written as
// if it were already in place, and no mark on the page says otherwise. A snippet that the tree
// cannot compile yet is registered here instead, so the page carries no sign of the gap and the
// gate does not go red for it.
//
// An entry names the page and the snippet's own text. `key` is the first 16 hex digits of the
// SHA-256 of the snippet's fences, line endings normalised, every line stripped of trailing
// whitespace and the body of trailing blank lines (one key per snippet, however many fences it
// groups). Editing a snippet therefore changes its key: the entry stops matching its snippet, and
// that is a failure of its own (the rule below), never a quiet pass. `reason` is one line naming
// the ruled construct the snippet is ahead of; a page may hold several entries.
//
// The list only shrinks. Three rules enforce that, and each one fails the run:
//   * an entry without a reason, or with a key that is not 16 hex digits, is refused before
//     anything is compiled;
//   * an entry whose snippet now compiles -- and prints what its `text` block says, when it has
//     one, a graph dump included -- must be deleted, because the exemption outlived the gap. One
//     key can stand for several occurrences of the same text, and the entry is stale only when
//     every one of them has caught up: the reason is below `pendingBehind`;
//   * an entry that matches no snippet of the scanned scope is refused, so an edit to the page (or
//     to the fence grouping) cannot orphan an exemption quietly, and an entry cannot be added for
//     a snippet that does not exist. This rule is applied under `--default-roots` only: a run over
//     one file has not seen the rest of the tree.
// The count is held at or below PENDING_MAX; deleting entries is routine, and adding one means
// raising the ceiling in the same edit, which a review can see. Every run prints each entry it
// used, with its reason, so the list can be reviewed from the output without reading this file.
const PENDING = [
    {
        file: 'docs/reference/camel-for-ai.md',
        key: 'fdcec12c6bd9c098',
        reason: 'streams: `yield` and the stream return type are ruled, and this tree cannot compile them yet',
    },
    {
        file: 'docs/tutorial/17_teaching_a_law.cn.md',
        key: '5ffb278a87e871fa',
        reason:
            'chapter 18: the `fact` compiles now; the text block asks for `check` to fire a law on ' +
            'a fact\'s conclusion (rewrite design 8.1), and `math:abs_l` is not translated',
    },
    {
        file: 'docs/tutorial/17_teaching_a_law.en.md',
        key: '5ffb278a87e871fa',
        reason:
            'chapter 18: the `fact` compiles now; the text block asks for `check` to fire a law on ' +
            'a fact\'s conclusion (rewrite design 8.1), and `math:abs_l` is not translated',
    },
]

const PENDING_MAX = 5

const OTHER_LANGUAGES = new Set([
    '', 'bash', 'c', 'cpp', 'c++', 'css', 'go', 'html', 'java', 'javascript', 'js', 'json',
    'lua', 'markdown', 'powershell', 'ps1', 'py', 'python', 'rust', 'sh', 'sql', 'toml', 'ts',
    'typescript', 'text', 'yaml', 'yml', 'diff', 'cmake', 'asm', 'elisp',
])

const CAMEL_LANGUAGES = new Set(['camel', 'cml'])
const MARKER = /<!--\s*camel-snippet:\s*([a-z-]+)\s*(?::\s*(.*?))?\s*-->/
const MARKED_KINDS = new Set(['fragment', 'counter-example', 'graph-dump', 'compile-only'])
const COUNTER_LABEL = /counter-?example|wrong\b|反例|错误|does not compile|is an error/i

const FENCE = /^(\s*)(```+|~~~+)\s*([A-Za-z0-9_+#-]*)(.*)$/
const FILE_HEADER = /^\s*\/\/\s*([A-Za-z0-9_.-]+\.cml)\b/
const HAS_ENTRY = /^\s*(?:sync\s+)?func\s+main\s*\(/m
const DIAGNOSTIC = /^(error|warning)\[([^\]]+)\]/gm

function relative(abs) {
    return path.relative(ROOT, abs).replace(/\\/g, '/')
}

function collect(entry, out) {
    const abs = path.resolve(ROOT, entry)
    if (!fs.existsSync(abs)) return
    const stat = fs.statSync(abs)
    if (stat.isFile()) {
        if (path.extname(abs) === '.md') out.push(abs)
        return
    }
    for (const name of fs.readdirSync(abs)) {
        if (name === '.git' || name === 'node_modules' || name === 'out' || name === 'build') continue
        const child = path.join(abs, name)
        const rel = relative(child)
        if (RECORDS.some((prefix) => rel === prefix.slice(0, -1) || rel.startsWith(prefix))) continue
        collect(rel, out)
    }
}

function allowedFile(abs) {
    const rel = relative(abs)
    return ALLOWED_FILES.find(([entry]) => rel === entry)
}

function excerptPage(abs) {
    const rel = relative(abs)
    return EXCERPTS.find(([prefix]) => rel === prefix.slice(0, -1) || rel.startsWith(prefix))
}

// ---- Markdown fences -----------------------------------------------------------------------

function parseFences(lines) {
    const fences = []
    let current = null
    for (let i = 0; i < lines.length; i++) {
        const match = FENCE.exec(lines[i])
        if (match) {
            if (current === null) {
                const before = lines
                    .slice(0, i)
                    .reverse()
                    .find((line) => line.trim() !== '')
                const marker = before ? MARKER.exec(before) : null
                current = {
                    open: i,
                    close: -1,
                    lang: match[3].toLowerCase(),
                    info: match[4].trim(),
                    body: [],
                    marker: marker ? { kind: marker[1].toLowerCase(), reason: (marker[2] || '').trim() } : null,
                }
            } else {
                current.close = i
                fences.push(current)
                current = null
            }
            continue
        }
        if (current !== null) current.body.push(lines[i])
    }
    if (current !== null) {
        current.close = lines.length - 1
        fences.push(current)
    }
    return fences
}

function isCamel(fence) {
    return CAMEL_LANGUAGES.has(fence.lang)
}

// True when only blank lines separate two fences.
function adjacent(lines, first, second) {
    for (let i = first.close + 1; i < second.open; i++) {
        if (lines[i].trim() !== '') return false
    }
    return true
}

function firstLine(fence) {
    return fence.body.find((line) => line.trim() !== '') || ''
}

function entryOf(fence) {
    return HAS_ENTRY.test(fence.body.join('\n'))
}

// The identity of a snippet, for PENDING: its own text, normalised so that a re-indent or a
// line-ending change does not re-key it, and so that any edit to a word does.
function snippetKey(snippet) {
    const text = snippet.fences
        .map((fence) =>
            fence.body
                .map((line) => line.replace(/\s+$/, ''))
                .join('\n')
                .replace(/\n+$/, ''),
        )
        .join('\n--\n')
    return crypto.createHash('sha256').update(text).digest('hex').slice(0, 16)
}

function pendingEntry(rel, snippet) {
    const key = snippetKey(snippet)
    return PENDING.find((entry) => entry.file === rel && entry.key === key) || null
}

// The first line of a snippet, for a message a reader can locate on the page.
function snippetOpens(snippet) {
    return firstLine(snippet.fences[0]).trim().slice(0, 60) || '<empty fence>'
}

// ---- Snippet construction ------------------------------------------------------------------

function fileNameOf(fence, fallback) {
    const match = FILE_HEADER.exec(firstLine(fence))
    return match ? match[1] : fallback
}

// Groups adjacent Camel fences that are the files of one example. A fence whose first line is
// `// name.cml` starts a group; the next fence belongs to it when it is also a file (`// x.cml`)
// or when it is the entry (`func main`) of the file just named.
//
// `fragment` and `counter-example` fences are not snippets. `graph-dump` and `compile-only`
// fences are: a dump is compiled and its `text` block is compared with the dump (see the header),
// and a compile-only fence is compiled with its block left alone.
function buildSnippets(lines, fences) {
    const snippets = []
    for (let i = 0; i < fences.length; i++) {
        const fence = fences[i]
        if (!isCamel(fence)) continue
        if (fence.marker && ['fragment', 'counter-example'].includes(fence.marker.kind)) continue
        const group = [fence]
        let last = i
        while (last + 1 < fences.length) {
            const next = fences[last + 1]
            if (!isCamel(next)) break
            if (next.marker && ['fragment', 'counter-example'].includes(next.marker.kind)) break
            if (!adjacent(lines, fences[last], next)) break
            const named = FILE_HEADER.test(firstLine(fence))
            const nextNamed = FILE_HEADER.test(firstLine(next))
            if (nextNamed || (named && entryOf(next))) {
                group.push(next)
                last++
                continue
            }
            break
        }
        i = last
        snippets.push({ fences: group, open: fence.open })
    }
    if (process.env.CAMEL_SNIPPET_DEBUG) {
        for (const snippet of snippets) {
            console.log(
                `DEBUG snippet ${snippet.open + 1}: ` +
                    snippet.fences
                        .map((fence) => `${fence.open + 1}:${fileNameOf(fence, '?')}${entryOf(fence) ? ':entry' : ''}`)
                        .join(', '),
            )
        }
    }
    return snippets
}

function outputAfter(lines, fences, snippet) {
    const last = snippet.fences[snippet.fences.length - 1]
    const index = fences.indexOf(last)
    if (index < 0) return null
    let cursor = index + 1
    if (cursor >= fences.length) return null
    if (!adjacent(lines, last, fences[cursor])) return null
    let command = null
    if (['bash', 'sh', 'powershell'].includes(fences[cursor].lang)) {
        command = fences[cursor]
        cursor++
        if (cursor >= fences.length || !adjacent(lines, command, fences[cursor])) return null
    }
    if (fences[cursor].lang !== 'text') return null
    return { command, output: fences[cursor] }
}

// ---- Command lines -------------------------------------------------------------------------

function shellSplit(text) {
    const tokens = []
    let current = ''
    let quote = null
    for (let i = 0; i < text.length; i++) {
        const ch = text[i]
        if (quote) {
            if (ch === quote) quote = null
            else current += ch
            continue
        }
        if (ch === "'" || ch === '"') {
            quote = ch
            continue
        }
        if (/\s/.test(ch)) {
            if (current !== '') {
                tokens.push(current)
                current = ''
            }
            continue
        }
        current += ch
    }
    if (current !== '') tokens.push(current)
    return tokens
}

function parseCommand(fence) {
    if (!fence) return null
    for (const line of fence.body) {
        const text = line
            .replace(/^\s*\/\/\s*/, '')
            .replace(/\s+#.*$/, '')
            .trim()
        if (!/^camel\b/.test(text)) continue
        const tokens = shellSplit(text)
        const args = []
        const passes = []
        let file = null
        let i = 1
        while (i < tokens.length) {
            const token = tokens[i]
            if (token === '--arg' || token === '-I' || token === '--include') {
                if (i + 1 < tokens.length) args.push(token, tokens[++i])
                i++
                continue
            }
            if (token.startsWith('--arg=')) {
                args.push('--arg', token.slice('--arg='.length))
                i++
                continue
            }
            if (token.startsWith('-I=')) {
                args.push('-I', token.slice(3))
                i++
                continue
            }
            if (token.startsWith('--include=')) {
                args.push('--include', token.slice('--include='.length))
                i++
                continue
            }
            if (token === '--input') {
                if (i + 1 < tokens.length) file = tokens[++i]
                i++
                continue
            }
            if (token.startsWith('--input=')) {
                file = token.slice('--input='.length)
                i++
                continue
            }
            if (token.startsWith('-')) {
                i++
                if (i < tokens.length && !tokens[i].startsWith('-') && !tokens[i].includes('::')) i++
                continue
            }
            if (file === null && /\.cml$/.test(token)) {
                file = token
                i++
                continue
            }
            if (token.includes('::')) {
                passes.push(token)
                i++
                continue
            }
            if (file === null) {
                file = token
                i++
                continue
            }
            i++
        }
        return { file, passes, args }
    }
    return null
}

// A graph pass prints the graph on stdout instead of running the program, so a `text` block after
// one of them is the dump, and is compared with it (see the header). The other passes here produce
// something that is not the program's stdout either -- `std::cpp` prints generated source,
// `onnx::export` writes a file -- and a block after one of those is still compiled and left alone:
// only a dump has an output on the page that the check can compare.
const PRINTS_A_GRAPH = /^(std::(?:rgir|gir|jgir|agir|argir)|.*::(?:rgir|gir|jgir))/

function printsAGraph(passes) {
    return passes.some((pass) => PRINTS_A_GRAPH.test(pass))
}

function isGraphDump(passes) {
    return passes.some((pass) =>
        /^(std::(?:rgir|gir|jgir|cpp|agir|argir)|onnx::export|.*::(?:rgir|gir|jgir))/.test(pass),
    )
}

// ---- Running camel -------------------------------------------------------------------------

function runCamel(args, cwd) {
    return new Promise((resolve) => {
        const started = Date.now()
        const env = { ...process.env }
        // The test runner sets the short form for its own assertions; a snippet's text block is
        // written in the default text form, and warnings are part of what a page shows.
        delete env.CAMEL_ERROR_FORMAT
        // The snippet runs inside the install this check measured, so its standard library and its
        // runtime library are that install's: `CAMEL_HOME` is the install root, not whatever the
        // shell exported.
        env.CAMEL_HOME = path.dirname(path.dirname(CAMEL))
        const child = spawn(CAMEL, args, {
            cwd,
            env,
            stdio: ['ignore', 'pipe', 'pipe'],
        })
        let stdout = ''
        let stderr = ''
        let timedOut = false
        const timer = setTimeout(() => {
            timedOut = true
            child.kill('SIGKILL')
        }, TIMEOUT_MS)
        child.stdout.on('data', (chunk) => (stdout += chunk))
        child.stderr.on('data', (chunk) => (stderr += chunk))
        child.on('error', (error) => {
            clearTimeout(timer)
            resolve({ code: -1, stdout, stderr: stderr + error.message, timedOut, ms: Date.now() - started })
        })
        child.on('close', (code) => {
            clearTimeout(timer)
            resolve({ code, stdout, stderr, timedOut, ms: Date.now() - started })
        })
    })
}

function firstDiagnostic(result) {
    const text = `${result.stdout}\n${result.stderr}`
    const line = text
        .split(/\r?\n/)
        .map((entry) => entry.trim())
        .find((entry) => /\[Error\]|error\[|RuntimeDiag|SemanticDiag|SyntaxDiag|LexicalDiag/.test(entry))
    return (line || text.trim().split(/\r?\n/)[0] || 'no diagnostic').slice(0, 240)
}

// A `text` block that shows a diagnostic: the program prints the lines before it, then fails
// (an error) or warns (a warning). The name is asserted; the path is not, because the snippet
// runs from a temporary file.
function parseExpected(text) {
    const lines = text.replace(/\r\n/g, '\n').split('\n')
    const kept = []
    const diagnostics = []
    let inDiagnostic = false
    for (const line of lines) {
        if (/^(error|warning)\[/.test(line)) {
            inDiagnostic = true
            const match = /^(error|warning)\[([^\]]+)\]/.exec(line)
            diagnostics.push({ severity: match[1], name: match[2] })
            continue
        }
        if (inDiagnostic) {
            if (line.trim() === '') inDiagnostic = false
            continue
        }
        kept.push(line)
    }
    const expected = kept.join('\n').replace(/\n+$/, '')
    return { expected, diagnostics }
}

function beforeDiagnostic(text) {
    const lines = text.replace(/\r\n/g, '\n').split('\n')
    const index = lines.findIndex((line) => /^(error|warning)\[/.test(line))
    return (index < 0 ? lines : lines.slice(0, index)).join('\n').replace(/\n+$/, '')
}

function compareText(actual, expected) {
    const normalize = (text) =>
        text
            .replace(/\r\n/g, '\n')
            .replace(/(?:[^\s:()]*[\\/])?[A-Za-z0-9_.-]+\.cml:(\d+):(\d+)/g, '<file>:$1:$2')
            .replace(/\n+$/, '')
    const left = normalize(actual)
    const right = normalize(expected)
    if (left === right) return null
    const a = left.split('\n')
    const b = right.split('\n')
    for (let i = 0; i < Math.max(a.length, b.length); i++) {
        if (a[i] !== b[i]) {
            return `line ${i + 1}: page says ${JSON.stringify(b[i] ?? '<end>')}, program printed ${JSON.stringify(a[i] ?? '<end>')}`
        }
    }
    return 'output differs'
}

// ---- Graph dumps ---------------------------------------------------------------------------
//
// A dump is compared line by line after the attributes that belong to the machine, not to the
// graph, lose their values. All three come from one place in the emitter
// (GraphVizDumpPass::debugOriginAttr), and there are exactly three:
//
//   * `origin=<n>` -- an offset of the file the compiler read. The checker writes the fence into a
//     temporary file and the page's dump was drawn from the tutorial's own example file, where the
//     same program sits at other offsets (this is also why the value moves with any edit above it);
//   * `span="l:c-l:c"` -- that offset resolved against the source text, so it moves for the same
//     reason;
//   * `srcfile="..."` -- the path of that file.
//
// The placeholder keeps the attribute: a dump that gained or lost one of them is a different dump
// and stays a difference. Everything else in the block is compared verbatim, node and edge
// identity included, because that is the graph.
const DUMP_LOCATION_ATTRS = [
    [/\borigin=\d+/g, 'origin=<origin>'],
    [/\bspan="\d+:\d+-\d+:\d+"/g, 'span="<span>"'],
    [/\bsrcfile="(?:[^"\\]|\\.)*"/g, 'srcfile="<srcfile>"'],
]

function normalizeDump(text) {
    let out = text
        .replace(/\r\n/g, '\n')
        .split('\n')
        .map((line) => line.replace(/\s+$/, ''))
        .join('\n')
        .replace(/\n+$/, '')
    for (const [pattern, replacement] of DUMP_LOCATION_ATTRS) out = out.replace(pattern, replacement)
    return out
}

// A block that opens the dump is the whole dump; anything else is one excerpt of it, and the
// rule for an excerpt is that its lines appear in the output one after another.
function opensTheDump(pageLines) {
    const first = pageLines.find((line) => line.trim() !== '') || ''
    return /^\s*digraph\b/.test(first)
}

// A node, a function cluster or an equivalence class is named by a letter and a number: `N03`,
// `O04`, `F0D`, `C01`, and the `c1` that a saturation dump writes as a class label. The number is
// an accident of the run -- the counter has drawn every node above it by the time this one is
// numbered -- so the rule is that an excerpt does not compare numbers:
// it matches when *some* one-to-one renaming of one side's numbers onto the other's makes it a run
// of the dump. Everything between the numbers is still compared verbatim, so a different operator
// or constant is still a difference, and two numbers of one side may not become one number of the
// other (an excerpt that lost a node, or reads two nodes as one, would otherwise pass).
const DUMP_NUMBER = /(?<![0-9A-Za-z])[NOFCc][0-9A-Fa-f]*[0-9][0-9A-Fa-f]*(?![0-9A-Za-z])/g

// A dump line as the pieces between its numbers, so that two lines can be compared piece by piece.
function splitDumpLine(line) {
    const parts = []
    let last = 0
    for (const match of line.matchAll(DUMP_NUMBER)) {
        parts.push(line.slice(last, match.index), match[0])
        last = match.index + match[0].length
    }
    parts.push(line.slice(last))
    return parts
}

function withoutDumpNumbers(line) {
    return line.replace(DUMP_NUMBER, '<n>')
}

// Compare one excerpt line with one dump line under a renaming of the page's numbers onto the
// dump's. `renaming` (page number -> dump number) and `taken` (dump number -> page number) are
// carried across the lines of one candidate run, so the renaming is one-to-one and holds
// throughout the excerpt. The odd positions of a split line are its numbers: the even ones are the
// text, and they have to be equal.
function matchDumpLineUnderRenaming(pageLine, actualLine, renaming, taken) {
    const page = splitDumpLine(pageLine)
    const actual = splitDumpLine(actualLine)
    if (page.length !== actual.length) return false
    for (let i = 0; i < page.length; i++) {
        if (i % 2 === 0) {
            if (page[i] !== actual[i]) return false
            continue
        }
        const known = renaming.get(page[i])
        if (known !== undefined) {
            if (known !== actual[i]) return false
            continue
        }
        if (taken.has(actual[i])) return false
        renaming.set(page[i], actual[i])
        taken.set(actual[i], page[i])
    }
    return true
}

function compareDump(actualText, pageText) {
    const actual = normalizeDump(actualText).split('\n')
    const page = normalizeDump(pageText).split('\n')
    if (opensTheDump(page)) {
        for (let i = 0; i < Math.max(actual.length, page.length); i++) {
            if (actual[i] !== page[i]) {
                return `line ${i + 1}: page says ${JSON.stringify(page[i] ?? '<end>')}, the dump printed ${JSON.stringify(actual[i] ?? '<end>')}`
            }
        }
        return null
    }
    // An excerpt: some run of consecutive dump lines must match it, up to renaming the numbers of
    // one side one-to-one onto the other's.
    for (let start = 0; start + page.length <= actual.length; start++) {
        const renaming = new Map()
        const taken = new Map()
        let matched = true
        for (let offset = 0; offset < page.length; offset++) {
            if (!matchDumpLineUnderRenaming(page[offset], actual[start + offset], renaming, taken)) {
                matched = false
                break
            }
        }
        if (matched) return null
    }
    const missing = page.findIndex(
        (line) => !actual.some((other) => withoutDumpNumbers(other) === withoutDumpNumbers(line)),
    )
    if (missing >= 0) {
        return (
            `the excerpt is not in the dump: its line ${missing + 1} ${JSON.stringify(page[missing])} ` +
            `occurs nowhere in the ${actual.length} line(s) of the dump`
        )
    }
    return (
        `the excerpt's lines are all in the dump but are not one run of them, even after renaming ` +
        `the numbers of one side one-to-one onto the other's (its first line is ` +
        `${JSON.stringify(page[0])})`
    )
}

// ---- One snippet ---------------------------------------------------------------------------

function writeSnippet(snippet, tempDir) {
    const used = new Set()
    const files = []
    for (let i = 0; i < snippet.fences.length; i++) {
        const fence = snippet.fences[i]
        const fallback = snippet.fences.length === 1 ? 'snippet.cml' : i === 0 ? 'lib.cml' : 'main.cml'
        let name = fileNameOf(fence, fallback)
        if (used.has(name)) name = `${i}_${name}`
        used.add(name)
        const file = path.join(tempDir, name)
        fs.writeFileSync(file, fence.body.join('\n') + '\n')
        files.push({ name, file, body: fence.body.join('\n'), entry: entryOf(fence) })
    }
    let entry = files.find((file) => file.entry) || files[0]
    if (!entry.entry) {
        // A fence with no entry point is checked as a module: parse, GCT and GIR only need an
        // entry graph to build, and a generated main does not change the fence.
        fs.appendFileSync(entry.file, '\nfunc main(): int sync { return 0 }\n')
    }
    return { entry, files }
}

async function checkSnippet(documentAbs, snippet, lines, fences) {
    const group = snippet.fences
    const marked = group.map((fence) => fence.marker).find(Boolean)
    if (marked && ['fragment', 'counter-example'].includes(marked.kind)) {
        return { status: 'marked', kind: marked.kind, reason: marked.reason }
    }
    const compileOnly = marked && ['graph-dump', 'compile-only'].includes(marked.kind)

    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-doc-snippet-'))
    const problems = []
    try {
        // A tutorial page keeps its example programs next to it; a snippet that imports one of
        // them (`import shapes`) finds it, as the page says it should.
        const examples = path.join(path.dirname(documentAbs), 'examples')
        if (fs.existsSync(examples) && fs.statSync(examples).isDirectory()) {
            fs.cpSync(examples, tempDir, { recursive: true })
        }
        const written = writeSnippet(snippet, tempDir)
        const after = outputAfter(lines, fences, snippet)
        const inline = written.entry.body
            .split(/\r?\n/)
            .find((line) => /^\s*\/\/\s*camel\b/.test(line))
        const command = parseCommand(after && after.command) || (inline ? parseCommand({ body: [inline] }) : null)
        const passes = command ? command.passes : []
        const args = command ? command.args : []

        // Compile first: parse + GCT + GIR, whatever the snippet is.
        const compilePasses = passes.length > 0 ? passes : ['std::gir']
        const compiled = await runCamel([written.entry.file, ...compilePasses], tempDir)
        if (compiled.timedOut) {
            problems.push(`compiling timed out after ${TIMEOUT_MS} ms`)
        } else if (compiled.code !== 0) {
            problems.push(`does not compile: ${firstDiagnostic(compiled)}`)
        }

        if (problems.length > 0) {
            return { status: 'failed', problems, ms: compiled.ms }
        }

        // A dump is compared like any other text block: the graph pass writes the dump to stdout, so
        // it takes the run path below. A marker decides: `graph-dump` says the block is a dump and is
        // compared, `compile-only` says it is not this run's output and is left alone. Without a
        // marker a fence that names a graph pass is a dump, since that is what such a fence prints;
        // and a dump without a block has nothing to compare.
        const dump = Boolean(after) && (marked ? marked.kind === 'graph-dump' : printsAGraph(passes))

        if (!dump && (compileOnly || !after || isGraphDump(passes))) {
            return { status: 'compiled', ms: compiled.ms, dump }
        }

        if (!written.entry.entry) {
            return {
                status: 'failed',
                problems: [
                    dump
                        ? 'a graph dump is compared with the program that prints it, so the fence needs func main (with a generated one the dump would show a function the page never wrote)'
                        : 'declares an output but has no func main to print it',
                ],
                ms: compiled.ms,
                dump,
            }
        }

        const expected = parseExpected(after.output.body.join('\n'))
        const options = []
        for (let i = 0; i < args.length; i++) {
            const arg = args[i]
            if (arg === '--arg') {
                const pair = args[++i] ?? ''
                const eq = pair.indexOf('=')
                if (eq > 0) {
                    const name = pair.slice(0, eq)
                    const value = pair.slice(eq + 1)
                    // A path on the page is relative to the repository root; the snippet runs in
                    // its own temp directory.
                    const resolved = path.isAbsolute(value) ? value : path.resolve(ROOT, value)
                    options.push('--arg', `${name}=${fs.existsSync(resolved) ? resolved : value}`)
                } else {
                    options.push('--arg', pair)
                }
                continue
            }
            if (arg === '-I' || arg === '--include') {
                const value = args[++i] ?? ''
                options.push(arg, path.isAbsolute(value) ? value : path.resolve(ROOT, value))
                continue
            }
            options.push(arg)
        }
        const runArgs = [...options, written.entry.file, ...passes]
        const ran = await runCamel(runArgs, tempDir)
        if (ran.timedOut) {
            problems.push(`running timed out after ${TIMEOUT_MS} ms`)
        } else {
            const errorDiagnostic = expected.diagnostics.some((entry) => entry.severity === 'error')
            const want = errorDiagnostic ? 1 : 0
            if (ran.code !== want) {
                problems.push(`expected exit code ${want}, got ${ran.code}: ${firstDiagnostic(ran)}`)
            }
            const pageText = after.output.body.join('\n')
            if (dump) {
                const difference = compareDump(ran.stdout, pageText)
                if (difference) problems.push(`the graph differs from the text block: ${difference}`)
            } else {
                const hasLocation = /-->\s/.test(pageText)
                let difference
                if (errorDiagnostic && hasLocation) {
                    // The text block is the whole run: the lines printed before the fault, then the
                    // located diagnostic. Compare all of it (the path is normalised).
                    difference = compareText(ran.stdout, pageText)
                } else if (errorDiagnostic) {
                    // A truncated diagnostic: the page keeps the lines printed before it and names
                    // the diagnostic, but leaves out the location and the help.
                    difference = compareText(beforeDiagnostic(ran.stdout), expected.expected)
                } else {
                    // A warning goes to standard error; stdout is the part after the block.
                    difference = compareText(ran.stdout, expected.expected)
                }
                if (difference) problems.push(`output differs from the text block: ${difference}`)
                for (const entry of expected.diagnostics) {
                    if (!new RegExp(`${entry.severity}\\[${entry.name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\]`).test(`${ran.stdout}\n${ran.stderr}`)) {
                        problems.push(`the text block promises ${entry.severity}[${entry.name}], which was not reported`)
                    }
                }
            }
        }
        if (problems.length > 0) return { status: 'failed', problems, ms: compiled.ms + ran.ms, dump }
        return { status: dump ? 'dumped' : 'ran', ms: compiled.ms + ran.ms, dump }
    } finally {
        fs.rmSync(tempDir, { recursive: true, force: true })
    }
}

// ---- Main ----------------------------------------------------------------------------------

const args = ARGV.filter((arg) => !arg.startsWith('--'))
const entries = ARGV.includes('--default-roots') || args.length === 0 ? ['README.md', 'docs'] : args
// The default roots are the whole reader-facing scope; a run over named files or directories has
// seen only part of the tree, which is what the third pending-list rule below turns on.
const FULL_SCOPE = ARGV.includes('--default-roots') || args.length === 0
// The defect: the plan passed the script's own path, the scope came out empty, and the guard
// reported a pass. A scope entry that does not exist and a scan of no document are now refusals.
const missing = entries.filter((entry) => !fs.existsSync(path.resolve(ROOT, entry)))
if (missing.length > 0) {
    for (const entry of missing) console.log(`${entry}: no such file or directory`)
    console.log(`${missing.length} scope entr${missing.length === 1 ? 'y does' : 'ies do'} not exist; refusing to check a smaller tree`)
    process.exit(2)
}
const files = []
for (const entry of entries) collect(entry, files)
const unique = [...new Set(files)].sort()
if (unique.length === 0) {
    console.log('no reader-facing document in the given scope: nothing was checked')
    process.exit(2)
}

const started = Date.now()
const problems = []
const marked = []
const excerpts = []
const snippets = []
const scanned = []
const skippedFiles = []

// The pending list is a promise about the tree; a malformed entry is refused before anything runs,
// so a mistyped key cannot look like an entry that happily matches nothing later.
for (const entry of PENDING) {
    if (!entry.file || typeof entry.file !== 'string')
        problems.push('a pending entry does not name its page')
    if (typeof entry.key !== 'string' || !/^[0-9a-f]{16}$/.test(entry.key))
        problems.push(`${entry.file || '<no page>'}: a pending entry needs a 16 hex digit key (see snippetKey)`)
    if (!entry.reason || !entry.reason.trim())
        problems.push(`${entry.file || '<no page>'}: a pending entry needs one line saying which ruled construct the snippet is ahead of`)
}
if (PENDING.length > PENDING_MAX) {
    problems.push(
        `the pending list holds ${PENDING.length} entries, more than the ${PENDING_MAX} it declares: ` +
            'the list only shrinks, so adding one also raises PENDING_MAX, in the same reviewed edit',
    )
}
if (problems.length > 0) {
    for (const problem of problems) console.log(problem)
    console.log(`${problems.length} problem(s) in the pending list of scripts/check-doc-snippets.mjs`)
    process.exit(1)
}

for (const abs of unique) {
    const rel = relative(abs)
    const skip = allowedFile(abs)
    if (skip) {
        skippedFiles.push({ rel, reason: skip[1] })
        continue
    }
    scanned.push(rel)
    const text = fs.readFileSync(abs, 'utf8')
    const lines = text.split(/\r?\n/)
    const fences = parseFences(lines)
    if (!fences.some(isCamel)) continue
    for (const snippet of buildSnippets(lines, fences)) snippets.push({ rel, abs, snippet, lines, fences })
    for (const fence of fences) {
        if (!isCamel(fence)) continue
        if (fence.marker) {
            const { kind, reason } = fence.marker
            if (!MARKED_KINDS.has(kind)) {
                problems.push(`${rel}:${fence.open + 1}: unknown snippet marker '${kind}' (fragment, counter-example, graph-dump or compile-only)`)
                continue
            }
            if (!reason) {
                problems.push(`${rel}:${fence.open + 1}: the '${kind}' marker needs a reason`)
                continue
            }
            if (kind === 'counter-example') {
                const before = lines.slice(Math.max(0, fence.open - 8), fence.open).join('\n')
                if (!COUNTER_LABEL.test(`${before}\n${fence.body.join('\n')}\n${reason}`)) {
                    problems.push(`${rel}:${fence.open + 1}: a counter-example must be labelled as one on the page`)
                }
            }
            marked.push({ rel, line: fence.open + 1, kind, reason })
        }
    }
}

const excerptSnippets = snippets.filter(
    (entry) => excerptPage(entry.abs) && !entry.snippet.fences.some(entryOf),
)
for (const entry of excerptSnippets) {
    for (const fence of entry.snippet.fences) {
        excerpts.push({
            rel: entry.rel,
            line: fence.open + 1,
            reason: excerptPage(entry.abs)[1],
        })
    }
}

// Every fence is either a snippet, marked, or an excerpt; a fence inside a multi-file group is
// counted once. A fence with no entry point on a design/technical page is an excerpt (its
// reason is in EXCERPTS); everywhere else a fence with no entry point is compiled as a module
// with a generated main, so that declarations alone are still checked.
const checkedSnippets = snippets.filter((entry) => {
    if (entry.snippet.fences.some((fence) => fence.marker && ['fragment', 'counter-example'].includes(fence.marker.kind))) {
        return false
    }
    return !(excerptPage(entry.abs) && !entry.snippet.fences.some(entryOf))
})
// A page with no Camel fence is not a checked snippet: if the scope holds none at all, the guard
// examined no code and must say so instead of printing "no stale snippet".
if (checkedSnippets.length === 0) {
    console.log(`no Camel fence in ${scanned.length} reader-facing document(s): nothing was compiled`)
    process.exit(2)
}

let cursor = 0
const results = []
async function worker() {
    while (cursor < checkedSnippets.length) {
        const index = cursor++
        const entry = checkedSnippets[index]
        const result = await checkSnippet(entry.abs, entry.snippet, entry.lines, entry.fences)
        results[index] = { ...entry, result }
    }
}
await Promise.all(Array.from({ length: Math.min(JOBS, checkedSnippets.length || 1) }, worker))

let compiled = 0
let ran = 0
let dumped = 0
// A dump that fails is exempted by its pending entry like any other snippet ahead of the tree, so
// it never reaches the counters below; without this one the summary would report only the dumps
// that match and the reader could not tell how many were compared.
let dumpsAhead = 0
const pendingUsed = new Map()
const exempted = []
// A page may show one program at several points (chapter 17 shows the same program text at four
// points, each with a text block), so one key can stand for several snippets and they need not be at
// the same stage. An entry speaks for the text its key is made of: while one snippet of that key is
// still behind the tree the text is ahead of the tree, and the entry is what exempts that snippet.
// The criterion is "every snippet of the key has caught up", not "one of them has":
//
//   * "a snippet of the key that compiles makes the entry stale" asks for the entry to be deleted
//     while another snippet of the same text is still failing. The occurrences that "compile" need
//     not have been asserted at all: chapter 17's three graph dumps were compiled with their text
//     blocks left alone, so they counted as caught up and asked for the entry to be deleted, while
//     the run's own text block was still failing. Deleting it turns that failure red with nothing
//     changed in the tree, and the only way out would have been to weaken the page -- which is why
//     the dumps are compared now (see the header), and why the criterion is the whole key's;
//   * it cannot keep an entry past its gap: every occurrence is re-checked on every run, so the
//     moment the last lagging one catches up the entry is reported stale. No occurrence can catch up
//     unseen -- each is compiled, and each is run and compared when it has a text block.
//
// What the criterion does not do is aim an entry at one occurrence: the entry exempts a failure of
// any snippet of its key, including one whose reason was written for another occurrence. That
// exemption prints the first problem under the reason (see the exempted lines of the output), so it
// is visible rather than silent; a page whose occurrences are that far apart wants two entries with
// one key each, not a wider rule.
const pendingBehind = new Set()
const pendingCaughtUp = []

// What a snippet that was checked and did not fail did. A snippet whose key is in the pending list
// is counted here too when it now passes: its occurrence has caught up (the entry stays only while
// another occurrence of the same text is still behind), and leaving it out of every count would make
// the summary's numbers hide it -- a dump that matches but shares its text with a pending snippet
// would show up neither as compared nor as exempted.
function countChecked(result) {
    if (result.status === 'ran') ran++
    else if (result.status === 'dumped') dumped++
    else if (result.status === 'compiled') compiled++
}

for (const entry of results) {
    if (!entry) continue
    const result = entry.result
    const line = entry.snippet.open + 1
    const pending = pendingEntry(entry.rel, entry.snippet)
    if (pending) {
        pendingUsed.set(pending, { entry, result })
        if (result.status === 'failed') {
            // A pending snippet is ahead of the compiler, not stale: the page says what the language
            // is, and the tree has not caught up. The failure is named in the output and does not
            // make the gate red. Once it compiles (and prints its text block, if it has one) the
            // entry must go: the list is a record of the gap, and the gap closes.
            pendingBehind.add(pending)
            if (result.dump) dumpsAhead++
            exempted.push({ entry, pending, line, problems: result.problems })
        } else {
            pendingCaughtUp.push({ entry, pending, line })
            countChecked(result)
        }
        continue
    }
    if (result.status === 'failed') {
        for (const problem of result.problems) problems.push(`${entry.rel}:${line}: ${problem}`)
    } else {
        countChecked(result)
    }
}

// An entry that matched no snippet is refused under the default roots: the page changed, the fence
// grouping changed, or the entry was written for a snippet that is not there. Each case needs a
// human to re-key the entry or delete it, and silence is the one answer that would let the list
// drift away from the pages it speaks for.
if (FULL_SCOPE) {
    for (const pending of PENDING) {
        if (pendingUsed.has(pending)) continue
        // The keys of that page's snippets are printed with the failure: re-keying an entry means
        // copying one of them, and a run is the only place they are ever computed.
        const onPage = checkedSnippets
            .filter((entry) => entry.rel === pending.file)
            .map((entry) => `  ${pending.file}:${entry.snippet.open + 1} key ${snippetKey(entry.snippet)} ${JSON.stringify(snippetOpens(entry.snippet))}`)
        problems.push(
            `${pending.file || '<no page>'}: the pending entry with key ${pending.key || '<no key>'} matches no ` +
                'snippet of the pages scanned; re-key it to the snippet it speaks for, or remove it' +
                (onPage.length > 0 ? `\n${onPage.join('\n')}` : ''),
        )
    }
}

// The snippets of a key that caught up, reported only when every snippet of that key has: while one
// of them is still behind the tree the entry is what exempts it (see the note above `pendingBehind`).
for (const { entry, pending, line } of pendingCaughtUp) {
    if (pendingBehind.has(pending)) continue
    problems.push(
        `${entry.rel}:${line}: this snippet has a pending entry (key ${pending.key}, ` +
            `${JSON.stringify(snippetOpens(entry.snippet))}) and now compiles, so the entry is stale: ` +
            'remove it from PENDING in scripts/check-doc-snippets.mjs',
    )
}

for (const problem of problems) console.log(problem)

for (const { entry, pending, line, problems: failed } of exempted) {
    console.log(
        `pending ${entry.rel}:${line} (key ${pending.key}, ${JSON.stringify(snippetOpens(entry.snippet))}) ` +
            `is exempted: ${pending.reason} -- ${failed[0]}`,
    )
}
for (const pending of PENDING) {
    const used = pendingUsed.get(pending)
    const where = used ? `${used.entry.rel}:${used.entry.snippet.open + 1}` : 'unused in this scope'
    console.log(`pending list: ${pending.file} ${pending.key} (${where}): ${pending.reason}`)
}

const seconds = ((Date.now() - started) / 1000).toFixed(1)
if (problems.length > 0) {
    console.log(
        `${problems.length} stale snippet problem(s) in ${scanned.length} reader-facing document(s) ` +
            `(${checkedSnippets.length} snippet(s) checked, ${seconds} s)`,
    )
    process.exit(1)
}
console.log(
    `no stale snippet in ${scanned.length} reader-facing document(s): ` +
        `${checkedSnippets.length} snippet(s), ${compiled} compiled only, ${ran} ran their text block, ` +
        `${dumped} graph dump(s) compared and matching, ${dumpsAhead} compared and exempted as ahead of the tree, ` +
        `${marked.length} marked incomplete, ` +
        `${exempted.length} pending snippet(s) exempted (${PENDING.length} of at most ${PENDING_MAX} entries in the list), ` +
        `${excerpts.length} excerpt(s) on design/technical pages ` +
        `(${skippedFiles.length} generated/dated reference(s) allowlisted; ` +
        `${RECORDS.length} record directories not scanned; ${seconds} s)`,
)
