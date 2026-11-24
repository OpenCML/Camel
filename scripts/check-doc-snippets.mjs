#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fails when a reader-facing page shows a Camel snippet that the current language does not
// accept, or whose printed output is not what the page says it is; and when a generated
// reference's example, written as an `@example` line of `src/stdlib/**/*.cml`, does not compile.
//
//   node scripts/check-doc-snippets.mjs              both halves: the pages and the source examples
//   node scripts/check-doc-snippets.mjs --default-roots  the same, ignoring a script path argument
//   node scripts/check-doc-snippets.mjs --pages      the reader-facing pages only
//   node scripts/check-doc-snippets.mjs --source-examples
//                                                 the `@example` lines of src/stdlib only
//   node scripts/check-doc-snippets.mjs FILE DIR  check the given files and directories (pages)
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
//     generated `func main() => int sync { return 0 }`; the fence itself is not changed.
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
// The record directories (internal, docs/reviews) are not pages and are not scanned; three
// generated or dated references are allowlisted because their `camel` fences are signatures,
// diagnostics or excerpts of an earlier state, not programs. An allowlist entry is about the
// *fences*, not about the page: `docs/stdlib-reference.md` prints its examples as inline code,
// written as the `@example` lines of `src/stdlib/**/*.cml`, and the source half of this run
// compiles those. So "every example on the page compiles" is answered by looking at the place the
// example is written, not by skipping the page.
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
        'usage: node scripts/check-doc-snippets.mjs [--default-roots] [--pages|--source-examples] ' +
            '[--camel PATH] [FILE|DIR ...]',
    )
    process.exit(2)
}

// The compiler every snippet is run with: `--camel <path>` names it, else this tree's
// `out/latest/bin/camel`. One rule, in one place: test/tools/camel-arg.mjs.
const { camel: CAMEL, rest: ARGV } = takeCamel(process.argv.slice(2), ROOT, usage)

// Directories that are records, not pages: they quote the old spelling to say what happened.
const RECORDS = ['internal/', 'docs/reviews/']

