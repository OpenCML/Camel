// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The generated half of docs/reference/camel-for-ai.md: the pages a reader can check against a
// normative source instead of trusting the prose.
//
//   node scripts/camel-for-ai.mjs --write          rewrite the four generated regions in place
//   node scripts/camel-for-ai.mjs --check          the file is what the sources produce, every
//                                                  diagnostic it names exists, it is within budget
//   node scripts/camel-for-ai.mjs --report         the character count of every region and of the
//                                                  hand-written part, against the budget
//   --root DIR                                     read and write under DIR (the proof cases)
//
// The reference is one file an AI can read whole (handover: "a distilled language reference for
// AI"), so it is written by hand where judgement is needed and generated where the sources are
// authoritative. A generated region is delimited by two HTML comments in the document:
//
//   <!-- camel-for-ai region: <name> (generated from <source>; node scripts/camel-for-ai.mjs --write) -->
//   <!-- camel-for-ai end: <name> -->
//
// Four regions, each with one source:
//
//   lexical             include/camel/parse/syntax/token_kinds.def -- the one keyword and
//                       punctuation table the lexer, the parser and the reference share
//   precedence          src/parse/syntax/parser_expr.cpp -- the `///  name : production` comments
//                       of the expression levels, loosest first, as the parser writes them
//   diagnostics         diagnostics/registry.tsv -- every diagnostic, its name, severity and
//                       message template
//   stdlib-signatures   docs/stdlib-reference.md -- the signatures of the standard library, which
//                       scripts/stdlib-reference.mjs generates from stdlib/*.cml and
//                       stdlib/signatures/*.cml (so the declarations stay the one source)
//
// --check refuses to report a pass over nothing: a run that read no diagnostic, no signature, no
// keyword or no precedence level fails with exit 2. It also holds the document to the rules the
// prose is written under:
//
//   * a diagnostic is named with its section (`SemanticDiag::UnresolvedReference`), and every name
//     the document uses is one the registry declares -- a renamed or deleted diagnostic cannot
//     stay in the reference;
//   * a `**target**` note names where its subject is specified or designed, as a backticked
//     `docs/...` path; a note that points nowhere is a defect;
//   * the document names no task number: the public documents describe the language, not the work
//     that produced it;
//   * the whole file and its hand-written part stay within the budget the document states.
//
// Exit status: 0 when everything agrees, 1 when a check failed, 2 when a scope is empty or the
// usage is wrong.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const DOCUMENT_REL = 'docs/reference/camel-for-ai.md'
const REGISTRY_REL = 'diagnostics/registry.tsv'
const SIGNATURES_REL = 'docs/stdlib-reference.md'
const PARSER_REL = 'src/parse/syntax/parser_expr.cpp'
const TOKENS_REL = 'include/camel/parse/syntax/token_kinds.def'

// The budget of the document, in characters. The document states these numbers itself; the check
// enforces them so the file cannot grow past what one context holds.
//
// The generated half keeps every entry of its source: 260 diagnostics and 411 library names with
// all their overloads cost 55,000 characters and are not trimmed, because a name left out is a
// name the reader has to look up elsewhere. The hand-written half is 39,000. The whole is therefore
// about 100,000 characters -- some 28,000 tokens of English prose, tables and Camel, against the
// roughly 700,000 characters of the four normative documents it distills (docs/language/,
// docs/diagnostics.md, docs/stdlib-reference.md, docs/design-principles.cn.md).
const BUDGET = {
    total: 100000,
    written: 39000,
    regions: {
        lexical: 4000,
        precedence: 3000,
        diagnostics: 27000,
        'stdlib-signatures': 27000,
    },
}

const REGIONS = ['lexical', 'precedence', 'diagnostics', 'stdlib-signatures']

const SECTIONS = [
    { id: 'internal', enumName: 'InternalDiag', title: 'Internal' },
    { id: 'lexical', enumName: 'LexicalDiag', title: 'Lexical' },
    { id: 'syntax', enumName: 'SyntaxDiag', title: 'Syntax' },
    { id: 'semantic', enumName: 'SemanticDiag', title: 'Semantic' },
    { id: 'runtime', enumName: 'RuntimeDiag', title: 'Run time and passes' },
]

function read(root, rel) {
    const file = path.join(root, rel)
    if (!fs.existsSync(file)) return null
    return fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n')
}

// ---- lexical ---------------------------------------------------------------------------------

