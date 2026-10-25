#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Checks the tables of syntaxes/camel.tmLanguage.json against the language's own sources:
//
//   keywords       every keyword of include/camel/parse/syntax/token_kinds.def (the table the
//                  lexer itself is built from) is matched by exactly one pattern of the
//                  grammar's "keywords" rule, and that rule lists no other word;
//   operators      every operator and separator of that table is matched in full by a pattern
//                  of the "operators", "punctuation" or "brackets" rules;
//   built-in types the "builtin-types" rule lists exactly the names that
//                  src/compile/gct/builder.cpp puts in the root type scope.
//   contextual     the "contextual-keywords" rule names exactly the contextual keywords of
//                  scripts/context-keywords.mjs (the table of the language reference, 02, 2.4.1),
//                  each with a scope, and none of them is a reserved keyword.
//
// No dependencies: the patterns of those rules are plain enough to be read as JavaScript
// regular expressions. Run from anywhere: node editors/vscode/scripts/check-grammar.mjs

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const EXT_ROOT = path.dirname(HERE)
const REPO_ROOT = path.resolve(EXT_ROOT, '..', '..')

const TOKENS = path.join(REPO_ROOT, 'include', 'camel', 'parse', 'syntax', 'token_kinds.def')
const TYPE_SCOPE = path.join(REPO_ROOT, 'src', 'compile', 'gct', 'builder.cpp')
const GRAMMAR = path.join(EXT_ROOT, 'syntaxes', 'camel.tmLanguage.json')
// The one table of the contextual keywords, shared with scripts/check-grammar.mjs and with the
// language reference (02, 2.4.1). Imported rather than copied: the words of this grammar, of the
// generator's quoted literals and of the page are then the same list.
const CONTEXT_TABLE = path.join(REPO_ROOT, 'scripts', 'context-keywords.mjs')

const errors = []
const notes = []

function rel(file) {
    return path.relative(REPO_ROOT, file).split(path.sep).join('/')
}

/** The texts of the entries of one macro of token_kinds.def, e.g. CAMEL_KEYWORD(KwIf, "if", ...). */
function tokenTexts(text, macro) {
    const texts = []
    const re = new RegExp(`^${macro}\\(\\w+, "((?:\\\\.|[^"\\\\])*)"`, 'gm')
    for (let m; (m = re.exec(text)); ) {
        texts.push(m[1].replace(/\\(.)/g, '$1'))
    }
    return texts
}

function collectMatches(rule, repository, out = [], seen = new Set()) {
    if (!rule || seen.has(rule)) return out
    seen.add(rule)
    for (const key of ['match', 'begin', 'end']) {
        if (typeof rule[key] === 'string') out.push({ regex: rule[key], name: rule.name || '' })
    }
    for (const sub of rule.patterns || []) {
        if (sub.include && sub.include.startsWith('#')) {
            collectMatches(repository[sub.include.slice(1)], repository, out, seen)
        } else if (!sub.include) {
            collectMatches(sub, repository, out, seen)
        }
    }
    return out
}

function matchesWhole(regex, text) {
    let re
    try {
        re = new RegExp(`^(?:${regex})$`)
    } catch (err) {
        errors.push(`pattern ${JSON.stringify(regex)} is not readable as a JavaScript regular expression: ${err.message}`)
        return false
    }
    return re.test(text)
}

/** The words of a `\b(?:a|b|c)\b` pattern, or null when the pattern has another shape. */
function wordList(regex) {
    const m = /^\\b\(\?:([A-Za-z_0-9|]+)\)\\b$/.exec(regex)
    return m ? m[1].split('|') : null
}

const tokenTable = fs.readFileSync(TOKENS, 'utf8')
const grammar = JSON.parse(fs.readFileSync(GRAMMAR, 'utf8'))
const repository = grammar.repository || {}

// ---------------------------------------------------------------------------------------------
// Keywords
// ---------------------------------------------------------------------------------------------

const lexerKeywords = tokenTexts(tokenTable, 'CAMEL_KEYWORD')

