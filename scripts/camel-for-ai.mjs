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
//   --camel PATH                                   the binary the counter-examples are compiled
//                                                  with, for --verify-errors; this tree's
//                                                  `out/latest/bin/camel` without it
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
//   diagnostics         diagnostics/registry.tsv -- every diagnostic, its name and message
//                       template; the severity is the heading of the block the row is in
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
import os from 'node:os'
import { spawn } from 'node:child_process'
import { defaultCamel, takeCamel } from '../test/tools/camel-arg.mjs'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const DOCUMENT_REL = 'docs/reference/camel-for-ai.md'
const REGISTRY_REL = 'diagnostics/registry.tsv'
const SIGNATURES_REL = 'docs/stdlib-reference.md'
const LEXICAL_DOC_REL = 'docs/language/02_lexical.md'
const PARSER_REL = 'src/parse/syntax/parser_expr.cpp'
const TOKENS_REL = 'include/camel/parse/syntax/token_kinds.def'

// The budget of the document, in characters. The document states these numbers itself; the check
// enforces them so the file cannot grow past what one context holds.
//
// The generated half keeps every entry of its source: 267 diagnostics and 421 library names with
// all their overloads cost about 55,700 characters and are not trimmed, because a name left out is
// a name the reader has to look up elsewhere. The hand-written half is 55,400: it was 55,000, and
// a round that closed measured gaps in it -- `loop` and `rule` had no entry, §11 taught a `for`
// that is not a statement, §12.13 taught a `var` spelling the compiler warns about, and §12.2
// pointed a `Tensor` parameter at a type the tensor operators do not take -- and a later round
// added the `static loop` entry §6 lacked. The whole is therefore about 112,000 characters -- some
// 30,000 tokens of English prose, tables and Camel, against the roughly 700,000 characters of the
// four normative documents it distills (docs/language/, docs/diagnostics.md,
// docs/stdlib-reference.md, docs/design-principles.cn.md).
//
// These numbers do not rise with the diagnostics: the file is read whole, so its budget is the
// context one read costs, and a language whose diagnostics keep arriving would keep pushing a
// growing budget ahead of itself. So the lane that adds a diagnostic owns keeping the whole file
// inside them -- it either fits the room the file has, or it tightens the wording around the entry
// it adds, the way a rule is stated once and a generated region carries a field only if no other
// part of the file already says it. When the only way to make room would be to drop something a
// reader needs, that is not a call for the lane that hit it: it opens a question page under
// docs/handover/questions/ instead of deleting the information or editing a number here.
//
// Nothing in this check warns that the room is nearly gone. Over budget is a failure and the merge
// gate reports it; a warning that is not a failure is a warning readers learn to scroll past.
const BUDGET = {
    total: 112000,
    written: 55400,
    regions: {
        lexical: 4000,
        precedence: 3000,
        diagnostics: 28000,
        'stdlib-signatures': 28000,
    },
}

// Each chapter's own budget, read out of the table the document carries ("What each part costs"):
// the number in the last cell of a row whose first cell starts with the chapter's number. The table
// is therefore not a comment on the file, it is the file's promise about itself, and a chapter that
// grows past its row is a problem. A row for a chapter the document does not have is a problem too.
const BUDGET_ROW = /^\|\s*(\d{1,2}|Appendix [A-C])\.\s*(.+?)\s*\|[^|]*\|\s*([\d,]+)\s*\|$/

function chapterBudgets(document) {
    const rows = []
    for (const line of document.split('\n')) {
        const match = BUDGET_ROW.exec(line)
        if (match) rows.push({ key: match[1], title: match[2], budget: Number(match[3].replace(/,/g, '')) })
    }
    return rows
}

