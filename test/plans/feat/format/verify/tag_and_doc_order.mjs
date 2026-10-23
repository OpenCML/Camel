// A declaration's tags and its comments, checked on one case: the formatter writes the comments and
// the tag lines of the case in the order the case wrote them, keeps every comment, and its output is
// a fixed point.
//
// The format corpus's own check (camel-parsecheck fmtcheck, test/plans/feat/parse/frontend.plan.toml)
// counts a comment the formatter could not place and compares the AST; the comment-retention check
// beside this one (verify/comments_kept.mjs) counts the comments of the case. Neither one reads the
// order: a formatter that writes every comment and moves a declaration's doc comment below its tag -
// or writes it at the end of the file - passes both, and the source the reader wrote is rewritten
// underneath them. This check is the one that pins the order.
//
// The payload is the runner's (docs/test-plans.md): case_path, stdout, command, test_tmp, test_name.
import { execFileSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'

const payload = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))

function fail(message) {
    console.error(message)
    process.exit(1)
}

/// The comments and the tag lines of a Camel source, in order, with the line each one stands on. A
/// comment is `//` (and `///`) to the end of its line, or `/* ... */` whole; a tag is a line whose
/// first non-blank character is `#` (the `#tag<...>` front matter of a declaration). A string
/// literal is skipped, so a `#` or a `//` inside one is not read as either.
function itemsOf(text) {
    const found = []
    let i = 0
    let line = 1
    let lineStart = 0
    while (i < text.length) {
        const c = text[i]
        if (c === '\n') {
            i += 1
            line += 1
            lineStart = i
            continue
        }
        if (c === "'" || c === '"' || c === '`') {
            i += 1
            while (i < text.length && text[i] !== c) {
                if (text[i] === '\\') i += 1
                if (text[i] === '\n') {
                    line += 1
                    lineStart = i + 1
                }
                i += 1
            }
            i += 1
            continue
        }
        if (c === '/' && text[i + 1] === '/') {
            const end = text.indexOf('\n', i)
            const stop = end === -1 ? text.length : end
            found.push({ kind: 'comment', line, text: text.slice(i, stop).trimEnd() })
            i = stop
            continue
        }
        if (c === '/' && text[i + 1] === '*') {
            const end = text.indexOf('*/', i + 2)
            const stop = end === -1 ? text.length : end + 2
            for (const ch of text.slice(i, stop)) if (ch === '\n') line += 1
            found.push({ kind: 'comment', line, text: text.slice(i, stop) })
            i = stop
            continue
        }
        if (c === '#' && text.slice(lineStart, i).trim() === '') {
            const end = text.indexOf('\n', i)
            const stop = end === -1 ? text.length : end
            found.push({ kind: 'tag', line, text: text.slice(i, stop).trim().replace(/\s+/g, ' ') })
            i = stop
            continue
        }
        i += 1
    }
    return found
}

const source = fs.readFileSync(payload.case_path, 'utf8')
const written = payload.stdout
const expected = itemsOf(source)
const got = itemsOf(written)

// A case that holds no tag, or no comment, or that keeps the two apart makes this check examine
// nothing: it would pass for a formatter that reorders everything, since there would be nothing to
// reorder. The two shapes under it - a tag directly above a comment, and a comment directly above a
// tag - are what the case has to write.
const show = (items) => items.map((item) => `${item.kind}@${item.line} ${item.text}`).join('\n    ')
const tags = expected.filter((item) => item.kind === 'tag')
const comments = expected.filter((item) => item.kind === 'comment')
if (tags.length === 0 || comments.length === 0) {
    fail(
        `${payload.test_name}: the case holds ${tags.length} tag line(s) and ${comments.length} ` +
            `comment(s), so a check on the order of the two examines nothing`
    )
}
const glued = expected.some((item, i) => {
    const next = expected[i + 1]
    if (!next) return false
    if (item.kind === 'tag' && next.kind === 'comment') return next.line === item.line + 1
    if (item.kind === 'comment' && next.kind === 'tag') return next.line === item.line + 1
    return false
})
if (!glued) {
    fail(
        `${payload.test_name}: no tag of the case stands directly above or below a comment, so the ` +
            `case pins no order the formatter could move`
    )
}

const differences = []
for (let i = 0; i < Math.max(expected.length, got.length); i++) {
    const want = expected[i]
    const have = got[i]
    if (!want) differences.push(`${i + 1}: the output writes '${have.text}' where the case has nothing`)
    else if (!have) differences.push(`${i + 1}: the case writes '${want.text}' and the output nothing`)
    else if (want.kind !== have.kind || want.text !== have.text) {
        differences.push(`${i + 1}: the case writes ${want.kind} '${want.text}', the output ${have.kind} '${have.text}'`)
    }
}
if (differences.length > 0) {
    fail(
        `${payload.test_name}: the formatter wrote the tags and the comments of the case in another ` +
            `order (${expected.length} in the case, ${got.length} in the output), at ${differences.length} ` +
            `position(s):\n` +
            differences.slice(0, 6).map((line) => `    ${line}`).join('\n') +
            `\n---- the case ----\n    ${show(expected)}\n---- the formatter wrote ----\n${written}`
    )
}

// The fixed point. camel-format is the word the runner ran for this case (payload.command).
const exe = payload.command.split(/\s+/)[0]
const again = path.join(payload.test_tmp, 'again.cml')
fs.mkdirSync(payload.test_tmp, { recursive: true })
fs.writeFileSync(again, written)
const twice = execFileSync(exe, [again], { encoding: 'utf8' })
if (twice !== written) {
    const once = written.split('\n')
    const againLines = twice.split('\n')
    let at = -1
    for (let i = 0; i < Math.max(once.length, againLines.length); i++) {
        if (once[i] !== againLines[i]) {
            at = i
            break
        }
    }
    fail(
        `${payload.test_name}: the formatter is not a fixed point (once ${once.length} lines, twice ` +
            `${againLines.length}), first difference at line ${at + 1}:\n` +
            `    once  ${JSON.stringify(once[at])}\n` +
            `    twice ${JSON.stringify(againLines[at])}\n` +
            `    once[0..] ${once.slice(0, 16).map((l) => JSON.stringify(l)).join(' ')}\n` +
            `    twice[0..] ${againLines.slice(0, 16).map((l) => JSON.stringify(l)).join(' ')}`
    )
}

console.log(
    `${comments.length} comment(s) and ${tags.length} tag line(s) in the order the case wrote them; ` +
        `the output is a fixed point`
)