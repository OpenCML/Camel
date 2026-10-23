// The formatter's comment retention, checked on one case: every comment of the case must appear in
// the text camel-format writes for it, and that text must be a fixed point (formatting it again
// changes nothing).
//
// The format gate's own check (camel-parsecheck fmtcheck, test/plans/feat/parse/frontend.plan.toml)
// compares the AST of a formatted file with the AST of the original; a comment is not a node of the
// AST, so a formatter that drops one passes that comparison and silently rewrites the source. A
// second pass that differs is what a formatter must never do in a gate: the gate would flip
// between red and green on the same input.
//
// The payload is the runner's (docs/test-plans.md): case_path, stdout, command, test_tmp.
import { execFileSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'

const payload = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))

function fail(message) {
    console.error(message)
    process.exit(1)
}

/// The comments of a Camel source, in order, as the formatter has to write them: `//` (and `///`)
/// to the end of its line, `/* ... */` whole. A string literal is skipped, so a `//` inside one is
/// not read as a comment.
function commentsOf(text) {
    const found = []
    let i = 0
    while (i < text.length) {
        const c = text[i]
        if (c === "'" || c === '"' || c === '`') {
            i += 1
            while (i < text.length && text[i] !== c) {
                if (text[i] === '\\') i += 1
                i += 1
            }
            i += 1
            continue
        }
        if (c === '/' && text[i + 1] === '/') {
            const end = text.indexOf('\n', i)
            found.push(text.slice(i, end === -1 ? text.length : end))
            i = end === -1 ? text.length : end
            continue
        }
        if (c === '/' && text[i + 1] === '*') {
            const end = text.indexOf('*/', i + 2)
            if (end === -1) {
                found.push(text.slice(i))
                break
            }
            found.push(text.slice(i, end + 2))
            i = end + 2
            continue
        }
        i += 1
    }
    return found
}

const source = fs.readFileSync(payload.case_path, 'utf8')
const written = payload.stdout
const expected = commentsOf(source)

// A case with no comment would make this check examine nothing and pass for that reason.
if (expected.length === 0) {
    fail(`${payload.test_name}: the case holds no comment, so a check on it examines nothing`)
}

const missing = expected.filter((comment) => !written.includes(comment))
if (missing.length > 0) {
    const listed = missing.map((comment) => `    ${JSON.stringify(comment)}`).join('\n')
    fail(
        `${payload.test_name}: ${missing.length} of the ${expected.length} comment(s) of the case ` +
            `are not in the formatter's output:\n${listed}\n` +
            `---- the formatter wrote ----\n${written}`
    )
}

// The fixed point. camel-format is the word the runner ran for this case (payload.command).
const exe = payload.command.split(/\s+/)[0]
const again = path.join(payload.test_tmp, 'again.cml')
fs.mkdirSync(payload.test_tmp, { recursive: true })
fs.writeFileSync(again, written)
const twice = execFileSync(exe, [again], { encoding: 'utf8' })
if (twice !== written) {
    fail(
        `${payload.test_name}: the formatter is not a fixed point: formatting its own output ` +
            `moved ${twice.length - written.length} character(s)\n` +
            `---- once ----\n${written}\n---- twice ----\n${twice}`
    )
}

console.log(`${expected.length} comment(s) kept; the output is a fixed point`)