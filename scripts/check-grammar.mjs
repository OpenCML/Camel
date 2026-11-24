#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The grammar of Camel is written once, as the production above each function of the parser
// (parse/src/syntax/parser*.cpp, the `/// name : ...` comments). This script keeps the copies
// in step with it:
//
//   node scripts/check-grammar.mjs --write    regenerate the grammar blocks of the language
//                                             reference (docs/language/08..., both languages) and
//                                             the contextual keyword table (docs/language/02...,
//                                             section 2.4.1, both languages)
//   node scripts/check-grammar.mjs --check    exit 1 unless
//       - those blocks are what --write would produce,
//       - every file at the top of parse/src/syntax was read, and a file under that directory
//         which writes a production and is not read is a failure, named by path,
//       - every name used on a right-hand side is a production (or a token class),
//       - every quoted literal is a token of parse/include/camel/parse/syntax/token_kinds.def, or a
//         contextual keyword of scripts/context-keywords.mjs,
//       - every production has a `parse` function or is listed as folded into another one,
//       - every node kind of node_kinds.def names a production in its comment,
//       - the keyword table of the reference (02, section 2.4) lists exactly the keywords,
//       - every contextual keyword of scripts/context-keywords.mjs is still read by name in the
//         parser, and every word the parser reads with atWord is on that table.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { CONTEXT_KEYWORDS, renderContextKeywordTable } from './context-keywords.mjs'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
// Where the parse block keeps its sources and its headers is a layout, not a path: the layout map
// (docs/design/architecture/layout-map.cn.md, table B) moved `src/parse/` to `parse/src/` and
// `include/camel/parse/` to `parse/include/camel/parse/`, and the two-install comparison runs this
// script in both trees, so both spellings are resolved here and everywhere else in this file asks
// them for a directory. Which one the tree has decides; no rule below spells either.
const firstOf = (candidates) =>
    candidates.find((candidate) => fs.existsSync(candidate)) ?? candidates[0]
const PARSE_SOURCES = firstOf([
    path.join(ROOT, 'parse', 'src'),
    path.join(ROOT, 'src', 'parse'),
])
const PARSE_HEADERS = firstOf([
    path.join(ROOT, 'parse', 'include', 'camel', 'parse'),
    path.join(ROOT, 'include', 'camel', 'parse'),
])
// The grammar is read from the directory and not from a list of file names. A list is a decision
// that nothing records: a new `parser_*.cpp` beside the old ones, or a parser source that moves
// into a subdirectory, is read by nothing -- its productions are compared with nothing while every
// check below stays green, which is the defect this script exists for (the shape that
// `scripts/vjp-parity.mjs`'s ruleset directory and `test/tools/reading_refusal/check.py`'s walk of
// `lib/transforms` were fixed for). So every file at the top of the directory is read, with no
// name and no extension filter: a filter is the same silent decision, one level down.
const PARSER_DIRECTORY = path.join(PARSE_SOURCES, 'syntax')
// The directory as the reports name it, so a message reads the tree it was run in.
const PARSER_DIRECTORY_NAME = path.relative(ROOT, PARSER_DIRECTORY).split(path.sep).join('/')
// The order the productions are written in is the reading order of the reference (08), not an
// alphabetical one (`parser_type.cpp` comes before `parser_rule.cpp` there). This list decides the
// order and not the membership: a file it does not name is read too, after the named ones, so a
// new parser source appends its productions to the block instead of dropping out of it.
const GRAMMAR_ORDER = ['parser.cpp', 'parser_expr.cpp', 'parser_type.cpp', 'parser_rule.cpp']
let parserEntries
try {
    parserEntries = fs.readdirSync(PARSER_DIRECTORY, { withFileTypes: true })
} catch {
    console.error(`error: ${PARSER_DIRECTORY_NAME} is not in the tree; the grammar is read from it`)
    process.exit(1)
}
const PARSER_FILES = parserEntries
    .filter((entry) => entry.isFile())
    .map((entry) => entry.name)
    .sort((a, b) => {
        const rank = (name) => {
            const at = GRAMMAR_ORDER.indexOf(name)
            return at < 0 ? GRAMMAR_ORDER.length : at
        }
        return rank(a) - rank(b) || a.localeCompare(b)
    })
    .map((name) => path.join(PARSER_DIRECTORY, name))
