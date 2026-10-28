// Every exported rule of the standard library is handed to `check`, checked name by name.
//
// The property this guards is the library's own header rule: no rule enters `stdlib/rules.cml`
// without passing the check. A test that hand-lists the rules -- and all three check cases did --
// makes that property drift silently: a rule is added to the library, the lists are not, and the
// suite stays green while one law of the library is never checked at all. The folding lane measured
// exactly that: `array_sigmoid_chain` was exported, used by the `rules::fusion` pass, and reached
// no `check` call, and the number of rules the check saw (68 of 69) was written down in a comment
// instead of being asserted.
//
// This script reads the library (`stdlib/rules.cml`), the case's source and the case's output, so
// the three lists have to agree:
//
//   * every `export rule <name>` of the library appears in the case (imported and handed to some
//     `check` call) -- a rule added to the library and not to the case fails here;
//   * every name that appears in the check's report is an exported rule of the library -- a name
//     the case invents, or a rule the library dropped, fails here;
//   * every name of the library appears in the check's report -- a rule the case hands over but
//     which produces no line (an instance walk that stopped reaching it) fails here;
//   * no line of the report is `refuted` -- a law of the library that does not hold is fixed, or its
//     guard is, and no law is registered as one the check may refuse.
//
// The case is read with its comments stripped: a rule named in a doc comment and nowhere else is a
// rule the case does not hand over, and a check that counted comment words would not notice.
//
// The payload is the runner's (docs/test-plans.md): case_path, stdout, command, test_tmp, test_name,
// and the absolute repo_root the library is read from.
import fs from 'node:fs'
import path from 'node:path'

const payload = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const input = payload.input ?? payload

function fail(message) {
    console.error(message)
    process.exit(1)
}

const casePath = input.case_path ?? payload.case_path
const stdout = input.normalized?.stdout ?? input.stdout ?? payload.stdout ?? ''
const result = String(input.exit_code ?? payload.exit_code ?? 0)
const repoRoot =
    input.repo_root ??
    payload.repo_root ??
    path.resolve(path.dirname(casePath), '..', '..', '..', '..')
const library = path.join(repoRoot, 'stdlib', 'rules.cml')

if (result !== '0') {
    fail(`${input.test_name ?? payload.test_name}: the check case exited ${result}`)
}
if (!fs.existsSync(library)) {
    fail(`the standard library is not at ${library}, so this check cannot say what it exports`)
}

// A rule's name as the library exports it. A `rule` declaration that is not exported cannot be
// handed to `check` from another module, so the exported ones are the ones a check on coverage can
// speak about; the count of the others is printed rather than asserted.
const libraryText = fs.readFileSync(library, 'utf8')
const exported = []
const notExported = []
for (const line of libraryText.split('\n')) {
    const declared = /^\s*(export\s+)?rule\s+([A-Za-z_][A-Za-z0-9_]*)/.exec(line)
    if (!declared) continue
    if (declared[1]) exported.push(declared[2])
    else notExported.push(declared[2])
}
if (exported.length === 0) {
    fail(`${library} declares no exported rule, so this check examines nothing`)
}

// The names the case mentions in its code: the imported ones and the ones it hands to a call. A
// name that is not an exported rule of the library is as much a drift as a missing one. Comments are
// dropped first, so a rule named in prose counts for nothing.
function withoutComments(text) {
    let out = ''
    let i = 0
    while (i < text.length) {
        const c = text[i]
        if (c === "'" || c === '"' || c === '`') {
            const end = i + 1
            out += c
            i = end
            while (i < text.length && text[i] !== c) {
                if (text[i] === '\\') i += 1
                i += 1
            }
            if (i < text.length) i += 1
            continue
        }
        if (c === '/' && text[i + 1] === '/') {
            const end = text.indexOf('\n', i)
            i = end === -1 ? text.length : end
            continue
        }
        if (c === '/' && text[i + 1] === '*') {
            const end = text.indexOf('*/', i + 2)
            i = end === -1 ? text.length : end + 2
            continue
        }
        out += c
        i += 1
    }
    return out
}

const caseText = withoutComments(fs.readFileSync(casePath, 'utf8'))
const mentioned = new Set()
for (const name of caseText.matchAll(/\b([A-Za-z_][A-Za-z0-9_]*)\b/g)) {
    mentioned.add(name[1])
}
const missing = exported.filter((name) => !mentioned.has(name))
if (missing.length > 0) {
    fail(
        `${input.test_name ?? payload.test_name}: ${library} exports ${exported.length} rule(s) ` +
            `and the case does not hand ${missing.length} of them to the check: ${missing.join(', ')}`
    )
}

// The check's own report: one or more lines per rule, each starting with the verdict word and the
// rule's name, an instance of a generic rule written `name<types>`.
const verdicts = new Map()
const unexpected = new Set()
for (const line of stdout.split('\n')) {
    const reported = /^(proved|refuted|sampled)\s+([A-Za-z_][A-Za-z0-9_]*)/.exec(line.trim())
    if (!reported) continue
    const name = reported[2]
    verdicts.set(name, (verdicts.get(name) ?? 0) + 1)
    if (!exported.includes(name)) unexpected.add(name)
}
if (unexpected.size > 0) {
    fail(
        `${input.test_name ?? payload.test_name}: the report names rule(s) the library does not ` +
            `export: ${[...unexpected].join(', ')}`
    )
}
const unreached = exported.filter((name) => !verdicts.has(name))
if (unreached.length > 0) {
    const lines = stdout.split('\n').filter((line) => line.trim() !== '')
    fail(
        `${input.test_name ?? payload.test_name}: ${unreached.length} of the library's ` +
            `${exported.length} exported rule(s) reached no line of the check's report: ` +
            `${unreached.join(', ')} (the report has ${lines.length} line(s))`
    )
}
// A `refuted` line is the check's verdict that a law of the library does not hold, and no name is
// excused from it: a rule a numeric licence puts outside this comparison passes under that licence
// in the check itself, and a rule with a real counterexample is fixed, or its guard is, rather than
// registered as known.
const refuted = (name) => {
    const word = new RegExp(`^refuted\\s+${name}(?![A-Za-z0-9_])`)
    return stdout.split('\n').some((line) => word.test(line.trim()))
}
const failed = [...verdicts.keys()].filter(refuted)
if (failed.length > 0) {
    fail(
        `${input.test_name ?? payload.test_name}: the check refuted ${failed.length} rule(s) of the ` +
            `library: ${failed.join(', ')}`
    )
}

const checked = [...verdicts.keys()].sort()
console.log(
    `${exported.length} exported rule(s) of ${path.relative(repoRoot, library)} are all handed to ` +
        `the check, and all ${checked.length} of them have a line in its report ` +
        `(${notExported.length} rule(s) are not exported and are not asked about); ` +
        `no rule of them is refuted`
)