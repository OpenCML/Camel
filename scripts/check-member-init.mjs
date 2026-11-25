#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A class member that no constructor initializes and that carries no default member initializer is
// read as whatever the allocator left in it. That is not a style question: `Builder`'s `synced_` /
// `varied_` were read by `UnitContext`'s save before any statement had written them, and a garbage
// `true` there built a closure under the wrong graph (the S1-6 capture refusal).
//
//   node scripts/check-member-init.mjs                 the files changed against the base
//   node scripts/check-member-init.mjs --all           every .h / .cpp under the scanned roots
//   node scripts/check-member-init.mjs <path>...       only these files or directories
//   node scripts/check-member-init.mjs --jobs N        parallel clang-tidy processes (default 4)
//   node scripts/check-member-init.mjs --db DIR        the directory holding compile_commands.json
//   node scripts/check-member-init.mjs --static-only   read the text instead of asking the compiler
//   node scripts/check-member-init.mjs --tool-only     refuse the text read (exit 2 without the tool)
//
// The engine is `clang-tidy` with `cppcoreguidelines-pro-type-member-init` wherever it and a
// compilation database are both present, and the summary line names the engine it used. That is the
// census instrument: it reads every constructor's initializer list and body, one constructor at a
// time, so it sees a member one constructor leaves out and another names. It also reports a
// record-type *variable* with no initializer, which is not a member: those rows are named and left
// alone. Where the toolchain carries no clang-tidy -- this repository's build host does not, and a
// whole-tree run there costs eleven minutes -- the same rule is read off the text in 1.2 s, and
// that reading is what the fast layer runs. It asks whether any constructor names the member, so it
// answers a weaker question than the compiler's, and it reports the members whose value would be
// indeterminate -- scalars, pointers, enums, bit-fields; a class-type member is constructed by its
// own default constructor and is left to the compiler's reading. Measured 2026-11-20 on this tree:
// before the sweep the compiler named 56 sites and the text read 12, the text read being a subset
// of the compiler's rows apart from one member the compiler reaches through a constructor defined
// in a .cpp; after the sweep both name none.
//
// What neither engine covers: a class with no user-declared constructor (an aggregate -- that is
// `-Wuninitialized`'s business, and giving its members initializers would change what the
// aggregate is), a `static` member (static storage, zero initialized), a reference member (it has
// no default initializer to give), and a member a delegating constructor leaves to the one it
// delegates to.
//
// Exit status 1 is a finding, one line each: `file:line  type  member`. Exit status 2 when a
// target does not exist, the scan holds no source file, or the tool was asked for and is not
// there: a check of nothing is refused, never reported as a pass.

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(
    process.env.CAMEL_MEMBER_INIT_ROOT || path.join(path.dirname(fileURLToPath(import.meta.url)), '..')
)
const SCAN_DIRS = ['src', 'include', 'modules', 'tools', 'bindings']
const CHECK = 'cppcoreguidelines-pro-type-member-init'
const HEADER_EXT = new Set(['.h', '.hpp', '.hh', '.hxx', '.inl'])
const SOURCE_EXT = new Set(['.cpp', '.cc', '.cxx'])

// -------------------------------------------------------------------------------------------------
// arguments
// -------------------------------------------------------------------------------------------------

function parseArgs(argv) {
    const opts = {
        targets: [],
        all: false,
        jobs: 4,
        db: null,
        staticOnly: false,
        toolOnly: false,
        base: process.env.CAMEL_MEMBER_INIT_BASE || 'origin/develop',
    }
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]
        if (arg === '--all') opts.all = true
        else if (arg === '--static-only') opts.staticOnly = true
        else if (arg === '--tool-only') opts.toolOnly = true
        else if (arg === '--jobs') opts.jobs = Math.max(1, Number(argv[++i]) || 4)
        else if (arg === '--db') opts.db = argv[++i]
        else if (arg === '--base') opts.base = argv[++i]
        else if (arg.startsWith('-')) {
            console.error(`check-member-init: unknown argument '${arg}'`)
            process.exit(2)
        } else opts.targets.push(arg)
    }
    return opts
}

// -------------------------------------------------------------------------------------------------
// the files the check reads
// -------------------------------------------------------------------------------------------------

function isSourceFile(file) {
    const ext = path.extname(file).toLowerCase()
    return HEADER_EXT.has(ext) || SOURCE_EXT.has(ext)
}

function walk(dir, out) {
    let entries
    try {
        entries = fs.readdirSync(dir, { withFileTypes: true })
    } catch {
        return out
    }
    for (const entry of entries) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) {
            if (entry.name === 'node_modules' || entry.name === 'build' || entry.name === 'out') continue
            walk(full, out)
        } else if (entry.isFile() && isSourceFile(entry.name)) out.push(full)
    }
    return out
}

