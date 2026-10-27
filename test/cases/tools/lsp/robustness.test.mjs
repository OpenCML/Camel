// Nothing a client sends may end the server or leave a request unanswered: malformed frames,
// empty and very large documents, text that is not UTF-8, positions outside the text. And the
// positions of the protocol count UTF-16 code units, the compiler counts code points and
// stores bytes: lines with Chinese text and emoji check the conversions.

import { withServer, check, checkEq, fmtRange, marked } from '../../../tools/lsp/client.mjs'

const POSITION_REQUESTS = [
    'textDocument/hover',
    'textDocument/definition',
    'textDocument/references',
    'textDocument/documentHighlight',
    'textDocument/completion',
    'textDocument/signatureHelp',
    'textDocument/prepareRename',
    'textDocument/rename',
    'camel/graph',
]
const DOCUMENT_REQUESTS = [
    'textDocument/documentSymbol',
    'textDocument/semanticTokens/full',
    'textDocument/formatting',
    'textDocument/diagnostic',
]

await withServer(async (ls) => {
    // Every request on a document answers (a result or an error of the server's own), in time.
    const answered = (r) => r.result !== undefined || (r.error && r.error.code !== -1 && r.error.code !== -2)
    const exercise = async (uri, positions, what, methods = POSITION_REQUESTS) => {
        for (const method of DOCUMENT_REQUESTS) {
            const r = await ls.requestRaw(method, {
                textDocument: { uri }, options: { tabSize: 4, insertSpaces: true },
            })
            check(answered(r), `${what}: ${method} answers`, r.error)
        }
        for (const position of positions) {
            for (const method of methods) {
                const r = await ls.requestRaw(method, {
                    textDocument: { uri }, position, newName: 'renamed',
                    context: { includeDeclaration: true },
                })
                check(answered(r), `${what}: ${method} at ${position.line}:${position.character} answers`, r.error)
            }
        }
    }
    const far = [
        { line: 0, character: 0 },
        { line: 0, character: 100000 },
        { line: 100000, character: 0 },
        { line: 4000000000, character: 4000000000 },
        { line: -1, character: -5 },
    ]

    // ---- malformed input ----
    ls.writeBytes('Content-Length: 12\r\n\r\n{not json!!}')
    ls.writeBytes('Content-Length: abc\r\n\r\n')
    ls.writeBytes('Garbage-Header: 1\r\n\r\n')
    ls.sendRaw([1, 2, 3])
    ls.sendRaw({ jsonrpc: '2.0', id: 9001 })
    ls.sendRaw({ jsonrpc: '2.0', method: 42, id: 9002 })
    ls.notify('textDocument/didOpen', {})
    ls.notify('textDocument/didChange', { textDocument: { uri: 'file:///nowhere.cml' }, contentChanges: 7 })
    let r = await ls.requestRaw('textDocument/hover', {})
    checkEq(r.error?.code, -32602, 'a request without parameters is answered with InvalidParams', r)
    r = await ls.requestRaw('textDocument/hover', { textDocument: { uri: 5 }, position: 'x' })
    checkEq(r.error?.code, -32602, 'parameters of the wrong type too', r)
    r = await ls.requestRaw('textDocument/hover', {
        textDocument: { uri: ls.uriOf('never-opened.cml') }, position: { line: 0, character: 0 },
    })
    checkEq(r.result, null, 'a document that is not open has no hover')

    // ---- an empty document ----
    let uri = ls.open('empty.cml', '')
    let pushed = await ls.diagnosticsFor(uri)
    check(pushed !== null, 'an empty document gets diagnostics published', pushed)
    await exercise(uri, far, 'empty document')

    // ---- half-written text ----
    const halves = ['func', 'func main(', 'func main() sync {\n    let x = ',
        'import { } from', '}}}}', '((((((((', "let s = 'unterminated", '/* open comment',
        'let a = 1 +\n', 'type T = {', 'match x {', '@', 'x.', 'a::', 'func f(x: ']
    for (const [i, text] of halves.entries()) {
        uri = ls.open(`half${i}.cml`, text)
        pushed = await ls.diagnosticsFor(uri)
        check(pushed !== null && pushed.diagnostics.length >= 1,
            `half-written text ${JSON.stringify(text)} is diagnosed`, pushed)
        const lines = text.split('\n')
        await exercise(uri, [
            { line: lines.length - 1, character: lines[lines.length - 1].length },
            { line: 0, character: 1 },
        ], `half-written ${JSON.stringify(text)}`)
        ls.notify('textDocument/didClose', { textDocument: { uri } })
        await ls.diagnosticsFor(uri)
    }

    // ---- what is recovered from a document with syntax errors ----
    // Two independent mistakes, in two functions; the rest of the file is intact. Each mistake
    // is reported once, where it is, and nothing else is; the outline still has every
    // declaration, and navigation works in the functions around the errors.
    {
        const broken = `func first(a: int): int {
    let x = (a + 1
    return x
}

func second(n: int): int {
    let y = if n > 0 n else 0
    return y
}

func third(k: int): int {
    let local = k * 2
    return local + 1
}

func main(): int sync {
    println(third(1))
    return 0
}
`
        uri = ls.open('recovery.cml', broken)
        pushed = await ls.diagnosticsFor(uri)
        const lines = pushed.diagnostics.map((d) => d.range.start.line)
        checkEq(JSON.stringify(lines), JSON.stringify([2, 6]),
            'two independent syntax errors are each reported once', pushed.diagnostics)
        check(/'\)'/.test(pushed.diagnostics[0].message) && /'then'/.test(pushed.diagnostics[1].message),
            'each says what is missing', pushed.diagnostics.map((d) => d.message))
        const outline = await ls.request('textDocument/documentSymbol', { textDocument: { uri } })
        checkEq(JSON.stringify(outline.map((s) => s.name)), JSON.stringify(['first', 'second', 'third', 'main']),
            'the outline of a document with syntax errors has every function')
        const use = broken.split('\n').findIndex((l) => l.includes('return local'))
        const d = await ls.request('textDocument/definition', {
            textDocument: { uri }, position: { line: use, character: 12 },
        })
        checkEq(d.length === 1 && d[0].range.start.line, use - 1,
            'a local is found in a function after the errors')
        const completion = await ls.request('textDocument/completion', {
            textDocument: { uri }, position: { line: use, character: 11 },
        })
        const items = Array.isArray(completion) ? completion : completion.items
        check(items.some((i) => i.label === 'local') && items.some((i) => i.label === 'third'),
            'completion after the errors offers the local and the functions of the file')
        ls.notify('textDocument/didClose', { textDocument: { uri } })
        await ls.diagnosticsFor(uri)
    }

    // ---- positions in UTF-16 ----
    // Line 3 has Chinese text (3 bytes, 1 unit each) and emoji (4 bytes, 2 units each) before
    // the names that are asked about.
    const src = marked(`func main() sync {
    let zh = '中文😀'; let /*w*/wide = '😀😀'; let after = /*u*/wide
    println(/*a*/after, undefined_name)
    return 0
}
`)
    uri = ls.open('unicode.cml', src.text)
    pushed = await ls.diagnosticsFor(uri)
    checkEq(pushed.diagnostics.length, 1, 'one error in the unicode document', pushed.diagnostics)
    const line4 = src.text.split('\n')[2]
    const start = line4.indexOf('undefined_name')
    checkEq(fmtRange(pushed.diagnostics[0].range), `2:${start}-2:${start + 'undefined_name'.length}`,
        'the range of a diagnostic on a plain line')
    const span = (pos, length) => fmtRange({ start: pos, end: { line: pos.line, character: pos.character + length } })
    const h = await ls.request('textDocument/hover', { textDocument: { uri }, position: src.at('u') })
    check(h && h.contents.value.includes('let wide: string'), 'hover after wide characters', h)
    checkEq(fmtRange(h.range), span(src.at('u'), 4), 'its range is in UTF-16 units')
    let d = await ls.request('textDocument/definition', { textDocument: { uri }, position: src.at('u') })
    checkEq(d.map((l) => fmtRange(l.range)), [span(src.at('w'), 4)],
        'the definition, on the same line after an emoji, is where the editor counts it')
    d = await ls.request('textDocument/references', {
        textDocument: { uri }, position: src.at('a'), context: { includeDeclaration: true },
    })
    checkEq(d.length, 2, 'references of a name declared after wide characters', d)
    // A diagnostic after wide characters on its own line.
    const src2 = marked(`func main() sync {
    let s = '日本語😀'; println(/*e*/missing)
    return 0
}
`)
    ls.change(uri, src2.text, 2)
    pushed = await ls.diagnosticsFor(uri, 2)
    checkEq(fmtRange(pushed.diagnostics[0].range), span(src2.at('e'), 7),
        'the range of a diagnostic after Chinese text and an emoji')
    // A position inside a surrogate pair, and an incremental edit that replaces the pair.
    const emojiAt = src2.text.split('\n')[1].indexOf('😀')
    await exercise(uri, [{ line: 1, character: emojiAt + 1 }], 'inside a surrogate pair')
    ls.notify('textDocument/didChange', {
        textDocument: { uri, version: 3 },
        contentChanges: [{
            range: { start: { line: 1, character: emojiAt }, end: { line: 1, character: emojiAt + 2 } },
            text: '语',
        }],
    })
    pushed = await ls.diagnosticsFor(uri, 3)
    checkEq(fmtRange(pushed.diagnostics[0].range),
        span({ line: 1, character: src2.at('e').character - 1 }, 7),
        'replacing an emoji by a range edit moves what follows by one unit')
    await exercise(uri, far, 'unicode document')

    // ---- text that is not UTF-8 ----
    // JSON is UTF-8, so such text can only arrive as a frame that is not valid JSON text.
    const bad = Buffer.concat([
        Buffer.from('{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///latin1.cml","languageId":"camel","version":1,"text":"let s = \''),
        Buffer.from([0xe9, 0xff, 0xfe]),
        Buffer.from('\'"}}}'),
    ])
    ls.writeBytes(`Content-Length: ${bad.length}\r\n\r\n`)
    ls.writeBytes(bad)
    r = await ls.requestRaw('textDocument/hover', {
        textDocument: { uri: 'file:///latin1.cml' }, position: { line: 0, character: 0 },
    })
    check(answered(r), 'a frame that is not UTF-8 is dropped and the server goes on', r)
    // A lone surrogate is a legal JSON escape that is not text: the frame is refused or the
    // document diagnosed, and either way the server goes on.
    uri = ls.open('surrogate.cml',
        'func main() sync {\n    let s = "' + String.fromCharCode(0xd83d) + '"\n    return nope\n}\n')
    await exercise(uri, [{ line: 1, character: 14 }], 'lone surrogate')

    // ---- a very large document ----
    let big = ''
    for (let i = 0; i < 4000; i++) {
        big += `func f${i}(x: int): int {\n    let y = x + ${i}\n    return y * 2\n}\n\n`
    }
    big += 'func main() sync {\n    return f0(1) + f3999(2)\n}\n'
    const bigLines = big.split('\n').length
    uri = ls.open('big.cml', big)
    const started = Date.now()
    pushed = await ls.diagnosticsFor(uri, undefined, 100000)
    check(pushed !== null && pushed.diagnostics.length === 0,
        `a document of ${bigLines} lines is compiled`, pushed && pushed.diagnostics.slice(0, 3))
    console.error(`large document: ${bigLines} lines, first diagnostics after ${Date.now() - started} ms`)
    d = await ls.request('textDocument/definition', {
        textDocument: { uri }, position: { line: bigLines - 3, character: 22 },
    }, 100000)
    checkEq(d.length === 1 && d[0].range.start.line, 3999 * 5, 'definition across the large document')
    const symbols = await ls.request('textDocument/documentSymbol', { textDocument: { uri } }, 100000)
    checkEq(symbols.length, 4001, 'its outline')
    // A burst of edits is compiled once: only the last version is published.
    for (let v = 2; v <= 12; v++) {
        ls.notify('textDocument/didChange', {
            textDocument: { uri, version: v },
            contentChanges: [{ range: { start: { line: 0, character: 11 }, end: { line: 0, character: 11 } }, text: ' ' }],
        })
    }
    pushed = await ls.diagnosticsFor(uri, undefined, 100000)
    checkEq(pushed.version, 12, 'a burst of edits is compiled once, for the last version')
    ls.notify('textDocument/didClose', { textDocument: { uri } })

    // ---- cancellation ----
    uri = ls.open('cancel.cml', big)
    const first = ls.requestRaw('textDocument/documentSymbol', { textDocument: { uri } }, 100000)
    const second = ls.requestRaw('textDocument/semanticTokens/full', { textDocument: { uri } }, 100000)
    ls.notify('$/cancelRequest', { id: ls.nextId - 1 })
    await first
    r = await second
    checkEq(r.error?.code, -32800, 'a request cancelled while it waited is answered with RequestCancelled', r.error)
    ls.notify('textDocument/didClose', { textDocument: { uri } })

    // ---- the server is still well ----
    uri = ls.open('ok.cml', "func main() sync {\n    println('ok')\n    return 0\n}\n")
    pushed = await ls.diagnosticsFor(uri)
    checkEq(pushed.diagnostics, [], 'after all of this a correct document has no diagnostics')
})