function chapterSizes(document) {
    const lines = document.split('\n')
    const sizes = new Map()
    let current = null
    let chars = 0
    const close = () => {
        if (current) sizes.set(current, chars)
    }
    for (const line of lines) {
        const heading = /^##\s+(\d{1,2}|Appendix [A-C])\./.exec(line)
        if (heading) {
            close()
            current = heading[1]
            chars = 0
            continue
        }
        if (/^##\s/.test(line)) {
            close()
            current = null
            chars = 0
            continue
        }
        chars += line.length + 1
    }
    close()
    return sizes
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
    const sizes = chapterSizes(document)
    const chapters = []
    for (const row of chapterBudgets(document)) {
        const size = sizes.get(row.key) ?? 0
        chapters.push({ ...row, size })
        if (size > row.budget) {
            problems.push(
                `the chapter '${row.key}. ${row.title}' is ${size} characters; the budget it states is ${row.budget}`,
            )
        }
    }
    return { problems, per, total, written, size: document.trim().length, chapters }
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

// The keyword table of `docs/language/02_lexical.md` §2.4 -- the page the token table's own comment
// names as the source of its `Group` field -- plus the operators of `token_kinds.def`.
//
// The two files disagree about one word: `enum` is grouped under `Type` by the reference (the
// language has nominal enums; §6 and §12 use them) and under `Reserved` by the token table, which
// did not move when the language gained them. The page follows the reference and says so, and the
// note is generated, so fixing the token table removes the note by itself instead of leaving a
// stale claim behind.
const KEYWORD_GROUPS = [
    ['声明', 'Declaration'],
    ['控制', 'Control'],
    ['类型与转换', 'Type and conversion'],
    ['字面量', 'Literal'],
    ['保留，尚未实现', 'Reserved (accepted, not implemented — §13)'],
]

function generateLexical(root) {
    const tokens = read(root, TOKENS_REL)
    if (!tokens) return { text: null, count: 0, problem: `${TOKENS_REL} does not exist` }
    const reference = read(root, LEXICAL_DOC_REL)
    if (!reference) return { text: null, count: 0, problem: `${LEXICAL_DOC_REL} does not exist` }
    const declared = []
    for (const match of tokens.matchAll(/^CAMEL_KEYWORD\((\w+), "([^"]+)", (\w+)\)/gm)) {
        declared.push({ word: match[2], group: match[3] })
    }
    const punctuation = [...tokens.matchAll(/^CAMEL_PUNCT\((\w+), "([^"]+)"\)/gm)].map((m) => m[2])
    if (declared.length === 0 || punctuation.length === 0) {
        return { text: null, count: 0, problem: `${TOKENS_REL} declares no keyword or no punctuation` }
    }

    // §2.4's table, kept in the reference's order.
    const from = reference.indexOf('## 2.4')
    const to = reference.indexOf('## 2.5')
    const rows = []
    for (const line of (from < 0 || to < 0 ? '' : reference.slice(from, to)).split('\n')) {
        if (!line.startsWith('|')) continue
        const cells = line.split('|').map((cell) => cell.trim())
        if (cells.length < 4 || cells[1] === '类别') continue
        const label = KEYWORD_GROUPS.find(([zh]) => zh === cells[1])
        if (!label) continue
        const words = [...cells[2].matchAll(/`([a-z_]+)`/g)].map((m) => m[1])
        rows.push({ label: label[1], words })
    }
    if (rows.length !== KEYWORD_GROUPS.length) {
        return {
            text: null,
            count: 0,
            problem: `${LEXICAL_DOC_REL} §2.4 has ${rows.length} of the ${KEYWORD_GROUPS.length} keyword rows`,
        }
    }

    const out = []
    out.push('| Group | Words |')
    out.push('|---|---|')
    for (const row of rows) {
        out.push(`| ${row.label} | ${row.words.map((word) => `\`${word}\``).join(' ')} |`)
    }
    out.push('')
    out.push(`**Operators and separators** (${punctuation.length}): ${punctuation.map((p) => `\`${p}\``).join(' ')}`)

    // The disagreement between the two tables, derived rather than asserted: a word whose group in
    // the token table is not the reference's row is named here, with both readings.
    const groupOfRow = new Map()
    for (let i = 0; i < rows.length; i++) {
        for (const word of rows[i].words) groupOfRow.set(word, { label: rows[i].label, tokenGroup: declared.find((k) => k.word === word)?.group })
    }
    const disagreements = declared.filter((keyword) => {
        const row = groupOfRow.get(keyword.word)
        if (!row) return false
        return row.label.split(' ')[0] !== keyword.group
    })
    if (disagreements.length > 0) {
        const written = disagreements
            .map((keyword) => `\`${keyword.word}\` (token table: \`${keyword.group}\`; reference: ${groupOfRow.get(keyword.word).label})`)
            .join(', ')
        out.push('')
        out.push(`The token table \`${TOKENS_REL}\` groups ${written}. This file follows the reference.`)
    }
    const missing = declared.filter((keyword) => !groupOfRow.has(keyword.word)).map((keyword) => keyword.word)
    if (missing.length > 0) {
        out.push('')
        out.push(`The token table declares ${missing.map((word) => `\`${word}\``).join(' ')}, which §2.4 does not list.`)
    }
    return { text: out.join('\n'), count: declared.length }
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
    // A block is one section of the registry at one severity: the section is the prefix every name in
    // it carries (`SemanticDiag::`), and the severity is written in the heading, so a row is the name
    // and the message only. A reader who needs the severity reads the heading over the row, and the
    // column that repeated it on all of them is gone.
    const out = []
    out.push('| Name | What the compiler says |')
    out.push('|---|---|')
    for (const section of SECTIONS) {
        for (const severity of ['E', 'W']) {
            const of = records.filter((r) => r.section === section.id && r.severity === severity)
            if (of.length === 0) continue
            out.push('')
            out.push(
                `**${section.title} — ${severity === 'E' ? 'errors' : 'warnings'}** ` +
                    `(\`${section.enumName}\`) — ${of.length}`,
            )
            out.push('')
            for (const record of of) {
                out.push(`| \`${record.fullName.split('::')[1]}\` | ${record.message} |`)
            }
        }
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

