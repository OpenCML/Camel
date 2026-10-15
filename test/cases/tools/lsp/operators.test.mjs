// Operator overloads of the program (design principles 2.12): hover and definition on the
// operator of a call that took one, the declaration in the outline, the formatter's spelling.

import { withServer, check, checkEq, fmtRange, marked } from '../../../tools/lsp/client.mjs'

const MAIN = `module main

type Vec = { x: float, y: float }

/// Adds two vectors.
func operator +(a: Vec, b: Vec): Vec {
    return { x: a.x + b.x, y: a.y + b.y }
}

func operator -(a: Vec): Vec {
    return { x: 0.0 - a.x, y: 0.0 - a.y }
}

func main(): int sync {
    let v: Vec = { x: 1.0, y: 2.0 }
    let w = v + v
    let n = -w
    println(w.x, n.y, 1 + 2)
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

    // ---- hover: the overload the operator called ----
    const hover = async (needle, nth, delta) => {
        const h = await ls.request('textDocument/hover', at(needle, nth, delta))
        return h ? h.contents.value : null
    }
    let h = await hover('v + v', 1, 2)
    check(h !== null && h.includes('operator +') && h.includes('Vec'),
        'the operator of a call that took an overload of the program shows it', h)
    h = await hover('-w', 1, 0)
    check(h !== null && h.includes('operator -'), 'a unary operator too', h)
    h = await hover('1 + 2', 1, 2)
    check(h !== null && h.includes('operator +') && h.includes('int'),
        'a built-in operator keeps its hover', h)

    // ---- definition: the declaration of the overload ----
    const def = async (needle, nth, delta) =>
        ls.request('textDocument/definition', at(needle, nth, delta))
    let d = await def('v + v', 1, 2)
    checkEq(d.length, 1, 'one definition for the operator', d)
    checkEq(fmtRange(d[0].range), '5:5-5:13', 'at the declaration of `operator +`')
    d = await def('-w', 1, 0)
    checkEq(d.map((l) => fmtRange(l.range)), ['9:5-9:13'], 'the unary overload')
    d = await def('1 + 2', 1, 2)
    checkEq(d, [], 'a built-in operator has no definition in source')

    // ---- outline ----
    const symbols = await ls.request('textDocument/documentSymbol', { textDocument: { uri } })
    const names = symbols.map((s) => s.name)
    check(names.filter((n) => n === 'operator').length === 2,
        'the two overloads are in the outline, named by their `operator` word', names)

    // ---- formatting: `func operator +(` ----
    const edits = await ls.request('textDocument/formatting', {
        textDocument: { uri },
        options: { tabSize: 4, insertSpaces: true },
    })
    const formatted = edits.length === 0 ? MAIN : edits[0].newText
    check(formatted.includes('func operator +(a: Vec, b: Vec): Vec'), 'the formatter keeps the form',
        formatted)
})
