// The diagnostics catalog, read from the sources of the compiler.
//
//   node scripts/diagnostics-catalog.mjs --check      consistency of the code tables (exit 1 on a defect)
//   node scripts/diagnostics-catalog.mjs --write      regenerate docs/diagnostics.md
//   node scripts/diagnostics-catalog.mjs --verify-doc docs/diagnostics.md is what --write would produce
//   node scripts/diagnostics-catalog.mjs --coverage   list the codes no test mentions (report only)
//
// The codes are the enumerators of include/camel/core/error/diagnostics/messages/*.h, and their
// names, messages and suggestions are the tables of src/core/error/diagnostics/messages/*.cpp.
// Both are read as text, so the check needs no build: two enumerators with one value compile
// without complaint (and the table, a hash map, silently keeps one of them), which is how two
// branches that each took "the next free number" collide unnoticed when they are merged.

import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'
import { parseTomlFile } from '../test/tools/toml-lite.mjs'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const HEADER_DIR = path.join(REPO_ROOT, 'include', 'camel', 'core', 'error', 'diagnostics', 'messages')
const TABLE_DIR = path.join(REPO_ROOT, 'src', 'core', 'error', 'diagnostics', 'messages')
const BASE_HEADER = path.join(REPO_ROOT, 'include', 'camel', 'core', 'error', 'diagnostics', 'base.h')
const DOC_PATH = path.join(REPO_ROOT, 'docs', 'diagnostics.md')
const PLAN_DIR = path.join(REPO_ROOT, 'test', 'plans')
const CASE_DIR = path.join(REPO_ROOT, 'test', 'cases')

const SEVERITIES = { 0: 'Error', 1: 'Warn', 2: 'Info', 3: 'Hint' }
const TYPE_TITLES = {
    InternalDiag: 'Internal',
    LexicalDiag: 'Lexical',
    SyntaxDiag: 'Syntax',
    SemanticDiag: 'Semantic',
    RuntimeDiag: 'Run time and passes',
}

function stripComments(text) {
    return text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '')
}

function hex8(value) {
    return '0x' + value.toString(16).toUpperCase().padStart(8, '0')
}

/** DiagType enumerators of base.h: name -> the 8-bit type value. */
function readDiagTypes() {
    const text = stripComments(fs.readFileSync(BASE_HEADER, 'utf8'))
    const body = text.match(/enum class DiagType\s*:\s*uint8_t\s*\{([\s\S]*?)\}/)
    if (!body) throw new Error('DiagType enum not found in base.h')
    const types = {}
    for (const m of body[1].matchAll(/(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)/g)) {
        types[m[1]] = Number(m[2])
    }
    return types
}

/** The enumerators of every `enum class <X>Diag : uint32_t` in the message headers. */
function readEnums() {
    const enums = []
    for (const file of fs.readdirSync(HEADER_DIR).sort()) {
        if (!file.endsWith('.h') || file === 'index.h') continue
        const text = stripComments(fs.readFileSync(path.join(HEADER_DIR, file), 'utf8'))
        for (const m of text.matchAll(/enum class (\w+Diag)\s*:\s*uint32_t\s*\{([\s\S]*?)\}/g)) {
            const members = []
            for (const e of m[2].matchAll(/(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)/g)) {
                members.push({ name: e[1], value: Number(e[2]) })
            }
            enums.push({ type: m[1], file: `include/camel/core/error/diagnostics/messages/${file}`, members })
        }
    }
    return enums
}

/** Reads adjacent C++ string literals starting at `pos`; returns the joined value and the end. */
function readStringLiterals(text, pos) {
    let value = ''
    let any = false
    for (;;) {
        while (pos < text.length && /\s/.test(text[pos])) pos++
        if (text[pos] !== '"') break
        any = true
        pos++
        while (pos < text.length && text[pos] !== '"') {
            if (text[pos] === '\\') {
                const next = text[pos + 1]
                value += next === 'n' ? '\n' : next === 't' ? '\t' : next
                pos += 2
            } else {
                value += text[pos++]
            }
        }
        pos++
    }
    return any ? { value, pos } : null
}