// ---- Counter-examples -------------------------------------------------------------------------

// A `counter-example` fence is not compiled by the documentation check, on purpose: a fence that
// does not compile cannot be a snippet. That leaves the errors of the reference checked for their
// diagnostic *name* only, and the pairing "this mistake gets that diagnostic" unchecked, which is
// the part a reader relies on. `--verify-errors` closes it: each counter-example is compiled and the
// diagnostic the entry names must be the one the compiler reports.
function counterExamples(document) {
    const lines = document.split('\n')
    const found = []
    for (let i = 0; i < lines.length; i++) {
        if (!/<!--\s*camel-snippet:\s*counter-example/.test(lines[i])) continue
        let j = i - 1
        const prose = []
        let claimLine = -1
        while (j >= 0 && !/^##\s/.test(lines[j]) && i - j <= 14) {
            const matches = [...lines[j].matchAll(/\b(?:Internal|Lexical|Syntax|Semantic|Runtime)Diag::[A-Za-z_]\w*/g)]
            if (matches.length > 0 && claimLine < 0) claimLine = j
            prose.unshift(lines[j])
            j--
        }
        let k = i + 1
        while (k < lines.length && !/^```(?:camel|cml)\s*$/.test(lines[k])) k++
        const body = []
        for (let m = k + 1; m < lines.length && !/^```\s*$/.test(lines[m]); m++) body.push(lines[m])
        const claim = claimLine < 0 ? null : /\b(?:Internal|Lexical|Syntax|Semantic|Runtime)Diag::[A-Za-z_]\w*/.exec(lines[claimLine])[0]
        found.push({ line: i + 1, claim, body })
    }
    return found
}

// The binary `--verify-errors` compiles each counter-example with. `--camel <path>` names it (the
// entry parses the argument); with none it is this tree's `out/latest/bin/camel`. One rule, in one
// place: test/tools/camel-arg.mjs.
let CAMEL = defaultCamel(REPO_ROOT)

function runCamel(args, cwd) {
    return new Promise((resolve) => {
        // The plan runner sets the short diagnostic form for its own assertions; the entry names a
        // diagnostic the way the default text form prints it, so the short form is not read here.
        const env = { ...process.env }
        delete env.CAMEL_ERROR_FORMAT
        // The child reads the standard library of the install it runs in, so `CAMEL_HOME` is the
        // root of the install this check measured -- `--camel <path>`, else this tree's
        // `out/latest` -- and never an exported `CAMEL_HOME`, which would point the child at a
        // different build than the one the check ran.
        env.CAMEL_HOME = path.dirname(path.dirname(CAMEL))
        const child = spawn(CAMEL, args, { cwd, env, stdio: ['ignore', 'pipe', 'pipe'] })
        let stdout = ''
        let stderr = ''
        const timer = setTimeout(() => child.kill('SIGKILL'), 30000)
        child.stdout.on('data', (chunk) => (stdout += chunk))
        child.stderr.on('data', (chunk) => (stderr += chunk))
        child.on('error', (error) => {
            clearTimeout(timer)
            resolve({ code: -1, output: String(error.message) })
        })
        child.on('close', (code) => {
            clearTimeout(timer)
            resolve({ code, output: `${stdout}\n${stderr}` })
        })
    })
}

