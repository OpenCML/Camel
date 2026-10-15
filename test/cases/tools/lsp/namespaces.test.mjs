// Namespace members are shown by their full names (design principles 2.13): hover, the
// outline, completion (the label is the full name; what is written is the member's own name,
// which is in scope there or follows the `N::` already typed).

import { withServer, check, checkEq, marked } from '../../../tools/lsp/client.mjs'

const MAIN = `module main

namespace Tools {
    /// One more.
    export func inc(x: int): int { return x + 1 }
    export type Pair = (int, int)
    export let limit = 10

    namespace Deep {
        export func twice(x: int): int { return x * 2 }
    }

    func use(): int { return inc(1) }
}

namespace A::B {
    export func f(): int { return 1 }
}

func main(): int sync {
    println(Tools::inc(1), Tools::Deep::twice(2), A::B::f(), Tools::limit)
    return 0
}
`

await withServer(async (ls) => {
    const src = marked(MAIN)
    const uri = ls.open('main.cml', MAIN)
    const at = (needle, nth = 1, delta = 0) => ({
        textDocument: { uri },
        position: src.find(needle, nth, delta),
    })

    const pushed = await ls.diagnosticsFor(uri)
    checkEq(pushed.diagnostics.length, 0, 'the program compiles', pushed.diagnostics)

    // ---- hover: the full name ----
    const hover = async (needle, nth, delta) => {
        const h = await ls.request('textDocument/hover', at(needle, nth, delta))
        return h ? h.contents.value : null
    }
    let h = await hover('Tools::inc(1)', 1, 8)
    check(h !== null && h.includes('func Tools::inc(x: int): int'), 'a namespace function by its full name', h)
    h = await hover('Tools::Deep::twice', 1, 14)
    check(h !== null && h.includes('func Tools::Deep::twice('), 'a nested namespace too', h)
    h = await hover('A::B::f', 1, 6)
    check(h !== null && h.includes('func A::B::f()'), 'a namespace written as a path', h)
    h = await hover('Tools::limit', 1, 8)
    check(h !== null && h.includes('let Tools::limit'), 'a value of a namespace', h)

    // ---- outline: the members by their full names ----
    const symbols = await ls.request('textDocument/documentSymbol', { textDocument: { uri } })
    const tools = symbols.find((s) => s.name === 'Tools')
    check(tools !== undefined, 'the namespace itself is in the outline', symbols.map((s) => s.name))
    const members = tools.children.map((s) => s.name)
    for (const name of ['Tools::inc', 'Tools::Pair', 'Tools::limit', 'Tools::Deep', 'Tools::use']) {
        check(members.includes(name), `the outline shows '${name}'`, members)
    }
    const deep = tools.children.find((s) => s.name === 'Tools::Deep')
    check(deep.children.some((s) => s.name === 'Tools::Deep::twice'), 'nested members by their full names',
        deep.children)
    check(symbols.some((s) => s.name === 'A::B'), 'a namespace written as a path', symbols.map((s) => s.name))

    // ---- completion ----
    const complete = async (text, needle, delta) => {
        const doc = ls.open('complete.cml', text)
        const s = marked(text)
        const r = await ls.request('textDocument/completion', {
            textDocument: { uri: doc },
            position: s.find(needle, 1, delta),
        })
        return Array.isArray(r) ? r : r.items
    }
    // Inside the namespace: shown by the full name, written by its own.
    let items = await complete(MAIN.replace('return inc(1)', 'return in(1)'), 'in(1)', 2)
    let item = items.find((i) => i.label === 'Tools::inc')
    check(item !== undefined, 'in scope inside the namespace, labelled with the full name',
        items.map((i) => i.label))
    checkEq(item.insertText, 'inc', 'and written by its own name')
    // After `Tools::`: the members by their full names, written by the rest.
    items = await complete(MAIN.replace('Tools::inc(1), ', 'Tools::'), 'Tools::', 7)
    item = items.find((i) => i.label === 'Tools::inc')
    check(item !== undefined, 'after Tools:: the members by their full names', items.map((i) => i.label))
    checkEq(item.insertText, 'inc', 'and written by the rest of the name')
})