// The keyword table of token_kinds.def, in the order the file declares it, plus the punctuation.
// A keyword's third field is the row of the language reference's table (2.4); this page keeps the
// file's own group name, because the file is what the compiler reads.
function generateLexical(root) {
    const text = read(root, TOKENS_REL)
    if (!text) return { text: null, count: 0, problem: `${TOKENS_REL} does not exist` }
    const keywords = []
    const groups = []
    for (const match of text.matchAll(/^CAMEL_KEYWORD\((\w+), "([^"]+)", (\w+)\)/gm)) {
        const group = match[3]
        if (!groups.includes(group)) groups.push(group)
        keywords.push({ word: match[2], group })
    }
    const punctuation = [...text.matchAll(/^CAMEL_PUNCT\((\w+), "([^"]+)"\)/gm)].map((m) => m[2])
    if (keywords.length === 0 || punctuation.length === 0) {
        return { text: null, count: 0, problem: `${TOKENS_REL} declares no keyword or no punctuation` }
    }
    const out = []
    out.push('| Group | Words |')
    out.push('|---|---|')
    for (const group of groups) {
        const words = keywords.filter((k) => k.group === group).map((k) => `\`${k.word}\``)
        out.push(`| ${group} | ${words.join(' ')} |`)
    }
    out.push('')
    out.push(`**Operators and separators** (${punctuation.length}): ${punctuation.map((p) => `\`${p}\``).join(' ')}`)
    return { text: out.join('\n'), count: keywords.length }
}

// ---- precedence ------------------------------------------------------------------------------

// The expression levels, from the productions the parser writes above its own functions. The chain
// is the run of productions from `dataExpr` (the loosest) to `annoExpr` (the tightest level that
// still has a production of its own); `accessExpr` and `primaryData` are the two below it.
//
// A production is one comment block; it is continued while the parentheses it opens are still
// open, so a production written over two lines is read whole and the prose that follows a
// production is not read as part of it.
function readProductions(root) {
    const text = read(root, PARSER_REL)
    if (!text) return { productions: [], problem: `${PARSER_REL} does not exist` }
    const lines = text.split('\n')
    const productions = []
    for (let i = 0; i < lines.length; i++) {
        const match = /^\/\/\/ (\w+) : (.*)$/.exec(lines[i])
        if (!match) continue
        let body = match[2]
        let open = balance(body)
        let j = i + 1
        while (open > 0 && j < lines.length && /^\/\/\//.test(lines[j])) {
            const more = lines[j].replace(/^\/\/\/ ?/, '')
            body += ` ${more.trim()}`
            open += balance(more)
            j++
        }
        while (body.endsWith(')') === false && /\s{2,}/.test(body)) body = body.replace(/\s{2,}/g, ' ')
        productions.push({ name: match[1], body: body.replace(/\s+/g, ' ').trim(), line: i + 1 })
    }
    return { productions }
}

function balance(text) {
    let open = 0
    for (const ch of text) {
        if (ch === '(' || ch === '[' || ch === '{') open++
        else if (ch === ')' || ch === ']' || ch === '}') open--
    }
    return open
}

// `repeat, left` when the production loops over its own operator (`... )*`), `right, one` when it
// names itself after the operator, `one` when the operator may appear once, `prefix` when the level
// is a prefix operator. Anything else is left as `-` rather than guessed at: the production is
// printed below the table, so a reader can check the shape against it.
function shapeOf(production) {
    const { name, body } = production
    const after = body.slice(body.indexOf(':') + 1).trim()
    const selfRecursive = new RegExp(`\\b${name}\\b`).test(after)
    const repeated = /\)\s*\*/.test(after)
    const optional = /\)\s*\?/.test(after)
    if (repeated) return 'repeat, left'
    if (after.startsWith("'")) return /^'[^']*'\s*\?/.test(after) ? 'prefix, optional' : 'construct'
    if (after.startsWith('(') && selfRecursive) return 'prefix, right'
    if (after.startsWith('(')) return 'prefix'
    if (optional && selfRecursive) return 'right, one'
    if (optional && /\bdataExpr\b/.test(after)) return 'right (a whole expression on the right)'
    if (optional) return 'one'
    return '-'
}

function operatorsOf(body) {
    const after = body.slice(body.indexOf(':') + 1)
    return [...after.matchAll(/'([^']+)'/g)].map((m) => m[1])
}

function generatePrecedence(root) {
    const { productions, problem } = readProductions(root)
    if (problem) return { text: null, count: 0, problem }
    const first = productions.findIndex((p) => p.name === 'dataExpr')
    const last = productions.findIndex((p) => p.name === 'annoExpr')
    if (first < 0 || last < first) {
        return { text: null, count: 0, problem: `${PARSER_REL}: the production of dataExpr or annoExpr is missing` }
    }
    const chain = productions.slice(first, last + 1)
    if (chain.length < 10) {
        return { text: null, count: 0, problem: `${PARSER_REL}: ${chain.length} expression level(s) read; the chain is short` }
    }
    const width = Math.max(...chain.map((p) => p.name.length))
    const out = []
    out.push('```text')
    chain.forEach((production, index) => {
        const level = String(index + 1).padStart(2)
        const ops = operatorsOf(production.body)
        const shape = shapeOf(production)
        out.push(
            `${level}  ${production.name.padEnd(width)}  ${(ops.join(' ') || '-').padEnd(34)} ${shape}`,
        )
    })
    out.push('```')
    out.push('')
    out.push('The production of each level, as `src/parse/syntax/parser_expr.cpp` writes it above the')
    out.push('function that parses it:')
    out.push('')
    out.push('```text')
    for (const production of chain) out.push(`${production.name.padEnd(width)} : ${production.body.slice(production.body.indexOf(':') + 1).trim()}`)
    out.push('```')
    return { text: out.join('\n'), count: chain.length }
}

