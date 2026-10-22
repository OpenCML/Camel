#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Checks the relative links of the Markdown documents: the target file exists and, for a link
// with a fragment into a Markdown file, a heading with that anchor exists.
//
//   node scripts/check-doc-links.mjs            the reader-facing documents (see SCOPE)
//   node scripts/check-doc-links.mjs --default-roots  the same, ignoring a script path argument
//   node scripts/check-doc-links.mjs --all      every tracked *.md of the repository
//   node scripts/check-doc-links.mjs --technical-index  every page under docs/technical is listed
//                                               in docs/technical/README.md (the list is the tree)
//   node scripts/check-doc-links.mjs --citations  every *.md path cited anywhere in the tree — in a
//                                               document, a code or .cml comment, a plan — exists, and
//                                               every line citation with a quoted anchor is verified
//                                               against the cited line; one without an anchor fails
//   node scripts/check-doc-links.mjs --citations --list FILE
//                                               allow the unanchored citations named one per line in
//                                               FILE; any other unanchored citation fails, and a line
//                                               that no longer names an unanchored citation fails too
//   node scripts/check-doc-links.mjs --citations --root DIR
//                                               check a fixture tree instead of the repository
//   node scripts/check-doc-links.mjs FILE DIR  the given files and directories
//
// Exit status 1 when a link is broken or an anchored citation does not match, 2 when the scope is
// empty or names a missing entry: a check of nothing is refused, never reported as a pass. External
// links (http, mailto) are not fetched.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const args = process.argv.slice(2)
const rootFlag = args.indexOf('--root')
const scriptRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const root = rootFlag < 0 ? scriptRoot : path.resolve(args[rootFlag + 1] ?? '')
if (rootFlag >= 0 && (args[rootFlag + 1] === undefined || !fs.existsSync(root))) {
    console.log(`--root ${args[rootFlag + 1] ?? ''}: no such directory`)
    process.exit(2)
}

const SCOPE = [
    'README.md',
    'CONTRIBUTING.md',
    'SECURITY.md',
    'CODE_OF_CONDUCT.md',
    'ROADMAP.md',
    'docs/README.cn.md',
    'docs/design-principles.cn.md',
    'docs/design-principles.en.md',
    'docs/cli.md',
    'docs/cli.cn.md',
    'docs/language',
    'docs/tutorial',
    'docs/technical/README.md',
    'examples',
]

// `fixture` trees are inputs of the checks themselves (test/plans/feat/*/fixture): their
// citations are deliberately wrong, so the whole-tree runs must not read them.
const SKIP_DIRS = new Set(['.git', 'node_modules', 'third_party', 'out', 'build', 'tmp', 'dist', 'sdks', 'fixture'])
// 'sdks' is upstream CPython: its headers cite CPython's own documentation, not ours.

function markdownFiles(entry) {
    const abs = path.resolve(root, entry)
    if (!fs.existsSync(abs)) return []
    if (fs.statSync(abs).isFile()) return abs.endsWith('.md') ? [abs] : []
    const out = []
    for (const name of fs.readdirSync(abs)) {
        if (SKIP_DIRS.has(name)) continue
        out.push(...markdownFiles(path.join(abs, name)))
    }
    return out
}

// The anchor GitHub gives a heading: lower case, punctuation dropped, spaces to hyphens.
function slug(heading) {
    return heading
        .trim()
        .replace(/!?\[([^\]]*)\]\([^)]*\)/g, '$1')
        .replace(/[`*_~]/g, (c) => (c === '_' ? '_' : ''))
        .toLowerCase()
        .replace(/[^\p{L}\p{N}\p{M}\s_-]/gu, '')
        .replace(/\s/g, '-')
}

function withoutCode(text) {
    // Blank out fenced blocks and inline code, keeping line numbers.
    let out = text.replace(/^(```|~~~)[^\n]*\n[\s\S]*?^\1[^\n]*$/gm, (m) => m.replace(/[^\n]/g, ' '))
    out = out.replace(/`[^`\n]*`/g, (m) => ' '.repeat(m.length))
    return out
}

const anchorCache = new Map()
function anchorsOf(file) {
    if (anchorCache.has(file)) return anchorCache.get(file)
    const text = fs.readFileSync(file, 'utf8')
    const fenced = text.replace(/^(```|~~~)[^\n]*\n[\s\S]*?^\1[^\n]*$/gm, '')
    const seen = new Map()
    const anchors = new Set()
    for (const m of fenced.matchAll(/^#{1,6}\s+(.+?)\s*#*\s*$/gm)) {
        const base = slug(m[1])
        const n = seen.get(base) ?? 0
        seen.set(base, n + 1)
        anchors.add(n === 0 ? base : `${base}-${n}`)
    }
    for (const m of text.matchAll(/<a\s+(?:name|id)="([^"]+)"/g)) anchors.add(m[1])
    anchorCache.set(file, anchors)
    return anchors
}