/** The table entries `{ <Type>::<Name>, { "name", "message", "suggestion" } }` of one type. */
function readTables() {
    const tables = {}
    for (const file of fs.readdirSync(TABLE_DIR).sort()) {
        if (!file.endsWith('.cpp')) continue
        const raw = fs.readFileSync(path.join(TABLE_DIR, file), 'utf8')
        // Comments are removed outside string literals only where they start a line: the
        // messages contain "//"-free text, but a URL in one must not be cut.
        const text = raw.replace(/^\s*\/\/[^\n]*$/gm, '').replace(/\/\*[\s\S]*?\*\//g, '')
        for (const m of text.matchAll(/(\w+Diag)::(\w+)\s*,\s*\{/g)) {
            let pos = m.index + m[0].length
            const fields = []
            for (let i = 0; i < 3; i++) {
                const lit = readStringLiterals(text, pos)
                if (!lit) break
                fields.push(lit.value)
                pos = lit.pos
                while (pos < text.length && /\s/.test(text[pos])) pos++
                if (text[pos] === ',') pos++
            }
            const list = (tables[m[1]] ||= [])
            list.push({
                enumerator: m[2],
                name: fields[0],
                message: fields[1],
                suggestion: fields[2],
                file: `src/core/error/diagnostics/messages/${file}`,
            })
        }
    }
    return tables
}

/** The catalog and the defects found while assembling it. */
function loadCatalog() {
    const types = readDiagTypes()
    const enums = readEnums()
    const tables = readTables()
    const problems = []
    const entries = []
    for (const en of enums) {
        if (!(en.type in types)) {
            problems.push(`${en.file}: enum ${en.type} has no DiagType of the same name in base.h`)
            continue
        }
        const byValue = new Map()
        for (const member of en.members) {
            if (member.value > 0xffffff) {
                problems.push(`${en.type}::${member.name} = ${hex8(member.value)} does not fit the 24 bits of a specific code`)
            }
            const severity = (member.value >> 20) & 0xf
            if (!(severity in SEVERITIES)) {
                problems.push(`${en.type}::${member.name} = ${hex8(member.value)} has severity bits ${severity}, which is no severity`)
            }
            if (byValue.has(member.value)) {
                problems.push(
                    `${en.type}: ${byValue.get(member.value)} and ${member.name} have the same value ${hex8(member.value)} (${en.file}); give one of them a free number`
                )
            } else {
                byValue.set(member.value, member.name)
            }
        }
        const table = tables[en.type] || []
        const seen = new Set()
        for (const row of table) {
            if (seen.has(row.enumerator)) {
                problems.push(`${row.file}: ${en.type}::${row.enumerator} has two table entries`)
            }
            seen.add(row.enumerator)
            if (!en.members.some((m) => m.name === row.enumerator)) {
                problems.push(`${row.file}: table entry ${en.type}::${row.enumerator} is not an enumerator`)
            }
        }
        const names = new Set()
        for (const member of en.members) {
            const row = table.find((r) => r.enumerator === member.name)
            if (!row) {
                problems.push(`${en.type}::${member.name} has no entry in the message table (no name, no message)`)
                continue
            }
            if (!row.name) problems.push(`${en.type}::${member.name} has an empty name`)
            if (row.name !== member.name) {
                problems.push(`${en.type}::${member.name} is named '${row.name}' in the table; the name must be the enumerator`)
            }
            if (!row.message) problems.push(`${en.type}::${member.name} has an empty message`)
            if (row.suggestion === undefined) problems.push(`${en.type}::${member.name} has no suggestion field`)
            if (names.has(row.name)) problems.push(`${en.type}: the name '${row.name}' is used twice`)
            names.add(row.name)
            // The templates of every type but RuntimeDiag are std::format strings, where a
            // literal brace is written twice; a run-time fault fills {0}, {1}, ... by index.
            const literal = (t) => (en.type === 'RuntimeDiag' ? t : t.replaceAll('{{', '{').replaceAll('}}', '}'))
            entries.push({
                type: en.type,
                name: row.name || member.name,
                fullName: `${en.type}::${row.name || member.name}`,
                code: ((types[en.type] << 24) | member.value) >>> 0,
                severity: SEVERITIES[(member.value >> 20) & 0xf] || 'Unknown',
                message: literal(row.message || ''),
                suggestion: literal(row.suggestion || ''),
            })
        }
    }
    // Warnings are switched by bare name on the command line (-Wno-<name>), so a warning's name
    // must be unique across the types.
    const warningNames = new Map()
    for (const entry of entries) {
        if (entry.severity !== 'Warn') continue
        const key = entry.name.toLowerCase()
        if (warningNames.has(key)) {
            problems.push(`the warning name '${entry.name}' exists in ${warningNames.get(key)} and ${entry.type}; -W options name warnings without the type`)
        }
        warningNames.set(key, entry.type)
    }
    entries.sort((a, b) => a.code - b.code)
    return { entries, problems }
}

// ---- Tests that mention a diagnostic ----
function walk(dir, predicate, out = []) {
    if (!fs.existsSync(dir)) return out
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) walk(full, predicate, out)
        else if (predicate(full)) out.push(full)
    }
    return out
}

function loadTests() {
    const tests = []
    for (const planPath of walk(PLAN_DIR, (f) => f.endsWith('.plan.toml')).sort()) {
        let plan
        try {
            plan = parseTomlFile(planPath)
        } catch {
            continue
        }
        for (const test of plan.tests || []) {
            tests.push({ ...test, planPath })
        }
    }
    return tests
}

function testText(test) {
    return JSON.stringify(test)
}

/** The text every test asserts on: the plans, their golden files and the scripted cases. */
function coverageCorpus() {
    const files = [
        ...walk(PLAN_DIR, () => true),
        ...walk(CASE_DIR, (f) => /\.(mjs|js|py)$/.test(f)),
        ...walk(path.join(REPO_ROOT, 'exemplars'), (f) => /\.(mjs|json|toml|md)$/.test(f)),
    ]
    return files.map((f) => fs.readFileSync(f, 'utf8')).join('\n')
}

