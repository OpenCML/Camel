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
//
//   node scripts/diagnostics-catalog.mjs --check       registry valid, emitted files agree
//   node scripts/diagnostics-catalog.mjs --write       assign numbers, emit headers, tables, docs
//   node scripts/diagnostics-catalog.mjs --generate    assign numbers, emit headers and tables
//   node scripts/diagnostics-catalog.mjs --verify-doc  docs/diagnostics.md is what --write produces
//   node scripts/diagnostics-catalog.mjs --coverage    list the codes no test mentions (report only)
//   node scripts/diagnostics-catalog.mjs --coverage-check  fail if any code is mentioned by no test
//   --root DIR                                        read and write under DIR (the proof cases)
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
import { parseTomlFile } from '../test/tools/toml-lite.mjs'

const DEFAULT_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const REGISTRY_REL = 'diagnostics/registry.tsv'
const DOC_REL = 'docs/diagnostics.md'

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

function hex6(value) {
    return '0x' + value.toString(16).toUpperCase().padStart(6, '0')
}

function hex8(value) {
    return '0x' + value.toString(16).toUpperCase().padStart(8, '0')
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
        if (fields.length < 5 || fields.length > 6) {
            problems.push(
                `${where}: expected section, name, number, severity, message and help separated by tabs`
            )
            continue
        }
        const [sectionId, name, codeText, severityId, message] = fields
        const suggestion = fields.length === 6 ? fields[5] : ''
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
        const record = { section, severity, name, code, message, suggestion, line: i + 1, index: i }
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

function loadTests(root) {
    const tests = []
    for (const planPath of walk(path.join(root, 'test', 'plans'), (f) => f.endsWith('.plan.toml')).sort()) {
        let plan
        try {
            plan = parseTomlFile(planPath)
        } catch {
            continue
        }
        for (const test of plan.tests || []) tests.push({ ...test, planPath })
    }
    return tests
}

function stripLeadingComment(source) {
    return source.replace(/^\s*\/\*[\s\S]*?\*\/\s*/, '')
}

function exampleFor(entry, tests, root) {
    const candidates = []
    for (const test of tests) {
        if (!test.case || (test.tool && test.tool !== 'camel')) continue
        const exact =
            test.expect_diagnostic_name === entry.fullName ||
            test.expect_diagnostic_code === hex8(entry.code)
        const loose = !exact && new RegExp(`\\b${entry.name}\\b`).test(JSON.stringify(test))
        if (!exact && !loose) continue
        const casePath = path.resolve(path.dirname(test.planPath), test.case)
        if (!casePath.endsWith('.cml') || !fs.existsSync(casePath)) continue
        const source = stripLeadingComment(fs.readFileSync(casePath, 'utf8'))
            .replace(/\r\n/g, '\n')
            .trimEnd()
        const lines = source.split('\n').length
        candidates.push({ test, casePath, source, lines, exact })
    }
    const small = candidates.filter((c) => c.lines <= 40)
    const best = small.find((c) => c.exact) || small[0]
    if (!best) return null
    const rel = path.relative(root, best.casePath).replaceAll('\\', '/')
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
        }))
        .sort((a, b) => a.code - b.code)
}

function emitDoc(entries, tests, root) {
    const out = []
    out.push('# Diagnostics Reference')
    out.push('')
    out.push('<!-- Generated by scripts/diagnostics-catalog.mjs --write. Do not edit: the codes and')
    out.push('     messages are declared in diagnostics/registry.tsv, and the examples come from the')
    out.push('     tests. Add one line there and run the script. -->')
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
            `${entries.filter((e) => e.severity === 'Warn').length} warnings.`
    )
    out.push('')
    out.push('| Code | Name | Severity |')
    out.push('|------|------|----------|')
    for (const entry of entries) {
        out.push(`| \`${hex8(entry.code)}\` | [${entry.fullName}](#${entry.name.toLowerCase()}) | ${entry.severity} |`)
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
        const example = exampleFor(entry, tests, root)
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
for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--root') {
        root = path.resolve(argv[++i] || '')
    } else {
        mode = argv[i]
    }
}
if (!['--check', '--write', '--generate', '--verify-doc', '--coverage', '--coverage-check'].includes(mode)) {
    console.log(
        'usage: node scripts/diagnostics-catalog.mjs --check | --write | --generate | --verify-doc | --coverage | --coverage-check [--root DIR]'
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
    const citedNames = new Set(
        [...asserted.matchAll(/\b(?:Internal|Lexical|Syntax|Semantic|Runtime)Diag::([A-Za-z_]\w*)/g)].map(
            (m) => m[1]
        )
    )
    for (const match of asserted.matchAll(/\bname=([A-Za-z_]\w*), code=/g)) {
        citedNames.add(match[1])
    }
    const citedCodes = new Set()
    for (const match of asserted.matchAll(/\bcode=(0[xX][0-9A-Fa-f]{1,8})/g)) {
        citedCodes.add(hex8(Number(match[1])))
    }
    const inRegistryByName = new Set(entries.map((e) => e.name))
    const inRegistryByCode = new Set(entries.map((e) => hex8(e.code)))
    const inTestsOnly = [
        ...[...citedNames].filter((name) => !inRegistryByName.has(name)).map((n) => `'${n}'`),
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
        `diagnostics registry: ${records.length} codes, every one with a name and a message, ` +
            `no value used twice, and the emitted headers agree`
    )
    process.exit(0)
}

if (mode === '--write' || mode === '--generate') {
    const wroteRegistry = rewriteRegistry(root, records, lines)
    let wrote = 0
    for (const [rel, text] of emitted) if (writeFile(root, rel, text)) wrote++
    if (mode === '--write') {
        const entries = catalogEntries(records)
        const doc = emitDoc(entries, loadTests(root), root)
        if (writeFile(root, DOC_REL, doc)) wrote++
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
    const expected = emitDoc(entries, loadTests(root), root)
    const problems = []
    verifyFile(root, DOC_REL, expected, problems)
    if (problems.length) fail(problems)
    console.log('docs/diagnostics.md is up to date')
    process.exit(0)
}