// A generated reference is not a teaching page: its `camel` fences are signatures (one per
// overload) and the triggering examples of diagnostics, deliberately wrong. The second entry's
// examples are programs of the test suite pinned in src/kernel/diagnostics/examples.tsv (not
// `@example` lines), and the source half of this run does not read that file; the diagnostics
// plan compiles the examples it pins.
const ALLOWED_FILES = [
    ['docs/stdlib-reference.md', 'generated: the `camel` fences are signature lines, one per overload; its examples are checked at their source, the @example lines of src/stdlib'],
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
        file: 'docs/tutorial/17_teaching_a_law.cn.md',
        key: '12ee3b07b5c68a43',
        reason:
            'chapter 18: the `fact` compiles now; the text block asks for `check` to fire a law on ' +
            'a fact\'s conclusion (rewrite design 8.1), and `math:abs_l` is not translated',
    },
    {
        file: 'docs/tutorial/17_teaching_a_law.en.md',
        key: '12ee3b07b5c68a43',
        reason:
            'chapter 18: the `fact` compiles now; the text block asks for `check` to fire a law on ' +
            'a fact\'s conclusion (rewrite design 8.1), and `math:abs_l` is not translated',
    },
    {
        file: 'docs/language/03_data_model.md',
        key: '1160bed5c280bcaa',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/language/en/03_data_model.md',
        key: '1160bed5c280bcaa',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/language/05_modules.md',
        key: '262e1b8beb3c5ce2',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/language/en/05_modules.md',
        key: '262e1b8beb3c5ce2',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/language/06_expressions.md',
        key: '75ac69f1262f3640',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/language/06_expressions.md',
        key: '82445c807f7cb320',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/language/en/06_expressions.md',
        key: '75ac69f1262f3640',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/language/en/06_expressions.md',
        key: '82445c807f7cb320',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/reference/camel-for-ai.md',
        key: '2e04c65435a5af5b',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/reference/camel-for-ai.md',
        key: '4c59e07cb3168df6',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/05_arrays_and_loops.cn.md',
        key: '35950cb2759fddcc',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/05_arrays_and_loops.cn.md',
        key: '2232e8099ab54822',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/05_arrays_and_loops.cn.md',
        key: '0e9cbd1eed1f8681',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/05_arrays_and_loops.en.md',
        key: '35950cb2759fddcc',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/05_arrays_and_loops.en.md',
        key: '2232e8099ab54822',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/05_arrays_and_loops.en.md',
        key: '0e9cbd1eed1f8681',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/08_modules_and_stdlib.cn.md',
        key: '7c62e03d1c46ada5',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/08_modules_and_stdlib.cn.md',
        key: 'd212419bb682fccc',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/08_modules_and_stdlib.en.md',
        key: '7c62e03d1c46ada5',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/08_modules_and_stdlib.en.md',
        key: 'd212419bb682fccc',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/09_evaluation_by_dependency.cn.md',
        key: 'adc1676a0405cb50',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/09_evaluation_by_dependency.en.md',
        key: 'adc1676a0405cb50',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/tutorial.cn.md',
        key: '3349eaa84b0532ae',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/tutorial.cn.md',
        key: '49e18a2d16879dcb',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/tutorial.cn.md',
        key: '2a6c7d995f322bdf',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/tutorial.en.md',
        key: '3349eaa84b0532ae',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/tutorial.en.md',
        key: '49e18a2d16879dcb',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
    {
        file: 'docs/tutorial/tutorial.en.md',
        key: '2a6c7d995f322bdf',
        reason:
            'range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a ' +
            '`Range` still shares no `map`/`filter`/`reduce`/`len`, and `to_array` is not ' +
            'reachable from the page; `D-1123-81` sends these sites back to `0 .. n` once it does',
    },
]

const PENDING_MAX = 30

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
// runs from a temporary file. The compiler writes that path exactly as the command line spells
// it (`camel fault.cml` prints `fault.cml:4:15`, an absolute argument prints an absolute path;
// see 4.6 of the language reference), and this checker hands over an absolute path, so the
// normalisation below has to erase a whole path however it is written -- on Windows that
// includes the drive letter and its colon, which is why the pattern takes one: the reading a
// page gets must not depend on which machine ran the checker.
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
    // `--> D:\path\to\fault.cml:4:15`, `--> /tmp/x/fault.cml:4:15`, `--> .\fault.cml:4:15` and
    // `--> fault.cml:4:15` are one reading: the location line is compared as the line and column
    // of a `.cml` file, not as the path of the machine the checker ran on. `[A-Za-z]:` is the
    // drive letter, which the older pattern left in front of `<file>` (it could not cross the
    // colon), so a Windows run compared `D:<file>:4:15` against the page's `<file>:4:15`.
    const normalize = (text) =>
        text
            .replace(/\r\n/g, '\n')
            .replace(/(?:[A-Za-z]:)?(?:[^\s:()]*[\\/])*[A-Za-z0-9_.-]+\.cml:(\d+):(\d+)/g, '<file>:$1:$2')
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
        fs.appendFileSync(entry.file, '\nfunc main() => int sync { return 0 }\n')
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

