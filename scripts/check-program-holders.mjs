#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A `Program` a class keeps in C++ is a `Program` the collector cannot see.
 *
 * The collector reaches a Program through the heap (a function value names its program, and the
 * trace of a graph's static area follows it) or through a root. A Program kept in a C++ member is
 * neither, so its graphs are not traced while a holder in C++ is the only thing keeping it: a
 * collection that falls in that window moves the values the graphs' static areas hold and leaves
 * the old addresses in the slots. Measured on the two `modules.nn` large-bytecode cases under
 * `--gc-check poison` (rc=134) and without poison under `--gc-stress safepoint=1,kind=minor`
 * (rc=139); the page is
 * `docs/handover/reviews/2026-11-06-who-roots-a-program-a-pass-holds-while-a-call-runs.cn.md`.
 *
 * The answer the author ruled (2026-11-13) is one handle: `camel::runtime::ProgramHold` pins the
 * program in its constructor and unpins it in its destructor (camel/runtime/graph.h). A holder
 * that lives across a safepoint therefore holds the program through that handle and nothing else --
 * no bare `Program *`, no `shared_ptr<Program>`.
 *
 *   node scripts/check-program-holders.mjs              the reform list, in report-only mode
 *   node scripts/check-program-holders.mjs --enforce     the hard gate: exit 1 when a holder exists
 *   node scripts/check-program-holders.mjs --path <p>    scan another file or directory (a sample)
 *
 * The ruling asked for the report first: this file lists the holders that still exist, so the list
 * is a work item rather than a wall, and `--enforce` is the mode the gate will run once the list is
 * empty. Only two names are exempt, both named by the ruling: `ProgramHold` (the handle) and
 * `GCGraphManager` (the owner of the Programs and the thing that pins them).
 *
 * What is a holder here: a *member* declaration at class scope whose type is a pointer or reference
 * to `Program`, or a `shared_ptr`/`unique_ptr`/`weak_ptr` of one. A local variable, a parameter
 * and a return type are not holders -- they do not outlive the call -- so the scan works on class
 * bodies only, and skips any declaration with a parameter list. A smart pointer to a program the
 * object *owns* is reported too: owning one is not rooting it, and what the collector traces is
 * the slot, not the owner.
 *
 * Two things the type alone cannot say, and what this file does about them:
 *
 *  - `camel::tiles` has a `Program` of its own (modules/tiles/include/tiles/internal/kernel_ir.h),
 *    and a bare `Program` inside `camel::tiles` is that one. It is skipped, and only there: a
 *    `rt::Program` or `camel::runtime::Program` in the same file is the runtime's and is reported.
 *  - a Program field of an object that lives *in the Camel heap* is a holder the collector does
 *    see (its `updateRefs` relocates the slot), which is why a function value may name its program.
 *    That is a property of the object's own trace, not of its type; the field is reported anyway,
 *    because the exemption is the object's trace and this check cannot read it. Whoever takes one
 *    of those off the list names the trace that covers it.
 */

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

/** The directories the scan covers by default. */
const SCANNED = ['include', 'src', 'modules', 'tools']

const SOURCE = /\.(?:h|hpp|hh|hxx|cpp|cc|cxx)$/

/** The two names the ruling exempts. */
const EXEMPT_CLASSES = new Set(['ProgramHold', 'GCGraphManager'])

/** A pointer or reference to the runtime Program, or a smart pointer to one. */
const PROGRAM_POINTER =
    /(?:\bProgram\s*(?:\*+|&)|\b(?:shared|unique|weak)_ptr\s*<\s*(?:const\s+)?(?:(?:camel::runtime|rt)::)?Program\b)/

/**
 * The type of the Program in a declaration, as written: qualified (`camel::runtime::Program`,
 * `rt::Program`) or bare. Only the tiles namespace has a Program of its own, so a bare one there
 * is not the runtime's.
 */
function programTypeIsRuntime(text, namespacePath) {
    if (/(?:camel::runtime|rt)::Program\b/.test(text)) return true
    return !namespacePath.includes('tiles')
}

const KEYWORDS = new Set([
    'alignas', 'auto', 'const', 'constexpr', 'explicit', 'extern', 'inline', 'mutable', 'static',
    'thread_local', 'volatile', 'class', 'struct', 'union', 'typename', 'using', 'public', 'private',
    'protected', 'friend', 'virtual', 'override', 'final', 'noexcept', 'nullptr', 'true', 'false',
    'Program', 'std', 'vector', 'shared_ptr', 'unique_ptr', 'weak_ptr', 'const_cast', 'optional',
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
 * The name a class/namespace header gives, when the text between the last `;`/`{`/`}` and the
 * `{` is a definition header. `struct X : Base`, `class X final`, `namespace a::b`,
 * `enum class X` are the shapes the tree writes.
 */
function headerOf(tail) {
    // A preprocessor line is not part of the declaration a `{` belongs to: `#include` between the
    // last `;` and a file's first `namespace` would otherwise be read as the header.
    const text = tail
        .split('\n')
        .filter((line) => !/^\s*#/.test(line))
        .join('\n')
        .replace(/\s+/g, ' ')
        .trim()
    let match = /^(?:template\s*<[^{]*>\s*)?(?:class|struct|union)\s+(?:alignas\s*\([^)]*\)\s*)?([A-Za-z_]\w*)/.exec(text)
    if (match) return { kind: 'class', name: match[1] }
    match = /^(?:inline\s+)?namespace\s+(?:[A-Za-z_]\w*\s*(?:::|$))+/.exec(text)
    if (match) {
        const name = /namespace\s+((?:[A-Za-z_]\w*\s*::?\s*)+)/.exec(text)
        const parts = name ? name[1].split('::').map((part) => part.trim()).filter(Boolean) : []
        return { kind: 'namespace', name: parts[parts.length - 1] ?? '' }
    }
    if (/^enum\s/.test(text)) return { kind: 'other', name: '' }
    return null
}

/** The declaring name of a member statement: the last identifier before any initializer. */
function declaredName(text) {
    const head = text.split('=')[0]
    const names = [...head.matchAll(/\b([A-Za-z_]\w*)\b/g)].map((match) => match[1])
    for (let index = names.length - 1; index >= 0; index -= 1) {
        if (!KEYWORDS.has(names[index])) return names[index]
    }
    return '<unnamed>'
}

/** Every class member of `file` whose type holds a Program, with the class that declares it. */
function holdersIn(file) {
    const text = stripCommentsAndStrings(fs.readFileSync(file, 'utf8'))
    const lineAt = (offset) => text.slice(0, offset).split('\n').length
    // Brace frames: a class body is the only scope a member lives in. Every other `{` (a function
    // body, a lambda, an initializer) is `other`, and a declaration inside one is not a member.
    const frames = [{ kind: 'other', name: '', start: 0, namespacePath: [] }]
    const found = []
    let index = 0
    while (index < text.length) {
        const char = text[index]
        if (char === '{') {
            const frame = frames[frames.length - 1]
            const header = headerOf(text.slice(frame.start, index))
            const namespacePath =
                header && header.kind === 'namespace'
                    ? [...frame.namespacePath, header.name]
                    : frame.namespacePath
            frames.push({
                kind: header ? header.kind : 'other',
                name: header ? header.name : '',
                start: index + 1,
                namespacePath,
            })
            index += 1
            continue
        }
        if (char === '}') {
            if (frames.length > 1) frames.pop()
            const frame = frames[frames.length - 1]
            frame.start = index + 1
            index += 1
            continue
        }
        if (char === ';') {
            const frame = frames[frames.length - 1]
            if (frame.kind === 'class') {
                const statement = text.slice(frame.start, index)
                const holdsProgram = PROGRAM_POINTER.test(statement) && !/\b(?:operator|function)\b\s*\(/.test(statement) && !statement.includes('(')
                if (holdsProgram && programTypeIsRuntime(statement, frame.namespacePath)) {
                    found.push({
                        file,
                        line: lineAt(frame.start + (statement.length - statement.trimStart().length)),
                        className: frame.name,
                        member: declaredName(statement),
                        type: statement.replace(/\s+/g, ' ').trim(),
                    })
                }
            }
            frame.start = index + 1
            index += 1
            continue
        }
        index += 1
    }
    return found
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

const argv = process.argv.slice(2)
const enforce = argv.includes('--enforce')
const paths = []
for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index]
    if (arg === '--path') {
        const value = argv[index + 1]
        if (!value) {
            console.error('check-program-holders: --path needs a file or a directory')
            process.exit(2)
        }
        paths.push(path.resolve(value))
        index += 1
    } else if (arg !== '--enforce' && !arg.startsWith('--')) {
        console.error(`check-program-holders: unknown argument '${arg}'`)
        process.exit(2)
    }
}

const targets = paths.length > 0 ? paths : SCANNED.map((name) => path.join(ROOT, name))
const scanned = targets.flatMap((target) => sourceFilesUnder(target))
if (scanned.length === 0) {
    console.error(
        `check-program-holders: ${targets.join(', ')} holds no C++ source, so this check matched ` +
            'nothing and proves nothing',
    )
    process.exit(2)
}

const holders = scanned
    .flatMap((file) => holdersIn(file))
    .filter((holder) => !EXEMPT_CLASSES.has(holder.className))
    .sort((left, right) => left.file.localeCompare(right.file) || left.line - right.line)

const shown = (file) => path.relative(ROOT, file).split(path.sep).join('/')
for (const holder of holders) {
    console.log(`${shown(holder.file)}:${holder.line}: ${holder.className}::${holder.member} -- ${holder.type}`)
}

const classes = new Set(holders.map((holder) => `${shown(holder.file)}#${holder.className}`))
const files = new Set(holders.map((holder) => shown(holder.file)))
console.log(
    `check-program-holders: ${holders.length} C++ Program holder(s) in ${classes.size} class(es) ` +
        `across ${files.size} file(s), of ${scanned.length} source file(s) scanned`,
)
if (holders.length === 0) {
    console.log('check-program-holders: every Program a C++ holder keeps is held through ProgramHold')
}
if (enforce && holders.length > 0) {
    console.error(
        'check-program-holders: a class member names a Program without ProgramHold: the program it ' +
            'keeps is one the collector does not trace, and a safepoint that falls while the holder ' +
            'keeps it leaves the old addresses of its static values in the slots ' +
            '(docs/handover/reviews/2026-11-06-who-roots-a-program-a-pass-holds-while-a-call-runs.cn.md)',
    )
    process.exit(1)
}
process.exit(0)