// An empty read set makes every check below true of nothing.
if (PARSER_FILES.length === 0) {
    console.error(
        `error: ${PARSER_DIRECTORY_NAME} holds no file; the grammar is read from it and there is nothing to compare`
    )
    process.exit(1)
}
const TOKENS = path.join(PARSE_HEADERS, 'syntax', 'token_kinds.def')
const NODES = path.join(PARSE_HEADERS, 'syntax', 'node_kinds.def')
const DOCS = [
    path.join(ROOT, 'docs', 'language', '08_top_level_and_grammar.md'),
    path.join(ROOT, 'docs', 'language', 'en', '08_top_level_and_grammar.md'),
]
const BEGIN = '<!-- grammar:begin (generated by scripts/check-grammar.mjs --write) -->'
const END = '<!-- grammar:end -->'
// The contextual keyword table of the reference (2.4.1): one table, the same words in both
// languages, written from scripts/context-keywords.mjs.
const CONTEXT_BEGIN =
    '<!-- context-keywords:begin (generated by scripts/check-grammar.mjs --write) -->'
const CONTEXT_END = '<!-- context-keywords:end -->'
// The pages the two generated blocks live in: the grammar of chapter 8 and the lexical chapter 2.
const LEXICAL_DOCS = [
    { file: path.join(ROOT, 'docs', 'language', '02_lexical.md'), lang: 'zh' },
    { file: path.join(ROOT, 'docs', 'language', 'en', '02_lexical.md'), lang: 'en' },
]
// Token classes: terminals that are not written as a quoted literal.
// OPERATOR: any operator token (the symbol of `func operator +`, production operatorName).
const TOKEN_CLASSES = new Set(['IDENTIFIER', 'INTEGER', 'REAL', 'STRING', 'MULTI_STR', 'FSTRING', 'SEP', 'EOF', 'OPERATOR'])
// Contextual keywords: written as a word, but not reserved words, so they are not tokens of
// token_kinds.def. A production may quote one where the parser decides on the text (`tag` in
// `tag x on nodes`, `on` and `read_by` in the same production, design principles 6.2);
// `struct`, `tuple` and `leaf` in a `match type` case (they stay ordinary names elsewhere);
// `resource`, `unordered`, `readonly`, `uses` and `pure` in a resource declaration and a
// function's resource clause (design effects plan 3.1, design principles 9.12). The table lives
// in scripts/context-keywords.mjs, with the reading of the parser that each word depends on.
const CONTEXTUAL = new Set(CONTEXT_KEYWORDS.map((entry) => entry.word))
// Productions parsed inside the function of another one.
const FOLDED = {
    decl: 'parseDecl',
    namespaceItem: 'parseNamespaceDecl',
    carrier: 'parseDataDecl',
    blockStmt: 'parseStmt',
    parentIdents: 'parseBracketedIdents',
    bracedIdents: 'parseBracketedIdents',
    bracketIdents: 'parseBracketedIdents',
    dataExpr: 'parseExpr',
    arrayDimItem: 'parseArrayDim',
    ctrlExpr: 'parseExpr',
    matchTypeExpr: 'parseMatchType',
    staticForExpr: 'parseStaticFor',
    staticLoopExpr: 'parseStaticLoop',
    staticForHeader: 'parseStaticForHeader',
    loopExpr: 'parseLoop',
    namedTypeArgument: 'parseTypeArguments',
    loopState: 'parseLoop',
    loopFor: 'parseLoop',
    loopWhile: 'parseLoop',
    assignExpr: 'parseAssign',
    logicalOrExpr: 'parseLogicalOr',
    logicalAndExpr: 'parseLogicalAnd',
    equalityExpr: 'parseEquality',
    relationalExpr: 'parseRelational',
    rangeExpr: 'parseRange',
    additiveExpr: 'parseAdditive',
    multiplicativeExpr: 'parseMultiplicative',
    powerExpr: 'parsePower',
    orExpr: 'parseOr',
    andExpr: 'parseAnd',
    negExpr: 'parseNeg',
    negPower: 'parseNeg',
    nullableExpr: 'parseNullable',
    unaryExpr: 'parseUnary',
    linkExpr: 'parseLink',
    annoExpr: 'parseAnno',
    accessExpr: 'parseAccess',
    memberAccess: 'parseAnno',
    primaryData: 'parsePrimary',
    fieldPattern: 'parsePattern',
    indexValues: 'parseArguments',
    argumentList: 'parseArguments',
    typeExpr: 'parseType',
    typeOrData: 'parseSpecType',
    tupleType: 'parsePrimaryType',
    keyTypePair: 'parseStructType',
}