// ---- The standard library's own examples -----------------------------------------------------
//
// A generated reference is a copy of something. `docs/stdlib-reference.md` is a copy of the
// `@example` lines of `src/stdlib/**/*.cml`: its own `camel` fences are signature lines, one per
// overload, and the examples are printed as inline code, so a page check can neither compile them
// nor see them. An allowlist entry that says "generated" is therefore not an answer to "are the
// examples spelled right" -- the promise is about the `.cml` lines, and this pass compiles them
// where they are written.
//
// The convention is the reference generator's (`scripts/stdlib-reference.mjs`), because the page is
// what a reader compares an example with: the module's names are in scope, the example's arguments
// are bound to the declared parameter types, the types it mentions are imported, an expression
// becomes `let __doc_example = ...`, and one that starts with a statement keyword is a statement (a
// call that returns nothing is retried as one). A `@example-fragment <reason>` says the example is
// not a program of its own: it is compiled too, and a *semantic* failure is exempted -- the marker
// names something the example needs and has not got -- while a syntax or lexical failure never is,
// because no marker makes a spelling the parser refuses into a legal one.
const STDLIB_ROOT = 'src/stdlib'
const DOC_NAME = /^[A-Za-z_]\w*$/
const DOC_RESERVED = new Set([
    'true', 'false', 'null', 'func', 'let', 'var', 'if', 'else', 'for', 'while', 'match', 'return',
    'sync', 'with', 'type', 'struct', 'enum', 'rule', 'export', 'module', 'import', 'as', 'is',
])
// Types that resolve nowhere because they are built in. `Map` and `Set` included, as in the
// reference generator: a signature file never imports them.
const DOC_BUILTIN_TYPES = new Set([
    'int', 'i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64', 'usize',
    'float', 'f32', 'f64', 'bool', 'string', 'char', 'void', 'null', 'any', 'never',
    'Map', 'Set', 'Bytes', 'T', 'U', 'V', 'K', 'P', 'R', 'A', 'F',
])
const DOC_STATEMENT = /^(let|var|if|for|while|return|sync|match)\b/
const DOC_DIAGNOSTIC_CLASS = /(?:name=)?([A-Za-z]+)Diag::/

function collectSource(dir, out) {
    const abs = path.resolve(ROOT, dir)
    for (const name of fs.readdirSync(abs).sort()) {
        const child = path.join(abs, name)
        if (fs.statSync(child).isDirectory()) collectSource(path.join(dir, name), out)
        else if (name.endsWith('.cml')) out.push(relative(child))
    }
}

// The module a `.cml` file belongs to, and which of the three shapes it is: a module written in
// Camel (`src/stdlib/<name>.cml`), a body-less signature file (`<name>/signatures/<file>.cml`,
// whose declarations stand for C++ operators and do not compile by themselves), or a rule set
// (`rulesets/<file>.cml`, which is imported by a transform rather than as a module).
function docFileOf(rel) {
    const parts = rel.split('/')
    const file = parts[parts.length - 1]
    const stem = file.replace(/\.cml$/, '')
    if (parts.length >= 5 && parts[3] === 'signatures') {
        return { module: parts[2], signature: true, ruleset: false }
    }
    if (parts[2] === 'rulesets') return { module: null, signature: false, ruleset: true }
    return { module: stem === 'prelude' ? 'globals' : stem, signature: false, ruleset: false }
}

// `skipBalanced`, `splitArguments`, `typeWithoutDefault` and `findCall` are the reference
// generator's, kept here because this pass is about the same text: an example's call is found by
// its own name, with the receiver of `->` as argument 0, and the arguments are split at top-level
// commas only.
function docSkipBalanced(text, start, open, close) {
    let depth = 0
    for (let index = start; index < text.length; index++) {
        if (text[index] === open) depth++
        else if (text[index] === close) {
            depth--
            if (depth === 0) return index + 1
        }
    }
    return text.length
}

function docFindCall(code, name) {
    const pattern = new RegExp(`\\b${name}\\b`, 'g')
    let match
    while ((match = pattern.exec(code))) {
        let index = match.index + name.length
        while (index < code.length && /\s/.test(code[index])) index++
        if (code[index] === '<') index = docSkipBalanced(code, index, '<', '>')
        while (index < code.length && /\s/.test(code[index])) index++
        let args = null
        if (code[index] === '(') {
            const end = docSkipBalanced(code, index, '(', ')')
            args = code.slice(index + 1, end - 1)
        }
        const receiver = code.slice(0, match.index).match(/([A-Za-z_]\w*)\s*->\s*$/)
        return { receiver: receiver ? receiver[1] : null, args }
    }
    return null
}