function git(args, root) {
    return new Promise((resolve) => {
        const proc = spawn('git', args, { cwd: root })
        let out = ''
        proc.stdout.on('data', (data) => (out += data))
        proc.on('close', () => resolve(out))
    })
}

async function changedFiles(base, root) {
    // Two reads, because a new file is untracked until it is staged: the diff names what changed in
    // the index and the working tree against the base, `ls-files --others` the rest. A base that
    // cannot be read leaves the diff empty, which would read as a pass -- so it is an error here.
    const [diffed, untracked] = await Promise.all([
        git(['diff', '--name-only', '--diff-filter=d', base, '--'], root),
        git(['ls-files', '--others', '--exclude-standard'], root),
    ])
    if (diffed.trim() === '' && untracked.trim() === '') {
        const hasBase = (await git(['rev-parse', '--verify', '--quiet', `${base}^{commit}`], root)).trim()
        if (!hasBase) {
            console.error(`check-member-init: cannot read the base '${base}', so a check of nothing would read as a pass`)
            process.exit(2)
        }
    }
    const files = []
    for (const name of `${diffed}\n${untracked}`.split(/\r?\n/).map((line) => line.trim())) {
        if (!name) continue
        const full = path.resolve(root, name)
        if (!isSourceFile(full) || !fs.existsSync(full)) continue
        files.push(full)
    }
    return files
}

// -------------------------------------------------------------------------------------------------
// the compiler engine: clang-tidy with the compilation database the build leaves behind
// -------------------------------------------------------------------------------------------------

function findOnPath(names) {
    const dirs = (process.env.PATH || '').split(path.delimiter).filter(Boolean)
    const exts = process.platform === 'win32' ? ['.exe', '.cmd', ''] : ['']
    for (const dir of dirs) {
        for (const name of names) {
            for (const ext of exts) {
                const full = path.join(dir, name + ext)
                try {
                    if (fs.statSync(full).isFile()) return full
                } catch {
                    /* not here */
                }
            }
        }
    }
    return null
}

function findClangTidy() {
    const named = process.env.CAMEL_CLANG_TIDY
    if (named && fs.existsSync(named)) return named
    const onPath = findOnPath(['clang-tidy'])
    if (onPath) return onPath
    // A toolchain that ships clang-tidy ships it beside the compiler, which is often named and
    // rarely on PATH.
    for (const variable of ['CXX', 'CC']) {
        const compiler = process.env[variable]
        if (!compiler) continue
        const twin = path.join(
            path.dirname(compiler),
            process.platform === 'win32' ? 'clang-tidy.exe' : 'clang-tidy'
        )
        if (fs.existsSync(twin)) return twin
    }
    return null
}

function findDatabase(root, named) {
    const candidates = []
    if (named) candidates.push(named)
    for (const relative of ['build', 'cmake-build-debug', 'cmake-build-release', 'out/latest', '.cache/build']) {
        candidates.push(path.join(root, relative))
    }
    for (const dir of candidates) {
        const file = path.join(dir, 'compile_commands.json')
        try {
            const entries = JSON.parse(fs.readFileSync(file, 'utf8'))
            if (Array.isArray(entries) && entries.length > 0) return { dir: path.dirname(file), file, entries }
        } catch {
            /* not here */
        }
    }
    return null
}

// The database names every translation unit once per build type. The compiler is asked about those
// and nothing else: a header is examined through the translation units that include it, which is
// the only way clang examines a header anyway, and giving a header a command of its own would
// invent a translation unit no build has. A header included by no translation unit is reported as
// uncovered rather than counted as clean.
function uniqueTranslationUnits(database, root, wanted) {
    const seen = new Map()
    for (const entry of database.entries) {
        if (!entry.file) continue
        const resolved = path.resolve(String(entry.file))
        if (!resolved.startsWith(root)) continue
        if (wanted && !wanted.has(resolved)) continue
        // One entry per file: the first build type in the database decides the flags.
        if (!seen.has(resolved)) seen.set(resolved, entry)
    }
    return [...seen.values()]
}

