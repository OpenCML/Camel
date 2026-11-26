#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * An effect is decided in the fact layer, and nowhere else.
 *
 * The target architecture (section 7, "守护") writes the invariant as "an effect is decided only
 * through the fact layer". The criterion the author set for it
 * (`internal/reviews/2026-11-23-who-owns-freely-arrangeable.cn.md`) is narrower than "every use
 * of the two atoms moves": **outside the fact layer nobody assembles these atoms into "freely
 * arrangeable" or "has an effect"**. So the finding this check reports is the **statement that
 * names both atoms** -- the one that builds the conjunction itself -- and a statement that reads
 * one of them is reported as a reading, not as a violation: asking "is it pure" of a thing that is
 * about to be put on a device is a different question from deciding whether a pass may move it.
 *
 * Two kinds of use are not counted at all, and both are exemptions written down here rather than
 * judged case by case:
 *
 *   - a **placement** judgement -- "can the device run this operator" (`tiles`). It reads the same
 *     atoms and answers a different question; folding it into the scheduling fact would merge "can
 *     the device run it" with "may a pass move it", which design principles rule out.
 *   - a **user query** -- `graph.is_pure`, and a listing. It asks the fact layer for the answer it
 *     already computes, so it is one of the places the fact is read, not one that assembles it.
 *
 * That page is not in this tree: it is the design branch's (`origin/fsh-dev`), so it is quoted
 * here rather than linked -- a link to a page this tree does not hold is a broken link, and
 * `scripts/check-doc-links.mjs --citations` says so.
 *
 * --- what "the fact layer" is here ------------------------------------------
 * It is derived from the source, not from a list of files a person keeps in step by hand, and it is
 * the **home** of the named symbols plus the home of the answer they are supposed to come from:
 *
 *   1. the directory tree of the header that declares a named symbol, recursively, the files that
 *      declare or define it, and the implementation files named after that header --
 *      `isPure`/`hasExternalEffect` are declared in `src/kernel/include/camel/core/operator_table.h` and
 *      defined in `src/kernel/core/operator_table.cpp`, so `src/kernel/include/camel/core/` and that `.cpp` are home;
 *   2. the same three things for `effectSummaryOf`, the function that hands out the fact layer's
 *      answer: it is declared in `src/kernel/include/camel/runtime/effects.h` and defined in
 *      `src/kernel/runtime/effects.cpp`, so `src/kernel/include/camel/runtime/` and that `.cpp` are home too.
 *   3. the same for `arrangeableKind`, the kind that carries "freely arrangeable" as one fact
 *      (`src/kernel/facts/`, `camel/facts/arrangeable.h`): the four-term conjunction is stated
 *      there or nowhere.
 *
 * **This rule is mechanical, and it is the author's to change.** If a file outside these trees is
 * where the answer belongs, move the answer there or add the file to `EXEMPT` with the reason
 * written beside it. If the atoms should be removed from `OperatorTable` altogether, this check
 * fails to find a declaring file and refuses (exit 2) instead of passing over nothing.
 *
 * --- usage -----------------------------------------------------------------
 *   node scripts/check-effect-facts.mjs                the list, in report-only mode (exit 0)
 *   node scripts/check-effect-facts.mjs --enforce       exit 1 when a statement assembles the answer
 *   node scripts/check-effect-facts.mjs --path <p>      scan another file or directory (a sample)
 *
 * The gate runs the bare form: this file prints the violations, the readings and the counts. The
 * violation count is the baseline that has to be 0; `--enforce` is the mode the gate entry uses
 * once it is.
 */

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const NAME = 'check-effect-facts'
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

/** The directories the scan covers by default, the same set the other source checks read. */
// `src` holds the five blocks and the standard library since the move, and the seven
// tools are `src/tools`; a directory a checkout does not have is skipped rather than stat'ed, so a
// tree of either layout is scanned instead of crashing on the way in.
const SCANNED = ['src', 'src/tools', 'include', 'modules', 'tools']

const SOURCE = /\.(?:h|hpp|hh|hxx|cpp|cc|cxx)$/
const HEADER = /(?:\.h|\.hpp|\.hh|\.hxx)$/

/** The two atoms an effect is assembled from. */
const SYMBOLS = ['isPure', 'hasExternalEffect']

/** The functions that hand out the fact layer's answer: their own home is the fact layer. */
const ANSWERS = ['effectSummaryOf', 'arrangeableKind']

/**
 * The places that read the atoms and answer another question, with the reason. A file listed here
 * is not reported: the rule exempts the shape of use it makes, and the reason says which one.
 */
const EXEMPT = new Map([
    [
        'src/stdlib/tiles/src/graph_regions.cpp',
        'placement: `touchesNothing` asks whether the device can run the operator, not whether a ' +
            'pass may move it (design principles 3.4 is the second question)',
    ],
])

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
 * Whether `line` declares or defines `symbol` rather than using it. See
 * `check-edge-kind-layer.mjs` for the shapes; the same rule, and the same two exclusions for a
 * qualified use (`->name(`, `.name(`).
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

