#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Only the semantic layer enumerates the kinds of an edge.
 *
 * The target architecture (section 7, "守护") states the
 * invariant and names its mechanical check: `orderPredsOf`, `syncPredsOf` and the queries of the
 * same kind are allowed only in the semantic layer's files. This script is that check, in
 * report-only mode.
 *
 * That page is not in this tree as of 2026-11-14: it is the design branch's (`origin/fsh-dev`), so
 * section 7 is quoted here rather than linked -- a link to a page this tree does not hold is a
 * broken link, and `scripts/check-doc-links.mjs --citations` says so.
 *
 * --- what "the semantic layer" is here -------------------------------------
 * It is derived from the source, not from a list of files a person keeps in step by hand. A named
 * symbol's **home** is
 *
 *   1. the directory tree of the header that declares it, recursively,
 *   2. the file that declares or defines it -- its `.cpp` implementation, or the header itself when
 *      the definition is inline (both of the symbols below are inline in `graph.h`), and
 *   3. the implementation files named after a declaring header: `graph.h` pairs with `graph.cpp`
 *      wherever that `.cpp` sits, which is how the class's own implementation file comes along
 *      without a list of paths here.
 *
 * A use of the symbol outside that home is a crossing, reported once per use (`path:line:
 * symbol`). The declaring file is found by scanning every source file for a declaration-shaped
 * line -- `bool isPure(OperId id) const;`, `std::span<const GCGraphOrderEdge>
 * orderPredsOf(gc_node_ref_t ref) const {` -- so a symbol that moves is followed by this check with
 * no edit here (see `declaresSymbol`). When a named symbol has no declaring file at all, the symbol
 * no longer exists under that name and this script refuses (exit 2) instead of passing over
 * nothing.
 *
 * For `orderPredsOf` and `syncPredsOf` that home is `include/camel/runtime/`,
 * `include/camel/runtime/graph.h` and `src/runtime/graph.cpp`. That is why `src/runtime/draft.cpp`
 * -- a draft copying a frozen graph's order and sync edges -- is a crossing: the header is where
 * the vocabulary is declared and the class's own implementation is where it is defined, and a
 * reader elsewhere is neither.
 *
 * **This rule is mechanical, and it is the author's to change.** If a directory belongs to the
 * semantic layer and is not under a declaring header's tree, the honest fix is either to move the
 * declaration into it or to add that directory here, deliberately, with the reason written beside
 * it -- that is what "the kinds of an edge are enumerated in one place" means once it is written
 * down. Two things this rule therefore deliberately does *not* do:
 *
 *   - it does not treat `src/runtime/` as a whole as the semantic layer. Several files there read
 *     the side arrays (`canonical.cpp`, `draft*.cpp`, `graph_edges.cpp`, `resources.cpp`,
 *     `rules.cpp`) and the invariant's point is that they should not have to;
 *   - it does not guess at "the same kind of query" for you. Section 7 says this row covers them,
 *     and these are the names that would be added to `SYMBOLS` after the argument that says which
 *     of them are the edge's kind and which are an ordinary user/predecessor query:
 *     `ctrlInputsOf`, `ctrlOutputsOf`, `ctrlEdgesOf`, `orderUsersOf`, `syncUsersOf`, `orderEdges`,
 *     `syncEdges`, `hasWaitEdge`, `isSyncStatement`.
 *
 * --- usage -----------------------------------------------------------------
 *   node scripts/check-edge-kind-layer.mjs                the list, in report-only mode (exit 0)
 *   node scripts/check-edge-kind-layer.mjs --enforce       exit 1 when the list is not empty
 *   node scripts/check-edge-kind-layer.mjs --path <p>      scan another file or directory (a sample)
 *
 * The gate runs the bare form: this file prints the list and the count, and the count is the
 * baseline the list has to come down from. `--enforce` is the mode the gate entry becomes once the
 * list is empty.
 */

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const NAME = 'check-edge-kind-layer'
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

/** The directories the scan covers by default, the same set the other source checks read. */
const SCANNED = ['include', 'src', 'modules', 'tools']

const SOURCE = /\.(?:h|hpp|hh|hxx|cpp|cc|cxx)$/