const errors = []

/** A text file with LF line ends, also in a checkout that has CRLF (git's core.autocrlf). */
function readText(file) {
    return fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n')
}

// The marker of a production: `/// name : ...` at the start of a line. The coverage check below
// reads this same marker, and deliberately not a bare `///`: `parser_impl.h`, `lexer.cpp` and
// `report.cpp` carry more than a hundred prose `///` lines between them and no production, so a
// check keyed on `///` would be red on the day it was written and would say nothing about a parser
// source that was really missed.
const PRODUCTION = /^\/\/\/ ([a-z][A-Za-z]*)\s*: (.*)$/

/** The productions of the parser sources, in order: [{name, lines, file}]. */
function readProductions() {
    const productions = []
    for (const file of PARSER_FILES) {
        const lines = readText(file).split('\n')
        let current = null
        for (const line of lines) {
            const start = PRODUCTION.exec(line)
            const more = /^\/\/\/ {2,}(\S.*)$/.exec(line)
            if (start) {
                current = { name: start[1], lines: [start[2].trim()], file }
                productions.push(current)
            } else if (more && current) {
                current.lines.push(more[1].trim())
            } else {
                current = null
            }
        }
    }
    return productions
}

/// The files under `parse/src/syntax` that write a production and are not in the read set.
///
/// The read set is the top of the directory, so a parser source that moves into a subdirectory
/// stops being read while every check stays green -- the same defect, one level down, and the one
/// this check was fixed for. A file is recognised by what it says (the `PRODUCTION` marker, and not
/// a bare `///`: three files of that directory hold prose doc comments and no production), and
/// every stray is named: a red that says only how many places were missed leaves the reader to work
/// out which, which is the work this check exists to do.
function unreadSources() {
    const readSet = new Set(PARSER_FILES)
    const stray = []
    const walk = (directory) => {
        let entries
        try {
            entries = fs.readdirSync(directory, { withFileTypes: true })
        } catch {
            return
        }
        for (const entry of entries) {
            const full = path.join(directory, entry.name)
            if (entry.isDirectory()) {
                walk(full)
                continue
            }
            if (readSet.has(full)) {
                continue
            }
            if (readText(full).split('\n').some((line) => PRODUCTION.test(line))) {
                stray.push(path.relative(ROOT, full).split(path.sep).join('/'))
            }
        }
    }
    walk(PARSER_DIRECTORY)
    return [...new Set(stray)].sort()
}

function render(productions) {
    const width = Math.max(...productions.map((p) => p.name.length))
    const out = []
    for (const p of productions) {
        out.push(`${p.name.padEnd(width)} : ${p.lines[0]}`)
        for (const line of p.lines.slice(1)) {
            out.push(`${' '.repeat(width)}   ${line}`)
        }
    }
    return out.join('\n')
}

const productions = readProductions()
// A parser refactor that drops the `/// name : ...` comments would leave every check below
// looping over nothing; the count is what makes that a failure instead of a pass.
if (productions.length === 0) {
    console.error('error: the parser sources hold no `/// name : ...` production; the guard checked nothing')
    process.exit(1)
}
const names = new Set()
for (const p of productions) {
    if (names.has(p.name)) errors.push(`production '${p.name}' is written twice`)
    names.add(p.name)
}

// A file under the directory that writes a production and is not read is a place this check
// stopped looking at, whoever moved it there. Named one per line, because the fix is to read it.
for (const unread of unreadSources()) {
    errors.push(
        `${unread} writes a \`/// name : \` production and is not read, so its productions are ` +
            `compared with nothing: the grammar is read from the top of ${PARSER_DIRECTORY_NAME}`
    )
}

const tokenText = readText(TOKENS)
const tokenLiterals = new Set()
for (const m of tokenText.matchAll(/CAMEL_(?:KEYWORD|PUNCT)\(\w+, "((?:\\.|[^"\\])*)"/g)) {
    tokenLiterals.add(m[1].replace(/\\(.)/g, '$1'))
}