function check(file) {
    const problems = []
    const text = withoutCode(fs.readFileSync(file, 'utf8'))
    const links = [...text.matchAll(/!?\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+"[^"]*")?\s*\)/g)]
    for (const m of links) {
        const target = m[1]
        if (/^[a-z][a-z0-9+.-]*:/i.test(target)) continue
        const line = text.slice(0, m.index).split('\n').length
        const [rawPath, fragment] = target.split('#')
        const dest = rawPath === '' ? file : path.resolve(path.dirname(file), decodeURIComponent(rawPath))
        if (!fs.existsSync(dest)) {
            problems.push(`${path.relative(root, file)}:${line}: '${target}': no such file`)
            continue
        }
        if (fragment !== undefined && fragment !== '' && dest.endsWith('.md') && fs.statSync(dest).isFile()) {
            const wanted = decodeURIComponent(fragment).toLowerCase()
            if (!anchorsOf(dest).has(wanted)) {
                problems.push(`${path.relative(root, file)}:${line}: '${target}': no heading with this anchor`)
            }
        }
    }
    return problems
}

// The technical index is a table of links, and a link can only be broken when a row exists:
// a page that was added without a row, or whose row was lost in a merge, is silent to the link
// check above. This mode derives the page list from the tree, so the index cannot quietly fall
// behind it. The pages of a subdirectory (jit/) are pages too; the top-level README is the index.
const TECHNICAL = path.join(root, 'docs', 'technical')
const TECHNICAL_INDEX = path.join(TECHNICAL, 'README.md')

function technicalPages(directory = TECHNICAL) {
    if (!fs.existsSync(directory)) return []
    const pages = []
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
        const full = path.join(directory, entry.name)
        if (entry.isDirectory()) {
            if (SKIP_DIRS.has(entry.name)) continue
            pages.push(...technicalPages(full))
        } else if (entry.name.endsWith('.md') && full !== TECHNICAL_INDEX) {
            pages.push(full)
        }
    }
    return pages.sort()
}

function checkTechnicalIndex() {
    if (!fs.existsSync(TECHNICAL_INDEX)) {
        console.log('docs/technical/README.md: no such file')
        return 2
    }
    const pages = technicalPages()
    if (pages.length === 0) {
        console.log('no page under docs/technical: nothing was checked')
        return 2
    }
    const text = withoutCode(fs.readFileSync(TECHNICAL_INDEX, 'utf8'))
    const listed = new Set()
    for (const m of text.matchAll(/!?\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+"[^"]*")?\s*\)/g)) {
        const target = m[1]
        if (/^[a-z][a-z0-9+.-]*:/i.test(target)) continue
        const [rawPath] = target.split('#')
        if (rawPath === '' || !rawPath.endsWith('.md')) continue
        listed.add(path.resolve(path.dirname(TECHNICAL_INDEX), decodeURIComponent(rawPath)))
    }
    const missing = pages.filter((page) => !listed.has(page))
    if (missing.length > 0) {
        for (const page of missing) {
            const name = path.relative(TECHNICAL, page).replace(/\\/g, '/')
            console.log(`docs/technical/README.md: no row for ${name}`)
        }
        console.log(`${missing.length} page(s) under docs/technical are not listed in its index`)
        return 1
    }
    console.log(
        `every page under docs/technical is listed in docs/technical/README.md (${pages.length} page(s))`,
    )
    return 0
}