/**
 * The named symbols. `orderPredsOf` and `syncPredsOf` are the two section 7 writes down; a symbol
 * added here is a symbol whose declaring header becomes home (see the file comment).
 */
const SYMBOLS = ['orderPredsOf', 'syncPredsOf']

/** Replaces comments and string/character literals with spaces, so offsets and lines survive. */
function stripCommentsAndStrings(text) {
    const out = text.split('')
    let index = 0
    while (index < out.length) {
        const two = text.slice(index, index + 2)
        if (two === '//') {
            while (index < out.length && text[index] !== '\n') out[index++] = ' '
        } else if (two === '/*') {
            out[index++] = ' '
            out[index++] = ' '
            while (index < out.length && text.slice(index, index + 2) !== '*/') {
                if (text[index] !== '\n') out[index] = ' '
                index += 1
            }
            if (index < out.length) {
                out[index++] = ' '
                out[index++] = ' '
            }
        } else if (text[index] === '"' || text[index] === "'") {
            const quote = text[index]
            out[index++] = ' '
            while (index < out.length && text[index] !== quote) {
                if (text[index] === '\\') out[index++] = ' '
                if (index < out.length) {
                    if (text[index] !== '\n') out[index] = ' '
                    index += 1
                }
            }
            if (index < out.length) out[index++] = ' '
        } else {
            index += 1
        }
    }
    return out.join('')
}

/**
 * Whether `line` is the declaration or the definition of `symbol`, rather than a use of it.
 *
 * The shapes C++ writes here are `bool isPure(OperId id) const;` and
 * `std::span<const GCGraphOrderEdge> orderPredsOf(gc_node_ref_t ref) const {`: a prefix that is a
 * type (so no `=`, no `(`, no `;`, no braces), the name, one parenthesised parameter list, an
 * optional `const`/`noexcept`, and the line ends at `;` or `{`. A call never matches, because a
 * call either has a qualifier (`graph.syncPredsOf(ref)`, `graph->syncPredsOf(ref)`) or continues
 * past the `)` (`.empty()`, `)`, `,`). The two qualifier shapes are excluded explicitly: their
 * prefix ends with `.` or `->`, which a type prefix never does.
 */
function declaresSymbol(line, symbol) {
    const expression = new RegExp(
        `^([^;{}()=]*[\\s>:])${symbol}\\s*\\([^()]*\\)\\s*(?:const\\s*)?(?:noexcept\\s*)?[;{]\\s*$`,
    )
    const match = expression.exec(line.trim())
    if (match === null) return false
    return !/(?:->|\.)$/.test(match[1])
}

function sourceFilesUnder(target) {
    const stat = fs.statSync(target)
    if (stat.isFile()) return SOURCE.test(target) ? [target] : []
    const files = []
    const walk = (dir) => {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            if (entry.name.startsWith('.') || entry.name === 'node_modules' || entry.name === 'build') {
                continue
            }
            const full = path.join(dir, entry.name)
            if (entry.isDirectory()) walk(full)
            else if (SOURCE.test(entry.name)) files.push(full)
        }
    }
    walk(target)
    return files
}

const shown = (file) => path.relative(ROOT, file).split(path.sep).join('/')

/** Every named symbol's declaring file, read out of the source and never out of a list here. */
function declaringFiles(files) {
    const declaring = new Map(SYMBOLS.map((symbol) => [symbol, []]))
    for (const file of files) {
        const text = stripCommentsAndStrings(fs.readFileSync(file, 'utf8'))
        for (const line of text.split('\n')) {
            for (const symbol of SYMBOLS) {
                if (declaresSymbol(line, symbol)) declaring.get(symbol).push(file)
            }
        }
    }
    for (const [symbol, found] of declaring) {
        if (found.length === 0) {
            console.error(
                `${NAME}: no file declares or defines '${symbol}', so this check would report a home ` +
                    'it never found and prove nothing',
            )
            process.exit(2)
        }
    }
    return declaring
}