/** The home of `symbols`: their declaring headers' trees, the declaring files, and their impls. */
function homeOf(files, symbols) {
    const declaring = new Set()
    const declared = new Map(symbols.map((symbol) => [symbol, 0]))
    for (const file of files) {
        const text = stripCommentsAndStrings(fs.readFileSync(file, 'utf8'))
        for (const line of text.split('\n')) {
            for (const symbol of symbols) {
                if (!declaresSymbol(line, symbol)) continue
                declared.set(symbol, declared.get(symbol) + 1)
                declaring.add(file)
            }
        }
    }
    for (const [symbol, count] of declared) {
        if (count === 0) {
            console.error(
                `${NAME}: no file declares or defines '${symbol}', so this check would report a home ` +
                    'it never found and prove nothing',
            )
            process.exit(2)
        }
    }
    // Only a header contributes its directory tree: the rule is "the tree of the header that
    // declares it", and letting an implementation file pull in its own directory would make all of
    // `src/kernel/runtime/` the fact layer.
    const dirs = [...new Set([...declaring].filter((file) => HEADER.test(file)).map((file) => path.dirname(file)))]
    const stems = new Set([...declaring].map((file) => path.basename(file).replace(SOURCE, '')))
    const impls = new Set(
        files.filter(
            (file) => !HEADER.test(file) && stems.has(path.basename(file).replace(SOURCE, '')),
        ),
    )
    return { files: declaring, dirs, impls }
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
const TREE = SCANNED.map((name) => path.join(ROOT, name))
    .filter((target) => fs.existsSync(target))
    .flatMap((target) => sourceFilesUnder(target))
const atoms = homeOf(TREE, SYMBOLS)
const fact = homeOf(TREE, ANSWERS)
const homeFiles = new Set([...atoms.files, ...fact.files])
const homeImpls = new Set([...atoms.impls, ...fact.impls])
const homeDirs = [...new Set([...atoms.dirs, ...fact.dirs])]
const isHome = (file) =>
    homeFiles.has(file) ||
    homeImpls.has(file) ||
    homeDirs.some((dir) => file.startsWith(dir + path.sep))

const targets = paths.length > 0 ? paths : SCANNED.map((name) => path.join(ROOT, name))
const scanned = targets.filter((target) => fs.existsSync(target)).flatMap((target) => sourceFilesUnder(target))
if (scanned.length === 0) {
    console.error(
        `${NAME}: ${targets.join(', ')} holds no C++ source, so this check matched nothing and ` +
            'proves nothing',
    )
    process.exit(2)
}

/** The statement an offset sits in: from the last `;`/`{`/`}` to the next one. */
function statementAt(text, offset) {
    const boundary = (index) => ';{}'.includes(text[index])
    let start = offset
    while (start > 0 && !boundary(start - 1)) start -= 1
    let end = offset
    while (end < text.length && !boundary(end)) end += 1
    return text.slice(start, end)
}

const findings = []
const readings = []
for (const file of scanned) {
    if (isHome(file)) continue
    if (EXEMPT.has(shown(file))) continue
    const text = stripCommentsAndStrings(fs.readFileSync(file, 'utf8'))
    for (const symbol of SYMBOLS) {
        const other = SYMBOLS.find((candidate) => candidate !== symbol)
        for (const match of text.matchAll(new RegExp(`\\b${symbol}\\b`, 'g'))) {
            const statement = statementAt(text, match.index)
            const line = text.slice(0, match.index).split('\n').length
            if (new RegExp(`\\b${other}\\b`).test(statement)) {
                findings.push({ file, line, symbol })
            } else {
                readings.push({ file, line, symbol })
            }
        }
    }
}
const byPlace = (left, right) =>
    left.file.localeCompare(right.file) || left.line - right.line || left.symbol.localeCompare(right.symbol)
findings.sort(byPlace)
readings.sort(byPlace)

for (const finding of findings) {
    console.log(
        `${shown(finding.file)}:${finding.line}: ${finding.symbol} -- assembles the answer ` +
            '("is it freely arrangeable / does it have an effect") from both atoms',
    )
}
for (const reading of readings) {
    console.log(`${shown(reading.file)}:${reading.line}: ${reading.symbol} -- one atom, read as itself`)
}

const files = new Set(findings.map((finding) => shown(finding.file)))
const places = new Set(readings.map((reading) => shown(reading.file)))
console.log(
    `${NAME}: ${findings.length} statement(s) assembling the answer outside the fact layer, in ` +
        `${files.size} file(s); ${readings.length} statement(s) reading one atom as itself, in ` +
        `${places.size} file(s); ${scanned.length} source file(s) scanned`,
)
if (findings.length === 0) {
    console.log(
        `${NAME}: the answer is assembled in the fact layer and nowhere else (${
            EXEMPT.size
        } exempt file(s): placement and user queries, see the file comment)`,
    )
}
if (enforce && findings.length > 0) {
    console.error(
        `${NAME}: a file outside the fact layer assembles an operator's effect from its atoms: the ` +
            'answer is one fact -- `EffectSummary` for "does it have an effect" and ' +
            '`core.arrangeable` for "is it freely arrangeable" (design principles 3.4)',
    )
    process.exit(1)
}
process.exit(0)