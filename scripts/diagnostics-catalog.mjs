// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The diagnostics registry, and the code tables it generates.
//
//   diagnostics/registry.tsv      the one place a diagnostic is declared: its section, name,
//                                 severity, message and help text, and the number it was
//                                 published with (empty until `--write` numbers it)
//   include/.../messages/*.h      the enums the compiler uses, generated
//   src/.../messages/*.cpp        the message tables, generated
//   docs/diagnostics.md           the reference, generated
//   diagnostics/examples.tsv      the example each diagnostic shows, pinned once (--pin-examples)
//
//   node scripts/diagnostics-catalog.mjs --check       registry valid, emitted files agree
//   node scripts/diagnostics-catalog.mjs --write       assign numbers, emit headers, tables, docs
//   node scripts/diagnostics-catalog.mjs --generate    assign numbers, emit headers and tables
//   node scripts/diagnostics-catalog.mjs --verify-doc  docs/diagnostics.md is what --write produces
//   node scripts/diagnostics-catalog.mjs --coverage    list the codes no test mentions (report only)
//   node scripts/diagnostics-catalog.mjs --coverage-check  fail if any code is mentioned by no test
//   node scripts/diagnostics-catalog.mjs --name-coverage   the semantic codes no case asserts by name
//   node scripts/diagnostics-catalog.mjs --name-coverage-check  fail if that set grew past the record
//   node scripts/diagnostics-catalog.mjs --no-producer   the semantic codes no code constructs
//   node scripts/diagnostics-catalog.mjs --no-producer-check  fail if any registered code is one
//   node scripts/diagnostics-catalog.mjs --pin-examples   fill in the example pins of new diagnostics
//   node scripts/diagnostics-catalog.mjs --text-coverage   the codes whose message a case reads
//   node scripts/diagnostics-catalog.mjs --text-coverage-check  fail if a section falls below its floor
//   --root DIR                                        read and write under DIR (the proof cases)
//
// The five counts answer five different questions and none of them replaces another. A code can be
// named by a case and still print a sentence nobody has read, which is what --text-coverage asks
// about; and a sentence that is right in the registry can be filled with the wrong arguments at the
// call site, which only the cases that run the program and compare its output can catch. "The words
// are in the registry" is not "the words the user sees", and "a needle quotes the sentence" is not
// "the sentence is composed right".
//
// --check runs in two places, so a hand-edited number cannot reach a build or a test run: the
// build (CMakeLists.txt makes it a dependency of libcamel_objects, before anything compiles
// against the enums) and the compile.diag_catalog plan, which also runs the failure-path proof.
// --generate is what `npm run build` runs first: it fills in the number of a declaration that
// has none and emits the headers and tables. --write is --generate plus the reference page;
// both refuse every defect named above before writing anything.
//
// A declaration is one line: section, name, number, severity, message, help. The number of an
// existing diagnostic is its published value and never moves. A declaration with no number gets
// the first free value after the last number of its section and severity (errors `0x000000` and
// up, warnings `0x100000` and up), in the order the lines are written; `--write` then writes that
// number into the line, so it is published from then on.
//
// What this refuses, and never repairs:
//   - one value twice in a section;
//   - a value outside its section's range for its severity;
//   - a name twice (one C++ enumerator per diagnostic name, and -W names are bare);
//   - an emitted file that differs from what the registry produces.
// A generator that renumbers to get past a collision is worse than the collision; none of the
// paths above writes anything.

import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'
// The runner's own reading of a plan file: which tests it holds, what `[defaults]` gives them, and
// which of them it marks `known_fail`. The examples below are taken from that reading, so the
// reference page and the suite cannot disagree about what a test is.
import { loadPlanTests } from '../test/tools/run-tests.mjs'

const DEFAULT_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const REGISTRY_REL = 'diagnostics/registry.tsv'
const DOC_REL = 'docs/diagnostics.md'
const EXAMPLES_REL = 'diagnostics/examples.tsv'

// The number of semantic codes with no case that asserts them by name, which `--name-coverage-check`
// refuses to see grow. It counts what is *not* done, so it is the one number here expected to fall,
// and it fell twice, recorded apart so neither hides in the other:
//   23 -> 22  `load.module_not_found` pinned `ModuleNotFound`.
//   22 -> 14  `calling_incomplete` pinned `CallingIncompleteFunction`, and the counter learned the
//             `verify_script` form (`diagnostics[0].name === 'SemanticDiag::X'`, `.code === '0x...'`),
//             which these seven had been pinned by all along: StatementAfterReturn,
//             UnusedFallibleValue, EffectNotExecuted, IgnoredSideEffect, NeverArgumentCall,
//             FailNotExecuted, StatementAfterFail.
//   14 -> 6   `null_test_never_null` pinned `NullTestNeverNull`; the counter learned the rendered form
//             a `-E short` run prints (`name=SemanticDiag::X, code=0x...`, which six had been pinned
//             by: ModuleParseFailed, ImportedModuleHasErrors, ComprehensionSourceRefused,
//             ExitInUnusedBranch, DimensionNameUsedOnce, FunctionTypeArgumentNotInferred); and the one
//             code no path raises, `FeatureNotSupported`, left the target as unraisable instead of
//             holding a place no case can take.
//    6 -> 5   `generics.type_variable_unused` pinned `TypeVariableUnused` (`FeatureNotSupported` had
//             already left the target by being deleted, not by being counted differently: nothing
//             raised it, so the registry no longer declares it).
//    5 -> 4   a section-qualified name inside an `expect_*_contains` needle is a pin -- the needle
//             requires the emitted name in the output -- so `TagHasNoReader` (`tags.plan.toml`, beside
//             the message it prints) had been pinned all along.
//    4 -> 3   `NodeOfNotAParameter` was pinned by a bare name in a needle; its test now asserts
//             `expect_diagnostic_name` too, which is the first-class form and makes the pin readable
//             rather than incidental. No case was written for it: one existed.
//    3 -> 1   a test can be a script: `test/cases/trans/cml_contract.py` runs the std::cml refusals
//             and asserts the name each one reports, so `CmlUnsupportedConstruct` and
//             `CmlBigConstant` were pinned in a file no count had read. Only a script under `test/`
//             counts as a case -- this file is one too (`diag_catalog` runs it), and reading it let
//             its own comments pin a name that nothing asserted.
//    1 -> 0   the name on its own in a needle: `WithPackNeedsNativeDeclaration` is required by
//             `prelude_signatures.plan.toml` without the section in front of it, and that test now
//             also asserts `expect_diagnostic_name` and the sentence it prints.
// A case added for a code lowers it in that commit; a correction lowers it in the commit that
// teaches the counter the form, and names what it found.
// The same ratchet for producers: the number of codes in each section that no code
// constructs. Measured on this tree: internal 0 of 1, lexical 0 of 5, syntax 0 of 19,
// semantic 0 of 238, runtime 1 of 43 (`FileSystemError`, which is in the message table and in the
// isolation predicate and nowhere else).
const NO_PRODUCER_RECORDED = new Map([
    ['semantic', 0],
    ['internal', 0],
    ['lexical', 0],
    ['syntax', 0],
    ['runtime', 0],
])

// Named exceptions: a code no case can reach positively, with the reason. Not a silent skip -- an
// exception is a decision that is written down, and every count that uses it says how many there are.
// A code here is outside the denominator of both the name count and the text count: it cannot be
// pinned by a case, and it cannot have its message read, for the same reason.
const NO_POSITIVE_CASE = new Map([
    [
        'InternalDiag::UnknownInternalError',
        'the compiler reports it when it reaches a state it has no code for; the tests that name it'
            + ' assert its absence (expect_output_not_contains), and making it happen would mean'
            + ' writing a defect into the compiler',
    ],
])
const NAME_COVERAGE_RECORDED = new Map([
    ['semantic', 0],
    ['internal', 0],
    ['lexical', 0],
    ['syntax', 0],
    ['runtime', 0],
])

// The sections, in the order the headers are included. `id` is also the stem of the two
// generated files; the type byte of the printed code is read from DiagType in base.h, under the
// name of the enum.
const SECTIONS = [
    { id: 'internal', enumName: 'InternalDiag', title: 'Internal' },
    { id: 'lexical', enumName: 'LexicalDiag', title: 'Lexical' },
    { id: 'syntax', enumName: 'SyntaxDiag', title: 'Syntax' },
    { id: 'semantic', enumName: 'SemanticDiag', title: 'Semantic' },
    { id: 'runtime', enumName: 'RuntimeDiag', title: 'Run time and passes' },
]
const SECTION_BY_ID = new Map(SECTIONS.map((s) => [s.id, s]))

