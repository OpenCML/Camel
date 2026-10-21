// The capabilities camel-ls had before the query layer: lifecycle, document synchronisation,
// pull diagnostics and whole-document formatting.

import { withServer, check, checkEq, fmtRange } from '../../../tools/lsp/client.mjs'

const GOOD = `module main

func main() sync {
    println('hi')
    return 0
}
`

const BAD = `module main

func main() sync {
    let r = nope + 1
    println(other_missing)
    return 0
}
`

await withServer(async (ls) => {
    const caps = ls.initializeResult.capabilities
    checkEq(ls.initializeResult.serverInfo.name, 'camel-ls', 'serverInfo.name')
    check(caps.textDocumentSync.openClose === true, 'openClose is announced')
    check(caps.documentFormattingProvider === true, 'formatting is announced')
    check(caps.diagnosticProvider !== undefined, 'pull diagnostics are announced')

    // ---- pull diagnostics ----
    const uri = ls.open('main.cml', GOOD)
    let report = await ls.request('textDocument/diagnostic', { textDocument: { uri } })
    checkEq(report.kind, 'full', 'the report is a full report')
    checkEq(report.items, [], 'a correct program has no diagnostics')

    ls.change(uri, BAD, 2)
    report = await ls.request('textDocument/diagnostic', { textDocument: { uri } })
    checkEq(report.items.length, 2, 'two independent errors are reported', report.items)
    const first = report.items[0]
    checkEq(fmtRange(first.range), '3:12-3:16', 'the range of the first error is the name')
    checkEq(first.severity, 1, 'severity error')
    checkEq(first.source, 'Camel', 'source')
    check(/^0x[0-9A-F]{8}$/.test(first.code), 'the code is the catalog code', first.code)
    checkEq(first.data.name, 'SemanticDiag::UnresolvedReference', 'data.name is the catalog name')
    check(first.message.includes('nope'), 'the message names the reference', first.message)

    // A syntax error: one diagnostic, the file is not compiled further.
    ls.change(uri, 'module main\n\nfunc main( {\n', 3)
    report = await ls.request('textDocument/diagnostic', { textDocument: { uri } })
    check(report.items.length >= 1, 'a syntax error is reported', report.items)

    // A document that is not open has an empty report.
    report = await ls.request('textDocument/diagnostic', {
        textDocument: { uri: ls.uriOf('never-opened.cml') },
    })
    checkEq(report.items, [], 'an unknown document has no diagnostics')

    // ---- formatting ----
    const messy = 'module main\n\nfunc main()   sync {\n      println( "hi" )\n  return 0\n}\n'
    ls.change(uri, messy, 4)
    let edits = await ls.request('textDocument/formatting', {
        textDocument: { uri },
        options: { tabSize: 4, insertSpaces: true },
    })
    checkEq(edits.length, 1, 'formatting answers with one edit')
    checkEq(fmtRange(edits[0].range), '0:0-6:0', 'the edit replaces the whole document')
    check(edits[0].newText.includes("\n    println('hi')\n"), 'the text is formatted', edits[0].newText)

    // A subscript is formatted by its own rule: the colons are kept where they were written and
    // one space follows a colon that a part follows (design principles 5.4).
    ls.change(uri, 'module main\n\nfunc part(xs: int[], a: int): int[] {\nreturn xs[a : 2]\n}\n', 5)
    edits = await ls.request('textDocument/formatting', {
        textDocument: { uri },
        options: { tabSize: 4, insertSpaces: true },
    })
    checkEq(edits.length, 1, 'a slice is formatted')
    check(edits[0].newText.includes('\n    return xs[a: 2]\n'), 'the parts keep their place',
        edits[0].newText)

    // A document with a syntax error is not formatted.
    ls.change(uri, 'func main( {', 6)
    edits = await ls.request('textDocument/formatting', {
        textDocument: { uri },
        options: { tabSize: 4, insertSpaces: true },
    })
    checkEq(edits, [], 'a document that does not parse gets no edits')

    // ---- close ----
    ls.notify('textDocument/didClose', { textDocument: { uri } })
    report = await ls.request('textDocument/diagnostic', { textDocument: { uri } })
    checkEq(report.items, [], 'a closed document has no diagnostics')

    // ---- unknown request ----
    const resp = await ls.requestRaw('camel/noSuchMethod', {})
    checkEq(resp.error?.code, -32601, 'an unknown request is answered with MethodNotFound')
})
