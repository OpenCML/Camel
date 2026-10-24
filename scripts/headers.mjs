#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// One two-line header for every hand-written source file (docs/design/public-repo/README.cn.md,
// section 4.5): the copyright line, the SPDX identifier, one blank line, then the file's own
// responsibility note, exactly as it was written. The root LICENSE carries the licence text, so the
// header does not restate it; git records the author and the date, so the header carries neither,
// and no year, which would have to be edited every January.
//
//   node scripts/headers.mjs --check            report every member file whose header is not the
//                                               canonical two lines; write nothing
//   node scripts/headers.mjs --check --root DIR check the files under DIR (the test fixture);
//                                               membership is decided on paths relative to DIR
//   node scripts/headers.mjs --fix              rewrite an old form into the canonical one; a file
//                                               that is already canonical is not touched, so a
//                                               second run reports that there is nothing to do
//   node scripts/headers.mjs --fix --dry-run    list the files --fix would rewrite, write nothing
//
// What makes the guard red, with the line it prints for each:
//
//   missing      no header at all, and no copyright line anywhere in the first 40 lines;
//   legacy       the old comment block is still there: the MIT text restated, Author, Created,
//                Updated, Supported by, a year;
//   wrong line   line 1 or line 2 is not the canonical text (a year in the copyright line is named
//                separately, because it is the most common way the old form comes back);
//   no blank     the header is not followed by exactly one blank line;
//   came back    an element of the old header sits between the header and the first code line:
//                the MIT restatement, Author, Created, Updated, Supported by, a second SPDX line;
//   undecided    the file has an extension that no rule covers, so the policy has not said whether
//                it is hand-written source; the guard reports it instead of skipping it.
//
// Membership is a path allow/deny list, not a guess: every rule below says why it is on its side.
// A file no rule matches is not silently skipped; if its extension is not one of the data or
// document formats in NOT_SOURCE, it is reported as undecided.
//
// Exit status: 0 when every member file is canonical, 1 when one is not, 2 when the scope is empty
// or a flag is wrong. A check of nothing is refused, never reported as a pass.