function mentions(corpus, entry) {
    return new RegExp(`\\b${entry.name}\\b`).test(corpus) || corpus.includes(hex8(entry.code))
}

function stripLeadingComment(source) {
    return source.replace(/^\s*\/\*[\s\S]*?\*\/\s*/, '')
}

/** A small program of the negative tests that raises `entry`, with the command that runs it. */
function exampleFor(entry, tests) {
    const candidates = []
    for (const test of tests) {
        if (!test.case || (test.tool && test.tool !== 'camel')) continue
        const exact = test.expect_diagnostic_name === entry.fullName || test.expect_diagnostic_code === hex8(entry.code)
        const loose = !exact && new RegExp(`\\b${entry.name}\\b`).test(testText(test))
        if (!exact && !loose) continue
        const casePath = path.resolve(path.dirname(test.planPath), test.case)
        if (!casePath.endsWith('.cml') || !fs.existsSync(casePath)) continue
        const source = stripLeadingComment(fs.readFileSync(casePath, 'utf8')).replace(/\r\n/g, '\n').trimEnd()
        const lines = source.split('\n').length
        candidates.push({ test, casePath, source, lines, exact })
    }
    // The first test, in the order of the plans, that names the diagnostic outright and whose
    // program is small enough to show; failing that, the first that mentions it. (Not the
    // shortest: a new test would then replace the example of an old code, and the document
    // would change with tests that have nothing to do with it.)
    const small = candidates.filter((c) => c.lines <= 40)
    const best = small.find((c) => c.exact) || small[0]
    if (!best) return null
    const rel = path.relative(REPO_ROOT, best.casePath).replaceAll('\\', '/')
    const vars = { run_nvm: 'std::nvm', run_fvm: 'std::fvm', run_jit: 'std::jit', run_prl: 'std::prl' }
    const args = (best.test.args || [])
        .flat()
        .map((a) => String(a).replace('${case}', rel).replace(/^\$\{(\w+)\}$/, (_, v) => vars[v] || `\${${v}}`))
    const envs = (best.test.envs || []).map((e) => `${e} `).join('')
    return { rel, source: best.source, command: `${envs}camel ${args.join(' ')}` }
}

// ---- Document ----
function fence(text, lang = '') {
    const ticks = text.includes('```') ? '````' : '```'
    return `${ticks}${lang}\n${text}\n${ticks}`
}

function renderDoc(entries, tests) {
    const out = []
    out.push('# Diagnostics Reference')
    out.push('')
    out.push('<!-- Generated by scripts/diagnostics-catalog.mjs --write. Do not edit: change the message tables')
    out.push('     under src/core/error/diagnostics/messages/ (or the tests the examples come from) and run the script. -->')
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
    out.push(`${entries.length} diagnostics: ${entries.filter((e) => e.severity === 'Error').length} errors, ${entries.filter((e) => e.severity === 'Warn').length} warnings.`)
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
            out.push(`## ${TYPE_TITLES[entry.type] || entry.type} (\`${entry.type}\`)`)
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
        const example = exampleFor(entry, tests)
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
    return out.join('\n')
}

// ---- Entry ----
const mode = process.argv[2] || '--check'
const { entries, problems } = loadCatalog()

if (mode === '--check') {
    if (problems.length > 0) {
        console.log(`diagnostics catalog: ${problems.length} problem(s)`)
        for (const p of problems) console.log(`  - ${p}`)
        process.exit(1)
    }
    console.log(`diagnostics catalog: ${entries.length} codes, every one with a name and a message, no value used twice`)
} else if (mode === '--write' || mode === '--verify-doc') {
    if (problems.length > 0) {
        console.log('diagnostics catalog: run --check first; the tables are inconsistent')
        for (const p of problems) console.log(`  - ${p}`)
        process.exit(1)
    }
    const doc = renderDoc(entries, loadTests()) + '\n'
    if (mode === '--write') {
        fs.writeFileSync(DOC_PATH, doc)
        console.log(`wrote ${path.relative(REPO_ROOT, DOC_PATH)} (${entries.length} diagnostics)`)
    } else {
        const current = fs.existsSync(DOC_PATH) ? fs.readFileSync(DOC_PATH, 'utf8').replace(/\r\n/g, '\n') : ''
        if (current !== doc) {
            console.log('docs/diagnostics.md is out of date: run `node scripts/diagnostics-catalog.mjs --write`')
            process.exit(1)
        }
        console.log('docs/diagnostics.md is up to date')
    }
} else if (mode === '--coverage') {
    // A report, not a gate: it always exits 0. A code counts as covered when a plan, a golden
    // file or a scripted case mentions its name or its number.
    const corpus = coverageCorpus()
    const missing = entries.filter((e) => !mentions(corpus, e))
    console.log(`diagnostic codes mentioned by no test: ${missing.length} of ${entries.length}`)
    for (const e of missing) console.log(`  ${hex8(e.code)}  ${e.fullName}`)
} else {
    console.log('usage: node scripts/diagnostics-catalog.mjs --check | --write | --verify-doc | --coverage')
    process.exit(2)
}