function runClangTidy(clangTidy, database, entries, jobs) {
    const warnings = []
    const failures = []
    let cursor = 0
    const worker = () =>
        new Promise((resolve) => {
            const step = () => {
                if (cursor >= entries.length) return resolve()
                const entry = entries[cursor++]
                const file = path.resolve(String(entry.file))
                const args = ['-p', database.dir, `--checks=-*,${CHECK}`, '--header-filter=.*', '--quiet', file]
                const proc = spawn(clangTidy, args, { cwd: database.dir })
                let out = ''
                proc.stdout.on('data', (data) => (out += data))
                proc.stderr.on('data', (data) => (out += data))
                proc.on('close', () => {
                    // A translation unit that did not parse produces findings whose locations are
                    // not the source: 134 of them came out of `src/bindings/python/module.cpp`, naming
                    // members of classes the compiler never finished reading. A unit that reported
                    // an error contributes nothing and is named instead.
                    const local = []
                    let broken = false
                    for (const line of out.split(/\r?\n/)) {
                        const head = line.match(/^(.+?):(\d+):(\d+): (warning|error): (.*)$/)
                        if (!head) continue
                        const message = head[5]
                        if (!message.includes(`[${CHECK}]`)) {
                            if (head[4] === 'error') broken = true
                            continue
                        }
                        // `... does not initialize these fields: a, b [check-name]` names the fields between the colon
                        // and the tag; a class-type member is named by `uninitialized record type: 'a'`
                        // instead, which is the same report with one name in quotes.
                        const plain = message.replace(/\s*\[[^\]]*\]\s*$/, '')
                        const names = /uninitialized record type/.test(plain)
                            ? [...plain.matchAll(/'([^']+)'/g)].map((quoted) => quoted[1].trim())
                            : plain
                                  .replace(/^.*?fields:\s*/, '')
                                  .split(',')
                                  .map((field) => field.trim())
                                  .filter(Boolean)
                        if (names.length === 0) continue
                        local.push({ file: path.normalize(head[1]), line: Number(head[2]), fields: names })
                    }
                    if (broken) failures.push({ file })
                    else warnings.push(...local)
                    step()
                })
            }
            step()
        })
    const width = Math.max(1, Math.min(jobs, entries.length))
    return Promise.all(Array.from({ length: width }, () => worker())).then(() => ({ warnings, failures }))
}

// -------------------------------------------------------------------------------------------------
// the text engine: the same rule, read off the file
// -------------------------------------------------------------------------------------------------

