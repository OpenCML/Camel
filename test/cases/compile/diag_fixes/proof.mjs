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
 * Two of the checks are properties rather than examples:
 *
 *   - every code `diagnostics/registry.tsv` marks `edit` has at least one case here that produces
 *     a fix and whose application removes the diagnostic - so a code that starts promising an edit
 *     without a case that proves it fails here, and a case that names a code the registry does not
 *     mark `edit` fails too (a bijection, asserted in both directions);
 *   - a code whose fix depends on the text it appears in has both kinds of case: `0x00000001`
 *     (`077`) is refused with no fix where the leading zero reads two ways, and repaired where it
 *     reads one way (`09`) - the two runs have to report the same code.
 *
 * The `text` format is checked as well: a fix is printed there too, as a `help:` line with the
 * fix's title and, under it, the source line the fix leaves behind, marked where it writes.
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

const REGISTRY = path.join(REPO_ROOT, 'diagnostics', 'registry.tsv')
const SECTION_TYPE = {
    internal: 'InternalDiag',
    lexical: 'LexicalDiag',
    syntax: 'SyntaxDiag',
    semantic: 'SemanticDiag',
    runtime: 'RuntimeDiag',
}

/// The names the registry marks `edit`: each one promises a machine-applicable fix, so each one
/// owes a case here that produces that fix and whose application removes the diagnostic.
function registryEditNames() {
    const names = new Set()
    for (const line of fs.readFileSync(REGISTRY, 'utf8').split('\n')) {
        if (line.startsWith('#') || line.trim() === '') continue
        const field = line.split('\t')
        if (field.length < 7 || field[6] !== 'edit') continue
        names.add(`${SECTION_TYPE[field[0]]}::${field[1]}`)
    }
    return names
}

/// The name -> code of every case that produced a fix which, applied, removed the diagnostic, and
/// of every case that reported no fix. The two properties at the end read them.
const covered = new Map()
const noFixCodes = new Map()

function run(file, args = ['--error-format', 'json']) {
    const proc = spawnSync(CAMEL, [file, ...args], { env, encoding: 'utf8' })
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
        return { code: null, verified: false }
    }
    const d = found[0]
    const want = [{ title, edits }]
    const got = d.data.fixes
    check(
        JSON.stringify(got) === JSON.stringify(want),
        `${label}: the fix is ${title}`,
        `wanted ${JSON.stringify(want)}, got ${JSON.stringify(got)}`
    )
    if (JSON.stringify(got) !== JSON.stringify(want)) return { code: d.code, verified: false }

    const fixed = applyEdits(source, d.data.fixes[0].edits)
    const dir = fs.mkdtempSync(path.join(TEMP, 'case-'))
    const copy = path.join(dir, path.basename(file))
    fs.writeFileSync(copy, fixed)
    const after = run(copy)
    const remaining = diagnosticsOf(after)
    const gone = !remaining.some((r) => r.data.name === name)
    check(
        gone,
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
    // The text format prints the fix too: the title as a `help:` line, and under it the source line
    // the fix leaves behind, marked where it writes (or, for a fix that only cuts text, the line as
    // it stood, marked where it cuts). The line and the marker are derived from the fix the JSON
    // carries, so this compares the two formats with each other and not with a copy of one of them.
    {
        const shown = run(path.join(CASE_DIR, file), ['--error-format', 'text', '--color', 'never'])
        const lines = shown.stdout.split('\n')
        const heading = lines.indexOf(`help: ${title}`)
        check(heading >= 0, `${label}: the text format names the fix in a help: line`, shown.stdout)
        if (heading >= 0) {
            const replaced = edits.every((e) => e.replacement === '')
                ? source.split('\n')[edits[0].range.start.line]
                : applyEdits(source, [edits[0]]).split('\n')[edits[0].range.start.line]
            const window = lines.slice(heading, heading + 6)
            const shownLine = window.find((line) => /^\s*\d+ \| /.test(line))
            const itsText = shownLine === undefined ? null : shownLine.replace(/^\s*\d+ \| /, '')
            check(
                itsText === replaced,
                `${label}: the text format prints the line the fix leaves behind`,
                `in ${JSON.stringify(window)}`
            )
            const mark = edits[0].replacement === '' ? '-' : '+'
            const marked = window.find(
                (line) => /^\s*\| /.test(line) && line.replace(/^\s*\| /, '').trim().length > 0
            )
            const marks = marked === undefined ? '' : marked.replace(/^\s*\| /, '').trim()
            check(
                marks.length > 0 && [...marks].every((c) => c === mark),
                `${label}: the text format marks the columns it ${mark === '+' ? 'writes' : 'cuts'}`,
                `in ${JSON.stringify(window)}`
            )
        }
    }
    if (gone) covered.set(name, d.code)
    return { code: d.code, verified: gone }
}

