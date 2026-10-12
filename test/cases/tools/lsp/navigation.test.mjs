// Push diagnostics, incremental synchronisation, hover, definition, references, highlights
// and symbols, on a program of two files (fixtures/main.cml imports fixtures/shapes.cml).

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { withServer, check, checkEq, fmtRange, marked } from '../../../tools/lsp/client.mjs'

const FIXTURES = path.join(path.dirname(fileURLToPath(import.meta.url)), 'fixtures')
const MAIN = fs.readFileSync(path.join(FIXTURES, 'main.cml'), 'utf8')
const SHAPES = fs.readFileSync(path.join(FIXTURES, 'shapes.cml'), 'utf8')

await withServer(async (ls) => {
    const caps = ls.initializeResult.capabilities
    checkEq(caps.textDocumentSync.change, 2, 'incremental synchronisation is announced')
    for (const provider of ['hoverProvider', 'definitionProvider', 'referencesProvider',
        'documentHighlightProvider', 'documentSymbolProvider', 'workspaceSymbolProvider']) {
        check(caps[provider] === true, `${provider} is announced`)
    }

    const shapesUri = ls.writeFile('shapes.cml', SHAPES)
    const src = marked(MAIN)
    const uri = ls.open('main.cml', MAIN)
    const at = (needle, nth = 1, delta = 0) => ({
        textDocument: { uri },
        position: src.find(needle, nth, delta),
    })

    // ---- push diagnostics ----
    let pushed = await ls.diagnosticsFor(uri)
    check(pushed !== null, 'diagnostics are published after didOpen')
    checkEq(pushed.version, 1, 'the published diagnostics carry the document version')
    checkEq(pushed.diagnostics.length, 1, 'one warning in the fixture', pushed.diagnostics)
    checkEq(pushed.diagnostics[0].severity, 2, 'it is a warning')
    checkEq(fmtRange(pushed.diagnostics[0].range), '22:4-22:17', 'at the unwaited assignment')

    // ---- hover ----
    const hover = async (needle, nth, delta) => {
        const h = await ls.request('textDocument/hover', at(needle, nth, delta))
        return h ? h.contents.value : null
    }
    let h = await hover('let p', 1, 4)
    check(h.includes('let p: { x: int, y: int }'), 'a let shows its type in source spelling', h)
    h = await hover('var total', 1, 4)
    check(h.includes('var total: int'), 'a var shows `var` and `int`, not i64', h)
    h = await hover('norm2(p)')
    check(h.includes('export func norm2(p: Point): int'), 'an imported function: its header as written', h)
    check(h.includes('The squared length of p.\nSecond line of the comment.'),
        'the comment above the declaration is the documentation', h)
    check(h.includes('shapes.cml'), 'the file of the definition is named', h)
    h = await hover('identity(n)')
    check(h.includes('func identity(x: typeas T): T'), 'a generic function: its header', h)
    check(h.includes('(x: int) => int') && h.includes('This call'),
        'at a call the instance of the generic function is shown', h)
    check(h.includes('Returns its argument.'), 'with its documentation', h)
    h = await hover("identity('text')")
    check(/This call:\s*```camel\s*\(x: string\) => string/.test(h),
        'another call shows another instance', h)
    h = await hover('func identity', 1, 5)
    check(h.includes('(x: int) => int') && h.includes('(x: string) => string'),
        'the declaration lists the instances', h)
    h = await hover('let copy', 1, 4)
    check(h.includes('copy: int') && h.includes('copy: string'),
        'a local of a generic function has one type per instance', h)
    h = await hover('add(1, 2)')
    check(h.includes('func add(a: int, b: int): int') && !h.includes('float'),
        'a call of an overloaded function shows the overload it chose', h)
    h = await hover('add(1.5')
    check(h.includes('func add(a: float, b: float): float'), 'and the other one', h)
    h = await hover('q.x', 1, 2)
    check(h.includes('(field) x: int'), 'a field shows its type', h)
    h = await hover('pair.0', 1, 5)
    check(h.includes('(field) 0: int'), 'a tuple element shows its type', h)
    h = await hover('println')
    check(h.includes('println: <fmt?: string> (...args: any) => void') && h.includes('module `io`'),
        'a library function shows its signature and its module', h)
    h = await hover('double(sum)')
    check(h.includes('let double: (v: int) => int'), 'a function value shows its type', h)
    h = await hover('func bump', 1, 5)
    check(h.includes('sync func bump(var counter: int, by: int)'), 'modifiers are shown', h)
    check(h.includes('=> int'), 'an inferred return type is shown', h)
    h = await hover('Pair = (1', 1, 0)
    check(h.includes('type Pair = (int, string)'), 'a type alias shows its definition', h)
    h = await hover('a: int, b: int): int', 1, 3)
    check(h.includes('type int'), 'a built-in type name', h)
    h = await hover('module main')
    checkEq(h, null, 'a keyword has no hover')
    // On the range of a diagnostic: the explanation of its code.
    h = await hover('counter += by', 1, 9)
    check(h.includes('SemanticDiag::IgnoredSideEffect') && h.includes('--explain'),
        'the code of a diagnostic is explained', h)

    // ---- definition ----
    const def = async (needle, nth, delta) =>
        ls.request('textDocument/definition', at(needle, nth, delta))
    let d = await def('norm2(p)')
    checkEq(d.length, 1, 'one definition for an imported function')
    checkEq(d[0].uri, shapesUri, 'in the other file')
    checkEq(fmtRange(d[0].range), '6:12-6:17', 'at the name of the function')
    d = await def('p)', 1)
    checkEq([d[0].uri, fmtRange(d[0].range)], [uri, '27:8-27:9'], 'a local: its let')
    d = await def('return a + b', 1, 7)
    checkEq(fmtRange(d[0].range), '13:9-13:10', 'a parameter: the parameter of its function')
    d = await def('return a + b', 2, 7)
    checkEq(fmtRange(d[0].range), '17:9-17:10', 'the same name in the other overload')
    d = await def('add(1.5')
    checkEq(d.map((l) => fmtRange(l.range)), ['17:5-17:8'], 'a call: the overload chosen')
    d = await def('Pair = (1')
    checkEq(fmtRange(d[0].range), '5:5-5:9', 'a type alias')
    d = await def('): T', 1, 3)
    checkEq(fmtRange(d[0].range), '8:24-8:25', 'a type variable: its typeas')
    d = await def('v * 2')
    checkEq(fmtRange(d[0].range), '38:18-38:19', 'a parameter of a function literal')
    d = await def('println')
    checkEq(d, [], 'a built-in function has no definition in source')
    d = await def('{ norm2', 1, 2)
    checkEq(d[0].uri, shapesUri, 'the name in an import declaration leads to the other file')

    // ---- references and highlights ----
    const refs = async (needle, nth, delta, includeDeclaration = true) =>
        (await ls.request('textDocument/references', {
            ...at(needle, nth, delta),
            context: { includeDeclaration },
        })).map((l) => `${l.uri === uri ? '' : path.basename(l.uri) + ' '}${fmtRange(l.range)}`)
    checkEq(await refs('let p', 1, 4), ['27:8-27:9', '28:18-28:19', '29:18-29:19'],
        'references of a local, with its declaration')
    checkEq(await refs('let p', 1, 4, false), ['28:18-28:19', '29:18-29:19'],
        'and without')
    checkEq(await refs('norm2(p)'),
        ['2:9-2:14', '28:12-28:17', 'shapes.cml 6:12-6:17'],
        'references of an imported function: the import, the call, the declaration')
    checkEq(await refs('counter += by'), ['21:19-21:26', '22:4-22:11', '23:11-23:18'],
        'a var parameter: the assignment is a reference')
    checkEq(await refs('add(1, 2)'), ['13:5-13:8', '34:14-34:17'],
        'references of one overload do not include calls of the other')
    checkEq(await refs('identity(n)'), ['8:5-8:13', '32:12-32:20', '33:12-33:20'],
        'a generic function')
    checkEq(await refs('q.x', 1, 2), ['31:15-31:16'], 'a field: its uses on values of that type')

    const hl = await ls.request('textDocument/documentHighlight', at('total += q.x'))
    checkEq(hl.map((x) => `${fmtRange(x.range)}/${x.kind}`), ['30:8-30:13/3', '31:4-31:9/2'],
        'highlights: the declaration as a write, the use as a read')

    // ---- symbols ----
    const symbols = await ls.request('textDocument/documentSymbol', { textDocument: { uri } })
    checkEq(symbols.map((s) => `${s.name}/${s.kind}`),
        ['Pair/23', 'identity/12', 'add/12', 'add/12', 'bump/12', 'main/12'],
        'the outline lists types and functions')
    const mainSymbol = symbols.find((s) => s.name === 'main')
    check(mainSymbol.children.some((c) => c.name === 'total' && c.kind === 13),
        'locals are children of their function', mainSymbol.children)
    checkEq(symbols[1].detail, '(x: typeas T): T', 'a function carries its signature')
    checkEq(fmtRange(symbols[1].selectionRange), '8:5-8:13', 'selection range is the name')

    // ---- incremental synchronisation ----
    // Rename the local `n` by two range edits in one notification, then ask about the result.
    const n1 = src.range('n = norm2')
    const n2 = src.find('identity(n)', 1, 9)
    const one = (pos) => ({ start: pos, end: { line: pos.line, character: pos.character + 1 } })
    ls.notify('textDocument/didChange', {
        textDocument: { uri, version: 2 },
        contentChanges: [
            // The later position first, so that the first edit does not move the second.
            { range: one(n2), text: 'count' },
            { range: one(n1.start), text: 'count' },
        ],
    })
    pushed = await ls.diagnosticsFor(uri, 2)
    checkEq(pushed.diagnostics.length, 1, 'the edited text compiles as before', pushed.diagnostics)
    h = await ls.request('textDocument/hover', { textDocument: { uri }, position: n1.start })
    check(h.contents.value.includes('let count: int'), 'the edits were applied', h.contents.value)
    // An edit that breaks the program is reported, and undoing it clears the report.
    ls.notify('textDocument/didChange', {
        textDocument: { uri, version: 3 },
        contentChanges: [{ range: { start: n1.start, end: n1.start }, text: 'x' }],
    })
    pushed = await ls.diagnosticsFor(uri, 3)
    check(pushed.diagnostics.some((x) => x.severity === 1 && x.message.includes('count')),
        'an unresolved reference after the edit', pushed.diagnostics)
    ls.notify('textDocument/didChange', {
        textDocument: { uri, version: 4 },
        contentChanges: [{ range: one(n1.start), text: '' }],
    })
    pushed = await ls.diagnosticsFor(uri, 4)
    check(!pushed.diagnostics.some((x) => x.severity === 1), 'the error is gone', pushed.diagnostics)

    // ---- workspace symbols ----
    const ws = await ls.request('workspace/symbol', { query: 'ident' })
    checkEq(ws.map((s) => s.name), ['identity'], 'workspace symbols of the open documents by substring')

    // ---- closing clears the diagnostics ----
    ls.notify('textDocument/didClose', { textDocument: { uri } })
    pushed = await ls.diagnosticsFor(uri)
    checkEq(pushed.diagnostics, [], 'closing a document clears its diagnostics')
})
