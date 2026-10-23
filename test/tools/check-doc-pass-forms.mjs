#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Fails when a reader-facing page teaches a pass invocation the command line refuses.
 *
 * A pass parameter is a `key=value` word after the pass name (design principles 9.3). The
 * ruling that wrote that down deleted three other spellings at once: `std::opt(safe_fp=true)`,
 * the colon spelling `safe_fp: true`, and positional parameters. A page that still shows one is
 * a defect by the same reasoning: the reader copies a spelling the tool refuses with
 * `RuntimeDiag::InvalidPassParameter`.
 *
 *   node test/tools/check-doc-pass-forms.mjs
 *   node test/tools/check-doc-pass-forms.mjs --list    print every invocation with its page
 *
 * What it checks, and where each rule comes from:
 *
 *   * A page's `camel …` command line is split the way the command line splits itself:
 *     `splitPassList` for a `--passes` list and `groupPassWords` for the words after the
 *     program (`src/execute/pass_spec.cpp`). Each specification that comes out is then read by
 *     the parser's own rule, `parseSpec` in the same file: the name may contain no `(`, and
 *     every word after the name is a `key=value` word whose key is a bare name and whose value
 *     is not empty. A command line is a fenced block line (`camel …`, optionally after a `//`
 *     comment mark), a `// camel …` line inside a fence, or an inline `` `camel …` `` span.
 *   * A word after the program that is not a pass name is reported when it is unmistakably a
 *     parameter: `safe_fp:` is the colon spelling, and a bare literal (`true`, `2`) directly
 *     after a pass name is a positional parameter. Both are refused, and both are exact shapes.
 *   * A pass name immediately followed by `(` is refused as well, anywhere in the scanned
 *     pages, run or only quoted in prose. That is how the one defect found by hand read:
 *     `stdlib/rules.cml` described `simplify_strict` with `std::opt(safe_fp=true)`.
 *   * Each distinct specification is then run through the **real parser**. The invocation is
 *     followed by a pass name no build registers, so the pipeline is resolved and refused
 *     before any pass runs: a spelling the parser rejects is `RuntimeDiag::InvalidPassParameter`,
 *     while a well-formed specification reaches the unknown name
 *     (`RuntimeDiag::UnrecognizedGraphPass`) or a parameter the declaration does not accept
 *     (`SemanticDiag::ParamArgument*`). An invocation the real parser accepts is one a reader
 *     can copy. This stage reads the value's text as the parser reads it, so it catches what no
 *     rule here mirrors: a value that is not a constant expression.
 *
 * What it cannot catch, and why:
 *
 *   * A pass name the build does not declare (a stale name such as the `std::tiles` a design
 *     note still writes) is not a spelling problem: `UnrecognizedGraphPass` is also what a page
 *     shows on purpose (`docs/cli.md` uses `std::no_such_pass` as its example). Names are not
 *     checked here. A rule for them is possible and exact — a `std::` name in a page must be a
 *     declaration of `stdlib/std.cml` or an alias of the core — but it needs the deliberate
 *     unknown-name examples excepted, so it is not part of this guard.
 *   * The colon and positional spellings are reported on a `camel` command line, and the paren
 *     spelling anywhere; a page that teaches the colon spelling in prose alone is not caught.
 *     `safe_fp: true` is also how Camel source writes a named argument, which the ruling kept,
 *     so away from a command line the two cannot be told apart.
 *   * A positional value that is not a bare literal (`std::opt model.onnx`) is not reported: a
 *     stray word cannot be told apart from a placeholder (`<file>`) or a redirection
 *     (`2>/dev/null`). The command line still refuses it, as an unknown pass name.
 *   * A parameter name or type the declaration does not accept is the other half of the same
 *     machinery (`SemanticDiag::ParamArgument*`), not a spelling: the real parser reports it and
 *     the run accepts it, because checking the name against every `module::pass` declaration
 *     needs the module search paths, which this guard does not read.
 *   * A specification written in Camel source (`f->std::opt<safe_fp: true>`) is the source
 *     spelling of a named argument, which the ruling kept; only the command line changed.
 *   * `changes.log` and the record directories are history: an entry states the release it
 *     belongs to, and `docs/handover/` and `docs/reviews/` quote the old spelling to say what
 *     happened. They are not scanned.
 *
 * A line the guard would report is excused when it says why it is not a defect: the removed form
 * is written there in order to be called removed, or a decision and the parser disagree and the
 * conflict is recorded rather than patched. The exceptions are listed in EXCUSED with their
 * words, and each is checked to still match a line, so none can outlive what it was written for.
 * `args` in a test plan are pins, not pages: a refusal test must contain the spelling it pins
 * (`error.positional` in `test/plans/feat/opt/pass_params.plan.toml`), so test plans contribute
 * their `desc` strings to the pass-name rule and nothing to the invocation rule.
 *
 * A check of nothing is refused (exit 2), and the number of invocations checked, the number of
 * distinct specifications and the number that reached the probe are printed and pinned by
 * `test/plans/feat/opt/pass_params.plan.toml`, so a rule that stops matching is red rather than
 * silent.
 *
 *   CAMEL_EXE, CAMEL_HOME   the binary to run the specifications through
 *   CAMEL_DOC_FORM_JOBS     parallel camel processes (default 8)
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..')
const JOBS = Math.max(1, Number(process.env.CAMEL_DOC_FORM_JOBS || 8))
const TIMEOUT_MS = Number(process.env.CAMEL_DOC_FORM_TIMEOUT_MS || 20000)
const LIST = process.argv.includes('--list')

