// A formatted string holds Camel expressions: hover, definition and completion work inside
// the `${...}` of a backtick string, which is a string to the lexer but code to the language.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { withServer, check, checkEq, marked } from '../../../tools/lsp/client.mjs'

const FIXTURES = path.join(path.dirname(fileURLToPath(import.meta.url)), 'fixtures')
const fixture = (name) => fs.readFileSync(path.join(FIXTURES, name), 'utf8').replace(/\r\n/g, '\n')
const SOURCE = fixture('fstring.cml')

await withServer(async (ls) => {
    const src = marked(SOURCE)
    const uri = ls.open('fstring.cml', SOURCE)
    await ls.diagnosticsFor(uri)
    const hover = async (needle, delta = 0) => {
        const h = await ls.request('textDocument/hover', {
            textDocument: { uri }, position: src.find(needle, 1, delta),
        })
        return h ? h.contents.value : null
    }

    // ---- hover: a local and a callee in `${...}` ----
    let h = await hover('${n}', 2)
    check(h !== null && h.includes('n: int'), 'a local inside an interpolation has a hover', h)
    h = await hover('${add(', 2)
    check(h !== null && h.includes('func add(a: int, b: int): int'),
        'a function called inside an interpolation has its signature as a hover', h)

    // ---- hover: a member after a dot in `${...}` ----
    h = await hover('${p.s}', 4)
    check(h !== null && h.includes('(field) s: string'),
        'a field read inside an interpolation has a hover', h)

    // ---- definition: a local declared outside the string ----
    const d = await ls.request('textDocument/definition', {
        textDocument: { uri }, position: src.find('${n}', 1, 2),
    })
    check(Array.isArray(d) && d.length === 1 && d[0].uri === uri,
        'the definition of a name inside an interpolation is in the same file', d)
    checkEq(d[0].range.start.line, 2, 'at its declaration')

    // Semantic string tokens must leave room for the names the index resolved inside them.
    const semantic = await ls.request('textDocument/semanticTokens/full', { textDocument: { uri } })
    const legend = ls.initializeResult.capabilities.semanticTokensProvider.legend.tokenTypes
    let line = 0, column = 0
    const decoded = []
    for (let i = 0; i < semantic.data.length; i += 5) {
        const [dl, dc, length, type] = semantic.data.slice(i, i + 4)
        column = dl ? dc : column + dc; line += dl
        decoded.push({ line, column, length, type: legend[type] })
    }
    for (const [needle, delta, type] of [['${n}', 2, 'variable'], ['${add(', 2, 'function'], ['${p.s}', 4, 'property']]) {
        const position = src.find(needle, 1, delta)
        check(decoded.some(t => t.line === position.line && t.column === position.character && t.type === type),
            `${type} inside an interpolation has its own semantic token`, decoded)
        check(!decoded.some(t => t.line === position.line && t.type === 'string' && t.column <= position.character && t.column + t.length > position.character),
            'a string token does not cover interpolation code', decoded)
    }

    // ---- completion: the names in scope, inside `${...}` ----
    const text = SOURCE.replace('${n}', '${n/*c*/}')
    const m = marked(text)
    ls.change(uri, m.text, 2)
    const result = await ls.request('textDocument/completion', {
        textDocument: { uri }, position: m.at('c'),
    })
    const labels = result.items.map((i) => i.label)
    for (const expected of ['n', 'p', 'add', 'println', 'format']) {
        check(labels.includes(expected), `completion inside an interpolation offers '${expected}'`)
    }
})