// ---- diagnostics ------------------------------------------------------------------------------

function normalizeMessage(section, message) {
    const text = section === 'runtime' ? message : message.replaceAll('{{', '{').replaceAll('}}', '}')
    return text.replaceAll('|', '\\|')
}

function generateDiagnostics(root) {
    const text = read(root, REGISTRY_REL)
    if (!text) return { text: null, count: 0, problem: `${REGISTRY_REL} does not exist` }
    const records = []
    const problems = []
    const lines = text.split('\n')
    for (let i = 0; i < lines.length; i++) {
        const raw = lines[i]
        if (!raw.trim() || raw.startsWith('#')) continue
        const fields = raw.split('\t')
        if (fields.length < 5) {
            problems.push(`${REGISTRY_REL}:${i + 1}: fewer than five fields`)
            continue
        }
        const section = SECTIONS.find((s) => s.id === fields[0])
        if (!section) {
            problems.push(`${REGISTRY_REL}:${i + 1}: '${fields[0]}' is not a section`)
            continue
        }
        records.push({
            section: section.id,
            fullName: `${section.enumName}::${fields[1]}`,
            severity: fields[3] === 'warn' ? 'W' : 'E',
            message: normalizeMessage(section.id, fields[4]),
        })
    }
    if (problems.length > 0) return { text: null, count: 0, problem: problems.join('; ') }
    if (records.length === 0) return { text: null, count: 0, problem: `${REGISTRY_REL} declares no diagnostic` }
    const out = []
    out.push('| Name | Sev | What the compiler says |')
    out.push('|---|---|---|')
    let current = null
    for (const record of records) {
        if (record.section !== current) {
            current = record.section
            const section = SECTIONS.find((s) => s.id === current)
            const of = records.filter((r) => r.section === current)
            const errors = of.filter((r) => r.severity === 'E').length
            out.push('')
            out.push(
                `**${section.title}** (\`${section.enumName}\`) — ${of.length} ` +
                    `(${errors} error${errors === 1 ? '' : 's'}, ${of.length - errors} warning${of.length - errors === 1 ? '' : 's'})`,
            )
            out.push('')
        }
        out.push(`| \`${record.fullName.split('::')[1]}\` | ${record.severity} | ${record.message} |`)
    }
    return { text: out.join('\n'), count: records.length, names: records.map((r) => r.fullName) }
}

// ---- standard library -------------------------------------------------------------------------

// The signatures come from docs/stdlib-reference.md, which scripts/stdlib-reference.mjs generates
// from the declarations; the declarations therefore stay the one source, and a signature that
// changes there changes here on the next --write.
function generateStdlib(root) {
    const text = read(root, SIGNATURES_REL)
    if (!text) return { text: null, count: 0, problem: `${SIGNATURES_REL} does not exist` }
    const lines = text.split('\n')
    const modules = []
    let module = null
    let name = null
    for (let i = 0; i < lines.length; i++) {
        const heading = /^## `?([^`]+?)`?(?:: .*)?$/.exec(lines[i])
        if (heading) {
            module = { name: heading[1].trim(), entries: [] }
            modules.push(module)
            continue
        }
        const entry = /^#### `(.+)`$/.exec(lines[i])
        if (entry && module) {
            name = entry[1]
            if (lines[i + 1] !== '' || !/^```camel$/.test(lines[i + 2])) {
                return { text: null, count: 0, problem: `${SIGNATURES_REL}:${i + 1}: '${name}' is not followed by a camel fence` }
            }
            const signatures = []
            let j = i + 3
            while (j < lines.length && !/^```$/.test(lines[j])) {
                if (lines[j].includes('```')) {
                    return { text: null, count: 0, problem: `${SIGNATURES_REL}:${j + 1}: a signature would close its fence` }
                }
                signatures.push(lines[j])
                j++
            }
            module.entries.push({ name, signatures })
            continue
        }
    }
    const withEntries = modules.filter((m) => m.entries.length > 0)
    const count = withEntries.reduce((n, m) => n + m.entries.length, 0)
    if (count === 0) return { text: null, count: 0, problem: `${SIGNATURES_REL}: no signature was read` }
    const out = []
    for (const m of withEntries) {
        const importLine = m.name === 'built-in' || m.name === 'Built-in functions' ? 'no import' : `import { ... } from ${m.name}`
        out.push('')
        out.push(`**\`${m.name}\`** — ${importLine} — ${m.entries.length} name(s)`)
        out.push('')
        out.push('```text')
        for (const entry of m.entries) for (const signature of entry.signatures) out.push(signature)
        out.push('```')
    }
    out.shift()
    return { text: out.join('\n'), count }
}

