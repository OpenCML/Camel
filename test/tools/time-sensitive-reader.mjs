#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The second reader of a plan's `time_sensitive` declaration -- written from the file format, not
 * from the gate's regular expressions.
 *
 * ## Why a second one
 *
 * `time_sensitive = true` at the top level of a *.plan.toml means "this plan's readings are
 * durations, so only a host it has to itself can produce a comparable one". Exactly one program acts
 * on it: the gate (`internal/tools/camel-remote-timesensitive.ps1`, in turn `camel-remote.ps1`),
 * which reads the file with two line regular expressions. A reader with one implementation cannot be
 * shown wrong -- it can only be "re-read", by itself, and agree. That is the shape this project keeps
 * paying for, so the declaration gets a second reader that shares nothing with the first but the
 * input: the plan files themselves.
 *
 * This file is that reader. It parses a document's **top level** -- strings, comments, arrays,
 * inline tables and table headers at the level of the TOML specification -- and reports the value of
 * the top-level key `time_sensitive`. It does not import the gate's parser, the runner's parser
 * (`test/tools/toml-lite.mjs`) or the runner itself. Its independence is the point; where it
 * disagrees with the gate, the disagreement is the reading, and which side is right is decided by
 * the file format, not by majority.
 *
 * ## What it answers, and the one place it knows more than the gate
 *
 * Two answers, deliberately kept apart:
 *
 *   `gate`    is this plan's declaration about the host, i.e. is the top-level `time_sensitive` the
 *             boolean `true`? This is the only question the gate asks, so this is what a
 *             cross-check compares.
 *   `problem` what would make the runner refuse this file before it ran anything: the key written
 *             inside a table (its value belongs to that table, and in `[defaults]` the schema has no
 *             such field), the key twice at the top level (not TOML), or a value that is not a
 *             boolean. The gate does not ask this question -- it takes the host or does not -- so a
 *             disagreement here is not a disagreement about the declaration, and the cross-check
 *             reports it as information rather than as a mismatch.
 *
 * `state` is `absent`, `true`, `false` or `invalid`: what the declaration itself says. `gate` is
 * `state === 'true'`, so a value TOML reads as a string (`time_sensitive = "true"`) is *not* a
 * declaration about the host -- it is a file the runner refuses, which is louder than a plan that
 * quietly measured beside somebody else's build.
 *
 * ## Command line
 *
 *   node test/tools/time-sensitive-reader.mjs <plan.toml>...        one line per file
 *   node test/tools/time-sensitive-reader.mjs --json <plan.toml>... one JSON document
 *   node test/tools/time-sensitive-reader.mjs --root <dir> --files-from <list>
 *                                                                   the paths a caller enumerated,
 *                                                                   read from a file, one per line
 *
 * `--files-from` exists so the cross-check and this reader can be held to **the same set of files**:
 * the harness enumerates the tree once, writes the list, and both sides read that list. A comparison
 * of two different populations is not a comparison.
 */
import fs from 'node:fs'
import path from 'node:path'
import url from 'node:url'

const BOOLEAN_TRUE = 'true'
const BOOLEAN_FALSE = 'false'

/**
 * Skip a comment that starts at `from` and runs to the end of the line it is on.
 * Returns the index of the newline (or the end of the document).
 */
function endOfComment(text, from) {
    const stop = text.indexOf('\n', from)
    return stop < 0 ? text.length : stop
}

/**
 * Read one string that starts at `at` (the text at `at` is the opening delimiter) and return the
 * index just past its closing delimiter, or `null` when the document ends first. Both quote kinds
 * are read: `"` with the TOML escapes, `'` literally, and the triple-quoted forms of each.
 */
function endOfString(text, at) {
    const quote = text[at]
    const triple = text.startsWith(quote.repeat(3), at)
    if (triple) {
        const close = text.indexOf(quote.repeat(3), at + 3)
        return close < 0 ? null : close + 3
    }
    let index = at + 1
    while (index < text.length) {
        const ch = text[index]
        if (quote === '"' && ch === '\\') {
            index += 2
            continue
        }
        // A single-line string ends at its own quote and cannot contain a newline; a document that
        // does not close it is not TOML, so the scan stops rather than guessing where it ended.
        if (ch === quote) return index + 1
        if (ch === '\n') return null
        index++
    }
    return null
}

/**
 * The document's top-level key/value pairs, in the order they are written, plus the keys the
 * document writes inside a table.
 *
 * The scan is over characters, not lines: a `[` on a line of its own is a table header only when no
 * value is open (a multi-line array's nested element and a line of a triple-quoted string both look
 * exactly like one), and a key is read only at the top level. That is where the gate's two
 * expressions and this scanner can differ, and the difference is the reading.
 */