const CAMEL =
    process.env.CAMEL_EXE ||
    path.join(
        process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest'),
        'bin',
        process.platform === 'win32' ? 'camel.exe' : 'camel',
    )

// A name no build registers: the pipeline is refused on it before any pass runs, so the
// specifications before it are parsed and never executed.
const PROBE = 'std::doc_form_probe'

// Not pages: a release log states the release it belongs to, and the record directories quote
// the old spelling to say what happened.
const NOT_A_PAGE = ['changes.log', 'docs/handover/', 'docs/reviews/']
// Written by a script, never by hand: their text comes from the pages and the sources scanned
// here, so a hit in them is fixed where it is written.
const GENERATED = ['docs/diagnostics.md', 'docs/stdlib-reference.md']
const SKIP_DIRS = new Set(['.git', 'out', 'build', 'node_modules', 'third_party', 'sdks'])
const VCS_DIR = '.git'

// A line the guard does not report, with the words that make it an exception. Every entry is
// checked to still match a line, so an exception cannot outlive what it was written for.
//
// The first three show a removed form while saying it was removed. The last two are a decision
// the implementation does not keep: design principles 9.3 writes `out=model.onnx` and says a
// string value is taken as written, while the parser reads a value as a Camel constant
// expression and refuses that one ("the value of 'out' is not a constant"). The design
// principles are normative (AGENTS.md), so the conflict is recorded here and reported rather
// than patched in passing: either the example becomes the `'onnx::export out="model.onnx"'`
// spelling `docs/cli.md` and `AGENTS.md` already teach, or the parser takes a string as written.
const EXCUSED = [
    {
        file: 'docs/cli.md',
        pattern: /The parenthesised form \(`std::opt\(safe_fp=true\)`\), the colon spelling/,
        reason: 'says the three spellings are gone and how the parameter is written now',
    },
    {
        file: 'docs/cli.cn.md',
        pattern: /括号形式（`std::opt\(safe_fp=true\)`）、命令行上的冒号写法/,
        reason: '同 docs/cli.md：说明三种写法已删除，并给出现在的写法',
    },
    {
        file: 'docs/design/gpu/plan.cn.md',
        pattern: /没有 `tiles::compile\(device=\)`/,
        reason: 'quotes the review question and answers it in the negative: only `--devices` stays',
    },
    {
        file: 'docs/design-principles.cn.md',
        pattern: /（字符串取原文），不需要引号/,
        reason:
            'design principles 9.3 says a string value is written as it is and needs no quotes; ' +
            'the parser refuses `out=model.onnx` as a value that is not a constant — a recorded ' +
            'conflict between the norm and the implementation, for the author to settle',
    },
    {
        file: 'docs/design-principles.en.md',
        pattern: /\(a string takes the text as written\), and no quoting is needed/,
        reason: 'the English page of the same recorded conflict',
    },
]

