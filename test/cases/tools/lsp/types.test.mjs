// Where a type name's declaration is, when the declaration is in another module: the
// compiler records the place while it declares the type (Module::noteTypeDeclarationOrigin)
// and the language server answers from that record, not by resolving the name again.

import { withServer, check, checkEq, fmtRange, marked } from '../../../tools/lsp/client.mjs'

const LIB = `module type_lib

export type Point = { x: int, y: int }

export func norm(p: Point): int {
    return p.x * p.x + p.y * p.y
}
`

const MAIN = `module type_main

import { Point, norm } from type_lib

func main() sync {
    let p: Point = { x: 3, y: 4 }
    let q: Point = { x: 1, y: 2 }
    println(norm(p), norm(q))
    return 0
}
`

await withServer(async (ls) => {
    const libUri = ls.writeFile('type_lib.cml', LIB)
    const src = marked(MAIN)
    const uri = ls.open('type_main.cml', MAIN)
    const at = (needle, nth = 1, delta = 0) => ({
        textDocument: { uri },
        position: src.find(needle, nth, delta),
    })

    const pushed = await ls.diagnosticsFor(uri)
    check(pushed !== null && pushed.diagnostics.length === 0,
        'the program of two files compiles without diagnostics', pushed && pushed.diagnostics)

    // Both `Point`s name the exported type of the other module; the definition is the type's
    // declaration there, not the import declaration in this file.
    for (const nth of [1, 2]) {
        const d = await ls.request('textDocument/definition', at('let ', nth, 7))
        checkEq(d.length, 1, `one definition for the type used at line ${nth}`)
        checkEq(d[0].uri, libUri, 'in the module that declares it')
        checkEq(fmtRange(d[0].range), '2:12-2:17', 'at the name of the type declaration')
    }

    // Completion of the imported type name, with the kind of a type.
    const list = await ls.request('textDocument/completion', at('let ', 1, 9))
    const items = list.items ?? list
    check(items.some((item) => item.label === 'Point' && item.kind === 22),
        'an imported type completes as a type', items)
})