// The fixes the compiler attaches to its diagnostics reach the editor: `textDocument/diagnostic`
// carries them in `data.fixes`, and `textDocument/codeAction` hands them out as quick fixes, so a
// reader and an AI in the editor get the change the compiler knows the text of.

import { withServer, check, checkEq, fmtRange } from '../../../tools/lsp/client.mjs'

// A missing ',' between two parameters: the compiler knows the text to write, so the diagnostic
// carries a fix, and the file is repaired by applying it.
const MISSING_COMMA = `func add(a: int b: int): int { return a + b }

func main(): int sync {
    return add(1, 2)
}
`

// A name that resolves to nothing is the one thing the compiler repairs by itself, by looking for
// the name that was meant among the names in scope: that candidate search is a code action too,
// and it is not a fix the compiler attached (`data.fixes` is absent).
const TYPO = `func main(): int sync {
    let counter = 1
    return countr
}
`

/// The same value with every object's keys in a fixed order, so that a check compares the shape the
/// protocol names and not the order a server happened to write it in.
function canonical(value) {
    if (Array.isArray(value)) return value.map(canonical)
    if (value && typeof value === 'object') {
        const out = {}
        for (const key of Object.keys(value).sort()) out[key] = canonical(value[key])
        return out
    }
    return value
}

function checkSame(actual, expected, what) {
    return checkEq(canonical(actual), canonical(expected), what)
}

/// The byte offset of a line / UTF-16 column pair, the way the protocol counts them.
function offsetOfUtf16(text, line, character) {
    let at = 0
    for (let l = 0; l < line; l += 1) at = text.indexOf('\n', at) + 1
    return at + character
}

/// The text with one workspace edit of this document applied.
function applyEdits(text, edits) {
    const spans = edits
        .map((e) => ({
            begin: offsetOfUtf16(text, e.range.start.line, e.range.start.character),
            end: offsetOfUtf16(text, e.range.end.line, e.range.end.character),
            newText: e.newText,
        }))
        .sort((a, b) => b.begin - a.begin)
    let out = text
    for (const span of spans) out = out.slice(0, span.begin) + span.newText + out.slice(span.end)
    return out
}

await withServer(async (ls) => {
    const uri = ls.open('fixes.cml', MISSING_COMMA)
    let report = await ls.request('textDocument/diagnostic', { textDocument: { uri } })
    checkEq(report.items.length, 1, 'one diagnostic for the missing comma', report.items)
    const item = report.items[0]
    checkEq(item.code, '0x02000007', 'the catalog code')
    checkEq(fmtRange(item.range), '0:16-0:17', 'the range of the diagnostic')
    checkSame(
        item.data.fixes,
        [
            {
                title: "insert ','",
                edits: [
                    {
                        range: { start: { line: 0, character: 15 }, end: { line: 0, character: 15 } },
                        newText: ',',
                    },
                ],
            },
        ],
        'data.fixes carries the compiler\'s fix, as range/newText'
    )

    // ---- code actions ----
    const range = { start: { line: 0, character: 15 }, end: { line: 0, character: 16 } }
    const actions = await ls.request('textDocument/codeAction', {
        textDocument: { uri },
        range,
        context: { diagnostics: [item], only: ['quickfix'] },
    })
    checkEq(actions.length, 1, 'the fix is offered as one code action', actions)
    if (actions.length === 1) {
        checkEq(actions[0].title, "insert ','", 'the title says what the change does')
        checkEq(actions[0].kind, 'quickfix', 'the kind is quickfix')
        checkSame(
            actions[0].edit,
            {
                changes: {
                    [uri]: [
                        {
                            range: {
                                start: { line: 0, character: 15 },
                                end: { line: 0, character: 15 },
                            },
                            newText: ',',
                        },
                    ],
                },
            },
            'the action carries a workspace edit for this document'
        )
        checkEq(
            actions[0].diagnostics && actions[0].diagnostics[0] && actions[0].diagnostics[0].code,
            '0x02000007',
            'the action quotes the diagnostic it repairs'
        )

        // The point of handing the fix to the editor instead of a sentence: applying it repairs the
        // file, and the server says so about the text that results.
        const edits = actions[0].edit.changes[uri]
        ls.change(uri, applyEdits(MISSING_COMMA, edits), 2)
        report = await ls.request('textDocument/diagnostic', { textDocument: { uri } })
        checkEq(report.items, [], 'applying the code action\'s edit removes the diagnostic')
    }

    // A request that touches no diagnostic gets no action.
    ls.change(uri, MISSING_COMMA, 3)
    const elsewhere = await ls.request('textDocument/codeAction', {
        textDocument: { uri },
        range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } },
        context: { diagnostics: [], only: ['quickfix'] },
    })
    checkEq(elsewhere, [], 'a range that touches no diagnostic gets no action')

    // A client that asks for another kind is not given a quick fix.
    const refactor = await ls.request('textDocument/codeAction', {
        textDocument: { uri },
        range,
        context: { diagnostics: [], only: ['refactor'] },
    })
    checkEq(refactor, [], 'only: refactor gets no quick fix')

    // ---- the candidate search the server has always done, still a code action ----
    const typoUri = ls.open('typo.cml', TYPO)
    report = await ls.request('textDocument/diagnostic', { textDocument: { uri: typoUri } })
    checkEq(report.items.length, 1, 'one diagnostic for the misspelt name', report.items)
    const typo = report.items[0]
    checkEq(typo.code, '0x0300000B', 'the unresolved name has its own code')
    checkEq(typo.data.fixes, undefined, 'the compiler attached no fix to an unresolved name')
    const typoActions = await ls.request('textDocument/codeAction', {
        textDocument: { uri: typoUri },
        range: typo.range,
        context: { diagnostics: [typo], only: ['quickfix'] },
    })
    checkEq(typoActions.length, 1, 'the one name that was meant is offered', typoActions)
    if (typoActions.length === 1) {
        checkEq(typoActions[0].title, "Replace with 'counter'", 'the candidate is named')
        check(typoActions[0].isPreferred === true, 'the candidate is preferred', typoActions[0])
        const edits = typoActions[0].edit.changes[typoUri]
        ls.change(typoUri, applyEdits(TYPO, edits), 2)
        report = await ls.request('textDocument/diagnostic', { textDocument: { uri: typoUri } })
        checkEq(report.items, [], 'applying the candidate removes the diagnostic')
    }
})