const problems = []
const excusedUsed = new Map()
const invocations = []
const pageLines = new Map()

// Report a problem against a page line, unless that line says why it is not one.
function report(rel, line, message) {
    const text = (pageLines.get(rel) || [])[line - 1] || ''
    const entry = EXCUSED.find(
        (candidate) => candidate.file === rel && candidate.pattern.test(text)
    )
    if (entry) {
        excusedUsed.set(entry, (excusedUsed.get(entry) || 0) + 1)
        return
    }
    problems.push(`${rel}:${line}: ${message}`)
}

function read(file) {
    return fs.readFileSync(path.join(REPO_ROOT, file), 'utf8')
}

function relative(abs) {
    return path.relative(REPO_ROOT, abs).replace(/\\/g, '/')
}

function* walk(dir) {
    let entries
    try {
        entries = fs.readdirSync(dir, { withFileTypes: true })
    } catch {
        return
    }
    for (const entry of entries) {
        if (entry.name === VCS_DIR || SKIP_DIRS.has(entry.name)) continue
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) yield* walk(full)
        else yield full
    }
}

// ---- The pass names the command line knows ------------------------------------------------

const PASS_NAMES = new Set()

// The core's passes are the declarations of stdlib/std.cml, and the alias table of
// src/execute/pass.cpp names the spellings that resolve to them.
{
    let namespace = []
    for (const line of read('stdlib/std.cml').split(/\r?\n/)) {
        const opened = line.trim().match(/^namespace\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{$/)
        if (opened) {
            namespace.push(opened[1])
            continue
        }
        if (line.trim() === '}') {
            namespace.pop()
            continue
        }
        const func = line.trim().match(/^export\s+func\s+([A-Za-z_][A-Za-z0-9_]*)::<F>/)
        if (func) PASS_NAMES.add('std::' + [...namespace, func[1]].join('::'))
    }
    const text = read('src/execute/pass.cpp')
    const start = text.indexOf('const std::map<std::string, std::string> passAliases = {')
    const end = start < 0 ? -1 : text.indexOf('\n};', start)
    if (start < 0 || end < 0) {
        problems.push('src/execute/pass.cpp: the core alias table is not where the guard reads it')
    } else {
        for (const match of text.slice(start, end).matchAll(/\{"(std::[^"]+)",/g)) {
            PASS_NAMES.add(match[1])
        }
    }
}

// A module's passes: the declarations of stdlib/<module>.cml and the names the module registers.
{
    for (const entry of fs.readdirSync(path.join(REPO_ROOT, 'stdlib'))) {
        if (!entry.endsWith('.cml') || entry === 'std.cml') continue
        const module = entry.slice(0, -4)
        for (const match of read(`stdlib/${entry}`).matchAll(
            /^\s*export\s+func\s+([A-Za-z_][A-Za-z0-9_]*)\s*::\s*<[^>]+>\s*\(/gm
        )) {
            PASS_NAMES.add(`${module}::${match[1]}`)
        }
    }
    for (const file of walk(path.join(REPO_ROOT, 'modules'))) {
        if (!file.endsWith('.cpp')) continue
        const text = fs.readFileSync(file, 'utf8')
        const constants = new Map()
        for (const match of text.matchAll(
            /constexpr\s+const\s+char\s+\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]+)"/g
        )) {
            constants.set(match[1], match[2])
        }
        for (const match of text.matchAll(
            /registerModulePass\(\s*([A-Za-z_][A-Za-z0-9_]*|"[^"]+")/g
        )) {
            let value = match[1]
            if (value.startsWith('"')) value = value.slice(1, -1)
            else if (constants.has(value)) value = constants.get(value)
            else continue // `module + "::" + function`: a Camel pass, named at run time
            PASS_NAMES.add(value)
        }
        for (const match of text.matchAll(/registerPassAlias\(\s*"([^"]+)"/g)) {
            PASS_NAMES.add(match[1])
        }
    }
}

const BY_LENGTH = [...PASS_NAMES].sort((a, b) => b.length - a.length)
const escape = (name) => name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
// A known pass name immediately followed by `(`: the spelling the ruling deleted.
const PAREN_FORM = new RegExp(`(${BY_LENGTH.map(escape).join('|')})\\(`)
// A word that can be a specification's name: a qualified pass name, the shape a module's pass
// has whether or not this build registers it (`tiles::compile`, `mymod::report`).
const QUALIFIED = /^[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)+/

function knownName(text) {
    return BY_LENGTH.find((name) => text === name || text.startsWith(`${name}(`))
}

// ---- Splitting a command line, the way the command line splits itself ----------------------

// A shell word splitter: a quote is a delimiter, its content one word (`src/execute/pass_spec.cpp`
// reads what the shell left, so the quoting of a page is undone here the same way).
function shellSplit(text) {
    const tokens = []
    let current = ''
    let quote = 0
    for (let i = 0; i < text.length; i++) {
        const ch = text[i]
        if (quote) {
            if (ch === '\\' && i + 1 < text.length) {
                current += ch + text[++i]
                continue
            }
            if (ch === quote) {
                quote = 0
                continue
            }
            current += ch
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

// `splitPassList` (src/execute/pass_spec.cpp): commas outside quotes separate passes. The quote
// characters stay, because the parser reads them as part of a value (`stats=""`).
function splitPassList(text) {
    const out = []
    let current = ''
    let quote = 0
    const add = () => {
        const trimmed = current.trim()
        if (trimmed !== '') out.push(trimmed)
        current = ''
    }
    for (let i = 0; i < text.length; i++) {
        const ch = text[i]
        if (quote) {
            current += ch
            if (ch === '\\' && i + 1 < text.length) current += text[++i]
            else if (ch === quote) quote = 0
            continue
        }
        if (ch === '"' || ch === "'") quote = ch
        else if (ch === ',') {
            add()
            continue
        }
        current += ch
    }
    add()
    return out
}

// `groupPassWords` (src/execute/pass_spec.cpp): a word whose text before `=` is a bare name
// continues the specification before it; every other word starts one.
function groupPassWords(words, first = 0) {
    const out = []
    for (let i = first; i < words.length; i++) {
        const word = words[i]
        const equals = word.indexOf('=')
        const parameter =
            equals > 0 && [...word.slice(0, equals)].every((c) => /[A-Za-z0-9_]/.test(c))
        if (parameter && out.length > 0) out[out.length - 1] += ' ' + word
        else out.push(word)
    }
    return out
}

// `parseSpec` (src/execute/pass_spec.cpp): the name ends at the first space, the name may hold
// no `(`, and every word after it is a `key=value` word whose key is a bare name with a value.
// The word scanner is the parser's: a quote is part of the word it delimits, so `stats=""` is a
// parameter whose value is the empty string literal, not a parameter with no value.
function readSpec(spec) {
    const first = spec.search(/\S/)
    if (first < 0) return 'the pass specification is empty'
    let nameEnd = first
    while (nameEnd < spec.length && !/\s/.test(spec[nameEnd])) nameEnd++
    const name = spec.slice(first, nameEnd)
    if (name.includes('(')) {
        return `'${name}': a parameter is written key=value after the pass name, not in parentheses`
    }
    let cursor = nameEnd
    for (;;) {
        while (cursor < spec.length && /\s/.test(spec[cursor])) cursor++
        if (cursor >= spec.length) return null
        let word = ''
        let quote = 0
        while (cursor < spec.length) {
            const ch = spec[cursor]
            if (quote) {
                word += ch
                cursor++
                if (ch === '\\' && cursor < spec.length) word += spec[cursor++]
                else if (ch === quote) quote = 0
                continue
            }
            if (ch === '"' || ch === "'") {
                quote = ch
                word += ch
                cursor++
                continue
            }
            if (/\s/.test(ch)) break
            word += ch
            cursor++
        }
        if (quote) return `'${word}': the string is not closed`
        const equals = word.indexOf('=')
        if (equals < 0) {
            if (word.includes(':') && !word.includes('::')) {
                return `'${word}': a pass parameter is written key=value on the command line; key: value is the spelling of a named argument in Camel source`
            }
            return `'${word}' is not a parameter: a parameter is written key=value`
        }
        const key = word.slice(0, equals)
        if (key === '' || !/^[A-Za-z0-9_]+$/.test(key)) return `'${key}' is not a parameter name`
        if (word.slice(equals + 1) === '') return `the parameter '${key}' has no value`
    }
}

function isInvocationName(word) {
    if (knownName(word)) return true
    return QUALIFIED.test(word) || word.includes('(')
}

// `safe_fp:` is the colon spelling the command line refuses; `true` after a pass name is the
// positional parameter it refuses. Both are exact shapes, so neither can catch a redirection.
const COLON_PARAMETER = /^[A-Za-z0-9_]+:$/
const BARE_LITERAL = /^(?:true|false|-?\d+(?:\.\d+)?)$/

// ---- Finding the command lines on a page ---------------------------------------------------

const FENCE = /^(\s*)(```+|~~~+)\s*([A-Za-z0-9_+#-]*)(.*)$/
const SPAN = /`([^`\n]+)`/g
const CAMEL_COMMAND = /^(?:[^\s]*[\\/])?camel(?:\.exe)?$/

function commandLines(text) {
    const lines = text.split(/\r?\n/)
    const found = []
    let insideFence = false
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i]
        const fence = FENCE.exec(line)
        if (fence) {
            insideFence = !insideFence
            continue
        }
        if (insideFence) {
            const stripped = line
                .replace(/^\s*(?:\/\/|#)\s*/, '')
                .replace(/\s+#.*$/, '')
                .trim()
            if (CAMEL_COMMAND.test(shellSplit(stripped)[0] || '')) {
                // A command continued on the next line with a trailing backslash.
                let whole = stripped
                let j = i
                while (/\\$/.test(whole) && j + 1 < lines.length) {
                    whole = whole.replace(/\\$/, '') + ' ' + lines[++j].trim()
                }
                found.push({ line: i + 1, text: whole })
            }
        }
        for (const span of line.matchAll(SPAN)) {
            const words = shellSplit(span[1])
            if (words.length > 0 && CAMEL_COMMAND.test(words[0])) {
                found.push({ line: i + 1, text: span[1] })
            }
        }
    }
    return found
}

// The specifications of one command line: the program and the options are not passes, and
// `groupPassWords` decides where one specification ends (`--passes` is a list of its own).
function specsOfCommand(text) {
    const tokens = shellSplit(text)
    const specs = []
    let i = 1 // the program name of the command
    let file = null
    while (i < tokens.length) {
        const token = tokens[i]
        if (token === '--passes') {
            const list = tokens[i + 1] ?? ''
            specs.push(...groupPassWords(splitPassList(list), 0))
            i += 2
            continue
        }
        if (['--arg', '-I', '--include', '--input', '--timeit'].includes(token)) {
            if (token === '--input') file = tokens[i + 1] ?? null
            i += 2
            continue
        }
        if (/^--(?:arg|include|input)=/.test(token)) {
            if (token.startsWith('--input=')) file = token.slice('--input='.length)
            i++
            continue
        }
        if (token.startsWith('-')) {
            i++
            // A value that is not a specification and does not look like one belongs to the option.
            if (i < tokens.length && !tokens[i].startsWith('-') && !isInvocationName(tokens[i])) i++
            continue
        }
        if (file === null && /\.cml$/i.test(token)) {
            file = token
            i++
            continue
        }
        // Every remaining word is grouped the way the command line groups it: the first word
        // that is not a `key=value` parameter opens a specification.
        specs.push(...groupPassWords(tokens, i))
        break
    }
    return specs
}

// ---- Scanning the pages --------------------------------------------------------------------

const files = []
for (const abs of walk(REPO_ROOT)) {
    const rel = relative(abs)
    if (NOT_A_PAGE.some((entry) => rel === entry.replace(/\/$/, '') || rel.startsWith(entry))) continue
    if (GENERATED.includes(rel)) continue
    const page =
        rel.endsWith('.md') ||
        (rel.startsWith('stdlib/') && rel.endsWith('.cml')) ||
        (rel.startsWith('test/plans/') && rel.endsWith('.toml'))
    if (page) files.push(rel)
}
files.sort()

if (files.length === 0) {
    console.log('no reader-facing page in the tree: nothing was checked')
    process.exit(2)
}

// In a test plan `args` is a pin, not a page: a refusal test has to contain the spelling it
// pins. The lines of an `args` array are therefore not read for the removed form.
function pinnedLines(lines) {
    const pinned = new Set()
    let inArgs = false
    lines.forEach((line, index) => {
        if (!inArgs && /^\s*args\s*=/.test(line)) {
            inArgs = true
            pinned.add(index)
            if (/\]/.test(line.replace(/^\s*args\s*=/, ''))) inArgs = false
            return
        }
        if (inArgs) {
            pinned.add(index)
            if (/^\s*\]/.test(line)) inArgs = false
        }
    })
    return pinned
}

for (const rel of files) {
    const text = read(rel)
    const lines = text.split(/\r?\n/)
    pageLines.set(rel, lines)
    const pinned = rel.startsWith('test/plans/') ? pinnedLines(lines) : new Set()

    // A pass name immediately followed by `(` is the deleted spelling wherever it is written.
    lines.forEach((line, index) => {
        if (pinned.has(index)) return
        if (!PAREN_FORM.test(line)) return
        report(
            rel,
            index + 1,
            `a pass name followed by '(' is the removed form: ${line.trim().slice(0, 160)}`
        )
    })

    // A `camel …` command line is an invocation a reader copies and runs. Every word after the
    // program is a pass specification to the command line, so a word that is not a pass name is
    // reported when it is unmistakably a parameter: `key:` is the colon spelling and a bare
    // literal is a positional parameter. A word that is neither (`<file>`, `2>/dev/null`) is
    // left alone: it cannot be told apart from a placeholder or a redirection.
    for (const command of commandLines(text)) {
        let afterAPass = false
        for (const spec of specsOfCommand(command.text)) {
            const name = shellSplit(spec)[0] ?? ''
            if (isInvocationName(name)) {
                invocations.push({ rel, line: command.line, spec })
                afterAPass = true
                continue
            }
            if (COLON_PARAMETER.test(spec)) {
                report(
                    rel,
                    command.line,
                    `'${spec}' is the removed \`key: value\` spelling: a pass parameter is ` +
                        `written key=value on the command line`
                )
            } else if (afterAPass && BARE_LITERAL.test(spec)) {
                report(
                    rel,
                    command.line,
                    `'${spec}' is the removed positional parameter: a parameter is written ` +
                        `key=value after the pass name`
                )
            }
            afterAPass = false
        }
    }
}

// The parser's own rule, applied to every invocation the pages show.
const seen = new Map()
for (const { rel, line, spec } of invocations) {
    const refusal = readSpec(spec)
    if (refusal) report(rel, line, `the command line refuses '${spec}': ${refusal}`)
    if (!seen.has(spec)) seen.set(spec, [])
    seen.get(spec).push({ rel, line })
    if (LIST) console.log(`invocation ${rel}:${line}: ${spec}`)
}
const distinct = [...seen.entries()].map(([spec, occurrences]) => ({ spec, occurrences }))

// ---- The real parser ----------------------------------------------------------------------

function runCamel(args, cwd) {
    return new Promise((resolve) => {
        const child = spawn(CAMEL, args, { cwd, stdio: ['ignore', 'pipe', 'pipe'] })
        let output = ''
        let timedOut = false
        const timer = setTimeout(() => {
            timedOut = true
            child.kill('SIGKILL')
        }, TIMEOUT_MS)
        child.stdout.on('data', (chunk) => (output += chunk))
        child.stderr.on('data', (chunk) => (output += chunk))
        child.on('error', (error) => {
            clearTimeout(timer)
            resolve({ failed: true, output: output + error.message, timedOut })
        })
        child.on('close', () => {
            clearTimeout(timer)
            resolve({ failed: false, output, timedOut })
        })
    })
}

if (!fs.existsSync(CAMEL)) {
    for (const problem of problems) console.log(`FAIL ${problem}`)
    console.log(
        `no camel binary at ${CAMEL}: the ${distinct.length} invocation(s) were not run through ` +
            `the parser; refusing to report a pass`
    )
    process.exit(2)
}

const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-doc-pass-form-'))
const program = path.join(tempDir, 'entry.cml')
fs.writeFileSync(program, 'module doc_form_probe\n\nfunc main(): int sync {\n    return 0\n}\n')

// The pipeline is resolved and refused before any pass runs, so the invocation never executes.
// Reaching the probe means the parser read the specification; a name this build does not
// register is refused as unknown, and a parameter the declaration does not accept is one of the
// `SemanticDiag::ParamArgument*` diagnostics — neither is a spelling problem.
const RESOLVED =
    /UnrecognizedGraphPass|PassNotProvidedByModule|PassNotExportedByModule|ParamArgumentUnknown|ParamArgumentDuplicate|ParamArgumentWrongType|ParamArgumentMissing|ParamArgumentValue/

// Every page that shows the specification is named, so one defect is not reported once and
// hidden on the other pages.
let parsed = 0
let cursor = 0
async function worker() {
    while (cursor < distinct.length) {
        const { spec, occurrences } = distinct[cursor++]
        const result = await runCamel([program, spec, PROBE], tempDir)
        if (result.timedOut) {
            for (const { rel, line } of occurrences) {
                report(rel, line, `'${spec}' did not come back in ${TIMEOUT_MS} ms`)
            }
            continue
        }
        const complaint = result.output
            .split(/\r?\n/)
            .map((text) => text.trim())
            .find((text) => text.includes('InvalidPassParameter') || text.includes('not a constant'))
        if (/InvalidPassParameter/.test(result.output)) {
            for (const { rel, line } of occurrences) {
                report(rel, line, `the parser refuses '${spec}': ${(complaint || '').slice(0, 200)}`)
            }
            continue
        }
        // The specification was read and the pipeline went on to the probe: either the probe's
        // unknown name, or the pass name itself when this build does not register it.
        if (!RESOLVED.test(result.output)) {
            const seenOutput = result.output.trim().split(/\r?\n/).slice(0, 2).join(' / ')
            for (const { rel, line } of occurrences) {
                report(
                    rel,
                    line,
                    `'${spec}' neither parsed nor reached the probe: ${seenOutput.slice(0, 200)}`
                )
            }
            continue
        }
        parsed++
    }
}
try {
    await Promise.all(Array.from({ length: Math.min(JOBS, distinct.length || 1) }, worker))
} finally {
    fs.rmSync(tempDir, { recursive: true, force: true })
}

// An exception that no longer excuses anything is a sentence that changed: report it, so the
// list cannot outlive what it was written for.
let excused = 0
for (const entry of EXCUSED) {
    const used = excusedUsed.get(entry) || 0
    excused += used
    if (used === 0) {
        problems.push(
            `${entry.file}: the exception '${entry.reason}' no longer matches a line; the ` +
                `sentence it excuses was rewritten or removed`
        )
    }
}

if (invocations.length === 0) {
    console.log(
        `no pass invocation in ${files.length} reader-facing page(s): nothing was checked; the ` +
            `extraction stopped matching the pages`
    )
    process.exit(2)
}

for (const problem of problems) console.log(`FAIL ${problem}`)
if (problems.length > 0) {
    console.log(
        `FAIL the pages teach a pass spelling the command line refuses: ${problems.length} ` +
            `problem(s) in ${files.length} page(s)`
    )
    process.exit(1)
}
console.log(
    `PASS doc pass forms: ${invocations.length} pass invocation(s) in ${files.length} page(s), ` +
        `${distinct.length} distinct, each read by the parser's own rule; ${distinct.length} run ` +
        `through the real parser, ${parsed} of them reached the probe, and ${excused} page line(s) ` +
        `are excused (the form is said to be removed, or a decision and the parser disagree)`
)