/// A case whose repair is not one text: the report has to carry no fix at all.
function expectNoFix({ label, file, name }) {
    const proc = run(path.join(CASE_DIR, file))
    const found = diagnosticsOf(proc).filter((d) => d.data.name === name)
    if (found.length !== 1) {
        check(false, label, `wanted one ${name}, got ${found.length}: ${proc.stdout}`)
        return { code: null, hasFix: null }
    }
    const hasFix = 'fixes' in found[0].data
    check(
        !hasFix,
        `${label}: no fix is offered`,
        `got ${JSON.stringify(found[0].data.fixes)}`
    )
    noFixCodes.set(name, found[0].code)
    return { code: found[0].code, hasFix }
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

// A lambda's parameters need their parentheses: the one name
// that stands before the arrow is the whole parameter list, so wrapping it is the whole repair and
// the program compiles. A list of names gets no fix: `x, y => e` and the arguments of a call read
// the same way before the arrow (`f(a, x => e)`), and wrapping the wrong range is a wrong change a
// tool would apply without asking.
expectFix({
    label: 'lambda-needs-parentheses',
    file: 'parse/rulings/lambda_without_parentheses.cml',
    name: 'SyntaxDiag::LambdaParametersNeedParentheses',
    title: 'put the parameters in parentheses',
    edits: [{ range: { start: { line: 16, character: 35 }, end: { line: 16, character: 36 } }, replacement: '(x)' }],
    accepted: true,
})

// --- The two properties, instead of one example per code --------------------------------------
// Every code the registry marks `edit` promises a machine-applicable fix. That promise is what the
// registry column is for, so the cases above have to cover the column exactly: a code that starts
// promising an edit with no case that produces it and proves it by recompiling would otherwise be
// published unchecked, and a case that repairs a code the registry does not mark `edit` means the
// column and the compiler disagree.
{
    const promised = registryEditNames()
    check(
        promised.size > 0,
        'the registry marks at least one code `edit`',
        'a property over an empty set is not a pass'
    )
    const missing = [...promised].filter((name) => !covered.has(name))
    check(
        missing.length === 0,
        `every code marked 'edit' has a case that produces a fix and removes the diagnostic (${promised.size} code(s))`,
        `no case proves: ${missing.join(', ')}`
    )
    const extra = [...covered.keys()].filter((name) => !promised.has(name))
    check(
        extra.length === 0,
        'every case that produces a fix names a code the registry marks `edit`',
        `not marked 'edit': ${extra.join(', ')}`
    )
    // The name -> code mapping the registry sections imply, confirmed against the compiler: a case
    // that named the wrong code would satisfy the coverage above without covering anything.
    const section = { InternalDiag: '0x00', LexicalDiag: '0x01', SyntaxDiag: '0x02', SemanticDiag: '0x03', RuntimeDiag: '0x04' }
    const wrong = [...covered].filter(([name, code]) => !code.startsWith(section[name.split('::')[0]]))
    check(wrong.length === 0, 'the code of every covered case belongs to the name it was found under', JSON.stringify(wrong))
}

// `077` and `09` are one code with two situations: `077` is refused where the leading zero reads
// two ways (decimal in an entry argument, octal elsewhere) so there is no one text to write, and
// `09` reads one way so it is repaired. Both cases have to be the same code, or the pair proves
// nothing about a code that has a fix "sometimes".
{
    const name = 'LexicalDiag::InvalidNumberFormat'
    const withFix = covered.get(name)
    const without = noFixCodes.get(name)
    check(
        withFix !== undefined && without !== undefined,
        `${name} is locked in both of its situations: with a fix (09) and with none (077)`,
        `with a fix: ${withFix}, without: ${without}`
    )
    check(
        withFix !== undefined && withFix === without,
        `both situations report the same code (${without})`,
        `${withFix} vs ${without}`
    )
}
fs.rmSync(TEMP, { recursive: true, force: true })
console.log(failed === 0 ? 'diagnostic fixes proof: checks passed' : `diagnostic fixes proof: ${failed} check(s) failed`)
process.exit(failed === 0 ? 0 : 1)
