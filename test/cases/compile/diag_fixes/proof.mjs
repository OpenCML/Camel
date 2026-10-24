#!/usr/bin/env node
/**
 * A diagnostic that carries a machine-applicable edit carries a correct one: this applies it.
 *
 * For every case below the compiler is run with `--error-format json`, the fix of the named
 * diagnostic is compared with the expected one byte for byte (title, and every edit's range and
 * replacement), the edits are then applied to the source, and the compiler is run again. What the
 * second run has to show is the point of the whole exercise: the diagnostic is gone, and where the
 * compiler knew the whole answer nothing is reported any more. A diagnostic with nothing certain
 * to write is checked the other way: it has no `fixes` at all, rather than a guess a tool applies.
 *
 * Usage: node proof.mjs
 */

import fs from 'fs'
import os from 'os'
import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const CAMEL = path.join(REPO_ROOT, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const CASE_DIR = path.join(REPO_ROOT, 'test', 'cases')
const TEMP = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-diag-fixes-'))

let failed = 0
const check = (ok, label, detail) => {
    if (ok) {
        console.log(`ok   ${label}`)
        return
    }
    console.log(`FAIL ${label}: ${detail}`)
    failed += 1
}

const env = { ...process.env, CAMEL_HOME: process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest') }

function run(file) {
    const proc = spawnSync(CAMEL, [file, '--error-format', 'json'], { env, encoding: 'utf8' })
    if (proc.error) throw new Error(`cannot run ${CAMEL}: ${proc.error.message}`)
    return proc
}

function diagnosticsOf(proc) {
    const text = proc.stdout
    const start = text.lastIndexOf('[\n{')
    if (start < 0) return []
    return JSON.parse(text.slice(start))
}

/// The byte offset of a code point counted in a line, the way the compiler counts: lines and
/// columns are 0-based and a column counts code points, not bytes.
function offsetOf(text, line, character) {
    let at = 0
    for (let l = 0; l < line; l += 1) {
        const end = text.indexOf('\n', at)
        if (end < 0) throw new Error(`no line ${line}`)
        at = end + 1
    }
    let index = at
    for (let c = 0; c < character; c += 1) {
        const code = text.codePointAt(index)
        if (code === undefined) throw new Error(`no column ${character} on line ${line}`)
        index += code > 0xffff ? 2 : 1
    }
    return index
}

function applyEdits(text, edits) {
    const spans = edits.map((edit) => ({
        begin: offsetOf(text, edit.range.start.line, edit.range.start.character),
        end: offsetOf(text, edit.range.end.line, edit.range.end.character),
        replacement: edit.replacement,
    }))
    // Applied from the end, so that an earlier edit never moves a later one's range.
    spans.sort((a, b) => b.begin - a.begin)
    let out = text
    let moved = 0
    let previous = null
    for (const span of spans) {
        if (previous !== null && span.end > previous.begin) {
            throw new Error('the edits of one fix overlap')
        }
        out = out.slice(0, span.begin) + span.replacement + out.slice(span.end)
        previous = span
        moved += 1
    }
    if (moved === 0) throw new Error('the fix has no edit')
    return out
}

function textOf(rel) {
    return fs.readFileSync(path.join(CASE_DIR, rel), 'utf8')
}

/// A case whose fix is known: the report has to carry exactly this one, and applying it has to
/// remove the diagnostic. `accepted` says whether the compiler then reports nothing at all - a
/// program that is accepted runs, and a `main` returning a value is the process's exit code, so
/// "no diagnostic" is the property to check and not "exit 0".
function expectFix({ label, file, name, title, edits, accepted }) {
    const source = textOf(file)
    const proc = run(path.join(CASE_DIR, file))
    const found = diagnosticsOf(proc).filter((d) => d.data.name === name)
    if (found.length !== 1) {
        check(false, label, `wanted one ${name}, got ${found.length}: ${proc.stdout}`)
        return
    }
    const d = found[0]
    const want = [{ title, edits }]
    const got = d.data.fixes
    check(
        JSON.stringify(got) === JSON.stringify(want),
        `${label}: the fix is ${title}`,
        `wanted ${JSON.stringify(want)}, got ${JSON.stringify(got)}`
    )
    if (JSON.stringify(got) !== JSON.stringify(want)) return

    const fixed = applyEdits(source, d.data.fixes[0].edits)
    const dir = fs.mkdtempSync(path.join(TEMP, 'case-'))
    const copy = path.join(dir, path.basename(file))
    fs.writeFileSync(copy, fixed)
    const after = run(copy)
    const remaining = diagnosticsOf(after)
    check(
        !remaining.some((r) => r.data.name === name),
        `${label}: applying it removes the diagnostic`,
        `${name} is still reported: ${after.stdout}`
    )
    if (accepted !== undefined) {
        check(
            (remaining.length === 0) === accepted,
            `${label}: after the fix the compiler ${accepted ? 'reports nothing' : 'reports what is left'}`,
            `exit ${after.status}, ${remaining.length} diagnostic(s): ${after.stdout}${after.stderr}`
        )
    }
}

/// A case whose repair is not one text: the report has to carry no fix at all.
function expectNoFix({ label, file, name }) {
    const proc = run(path.join(CASE_DIR, file))
    const found = diagnosticsOf(proc).filter((d) => d.data.name === name)
    if (found.length !== 1) {
        check(false, label, `wanted one ${name}, got ${found.length}: ${proc.stdout}`)
        return
    }
    check(
        !('fixes' in found[0].data),
        `${label}: no fix is offered`,
        `got ${JSON.stringify(found[0].data.fixes)}`
    )
}

const render = (name) => `compile/diag_render/${name}.cml`

// --- The fixes ------------------------------------------------------------------------------
expectFix({
    label: 'missing-comma',
    file: render('syntax_missing_comma'),
    name: 'SyntaxDiag::MissingComma',
    title: "insert ','",
    // Between the two parameters: at the space that is already there, so the file gets
    // `a: int, b: int` and not `a: int , b: int`.
    edits: [{ range: { start: { line: 2, character: 15 }, end: { line: 2, character: 15 } }, replacement: ',' }],
    accepted: true,
})
expectFix({
    label: 'missing-then',
    file: render('syntax_missing_then'),
    name: 'SyntaxDiag::MissingThen',
    title: "insert 'then'",
    edits: [{ range: { start: { line: 4, character: 21 }, end: { line: 4, character: 21 } }, replacement: 'then ' }],
    accepted: true,
})
expectFix({
    label: 'extra-semicolon',
    file: render('syntax_extra_semicolon'),
    name: 'SyntaxDiag::ExtraSemicolon',
    title: "remove the extra ';'",
    edits: [{ range: { start: { line: 3, character: 14 }, end: { line: 3, character: 15 } }, replacement: '' }],
    accepted: true,
})
expectFix({
    label: 'stray-closer',
    file: render('syntax_stray_closer'),
    name: 'SyntaxDiag::UnmatchedClosingDelimiter',
    title: "remove the unmatched ')'",
    edits: [{ range: { start: { line: 3, character: 16 }, end: { line: 3, character: 17 } }, replacement: '' }],
    // `let x = 1 + )` becomes `let x = 1 + `, which is a second mistake of its own.
    accepted: false,
})
expectFix({
    label: 'missing-paren',
    file: render('syntax_missing_paren'),
    name: 'SyntaxDiag::MissingClosingDelimiter',
    title: "insert the ')' that is missing",
    // After the last token before the closer that `)` belongs to, not at the token the parser
    // tripped over.
    edits: [{ range: { start: { line: 3, character: 19 }, end: { line: 3, character: 19 } }, replacement: ')' }],
    accepted: true,
})
expectFix({
    label: 'missing-token',
    file: render('syntax_missing_token'),
    name: 'SyntaxDiag::MissingToken',
    title: "insert ':'",
    edits: [{ range: { start: { line: 2, character: 8 }, end: { line: 2, character: 8 } }, replacement: ':' }],
    accepted: true,
})
expectFix({
    label: 'unclosed-string',
    file: render('lexical_unclosed_string'),
    name: 'LexicalDiag::UnclosedStringLiteral',
    title: 'close the string at the end of the line',
    // The end of the line the string opens on, never the end of the file: the lexer reads an
    // unclosed string to the end of the file, and a quote there would put the rest of the program
    // inside it - the fix would compile and mean something else.
    edits: [{ range: { start: { line: 3, character: 19 }, end: { line: 3, character: 19 } }, replacement: "'" }],
    // The string is closed, and the compiler then reports the next mistake the file always had:
    // the ')' the writer meant to close the call with was inside the string as written, so the
    // call is missing it (MissingClosingDelimiter). Closing the string at the end of the file
    // instead would report nothing and put every following line inside the string.
    accepted: false,
})
expectFix({
    label: 'illegal-character',
    file: render('lexical_illegal_character'),
    name: 'LexicalDiag::IllegalCharacter',
    title: 'remove it',
    edits: [{ range: { start: { line: 3, character: 14 }, end: { line: 3, character: 15 } }, replacement: '' }],
    // `let x = 1 $ 2` becomes `let x = 1  2`: the line break rule makes the `2` a statement of
    // its own, so the file is accepted - which is what removing a character that no token starts
    // with means, not a claim that it is what the writer wanted.
    accepted: true,
})
expectFix({
    label: 'comparison-needs-space',
    file: 'parse/syntax/comparison_needs_space.cml',
    name: 'SyntaxDiag::ComparisonNeedsSpace',
    title: "write a space before '<'",
    edits: [{ range: { start: { line: 6, character: 8 }, end: { line: 6, character: 8 } }, replacement: ' ' }],
    accepted: true,
})
expectFix({
    label: 'angle-close-after-space',
    file: 'parse/syntax/angle_close_after_space.cml',
    name: 'SyntaxDiag::AngleCloseAfterSpace',
    title: "remove the space before '>'",
    // The space before the '>' the message is about, not the whitespace in front of the token the
    // parser stopped at (which is on the next line).
    edits: [{ range: { start: { line: 8, character: 20 }, end: { line: 8, character: 21 } }, replacement: '' }],
    // The parse is repaired; the file has a second mistake of its own, because `pick<int>` passes
    // `int` as a value rather than as a type argument (FunctionNameAsValue).
    accepted: false,
})
expectFix({
    label: 'line-starts-with-operator',
    file: 'parse/syntax/line_starts_with_operator.cml',
    name: 'SyntaxDiag::LineStartsWithOperator',
    title: 'join this line to the one before it',
    edits: [{ range: { start: { line: 4, character: 13 }, end: { line: 5, character: 4 } }, replacement: ' ' }],
    // The line joins the one before it; the case has a second mistake of its own, because `x` was
    // never declared (UnresolvedReference).
    accepted: false,
})
expectFix({
    label: 'unterminated-comment',
    file: 'parse/syntax/unterminated_comment.cml',
    name: 'LexicalDiag::UnterminatedComment',
    title: 'close the comment',
    edits: [{ range: { start: { line: 8, character: 0 }, end: { line: 8, character: 0 } }, replacement: '*/' }],
    accepted: true,
})
expectFix({
    label: 'when-after-body',
    file: 'parse/rules/when_after_body.cml',
    name: 'SyntaxDiag::WhenAfterBody',
    title: 'move the condition in front of the body',
    // One fix with two edits: the clause is written in front of the body and removed from behind
    // it. They name disjoint ranges, so a tool applies both to the text as written.
    edits: [
        { range: { start: { line: 3, character: 21 }, end: { line: 3, character: 21 } }, replacement: 'when true ' },
        { range: { start: { line: 3, character: 36 }, end: { line: 3, character: 45 } }, replacement: '' },
    ],
    accepted: true,
})

// --- The reports that have nothing certain to write -------------------------------------------
expectNoFix({ label: 'missing-initializer', file: render('syntax_missing_initializer'), name: 'SyntaxDiag::MissingInitializer' })
expectNoFix({ label: 'unexpected-token', file: render('syntax_unexpected_token'), name: 'SyntaxDiag::UnexpectedToken' })
expectNoFix({ label: 'keyword-as-name', file: render('syntax_keyword_as_name'), name: 'SyntaxDiag::KeywordAsIdentifier' })
expectNoFix({ label: 'assign-in-condition', file: render('syntax_assign_in_condition'), name: 'SyntaxDiag::AssignmentInCondition' })
expectNoFix({ label: 'unexpected-end', file: 'compile/fstring/bad_expr.cml', name: 'SyntaxDiag::UnexpectedEndOfFile' })

// `077` is refused as a number and the writer has two repairs that mean different things: it is
// decimal 77 in an entry argument and a conversion text, and octal 63 everywhere else. The fix
// would have to choose, so there is none; `09` has only one reading and gets one.
expectNoFix({ label: 'leading-zero-077', file: 'parse/numbers/leading_zero.cml', name: 'LexicalDiag::InvalidNumberFormat' })
expectFix({
    label: 'leading-zero-09',
    file: 'compile/diag_fixes/leading_zero_09.cml',
    name: 'LexicalDiag::InvalidNumberFormat',
    title: 'write the number without its leading zero',
    edits: [{ range: { start: { line: 9, character: 12 }, end: { line: 9, character: 14 } }, replacement: '9' }],
    accepted: true,
})

// A name that resolves to nothing: the compiler does not know which name was meant, and a fix that
// picks one is worse than a sentence.
expectNoFix({ label: 'unresolved-reference', file: 'compile/semantic/unresolved_reference.cml', name: 'SemanticDiag::UnresolvedReference' })

fs.rmSync(TEMP, { recursive: true, force: true })
console.log(failed === 0 ? 'diagnostic fixes proof: checks passed' : `diagnostic fixes proof: ${failed} check(s) failed`)
process.exit(failed === 0 ? 0 : 1)