/** DiagType::<EnumName> -> the type byte of the printed code. */
function readDiagTypes(root) {
    const file = path.join(root, 'include/camel/core/error/diagnostics/base.h')
    if (!fs.existsSync(file)) return null
    const text = fs
        .readFileSync(file, 'utf8')
        .replace(/\/\*[\s\S]*?\*\//g, '')
        .replace(/\/\/[^\n]*/g, '')
    const body = text.match(/enum class DiagType\s*:\s*uint8_t\s*\{([\s\S]*?)\}/)
    if (!body) return null
    const types = {}
    for (const m of body[1].matchAll(/(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)/g)) {
        types[m[1]] = Number(m[2])
    }
    return types
}

// The range of a section is the range of its severity band: the low 20 bits are the number, and
// the severity nibble sits just above them. `0x000000`..`0x0FFFFF` is an error, `0x100000`..
// `0x1FFFFF` a warning; the section adds the type byte.
const SEVERITIES = {
    error: { id: 'error', label: 'Error', min: 0x000000, max: 0x0fffff },
    warn: { id: 'warn', label: 'Warn', min: 0x100000, max: 0x1fffff },
}

// The seventh column: what the declaration says about a machine-applicable edit. `edit` means the
// report attaches at least one where the text to write is known (DiagFix, and `data.fixes` of the
// JSON output); `none: <cause>` means it attaches none, and the cause is why. A new declaration
// has to say which, so "every diagnostic carries an edit or the reason it has none" is a property
// of the registry and not a habit.
//
// The cause is one word of a closed vocabulary, so `--check` can refuse anything else; a
// declaration may add a sentence of its own after it.
const FIX_CAUSES = {
    runtime: 'the report is raised while the program runs, so there is no source text to rewrite',
    choice: 'the repair is a decision only the writer can make (which name, which type, which value)',
    context: 'the repair is known but is not one text: it needs a second place, another file, or a restructuring',
    internal: 'the report is about the compiler itself, and there is nothing in the program to change',
}

function hex6(value) {
    return '0x' + value.toString(16).toUpperCase().padStart(6, '0')
}

function hex8(value) {
    return '0x' + value.toString(16).toUpperCase().padStart(8, '0')
}

/**
 * The brace rules of `substitute` in src/core/error/diagnostics/format.cpp, checked here so that
 * a pattern which would throw while a report is rendered is refused at the registry instead. A
 * message and a help are both put through it: `{}` and `{N}` are fields, `{{` and `}}` are
 * literal braces, a field carries no format specifier, and one pattern does not mix the two ways
 * of indexing. A program that meets a broken pattern would be told about an internal error, and
 * the position it should carry is lost, so the registry is the place to catch it.
 */
function substitutionProblem(pattern) {
    let automatic = false
    let explicit = false
    let i = 0
    while (i < pattern.length) {
        const current = pattern[i]
        if (current === '}') {
            if (pattern[i + 1] === '}') {
                i += 2
                continue
            }
            return "has an unmatched '}'"
        }
        if (current !== '{') {
            i += 1
            continue
        }
        if (pattern[i + 1] === '{') {
            i += 2
            continue
        }
        let cursor = i + 1
        while (cursor < pattern.length && pattern[cursor] >= '0' && pattern[cursor] <= '9') cursor += 1
        const digits = pattern.slice(i + 1, cursor)
        if (cursor >= pattern.length) return "has an unmatched '{'"
        if (pattern[cursor] === ':') return 'has a format specifier, which a diagnostic message carries none of'
        if (pattern[cursor] !== '}') {
            return `has a malformed field beginning '${pattern.slice(i, cursor + 1)}'`
        }
        if (digits) explicit = true
        else automatic = true
        if (explicit && automatic) return 'mixes {} and {N} fields'
        i = cursor + 1
    }
    return null
}

function fail(lines) {
    console.log(`diagnostics registry: ${lines.length} problem(s)`)
    for (const line of lines) console.log(`  - ${line}`)
    process.exit(1)
}

function stripLineBreaks(text) {
    return text.replace(/\r/g, '')
}

// ---- registry ---------------------------------------------------------------------------------

/**
 * Reads diagnostics/registry.tsv. Returns the declarations in file order, the layout comments
 * grouped by the band of the declaration that follows them, the raw lines (so `--write` can fill
 * in numbers without touching anything else) and every defect found.
 */
function readRegistry(root) {
    const file = path.join(root, REGISTRY_REL)
    if (!fs.existsSync(file)) {
        return { problems: [`${REGISTRY_REL} does not exist`], records: [], lines: [] }
    }
    const text = stripLineBreaks(fs.readFileSync(file, 'utf8'))
    const lines = text.split('\n')
    const records = []
    const layout = new Map()
    const problems = []
    const namesInSection = new Map()
    const valuesInSection = new Map()
    const warningNames = new Map()
    const namesAnywhere = new Map()
    let pending = []
    let lastBand = null
    for (let i = 0; i < lines.length; i++) {
        const raw = lines[i]
        const where = `${REGISTRY_REL}:${i + 1}`
        if (!raw.trim()) continue
        if (raw.startsWith('#')) {
            pending.push(raw.slice(1))
            continue
        }
        const fields = raw.split('\t')
        if (fields.length !== 7) {
            problems.push(
                `${where}: expected section, name, number, severity, message, help and fix separated ` +
                    `by tabs (${fields.length} field(s))`
            )
            continue
        }
        const [sectionId, name, codeText, severityId, message] = fields
        const suggestion = fields[5]
        const fix = fields[6]
        const section = SECTION_BY_ID.get(sectionId)
        const severity = SEVERITIES[severityId]
        if (!section) {
            problems.push(`${where}: '${sectionId}' is not a section (${[...SECTION_BY_ID.keys()].join(', ')})`)
            continue
        }
        if (!severity) {
            problems.push(`${where}: '${severityId}' is not a severity (error, warn)`)
            continue
        }
        if (!/^[A-Za-z_]\w*$/.test(name)) {
            problems.push(`${where}: '${name}' is not a C++ enumerator name`)
            continue
        }
        let code = null
        if (codeText !== '') {
            if (!/^0x[0-9A-F]{6}$/.test(codeText)) {
                problems.push(`${where}: '${codeText}' is not a number of six uppercase hex digits (0x1A2B3C)`)
                continue
            }
            code = Number(codeText)
            if (code < severity.min || code > severity.max) {
                problems.push(
                    `${sectionId}:${name} = ${hex6(code)} is outside the ${severityId} range ` +
                        `${hex6(severity.min)}..${hex6(severity.max)} of section ${sectionId}`
                )
            }
        }
        if (!message) problems.push(`${where}: ${sectionId}:${name} has an empty message`)
        // A brace that is not a field is a defect here, not when a program is told about its
        // error: see substitutionProblem.
        for (const [what, pattern] of [
            ['message', message],
            ['help', suggestion],
        ]) {
            const problem = substitutionProblem(pattern)
            if (problem) {
                problems.push(
                    `${where}: ${sectionId}:${name} ${what} ${problem}; write '{0}' or '{1}' for a ` +
                        `field and '{{' or '}}' for a literal brace`
                )
            }
        }
        // What the declaration says about an edit. `edit` promises that a report of this code
        // attaches one where the text is known, and a promise the reader is told in words to
        // carry out needs the words: an edit without help says nothing to a person.
        if (fix === 'edit') {
            if (!suggestion.trim()) {
                problems.push(
                    `${where}: ${sectionId}:${name} declares 'edit' and has no help; a report that ` +
                        `hands a tool the text to write says what it does to the reader too`
                )
            }
        } else if (fix.startsWith('none:')) {
            const parts = fix
                .slice('none:'.length)
                .split(':')
                .map((part) => part.trim())
            const cause = parts[0]
            if (!(cause in FIX_CAUSES)) {
                problems.push(
                    `${where}: '${cause}' is not a reason for having no edit ` +
                        `(${Object.keys(FIX_CAUSES).join(', ')})`
                )
            }
        } else {
            problems.push(
                `${where}: ${sectionId}:${name} says '${fix}' about an edit; write 'edit' or ` +
                    `'none: <cause>' (${Object.keys(FIX_CAUSES).join(', ')}), so that a new ` +
                    `diagnostic always says whether it carries an edit and why not when it does not`
            )
        }
        const key = `${sectionId}:${name}`
        if (namesInSection.has(key)) {
            problems.push(
                `${sectionId}: the name '${name}' is already declared (${REGISTRY_REL}:${namesInSection.get(key)})`
            )
        } else {
            namesInSection.set(key, i + 1)
        }
        if (namesAnywhere.has(name)) {
            const other = namesAnywhere.get(name)
            problems.push(
                `the name '${name}' is already declared in ${other.section} (${REGISTRY_REL}:${other.line}); ` +
                    `a diagnostic name is unique across the registry`
            )
        } else {
            namesAnywhere.set(name, { section: sectionId, line: i + 1 })
        }
        if (code !== null) {
            const vkey = `${sectionId}:${code}`
            if (valuesInSection.has(vkey)) {
                const other = valuesInSection.get(vkey)
                problems.push(
                    `${sectionId}: ${other.name} and ${name} have the same value ${hex6(code)}; ` +
                        `give the new declaration no number and let --write assign the next free one`
                )
            } else {
                valuesInSection.set(vkey, { name, line: i + 1 })
            }
        }
        if (severityId === 'warn') {
            const wkey = name.toLowerCase()
            if (warningNames.has(wkey) && warningNames.get(wkey) !== sectionId) {
                problems.push(
                    `the warning name '${name}' exists in ${warningNames.get(wkey)} and ${sectionId}; ` +
                        `-W options name warnings without the section`
                )
            }
            warningNames.set(wkey, sectionId)
        }
        const record = { section, severity, name, code, message, suggestion, fix, line: i + 1, index: i }
        records.push(record)
        const band = `${section.id}/${severity.id}`
        if (!layout.has(band)) layout.set(band, [])
        for (const comment of pending) layout.get(band).push({ kind: 'comment', text: comment })
        pending = []
        layout.get(band).push({ kind: 'entry', record })
        lastBand = band
    }
    if (pending.length) {
        if (!lastBand) {
            problems.push(`${REGISTRY_REL}: comments but no declaration`)
        } else {
            for (const comment of pending) layout.get(lastBand).push({ kind: 'comment', text: comment })
        }
    }
    return { problems, records, lines, layout }
}

/** Fills the number of every declaration that has none, at the end of its band. */
function assignNumbers(records) {
    const problems = []
    const next = new Map()
    for (const section of SECTIONS) {
        for (const severity of Object.values(SEVERITIES)) {
            const band = `${section.id}/${severity.id}`
            const codes = records.filter((r) => r.section === section && r.severity === severity && r.code !== null)
            next.set(band, codes.length ? Math.max(...codes.map((r) => r.code)) + 1 : severity.min)
        }
    }
    for (const record of records) {
        if (record.code !== null) continue
        const band = `${record.section.id}/${record.severity.id}`
        const value = next.get(band)
        if (value > record.severity.max) {
            problems.push(
                `${record.section.id}: section ${record.section.id}/${record.severity.id} has no free ` +
                    `number at or below ${hex6(record.severity.max)}`
            )
            continue
        }
        record.code = value
        record.assigned = true
        next.set(band, value + 1)
    }
    return problems
}

// ---- emitted files ----------------------------------------------------------------------------

const HEADER_NOTE = [
    '// Generated by scripts/diagnostics-catalog.mjs from diagnostics/registry.tsv. Do not edit:',
    '// change the registry and run the script with --write.',
]

function layoutFor(layout, section, severity) {
    return layout.get(`${section.id}/${severity.id}`) || []
}

function emitEnumBody(layout, section) {
    const out = []
    for (const severity of [SEVERITIES.error, SEVERITIES.warn]) {
        const items = layoutFor(layout, section, severity)
        let run = []
        const flush = () => {
            if (run.length === 0) return
            const width = Math.max(...run.map((r) => r.name.length))
            for (const r of run) out.push(`    ${r.name.padEnd(width)} = ${hex6(r.code)},`)
            run = []
        }
        for (const item of items) {
            if (item.kind === 'comment') {
                flush()
                out.push(item.text === '' ? '' : `    //${item.text}`)
            } else {
                run.push(item.record)
            }
        }
        flush()
    }
    return out
}

function emitHeader(layout, section) {
    return [
        '// Copyright (c) the OpenCML Organization',
        '// SPDX-License-Identifier: MIT',
        '',
        ...HEADER_NOTE,
        '',
        '// clang-format off',
        '',
        '#pragma once',
        '',
        '#include <cstdint>',
        '#include <unordered_map>',
        '',
        '#include "../base.h"',
        '',
        'namespace camel::core::error {',
        '',
        `enum class ${section.enumName} : uint32_t {`,
        ...emitEnumBody(layout, section),
        '};',
        '',
        `constexpr DiagType diagTypeOf(${section.enumName}) { return DiagType::${section.enumName}; }`,
        '',
        `const std::unordered_map<${section.enumName}, DiagInfo> &get${section.id[0].toUpperCase()}${section.id.slice(1)}DiagInfoMap();`,
        '',
        '} // namespace camel::core::error',
        '',
    ].join('\n')
}

function cppString(text) {
    return text
        .replaceAll('\\', '\\\\')
        .replaceAll('"', '\\"')
        .replaceAll('\n', '\\n')
        .replaceAll('\t', '\\t')
        .replaceAll('\r', '\\r')
        // A trigraph needs two `?` in a row; the compiler warns about `??'` and its siblings even
        // though C++17 ignores them, and a one-line literal puts them next to each other. Escape
        // the second `?` of such a pair: `?\?'` is the same string and no trigraph.
        .replace(/\?\?([=/'()!<>-])/g, '?\\?$1')
}

function emitTable(layout, section) {
    const out = [
        '// Copyright (c) the OpenCML Organization',
        '// SPDX-License-Identifier: MIT',
        '',
        ...HEADER_NOTE,
        '',
        '// clang-format off',
        '',
        `#include "camel/core/error/diagnostics/messages/${section.id}.h"`,
        '',
        'namespace camel::core::error {',
        '',
        `const std::unordered_map<${section.enumName}, DiagInfo> &get${section.id[0].toUpperCase()}${section.id.slice(1)}DiagInfoMap() {`,
        `    static const std::unordered_map<${section.enumName}, DiagInfo> infoMap = {`,
    ]
    for (const severity of [SEVERITIES.error, SEVERITIES.warn]) {
        for (const item of layoutFor(layout, section, severity)) {
            if (item.kind !== 'entry') continue
            const r = item.record
            out.push('        {')
            out.push(`            ${section.enumName}::${r.name},`)
            out.push('            {')
            out.push(`                ${JSON.stringify(r.name)},`)
            out.push(`                "${cppString(r.message)}",`)
            out.push(`                "${cppString(r.suggestion)}",`)
            out.push('            },')
            out.push('        },')
        }
    }
    out.push('    };')
    out.push('    return infoMap;')
    out.push('}')
    out.push('')
    out.push('} // namespace camel::core::error')
    out.push('')
    return out.join('\n')
}

function headerRel(section) {
    return `include/camel/core/error/diagnostics/messages/${section.id}.h`
}

function tableRel(section) {
    return `src/core/error/diagnostics/messages/${section.id}.cpp`
}

// ---- document ---------------------------------------------------------------------------------

function fence(text, lang = '') {
    const ticks = text.includes('```') ? '````' : '```'
    return `${ticks}${lang}\n${text}\n${ticks}`
}

function walk(dir, predicate, out = []) {
    if (!fs.existsSync(dir)) return out
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) walk(full, predicate, out)
        else if (predicate(full)) out.push(full)
    }
    return out
}

// The codes some code constructs. A producer is an occurrence of the name in the compiler's own
// sources -- src, include, modules -- with comments removed and the message table left out (the
// table names every code, including one nothing raises). Not the `.of(...)` shape alone: a code can
// be chosen by a ternary inside the call (`of(cond ? SemanticDiag::A : SemanticDiag::B)`) or
// returned by a helper that picks one (`return SemanticDiag::NotDifferentiable`) and constructed by
// its caller. Both are producers, and a rule that read only `of(SemanticDiag::X)` would call ten
// live codes dead.
function producedNames(root) {
    const files = [
        ...walk(path.join(root, 'src'), (f) => /\.(cpp|h|hpp|cc)$/.test(f)),
        ...walk(path.join(root, 'include'), (f) => /\.(cpp|h|hpp|cc)$/.test(f)),
        ...walk(path.join(root, 'modules'), (f) => /\.(cpp|h|hpp|cc)$/.test(f)),
    ].filter((f) => !/[\\/]diagnostics[\\/]messages[\\/]/.test(f))
    const names = new Set()
    for (const file of files) {
        const text = fs
            .readFileSync(file, 'utf8')
            .replace(/\/\*[\s\S]*?\*\//g, '')
            .replace(/\/\/.*$/gm, '')
            // A switch label names a code to classify it, not to raise it: `case
            // RuntimeDiag::FileSystemError:` sits in the "does this fault isolate?" predicate, and
            // counting it kept that code looking alive while nothing constructed it.
            .replace(/\bcase\s+[A-Za-z_:]*Diag::[A-Za-z0-9_]+\s*:/g, '')
        // Every section, section-qualified: `SyntaxDiag::X` and `RuntimeDiag::X` are as much a
        // producer as `SemanticDiag::X`, and keeping the section in the key means a name that
        // appears under one cannot make a code of another look alive.
        for (const m of text.matchAll(/\b([A-Za-z]+Diag)::([A-Za-z0-9_]+)/g)) names.add(`${m[1]}::${m[2]}`)
    }
    return names
}

function loadTests(root) {
    const tests = []
    for (const planPath of walk(path.join(root, 'test', 'plans'), (f) => f.endsWith('.plan.toml')).sort()) {
        let read
        try {
            read = loadPlanTests(planPath)
        } catch {
            continue
        }
        for (const test of read.tests) tests.push({ ...test, planPath })
    }
    return tests
}

function stripLeadingComment(source) {
    return source.replace(/^\s*\/\*[\s\S]*?\*\/\s*/, '')
}

// The example each diagnostic shows, pinned once: `<Section>::<Name>` and the case that raises it.
// Pinning is what makes "add a test" unable to change another diagnostic's example -- see
// exampleFor. `--pin-examples` fills in the entries that have none and never rewrites a pin it
// finds; a pin whose case stops raising the diagnostic falls back to the search, and the page's
// diff is then the record of it.
function loadExamplePins(root) {
    const pins = new Map()
    const file = path.join(root, EXAMPLES_REL)
    if (!fs.existsSync(file)) return pins
    for (const line of fs.readFileSync(file, 'utf8').split('\n')) {
        const text = line.replace(/\r$/, '')
        if (!text || text.startsWith('#')) continue
        const [name, example] = text.split('\t')
        if (name && example) pins.set(name.trim(), example.trim())
    }
    return pins
}

function writeExamplePins(root, pins) {
    const text = [
        '# The example each diagnostic shows in docs/diagnostics.md: the diagnostic, and the case',
        '# that raises it. `--write` fills in the ones that are missing and never rewrites the others:',
        '# adding a test must not move another diagnostic\'s example, and a pin is what makes that true.',
        ...[...pins.keys()]
            .sort()
            .map((name) => `${name}\t${pins.get(name)}`),
        '',
    ].join('\n')
    return writeFile(root, EXAMPLES_REL, text)
}

// The entry as its author wrote it, for the loose name search below: the runner's own bookkeeping
// (`__planPath`, `__backend`, `__knownFail`, ...) names no diagnostic and is not part of what the
// entry declares.
function declaredFields(test) {
    return JSON.stringify(test, (key, value) => (key.startsWith('__') ? undefined : value))
}

function exampleFor(entry, tests, root, pins) {
    const candidates = []
    for (const test of tests) {
        // An example must be a program that passes: a test the runner marks `known_fail` on the
        // back end it runs fails on purpose, and showing it would show the reader a program that
        // fails. The mark is the plan's own `known_fail` -- the runner expands an entry into one
        // test per back end and sets `__knownFail` on each -- so this reads the declaration the
        // suite runs by and not a second one.
        if (test.__knownFail) continue
        if (!test.case || (test.tool && test.tool !== 'camel')) continue
        const exact =
            test.expect_diagnostic_name === entry.fullName ||
            test.expect_diagnostic_code === hex8(entry.code)
        const loose = !exact && new RegExp(`\\b${entry.name}\\b`).test(declaredFields(test))
        if (!exact && !loose) continue
        const casePath = path.resolve(path.dirname(test.planPath), test.case)
        if (!casePath.endsWith('.cml') || !fs.existsSync(casePath)) continue
        const source = stripLeadingComment(fs.readFileSync(casePath, 'utf8'))
            .replace(/\r\n/g, '\n')
            .trimEnd()
        const lines = source.split('\n').length
        candidates.push({ test, casePath, source, lines, exact })
    }
    const relOf = (candidate) => path.relative(root, candidate.casePath).replaceAll('\\', '/')
    // The example is pinned in diagnostics/examples.tsv once, and a pinned one is what the page
    // keeps: picking by position in the test list is only stable while nothing is added to it, so
    // a case a lane adds above another one used to move that diagnostic's example -- the page
    // changed for a reason that had nothing to do with the diagnostic. A pin moves only when the
    // case it names stops raising the diagnostic or is deleted, and then the diff says so.
    const pinned = pins.get(entry.fullName)
    let best = null
    if (pinned) {
        // One case can be cited by more than one test (a plan entry that also asks for JSON output,
        // a plan that runs it on another back end). The pin names the case; which of its citations
        // is shown keeps the preference the search had -- the one that expects this diagnostic by
        // name or number -- so pinning does not rewrite the command line under the example.
        const same = candidates.filter((c) => relOf(c) === pinned)
        best = same.find((c) => c.exact) || same[0] || null
    }
    if (!best) {
        const small = candidates.filter((c) => c.lines <= 40)
        best = small.find((c) => c.exact) || small[0]
    }
    if (!best) return null
    const rel = relOf(best)
    const vars = { run_nvm: 'std::nvm', run_fvm: 'std::fvm', run_jit: 'std::jit', run_prl: 'std::prl' }
    const args = (best.test.args || [])
        .flat()
        .map((a) =>
            String(a)
                .replace('${case}', rel)
                .replace(/^\$\{(\w+)\}$/, (_, v) => vars[v] || `\${${v}}`)
        )
    const envs = (best.test.envs || []).map((e) => `${e} `).join('')
    return { rel, source: best.source, command: `${envs}camel ${args.join(' ')}` }
}

function catalogEntries(records) {
    return records
        .map((r) => ({
            type: r.section.enumName,
            sectionId: r.section.id,
            title: r.section.title,
            name: r.name,
            fullName: `${r.section.enumName}::${r.name}`,
            code: ((r.section.type << 24) | r.code) >>> 0,
            severity: r.severity.label,
            message: r.section.id === 'runtime' ? r.message : r.message.replaceAll('{{', '{').replaceAll('}}', '}'),
            suggestion: r.section.id === 'runtime' ? r.suggestion : r.suggestion.replaceAll('{{', '{').replaceAll('}}', '}'),
            fix: r.fix,
        }))
        .sort((a, b) => a.code - b.code)
}

function emitDoc(entries, tests, root, pins) {
    const out = []
    out.push('# Diagnostics Reference')
    out.push('')
    out.push('<!-- Generated by scripts/diagnostics-catalog.mjs --write. Do not edit: the codes and')
    out.push('     messages are declared in diagnostics/registry.tsv, and each example is the one')
    out.push('     diagnostics/examples.tsv pins for it. Add one line there and run the script. -->')
    out.push('')
    out.push('Every diagnostic the compiler, the passes and the runtime can report. `camel --explain <name or code>`')
    out.push('prints one entry.')
    out.push('')
    out.push('- **Code.** 32 bits: the type in the top 8 (`00` internal, `01` lexical, `02` syntax, `03` semantic,')
    out.push('  `04` run time and passes), the severity in the next 4 (`0` error, `1` warning, `2` info, `3` hint) and a number')
    out.push('  in the low 20. It is the `code` of the JSON output and of the language server.')
    out.push('- **Name.** `<Type>::<Name>`, the `data.name` of the JSON output. The command line names a warning by')
    out.push('  the part after `::`, ignoring case, `-` and `_`: `-Wno-ignored-side-effect`, `-Werror=IgnoredSideEffect`.')
    out.push('- **Message, help.** The templates; `{0}`, `{1}`, ... are filled in by the report.')
    out.push('- **Example.** A program of the test suite that raises the diagnostic, where one is small enough to show.')
    out.push('')
    out.push(
        `${entries.length} diagnostics: ${entries.filter((e) => e.severity === 'Error').length} errors, ` +
            `${entries.filter((e) => e.severity === 'Warn').length} warnings. ` +
            `${entries.filter((e) => e.fix === 'edit').length} carry a machine-applicable edit, ` +
            `${entries.filter((e) => e.fix !== 'edit').length} say why they carry none.`
    )
    out.push('')
    out.push('| Code | Name | Severity | Edit |')
    out.push('|------|------|----------|------|')
    for (const entry of entries) {
        out.push(
            `| \`${hex8(entry.code)}\` | [${entry.fullName}](#${entry.name.toLowerCase()}) | ` +
                `${entry.severity} | ${entry.fix === 'edit' ? 'yes' : 'no'} |`
        )
    }
    out.push('')
    let currentType = null
    for (const entry of entries) {
        if (entry.type !== currentType) {
            currentType = entry.type
            out.push(`## ${entry.title} (\`${entry.type}\`)`)
            out.push('')
        }
        out.push(`### ${entry.name}`)
        out.push('')
        out.push(`\`${hex8(entry.code)}\` · ${entry.severity} · \`${entry.fullName}\``)
        out.push('')
        out.push('**Fix**')
        out.push('')
        out.push(
            entry.fix === 'edit'
                ? 'A machine-applicable edit is attached where the report knows the text to write: ' +
                      '`data.fixes` of `--error-format json` carries the range and the replacement.'
                : `None: ${entry.fix.slice('none:'.length).trim()}`
        )
        out.push('')
        out.push('**Message**')
        out.push('')
        out.push(fence(entry.message, 'text'))
        out.push('')
        if (entry.suggestion) {
            out.push('**Help**')
            out.push('')
            out.push(fence(entry.suggestion, 'text'))
            out.push('')
        }
        const example = exampleFor(entry, tests, root, pins)
        if (example) {
            out.push(`**Example** (\`${example.rel}\`, run as \`${example.command}\`)`)
            out.push('')
            out.push(fence(example.source, 'camel'))
            out.push('')
        } else {
            out.push('*No example: no test small enough to show raises it.*')
            out.push('')
        }
    }
    return out.join('\n') + '\n'
}

// ---- files ------------------------------------------------------------------------------------

function readFileOrEmpty(file) {
    return fs.existsSync(file) ? fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n') : ''
}

function verifyFile(root, rel, expected, problems) {
    const actual = readFileOrEmpty(path.join(root, rel))
    if (actual !== expected) {
        problems.push(
            `${rel} is not what diagnostics/registry.tsv produces; run ` +
                `\`node scripts/diagnostics-catalog.mjs --write\``
        )
    }
}

function writeFile(root, rel, text) {
    const file = path.join(root, rel)
    fs.mkdirSync(path.dirname(file), { recursive: true })
    const current = readFileOrEmpty(file)
    if (current === text) return false
    fs.writeFileSync(file, text)
    return true
}

function rewriteRegistry(root, records, lines) {
    const byLine = new Map(records.map((r) => [r.index, r]))
    const out = lines.map((raw, i) => {
        const record = byLine.get(i)
        if (!record || !record.assigned) return raw
        const fields = raw.split('\t')
        fields[2] = hex6(record.code)
        return fields.join('\t')
    })
    const text = out.join('\n')
    if (text === stripLineBreaks(readFileOrEmpty(path.join(root, REGISTRY_REL)))) return false
    fs.writeFileSync(path.join(root, REGISTRY_REL), text)
    return true
}

// ---- entry ------------------------------------------------------------------------------------

const argv = process.argv.slice(2)
let mode = '--check'
let root = DEFAULT_ROOT
// `--section <id>` narrows the name-coverage modes to one section, so a single section can be read
// or checked while the others keep their numbers. It narrows the report, never the reading: the
// pins are collected the same way whichever sections are printed.
let sectionFilter = null
for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--root') {
        root = path.resolve(argv[++i] || '')
    } else if (argv[i] === '--section') {
        sectionFilter = argv[++i] || null
    } else {
        mode = argv[i]
    }
}
if (!['--check', '--write', '--generate', '--verify-doc', '--coverage', '--coverage-check', '--name-coverage', '--name-coverage-check', '--pinned-coverage', '--pinned-coverage-check', '--no-producer', '--no-producer-check', '--text-coverage', '--text-coverage-check', '--pin-examples'].includes(mode)) {
    console.log(
        'usage: node scripts/diagnostics-catalog.mjs --check | --write | --generate | --verify-doc | --coverage | --coverage-check | --name-coverage | --name-coverage-check | --no-producer | --no-producer-check | --text-coverage | --text-coverage-check | --pinned-coverage | --pinned-coverage-check | --pin-examples [--root DIR]'
    )
    process.exit(2)
}

const diagTypes = readDiagTypes(root)
if (!diagTypes) {
    fail([`include/camel/core/error/diagnostics/base.h: DiagType could not be read`])
}
for (const section of SECTIONS) {
    if (!(section.enumName in diagTypes)) {
        fail([`base.h: DiagType::${section.enumName} is missing for section ${section.id}`])
    }
    section.type = diagTypes[section.enumName]
}

const { problems, records, lines, layout } = readRegistry(root)
if (records.length === 0) {
    fail([...problems, 'no diagnostic was read from diagnostics/registry.tsv; nothing was checked'])
}
if (problems.length) fail(problems)

if (mode === '--coverage' || mode === '--coverage-check') {
    const entries = catalogEntries(records)
    const files = [
        ...walk(path.join(root, 'test', 'plans'), () => true),
        ...walk(path.join(root, 'test', 'cases'), (f) => /\.(mjs|js|py)$/.test(f)),
        ...walk(path.join(root, 'exemplars'), (f) => /\.(mjs|json|toml|md)$/.test(f)),
    ]
    const corpus = files.map((f) => fs.readFileSync(f, 'utf8')).join('\n')
    // A test mentions a code by its name (`SemanticDiag::UnresolvedReference`, the `name=` field of
    // a report, the `expect_diagnostic_name` of a plan) or by the number it prints
    // (`code=0x0300000B`, `expect_diagnostic_code`). Both are read as whole words, so a longer
    // identifier or number that happens to contain them is not read as a mention.
    const wholeWord = (text, word) => new RegExp(`\\b${word}\\b`).test(text)
    const mentioned = (entry) =>
        wholeWord(corpus, entry.name) || wholeWord(corpus, hex8(entry.code))
    const missing = entries.filter((e) => !mentioned(e))
    if (mode === '--coverage') {
        console.log(`diagnostic codes mentioned by no test: ${missing.length} of ${entries.length}`)
        for (const e of missing) console.log(`  ${hex8(e.code)}  ${e.fullName}`)
        process.exit(0)
    }


    // The property is the bijection between the two sets -- every code the registry declares is
    // mentioned by a test, and every code a test mentions is one the registry declares -- and not a
    // count. A count of the declared codes moves with every diagnostic the language gains or loses
    // while the relation it was meant to guard does not, and it narrates the edits that moved it
    // instead of saying what is checked; with the relation asserted, adding or deleting a code
    // touches no plan. Each side must be non-empty, so a run that read no declaration or no
    // mention fails here instead of passing over nothing.
    //
    // The registry-to-test direction reads the whole test corpus: a plan's prose names a diagnostic
    // as much as its `expect_diagnostic_name` does. The test-to-registry direction reads the
    // citations a test acts on -- a section-qualified name, the `name=` field of a report, the
    // number a `code=` reports -- with comments and a plan's `desc` left out. An assertion that a
    // deleted diagnostic is absent stays a mention of it; a comment that says which diagnostic a
    // past revision produced does not, because it is a statement about history and not a test of
    // the code that exists.
    const assertedText = (file) => {
        const text = fs.readFileSync(file, 'utf8')
        if (file.endsWith('.toml')) return text.replace(/^\s*(#|desc\s*=).*$/gm, '')
        if (/\.(mjs|js)$/.test(file)) return text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/^\s*\/\/.*$/gm, '')
        if (file.endsWith('.py')) return text.replace(/"""[\s\S]*?"""/g, '').replace(/^\s*#.*$/gm, '')
        return text
    }
    const asserted = files.map(assertedText).join('\n')
    // A section-qualified citation is kept with its section, so `LexicalDiag::UnterminatedComment`
    // is not read as `SyntaxDiag::UnterminatedComment`: the name is not the code.
    const citedNames = new Set(
        [...asserted.matchAll(/\b(Internal|Lexical|Syntax|Semantic|Runtime)Diag::([A-Za-z_]\w*)/g)].map(
            (m) => `${m[1]}Diag::${m[2]}`
        )
    )
    for (const match of asserted.matchAll(/\bname=([A-Za-z_]\w*), code=/g)) {
        citedNames.add(match[1])
    }
    const citedCodes = new Set()
    for (const match of asserted.matchAll(/\bcode=(0[xX][0-9A-Fa-f]{1,8})/g)) {
        citedCodes.add(hex8(Number(match[1])))
    }
    const inRegistryByName = new Set(entries.map((e) => e.fullName))
    const inRegistryByNameOnly = new Set(entries.map((e) => e.name))
    const inRegistryByCode = new Set(entries.map((e) => hex8(e.code)))
    const inTestsOnly = [
        ...[...citedNames]
            .filter((name) => !inRegistryByName.has(name) && !inRegistryByNameOnly.has(name))
            .map((n) => `'${n}'`),
        ...[...citedCodes].filter((code) => !inRegistryByCode.has(code)).map((c) => `'${c}'`),
    ].sort()
    const problems = []
    if (entries.length === 0) problems.push('the registry declares no code; nothing was compared')
    if (missing.length === entries.length) {
        problems.push('no test mentions a declared code; nothing was compared')
    }
    if (citedNames.size === 0 && citedCodes.size === 0) {
        problems.push('no test cites a code; nothing was compared')
    }
    for (const e of missing) {
        problems.push(
            `${hex8(e.code)} ${e.fullName} is declared and no test mentions it; name it in a test ` +
                `(\`name=${e.fullName}\`)`
        )
    }
    for (const member of inTestsOnly) {
        problems.push(`${member} is cited by a test and ${REGISTRY_REL} declares no such code`)
    }
    if (problems.length) {
        console.log(
            `diagnostics coverage: ${problems.length} problem(s); the registry declares ` +
                `${entries.length} code(s), the tests cite ${citedNames.size} name(s) and ` +
                `${citedCodes.size} number(s)`
        )
        for (const problem of problems) console.log(`  - ${problem}`)
        process.exit(1)
    }
    console.log(
        'every diagnostic code the registry declares is mentioned by a test, and every code a ' +
            'test cites is one the registry declares'
    )
    process.exit(0)
}

// ---- the stronger direction: a code a case acts on, not one the prose names -------------------
// `--coverage-check` asks that every code be *mentioned* by a test, and a plan's prose counts as a
// mention, so a sentence that would not notice the compiler emitting a different diagnostic keeps
// it green. This counts the other thing: a case that *pins* the report a run produced -- the name
// through `expect_diagnostic_name`, the number through `expect_diagnostic_code`, and the same two
// read out of a `verify_script` (`diagnostics[0].name === 'SemanticDiag::X'`, which usually pins the
// line and the column beside it). A code with none of those is a diagnostic whose identity no test
// would notice changing.
//
// Both checks stay, and both run in `compile.diag_catalog`: this one narrows what counts, it does
// not replace the wider one, because replacing it would drop the question "does any test at all
// exercise this code" for the codes this one still counts as unpinned, and the wider check is the
// layer that holds that question at zero.
//
// Measured again after the two pass-boundary guards got their cases (runtime 2 -> 0): a broken pass
// in `test::break_exit` / `test::break_ports` hands back the graph each guard refuses.
//
// Measured again after the deletion of FileSystemError (runtime 43 -> 42 codes): runtime 2 of 42.
//
// Measured again after the syntax round: syntax 0 of 19 (four cases: ExtraSemicolon,
// MissingInitializer, UnmatchedClosingDelimiter, ComparisonAcrossLines) and runtime 3 of 43 --
// RegexInvalid, TimeFormatFailed and FormatFailed are pinned by the golden snapshots of
// `diag_render`, whose text comparison requires the rendered name. The three left are the ones no
// minimal program reaches, listed in the round's report: PassBoundaryNodes and PassBoundaryPorts
// need a pass that returns a broken graph, and FileSystemError has no producer at all today.
//
// The ratchet is per section, because the sections stand in very different places and one number
// would hide the other four. Measured when the map was written (307 codes, before
// `FeatureNotSupported` was deleted): internal 0 of 1, lexical 0 of 5, syntax 5 of 19,
// semantic 0 of 238, runtime 6 of 43. A section whose number is low is not a section nobody tested:
// it is where the next case goes, one per round, starting with the one whose message carries a fact
// the reader needs.
//
// The count is a ratchet. `NAME_COVERAGE_RECORDED` is where the tree stands, the check fails when
// the set grows past it -- a case that stopped pinning its code is a regression here -- and it is
// lowered in the commit that adds a case, or in the commit that teaches this counter a form of
// assertion it was missing, which is a correction and is recorded as one. The list is printed so
// the next case can be picked without re-deriving it.
if (mode === '--name-coverage' || mode === '--name-coverage-check') {
    const entries = catalogEntries(records)
    const sectionOf = new Map(entries.map((e) => [e.name, e.type]))
    const plans = walk(path.join(root, 'test', 'plans'), (f) => f.endsWith('.plan.toml'))
    const pinned = new Set()
    for (const file of plans) {
        let text = fs
            .readFileSync(file, 'utf8')
            .replace(/^\s*#.*$/gm, '')
            .replace(/desc\s*=\s*"""([\s\S]*?)"""/g, '')
            .replace(/^\s*desc\s*=.*$/gm, '')
        // `expect_output_not_contains` requires the words to be *absent*, so a name there is the
        // opposite of a pin: drop those lists before anything reads a name out of a needle.
        //
        // One level of brackets is allowed inside the list, and that is not cosmetic. Read up to the
        // first `]`, a list written `["[Error]", "UnknownInternalError"]` is stripped only as far as
        // `["[Error]`, and the rest of it survives as text -- so the absence guard reads as a pin.
        // That is exactly what happened: `InternalDiag::UnknownInternalError` was counted as covered
        // by name for as long as this line was short-sighted, in the two plans that write a bracket
        // before the name (stream, tagged). This count must under-report a pin, never invent one.
        //
        // Measured over test/plans when this was fixed: 940 absence lists, 106 of them holding a
        // string with a bracket in it, spread over 31 plans, and 9 of those leaving a name after the
        // truncation. (The commit that made this change said "46 lists" -- that number was written
        // from the two plans it had looked at, not counted; these are counted.)
        text = text.replace(
            /expect_(?:output|stdout|stderr)_not_contains\s*=\s*\[((?:[^\[\]]|\[[^\[\]]*\])*)\]/g,
            ''
        )
        for (const m of text.matchAll(/expect_diagnostic_name\s*=\s*["']([A-Za-z]+)::([A-Za-z]+)["']/g)) {
            pinned.add(`${m[1]}::${m[2]}`)
        }
        for (const m of text.matchAll(/expect_diagnostic_code\s*=\s*["']?(0[xX][0-9A-Fa-f]{8})["']?/g)) {
            pinned.add(m[1].toLowerCase())
        }
        for (const m of text.matchAll(/\.name\s*===\s*["']([A-Za-z]+)::([A-Za-z]+)["']/g)) {
            pinned.add(`${m[1]}::${m[2]}`)
        }
        for (const m of text.matchAll(/\.code\s*===\s*["'](0[xX][0-9A-Fa-f]{8})["']/g)) {
            pinned.add(m[1].toLowerCase())
        }
        for (const n of text.matchAll(/name=([A-Za-z]+)::([A-Za-z]+)/g)) pinned.add(`${n[1]}::${n[2]}`)
        for (const c of text.matchAll(/\bcode=(0[xX][0-9A-Fa-f]{8})/g)) pinned.add(c[1].toLowerCase())
        for (const n of text.matchAll(/\b([A-Za-z]+Diag)::([A-Za-z0-9_]+)/g)) pinned.add(`${n[1]}::${n[2]}`)
        // The name on its own, without the section in front of it: a needle that requires the word
        // requires the name the compiler emitted, and the registered names are unique enough that a
        // word of them in a needle can only be that code. Only registered names are looked for, and
        // the `not_contains` lists and `desc` blocks are gone by now, so this cannot read prose or an
        // absence guard -- that is the pair of mistakes this counter has made: a reverse guard read as
        // a pin, and a file read as its own test.
        for (const [name, section] of sectionOf) {
            if (text.includes(name)) pinned.add(`${section}::${name}`)
        }
        // A snapshot test compares a whole report against a golden file, so a name in that file is
        // required text: the run has to print it exactly. Read the files the plan points at.
        for (const g of text.matchAll(/^\s*golden\s*=\s*["']([^"']+)["']/gm)) {
            const golden = path.resolve(path.dirname(file), g[1])
            if (!fs.existsSync(golden)) continue
            const body = fs.readFileSync(golden, 'utf8')
            for (const n of body.matchAll(/\b([A-Za-z]+Diag)::([A-Za-z0-9_]+)/g)) pinned.add(`${n[1]}::${n[2]}`)
            for (const [name, section] of sectionOf) {
                if (body.includes(name)) pinned.add(`${section}::${name}`)
            }
        }
        // A test can be a script rather than a plan case, and then the assertion is inside it:
        // `cml_contract.py` runs five refusals and asserts the name each one reports. Only a script
        // under `test/` is a case: this file is one too (`diag_catalog` runs it), and reading it let
        // its own comments pin a name that nothing asserted.
        for (const m of text.matchAll(/^\s*case\s*=\s*["']([^"']+)["']/gm)) {
            if (!/\.(py|mjs|js|sh)$/.test(m[1])) continue
            const script = path.resolve(path.dirname(file), m[1])
            if (!script.startsWith(path.join(root, 'test') + path.sep)) continue
            if (!fs.existsSync(script)) continue
            const body = fs.readFileSync(script, 'utf8')
            for (const [name, section] of sectionOf) {
                if (body.includes(`"${name}"`) || body.includes(`'${name}'`)) {
                    pinned.add(`${section}::${name}`)
                }
            }
        }
    }
    // A code nothing raises cannot be pinned by a case, so it is not a target: it is listed apart
    // and does not sit in the set this check refuses to see grow.
    const raised = producedNames(root)
    const report = SECTIONS.filter((s) => sectionFilter === null || s.id === sectionFilter).map((s) => {
        const all = entries.filter(
            (e) => e.sectionId === s.id && !NO_POSITIVE_CASE.has(e.fullName)
        )
        const unraisable = all.filter((e) => !raised.has(e.fullName))
        const target = all.filter((e) => raised.has(e.fullName))
        const missing = target.filter((e) => !pinned.has(e.fullName) && !pinned.has(hex8(e.code).toLowerCase()))
        return { id: s.id, target, missing, unraisable, recorded: NAME_COVERAGE_RECORDED.get(s.id) }
    })
    if (report.length === 0) {
        console.log(`name coverage: no section named '${sectionFilter}'`)
        process.exit(2)
    }
    for (const r of report) {
        console.log(
            `${r.id} codes with no case that asserts them by name: ${r.missing.length} of ${r.target.length}`
                + ` (recorded ${r.recorded})`
        )
        for (const e of r.missing) console.log(`  ${hex8(e.code)}  ${e.fullName}`)
    }
    const unraisable = report.flatMap((r) => r.unraisable)
    if (unraisable.length > 0) {
        console.log('no path raises these, so no case can pin them:')
        for (const e of unraisable) console.log(`  ${hex8(e.code)}  ${e.fullName}`)
    }
    if (mode === '--name-coverage') process.exit(0)
    if (report.every((r) => r.target.length === 0) || pinned.size === 0) {
        console.log('name coverage: the run read no code or no assertion, which is not a pass')
        process.exit(1)
    }
    for (const r of report) {
        if (r.recorded === undefined || r.recorded === null) {
            console.log(`name coverage: ${r.id} has no recorded value; add one to NAME_COVERAGE_RECORDED`)
            process.exit(1)
        }
        if (r.missing.length > r.recorded) {
            console.log(
                `name coverage: ${r.id} grew by ${r.missing.length - r.recorded} past the ${r.recorded}`
                    + ' this file records: a case that pinned its diagnostic stopped pinning it, or a'
                    + ` ${r.id} code was added with no case of its own. Fix that case, or add one and`
                    + ' lower the record in the same commit, with the reason the number moved.'
            )
            process.exit(1)
        }
    }
    process.exit(0)
}

// ---- whether a case reads the message a code prints -------------------------------------------
// The other three counts ask whether a code exists, is raised, and is named by a case. None of them
// reads the sentence the user sees, and a sentence nobody has ever read is broken as often as not:
// `PassBoundaryNodes` printed "... whose graph has no graph 1 of it has no exit node ..." and
// `PassBoundaryPorts` printed the graph where the node belonged, both from the day they were written
// until a case made them print. So this asks a fifth question, of each code: does some test's
// expected output contain a literal run of the code's own message?
//
// The comparison is against the registry's message with its placeholders as separators: only the
// text between `{0}`, `{1}` ... is fixed, and a run of at least `TEXT_ASSERTED_RUN` characters of it
// has to appear in a needle of a test that pins that code. A shorter template has no fixed run to
// compare and counts as not asserted -- there is nothing in it a reader could have read wrong.
//
// What this cannot see is the arguments: a template whose slots are filled with the wrong values
// (`{1}` given the whole sentence) still matches a needle that quotes the template's fixed text. That
// class was caught by the plan cases that run the program, which is why they exist; this check is for
// the sentence itself, which nothing else reads.
const TEXT_ASSERTED_RUN = 12

const TEXT_COVERAGE_RECORDED = new Map([
    ['semantic', 136],
    ['internal', 0],
    ['lexical', 3],
    ['syntax', 14],
    ['runtime', 27],
])

if (mode === '--text-coverage' || mode === '--text-coverage-check') {
    const entries = catalogEntries(records)
    const sectionOf = new Map(entries.map((e) => [e.name, e.type]))
    const plans = walk(path.join(root, 'test', 'plans'), (f) => f.endsWith('.plan.toml'))
    // Code -> the needles of the tests that pin it.
    const needleOf = new Map()
    const addNeedles = (keys, needles) => {
        if (needles.length === 0) return
        for (const key of keys) {
            if (!needleOf.has(key)) needleOf.set(key, [])
            needleOf.get(key).push(needles.join('\n'))
        }
    }
    for (const file of plans) {
        const whole = fs
            .readFileSync(file, 'utf8')
            .replace(/^\s*#.*$/gm, '')
            .replace(/desc\s*=\s*"""([\s\S]*?)"""/g, '')
            .replace(/^\s*desc\s*=.*$/gm, '')
        for (const block of whole.split(/\[\[tests\]\]/).slice(1)) {
            const pins = new Set()
            for (const m of block.matchAll(/expect_diagnostic_name\s*=\s*["']([A-Za-z]+)::([A-Za-z]+)["']/g))
                pins.add(`${m[1]}::${m[2]}`)
            for (const m of block.matchAll(/expect_diagnostic_code\s*=\s*["']?(0[xX][0-9A-Fa-f]{8})["']?/g))
                pins.add(m[1].toLowerCase())
            for (const m of block.matchAll(/\b([A-Za-z]+Diag)::([A-Za-z0-9_]+)/g)) pins.add(`${m[1]}::${m[2]}`)
            for (const [name, section] of sectionOf) {
                if (block.includes(name)) pins.add(`${section}::${name}`)
            }
            if (pins.size === 0) continue
            const needles = []
            // One level of brackets is allowed inside the list: a needle is often a fragment of a
            // printed value, and 'f64[..]' or '[0, 3, 6, 9]' ends the match early when the list is
            // read up to the first ']'.
            for (const m of block.matchAll(
                /expect_(?:output|stdout|stderr)_contains\s*=\s*\[((?:[^\[\]]|\[[^\[\]]*\])*)\]/g
            )) {
                for (const s of m[1].matchAll(/"((?:[^"\\]|\\.)*)"|'((?:[^'\\]|\\.)*)'/g)) {
                    needles.push(s[1] ?? s[2])
                }
            }
            // A snapshot compares a whole report, so its golden file is expected output too -- and
            // the name in it is what pins the code, since the plan block itself may name none.
            for (const g of block.matchAll(/^\s*golden\s*=\s*["']([^"']+)["']/gm)) {
                const golden = path.resolve(path.dirname(file), g[1])
                if (!fs.existsSync(golden)) continue
                const body = fs.readFileSync(golden, 'utf8')
                needles.push(body)
                for (const m of body.matchAll(/\b([A-Za-z]+Diag)::([A-Za-z0-9_]+)/g)) {
                    pins.add(`${m[1]}::${m[2]}`)
                }
            }
            if (pins.size === 0) continue
            addNeedles(pins, needles)
        }
    }
    // The longest fixed run of a message: the text between its placeholders, most specific first.
    const fixedRuns = (message) =>
        message
            .split(/\{\d+\}/)
            .map((s) => s.trim())
            .filter((s) => s.length >= TEXT_ASSERTED_RUN)
            .sort((a, b) => b.length - a.length)
    const report = SECTIONS.filter((s) => sectionFilter === null || s.id === sectionFilter).map((s) => {
        const all = entries.filter((e) => e.sectionId === s.id && !NO_POSITIVE_CASE.has(e.fullName))
        const reading = all.filter((e) => {
            const runs = fixedRuns(e.message)
            if (runs.length === 0) return false
            const seen = [
                ...(needleOf.get(e.fullName) ?? []),
                ...(needleOf.get(hex8(e.code).toLowerCase()) ?? []),
            ]
            return seen.some((text) => runs.some((run) => text.includes(run)))
        })
        return { id: s.id, all, reading, recorded: TEXT_COVERAGE_RECORDED.get(s.id) }
    })
    if (report.length === 0) {
        console.log(`text coverage: no section named '${sectionFilter}'`)
        process.exit(2)
    }
    for (const [key, why] of NO_POSITIVE_CASE) {
        console.log(`  excepted: ${key} -- ${why}`)
    }
    for (const r of report) {
        console.log(
            `${r.id} codes whose message text a case asserts: ${r.reading.length} of ${r.all.length}`
                + ` (recorded ${r.recorded})`
        )
        for (const e of r.all) {
            const runs = fixedRuns(e.message)
            if (runs.length === 0 || r.reading.includes(e)) continue
            const why = runs.length === 0 ? 'no fixed text' : `no needle has "${runs[0]}"`
            console.log(`  ${hex8(e.code)}  ${e.fullName}: ${why}`)
        }
    }
    if (mode === '--text-coverage') process.exit(0)
    for (const r of report) {
        if (r.recorded === undefined || r.recorded === null) {
            console.log(`text coverage: ${r.id} has no recorded value; add one to TEXT_COVERAGE_RECORDED`)
            process.exit(1)
        }
        if (r.reading.length < r.recorded) {
            console.log(
                `text coverage: ${r.id} fell from ${r.recorded} to ${r.reading.length}: a case's`
                    + ' expected output stopped quoting the message of a code it pins, or the message'
                    + ' changed and no case followed it. Read the sentence, then make the case assert'
                    + ' it -- or lower the record in the commit that lowers it, and say why.'
            )
            process.exit(1)
        }
    }
    process.exit(0)
}

// ---- every registered code has a producer ------------------------------------------------------
// A diagnostic nobody constructs is either a path that should refuse and does not, or a row that
// outlived its last use. Either way the registry is claiming something about the compiler that the
// compiler does not do, and the audit entry "every registered diagnostic code has a producer" is
// the rule this holds. `FeatureNotSupported` was such a row for a month: after the generator
// refusal retired (2026-10-31) nothing constructed it, and no test could see that, because a code
// with no producer is also a code no case can pin.
//
// A producer is an occurrence of the name in code -- src, include, modules -- with comments and the
// message table left out. Not the `.of(...)` shape alone: a code can be chosen by a ternary inside
// the call (`of(cond ? SemanticDiag::A : SemanticDiag::B)`) or returned by a helper that picks one
// (`return SemanticDiag::NotDifferentiable`) and constructed by its caller, and both are producers.
if (mode === '--no-producer' || mode === '--no-producer-check') {
    const entries = catalogEntries(records)
    const produced = producedNames(root)
    const report = SECTIONS.filter((s) => sectionFilter === null || s.id === sectionFilter).map((s) => {
        const all = entries.filter((e) => e.sectionId === s.id)
        const dead = all.filter((e) => !produced.has(e.fullName))
        return { id: s.id, all, dead, recorded: NO_PRODUCER_RECORDED.get(s.id) }
    })
    if (report.length === 0) {
        console.log(`no producer: no section named '${sectionFilter}'`)
        process.exit(2)
    }
    for (const r of report) {
        console.log(
            `${r.id} codes no code constructs: ${r.dead.length} of ${r.all.length}`
                + ` (recorded ${r.recorded})`
        )
        for (const e of r.dead) console.log(`  ${hex8(e.code)}  ${e.fullName}`)
    }
    if (mode === '--no-producer') process.exit(0)
    for (const r of report) {
        if (r.recorded === undefined || r.recorded === null) {
            console.log(`no producer: ${r.id} has no recorded value; add one to NO_PRODUCER_RECORDED`)
            process.exit(1)
        }
        if (r.dead.length > r.recorded) {
            console.log(
                `no producer: ${r.id} grew by ${r.dead.length - r.recorded} past the ${r.recorded} this`
                    + ' file records. A registered code that nothing constructs is either a path that'
                    + ' should refuse and does not, or a row that outlived its last use. Find the path,'
                    + ' or delete the row in the commit that says which it was.'
            )
            process.exit(1)
        }
    }
    process.exit(0)
}

// ---- the gate that replaced the name counter: the registry enumerated, a code asserted by code ---
// P2-19. A code counts only when a plan asserts *it* (`expect_diagnostic_name`,
// `expect_diagnostic_code`, or the same two inside a `verify_script`); free text is not read, because
// free text was what could be pressed. A code no test can provoke is a named exception below, with its
// reason printed. `PINNED_COVERAGE_RECORDED` is the floor this tree stands at.
const PINNED_COVERAGE_EXCEPTIONS = [
    {
        fullName: 'InternalDiag::UnknownInternalError',
        reason:
            'the compiler reporting that it has no code for a state it reached; no program provokes it,'
            + ' so no case can pin it -- the registry entry exists so the reporter has something to say',
    },
]

// Back to 16, and this time it is a reading rather than the prediction it was two commits ago.
// `SemanticDiag::ResidualsAreNotVisible` lost the only case that asserted it when
// `split_residuals_destructured` moved to `SemanticDiag::TypeNotIndexable` (`3765513d5`), which put
// the count at 17 (`149fbaa29`); `79c61958b` then deleted the diagnostic itself, 307 codes -> 306,
// and `--pinned-coverage` now prints "289 of 306 (1 named exception(s), 16 with no assertion)". The
// number moved because coverage moved in both directions, not because the gate was relaxed.
const PINNED_COVERAGE_RECORDED = 16

if (mode === '--pinned-coverage' || mode === '--pinned-coverage-check') {
    const entries = catalogEntries(records)
    const plans = walk(path.join(root, 'test', 'plans'), (f) => f.endsWith('.plan.toml'))
    const asserted = new Set()
    for (const file of plans) {
        const text = fs
            .readFileSync(file, 'utf8')
            .replace(/^\s*#.*$/gm, '')
            .replace(/desc\s*=\s*"""[\s\S]*?"""/g, '')
            .replace(/^\s*desc\s*=.*$/gm, '')
        for (const m of text.matchAll(/expect_diagnostic_name\s*=\s*["']([A-Za-z]+::[A-Za-z0-9_]+)["']/g)) {
            asserted.add(m[1])
        }
        for (const m of text.matchAll(/expect_diagnostic_code\s*=\s*["']?(0[xX][0-9A-Fa-f]{8})["']?/g)) {
            asserted.add(m[1].toLowerCase())
        }
        for (const m of text.matchAll(/\.name\s*===\s*["']([A-Za-z]+::[A-Za-z0-9_]+)["']/g)) {
            asserted.add(m[1])
        }
        for (const m of text.matchAll(/\.code\s*===\s*["'](0[xX][0-9A-Fa-f]{8})["']/g)) {
            asserted.add(m[1].toLowerCase())
        }
    }
    const excepted = new Set(PINNED_COVERAGE_EXCEPTIONS.map((e) => e.fullName))
    const missing = entries.filter(
        (e) => !excepted.has(e.fullName) && !asserted.has(e.fullName) && !asserted.has(hex8(e.code).toLowerCase())
    )
    const covered = entries.length - missing.length - excepted.size
    console.log(
        `diagnostic codes a case asserts by code: ${covered} of ${entries.length}`
            + ` (${excepted.size} named exception(s), ${missing.length} with no assertion)`
    )
    for (const e of PINNED_COVERAGE_EXCEPTIONS) console.log(`  excepted ${e.fullName}: ${e.reason}`)
    for (const e of missing) console.log(`  no assertion ${hex8(e.code)}  ${e.fullName}`)
    if (mode === '--pinned-coverage') process.exit(0)
    if (entries.length === 0 || asserted.size === 0) {
        console.log('pinned coverage: the run read no registry entry or no assertion, which is not a pass')
        process.exit(1)
    }
    if (missing.length > PINNED_COVERAGE_RECORDED) {
        console.log(
            `pinned coverage: ${missing.length} code(s) with no case asserting them, past the`
                + ` ${PINNED_COVERAGE_RECORDED} this file records. Lower the record only in the commit`
                + ' that adds the case, and say why the number moved.'
        )
        process.exit(1)
    }
    process.exit(0)
}
if (mode === '--pin-examples') {
    const entries = catalogEntries(records)
    const tests = loadTests(root)
    const pins = loadExamplePins(root)
    const before = pins.size
    for (const entry of entries) {
        if (pins.has(entry.fullName)) continue
        const example = exampleFor(entry, tests, root, new Map())
        if (!example) continue
        pins.set(entry.fullName, example.rel)
    }
    const wrote = writeExamplePins(root, pins)
    console.log(
        `pinned ${pins.size - before} example(s), ${pins.size} in all` +
            `${wrote ? `, wrote ${EXAMPLES_REL}` : `, ${EXAMPLES_REL} unchanged`}`
    )
    process.exit(0)
}

if (mode === '--check' || mode === '--verify-doc') {
    const unnumbered = records.filter((r) => r.code === null)
    if (unnumbered.length) {
        fail(
            unnumbered.map(
                (r) =>
                    `${r.section.id}:${r.name} has no number yet; ` +
                    `run \`node scripts/diagnostics-catalog.mjs --write\` to take the next free one`
            )
        )
    }
}

if (mode === '--write' || mode === '--generate') {
    const assignmentProblems = assignNumbers(records)
    if (assignmentProblems.length) fail(assignmentProblems)
}

const emitted = []
for (const section of SECTIONS) {
    emitted.push([headerRel(section), emitHeader(layout, section)])
    emitted.push([tableRel(section), emitTable(layout, section)])
}

if (mode === '--check') {
    const verifyProblems = []
    for (const [rel, text] of emitted) verifyFile(root, rel, text, verifyProblems)
    if (verifyProblems.length) fail(verifyProblems)
    console.log(
        `diagnostics registry: ${records.length} codes, every one with a name, a message and what it ` +
            `says about an edit (${records.filter((r) => r.fix === 'edit').length} with one, ` +
            `${records.filter((r) => r.fix !== 'edit').length} with a reason), no value used twice, ` +
            `and the emitted headers agree`
    )
    process.exit(0)
}

if (mode === '--write' || mode === '--generate') {
    const wroteRegistry = rewriteRegistry(root, records, lines)
    let wrote = 0
    for (const [rel, text] of emitted) if (writeFile(root, rel, text)) wrote++
    if (mode === '--write') {
        const entries = catalogEntries(records)
        const tests = loadTests(root)
        const pins = loadExamplePins(root)
        // A diagnostic with no pin yet is pinned from the search this run would have shown, so the
        // page keeps that example from then on instead of moving when a test is added later. The
        // pin file is written only when something was added: a re-run writes nothing.
        //
        // The example of a code is the test that names it, so a code that no plan entry names yet
        // gets no pin: run this before the entry is written and the pin (and the example the page
        // shows) stays empty until a later run, which is not a second pass of this script -- the
        // input changed, not the writer. Write the plan entry first, or run this again after it.
        for (const entry of entries) {
            if (pins.has(entry.fullName)) continue
            const found = exampleFor(entry, tests, root, new Map())
            if (found) pins.set(entry.fullName, found.rel)
        }
        const doc = emitDoc(entries, tests, root, pins)
        if (writeFile(root, DOC_REL, doc)) wrote++
        if (writeExamplePins(root, pins)) wrote++
    }
    const assigned = records.filter((r) => r.assigned).length
    console.log(
        `${mode === '--write' ? 'wrote' : 'generated'} diagnostics/registry.tsv${wroteRegistry ? '' : ' (unchanged)'} ` +
            `and ${wrote} file(s); ${assigned} new number(s), ${records.length} codes`
    )
    process.exit(0)
}

if (mode === '--verify-doc') {
    const entries = catalogEntries(records)
    const expected = emitDoc(entries, loadTests(root), root, loadExamplePins(root))
    const problems = []
    verifyFile(root, DOC_REL, expected, problems)
    if (problems.length) fail(problems)
    console.log('docs/diagnostics.md is up to date')
    process.exit(0)
}