import fs from 'node:fs'
import path from 'node:path'
import { execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { isRunnerOutput } from '../test/tools/runner-output.mjs'

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(SCRIPT_DIR, '..')

const COPYRIGHT = 'Copyright (c) the OpenCML Organization'
const SPDX = 'SPDX-License-Identifier: MIT'

// C/C++, Camel and the JavaScript tools share `//`; Python and CMake use `#`.
const CPP_EXT = /\.(?:c|cc|cpp|h|hpp|inl)$/
// `.ts` is the VS Code extension's source. It is hand-written source of the same kind as the .mjs
// scripts, so it is named here rather than left undecided; the policy's four script languages did
// not mention it because the extension was the only place it appears. `.cjs` is named for the same
// reason: it is a CommonJS script a test fixture loads with `--require`, hand-written source of the
// same kind as the `.mjs` fixtures beside it, and leaving it undecided fails the guard on a file
// whose header is canonical.
const SCRIPT_EXT = /\.(?:js|mjs|cjs|ts|py|cmake)$/

function commentPrefix(ext) {
    return ext === '.py' || ext === '.cmake' ? '#' : '//'
}

function canonicalLines(ext) {
    const c = commentPrefix(ext)
    return [`${c} ${COPYRIGHT}`, `${c} ${SPDX}`]
}

// ---- membership ------------------------------------------------------------------------------

// Ordered: the first matching rule decides. The deny list comes first, because a denied directory
// must win over an extension rule that covers everything (`.py` anywhere is a script, except a test
// case, an example program or a shell helper).
const RULES = [
    // Deny: not ours, not source, or source whose line numbers are part of what it pins.
    {
        path: /^third_party\//,
        member: false,
        reason: 'vendored third-party code; its own authors and licence stay as they are',
    },
    {
        path: /^docs\//,
        member: false,
        reason: 'documentation, including the source snippets quoted inside it',
    },
    {
        path: /^examples\//,
        member: false,
        reason: 'a tutorial program a reader is meant to copy; a header is noise there',
    },
    {
        path: /^showcase\//,
        member: false,
        reason: 'the flagship models, which become examples/models/; same reason as examples/',
    },
    {
        path: /^test\/cases\//,
        member: false,
        reason: 'test cases and expected outputs: a header would move the diagnostic line numbers they pin',
    },
    {
        path: /^test\/plans\//,
        member: false,
        reason: 'test plans, plan helpers, golden files and this check own fixture: same reason as test/cases/',
    },
    {
        path: /^hello\.cml$/,
        member: false,
        reason: 'the root program, which moves to examples/',
    },
    {
        path: /\.sh$/,
        member: false,
        reason: 'a shell helper; the policy names four script languages, and this is not one of them',
    },

    // Allow: C/C++ that ships in the product.
    {
        path: /^(?:src|include|tools)\//,
        ext: CPP_EXT,
        member: true,
        kind: 'C/C++',
        reason: 'the compiler and runtime, the public libcamel headers and the installable tools',
    },
    {
        path: /^modules\//,
        ext: CPP_EXT,
        member: true,
        kind: 'C/C++',
        reason: 'the native implementation of a standard library module, which stdlib/<m>/native/ will hold',
    },
    {
        path: /^stdlib\/[^/]+\/native\//,
        ext: CPP_EXT,
        member: true,
        kind: 'C/C++',
        reason: 'the native part of a standard library module, in the layout the stdlib reorganisation gives it',
    },
    {
        path: /^test\/tools\//,
        ext: CPP_EXT,
        member: true,
        kind: 'C/C++',
        reason: 'a C helper of the test tooling (the allocator shim), not a test case',
    },
    {
        path: /^bindings\//,
        ext: CPP_EXT,
        member: true,
        kind: 'C/C++',
        reason: 'a host binding of the runtime (the CPython extension module links libcamel and ships with it), which is product code and not a test',
    },

    // Allow: the standard library's declarations.
    {
        path: /^stdlib\/.*\.cml$/,
        member: true,
        kind: 'Camel',
        reason: 'the standard library: the one place its operators and values are declared',
    },

    // Allow: scripts, in the comment syntax of their language.
    {
        path: /./,
        ext: SCRIPT_EXT,
        member: true,
        kind: 'script',
        reason: 'a hand-written script (.js/.mjs/.py/.cmake): build, test, editor or package tooling',
    },

    // Deny: Camel outside the standard library is a program, not the library.
    {
        path: /\.cml$/,
        member: false,
        reason: 'a Camel program (test case, example, benchmark, exemplar, tutorial), not the standard library',
    },
]

// Formats that are data or documents, never hand-written source in a language this policy names.
// A file with one of these extensions that no rule matched is decided: not a member.
const NOT_SOURCE = new Set([
    '',
    '.md',
    '.txt',
    '.toml',
    // The diagnostic registry is a table edited through the generator that owns its layout; like
    // a plan file it is data in the repository, not source in a language this policy names.
    '.tsv',
    '.json',
    '.jsonl',
    '.yml',
    '.yaml',
    '.npy',
    '.npz',
    '.dot',
    '.scopes',
    '.log',
    '.png',
    '.jpg',
    '.jpeg',
    '.gif',
    '.svg',
    '.ico',
    '.def',
    '.in',
    '.cmo',
    '.pdf',
    '.bib',
    '.tex',
    '.csv',
])

/** Decide one path, relative to the root and separated by forward slashes. */
function classify(rel) {
    for (const rule of RULES) {
        if (!rule.path.test(rel)) continue
        if (rule.ext && !rule.ext.test(rel)) continue
        return { member: rule.member, kind: rule.kind, reason: rule.reason }
    }
    const ext = path.extname(rel).toLowerCase()
    if (NOT_SOURCE.has(ext)) {
        return { member: false, reason: 'a data or document format, not source' }
    }
    return {
        member: false,
        undecided: true,
        reason: `${ext || 'a file with no extension'} is not a language the header rules cover`,
    }
}

// ---- which lines are header, which are note --------------------------------------------------

// The old header after its licence prose begins. A line that matches belongs to the header the
// two-line form replaces; the note never starts with one of these. The guard and the fixer read the
// same list, so "the guard is red" and "the fixer rewrites" cannot disagree.
const TRAILER = [
    /^Copyright \(c\)/i,
    /^SPDX-License-Identifier:/i,
    /^Camel is licensed/i,
    /^This software is licensed/i,
    /^You (?:can|may) use this software/i,
    /^you may not use this software/i,
    /^You may obtain (?:a )?copy/i,
    /^MIT license/i,
    /^\[?https?:\/\/(?:www\.)?opensource\.org\/license\/mit\]?$/i,
    /^THIS SOFTWARE IS PROVIDED/i,
    /^WARRANTIES OF ANY/i,
    /^KIND, EITHER EXPRESS/i,
    /^NON-INFRINGEMENT/i,
    /^MERCHANTABILITY/i,
    /^See the (?:the )?MIT license/i,
    /^See the License for/i,
    /^Author:/i,
    /^Created:/i,
    /^Updated:/i,
    /^Supported by:/i,
    /^Permission is hereby granted/i,
    /^The above copyright notice/i,
    /^copies of the Software/i,
    /^THE SOFTWARE IS PROVIDED/i,
    /^FIT FOR A PARTICULAR PURPOSE/i,
    /^IN NO EVENT SHALL/i,
    /^AUTHORS OR COPYRIGHT/i,
]

function isTrailerMarker(text) {
    return TRAILER.some((re) => re.test(text.trim()))
}

function isHeaderLine(text) {
    return text.trim() === '' || isTrailerMarker(text)
}

// A ` * ...` line without its decoration.
function undecorate(line) {
    return line.replace(/^\s*\*+ ?/, '')
}

// ---- text handling ---------------------------------------------------------------------------

function splitText(text) {
    const bom = text.startsWith('\uFEFF') ? '\uFEFF' : ''
    const body = bom ? text.slice(1) : text
    const crlf = body.includes('\r\n')
    const normalized = crlf ? body.replace(/\r\n/g, '\n') : body
    const finalNewline = normalized.endsWith('\n')
    const lines = normalized.split('\n')
    if (finalNewline) lines.pop()
    return { bom, crlf, finalNewline, lines }
}

function joinText({ bom, crlf, finalNewline, lines }) {
    let out = lines.join('\n')
    if (finalNewline) out += '\n'
    if (crlf) out = out.replace(/\n/g, '\r\n')
    return bom + out
}

function stripLeadingBlanks(lines) {
    let k = 0
    while (k < lines.length && lines[k].trim() === '') k++
    return lines.slice(k)
}

// Remove the elements of the old header that came back into the space before the first code line.
// Comment blocks and line comments are read; the first code line ends the sweep. Blank lines stay
// (a note may separate paragraphs); only the markers go, and a block that held nothing else goes
// with them.
function sweepLeadingStale(lines, ext) {
    const c = commentPrefix(ext)
    const out = []
    let i = 0
    while (i < lines.length) {
        const t = lines[i].trim()
        if (t === '') {
            out.push(lines[i])
            i++
            continue
        }
        if (c === '//' && t.startsWith('/*')) {
            let end = i
            while (end < lines.length && !lines[end].includes('*/')) end++
            if (end >= lines.length) break
            if (end === i) {
                // A comment that opens and closes on one line; its content is the whole line.
                const inner = t.replace(/^\/\*+/, '').replace(/\*+\/$/, '').trim()
                if (!isTrailerMarker(inner)) out.push(lines[i])
                i++
                continue
            }
            // A block with text on the opener or the closer line is a note, never the header: it
            // is kept whole rather than read line by line (its content is not ` * `-prefixed).
            const openerText = lines[i].replace(/^\/\*+/, '').trim()
            const closerText = lines[end].slice(0, lines[end].indexOf('*/')).trim()
            if (openerText !== '' || closerText !== '') {
                out.push(...lines.slice(i, end + 1))
                i = end + 1
                continue
            }
            const kept = lines.slice(i + 1, end)
            // The header ends at the last line that matches a marker: a licence sentence wrapped
            // over several lines continues on lines that match nothing, and only the last marker
            // (Author, Supported by, the warranty clause) tells the header from the note below it.
            let last = -1
            for (let k = 0; k < kept.length; k++) {
                if (isTrailerMarker(undecorate(kept[k]))) last = k
            }
            if (last < 0) {
                out.push(...lines.slice(i, end + 1))
                i = end + 1
                continue
            }
            const note = kept.slice(last + 1)
            while (note.length && undecorate(note[0]).trim() === '') note.shift()
            while (note.length && undecorate(note[note.length - 1]).trim() === '') note.pop()
            if (note.length) out.push(lines[i], ...note, lines[end])
            i = end + 1
            continue
        }
        if (t.startsWith(c)) {
            // A run of line comments is read the same way: a run that starts with a header line is
            // header up to its last marker, and a note that happens to contain one keeps its text.
            let end = i
            while (end < lines.length && lines[end].trim().startsWith(c)) end++
            const run = lines.slice(i, end)
            const content = run.map((line) => line.trim().slice(c.length))
            const first = content.findIndex((line) => isTrailerMarker(line))
            if (first === 0) {
                let last = 0
                for (let k = 0; k < content.length; k++) if (isTrailerMarker(content[k])) last = k
                out.push(...run.slice(last + 1))
            } else {
                for (let k = 0; k < run.length; k++) {
                    if (!isTrailerMarker(content[k])) out.push(run[k])
                }
            }
            i = end
            continue
        }
        break
    }
    out.push(...lines.slice(i))
    return stripLeadingBlanks(out)
}

// Split a file's head into what goes and what stays. `lines` starts at line 1, or at line 2 when
// the file has a shebang (a script keeps its `#!` as line 1). The result is the prelude (code that
// stood above an old header), the note (kept verbatim), and the body below them.
function splitHead(lines, ext) {
    const header = canonicalLines(ext)
    const c = commentPrefix(ext)

    // Already canonical: consume the two lines and the blank line; what follows is read for
    // elements that came back.
    if (lines[0] === header[0] && lines[1] === header[1]) {
        const at = lines[2] === '' ? 3 : 2
        return { prelude: [], note: [], body: sweepLeadingStale(lines.slice(at), ext) }
    }

    const copyrightAt = lines.findIndex((line, index) => index < 40 && /Copyright \(c\)/.test(line))
    if (copyrightAt < 0) {
        return { prelude: [], note: [], body: sweepLeadingStale(lines, ext) }
    }

    // The block that holds the old header: the nearest `/*` above the copyright line, provided that
    // only comment decoration stands between the two.
    let open = -1
    for (let k = copyrightAt - 1; k >= 0; k--) {
        const t = lines[k].trim()
        if (t.startsWith('/*')) {
            open = k
            break
        }
        if (t === '' || t.startsWith('*')) continue
        break
    }

    if (open < 0) {
        // A header that is not a block comment (a `//` line, say): drop the contiguous run of
        // header lines from the copyright line and keep everything else.
        let last = copyrightAt
        for (let k = copyrightAt + 1; k < lines.length && isHeaderLine(lines[k]); k++) {
            if (lines[k].trim() !== '') last = k
        }
        return {
            prelude: lines.slice(0, copyrightAt).filter((line) => line.trim() !== ''),
            note: [],
            body: stripLeadingBlanks(lines.slice(last + 1)),
        }
    }

    let close = -1
    for (let k = copyrightAt + 1; k < lines.length; k++) {
        if (lines[k].includes('*/')) {
            close = k
            break
        }
    }
    if (close < 0) close = lines.length - 1

    // The note may be the tail of the header block: everything after the last licence line. The
    // last marker, not the first non-marker, ends the header, because a wrapped licence sentence
    // continues on lines that match nothing (see sweepLeadingStale).
    const inside = lines.slice(open + 1, close)
    let last = -1
    for (let k = 0; k < inside.length; k++) {
        if (isTrailerMarker(undecorate(inside[k]))) last = k
    }
    const noteInside = last >= 0 ? inside.slice(last + 1) : inside
    while (noteInside.length && undecorate(noteInside[noteInside.length - 1]).trim() === '') {
        noteInside.pop()
    }

    const prelude = lines.slice(0, open).filter((line) => line.trim() !== '')
    let after = lines.slice(close + 1)

    // The note may also be the block below the header: a comment block or a run of line comments,
    // taken verbatim.
    let noteOutside = []
    let s = 0
    while (s < after.length && after[s].trim() === '') s++
    if (s < after.length) {
        const first = after[s].trim()
        if (c === '//' && first.startsWith('/*')) {
            let end = s
            while (end < after.length && !after[end].includes('*/')) end++
            if (end < after.length) {
                noteOutside = after.slice(s, end + 1)
                after = after.slice(end + 1)
            }
        } else if (first.startsWith(c)) {
            let end = s
            while (end < after.length && after[end].trim().startsWith(c)) end++
            noteOutside = after.slice(s, end)
            after = after.slice(end)
        }
    }

    const note = []
    if (noteInside.length) note.push(lines[open], ...noteInside, lines[close])
    if (noteOutside.length) {
        const swept = sweepLeadingStale(noteOutside, ext)
        if (swept.length) note.push(...swept)
    }

    return { prelude, note, body: stripLeadingBlanks(after) }
}

/**
 * The canonical form of one file. A canonical file comes back unchanged, byte for byte.
 * @param {string} text
 * @param {string} ext
 * @returns {string}
 */
function canonicalText(text, ext) {
    const file = splitText(text)
    let lines = file.lines
    let shebang = null
    if (lines[0]?.startsWith('#!')) {
        shebang = lines[0]
        lines = lines.slice(1)
    }

    const head = splitHead(lines, ext)
    const out = []
    if (shebang) out.push(shebang)
    out.push(...canonicalLines(ext), '')
    if (head.note.length) out.push(...head.note, '')
    if (head.prelude.length) out.push(...head.prelude, '')
    out.push(...head.body)
    if (!head.note.length && !head.prelude.length && !head.body.length) out.pop()

    return joinText({ ...file, lines: out })
}

// ---- what is wrong, in words -----------------------------------------------------------------

// The old-header elements that stand between the canonical header and the first code line.
function staleElements(lines, ext) {
    const c = commentPrefix(ext)
    const found = []
    let i = 0
    while (i < lines.length) {
        const t = lines[i].trim()
        if (t === '') {
            i++
            continue
        }
        if (c === '//' && t.startsWith('/*')) {
            let end = i
            while (end < lines.length && !lines[end].includes('*/')) end++
            if (end >= lines.length) break
            if (end === i) {
                const inner = t.replace(/^\/\*+/, '').replace(/\*+\/$/, '').trim()
                if (isTrailerMarker(inner)) found.push({ index: i, text: inner })
                i++
                continue
            }
            for (let k = i + 1; k < end; k++) {
                const content = undecorate(lines[k]).trim()
                if (isTrailerMarker(content)) found.push({ index: k, text: content })
            }
            i = end + 1
            continue
        }
        if (t.startsWith(c)) {
            const content = t.slice(c.length).trim()
            if (isTrailerMarker(content)) found.push({ index: i, text: content })
            i++
            continue
        }
        break
    }
    return found
}

const YEAR = /(?:^|\D)(?:19|20)\d{2}(?:\D|$)/

function describe(text, ext) {
    const file = splitText(text)
    const lines = file.lines
    const at = lines[0]?.startsWith('#!') ? 1 : 0
    const header = canonicalLines(ext)
    const copyrightAt = lines.findIndex((line, index) => index < 40 && /Copyright \(c\)/.test(line))
    if (copyrightAt < 0) return ['missing the two-line header (there is no copyright line)']

    const problems = []
    const first = lines[at] ?? ''
    if (first !== header[0]) {
        if (first.trim().startsWith('/*') || first.trim().startsWith('*')) {
            problems.push(
                `legacy header: an old comment block starts at line ${at + 1}, and the copyright line is line ${copyrightAt + 1}`
            )
        } else {
            problems.push(
                `header line ${at + 1} is "${first}" (expected "${header[0]}")` +
                    (YEAR.test(first) ? '; a header line carries a year' : '')
            )
        }
    }
    if (lines[at + 1] !== header[1]) {
        problems.push(`header line ${at + 2} is "${lines[at + 1] ?? ''}" (expected "${header[1]}")`)
    }
    if (lines[at] === header[0] && lines[at + 1] === header[1]) {
        if (lines[at + 2] !== '') {
            problems.push(
                `the header is not followed by a blank line (line ${at + 3} is "${lines[at + 2] ?? ''}")`
            )
        } else if (lines[at + 3] === '') {
            problems.push(`the header is followed by more than one blank line (line ${at + 4})`)
        }
        for (const element of staleElements(lines.slice(at + 3), ext)) {
            problems.push(
                `line ${at + 4 + element.index} "${element.text}" is an element of the old header that came back`
            )
        }
    }
    return problems.length ? problems : ['the two-line header is not in the canonical form']
}

// ---- listing the files -----------------------------------------------------------------------

function fail(code, message) {
    console.error(`headers: ${message}`)
    process.exit(code)
}

// The tracked files of a checkout: what the repository is, without build output or scratch files.
function collectTracked(root) {
    let raw
    try {
        raw = execFileSync('git', ['ls-files', '-z'], {
            cwd: root,
            encoding: 'utf8',
            maxBuffer: 512 * 1024 * 1024,
        })
    } catch (error) {
        fail(2, `cannot list the tracked files of ${root} (is it a git checkout?): ${error.message.split('\n')[0]}`)
    }
    return raw.split('\u0000').filter(Boolean).sort()
}

// `--root` walks the tree, so a fixture that was never added to an index can be checked too. It
// never walks a tree a run writes: those are not part of a checkout, and with `--root .` on a used
// worktree they are hundreds of thousands of files (test/tools/runner-output.mjs is the one
// definition of which trees those are).
function collectWalked(root) {
    const files = []
    const skipped = new Set(['.git', 'node_modules', 'out', 'build'])
    const visit = (dir) => {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            const full = path.join(dir, entry.name)
            if (entry.isDirectory()) {
                if (skipped.has(entry.name) || isRunnerOutput(full, root)) continue
                visit(full)
            } else if (entry.isFile()) {
                files.push(path.relative(root, full).split(path.sep).join('/'))
            }
        }
    }
    visit(root)
    return files.sort()
}