// ---- the document -----------------------------------------------------------------------------

function regionMarkers(name) {
    return {
        begin: new RegExp(`^<!-- camel-for-ai region: ${name}\\b.*-->$`, 'm'),
        end: new RegExp(`^<!-- camel-for-ai end: ${name} -->$`, 'm'),
    }
}

function generateAll(root) {
    return {
        lexical: generateLexical(root),
        precedence: generatePrecedence(root),
        diagnostics: generateDiagnostics(root),
        'stdlib-signatures': generateStdlib(root),
    }
}

function regionsOf(document) {
    const found = new Map()
    for (const name of REGIONS) {
        const { begin, end } = regionMarkers(name)
        const marks = [...document.matchAll(new RegExp(begin.source + '|' + end.source, 'gm'))]
        if (marks.length !== 2) {
            found.set(name, { problem: `the region '${name}' has ${marks.length} marker(s); expected a begin and an end`, text: null })
            continue
        }
        const open = document.indexOf(marks[0][0]) + marks[0][0].length
        const close = document.indexOf(marks[1][0])
        if (close < open) {
            found.set(name, { problem: `the region '${name}' ends before it begins`, text: null })
            continue
        }
        found.set(name, { text: document.slice(open, close), start: open, end: close })
    }
    return found
}

function applyRegions(document, generated) {
    let out = document
    for (const name of REGIONS) {
        const region = regionsOf(out).get(name)
        if (!region || region.problem) continue
        out = out.slice(0, region.start) + '\n' + generated[name].text + '\n' + out.slice(region.end)
    }
    return out
}

function countRegionChars(document) {
    const regions = regionsOf(document)
    let total = 0
    const per = {}
    for (const name of REGIONS) {
        const region = regions.get(name)
        const chars = region && region.text ? region.text.trim().length : 0
        per[name] = chars
        total += chars
    }
    return { per, total }
}

function budgetProblems(document) {
    const problems = []
    const { per, total } = countRegionChars(document)
    const written = document.trim().length - total
    for (const name of REGIONS) {
        const cap = BUDGET.regions[name]
        if (per[name] > cap) {
            problems.push(`the '${name}' region is ${per[name]} characters; its budget is ${cap}`)
        }
    }
    if (written > BUDGET.written) {
        problems.push(`the hand-written part is ${written} characters; its budget is ${BUDGET.written}`)
    }
    if (document.trim().length > BUDGET.total) {
        problems.push(`the document is ${document.trim().length} characters; its budget is ${BUDGET.total}`)
    }
    return { problems, per, total, written, size: document.trim().length }
}

// What the prose promises and the sources decide: a diagnostic named in the document must exist,
// and a `target` note must say where its subject is designed.
function promiseProblems(document, diagnostics, documentRel) {
    const problems = []
    const known = new Set(diagnostics.names || [])
    const seen = new Set()
    for (const match of document.matchAll(/\b(Internal|Lexical|Syntax|Semantic|Runtime)Diag::([A-Za-z_]\w*)/g)) {
        const full = `${match[1]}Diag::${match[2]}`
        if (seen.has(full)) continue
        seen.add(full)
        if (!known.has(full)) problems.push(`the document names ${full}, which ${REGISTRY_REL} does not declare`)
    }
    // A `target` note is read with the lines that continue it: prose wraps, and the path it names
    // may be on the next line of the same paragraph.
    const lines = document.split('\n')
    for (let i = 0; i < lines.length; i++) {
        if (lines[i].includes('**target**')) {
            const paragraph = [lines[i]]
            for (let j = i + 1; j < lines.length && lines[j].trim() !== '' && !/^[#|]/.test(lines[j]); j++) {
                paragraph.push(lines[j])
            }
            if (!/`docs\/[^`]+`/.test(paragraph.join(' '))) {
                problems.push(`${documentRel}:${i + 1}: a **target** note names no \`docs/...\` source`)
            }
        }
        const task = /\bH\d{2,}\b/.exec(lines[i])
        if (task) {
            problems.push(`${documentRel}:${i + 1}: the document names the task '${task[0]}'; public documents describe the language`)
        }
    }
    return { problems, cited: seen.size }
}