const parserText = PARSER_FILES.map((f) => readText(f)).join('\n')
for (const p of productions) {
    const body = p.lines.join(' ')
    for (const m of body.matchAll(/'((?:\\.|[^'\\])+)'/g)) {
        const literal = m[1].replace(/\\(.)/g, '$1')
        if (!tokenLiterals.has(literal) && !CONTEXTUAL.has(literal)) {
            errors.push(`production '${p.name}' uses the literal '${literal}', which is not a token of token_kinds.def`)
        }
    }
    const bare = body.replace(/'((?:\\.|[^'\\])+)'/g, ' ')
    for (const m of bare.matchAll(/[A-Za-z_][A-Za-z_]*/g)) {
        if (!names.has(m[0]) && !TOKEN_CLASSES.has(m[0])) {
            errors.push(`production '${p.name}' refers to '${m[0]}', which is neither a production nor a token class`)
        }
    }
    const fn = FOLDED[p.name] || 'parse' + p.name[0].toUpperCase() + p.name.slice(1)
    if (!new RegExp(`void ParserImpl::${fn}\\(`).test(parserText)) {
        errors.push(`production '${p.name}' has no function ${fn} in the parser`)
    }
}

for (const m of readText(NODES).matchAll(/^CAMEL_NODE\((\w+), \w+\)\s*\/\/\s*(.*)$/gm)) {
    if (m[1] === 'Error') continue
    const kind = m[1]
    const production = kind[0].toLowerCase() + kind.slice(1)
    // A node kind is a production, or one alternative of one (named in NODE_OF).
    const NODE_OF = {
        restValue: 'indexValue', ifExpr: 'ctrlExpr', matchExpr: 'ctrlExpr', tryExpr: 'ctrlExpr',
        castExpr: 'unaryExpr', refData: 'primaryData', constData: 'typeOrData', refType: 'primaryType',
        parenType: 'primaryType', typeOfExpr: 'primaryType', packType: 'typeExpr', matchTypeType: 'primaryType', typeArm: 'matchTypeType', typeAsType: 'primaryType',
        tupleParam: 'keyParamPair', strayPositional: 'indexValue',
    }
    const owner = NODE_OF[production] || production
    if (!names.has(owner)) {
        errors.push(`node kind ${kind} of node_kinds.def corresponds to no production ('${owner}')`)
    }
}