// A link check reads Markdown links and nothing else, which is how a deleted page could stay
// cited in code comments, .cml comments, plan descriptions and README prose. This mode reads every
// page, every plan and every code comment in the tree and resolves every `*.md` path it finds there.
// A reference to a file that does not exist is a defect, not a style issue.
//
// A citation that names a line promises more than the file's existence: it promises that the line
// still holds what the sentence says. The line is written `path:LINE` or `path:START-END`, and the
// evidence is a quoted anchor after it, inside the same code span:
//
//     `src/runtime/graph.cpp:574 # "case GCNodeKind::Brch:"`
//
// The anchor is text the cited range must contain (whitespace is compared as single spaces); it is
// also what tells an ambiguous short name (`graph.h` names two files) which file was meant. A
// citation whose anchor is not in the cited range fails. The failure names the citing line, the
// target, what the line holds and what was claimed, plus the line the anchor moved to, so a sweep
// that only shifts lines can be repaired by re-deriving the line from the tree instead of adding an
// offset. A citation with no anchor fails: the check refuses to certify a claim it cannot read.
// `--list FILE` is the one checked exception: it names each tolerated unanchored citation, one per
// line with a reason, and the set can only shrink. An unanchored citation not in the file fails, and
// so does a line whose citation has since been anchored or deleted. Taking the anchor from the line
// itself would bless a stale pointer, which is the defect this check exists to prevent. A bare file
// name that exists nowhere is an example in prose (a diagnostic's `a.cml:3`), not a citation of the
// tree; a missing path with a directory or a missing `.md` path is still a failure.
//
// Not every `*.md` string is a citation: a script that writes its report names a run output. Only
// the files listed above are read, and in code only a comment line is (a string literal that names
// an output is not a citation). The records (docs/handover, docs/reviews, changes.log) are history
// and are not scanned. EXTERNAL_CITATIONS names files of other repositories: TSE2027 is the paper
// repository, docs/debugger-migration.md is in OpenCML-Ext.
const RECORD_DIRS = new Set(['docs/handover', 'docs/reviews'])
// Gitignored output of a local test run: a developer tree can hold hundreds of thousands of files
// here, which is why it is skipped by path and why the walkers collect one file at a time instead of
// spreading a directory's whole subtree into one push (that overflowed the stack on such a tree).
const GENERATED_DIRS = new Set(['test/results'])
const C_LIKE = new Set(['.c', '.cc', '.cpp', '.h', '.hpp', '.cml', '.js', '.mjs', '.ts'])
const HASH_COMMENT = new Set(['.py', '.sh', '.ps1'])
const SCANNED = new Set(['.md', '.toml', ...C_LIKE, ...HASH_COMMENT])

const EXTERNAL_CITATIONS = ['TSE2027/', 'docs/debugger-migration.md']
// Names the measurement harness writes at run time; they are outputs, not files of the tree.
const RUN_OUTPUTS = new Set([
    'CLASSIFIER-RULES.md', 'HARNESS-CHANGES.md', 'HARNESS-CHANGES-2.md', 'RESULTS.md', 'perf-baseline.md',
    'latency.md', 'memory.md', 'export.md',
])

function allFiles(directory = root) {
    const out = []
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
        const full = path.join(directory, entry.name)
        if (entry.isDirectory()) {
            if (SKIP_DIRS.has(entry.name) || GENERATED_DIRS.has(path.relative(root, full).replace(/\\/g, '/'))) continue
            for (const file of allFiles(full)) out.push(file)
        } else if (entry.isFile()) {
            out.push(full)
        }
    }
    return out
}

function scannedFiles(directory = root) {
    const out = []
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
        const full = path.join(directory, entry.name)
        const rel = path.relative(root, full).replace(/\\/g, '/')
        if (entry.isDirectory()) {
            if (SKIP_DIRS.has(entry.name) || RECORD_DIRS.has(rel) || GENERATED_DIRS.has(rel)) continue
            for (const file of scannedFiles(full)) out.push(file)
        } else if (entry.isFile() && rel !== 'changes.log' && SCANNED.has(path.extname(entry.name).toLowerCase())) {
            out.push(full)
        }
    }
    return out
}

function isComment(file, text, index) {
    const extension = path.extname(file).toLowerCase()
    const lineStart = text.lastIndexOf('\n', index - 1) + 1
    const prefix = text.slice(lineStart, index)
    if (C_LIKE.has(extension)) return prefix.includes('//') || /^\s*\*/.test(prefix) || prefix.includes('/*')
    // A plan's strings are test data (`expect_output_contains = ["file.cml:12"]`), not citations;
    // a plan cites a line in a comment, like code does.
    if (HASH_COMMENT.has(extension) || extension === '.toml') return prefix.includes('#')
    return true
}