export function scanDocument(text) {
    const top = []
    const inTable = []
    const tables = []
    const problems = []
    // `table` is null at the top level and the header's name inside one. `depth` counts the `[`/`{`
    // of a value still being written; a newline means nothing while it is above zero.
    let table = null
    let depth = 0
    let index = 0
    let lineStart = 0
    let line = 1
    const advanceTo = (to) => {
        for (let at = index; at < to && at < text.length; at++) {
            if (text[at] === '\n') {
                line++
                lineStart = at + 1
            }
        }
        index = Math.min(to, text.length)
    }
    while (index < text.length) {
        const ch = text[index]
        if (ch === '\n') {
            line++
            index++
            lineStart = index
            continue
        }
        // A comment runs to the end of its line, at any depth and in either position.
        if (ch === '#') {
            advanceTo(endOfComment(text, index))
            continue
        }
        // At a line's first non-space character, with no value open, the text is either a key/value
        // pair or a table header. The key is read first, and it is read *here* rather than in the
        // value branch below: a quoted key (`"time_sensitive" = true`, which TOML allows) opens with
        // the same character a string value does, and a scanner that skipped strings before looking
        // for a key silently dropped that spelling.
        const firstOnLine = text.slice(lineStart, index).trim() === ''
        if (depth === 0 && firstOnLine) {
            if (ch === ' ' || ch === '\t') {
                index++
                continue
            }
            const parsed = readKeyValue(text, index, line)
            if (parsed) {
                advanceTo(parsed.end)
                const entry = { key: parsed.key, value: parsed.value, line: parsed.line }
                if (table === null) {
                    top.push(entry)
                } else {
                    inTable.push({ ...entry, table })
                }
                continue
            }
            if (ch === '[') {
                // `[defaults]` and `[[tests]]` both end the document's top level; whatever follows
                // belongs to the table.
                const end = text.indexOf('\n', index)
                const stop = end < 0 ? text.length : end
                const header = text.slice(index, stop).trim()
                const name = header.replace(/^\[+/, '').replace(/\]+$/, '').trim()
                if (!/^\[\[?[^\]]+\]\]?$/.test(header.split('#')[0].trim())) {
                    problems.push(`line ${line}: a line starting with \`[\` that is not a table header: ${header}`)
                }
                table = name
                tables.push({ name, line })
                advanceTo(stop)
                continue
            }
            // Neither: this line is not TOML's top level at all. It is reported rather than
            // skipped, because a shape this reader cannot read is a shape it must not answer about.
            problems.push(`line ${line}: neither a key/value pair nor a table header: ${text.slice(index, text.indexOf('\n', index) < 0 ? text.length : text.indexOf('\n', index)).trim()}`)
            advanceTo(endOfComment(text, index))
            continue
        }
        // Inside a value, or partway along a line: strings and brackets only.
        if (ch === '"' || ch === "'") {
            const after = endOfString(text, index)
            if (after === null) {
                problems.push(`line ${line}: a string that never closes`)
                break
            }
            advanceTo(after)
            continue
        }
        if (ch === '[' || ch === '{') {
            depth++
            index++
            continue
        }
        if (ch === ']' || ch === '}') {
            if (depth > 0) depth--
            index++
            continue
        }
        index++
    }
    return { top, inTable, tables, problems }
}

/**
 * Read `key = value` at `at`, where `at` is the line's first non-space character. Returns `null`
 * when the text there is not a key/value pair, so the caller can skip it.
 *
 * The value is returned as the raw text TOML would read, minus a trailing comment: for a boolean
 * that is the word itself, and for anything else it is whatever the line holds, which is enough to
 * say "not a boolean" and to name it in the message.
 */
function readKeyValue(text, at, line) {
    let index = at
    const quoted = text[index] === '"' || text[index] === "'"
    let key
    if (quoted) {
        const after = endOfString(text, index)
        if (after === null) return null
        key = text.slice(index + 1, after - 1)
        index = after
    } else {
        const match = /^[A-Za-z0-9_-]+/.exec(text.slice(index))
        if (!match) return null
        key = match[0]
        index += match[0].length
    }
    while (text[index] === ' ' || text[index] === '\t') index++
    if (text[index] !== '=') return null
    index++
    while (text[index] === ' ' || text[index] === '\t') index++
    if (text[index] === '\n' || index >= text.length) {
        return { key, value: '', line, end: index }
    }
    // An array or an inline table: the value is everything up to its own close, comments and
    // strings inside it read the same way they are read anywhere else.
    if (text[index] === '[' || text[index] === '{') {
        const start = index
        let depth = 0
        while (index < text.length) {
            const ch = text[index]
            if (ch === '#') {
                index = endOfComment(text, index)
                continue
            }
            if (ch === '"' || ch === "'") {
                const after = endOfString(text, index)
                if (after === null) return { key, value: text.slice(start), line, end: text.length }
                index = after
                continue
            }
            if (ch === '[' || ch === '{') depth++
            if (ch === ']' || ch === '}') {
                depth--
                if (depth === 0) {
                    index++
                    break
                }
            }
            index++
        }
        return { key, value: text.slice(start, index).trim(), line, end: index }
    }
    const start = index
    while (index < text.length && text[index] !== '\n' && text[index] !== '#') {
        if (text[index] === '"' || text[index] === "'") {
            const after = endOfString(text, index)
            if (after === null) return { key, value: text.slice(start), line, end: text.length }
            index = after
            continue
        }
        index++
    }
    return { key, value: text.slice(start, index).trim(), line, end: index }
}