// Comments and literal contents out, newlines kept so line numbers stay right.
function scrub(input) {
    // CRLF is normalised first: the same file is checked on Windows and on the Linux build host,
    // and a `\r` left at the end of a line is a difference between the two readings that neither
    // of them reports.
    const text = String(input)
        .replace(/\r\n/g, '\n')
        // A preprocessor directive is not a declaration, and one left between two members (`#endif`
        // after a member, `#if` before one) reads as part of the next member's type: a directive
        // line, and the lines it continues with a backslash, is blanked.
        .replace(/^[ \t]*#(?:[^\n]*\\\n)*[^\n]*/gm, (directive) => directive.replace(/[^\n]/g, ' '))
    let out = ''
    let i = 0
    const blank = (ch) => (ch === '\n' ? '\n' : ' ')
    while (i < text.length) {
        const two = text.slice(i, i + 2)
        if (two === '//') {
            while (i < text.length && text[i] !== '\n') i++
        } else if (two === '/*') {
            i += 2
            while (i < text.length && !(text[i] === '*' && text[i + 1] === '/')) {
                out += blank(text[i])
                i++
            }
            i += 2
        } else if (text[i] === '"' || text[i] === "'") {
            const quote = text[i]
            out += ' '
            i++
            while (i < text.length && text[i] !== quote) {
                if (text[i] === '\\') {
                    out += ' '
                    i++
                }
                out += blank(text[i])
                i++
            }
            out += ' '
            i++
        } else {
            out += text[i]
            i++
        }
    }
    return out
}

const CLASS_HEAD = /\b(class|struct|union)\s+([A-Za-z_]\w*)?\s*(final\b)?\s*([^;{}()]*)?\{/g

function matchingBrace(text, open) {
    let depth = 0
    for (let i = open; i < text.length; i++) {
        if (text[i] === '{') depth++
        else if (text[i] === '}') {
            depth--
            if (depth === 0) return i
        }
    }
    return -1
}

// `<` and `>` are dropped with what they hold, so that a template argument list cannot be mistaken
// for a parameter list (`std::function<void()> f;` has one and is a data member).
function collapseAngles(text) {
    let out = ''
    let depth = 0
    for (let i = 0; i < text.length; i++) {
        const ch = text[i]
        if (ch === '<' && depth >= 0 && i > 0 && /[\w:>]/.test(text[i - 1])) {
            depth++
            continue
        }
        if (ch === '>' && depth > 0) {
            depth--
            continue
        }
        if (depth === 0) out += ch
    }
    return out
}

function splitDeclarators(text) {
    const parts = []
    let depth = 0
    let start = 0
    for (let i = 0; i < text.length; i++) {
        const ch = text[i]
        if (ch === '(' || ch === '[' || ch === '{' || ch === '<') depth++
        else if (ch === ')' || ch === ']' || ch === '}' || ch === '>') depth--
        else if (ch === ',' && depth === 0) {
            parts.push(text.slice(start, i))
            start = i + 1
        }
    }
    parts.push(text.slice(start))
    return parts
}

// The declaration without its initializer, its initializer braces and its array bounds: what is
// left starts with the type and ends with the name.
function headOfDeclarator(declarator) {
    let head = declarator
    let depth = 0
    for (let i = 0; i < head.length; i++) {
        const ch = head[i]
        if (ch === '(' || ch === '[' || ch === '{' || ch === '<') depth++
        else if (ch === ')' || ch === ']' || ch === '}' || ch === '>') depth--
        else if (ch === '=' && depth === 0) {
            head = head.slice(0, i)
            break
        } else if (ch === '{' && depth === 0) {
            head = head.slice(0, i)
            break
        } else if (ch === ':' && depth === 0 && head[i - 1] !== ':' && head[i + 1] !== ':') {
            // A bit-field width. `::` is a scope and `a ? b : c` cannot appear here.
            head = head.slice(0, i)
            break
        }
    }
    const bound = head.indexOf('[')
    if (bound >= 0) head = head.slice(0, bound)
    return head
}

function declaratorName(declarator) {
    const names = [...headOfDeclarator(declarator).matchAll(/([A-Za-z_]\w*)/g)].map((match) => match[1])
    return names.length > 0 ? names[names.length - 1] : null
}

function declaratorType(declarator) {
    const name = declaratorName(declarator)
    if (!name) return ''
    const head = headOfDeclarator(declarator)
    const index = head.lastIndexOf(name)
    return head.slice(0, index).trim().replace(/\s+/g, ' ')
}

const SKIP_STATEMENT = /^(using|typedef|friend|template|enum|class|struct|union|namespace|static|extern|thread_local)\b/
const ACCESS_SPECIFIER = /^(public|private|protected)\s*$/

// -------------------------------------------------------------------------------------------------
// which member types are indeterminate when a constructor leaves them alone
// -------------------------------------------------------------------------------------------------
//
// `cppcoreguidelines-pro-type-member-init` reports the members whose value would be whatever was
// in the storage: the scalars, the pointers, the enumerators, the bit-fields and the unions. A
// member of a class type is default constructed and has a value already -- `std::string
// libraryEntryFunction_` is `""` whatever the constructor does -- and reporting it would ask for
// an initializer that changes nothing. The text read needs the same distinction, so it builds the
// list of what this tree's names mean: an enumerator, a union, an alias of either, or a class.

const BUILTIN = new Set([
    'bool', 'char', 'char8_t', 'char16_t', 'char32_t', 'wchar_t', 'short', 'int', 'long', 'float',
    'double', 'signed', 'unsigned', 'void', 'auto', 'size_t', 'ssize_t', 'ptrdiff_t', 'intptr_t',
    'uintptr_t', 'intmax_t', 'uintmax_t', 'int8_t', 'int16_t', 'int32_t', 'int64_t', 'uint8_t',
    'uint16_t', 'uint32_t', 'uint64_t', 'byte', 'nullptr_t', 'decltype',
])

function buildTypeIndex(files) {
    const kinds = new Map() // name -> 'scalar' | 'record' | { alias: 'text' }
    // A constructor can be declared in the class and defined in a .cpp, and then the initializer
    // list that tells this check anything is in the .cpp: `LoopView::LoopView(...) : program_(...)`.
    // The names those definitions initialize are collected per class, because the read of the
    // header alone would report every member of such a class.
    const outOfLine = new Map() // class name -> Set(member names)
    const note = (name, kind) => {
        if (!name) return
        if (!kinds.has(name)) kinds.set(name, kind)
    }
    for (const file of files) {
        let text
        try {
            text = scrub(fs.readFileSync(file, 'utf8'))
        } catch {
            continue
        }
        for (const match of text.matchAll(/\benum\s+(?:class\s+|struct\s+)?([A-Za-z_]\w*)/g)) note(match[1], 'scalar')
        for (const match of text.matchAll(/\bunion\s+([A-Za-z_]\w*)\s*\{/g)) note(match[1], 'scalar')
        for (const match of text.matchAll(/\b(class|struct)\s+([A-Za-z_]\w*)\s*(?:final\b)?\s*(?::[^;{}()]*)?\{/g))
            note(match[2], 'record')
        for (const match of text.matchAll(/\busing\s+([A-Za-z_]\w*)\s*=\s*([^;]+);/g))
            note(match[1], { alias: match[2] })
        for (const match of text.matchAll(/\btypedef\s+([^;{}]+?)\s+([A-Za-z_]\w*)\s*;/g))
            note(match[2], { alias: match[1] })
        const definitions = /(?:^|[^\w:])([A-Za-z_]\w*)::\1\s*\(/g
        let definition
        while ((definition = definitions.exec(text)) !== null) {
            const open = definition.index + definition[0].length - 1
            const close = closingParen(text, open)
            if (close < 0) continue
            const tail = ctorTail(text, close + 1)
            if (tail.defaulted) continue
            const names = outOfLine.get(definition[1]) || new Set()
            for (const name of initializedNames(tail.list)) names.add(name)
            if (tail.body >= 0) for (const name of assignedNames(text, tail.body)) names.add(name)
            outOfLine.set(definition[1], names)
            definitions.lastIndex = Math.max(definitions.lastIndex, tail.body >= 0 ? tail.body + 1 : close + 1)
        }
    }
    return { kinds, outOfLine }
}

function lastName(text) {
    const names = [...String(text).matchAll(/([A-Za-z_]\w*)/g)].map((match) => match[1])
    return names.length > 0 ? names[names.length - 1] : ''
}

// The names an initializer list names: `: a_(x), b_{y}, c(z)`. Entries can hold a braced value of
// their own (`elderGenSpace_(GrowableSpaceConfig{...})`), so a braced group is stepped over.
function initializedNames(list) {
    const names = []
    const text = String(list || '').replace(/^\s*:/, '')
    let depth = 0
    let entry = ''
    for (const ch of text) {
        if (ch === '(' || ch === '{') depth++
        else if (ch === ')' || ch === '}') depth--
        if (ch === ',' && depth === 0) {
            names.push(...fromEntry(entry))
            entry = ''
        } else entry += ch
    }
    names.push(...fromEntry(entry))
    return names.filter(Boolean)
}

function fromEntry(entry) {
    const named = entry.match(/^\s*([A-Za-z_]\w*)\s*[({]/)
    return named ? [named[1]] : []
}

// A member assigned in the constructor body counts as initialized: the compiler's check reads
// `kind_ = graph.loopKindOf(node);` in the body as initialization, so a read of the header that
// stops at the initializer list would report every member such a constructor sets.
function assignedNames(text, braceAt) {
    const close = matchingBrace(text, braceAt)
    if (close < 0) return []
    const body = text.slice(braceAt + 1, close)
    const names = []
    for (const match of body.matchAll(/([A-Za-z_]\w*)\s*(?:=(?!=)|[-+*/|&]=)/g)) {
        const name = match[1]
        if (name === 'return') continue
        names.push(name)
    }
    return names
}

// The tail of a constructor after its parameter list: its initializer list and its body. Reading
// it by hand is what tells the body's `{` from a braced initializer of a member
// (`elderGenSpace_(GrowableSpaceConfig{...})`): a top-level `{` that follows a name belongs to the
// name, and any other top-level `{` starts the body. `= default` and `= delete` answer `defaulted`,
// because a defaulted constructor is not one that can leave a member out.
function ctorTail(text, after) {
    let i = after
    while (i < text.length && /\s/.test(text[i])) i++
    if (text[i] === '=') return { list: '', body: -1, defaulted: true }
    if (text[i] === ';') return { list: '', body: -1, defaulted: false }
    if (text[i] !== ':') return { list: '', body: text[i] === '{' ? i : -1, defaulted: false }
    i++
    const start = i
    let paren = 0
    while (i < text.length) {
        const ch = text[i]
        if (ch === '(') paren++
        else if (ch === ')') paren--
        else if (ch === ';') return { list: text.slice(start, i), body: -1, defaulted: false }
        else if (ch === '{' && paren === 0) {
            let back = i - 1
            while (back >= start && /\s/.test(text[back])) back--
            const previous = back >= start ? text[back] : ''
            if (!/[\w>\]]/.test(previous)) return { list: text.slice(start, i), body: i, defaulted: false }
            const close = matchingBrace(text, i)
            if (close < 0) break
            i = close
        }
        i++
    }
    return { list: text.slice(start), body: -1, defaulted: false }
}

// The `)` that closes the parameter list opened at `open`.
function closingParen(text, open) {
    let depth = 0
    for (let i = open; i < text.length; i++) {
        if (text[i] === '(') depth++
        else if (text[i] === ')') {
            depth--
            if (depth === 0) return i
        }
    }
    return -1
}

function classifyType(typeText, kinds, depth = 0) {
    let text = String(typeText || '').trim()
    if (!text) return 'unknown'
    // Inside a template argument list a pointer is the argument, not the member: `std::vector<const
    // ModuleUnit *>` is a vector, and a vector is default constructed.
    const flat = collapseAngles(text)
    if (flat.includes('&')) return 'reference'
    if (flat.includes('*')) return 'scalar'
    if (text.includes('<')) return 'record'
    for (const word of ['const', 'volatile', 'mutable', 'struct', 'class', 'enum', 'typename', 'register'])
        text = text.replace(new RegExp(`\\b${word}\\b`, 'g'), ' ')
    text = text.replace(/^\s*::/, '').replace(/\s+/g, ' ').trim()
    if (!text) return 'unknown'
    const base = lastName(text.split(/\s+/).pop())
    if (!base) return 'unknown'
    if (BUILTIN.has(base)) return 'scalar'
    const known = kinds.get(base)
    if (!known) {
        // A name in another namespace is a type of another library (`std::string`, a third-party
        // handle): its default constructor runs, so it has a value. An unqualified name the tree
        // never declares is one this read cannot place, and it says so instead of exempting it.
        return text.includes('::') ? 'record' : 'unknown'
    }
    if (known === 'scalar' || known === 'record') return known
    if (depth > 4) return 'unknown'
    return classifyType(known.alias, kinds, depth + 1)
}

function membersOfClass(body, className, firstLine) {
    const members = []
    let depth = 0
    let line = firstLine
    let statementLine = firstLine
    let statement = ''

    const flush = () => {
        const raw = statement
        statement = ''
        const leading = raw.match(/^\s*/)[0]
        const atLine = statementLine + (leading.match(/\n/g) || []).length
        const text = raw.replace(/\s+/g, ' ').trim()
        if (!text) return
        if (ACCESS_SPECIFIER.test(text.replace(/:/g, '').trim())) return
        if (SKIP_STATEMENT.test(text)) return
        if (text.includes('operator')) return
        // A parameter list, or a functional initializer (`int x(0);`): neither is an
        // uninitialized data member, and the two cannot be told apart without a parser.
        if (collapseAngles(text).includes('(')) return
        for (const declarator of splitDeclarators(text)) {
            if (!declarator.trim()) continue
            const name = declaratorName(declarator)
            if (!name || name === className) continue
            if (/\bstatic\b/.test(declarator)) continue
            // A flexible array member (char data_[];) has no bound to fill and cannot be given an
            // initializer at all; the compiler's check does not report it either.
            if (/\[\s*\]/.test(declarator)) continue
            members.push({
                name,
                type: declaratorType(declarator),
                line: atLine,
                initialized: /=[^=]/.test(declarator) || /\{/.test(declarator) || /\(/.test(declarator),
            })
        }
    }

    for (let i = 0; i < body.length; i++) {
        const ch = body[i]
        if (ch === '\n') line++
        if (depth === 0 && ch === ':') {
            // `public:` and `private:` end a run of members and carry no statement of their own.
            const so_far = statement.replace(/\s+/g, ' ').trim()
            if (ACCESS_SPECIFIER.test(so_far)) {
                statement = ''
                statementLine = line
                continue
            }
        }
        if (ch === '{') depth++
        else if (ch === '}') {
            depth--
            if (depth === 0) {
                // A function body or a nested class ends here. It is a statement of its own unless
                // a `;` or `,` follows, and the `;` path below is what ends a nested class.
                let j = i + 1
                while (j < body.length && /\s/.test(body[j])) j++
                if (body[j] !== ';' && body[j] !== ',') {
                    flush()
                    statementLine = line
                    continue
                }
            }
        }
        if (ch === ';' && depth === 0) {
            flush()
            statementLine = line
            continue
        }
        statement += ch
    }
    return members
}

function constructorsOf(body, className, outOfLine) {
    const initializers = []
    let hasConstructor = false
    const pattern = new RegExp(`(^|[^\\w~])${className}\\s*\\(`, 'g')
    let match
    while ((match = pattern.exec(body)) !== null) {
        const open = match.index + match[0].length - 1
        const close = closingParen(body, open)
        if (close < 0) continue
        const tail = ctorTail(body, close + 1)
        if (tail.defaulted) continue // `Foo() = default;`: nothing to leave out
        hasConstructor = true
        if (new RegExp(`\\b${className}\\s*\\(`).test(tail.list)) continue // delegates: the target initializes
        const names = new Set(initializedNames(tail.list))
        if (tail.body >= 0) for (const name of assignedNames(body, tail.body)) names.add(name)
        initializers.push(names)
        pattern.lastIndex = Math.max(pattern.lastIndex, tail.body >= 0 ? tail.body + 1 : close + 1)
    }
    // A constructor declared in the class and defined in a .cpp carries its initializer list there,
    // and what those definitions initialize counts as initialized by this class.
    const elsewhere = outOfLine && outOfLine.get(className)
    if (elsewhere) initializers.push(elsewhere)
    return { hasConstructor, initializers }
}

// `struct { int a; int b; } pair_;` puts `a` and `b` in the class that holds the anonymous
// aggregate, so they are members of it and are read like any other. The block is read on its own
// and blanked out of the class body, so its members are not counted twice and its braces do not
// make the members look nested.
function anonymousBlocks(body) {
    const blocks = []
    const pattern = /\b(struct|union)\s*\{/g
    let match
    while ((match = pattern.exec(body)) !== null) {
        const open = match.index + match[0].length - 1
        const close = matchingBrace(body, open)
        if (close < 0) continue
        blocks.push({ open, close })
        pattern.lastIndex = close
    }
    return blocks
}

function scanText(file, index, unknowns) {
    const { kinds, outOfLine } = index
    const text = scrub(fs.readFileSync(file, 'utf8'))
    const findings = []
    CLASS_HEAD.lastIndex = 0
    let head
    while ((head = CLASS_HEAD.exec(text)) !== null) {
        const name = head[2]
        if (!name) continue
        // A union's variant members cannot each carry an initializer -- C++ allows one, and the
        // union's default constructor decides which is live -- so the rule this check reads does
        // not apply to a union, and its variants are not reported.
        if (head[1] === 'union') continue
        const open = text.indexOf('{', head.index + head[0].length - 1)
        if (open < 0) continue
        const close = matchingBrace(text, open)
        if (close < 0) continue
        const body = text.slice(open + 1, close)
        // The body starts right after the `{`, which can be lines below the `class` keyword when
        // the class head or its base clause wraps.
        const classLine = text.slice(0, open).split('\n').length
        const blocks = anonymousBlocks(body)
        let outer = body
        if (blocks.length > 0) {
            const chars = [...body]
            for (const block of blocks)
                for (let i = block.open + 1; i < block.close; i++) if (chars[i] !== '\n') chars[i] = ' '
            outer = chars.join('')
        }
        const members = membersOfClass(outer, name, classLine)
        for (const block of blocks) {
            const innerLine = classLine + (body.slice(0, block.open + 1).match(/\n/g) || []).length
            members.push(...membersOfClass(body.slice(block.open + 1, block.close), name, innerLine))
        }
        if (members.length === 0) continue
        const { hasConstructor, initializers } = constructorsOf(body, name, outOfLine)
        if (!hasConstructor) continue // an aggregate: no constructor to leave anything out
        for (const member of members) {
            if (member.initialized) continue
            // A member one constructor names counts as initialized: reading a class's constructors one by
            // one and asking each whether it names the member needs a parser (the compiler does
            // that), and this read answers the weaker question -- does any constructor name it.
            if (initializers.some((names) => names.has(member.name))) continue
            const kind = classifyType(member.type, kinds)
            if (kind === 'unknown') {
                // An unknown spelling is counted so that it is not silently exempted.
                unknowns.push({ file, line: member.line, type: member.type, name: member.name })
                continue
            }
            // Scalars, pointers, enums, bit-fields and function pointers: the members whose value
            // is whatever the storage held. A class-type member is constructed by its own default
            // constructor, so leaving it out of the initializer list creates no indeterminate
            // value; the compiler's check reports it as a matter of consistency (and the census
            // gives it an initializer too), but a check that flagged one would be asking for a
            // line to be written, not for a defect to be closed. A reference cannot be left out.
            if (kind !== 'scalar') continue
            findings.push({ file, line: member.line, name: member.name, type: member.type })
        }
    }
    return findings
}

// -------------------------------------------------------------------------------------------------
// reporting
// -------------------------------------------------------------------------------------------------

const NOT_A_TYPE = new Set([
    'class', 'struct', 'union', 'enum', 'public', 'private', 'protected', 'return', 'namespace',
    'using', 'typedef', 'template', 'friend', 'static', 'constexpr', 'inline', 'virtual', 'mutable',
])

// The tool reports at the constructor; the declaration it names is elsewhere in the same class.
// This finds that declaration so the report can print its line and its type.
function resolveMember(file, name, hintLine) {
    const candidates = [file]
    const stem = path.join(path.dirname(file), path.basename(file, path.extname(file)))
    for (const ext of ['.h', '.hpp', '.hh', '.hxx']) {
        if (fs.existsSync(stem + ext)) candidates.push(stem + ext)
    }
    const pattern = new RegExp(`(^|[^\\w.>])${name}\\s*(=[^=]|;|:|\\[|\\{|\\()`)
    for (const candidate of candidates) {
        let lines
        try {
            lines = scrub(fs.readFileSync(candidate, 'utf8')).split(/\r?\n/)
        } catch {
            continue
        }
        const found = []
        for (let i = 0; i < lines.length; i++) {
            const text = lines[i].trim()
            if (!text || !pattern.test(text)) continue
            if (!/(;|,)\s*$/.test(text)) continue
            const at = text.lastIndexOf(name)
            if (at <= 0) continue
            const type = text
                .slice(0, at)
                .replace(/^\s*(public|private|protected)\s*:\s*/, '')
                .trim()
                .replace(/\s+/g, ' ')
            if (!type || NOT_A_TYPE.has(type.split(/\s+/)[0])) continue
            found.push({ file: candidate, line: i + 1, type, ends: text.endsWith(';') ? 0 : 1 })
        }
        // The name can be declared in several places in one file (a parameter with a default, the
        // member, a local); the declaration is the one nearest the line the compiler named, and a
        // declaration that ends a statement before one that ends a declarator list.
        found.sort((a, b) => a.ends - b.ends || Math.abs(a.line - hintLine) - Math.abs(b.line - hintLine))
        if (found.length > 0) return found[0]
    }
    return null
}

// -------------------------------------------------------------------------------------------------
// main
// -------------------------------------------------------------------------------------------------

const opts = parseArgs(process.argv.slice(2))

async function main() {
    let files = []
    if (opts.targets.length > 0) {
        for (const target of opts.targets) {
            const full = path.resolve(ROOT, target)
            if (!fs.existsSync(full)) {
                console.error(`check-member-init: '${target}' does not exist`)
                process.exit(2)
            }
            if (fs.statSync(full).isDirectory()) walk(full, files)
            else if (isSourceFile(full)) files.push(full)
        }
    } else if (opts.all) {
        for (const dir of SCAN_DIRS) walk(path.join(ROOT, dir), files)
    } else {
        files = await changedFiles(opts.base, ROOT)
    }
    files = [...new Set(files.map((file) => path.resolve(file)))].sort()

    if (files.length === 0) {
        console.error(
            'check-member-init: the scan holds no C++ source, so a check of nothing would read as a pass'
        )
        process.exit(2)
    }

    const clangTidy = opts.staticOnly ? null : findClangTidy()
    const database = clangTidy ? findDatabase(ROOT, opts.db) : null
    const useTool = Boolean(clangTidy && database)

    if (!useTool && opts.toolOnly) {
        console.error('check-member-init: --tool-only was asked for and there is no clang-tidy with a compilation database')
        process.exit(2)
    }

    let findings
    let engine
    let failures = []
    let unknowns = []
    if (useTool) {
        const version = await new Promise((resolve) => {
            const proc = spawn(clangTidy, ['--version'])
            let out = ''
            proc.stdout.on('data', (data) => (out += data))
            proc.on('close', () => resolve((out.match(/LLVM version (\S+)/) || [])[1] || 'unknown'))
        })
        // Named paths mean "the translation units among these": the compiler cannot be pointed at a
        // header on its own without inventing a translation unit no build has.
        const wanted = opts.targets.length > 0 ? new Set(files) : null
        const units = uniqueTranslationUnits(database, ROOT, wanted)
        if (units.length === 0) {
            console.error(
                'check-member-init: the named files are not translation units of the compilation database, ' +
                    'so the compiler was not asked anything; use the text read for headers'
            )
            process.exit(2)
        }
        const result = await runClangTidy(clangTidy, database, units, opts.jobs)
        failures = result.failures
        const unitset = new Set(units.map((entry) => path.resolve(String(entry.file))))
        const headers = opts.targets.length > 0 ? files.filter((file) => !unitset.has(file)) : []
        if (headers.length > 0)
            console.log(
                `check-member-init: ${headers.length} named file(s) are headers, which the compiler examines ` +
                    'through the translation units that include them'
            )
        findings = []
        const seen = new Set()
        for (const warning of result.warnings) {
            if (!warning.file.startsWith(ROOT)) continue
            for (const name of warning.fields) {
                const resolved = resolveMember(warning.file, name, warning.line)
                const file = resolved ? resolved.file : warning.file
                const line = resolved ? resolved.line : warning.line
                const key = `${file}:${line}:${name}`
                if (seen.has(key)) continue
                seen.add(key)
                findings.push({ file, line, name, type: resolved ? resolved.type : '' })
            }
        }
        engine = `clang-tidy ${version}, ${units.length} translation unit(s)`
    } else {
        findings = []
        // What this tree's names mean is read from the whole tree, whichever files are checked: a
        // member's type is usually declared in another file.
        const everyFile = []
        for (const dir of SCAN_DIRS) walk(path.join(ROOT, dir), everyFile)
        const index = buildTypeIndex(everyFile)
        for (const file of files) {
            for (const finding of scanText(file, index, unknowns)) findings.push(finding)
        }
        engine = `text read, ${files.length} file(s), no clang-tidy with a compilation database`
    }

    findings.sort((a, b) => (a.file === b.file ? a.line - b.line : a.file < b.file ? -1 : 1))
    for (const finding of findings) {
        const where = path.relative(ROOT, finding.file).split(path.sep).join('/')
        console.log(`check-member-init: ${where}:${finding.line}  ${finding.type}  ${finding.name}`)
    }
    // A file the compiler could not parse contributes nothing, which would read as a clean file:
    // it is named instead of being counted as checked.
    if (failures.length > 0) {
        const unique = [...new Set(failures.map((failure) => failure.file))]
        console.log(`check-member-init: ${unique.length} file(s) did not parse under the compilation database`)
        for (const file of unique.slice(0, 10)) console.log(`check-member-init:   unparsed ${path.relative(ROOT, file)}`)
    }
    // A member whose type the text read cannot place is not reported and not silently skipped: it
    // is counted, because "no finding" and "not examined" must not read the same.
    if (unknowns.length > 0) {
        console.log(`check-member-init: ${unknowns.length} member(s) of a type the text read cannot place`)
        for (const unknown of unknowns.slice(0, 10)) {
            const where = path.relative(ROOT, unknown.file).split(path.sep).join('/')
            console.log(`check-member-init:   unplaced ${where}:${unknown.line}  ${unknown.type}  ${unknown.name}`)
        }
    }
    console.log(`check-member-init: ${findings.length} finding(s) [${engine}]`)
    process.exit(findings.length === 0 ? 0 : 1)
}

main()