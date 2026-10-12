// camel/graph: the graph of the function at the cursor, as the DOT text std::rgir prints.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { withServer, check, checkEq, marked } from '../../../tools/lsp/client.mjs'

const FIXTURES = path.join(path.dirname(fileURLToPath(import.meta.url)), 'fixtures')
const MAIN = fs.readFileSync(path.join(FIXTURES, 'main.cml'), 'utf8')
const SHAPES = fs.readFileSync(path.join(FIXTURES, 'shapes.cml'), 'utf8')

await withServer(async (ls) => {
    check(ls.initializeResult.capabilities.experimental.camelGraphProvider === true,
        'the capability is announced')
    ls.writeFile('shapes.cml', SHAPES)
    const src = marked(MAIN)
    const uri = ls.open('main.cml', MAIN)

    let g = await ls.request('camel/graph', {
        textDocument: { uri }, position: src.find('return a + b', 1, 4),
    })
    checkEq(g.function, 'add', 'the function around the cursor')
    check(g.text.startsWith('digraph') && g.text.includes('label="add"'), 'its graph as DOT', g.text.slice(0, 200))
    check(g.text.includes('type="OPER"') && !g.text.includes('label="bump"'),
        'only that function, with its nodes', g.text.slice(0, 400))
    checkEq(g.passes, [], 'no passes were asked for')

    // Outside a function: the whole program.
    g = await ls.request('camel/graph', { textDocument: { uri }, position: { line: 0, character: 0 } })
    checkEq(g.function, null, 'no function at the module line')
    check(g.text.includes('label="main"') && g.text.includes('label="add"'), 'the whole graph', g.text.slice(0, 200))

    // After a pass sequence.
    const inlined = await ls.request('camel/graph', {
        textDocument: { uri }, position: src.find('let sum'), passes: ['std::inline'],
    })
    checkEq(inlined.passes, ['std::inline'], 'the passes are reported back')
    check(inlined.text.startsWith('digraph') && inlined.text !== g.text, 'the graph after std::inline differs')

    // The text of the editor is used, not the file on disk (there is none).
    ls.change(uri, MAIN.replace('return a + b\n}\n\nfunc add(a: float', 'return a - b\n}\n\nfunc add(a: float'), 2)
    g = await ls.request('camel/graph', { textDocument: { uri }, position: src.find('return a + b', 1, 4) })
    check(g.text.includes('sub'), 'an unsaved edit is in the graph', g.text.slice(0, 600))

    // A program with an error: no graph, the compiler's report instead.
    ls.change(uri, 'module main\n\nfunc main() sync {\n    return nope\n}\n', 3)
    g = await ls.request('camel/graph', { textDocument: { uri }, position: { line: 3, character: 4 } })
    checkEq(g.text, '', 'no graph for a program in error')
    check(g.diagnostics.includes('nope') && g.diagnostics.includes('main.cml'),
        'the report names the document, not the scratch file', g.diagnostics)

    // A pass name is not a shell command.
    const r = await ls.requestRaw('camel/graph', {
        textDocument: { uri }, position: { line: 0, character: 0 }, passes: ['std::inline; echo hi'],
    })
    checkEq(r.error?.code, -32602, 'a pass name with shell syntax is refused')
    check(fs.readdirSync(ls.dir()).every((f) => !f.startsWith('.camel-ls-')), 'no scratch file is left behind')
})
