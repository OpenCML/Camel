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
 * A holder that must not or cannot pin -- a Program the object itself traces through its own
 * `updateRefs`, a Program another root already keeps, a Program a cache must not keep alive, a
 * borrowed view -- says so where it is declared, in the comment directly above the member:
 *
 *   // program-holder-exempt: kept-by-another-root -- rootForRun pins it for the whole run
 *   Program *program_;
 *
 * The label is one of four, and it also says why the holder does not pin:
 *
 *   heap-self-tracking      the holding object lives in the Camel heap and relocates the slot in
 *                           its own updateRefs; the collector already sees it
 *   kept-by-another-root    another root owns the program for the holder's whole life
 *   long-lived-cache        pinning would keep every program the cache ever saw alive; the cache
 *                           names programs that a root (or the heap) already keeps
 *   borrow-or-foreign-type  a view or a reference used only while the caller's program is
 *                           alive, or a `Program` of `camel::tiles` and not the runtime's
 *
 * `docs/technical/36_gc_handle_rules.md` states the four as the exceptions of rule 7. A holder
 * with no such comment, a label outside the four, or a comment with no reason after `--` is
 * *unexplained*, and that is the only thing the hard gate reports.
 *
 *   node scripts/check-program-holders.mjs              holders=<N> exempt=<M> unexplained=<K>
 *   node scripts/check-program-holders.mjs --enforce     exit 1 only when unexplained > 0
 *   node scripts/check-program-holders.mjs --path <p>    scan another file or directory (a sample)
 *
 * What is a holder here: a *member* declaration at class scope whose type is a pointer or reference
 * to the runtime `Program`, or a `shared_ptr`/`unique_ptr`/`weak_ptr` of one. A local variable, a
 * parameter and a return type are not holders -- they do not outlive the call -- so the scan works
 * on class bodies only, and skips any declaration with a parameter list. A smart pointer to a
 * program the object *owns* is reported too: owning one is not rooting it, and what the collector
 * traces is the slot, not the owner.
 *
 * The type is decided by identity, not by the name: the runtime's Program is `camel::Program` /
 * `camel::runtime::Program` (and the file-local `rt` alias), while `camel::tiles` has a Program of
 * its own (modules/tiles/include/tiles/program.h) that is not the runtime's and that this check
 * does not report. An unqualified `Program` takes the identity of the namespace it is declared in,
 * so a bare `Program *` inside `camel::tiles` is the tiles one. A qualifier this file does not know
 * is reported, not skipped: a holder the check cannot place is a holder to look at.
 */

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

/** The directories the scan covers by default. */
const SCANNED = ['include', 'src', 'modules', 'tools']

const SOURCE = /\.(?:h|hpp|hh|hxx|cpp|cc|cxx)$/

/** The two names the ruling exempts outright: the handle, and the owner that pins. */
const EXEMPT_CLASSES = new Set(['ProgramHold', 'GCGraphManager'])

/** The four reasons a holder may give for not pinning. See docs/technical/36_gc_handle_rules.md. */
const EXEMPT_LABELS = new Set([
    'heap-self-tracking',
    'kept-by-another-root',
    'long-lived-cache',
    'borrow-or-foreign-type',
])

/** `program-holder-exempt: <label> -- <reason>`; the reason is what makes the label a decision. */
const EXEMPTION = /program-holder-exempt:\s*([A-Za-z][A-Za-z-]*)\s*(?:--\s*(.*))?/

/** A pointer or reference to a `Program`, with the qualifier it was written with. */
const PROGRAM_POINTER =
    /((?:[A-Za-z_]\w*\s*::\s*)*)Program\s*(?:\*+|&)/

/** A smart pointer to a `Program`, with the qualifier it was written with. */
const PROGRAM_SMART_POINTER =
    /\b(?:shared|unique|weak)_ptr\s*<\s*(?:const\s+)?((?:[A-Za-z_]\w*\s*::\s*)*)Program\b/

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
    if (match) return { kind: 'class', name: match[1], path: null }
    // `namespace a`, `namespace a::b`, `namespace a::b::c`: every segment is a scope, and the
    // last one is not enough -- `namespace camel::tiles` inside `camel` is `camel::tiles`, and a
    // bare `Program` there is the tiles one. An anonymous `namespace {` has no name, so it keeps
    // the enclosing path.
    match = /^(?:inline\s+)?namespace\s+([A-Za-z_]\w*(?:\s*::\s*[A-Za-z_]\w*)*)\s*$/.exec(text)
    if (match) {
        const parts = match[1].split('::').map((part) => part.trim()).filter(Boolean)
        return { kind: 'namespace', name: parts[parts.length - 1] ?? '', path: parts }
    }
    if (/^enum\s/.test(text)) return { kind: 'other', name: '', path: null }
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

/**
 * Which Program a mention names: `runtime` (the one ProgramHold pins), or `foreign` (a Program of
 * another namespace). An unqualified mention takes the identity of the namespace it sits in; a
 * qualifier this file does not know is `runtime`, because a holder it cannot place is one to look
 * at rather than one to skip.
 */