function docSplitArguments(text) {
    if (text === null) return []
    const parts = []
    let depth = 0
    let quote = ''
    let start = 0
    for (let index = 0; index < text.length; index++) {
        const char = text[index]
        if (quote) {
            if (char === quote && text[index - 1] !== '\\') quote = ''
            continue
        }
        if (char === "'" || char === '"') quote = char
        else if ('([{<'.includes(char)) depth++
        else if (')]}>'.includes(char)) depth--
        else if (char === ',' && depth === 0) {
            parts.push(text.slice(start, index).trim())
            start = index + 1
        }
    }
    if (text.slice(start).trim() !== '') parts.push(text.slice(start).trim())
    return parts
}

function docTypeWithoutDefault(type) {
    let depth = 0
    for (let i = 0; i < type.length; i++) {
        const char = type[i]
        if ('([{<'.includes(char)) depth++
        else if (')]}>'.includes(char)) depth--
        else if (char === '=' && type[i + 1] !== '>' && depth === 0) return type.slice(0, i).trim()
    }
    return type.trim()
}

// A parameter list's entries: `name: type`, a pack's dots before the name, an optional `?` after
// it. A type variable makes a parameter unwritable and binds nothing (`typeAtScalar`), so the
// example has to supply that value itself, as the reference generator says.
function docParams(inner) {
    return docSplitArguments(inner)
        .map((text) => {
            const match = text.match(/^(?:\.{2,3})?(\w+)\s*\??\s*:\s*(.+)$/)
            return match ? { text, name: match[1], type: docTypeWithoutDefault(match[2]) } : null
        })
        .filter(Boolean)
}

function docTypeAtScalar(type) {
    const text = (type || '').trim()
    if (!/\b[A-Z]\b/.test(text)) return text
    if (!text.includes('=>')) return null
    return text.replace(/\b[A-Z]\b/g, 'float')
}

// The *code* of one `@example`: the `// result` a page appends to it is the page's, not the
// example's. The split is the generator's (two or more spaces, then `//`).
function docExampleCode(text) {
    return text.split(/\s{2,}\/\/\s*/)[0].trim()
}

// The angle group of a `with <...>` list, closing on `>` and stepping over the `>` of `=>`.
function docAngleEnd(text, start) {
    let depth = 0
    for (let i = start; i < text.length; i++) {
        if (text[i] === '=' && text[i + 1] === '>') {
            i++
            continue
        }
        if (text[i] === '<') depth++
        else if (text[i] === '>' && --depth === 0) return i
    }
    return -1
}