// ---- entry ------------------------------------------------------------------------------------

const argv = process.argv.slice(2)
let mode = '--check'
let root = REPO_ROOT
let documentRel = DOCUMENT_REL
for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--root') root = path.resolve(argv[++i] || '')
    else if (argv[i] === '--document') documentRel = argv[++i] || DOCUMENT_REL
    else mode = argv[i]
}
if (!['--check', '--write', '--report'].includes(mode)) {
    console.log('usage: node scripts/camel-for-ai.mjs --check | --write | --report [--root DIR] [--document REL]')
    process.exit(2)
}

const documentPath = path.join(root, documentRel)
if (!fs.existsSync(documentPath)) {
    console.log(`${documentRel}: no such file under ${root}`)
    process.exit(2)
}
const document = read(root, documentRel)
const generated = generateAll(root)

if (mode === '--report') {
    const { problems, per, total, written, size } = budgetProblems(document)
    console.log(`document       ${String(size).padStart(7)}  budget ${BUDGET.total}`)
    console.log(`hand-written   ${String(written).padStart(7)}  budget ${BUDGET.written}`)
    for (const name of REGIONS) {
        console.log(`${name.padEnd(14)} ${String(per[name]).padStart(7)}  budget ${BUDGET.regions[name]}`)
    }
    for (const name of REGIONS) {
        const region = generated[name]
        console.log(`${name.padEnd(14)} source entries: ${region.text === null ? 'none (' + region.problem + ')' : region.count}`)
    }
    for (const problem of problems) console.log(`OVER BUDGET: ${problem}`)
    process.exit(problems.length > 0 ? 1 : 0)
}

if (mode === '--write') {
    const missing = REGIONS.filter((name) => generated[name].text === null)
    if (missing.length > 0) {
        for (const name of missing) console.log(`${name}: ${generated[name].problem}`)
        console.log(`${missing.length} region(s) could not be generated; nothing was written`)
        process.exit(1)
    }
    const next = applyRegions(document, generated)
    if (next === document) {
        console.log(`${documentRel} is already what the sources produce (nothing written)`)
        process.exit(0)
    }
    fs.writeFileSync(documentPath, next)
    const { total, written, size } = budgetProblems(next)
    console.log(
        `wrote ${documentRel}: ${REGIONS.length} region(s), ${total} generated characters, ` +
            `${written} hand-written, ${size} in total`,
    )
    process.exit(0)
}

// --check
const problems = []
let checked = 0
for (const name of REGIONS) {
    const region = regionsOf(document).get(name)
    if (!region || region.problem) {
        problems.push(region ? region.problem : `the region '${name}' is missing`)
        continue
    }
    const source = generated[name]
    if (source.text === null) {
        problems.push(`the '${name}' region could not be read from its source: ${source.problem}`)
        continue
    }
    if (region.text.trim() !== source.text.trim()) {
        problems.push(
            `the '${name}' region is not what its source produces (${source.count} entries); ` +
                'run `node scripts/camel-for-ai.mjs --write`',
        )
        continue
    }
    checked += source.count
}
if (checked === 0) {
    console.log(`no generated region of ${documentRel} was verified against a source: nothing was checked`)
    process.exit(2)
}
const diagnostics = generated.diagnostics
if (diagnostics.text === null) {
    problems.push(`the diagnostic names could not be read: ${diagnostics.problem}`)
} else {
    const promise = promiseProblems(document, diagnostics, documentRel)
    problems.push(...promise.problems)
}
const budget = budgetProblems(document)
problems.push(...budget.problems)

for (const problem of problems) console.log(problem)
if (problems.length > 0) {
    console.log(`${problems.length} problem(s) in ${documentRel}`)
    process.exit(1)
}
const promises = promiseProblems(document, diagnostics, documentRel)
console.log(
    `${documentRel} is what its sources produce: ${REGIONS.length} generated region(s), ${checked} entries, ` +
        `${diagnostics.count} diagnostics, ${promises.cited} diagnostic name(s) cited by the prose, ` +
        `${budget.size} characters (budget ${BUDGET.total})`,
)