// ---- command line ----------------------------------------------------------------------------

const USAGE = `usage: node scripts/headers.mjs --check|--fix [--root DIR] [--dry-run]

  --check     report every member file whose header is not canonical; write nothing; exit 1
  --fix       rewrite an old header into the canonical two lines; idempotent
  --root DIR  check the files under DIR instead of the tracked files of this checkout
  --dry-run   with --fix: list the rewrites, write nothing
  --help      this text

A member file is a hand-written source file: C/C++ under src/, include/, tools/, modules/ and
stdlib/*/native/; the standard library's .cml; and .js/.mjs/.py/.cmake scripts everywhere except
the denied trees (third_party/, docs/, examples/, showcase/, test/cases/, test/plans/).

The guard is red for a missing header, a header line that is not the canonical text, a year in a
header line, a header that is not followed by one blank line, an element of the old header that
came back (MIT restatement, Author, Created, Updated, Supported by, a second SPDX line), and a
source file whose extension no rule covers (undecided).`

function summary(counts) {
    return `C/C++ ${counts['C/C++']}, Camel ${counts.Camel}, scripts ${counts.script}`
}

function main(argv) {
    const args = argv.slice(2)
    let mode = null
    let root = null
    let dryRun = false
    for (let i = 0; i < args.length; i++) {
        const arg = args[i]
        if (arg === '--check' || arg === '--fix') {
            if (mode) fail(2, 'give exactly one of --check and --fix')
            mode = arg.slice(2)
        } else if (arg === '--root') {
            root = args[++i]
            if (!root) fail(2, '--root needs a directory')
        } else if (arg === '--dry-run') {
            dryRun = true
        } else if (arg === '--help' || arg === '-h') {
            console.log(USAGE)
            return
        } else {
            fail(2, `unknown argument "${arg}" (see --help)`)
        }
    }
    if (!mode) fail(2, 'give --check or --fix (see --help)')
    if (dryRun && mode !== 'fix') fail(2, '--dry-run only means something with --fix')

    const rootDir = root ? path.resolve(root) : REPO_ROOT
    if (!fs.existsSync(rootDir) || !fs.statSync(rootDir).isDirectory()) {
        fail(2, `"${root}" is not a directory`)
    }
    const relPaths = root ? collectWalked(rootDir) : collectTracked(rootDir)

    const members = []
    const undecided = []
    for (const rel of relPaths) {
        const decision = classify(rel)
        if (decision.undecided) undecided.push({ rel, reason: decision.reason })
        else if (decision.member) members.push({ rel, kind: decision.kind })
    }
    if (members.length === 0) {
        fail(2, `no member file under ${rootDir}; refusing to report an empty check as a pass`)
    }

    const byKind = { 'C/C++': 0, Camel: 0, script: 0 }
    for (const member of members) byKind[member.kind]++

    if (mode === 'fix') {
        const rewritten = { 'C/C++': 0, Camel: 0, script: 0 }
        for (const member of members) {
            const abs = path.join(rootDir, member.rel)
            const before = fs.readFileSync(abs, 'utf8')
            const after = canonicalText(before, path.extname(member.rel).toLowerCase())
            if (after === before) continue
            if (dryRun) console.log(`would fix  ${member.rel}  (${member.kind})`)
            else {
                fs.writeFileSync(abs, after, 'utf8')
                console.log(`fixed  ${member.rel}  (${member.kind})`)
            }
            rewritten[member.kind]++
        }
        const total = rewritten['C/C++'] + rewritten.Camel + rewritten.script
        if (total === 0) {
            console.log(`headers: nothing to do: ${members.length} member files already carry the two-line header`)
        } else if (dryRun) {
            console.log(`headers: ${total} files would be rewritten (${summary(rewritten)})`)
        } else {
            console.log(`headers: ${total} files rewritten (${summary(rewritten)})`)
        }
        return
    }

    const wrong = []
    for (const member of members) {
        const abs = path.join(rootDir, member.rel)
        const text = fs.readFileSync(abs, 'utf8')
        const ext = path.extname(member.rel).toLowerCase()
        if (canonicalText(text, ext) === text) continue
        wrong.push({ rel: member.rel, problems: describe(text, ext) })
    }

    for (const file of wrong) for (const problem of file.problems) console.log(`${file.rel}: ${problem}`)
    for (const entry of undecided) console.log(`${entry.rel}: undecided: ${entry.reason}`)

    if (wrong.length === 0 && undecided.length === 0) {
        console.log(
            `headers: every one of ${members.length} member files carries the two-line header (${summary(byKind)})`
        )
        return
    }
    if (wrong.length) {
        console.log(
            `headers: ${wrong.length} of ${members.length} member files have a header that is not canonical (${summary(byKind)})`
        )
    }
    if (undecided.length) {
        console.log(`headers: ${undecided.length} files are undecided; name the language in the rules or deny it`)
    }
    process.exit(1)
}

main(process.argv)