if (lexerKeywords.length === 0) {
    errors.push(`no keyword token found in ${rel(TOKENS)}: has the format of the file changed?`)
}

const keywordRule = repository.keywords
const grammarKeywords = new Map() // word -> scope names
if (!keywordRule) {
    errors.push('the grammar has no "keywords" rule')
} else {
    for (const pattern of keywordRule.patterns || []) {
        const words = typeof pattern.match === 'string' ? wordList(pattern.match) : null
        if (!words) {
            errors.push(`"keywords" pattern ${JSON.stringify(pattern.match || pattern)} is not of the form \\b(?:a|b|c)\\b`)
            continue
        }
        if (!pattern.name) {
            errors.push(`"keywords" pattern ${JSON.stringify(pattern.match)} has no scope name`)
        }
        for (const word of words) {
            if (!grammarKeywords.has(word)) grammarKeywords.set(word, [])
            grammarKeywords.get(word).push(pattern.name || '')
        }
    }
}

for (const word of lexerKeywords) {
    const scopes = grammarKeywords.get(word)
    if (!scopes) {
        errors.push(`keyword '${word}' of ${rel(TOKENS)} is matched by no pattern of the grammar's "keywords" rule`)
    } else if (scopes.length > 1) {
        errors.push(`keyword '${word}' is listed ${scopes.length} times in the grammar's "keywords" rule (${scopes.join(', ')})`)
    }
}
for (const word of grammarKeywords.keys()) {
    if (!lexerKeywords.includes(word)) {
        errors.push(`the grammar's "keywords" rule lists '${word}', which is not a keyword token of ${rel(TOKENS)}`)
    }
}

// ---------------------------------------------------------------------------------------------
// Operators and punctuation
// ---------------------------------------------------------------------------------------------

const parserLiterals = [...new Set(tokenTexts(tokenTable, 'CAMEL_PUNCT'))]
const symbolPatterns = [
    ...collectMatches(repository.operators, repository),
    ...collectMatches(repository.punctuation, repository),
    ...collectMatches(repository.brackets, repository),
    ...collectMatches(repository.wildcard, repository),
].map((entry) => entry.regex)
// A bracket rule's begin/end and the '<' '>' of the angle rules carry look-around; the plain
// patterns of "operators" cover '<' and '>' themselves.

const unmatchedSymbols = parserLiterals.filter((text) => !symbolPatterns.some((regex) => matchesWhole(regex, text)))
for (const text of unmatchedSymbols) {
    errors.push(`token '${text}' of ${rel(TOKENS)} is matched by no operator, punctuation or bracket pattern of the grammar`)
}

// ---------------------------------------------------------------------------------------------
// Built-in types
// ---------------------------------------------------------------------------------------------

const typeRule = repository['builtin-types']
const grammarTypes = []
if (!typeRule) {
    errors.push('the grammar has no "builtin-types" rule')
} else {
    for (const pattern of typeRule.patterns || []) {
        const words = typeof pattern.match === 'string' ? wordList(pattern.match) : null
        if (!words) {
            errors.push(`"builtin-types" pattern ${JSON.stringify(pattern.match || pattern)} is not of the form \\b(?:a|b|c)\\b`)
        } else {
            grammarTypes.push(...words)
        }
    }
}
for (const word of grammarTypes) {
    if (lexerKeywords.includes(word)) {
        errors.push(`built-in type '${word}' is a keyword token of ${rel(TOKENS)}`)
    }
}