/**
 * The declaration of one document.
 *
 *   state     `absent` | `true` | `false` | `invalid`
 *   gate      whether the declaration is the boolean `true` -- the only thing the gate reads
 *   line      where the declaration is (1-based), or 0 when there is none
 *   occurrence  the top-level occurrences, in order, for a document that writes the key more than once
 *   problem   what the runner would refuse this file for, or null
 */
export function readTimeSensitiveDeclaration(text) {
    const scanned = scanDocument(text)
    const occurrences = scanned.top
        .filter((entry) => entry.key === 'time_sensitive')
        .map((entry) => ({
            line: entry.line,
            raw: entry.value,
            state: entry.value === BOOLEAN_TRUE ? 'true' : entry.value === BOOLEAN_FALSE ? 'false' : 'invalid',
        }))
    const nested = scanned.inTable.filter((entry) => entry.key === 'time_sensitive')
    const problems = []
    if (occurrences.length > 1) {
        problems.push(
            `the key is written ${occurrences.length} times at the top level ` +
                `(lines ${occurrences.map((one) => one.line).join(', ')}): a repeated key is not TOML`,
        )
    }
    for (const one of nested) {
        problems.push(
            `line ${one.line}: \`time_sensitive\` is written inside [${one.table}], so its value belongs to ` +
                'that table and is not a declaration about the host',
        )
    }
    for (const one of occurrences) {
        if (one.state === 'invalid') {
            problems.push(
                `line ${one.line}: \`time_sensitive\` is \`${one.raw}\`, which TOML does not read as a ` +
                    'boolean; the runner refuses the plan rather than reading it as false',
            )
        }
    }
    for (const problem of scanned.problems) problems.push(problem)
    const state =
        occurrences.length === 0
            ? 'absent'
            : occurrences.some((one) => one.state === 'true')
              ? 'true'
              : occurrences.some((one) => one.state === 'false')
                ? 'false'
                : 'invalid'
    return {
        state,
        gate: state === 'true',
        line: occurrences.length > 0 ? occurrences[0].line : 0,
        occurrence: occurrences,
        nested: nested.map((one) => ({ line: one.line, table: one.table })),
        problem: problems.length > 0 ? problems.join('; ') : null,
    }
}

export function readTimeSensitiveFile(filePath) {
    return readTimeSensitiveDeclaration(fs.readFileSync(filePath, 'utf8'))
}

/** `--files-from` lines, blank lines dropped, paths left exactly as written. */
function readFileList(listPath) {
    return fs
        .readFileSync(listPath, 'utf8')
        .split(/\r?\n/)
        .map((one) => one.trim())
        .filter((one) => one !== '')
}

function main(argv) {
    const json = argv.includes('--json')
    const rootAt = argv.indexOf('--root')
    const fromAt = argv.indexOf('--files-from')
    const root = rootAt >= 0 ? argv[rootAt + 1] : process.cwd()
    let files = []
    if (fromAt >= 0) {
        files = readFileList(argv[fromAt + 1])
    }
    const skip = new Set()
    for (const flag of ['--json', '--root', '--files-from']) {
        const at = argv.indexOf(flag)
        if (at >= 0) {
            skip.add(at)
            if (flag !== '--json') skip.add(at + 1)
        }
    }
    argv.forEach((value, at) => {
        if (!skip.has(at) && !value.startsWith('--')) files.push(value)
    })
    const rows = files.map((file) => {
        const full = path.isAbsolute(file) ? file : path.join(root, file)
        let reading
        try {
            reading = readTimeSensitiveFile(full)
        } catch (error) {
            reading = {
                state: 'absent',
                gate: false,
                line: 0,
                occurrence: [],
                nested: [],
                problem: `cannot be read (${error.code || error.message})`,
            }
        }
        return { path: file, ...reading }
    })
    if (json) {
        console.log(JSON.stringify({ files: rows }, null, 2))
        return 0
    }
    for (const row of rows) {
        const where = row.line > 0 ? `${row.path}:${row.line}` : row.path
        const problem = row.problem ? `  problem: ${row.problem}` : ''
        console.log(`time-sensitive gate=${row.gate ? 'true' : 'false'} state=${row.state} ${where}${problem}`)
    }
    return 0
}

if (process.argv[1] && path.resolve(process.argv[1]) === url.fileURLToPath(import.meta.url)) {
    process.exitCode = main(process.argv.slice(2))
}