// A fenced block shows an example, often of a file that a plan *will* create; it is not a citation.
// A fence may be indented, as it is inside a list item.
function inFence(text, index) {
    const fences = (text.slice(0, index).match(/^\s*(?:```|~~~)/gm) || []).length
    return fences % 2 === 1
}

function checkCitations() {
    const files = scannedFiles()
    const byName = new Map()
    for (const file of allFiles()) {
        const name = path.basename(file)
        if (!byName.has(name)) byName.set(name, [])
        byName.get(name).push(file)
    }
    const pathCitation = /(?<![\w./*-])((?:\.{1,2}\/|[\w@-]+\/)*[\w-][\w.-]*\.md)\b/g
    // `path:LINE`, `path:START-END`, and the anchor that proves the line: ` # "text on it"`.
    // `(?![\d:])` keeps a diagnostic's `file.cml:12:7` (a column, not a citation) out.
    const lineCitation =
        /(?<![\w./*-])((?:\.{1,2}\/|[\w@-]+\/)*[\w-][\w.-]*\.(?:c|cc|cpp|cxx|h|hpp|inl|cml|mjs|js|ts|py|sh|ps1|cmake|md|toml|json|yml|yaml))\b:(\d+)(?:[-–](\d+))?(?: # "([^"\n]*)")?(?![\d:])(?!\s*:\d)/g
    const problems = []
    const unanchored = new Map()
    let unanchoredCount = 0
    let newUnanchored = 0
    let staleListEntries = 0
    let pathCitations = 0
    let lineCitations = 0
    let verified = 0
    const listFlag = args.indexOf('--list')
    const listPath = listFlag < 0 ? null : path.resolve(args[listFlag + 1] ?? '')
    if (listFlag >= 0 && (args[listFlag + 1] === undefined || !fs.existsSync(listPath))) {
        console.log(`--list ${args[listFlag + 1] ?? ''}: no such file`)
        return 2
    }
    // One line per tolerated unanchored citation: `<citing file>|<cited path>  # reason`. The target
    // as written, not its line numbers, is the key: an edit that only moves a citation does not
    // churn the list, while a second citation to the same target does, because each line permits one
    // occurrence. A key that is now anchored or gone is an error, so the list can only shrink.
    const allowed = new Map()
    if (listPath !== null) {
        for (const [number, raw] of fs.readFileSync(listPath, 'utf8').split(/\r?\n/).entries()) {
            const line = raw.trim()
            if (line === '' || line.startsWith('#')) continue
            const match = /^(\S.*?)\s+#\s*(.+)$/.exec(line)
            const key = (match === null ? line : match[1]).trim()
            const reason = match === null ? '' : match[2].trim()
            if (!key.includes('|') || reason === '') {
                console.log(
                    `${path.relative(process.cwd(), listPath)}:${number + 1}: ` +
                        `want '<citing file>|<cited path>  # <reason>'`,
                )
                return 2
            }
            if (!allowed.has(key)) allowed.set(key, [])
            allowed.get(key).push(reason)
        }
    }
    const relative = (file) => path.relative(root, file).replace(/\\/g, '/')
    const outside = (text) => text.replace(/\s+/g, ' ').trim()
    const lineProblem = (file, line, cite, message) =>
        problems.push(`${relative(file)}:${line}: '${cite}': ${message}`)
    const excerpt = (text) => (text.length > 160 ? `${text.slice(0, 157)}...` : text)
    // Records one unanchored citation and decides whether the list tolerates it.
    const unanchoredAt = (file, line, cite, decoded, message) => {
        const key = `${relative(file)}|${decoded}`
        const seen = (unanchored.get(key) ?? 0) + 1
        unanchored.set(key, seen)
        unanchoredCount++
        if (seen <= (allowed.get(key)?.length ?? 0)) return
        newUnanchored++
        lineProblem(
            file,
            line,
            cite,
            listPath === null
                ? message
                : `unanchored citation is not in ${relative(listPath)}; anchor it or add ` +
                      `"${key}  # <reason>" (${message})`,
        )
    }

    // The cited text may be written with the file's short name (`graph.h`), which two files share.
    // The anchor is the tie-break: exactly one candidate must hold it in the cited range.
    const targetOf = (file, decoded, anchor, start, end) => {
        const local = path.resolve(path.dirname(file), decoded)
        const fromRoot = path.resolve(root, decoded)
        if (fs.existsSync(local) && fs.statSync(local).isFile()) return { target: local }
        if (fs.existsSync(fromRoot) && fs.statSync(fromRoot).isFile()) return { target: fromRoot }
        const candidates = byName.get(path.basename(decoded)) ?? []
        if (candidates.length === 1) return { target: candidates[0] }
        if (candidates.length === 0) return { target: null }
        if (anchor !== null && start >= 1 && end >= start) {
            const holding = candidates.filter((candidate) => {
                const lines = fs.readFileSync(candidate, 'utf8').split(/\r?\n/)
                if (end > lines.length) return false
                return outside(lines.slice(start - 1, end).join('\n')).includes(anchor)
            })
            if (holding.length === 1) return { target: holding[0] }
        }
        return { target: null, ambiguous: candidates }
    }

    for (const file of files) {
        const text = fs.readFileSync(file, 'utf8')
        const markdown = path.extname(file).toLowerCase() === '.md'
        const isProseExample = (index) => {
            let start = index
            // A path inside a URL is not a citation of a file of this repository.
            while (start > 0 && /[\w:/?=&%#+.-]/.test(text[start - 1])) start--
            return /^[a-z][a-z0-9+.-]*:\/\//i.test(text.slice(start, index)) ||
                (markdown && inFence(text, index))
        }

        for (const m of text.matchAll(pathCitation)) {
            const target = m[1]
            if (EXTERNAL_CITATIONS.some((prefix) => target.startsWith(prefix))) continue
            if (isProseExample(m.index)) continue
            if (!isComment(file, text, m.index)) continue
            let decoded = target
            try {
                decoded = decodeURIComponent(target)
            } catch {
                // A malformed escape is still resolved as written.
            }
            if (decoded.startsWith('tmp/') || RUN_OUTPUTS.has(path.basename(decoded))) continue
            const line = text.slice(0, m.index).split('\n').length
            const local = path.resolve(path.dirname(file), decoded)
            const fromRoot = path.resolve(root, decoded)
            const byTailName = byName.get(path.basename(decoded))
            // A name that exists somewhere resolves the citation even when the path was written for
            // another directory: the exact path is the link check's business, existence is this
            // check's.
            if (!fs.existsSync(local) && !fs.existsSync(fromRoot) && !(byTailName && byTailName.length > 0)) {
                lineProblem(file, line, target, 'no such file')
            }
            pathCitations++
        }

        for (const m of text.matchAll(lineCitation)) {
            const target = m[1]
            if (EXTERNAL_CITATIONS.some((prefix) => target.startsWith(prefix))) continue
            if (isProseExample(m.index)) continue
            if (!isComment(file, text, m.index)) continue
            let decoded = target
            try {
                decoded = decodeURIComponent(target)
            } catch {
                // A malformed escape is still resolved as written.
            }
            if (decoded.startsWith('tmp/') || RUN_OUTPUTS.has(path.basename(decoded))) continue
            const start = Number(m[2])
            const end = m[3] === undefined ? start : Number(m[3])
            const anchor = m[4] === undefined ? null : outside(decodeURIComponent(m[4]))
            const cite = `${target}:${m[2]}${m[3] === undefined ? '' : `-${m[3]}`}`
            const { target: resolved, ambiguous } = targetOf(file, decoded, anchor, start, end)
            const line = text.slice(0, m.index).split('\n').length
            if (!resolved) {
                if (ambiguous) {
                    if (anchor === null) {
                        unanchoredAt(
                            file,
                            line,
                            cite,
                            decoded,
                            `cannot be verified: ${ambiguous.length} files are named '${path.basename(decoded)}' ` +
                                'and there is no quoted anchor to say which',
                        )
                    } else {
                        lineProblem(file, line, cite, `cannot be verified: ${ambiguous.length} files are named '${path.basename(decoded)}' and none holds the claimed "${excerpt(anchor)}" in its cited range`)
                    }
                } else if (/[\\/]/.test(decoded) || decoded.endsWith('.md')) {
                    // A missing .md path is already reported by the path check above.
                    if (!decoded.endsWith('.md')) {
                        lineProblem(file, line, cite, `no such file${anchor === null ? '' : `; the citation claims "${excerpt(anchor)}"`}`)
                    }
                }
                // A bare name that exists nowhere is an example in prose (`a.cml:3`), not a citation.
                continue
            }
            lineCitations++
            const lines = fs.readFileSync(resolved, 'utf8').split(/\r?\n/)
            const actual = lines[start - 1] === undefined ? '' : outside(lines[start - 1])
            // The citation with no quoted anchor makes no claim this check can read. It fails unless the
            // checked-in list names it, one line per citation: an unanchored citation that is not in
            // the list cannot enter, and a line whose citation has since been anchored or deleted is
            // itself an error. Taking the anchor from the line instead would bless whatever a stale
            // pointer holds, which is the defect this check exists to prevent.
            if (anchor === null || anchor === '') {
                unanchoredAt(
                    file,
                    line,
                    cite,
                    decoded,
                    'cannot be verified: no quoted anchor; ' +
                        (start < 1 || end > lines.length
                            ? `line ${start}${end === start ? '' : `-${end}`} does not exist (the file has ${lines.length} lines)`
                            : `the cited line holds "${excerpt(actual)}"`),
                )
                continue
            }
            if (start < 1 || end < start || end > lines.length) {
                lineProblem(
                    file,
                    line,
                    cite,
                    `line ${start} does not exist (the file has ${lines.length} lines); the citation claims "${excerpt(anchor)}"`,
                )
                continue
            }
            const range = outside(lines.slice(start - 1, end).join('\n'))
            if (range.includes(anchor)) {
                verified++
                continue
            }
            const found = lines.findIndex((sourceLine) => outside(sourceLine).includes(anchor)) + 1
            lineProblem(
                file,
                line,
                cite,
                `line ${start}${end === start ? '' : `-${end}`} holds "${excerpt(actual)}"; the citation claims "${excerpt(anchor)}"` +
                    (found > 0 ? ` (that text is at line ${found})` : ' (that text is not in the file)'),
            )
        }
    }

    if (listPath !== null) {
        for (const [key, reasons] of allowed) {
            const missing = reasons.length - (unanchored.get(key) ?? 0)
            if (missing <= 0) continue
            staleListEntries += missing
            problems.push(
                `${relative(listPath)}: '${key}': ${missing} listed unanchored citation(s) no longer exist ` +
                    '(anchored or deleted); delete the line',
            )
        }
    }
    for (const problem of problems) console.log(problem)
    if (problems.length > 0) {
        console.log(
            `${problems.length} unverified citation(s); ${verified} anchored, ${unanchoredCount} unanchored ` +
                `(${newUnanchored} new, ${staleListEntries} stale list entr${staleListEntries === 1 ? 'y' : 'ies'})`,
        )
        return 1
    }
    if (lineCitations === 0) {
        console.log(`no line citation in ${files.length} file(s): nothing was checked`)
        return 2
    }
    if (verified === 0) {
        console.log(`no line citation carried a matching anchor: nothing was verified`)
        return 2
    }
    console.log(
        `no unverified anchored citation: ${verified} anchored, ${unanchoredCount} unanchored (0 new), ` +
            `${pathCitations} path citation(s) across ${files.length} file(s)`,
    )
    return 0
}
if (args.includes('--citations')) {
    process.exit(checkCitations())
}
if (args.includes('--technical-index')) {
    process.exit(checkTechnicalIndex())
}
const explicit = args.filter((a) => !a.startsWith('--'))
const entries = args.includes('--all')
    ? ['.']
    : args.includes('--default-roots') || explicit.length === 0
      ? SCOPE
      : explicit
// A scope entry that does not exist is a stale list, not a clean tree: it used to be dropped
// silently, which is how the guard once reported "no broken links in 0 document(s)".
const missing = entries.filter((entry) => !fs.existsSync(path.resolve(root, entry)))
if (missing.length > 0) {
    for (const entry of missing) console.log(`${entry}: no such file or directory`)
    console.log(`${missing.length} scope entr${missing.length === 1 ? 'y does' : 'ies do'} not exist; refusing to check a smaller tree`)
    process.exit(2)
}
const files = [...new Set(entries.flatMap(markdownFiles))].sort()
// A pass over no document proves nothing, whichever way the scope came out empty.
if (files.length === 0) {
    console.log('no Markdown document in the given scope: nothing was checked')
    process.exit(2)
}
const problems = files.flatMap(check)
for (const p of problems) console.log(p.replace(/\\/g, '/'))
if (problems.length > 0) {
    console.log(`${problems.length} broken link(s) in ${files.length} document(s)`)
    process.exit(1)
}
console.log(`no broken links in ${files.length} document(s)`)