let sourceTypes = []
if (fs.existsSync(TYPE_SCOPE)) {
    const re = /typeScope_->insert\(Reference\("([A-Za-z_][A-Za-z_0-9]*)"\)/g
    const text = fs.readFileSync(TYPE_SCOPE, 'utf8')
    for (let m; (m = re.exec(text)); ) sourceTypes.push(m[1])
    sourceTypes = [...new Set(sourceTypes)]
}
if (sourceTypes.length === 0) {
    notes.push(`built-in types: no 'typeScope_->insert(Reference("..."))' found in ${rel(TYPE_SCOPE)}; the list of the grammar was not compared`)
} else {
    for (const word of sourceTypes) {
        if (!grammarTypes.includes(word)) {
            errors.push(`built-in type '${word}' of ${rel(TYPE_SCOPE)} is missing from the grammar's "builtin-types" rule`)
        }
    }
    for (const word of grammarTypes) {
        if (!sourceTypes.includes(word)) {
            errors.push(`the grammar's "builtin-types" rule lists '${word}', which ${rel(TYPE_SCOPE)} does not put in the root type scope`)
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Contextual keywords
// ---------------------------------------------------------------------------------------------

const contextWords = []
const contextScopes = new Set()
const contextRule = repository['contextual-keywords']
if (!fs.existsSync(CONTEXT_TABLE)) {
    errors.push(`${rel(CONTEXT_TABLE)} does not exist: it is the one table of the contextual keywords`)
} else {
    const table = await import(pathToFileURL(CONTEXT_TABLE).href)
    const entries = table.CONTEXT_KEYWORDS || []
    if (entries.length === 0) {
        errors.push(`${rel(CONTEXT_TABLE)} has no CONTEXT_KEYWORDS; the check below would compare two empty sets`)
    }
    if (!contextRule) {
        errors.push('the grammar has no "contextual-keywords" rule')
    } else {
        for (const pattern of contextRule.patterns || []) {
            // Each pattern names its words in its comment, in the fixed form
            // "contextual keyword(s): 'a', 'b' — where they are keywords".
            const named = /^contextual keywords?: ((?:'[a-z_]+'(?:, )?)+)/.exec(pattern.comment || '')
            if (!named) {
                errors.push(
                    `"contextual-keywords" pattern ${JSON.stringify(pattern.match || pattern)} does not name its words in a comment of the form "contextual keywords: 'a', 'b" —`
                )
                continue
            }
            const words = [...named[1].matchAll(/'([a-z_]+)'/g)].map((m) => m[1])
            const names = [pattern.name, ...Object.values(pattern.captures || {}).map((c) => c.name)].filter(Boolean)
            if (names.length === 0) {
                errors.push(`"contextual-keywords" pattern ${JSON.stringify(pattern.match)} has no scope name`)
            }
            names.forEach((name) => contextScopes.add(name))
            for (const word of words) {
                if (!pattern.match.includes(word)) {
                    errors.push(
                        `"contextual-keywords" pattern names '${word}' in its comment, and the pattern does not match it: ${JSON.stringify(pattern.match)}`
                    )
                }
                if (lexerKeywords.includes(word)) {
                    errors.push(`the "contextual-keywords" rule names '${word}', which is a reserved keyword token of ${rel(TOKENS)}`)
                }
                contextWords.push(word)
            }
        }
    }
    for (const entry of entries) {
        if (!contextWords.includes(entry.word)) {
            errors.push(`contextual keyword '${entry.word}' of ${rel(CONTEXT_TABLE)} is matched by no pattern of the grammar's "contextual-keywords" rule`)
        }
    }
    for (const word of contextWords) {
        if (!entries.some((entry) => entry.word === word)) {
            errors.push(`the grammar's "contextual-keywords" rule names '${word}', which ${rel(CONTEXT_TABLE)} does not list`)
        }
    }
}

// ---------------------------------------------------------------------------------------------

for (const note of notes) console.log(`note: ${note}`)
if (errors.length > 0) {
    for (const error of errors) console.log(`error: ${error}`)
    console.log(`grammar check failed: ${errors.length} error(s)`)
    process.exit(1)
}
console.log(
    `grammar check passed: ${lexerKeywords.length} keywords, ${new Set(contextWords).size} contextual keywords, ` +
        `${parserLiterals.length} operator and punctuation tokens, ${grammarTypes.length} built-in types`
)
if (process.argv.includes('--list')) {
    console.log(`keywords: ${lexerKeywords.join(' ')}`)
    console.log(`contextual keywords: ${[...new Set(contextWords)].join(' ')} (scopes: ${[...contextScopes].join(', ')})`)
    console.log(`tokens: ${parserLiterals.join(' ')}`)
    console.log(`types: ${grammarTypes.join(' ')}`)
}