function programIdentity(qualifier, namespacePath) {
    const parts = qualifier
        .split('::')
        .map((part) => part.trim())
        .filter(Boolean)
    if (parts.length > 0) {
        const head = parts[0]
        if (head === 'rt' || head === 'runtime') return 'runtime'
        // `camel::tiles::...` and a `tiles::...` written from inside `camel`.
        if (head === 'tiles') return 'foreign'
        if (head === 'camel' && parts[1] === 'tiles') return 'foreign'
        return 'runtime'
    }
    return namespacePath.includes('tiles') ? 'foreign' : 'runtime'
}

/** Whether the declaration holds the runtime Program, by the identity of every Program it names. */
function holdsRuntimeProgram(statement, namespacePath) {
    const identities = []
    for (const pattern of [PROGRAM_POINTER, PROGRAM_SMART_POINTER]) {
        for (const match of statement.matchAll(new RegExp(pattern.source, 'g'))) {
            identities.push(programIdentity(match[1] ?? '', namespacePath))
        }
    }
    return identities.includes('runtime')
}

/** The comment text on `line` (1-based) that trails the code, or null. */
function trailingComment(line) {
    const at = line.indexOf('//')
    if (at < 0) return null
    return line.slice(at)
}

/** Whether a raw line is comment text alone, for the block above a declaration. */
function isCommentOnly(line) {
    const text = line.trim()
    if (text === '') return false
    return (
        text.startsWith('//') ||
        text.startsWith('/*') ||
        text.startsWith('*') ||
        text === '*/' ||
        text.endsWith('*/')
    )
}

/**
 * The exemption a holder declares, read from the comment on its own line or the comment block
 * directly above it. The block is joined before it is read, so a reason the tree wrapped at the
 * column limit still counts. A label outside the four, or no reason after `--`, is not an
 * exemption.
 */
function exemptionOf(rawLines, line) {
    const above = []
    let index = line - 2
    while (index >= 0 && isCommentOnly(rawLines[index])) {
        above.unshift(rawLines[index])
        index -= 1
    }
    const own = trailingComment(rawLines[line - 1] ?? '')
    if (own !== null) above.push(own)
    const text = above.join(' ').replace(/\s+/g, ' ')
    const at = text.indexOf('program-holder-exempt:')
    if (at < 0) return { stated: false, exempt: false, why: 'no program-holder-exempt comment' }
    const match = EXEMPTION.exec(text.slice(at))
    const label = match[1]
    const reason = (match[2] ?? '').replace(/^\/\/\s*/, '').trim()
    if (!EXEMPT_LABELS.has(label)) {
        return {
            stated: true,
            exempt: false,
            why: `'${label}' is not one of ${[...EXEMPT_LABELS].join(', ')}`,
        }
    }
    if (reason === '') {
        return { stated: true, exempt: false, why: `'${label}' gives no reason after '--'` }
    }
    return { stated: true, exempt: true, label, reason }
}

/**
 * The line a member declaration starts on. A statement may open with an access specifier
 * (`private:`) that shares no line with the member; the comment above a member belongs above the
 * member, so the label is skipped before the line is taken.
 */
function declarationLine(statement, frameStart, lineAt) {
    let offset = 0
    let rest = statement
    for (;;) {
        const access = /^\s*(?:private|public|protected)\s*:\s*/.exec(rest)
        if (!access) break
        offset += access[0].length
        rest = rest.slice(access[0].length)
    }
    return lineAt(frameStart + offset + (rest.length - rest.trimStart().length))
}

/** Every class member of `file` whose type holds a Program, with the class that declares it. */
function holdersIn(file) {
    const raw = fs.readFileSync(file, 'utf8')
    const text = stripCommentsAndStrings(raw)
    const rawLines = raw.split('\n')
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
                    ? [...frame.namespacePath, ...header.path]
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
                const holdsProgram =
                    PROGRAM_POINTER.test(statement) || PROGRAM_SMART_POINTER.test(statement)
                const declaresFunction =
                    /\b(?:operator|function)\b\s*\(/.test(statement) || statement.includes('(')
                if (
                    holdsProgram &&
                    !declaresFunction &&
                    holdsRuntimeProgram(statement, frame.namespacePath)
                ) {
                    const line = declarationLine(statement, frame.start, lineAt)
                    found.push({
                        file,
                        line,
                        className: frame.name,
                        member: declaredName(statement),
                        type: statement.replace(/\s+/g, ' ').trim(),
                        exemption: exemptionOf(rawLines, line),
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
const exempt = holders.filter((holder) => holder.exemption.exempt)
const unexplained = holders.filter((holder) => !holder.exemption.exempt)

for (const holder of unexplained) {
    console.log(
        `${shown(holder.file)}:${holder.line}: ${holder.className}::${holder.member} -- ` +
            `${holder.exemption.why} (${holder.type})`,
    )
}
console.log(
    `holders=${holders.length} exempt=${exempt.length} unexplained=${unexplained.length}`,
)

// The report is the default: the numbers are a work item, not a wall. The gate is the unexplained
// count alone -- a holder that stated a reason is a decision, and a decision is not a failure.
if (enforce && unexplained.length > 0) {
    console.error(
        'check-program-holders: a class member names a Program it does not root and does not ' +
            'explain: hold it through ProgramHold, or say at the declaration which of the four ' +
            'reasons lets it stay (docs/technical/36_gc_handle_rules.md, rule 7)',
    )
    process.exit(1)
}
process.exit(0)