async function verifyErrors(document, documentRel) {
    const examples = counterExamples(document)
    if (examples.length === 0) {
        console.log(`${documentRel} carries no counter-example to verify: nothing was checked`)
        return 2
    }
    if (!fs.existsSync(CAMEL)) {
        console.log(`no compiler at ${CAMEL}: the counter-examples cannot be verified (build first, or pass --camel <path>)`)
        return 2
    }
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-for-ai-errors-'))
    const problems = []
    let verified = 0
    for (let index = 0; index < examples.length; index++) {
        const example = examples[index]
        const file = path.join(dir, `example_${index + 1}.cml`)
        fs.writeFileSync(file, example.body.join('\n') + '\n')
        const ran = await runCamel([file, 'std::gir'], dir)
        const reported = [...ran.output.matchAll(/^(?:error|warning)\[([^\]]+)\]/gm)].map((match) => match[1])
        if (example.claim === null) {
            problems.push(`${documentRel}:${example.line}: the counter-example names no diagnostic (the compiler reports ${reported.join(', ') || 'nothing'})`)
            continue
        }
        if (!reported.includes(example.claim)) {
            problems.push(
                `${documentRel}:${example.line}: the entry promises ${example.claim}; the compiler reports ` +
                    `${reported.join(', ') || 'no diagnostic'}`,
            )
            continue
        }
        verified++
    }
    for (const problem of problems) console.log(problem)
    if (problems.length > 0) {
        console.log(`${problems.length} counter-example(s) do not report what the entry promises`)
        return 1
    }
    console.log(
        `${documentRel} promises what the compiler answers: ${verified} counter-example(s) compiled, ` +
            `each reporting the diagnostic its entry names`,
    )
    return 0
}

// ---- entry ------------------------------------------------------------------------------------

function usage(message) {
    if (message) console.log(`camel-for-ai: ${message}`)
    console.log(
        'usage: node scripts/camel-for-ai.mjs --check | --write | --report | --verify-errors ' +
            '[--root DIR] [--document REL] [--camel PATH]',
    )
    process.exit(2)
}

const { camel, rest: argv } = takeCamel(process.argv.slice(2), REPO_ROOT, usage)
CAMEL = camel
let mode = '--check'
let root = REPO_ROOT
let documentRel = DOCUMENT_REL
for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--root') root = path.resolve(argv[++i] || '')
    else if (argv[i] === '--document') documentRel = argv[++i] || DOCUMENT_REL
    else mode = argv[i]
}
if (!['--check', '--write', '--report', '--verify-errors'].includes(mode)) {
    usage(`unknown mode "${mode}"`)
}

const documentPath = path.join(root, documentRel)
if (!fs.existsSync(documentPath)) {
    console.log(`${documentRel}: no such file under ${root}`)
    process.exit(2)
}
const document = read(root, documentRel)
const generated = generateAll(root)

if (mode === '--verify-errors') {
    process.exit(await verifyErrors(document, documentRel))
}

if (mode === '--report') {
    const { problems, per, total, written, size, chapters } = budgetProblems(document)
    console.log(`document       ${String(size).padStart(7)}  budget ${BUDGET.total}`)
    console.log(`hand-written   ${String(written).padStart(7)}  budget ${BUDGET.written}`)
    for (const name of REGIONS) {
        console.log(`${name.padEnd(14)} ${String(per[name]).padStart(7)}  budget ${BUDGET.regions[name]}`)
    }
    for (const name of REGIONS) {
        const region = generated[name]
        console.log(`${name.padEnd(14)} source entries: ${region.text === null ? 'none (' + region.problem + ')' : region.count}`)
    }
    for (const chapter of chapters) {
        console.log(
            `section ${String(chapter.key).padStart(2)}      ${String(chapter.size).padStart(7)}  budget ${chapter.budget}`,
        )
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