// The keyword table of the reference (2.4) lists exactly the keywords of the token table.
const keywords = new Set([...tokenText.matchAll(/^CAMEL_KEYWORD\(\w+, "(\w+)"/gm)].map((m) => m[1]))
if (keywords.size === 0) {
    errors.push('the token table has no CAMEL_KEYWORD line; the keyword-table check would compare two empty sets')
}
for (const doc of DOCS.map((d) => d.replace('08_top_level_and_grammar.md', '02_lexical.md'))) {
    const rel = path.relative(ROOT, doc).split(path.sep).join('/')
    const text = readText(doc)
    const from = text.indexOf('## 2.4')
    const to = text.indexOf('## 2.5')
    // The reserved table is the rows above the contextual table of 2.4.1; that block lists words
    // that are not keywords, so it is not read here.
    const contextAt = text.indexOf(CONTEXT_BEGIN, from)
    const end = contextAt >= 0 && contextAt < to ? contextAt : to
    const listed = new Set()
    for (const line of text.slice(from, end).split('\n')) {
        if (!line.startsWith('|')) continue
        for (const m of line.matchAll(/`([a-z_]+)`/g)) listed.add(m[1])
    }
    for (const word of keywords) {
        if (!listed.has(word)) errors.push(`${rel}: the keyword table (2.4) lacks '${word}'`)
    }
    for (const word of listed) {
        if (!keywords.has(word)) errors.push(`${rel}: the keyword table (2.4) lists '${word}', which is not a keyword`)
    }
}

// The contextual keyword table (scripts/context-keywords.mjs) is written from the readings of the
// parser, so each entry must still have its reading, and no word may be read by name that the
// table does not have. A word may also be read where the parser leaves it as a name and the builder
// that turns the tree into a graph decides on it: `error` in the two-word pattern `error e`.
function flat(text) {
    return text.replace(/\s+/g, ' ').trim()
}
const BUILDER = path.join(PARSE_SOURCES, 'ast', 'builder.cpp')
if (!fs.existsSync(BUILDER)) {
    errors.push(`${path.relative(ROOT, BUILDER).split(path.sep).join('/')} does not exist`)
}
const evidenceFlat = flat(parserText + '\n' + (fs.existsSync(BUILDER) ? readText(BUILDER) : ''))
for (const entry of CONTEXT_KEYWORDS) {
    for (const line of entry.evidence) {
        if (!evidenceFlat.includes(flat(line))) {
            errors.push(
                `contextual keyword '${entry.word}' (scripts/context-keywords.mjs) cites a reading ` +
                    `the parser no longer has: ${flat(line)}`
            )
        }
    }
}
const readByWord = new Set()
for (const m of parserText.matchAll(/atWord\(\s*[^,()]+?,\s*"([a-z_]+)"\s*\)/g)) readByWord.add(m[1])
if (readByWord.size === 0) {
    errors.push('the parser reads no word with atWord("<word>"); the contextual-keyword check would compare two empty sets')
}
for (const word of readByWord) {
    if (!CONTEXTUAL.has(word)) {
        errors.push(`the parser reads '${word}' with atWord, and it is not in scripts/context-keywords.mjs`)
    }
}

const block = BEGIN + '\n\n```\n' + render(productions) + '\n```\n\n' + END
const contextBlocks = new Map(LEXICAL_DOCS.map((d) => [d.file, renderContextKeywordTable(d.lang)]))
const mode = process.argv[2]
if (mode !== '--write' && mode !== '--check') {
    console.error('usage: node scripts/check-grammar.mjs --write | --check')
    process.exit(2)
}
for (const doc of DOCS) {
    const rel = path.relative(ROOT, doc).split(path.sep).join('/')
    if (!fs.existsSync(doc)) {
        errors.push(`${rel} does not exist`)
        continue
    }
    const raw = fs.readFileSync(doc, 'utf8')
    const text = raw.replace(/\r\n/g, '\n')
    const a = text.indexOf(BEGIN)
    const b = text.indexOf(END)
    if (a < 0 || b < 0) {
        errors.push(`${rel} has no grammar block (the two marker comments)`)
        continue
    }
    const updated = text.slice(0, a) + block + text.slice(b + END.length)
    if (mode === '--write') {
        if (updated !== text) {
            // Written with the line ends the file had.
            fs.writeFileSync(doc, raw.includes('\r\n') ? updated.replace(/\n/g, '\r\n') : updated)
            console.log(`wrote ${rel}`)
        }
    } else if (updated !== text) {
        errors.push(`the grammar block of ${rel} is out of date: run node scripts/check-grammar.mjs --write`)
    }
}

// The contextual keyword table of 2.4.1, in both languages, from scripts/context-keywords.mjs.
for (const { file, lang } of LEXICAL_DOCS) {
    const rel = path.relative(ROOT, file).split(path.sep).join('/')
    if (!fs.existsSync(file)) {
        errors.push(`${rel} does not exist`)
        continue
    }
    const raw = fs.readFileSync(file, 'utf8')
    const text = raw.replace(/\r\n/g, '\n')
    const a = text.indexOf(CONTEXT_BEGIN)
    const b = text.indexOf(CONTEXT_END)
    if (a < 0 || b < 0) {
        errors.push(`${rel} has no contextual keyword table (the two marker comments of 2.4.1)`)
        continue
    }
    const updated =
        text.slice(0, a) +
        CONTEXT_BEGIN +
        '\n\n' +
        contextBlocks.get(file) +
        '\n\n' +
        text.slice(b)
    if (mode === '--write') {
        if (updated !== text) {
            fs.writeFileSync(file, raw.includes('\r\n') ? updated.replace(/\n/g, '\r\n') : updated)
            console.log(`wrote ${rel}`)
        }
    } else if (updated !== text) {
        errors.push(
            `the contextual keyword table of ${rel} (2.4.1) is out of date (${lang}): ` +
                'run node scripts/check-grammar.mjs --write'
        )
    }
}

for (const error of errors) console.error('error: ' + error)
if (errors.length > 0) process.exit(1)
console.log(
    `grammar: ${productions.length} productions and ${CONTEXT_KEYWORDS.length} contextual keywords, ` +
        'in step with the parser, the token table and the language reference'
)
