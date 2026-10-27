// Completion, signature help, rename and semantic tokens, also on text that does not compile
// (which is what a text being typed is).

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { withServer, check, checkEq, fmtRange, marked } from '../../../tools/lsp/client.mjs'

const FIXTURES = path.join(path.dirname(fileURLToPath(import.meta.url)), 'fixtures')
// With LF line ends also in a checkout that has CRLF: the checks below name lines and offsets.
const fixture = (name) => fs.readFileSync(path.join(FIXTURES, name), 'utf8').replace(/\r\n/g, '\n')
const MAIN = fixture('main.cml')
const SHAPES = fixture('shapes.cml')

await withServer(async (ls) => {
    const caps = ls.initializeResult.capabilities
    checkEq(caps.completionProvider.triggerCharacters, ['.', ':'], 'completion triggers')
    checkEq(caps.signatureHelpProvider.triggerCharacters, ['(', ','], 'signature help triggers')
    checkEq(caps.renameProvider, { prepareProvider: true }, 'rename with prepare')
    const legend = caps.semanticTokensProvider.legend
    check(legend.tokenModifiers.includes('mutable') && legend.tokenModifiers.includes('sync'),
        'the legend has the modifiers for var and sync', legend)

    ls.writeFile('shapes.cml', SHAPES)
    const src = marked(MAIN)
    const uri = ls.open('main.cml', MAIN)
    await ls.diagnosticsFor(uri)
    let version = 1
    // Replaces the text of the document; returns the new marked source.
    const edit = (text) => {
        const m = marked(text)
        ls.change(uri, m.text, ++version)
        return m
    }
    const complete = async (m, mark) => {
        const r = await ls.request('textDocument/completion', {
            textDocument: { uri }, position: m.at(mark),
        })
        return r.items
    }
    const labels = (items) => items.map((i) => i.label)

    // ---- completion: names in scope ----
    let m = edit(MAIN.replace('    return 0\n}', '    /*c*/\n    return 0\n}'))
    let items = await complete(m, 'c')
    let names = labels(items)
    for (const expected of ['p', 'q', 'total', 'sum', 'double', 'identity', 'add', 'bump',
        'norm2', 'scale', 'now', 'Pair', 'println', 'format', 'let', 'var', 'sync', 'return',
        'int', 'string']) {
        check(names.includes(expected), `completion offers '${expected}'`)
    }
    check(!names.includes('copy') && !names.includes('counter'),
        'names of other functions are not offered')
    check(!names.includes('__add__'), 'operator groups are not offered')
    checkEq(names.filter((n) => n === 'add').length, 1, 'overloads are one entry')
    const item = (label) => items.find((i) => i.label === label)
    checkEq(item('total').detail, 'int', 'a variable carries its type')
    checkEq(item('identity').detail, 'func identity::<T>(x: T): T', 'a function its header')
    checkEq(item('identity').documentation, 'Returns its argument.', 'and its documentation')
    check(item('norm2').detail.includes('(p: Point) => int'),
        'an imported function its signature', item('norm2'))
    check(item('println').detail.includes('(x: T, ...rest: U...) => T'),
        'a library function its signature', item('println'))
    checkEq(item('let').kind, 14, 'keywords are keywords')
    check(item('p').sortText < item('println').sortText, 'locals sort before library names')
    // A name is offered only after its declaration.
    m = edit(MAIN.replace('    let n = norm2(p)', '    /*c*/\n    let n = norm2(p)'))
    names = labels(await complete(m, 'c'))
    check(names.includes('p') && !names.includes('q') && !names.includes('n'),
        'a local is offered only after its declaration', names.slice(0, 12))

    // ---- completion: fields after a dot, in text that does not parse ----
    m = edit(MAIN.replace('    total += q.x', '    total += q./*c*/'))
    items = await complete(m, 'c')
    checkEq(items.map((i) => `${i.label}: ${i.detail}/${i.kind}`), ['x: int/5', 'y: int/5'],
        'struct fields after a dot, from the types of the last text that compiled')
    m = edit(MAIN.replace('    let first = pair.0', '    let first = pair./*c*/'))
    checkEq(labels(await complete(m, 'c')), ['0', '1'], 'tuple elements after a dot')
    // In text that compiles: the receiver may be any expression.
    m = edit(MAIN.replace('    let q = scale(p, 2)', '    let q = scale(p, 2)\n    let w = scale(p, 3).x/*c*/'))
    checkEq(labels(await complete(m, 'c')), ['x', 'y'], 'fields of the result of a call')

    // ---- completion: inside the brackets of a subscript ----
    // A subscript is an expression position like any other: the names in scope are offered in
    // it, and the parts of a slice are expressions too.
    m = edit(MAIN.replace('    let first = pair.0', '    let first = [sum][/*c*/0]'))
    names = labels(await complete(m, 'c'))
    check(names.includes('sum') && names.includes('total'), 'completion inside a subscript', names.slice(0, 12))
    m = edit(MAIN.replace('    let first = pair.0', '    let first = [sum][/*c*/:2]'))
    check(labels(await complete(m, 'c')).includes('sum'), 'completion before the colon of a slice')
    m = edit(MAIN.replace('    let first = pair.0', '    let first = [sum][2:/*c*/]'))
    check(labels(await complete(m, 'c')).includes('sum'), 'completion after the colon of a slice')

    // ---- completion: members of a module after `::` ----
    // A migrated module's names are declared in `stdlib/<m>.cml`; the signatures C++ computes
    // stay in C++. Both are exports of the one module, and both are offered.
    const membersOf = async (imports, expr) => {
        m = edit(MAIN.replace('import { now } from time', `import { now } from time\n${imports}`)
            .replace('    return 0\n}', `    let r = ${expr}::/*c*/\n    return 0\n}`))
        return complete(m, 'c')
    }
    items = await membersOf('import graph', 'graph')
    check(labels(items).includes('graph_of'), 'the exports of a module after `::`', labels(items).slice(0, 20))
    check((items.find((i) => i.label === 'graph_of') || {}).detail !== undefined,
        'with their signatures', items.find((i) => i.label === 'graph_of'))
    // Two declared members side by side: `graph_of` takes any function (`when is_function::<F>()`), and
    // `constant` takes a node and answers the type its caller names as a type argument.
    const graphOf = items.find((i) => i.label === 'graph_of')
    check(graphOf !== undefined && graphOf.detail.startsWith('(f: F)'),
        'a member declared over a type variable keeps its declared signature', graphOf)
    const constant = items.find((i) => i.label === 'constant')
    check(constant !== undefined && constant.detail.startsWith('(n: Node) => T?'),
        'a member whose answer type is a type argument carries its declared signature', constant)
    // A module whose every name is declared: no `exportEntity` answers for it any more.
    items = await membersOf('import re', 're')
    checkEq(labels(items), ['Match', 'find', 'find_all', 'has_match', 'replace', 'replace_with', 'split', 'validate'], 'the members of a module that is all declarations')
    checkEq(items.find((i) => i.label === 'validate').detail, '(pattern: string) => string!', 'with the declared signature')

    // ---- hover: the same module members ----
    const hoverMember = async (imports, call, needle, delta) => {
        m = edit(MAIN.replace('import { now } from time', `import { now } from time\n${imports}`)
            .replace('    return 0\n}', `    let r = ${call}\n    return 0\n}`))
        const r = await ls.request('textDocument/hover', {
            textDocument: { uri }, position: m.find(needle, 1, delta),
        })
        return r ? r.contents.value : null
    }
    let h = await hoverMember('import graph', 'graph::graph_of((x: float): float => x + 1.0)', 'graph::graph_of', 7)
    check(h !== null && h.includes('graph_of: (f: F) => Graph'),
        'hover on a member of a migrated module over a type variable', h)
    check(h.includes('Function of module `graph`'), 'naming the module it comes from', h)
    h = await hoverMember('import re', 're::has_match<"a">("b")', 're::has_match', 5)
    check(h !== null && h.includes('func has_match(text: string): bool'),
        'hover on a declared member of a migrated module', h)

    // ---- completion: the variants of an enum after `Type::` ----
    // A text that compiles first: a text being typed inherits the declaration types from it.
    const ENUMS = `export type Shape = enum {
    Circle(float),
    Rect { w: float, h: float },
    Empty,
}
`
    ls.writeFile('enums.cml', ENUMS)
    const withEnums = (body) => `import { norm2, scale } from shapes
import { Shape } from enums

type Local = enum { A(float), B { k: int }, C }

func main() sync {
    let a = Local::A(1.0)
    let s = Shape::Circle(1.0)
${body}
    return 0
}
`
    m = edit(withEnums(''))
    await ls.diagnosticsFor(uri, version)
    m = edit(withEnums('    let e = Local::/*c*/'))
    items = await complete(m, 'c')
    checkEq(items.map((i) => `${i.label}: ${i.detail}/${i.kind}`),
        ['A: A(float)/5', 'B: B { k: int }/5', 'C: C/5'],
        'the variants of an enum declared in this file, with how each is written')

    // Imported from another module: the module that declares it says which variants there are.
    m = edit(withEnums('    let e = Shape::/*c*/'))
    items = await complete(m, 'c')
    checkEq(items.map((i) => `${i.label}: ${i.detail}`),
        ['Circle: Circle(float)', 'Rect: Rect { w: float, h: float }', 'Empty: Empty'],
        'the variants of an imported enum')

    // ---- completion: nothing in comments and strings ----
    m = edit(MAIN.replace('/// Returns its argument.', '/// Returns its/*c*/ argument.'))
    checkEq(await complete(m, 'c'), [], 'no completion inside a comment')
    m = edit(MAIN.replace("identity('text')", "identity('te/*c*/xt')"))
    checkEq(await complete(m, 'c'), [], 'no completion inside a string')

    // ---- signature help ----
    const help = async (mm, mark) => ls.request('textDocument/signatureHelp', {
        textDocument: { uri }, position: mm.at(mark),
    })
    m = edit(MAIN.replace('    let sum = add(1, 2)', '    let sum = add(/*a*/1, /*b*/2)\n    let more = scale(p, /*s*/'))
    let sh = await help(m, 'a')
    checkEq(sh.signatures.map((s) => s.label),
        ['func add(a: int, b: int): int', 'func add(a: float, b: float): float'],
        'every overload is a signature')
    checkEq(sh.activeParameter, 0, 'the first argument is active')
    checkEq(sh.signatures[0].parameters.map((p) => sh.signatures[0].label.slice(...p.label)),
        ['a: int', 'b: int'], 'the parameters are ranges of the label')
    sh = await help(m, 'b')
    checkEq(sh.activeParameter, 1, 'after a comma the second')
    sh = await help(m, 's')
    check(sh.signatures[0].label.includes('scale') && sh.activeParameter === 1,
        'an imported function, in a call that is not finished', sh)
    checkEq(sh.signatures[0].parameters.map((p) => sh.signatures[0].label.slice(...p.label)),
        ['p: Point', 'k: int'], 'its parameters')
    m = edit(MAIN.replace("println(format<i, s, d>('{} {} {}'))", 'println(/*p*/)'))
    sh = await help(m, 'p')
    check(sh.signatures[0].label.startsWith('println: '), 'a library function', sh)
    m = edit(MAIN.replace('    return 0\n}', '    let z = (1, /*t*/2)\n    return 0\n}'))
    checkEq(await help(m, 't'), null, 'no signature help inside a tuple')
    // A member of a migrated module: one over a type variable and one over plain types.
    m = edit(MAIN.replace('import { now } from time', 'import { now } from time\nimport graph')
        .replace('    return 0\n}', '    let r = graph::graph_of(/*m*/(x: float): float => x + 1.0)\n    return 0\n}'))
    sh = await help(m, 'm')
    check(sh !== null && sh.signatures[0].label.includes('graph_of: (f: F)'),
        'signature help on a member of a migrated module over a type variable', sh)
    checkEq(sh.activeParameter, 0, 'its first argument is active')
    m = edit(MAIN.replace('import { now } from time', 'import { now } from time\nimport math')
        .replace('    return 0\n}', '    let r = math::abs(/*m*/1)\n    return 0\n}'))
    sh = await help(m, 'm')
    check(sh !== null && sh.signatures.some((s) => s.label.includes('abs: (x: T) => T')),
        'signature help on a generic member of a migrated module, one signature per declaration', sh)

    // ---- rename ----
    edit(MAIN)
    await ls.diagnosticsFor(uri, version)
    const pos = (needle, nth = 1, delta = 0) => ({ textDocument: { uri }, position: src.find(needle, nth, delta) })
    let prep = await ls.request('textDocument/prepareRename', pos('let sum', 1, 4))
    checkEq([fmtRange(prep.range), prep.placeholder], ['32:8-32:11', 'sum'], 'prepareRename gives the name')
    let we = await ls.request('textDocument/rename', { ...pos('let sum', 1, 4), newName: 'result' })
    checkEq(we.changes[uri].map((e) => `${fmtRange(e.range)} ${e.newText}`),
        ['32:8-32:11 result', '37:19-37:22 result'], 'a local: its declaration and its uses')
    // From a use, and for a parameter.
    we = await ls.request('textDocument/rename', { ...pos('counter += by'), newName: 'cell' })
    checkEq(we.changes[uri].map((e) => fmtRange(e.range)), ['19:14-19:21', '20:4-20:11', '21:11-21:18'],
        'a var parameter, renamed from a use')
    // Every overload of a function is renamed with it.
    we = await ls.request('textDocument/rename', { ...pos('add(1, 2)'), newName: 'plus' })
    checkEq(we.changes[uri].map((e) => fmtRange(e.range)),
        ['11:5-11:8', '15:5-15:8', '32:14-32:17', '33:15-33:18'],
        'overloads share their name: all are renamed, with the calls of each')
    we = await ls.request('textDocument/rename', { ...pos('): T', 1, 3), newName: 'U' })
    checkEq(we.changes[uri].map((e) => fmtRange(e.range)), ['6:16-6:17', '6:22-6:23', '6:26-6:27'],
        'a type parameter: its head declaration and its uses')
    we = await ls.request('textDocument/rename', { ...pos('type Pair', 1, 5), newName: 'Entry' })
    checkEq(we.changes[uri].map((e) => fmtRange(e.range)), ['3:5-3:9', '34:14-34:18'], 'a type alias')

    const refused = async (params, reason, what) => {
        const r = await ls.requestRaw('textDocument/rename', params)
        check(r.error && r.error.message.includes(reason), what, r)
    }
    await refused({ ...pos('let sum', 1, 4), newName: 'd' }, "'d' is already", 'a name already in use is refused')
    await refused({ ...pos('let sum', 1, 4), newName: 'let' }, 'keyword', 'a keyword is refused')
    await refused({ ...pos('let sum', 1, 4), newName: '2x' }, 'not an identifier', 'a non-identifier is refused')
    await refused({ ...pos('norm2(p)'), newName: 'len2' }, 'outside the workspace', 'a function outside the workspace is refused')
    await refused({ ...pos('println'), newName: 'say' }, 'not declared in a source file', 'a library function is refused')
    await refused({ ...pos('q.x', 1, 2), newName: 'xx' }, 'field', 'a field is refused')
    await refused({ ...pos('func main', 1, 5), newName: 'start' }, 'entry', 'main is refused')
    let r = await ls.requestRaw('textDocument/prepareRename', pos('func main', 1, 0))
    check(r.error && r.error.message.includes('nothing here'), 'prepareRename on a keyword is refused', r)

    // A generic function that is never called: the compiler has not seen the uses in it.
    m = edit(`func unused::<T>(x: T): T {
    let /*y*/y = x
    return y
}

func main() sync {
    return 0
}
`)
    await refused({ textDocument: { uri }, position: m.at('y'), newName: 'z' },
        'cannot be confirmed', 'a name used only in code the compiler did not build is refused')
    // Exported functions are now renamed across the workspace after validation.
    m = edit('export func /*f*/twice(x: int): int {\n    return x * 2\n}\n\nfunc main() sync {\n    return twice(1)\n}\n')
    we = await ls.request('textDocument/rename', {
        textDocument: { uri }, position: m.at('f'), newName: 'double',
    })
    checkEq(we.changes[uri].map((e) => `${fmtRange(e.range)} ${e.newText}`),
        ['5:11-5:16 double', '0:12-0:17 double'],
        'an exported function: its declaration and call are renamed together')
    // A text with an error.
    m = edit('func main() sync {\n    let /*a*/a = 1\n    println(nope)\n    return a\n}\n')
    await refused({ textDocument: { uri }, position: m.at('a'), newName: 'b' },
        'has errors', 'a rename in a file with errors is refused')

    // ---- semantic tokens ----
    edit(MAIN)
    const tokens = await ls.request('textDocument/semanticTokens/full', { textDocument: { uri } })
    const lines = MAIN.split('\n')
    const decoded = []
    let line = 0
    let col = 0
    for (let i = 0; i < tokens.data.length; i += 5) {
        line += tokens.data[i]
        col = tokens.data[i] === 0 ? col + tokens.data[i + 1] : tokens.data[i + 1]
        const mods = legend.tokenModifiers.filter((_, bit) => tokens.data[i + 4] & (1 << bit))
        decoded.push({
            line,
            text: lines[line].slice(col, col + tokens.data[i + 2]),
            type: legend.tokenTypes[tokens.data[i + 3]],
            mods: mods.join('+'),
        })
    }
    const tok = (lineNo, text) => {
        const t = decoded.find((x) => x.line === lineNo && x.text === text)
        return t ? `${t.type}${t.mods ? ' ' + t.mods : ''}` : 'none'
    }
    checkEq(tok(19, 'bump'), 'function declaration', 'a function is marked')
    checkEq(tok(19, 'counter'), 'parameter declaration+mutable', 'a var parameter is mutable')
    checkEq(tok(19, 'by'), 'parameter declaration+readonly', 'a plain parameter is not')
    checkEq(tok(25, 'p'), 'variable declaration+readonly', 'let: readonly')
    checkEq(tok(28, 'total'), 'variable declaration+mutable', 'var: mutable')
    checkEq(tok(29, 'total'), 'variable mutable', 'a use of a var')
    checkEq(tok(29, 'x'), 'property', 'a field')
    checkEq(tok(29, '+='), 'operator', 'an operator')
    checkEq(tok(26, 'norm2'), 'function', 'a call of an imported function')
    checkEq(tok(0, 'norm2'), 'function declaration', 'the import of a function')
    checkEq(tok(3, 'Pair'), 'type declaration', 'a type alias')
    checkEq(tok(34, 'Pair'), 'type', 'its use')
    checkEq(tok(3, 'int'), 'type defaultLibrary', 'a built-in type')
    checkEq(tok(6, 'T'), 'typeParameter declaration', 'a type variable')
    checkEq(tok(38, 'println'), 'function defaultLibrary', 'a library function')
    checkEq(tok(38, 'i'), 'variable readonly', 'a with-argument')
    checkEq(tok(25, '3'), 'number', 'a number')
    checkEq(tok(31, "'text'"), 'string', 'a string')
    checkEq(tok(24, 'main'), 'function declaration', 'main')
    checkEq(tok(1, 'anything'), 'none', 'nothing for what is not there')
    // A call of a function with a var parameter: the use is marked too.
    m = edit(MAIN.replace('    return 0\n}', '    bump(total, 1)\n    return 0\n}'))
    const t2 = await ls.request('textDocument/semanticTokens/full', { textDocument: { uri } })
    check(t2.data.length > tokens.data.length, 'tokens of the edited text', t2.data.length)
})
