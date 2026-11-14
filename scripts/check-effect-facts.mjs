#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * An effect is decided in the fact layer, and nowhere else.
 *
 * The target architecture (section 7, "守护") writes the invariant as "an effect is decided only
 * through the fact layer", with the mechanical check "the combination of
 * `isPure`/`hasExternalEffect` is allowed only in the fact layer". A caller that asks "is this pure"
 * or "does this have an external effect" is asking the fact layer for the answer it already
 * computes; a caller that takes the two atoms and assembles the answer itself is the second place
 * where the effect of an operator is decided, which is the defect the row names.
 *
 * That page is not in this tree as of 2026-11-14: it is the design branch's (`origin/fsh-dev`), so
 * section 7 is quoted here rather than linked -- a link to a page this tree does not hold is a
 * broken link, and `scripts/check-doc-links.mjs --citations` says so.
 *
 * --- what "the fact layer" is here ------------------------------------------
 * It is derived from the source, not from a list of files a person keeps in step by hand, and it is
 * the **home** of the named symbols plus the home of the answer they are supposed to come from:
 *
 *   1. the directory tree of the header that declares a named symbol, recursively, the files that
 *      declare or define it, and the implementation files named after that header --
 *      `isPure`/`hasExternalEffect` are declared in `include/camel/core/operator_table.h` and
 *      defined in `src/core/operator_table.cpp`, so `include/camel/core/` and that `.cpp` are home;
 *   2. the same three things for `effectSummaryOf`, the function that hands out the fact layer's
 *      answer: it is declared in `include/camel/runtime/effects.h` and defined in
 *      `src/runtime/effects.cpp`, so `include/camel/runtime/` and that `.cpp` are home too.
 *
 * A named symbol used outside that home is a crossing, reported once per use (`path:line: symbol`).
 * A use whose **statement** also names the other atom is tagged as the combination the invariant
 * forbids, because that is the shape the row is about; a lone atom outside the home is reported
 * too, because it is the same question asked of the same two declarations in a second place.
 *
 * **This rule is mechanical, and it is the author's to change.** The judgement it encodes is that
 * the answer is one fact (`EffectSummary`, declared in `include/camel/runtime/effects.h`) and that
 * its two atoms (`OperatorTable::isPure`, `OperatorTable::hasExternalEffect`) are its inputs. If a
 * file outside these trees is where the answer belongs, move the answer there or add the file here,
 * deliberately, with the reason written beside it -- and if the second atom should be removed from
 * `OperatorTable` altogether, this check then fails to find a declaring file and refuses (exit 2)
 * instead of passing over nothing.
 *
 * --- usage -----------------------------------------------------------------
 *   node scripts/check-effect-facts.mjs                the list, in report-only mode (exit 0)
 *   node scripts/check-effect-facts.mjs --enforce       exit 1 when the list is not empty
 *   node scripts/check-effect-facts.mjs --path <p>      scan another file or directory (a sample)
 *
 * The gate runs the bare form: this file prints the list and the count, and the count is the
 * baseline the list has to come down from. `--enforce` is the mode the gate entry becomes once the
 * list is empty.
 */

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const NAME = 'check-effect-facts'
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

/** The directories the scan covers by default, the same set the other source checks read. */
const SCANNED = ['include', 'src', 'modules', 'tools']

const SOURCE = /\.(?:h|hpp|hh|hxx|cpp|cc|cxx)$/
const HEADER = /(?:\.h|\.hpp|\.hh|\.hxx)$/

/** The two atoms an effect is assembled from. */
const SYMBOLS = ['isPure', 'hasExternalEffect']

/** The function that hands out the fact layer's answer: its own home is the fact layer. */
const ANSWER = 'effectSummaryOf'

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
    // `src/runtime/` the fact layer.
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
const TREE = SCANNED.map((name) => path.join(ROOT, name)).flatMap((target) => sourceFilesUnder(target))
const atoms = homeOf(TREE, SYMBOLS)
const fact = homeOf(TREE, [ANSWER])
const homeFiles = new Set([...atoms.files, ...fact.files])
const homeImpls = new Set([...atoms.impls, ...fact.impls])
const homeDirs = [...new Set([...atoms.dirs, ...fact.dirs])]
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
for (const file of scanned) {
    if (isHome(file)) continue
    const text = stripCommentsAndStrings(fs.readFileSync(file, 'utf8'))
    for (const symbol of SYMBOLS) {
        const other = SYMBOLS.find((candidate) => candidate !== symbol)
        for (const match of text.matchAll(new RegExp(`\\b${symbol}\\b`, 'g'))) {
            const statement = statementAt(text, match.index)
            const combines = new RegExp(`\\b${other}\\b`).test(statement)
            findings.push({
                file,
                line: text.slice(0, match.index).split('\n').length,
                symbol,
                detail: combines
                    ? `combined with ${other} in one statement: the answer belongs to the fact layer`
                    : 'asked outside the fact layer',
            })
        }
    }
}
findings.sort(
    (left, right) =>
        left.file.localeCompare(right.file) || left.line - right.line || left.symbol.localeCompare(right.symbol),
)

for (const finding of findings) {
    console.log(`${shown(finding.file)}:${finding.line}: ${finding.symbol} -- ${finding.detail}`)
}

const homeTrees = [...new Set(homeDirs.map((dir) => `${shown(dir)}/`))].sort()
const home = [...new Set([...homeFiles, ...homeImpls].map(shown))].sort()
const files = new Set(findings.map((finding) => shown(finding.file)))
const combined = findings.filter((finding) => finding.detail.startsWith('combined')).length
console.log(
    `${NAME}: ${findings.length} effect-atom call(s) outside the fact layer (${combined} of them ` +
        `combining both atoms), in ${files.size} file(s) of ${scanned.length} source file(s) scanned`,
)
console.log(
    `${NAME}: home is ${homeTrees.concat(home).join(', ')} ` +
        '(derived from the declarations; see the file comment)',
)
if (findings.length === 0) {
    console.log(`${NAME}: every effect question is answered by the fact layer`)
}
if (enforce && findings.length > 0) {
    console.error(
        `${NAME}: a file outside the fact layer assembles an operator's effect from its atoms: the ` +
            'answer is one fact, declared with the other graph facts ' +
            '(the target architecture, section 7)',
    )
    process.exit(1)
}
process.exit(0)