// The declaration a `///` block belongs to. The block may be followed by blank lines, a `#op(...)`
// tag and a `with <...>` list -- all of them the declaration's front matter -- and the parameter
// list itself may run over several lines, which is how the formatter writes a long one.
function docDeclarationBelow(lines, start) {
    let k = start
    let withText = ''
    while (k < lines.length) {
        const text = lines[k].trim()
        if (text === '' || text.startsWith('//') || text.startsWith('#')) {
            k++
            continue
        }
        if (/^with\s*</.test(text)) {
            let acc = text
            while (docAngleEnd(acc, acc.indexOf('<')) < 0 && k + 1 < lines.length) {
                k++
                acc += ' ' + lines[k].trim()
            }
            withText += ' ' + acc
            k++
            continue
        }
        break
    }
    if (k >= lines.length) return null
    const declaration = lines[k]
    const func = /^\s*(?:export\s+)?func\s+([A-Za-z_]\w*)/.exec(declaration)
    if (func) {
        let text = declaration
        let end = k
        const open = text.indexOf('(', func.index + func[0].length)
        while (open >= 0 && docSkipBalanced(text, open, '(', ')') >= text.length && end + 1 < lines.length) {
            end++
            text += ' ' + lines[end].trim()
        }
        const close = open < 0 ? -1 : docSkipBalanced(text, open, '(', ')')
        const inner = close < 0 ? '' : text.slice(open + 1, close - 1)
        const angle = withText.indexOf('<')
        const withInner = angle < 0 ? '' : withText.slice(angle + 1, docAngleEnd(withText, angle))
        const tail = close < 0 ? '' : text.slice(close)
        const result = /=>\s*([^{]+?)\s*(?=[{]|when\b|uses\b|sync\b|$)/.exec(tail)?.[1] || null
        return { name: func[1], line: k + 1, params: docParams(inner), withParams: docParams(withInner), result }
    }
    const value = /^\s*(?:export\s+)?let\s+([A-Za-z_]\w*)\s*:\s*(.+)$/.exec(declaration)
    if (value) return { name: value[1], line: k + 1, params: [], withParams: [], result: value[2].trim() }
    // A type or a rule is documented the same way, and its example is about the name rather than a
    // call of it (`@example is_same::<PayloadOf<float?>, float>()` above `export type PayloadOf`).
    // A rule's parameters bind like a function's; a type's do not bind at all.
    const rule = /^\s*(?:export\s+)?rule\s+([A-Za-z_]\w*)/.exec(declaration)
    if (rule) {
        const open = declaration.indexOf('(', rule.index + rule[0].length)
        const close = open < 0 ? -1 : docSkipBalanced(declaration, open, '(', ')')
        const inner = close < 0 ? '' : declaration.slice(open + 1, close - 1)
        return { name: rule[1], line: k + 1, params: docParams(inner), withParams: [], result: null }
    }
    const type = /^\s*(?:export\s+)?(?:type|struct|enum|tag)\s+([A-Za-z_]\w*)/.exec(declaration)
    if (type) return { name: type[1], line: k + 1, params: [], withParams: [], result: null }
    return null
}

// Every `@example` of one `///` block, in the order written, with the line it is on.
function docBlockEntries(lines, start, end) {
    const examples = []
    const fragments = []
    for (let i = start; i < end; i++) {
        const text = lines[i].replace(/^\s*\/\/\/\s?/, '')
        if (text.startsWith('@example-fragment')) fragments.push({ text: text.slice('@example-fragment'.length).trim(), line: i + 1 })
        else if (text.startsWith('@example')) examples.push({ text: text.slice('@example'.length).trim(), line: i + 1 })
    }
    return { examples, fragments }
}

// The names the example's arguments bind: the receiver of `->` is parameter 0 and argument i is
// parameter i (shifted by the receiver). A literal or an expression is not a name and binds
// nothing.
function docBindings(entry, code) {
    const call = docFindCall(code, entry.name)
    if (!call) return []
    const params = entry.params
    const bindings = []
    const bind = (name, param) => {
        if (!param || !DOC_NAME.test(name) || DOC_RESERVED.has(name)) return
        const type = docTypeAtScalar(docTypeWithoutDefault(param.type))
        if (type === null) return
        if (!bindings.some((binding) => binding.name === name)) bindings.push({ name, type })
    }
    if (call.receiver) bind(call.receiver, entry.params[0])
    docSplitArguments(call.args).forEach((argument, index) => {
        bind(argument, params[call.receiver ? index + 1 : index])
    })
    return bindings
}

// The capitalized names the example needs in scope: the types of the parameters it binds, and the
// ones it writes itself (`(j: Json) => ...`, `is_same::<Tangent<f32>, f32>()`).
function docTypeNames(bindings, code) {
    const names = []
    for (const binding of bindings) {
        for (const name of binding.type.match(/\b[A-Z]\w*/g) || []) if (!names.includes(name)) names.push(name)
    }
    for (const name of code.match(/\b[A-Z]\w*/g) || []) if (!names.includes(name)) names.push(name)
    return names.filter((name) => !DOC_BUILTIN_TYPES.has(name))
}

// One example as a program: the module's documented names in scope, the arguments bound to their
// declared types, the example inside a function, and an entry point. An example that starts with a
// statement keyword is used as a statement; anything else is an expression. A call that returns
// nothing cannot be bound, so the caller retries a failed expression as a statement.
function docExampleSource(context, entry, code, asStatement) {
    const lines = []
    if (context.ruleset) {
        // A rule set is not importable as a module: its own text is what gives the example its
        // names (its types, its rules, and the imports it already writes).
        lines.push(context.text)
    } else if (entry.module !== 'globals') {
        if (context.names.length > 0) lines.push(`import { ${context.names.join(', ')} } from ${entry.module}`)
    }
    for (const [name, owner] of context.typeImports) {
        lines.push(`import { ${name} } from ${owner}`)
    }
    const bindings = docBindings(entry, code)
    const params = bindings.map((binding) => `${binding.name}: ${binding.type}`).join(', ')
    const fallible = /^return\s+error\(/.test(code)
    lines.push(`func __doc_example(${params}) => ${fallible ? 'int!' : 'void'} {`)
    if (asStatement || entry.result === 'void' || DOC_STATEMENT.test(code)) {
        lines.push(`    ${code}`)
    } else {
        lines.push(`    let __doc_example = ${code}`)
    }
    lines.push('}')
    lines.push('func main() => int sync { return 0 }')
    return lines.join('\n') + '\n'
}

// One example compiled: what the fragment compiler said, and which of the two writings was used.
async function compileDocExample(context, entry, code) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-doc-example-'))
    try {
        const file = path.join(dir, 'example.cml')
        let result = null
        for (const asStatement of [false, true]) {
            fs.writeFileSync(file, docExampleSource(context, entry, code, asStatement))
            result = await runCamel([file, 'std::gir'], dir)
            if (result.code === 0) return { ok: true, result }
            // A statement retry is for a call that returns nothing; a syntax error is the same
            // both ways, so only retry what parsed.
            if (result.timedOut || /SyntaxDiag|LexicalDiag/.test(`${result.stdout}\n${result.stderr}`)) break
        }
        return { ok: false, result }
    } finally {
        fs.rmSync(dir, { recursive: true, force: true })
    }
}

// The whole source pass. Returns the red lines and the counts the summary prints.
async function checkSourceExamples() {
    const files = []
    collectSource(STDLIB_ROOT, files)
    if (files.length === 0) {
        console.log(`${STDLIB_ROOT}: no .cml file found; refusing to report a pass over no source`)
        return { failed: true }
    }
    // Every exported type, so an example's capitalized name can be imported from the module that
    // declares it (`Match` is `re`'s, `Json` is `json`'s). A module's *library* file wins over a
    // signature file: `Tangent` is declared both by `transforms`'s signature file and by `types`,
    // and the one an `import` brings is `types`'s.
    const typeOwners = new Map()
    const fileContexts = new Map()
    for (const rel of files) {
        const text = fs.readFileSync(path.resolve(ROOT, rel), 'utf8')
        const info = docFileOf(rel)
        const types = [...text.matchAll(/^\s*export\s+type\s+([A-Za-z_]\w*)/gm)].map((one) => one[1])
        fileContexts.set(rel, { ...info, text, types })
        if (info.module && !info.ruleset && !info.signature) {
            for (const name of types) if (!typeOwners.has(name)) typeOwners.set(name, info.module)
        }
    }
    for (const rel of files) {
        const context = fileContexts.get(rel)
        if (!context.module || context.ruleset || !context.signature) continue
        for (const name of context.types) if (!typeOwners.has(name)) typeOwners.set(name, context.module)
    }
    // Which declarations a file documents. A module's examples see the names its module documents,
    // library file and signature file together -- `zeros` is documented in
    // `tensor/signatures/tensor.cml` and called by an example of `tensor.cml`, which is the
    // reference generator's convention. So every file is parsed before any example is compiled.
    const entryNames = new Map()
    const parsed = []
    for (const rel of files) {
        const context = fileContexts.get(rel)
        const lines = context.text.replace(/\r\n/g, '\n').split('\n')
        const blocks = []
        for (let i = 0; i < lines.length; i++) {
            if (!/^\s*\/\/\//.test(lines[i])) continue
            const blockStart = i
            while (i < lines.length && /^\s*\/\/\//.test(lines[i])) i++
            const block = docBlockEntries(lines, blockStart, i)
            if (block.examples.length === 0) continue
            const declaration = docDeclarationBelow(lines, i)
            const entry = declaration ? { ...declaration, module: context.module, fragments: block.fragments } : null
            if (entry !== null && context.module && !context.ruleset) {
                if (!entryNames.has(context.module)) entryNames.set(context.module, new Set())
                entryNames.get(context.module).add(entry.name)
            }
            blocks.push({ entry, examples: block.examples, fragments: block.fragments })
        }
        parsed.push({ rel, context, blocks })
    }
    const jobs = []
    for (const { rel, context, blocks } of parsed) {
        for (const block of blocks) {
            if (block.entry === null) {
                for (const example of block.examples) {
                    jobs.push({
                        rel, line: example.line, code: docExampleCode(example.text), entry: null,
                        fragments: block.fragments, context,
                    })
                }
                continue
            }
            const entry = block.entry
            // A documented *type* is imported from the module that exports it, which is not alway the
            // one the example is written in: `import { Tangent } from transforms` brings no type,
            // and `types` does. A name the module line already carries is not imported twice.
            const names = entry.module && !context.ruleset && entry.module !== 'globals' ? [...(entryNames.get(entry.module) || [])] : []
            const baseTypes = new Map()
            const moduleNames = []
            for (const name of names) {
                const owner = typeOwners.get(name)
                if (owner && owner !== 'globals') baseTypes.set(name, owner)
                else moduleNames.push(name)
            }
            for (const example of block.examples) {
                const code = docExampleCode(example.text)
                const typeImports = new Map(baseTypes)
                if (!context.ruleset) {
                    for (const name of docTypeNames(docBindings(entry, code), code)) {
                        const owner = typeOwners.get(name)
                        if (owner && owner !== 'globals' && !moduleNames.includes(name)) typeImports.set(name, owner)
                    }
                }
                jobs.push({ rel, line: example.line, code, entry, fragments: block.fragments, context: { ...context, typeImports, names: moduleNames } })
            }
        }
    }
    if (jobs.length === 0) {
        console.log(`${STDLIB_ROOT}: no @example found; refusing to report a pass over no example`)
        return { failed: true }
    }
    const results = new Array(jobs.length)
    let next = 0
    const worker = async () => {
        while (next < jobs.length) {
            const index = next++
            const job = jobs[index]
            if (job.entry === null) {
                results[index] = { ...job, ok: false, diagnostic: 'the @example has no declaration below it to bind its names' }
                continue
            }
            const compiled = await compileDocExample(job.context, job.entry, job.code)
            const text = `${compiled.result.stdout}\n${compiled.result.stderr}`
            const match = DOC_DIAGNOSTIC_CLASS.exec(text)
            const kind = match ? match[1].toLowerCase() : null
            results[index] = {
                ...job,
                ok: compiled.ok,
                kind,
                excused: !compiled.ok && job.fragments.length > 0 && (kind === 'semantic' || kind === 'internal'),
                diagnostic: compiled.ok ? '' : firstDiagnostic(compiled.result),
            }
        }
    }
    await Promise.all(Array.from({ length: Math.min(JOBS, jobs.length) }, worker))

    const red = results.filter((one) => !one.ok && !one.excused)
    const excused = results.filter((one) => one.excused)
    const classes = new Map()
    for (const one of red) {
        const label = one.kind ? `a ${one.kind} error` : 'no diagnostic class'
        classes.set(label, (classes.get(label) || 0) + 1)
    }
    for (const one of red) {
        console.log(`${one.rel}:${one.line}: ${one.kind ? `a ${one.kind} error` : 'no diagnostic class'} -- ${one.code}`)
        console.log(`    ${one.diagnostic}`)
    }
    for (const [label, count] of [...classes].sort(([a], [b]) => a.localeCompare(b))) {
        console.log(`    ${label}: ${count}`)
    }
    console.log(
        `${red.length} of the standard library's ${jobs.length} @example(s) do not compile` +
            `${excused.length > 0 ? `, ${excused.length} more are exempted by their @example-fragment marker` : ''}: ` +
            'each line above is the file, the line and the example as it is written',
    )
    return { failed: red.length > 0 }
}

// ---- Main ----------------------------------------------------------------------------------

const args = ARGV.filter((arg) => !arg.startsWith('--'))
// Two halves of one question. The pages half reads the reader-facing documents; the source half
// reads the `@example` lines of `src/stdlib/**/*.cml`, which is where the generated reference's
// examples are written. A run over named files or directories is the pages half (the fixtures of
// its plan); the default run is both, because a generated page is a copy and the promise is about
// the source.
const wantSources = ARGV.includes('--source-examples') || (args.length === 0 && !ARGV.includes('--pages'))
const wantPages = !ARGV.includes('--pages') && (args.length > 0 || !ARGV.includes('--source-examples'))
let sourceFailed = false
if (wantSources) {
    sourceFailed = (await checkSourceExamples()).failed
    if (!wantPages) process.exit(sourceFailed ? 1 : 0)
}
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
// The roster says, for every entry of the list, whether it is doing anything in this scope. Without
// the word at the end, an entry that waits and an entry whose snippet has caught up print the same
// line, and a reader takes the list for a set of exemptions that is in force when part of it is only
// a debt already paid: exactly one thing has to be done about the second kind, and nothing about the
// first.
const caughtUpKeys = new Set(pendingCaughtUp.map(({ pending }) => pending))
for (const pending of PENDING) {
    const used = pendingUsed.get(pending)
    const where = used ? `${used.entry.rel}:${used.entry.snippet.open + 1}` : 'unused in this scope'
    const state = pendingBehind.has(pending)
        ? 'waiting: its snippet does not compile yet in this scope'
        : caughtUpKeys.has(pending)
            ? 'STALE: every snippet of its key compiles now, so this entry must be deleted'
            : 'unused: no snippet of this scope is its text'
    console.log(`pending list: ${pending.file} ${pending.key} (${where}): ${pending.reason} -- ${state}`)
}

// One line that answers "how much is still waiting on a ruled construct, and on which one". The
// number and the reason are both read from the list on every run, so a snippet that catches up, or
// an entry whose reason is rewritten, changes this line without anybody remembering to change it --
// the pending list is a debt with a shrinking balance, not an exemption that stays in force.
const waiting = new Map()
for (const pending of PENDING) {
    if (!pendingBehind.has(pending)) continue
    const count = pendingUsed.get(pending) ? 1 : 0
    if (count === 0) continue
    waiting.set(pending.reason, (waiting.get(pending.reason) || 0) + 1)
}
if (waiting.size === 0) {
    console.log(
        `[PENDING] 0 snippet(s) wait for a ruled construct: the list holds ${PENDING.length} ` +
            `entr${PENDING.length === 1 ? 'y' : 'ies'} and none of them is behind in this scope`,
    )
} else {
    for (const [reason, count] of waiting) {
        console.log(`[PENDING] ${count} snippet(s) wait for ${reason}`)
    }
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
// The two halves are one answer: a page that holds is not enough when the examples it prints were
// not compiled at their source.
if (sourceFailed) process.exit(1)