const argv = process.argv.slice(2)
const enforce = argv.includes('--enforce')
const paths = []
for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index]
    if (arg === '--path') {
        const value = argv[index + 1]
        if (!value) {
            console.error(`${NAME}: --path needs a file or a directory`)
            process.exit(2)
        }
        paths.push(path.resolve(value))
        index += 1
    } else if (arg !== '--enforce' && !arg.startsWith('--')) {
        console.error(`${NAME}: unknown argument '${arg}'`)
        process.exit(2)
    }
}

// The home is always derived from the whole tree, never from the sample: a sample is a place to
// read findings from, and moving the boundary with it would make `--path` measure another rule.
const TREE = SCANNED.map((name) => path.join(ROOT, name)).flatMap((target) => sourceFilesUnder(target))
const declaring = declaringFiles(TREE)
const homeFiles = new Set([...declaring.values()].flat())
// Only a header contributes its directory tree: the rule is "the tree of the header that declares
// it", and letting an implementation file pull in its own directory would make all of `src/runtime`
// the semantic layer.
const homeDirs = [
    ...new Set(
        [...homeFiles]
            .filter((file) => /(?:\.h|\.hpp|\.hh|\.hxx)$/.test(file))
            .map((file) => path.dirname(file)),
    ),
]
// The `.cpp` implementation of a declaring header is home too: both symbols here are defined
// inline, so the file that implements the class is `graph.cpp` beside a `graph.h` declaration. A
// header's own name is what pairs the two, so a renamed or a moved implementation is followed with
// no edit here.
const homeStems = new Set(
    [...homeFiles].map((file) => path.basename(file).replace(SOURCE, '')),
)
const homeImpls = new Set(
    TREE.filter(
        (file) =>
            !/(?:\.h|\.hpp|\.hh|\.hxx)$/.test(file) &&
            homeStems.has(path.basename(file).replace(SOURCE, '')),
    ),
)
const isHome = (file) =>
    homeFiles.has(file) ||
    homeImpls.has(file) ||
    homeDirs.some((dir) => file.startsWith(dir + path.sep))

const targets = paths.length > 0 ? paths : SCANNED.map((name) => path.join(ROOT, name))
const scanned = targets.flatMap((target) => sourceFilesUnder(target))
if (scanned.length === 0) {
    console.error(
        `${NAME}: ${targets.join(', ')} holds no C++ source, so this check matched nothing and ` +
            'proves nothing',
    )
    process.exit(2)
}

const findings = []
for (const file of scanned) {
    if (isHome(file)) continue
    const text = stripCommentsAndStrings(fs.readFileSync(file, 'utf8'))
    const lines = text.split('\n')
    for (let line = 0; line < lines.length; line += 1) {
        for (const symbol of SYMBOLS) {
            const uses = lines[line].match(new RegExp(`\\b${symbol}\\b`, 'g'))
            if (uses === null) continue
            for (let count = 0; count < uses.length; count += 1) {
                findings.push({
                    file,
                    line: line + 1,
                    symbol,
                    detail: 'read outside the semantic layer',
                })
            }
        }
    }
}
findings.sort((left, right) => left.file.localeCompare(right.file) || left.line - right.line)

for (const finding of findings) {
    console.log(`${shown(finding.file)}:${finding.line}: ${finding.symbol} -- ${finding.detail}`)
}

const homeTrees = [...new Set(homeDirs.map((dir) => `${shown(dir)}/`))].sort()
const home = [...new Set([...homeFiles].map(shown))].sort()
const impls = [...homeImpls].map(shown).sort()
const files = new Set(findings.map((finding) => shown(finding.file)))
console.log(
    `${NAME}: ${findings.length} edge-kind call(s) outside the semantic layer, in ${files.size} ` +
        `file(s) of ${scanned.length} source file(s) scanned`,
)
console.log(
    `${NAME}: home is ${homeTrees.concat(home, impls).join(', ')} ` +
        '(derived from the declaration; see the file comment)',
)
if (findings.length === 0) {
    console.log(`${NAME}: every edge kind is enumerated inside the semantic layer`)
}
if (enforce && findings.length > 0) {
    console.error(
        `${NAME}: a file outside the semantic layer enumerates a kind of edge: the vocabulary of ` +
            'the graph belongs to the layer that declares it (the target architecture, section 7)',
    )
    process.exit(1)
}
process.exit(0)