#!/usr/bin/env node
// Checks the tables of syntaxes/camel.tmLanguage.json against the language's own sources:
//
//   keywords       every keyword token of antlr/OpenCMLLex.g4 is matched by exactly one pattern
//                  of the grammar's "keywords" rule, and that rule lists no other word;
//   operators      every literal token of antlr/OpenCML.g4 (and the punctuation tokens of the
//                  lexer) is matched in full by a pattern of the "operators", "punctuation" or
//                  "brackets" rules;
//   built-in types the "builtin-types" rule lists exactly the names that
//                  src/compile/gct/builder.cpp puts in the root type scope.
//
// No dependencies: the patterns of those rules are plain enough to be read as JavaScript
// regular expressions. Run from anywhere: node editors/vscode/scripts/check-grammar.mjs

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const EXT_ROOT = path.dirname(HERE)
const REPO_ROOT = path.resolve(EXT_ROOT, '..', '..')

const LEXER = path.join(REPO_ROOT, 'antlr', 'OpenCMLLex.g4')
const PARSER = path.join(REPO_ROOT, 'antlr', 'OpenCML.g4')
const TYPE_SCOPE = path.join(REPO_ROOT, 'src', 'compile', 'gct', 'builder.cpp')
const GRAMMAR = path.join(EXT_ROOT, 'syntaxes', 'camel.tmLanguage.json')

const errors = []
const notes = []

function rel(file) {
    return path.relative(REPO_ROOT, file).split(path.sep).join('/')
}

/** Removes comments and action blocks so that only rule bodies are scanned for literals. */
function stripAntlrNoise(text) {
    // Comments start outside quoted literals only: the lexer has the literals '//' and '/*'.
    let out = ''
    for (let i = 0; i < text.length; ) {
        const c = text[i]
        if (c === "'") {
            let end = i + 1
            while (end < text.length && text[end] !== "'" && text[end] !== '\n') {
                end += text[end] === '\\' ? 2 : 1
            }
            out += text.slice(i, end + 1)
            i = end + 1
        } else if (c === '/' && text[i + 1] === '/') {
            while (i < text.length && text[i] !== '\n') ++i
        } else if (c === '/' && text[i + 1] === '*') {
            const end = text.indexOf('*/', i + 2)
            i = end < 0 ? text.length : end + 2
            out += ' '
        } else {
            out += c
            ++i
        }
    }
    // @header { ... } and @parser::members { ... } hold C++ code, not grammar.
    for (;;) {
        const at = out.search(/@[A-Za-z:]+\s*\{/)
        if (at < 0) break
        let depth = 0
        let end = out.indexOf('{', at)
        for (; end < out.length; ++end) {
            if (out[end] === '{') ++depth
            else if (out[end] === '}' && --depth === 0) break
        }
        out = out.slice(0, at) + out.slice(end + 1)
    }
    return out
}

/** The quoted literals of an ANTLR grammar text, unescaped. */
function antlrLiterals(text) {
    const literals = []
    const re = /'((?:\\.|[^'\\])+)'/g
    for (let m; (m = re.exec(text)); ) {
        literals.push(m[1].replace(/\\(.)/g, '$1'))
    }
    return literals
}

/** Lexer rules of the form `NAME : 'text' ;`, as [name, text] pairs. */
function lexerLiteralRules(text) {
    const rules = []
    const re = /^([A-Z_][A-Z_0-9]*)\s*:\s*'((?:\\.|[^'\\])+)'\s*;/gm
    for (let m; (m = re.exec(text)); ) {
        rules.push([m[1], m[2].replace(/\\(.)/g, '$1')])
    }
    return rules
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

const lexerText = stripAntlrNoise(fs.readFileSync(LEXER, 'utf8'))
const parserText = stripAntlrNoise(fs.readFileSync(PARSER, 'utf8'))
const grammar = JSON.parse(fs.readFileSync(GRAMMAR, 'utf8'))
const repository = grammar.repository || {}

// ---------------------------------------------------------------------------------------------
// Keywords
// ---------------------------------------------------------------------------------------------

const literalRules = lexerLiteralRules(lexerText)
const lexerKeywords = literalRules.filter(([, text]) => /^[A-Za-z_]+$/.test(text)).map(([, text]) => text)
const lexerPunctuation = literalRules.filter(([, text]) => !/^[A-Za-z_]+$/.test(text)).map(([, text]) => text)

if (lexerKeywords.length === 0) {
    errors.push(`no keyword token found in ${rel(LEXER)}: has the format of the file changed?`)
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
        errors.push(`keyword '${word}' of ${rel(LEXER)} is matched by no pattern of the grammar's "keywords" rule`)
    } else if (scopes.length > 1) {
        errors.push(`keyword '${word}' is listed ${scopes.length} times in the grammar's "keywords" rule (${scopes.join(', ')})`)
    }
}
for (const word of grammarKeywords.keys()) {
    if (!lexerKeywords.includes(word)) {
        errors.push(`the grammar's "keywords" rule lists '${word}', which is not a keyword token of ${rel(LEXER)}`)
    }
}

// ---------------------------------------------------------------------------------------------
// Operators and punctuation
// ---------------------------------------------------------------------------------------------

const parserLiterals = [...new Set([...antlrLiterals(parserText), ...lexerPunctuation])].filter(
    (text) => !/^[A-Za-z_]+$/.test(text) || text === '_'
)
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
    errors.push(`token '${text}' of ${rel(PARSER)} is matched by no operator, punctuation or bracket pattern of the grammar`)
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
        errors.push(`built-in type '${word}' is a keyword token of ${rel(LEXER)}`)
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

for (const note of notes) console.log(`note: ${note}`)
if (errors.length > 0) {
    for (const error of errors) console.log(`error: ${error}`)
    console.log(`grammar check failed: ${errors.length} error(s)`)
    process.exit(1)
}
console.log(
    `grammar check passed: ${lexerKeywords.length} keywords, ${parserLiterals.length} operator and punctuation tokens, ${grammarTypes.length} built-in types`
)
if (process.argv.includes('--list')) {
    console.log(`keywords: ${lexerKeywords.join(' ')}`)
    console.log(`tokens: ${parserLiterals.join(' ')}`)
    console.log(`types: ${grammarTypes.join(' ')}`)
}
