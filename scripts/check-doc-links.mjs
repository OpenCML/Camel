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
//                                               every `path#Symbol` citation names a file that exists
//                                               and contains that symbol; a citation with a line number is a
//                                               defect, because a line number moves with every edit
//   node scripts/check-doc-links.mjs --citations --root DIR
//                                               check a fixture tree instead of the repository
//   node scripts/check-doc-links.mjs FILE DIR  the given files and directories
//
// Exit status 1 when a link is broken or a citation does not resolve, 2 when the scope is empty or
// names a missing entry: a check of nothing is refused, never reported as a pass. External links
// (http, mailto) are not fetched.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { isRunnerOutput } from '../test/tools/runner-output.mjs'

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
        const child = path.join(abs, name)
        // A tree a run writes is not part of the checkout: `isRunnerOutput` is the one answer to
        // which trees those are (test/tools/runner-output.mjs). A developer tree can hold hundreds
        // of thousands of files there, which is also why the walkers collect one file at a time
        // instead of spreading a directory's whole subtree into one push (that overflowed the stack
        // on such a tree).
        if (fs.statSync(child).isDirectory() && isRunnerOutput(child, root)) continue
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
// page, every plan and every code comment in the tree, so a cited file that does not exist is a
// defect rather than a style issue.
//
// A source citation names a file and a symbol, never a line:
//
//     `src/compile/gir/builder_control.cpp#Builder::buildArm`
//
// A line number moves with every edit, so a citation that carries one is reported as a defect, not
// verified: the check used to compare the cited line's text and that only moved the maintenance
// into an allowlist of citations that no longer matched. What the check can know is that the file
// exists and that it names the symbol. It cannot know that the symbol still supports what the
// sentence says about it; no check can, because the sentence is prose. A bare line shorthand is a
// line citation too and fails the same way. A symbol may be written qualified (`Builder::buildArm`);
// the last component is what a definition carries, so either form is accepted when the file names it.
//
// A path with a quoted anchor (`path # "text"`) belongs to the old mechanism and is not special:
// the anchor is ordinary prose to this check.
//
// Not every `*.md` string is a citation: a script that writes its report names a run output. Only
// the files listed above are read, and in code only a comment line is (a string literal that names
// an output is not a citation). The records (docs/handover, docs/reviews, changes.log) are history
// and are not scanned. EXTERNAL_CITATIONS names files of other repositories: TSE2027 is the paper
// repository, docs/debugger-migration.md is in OpenCML-Ext.
const RECORD_DIRS = new Set(['docs/handover', 'docs/reviews'])
const C_LIKE = new Set(['.c', '.cc', '.cpp', '.h', '.hpp', '.cml', '.js', '.mjs', '.ts'])
const HASH_COMMENT = new Set(['.py', '.sh', '.ps1'])
const SCANNED = new Set(['.md', '.toml', ...C_LIKE, ...HASH_COMMENT])
const CITATION_EXT = 'c|cc|cpp|cxx|h|hpp|inl|cml|mjs|js|ts|py|sh|ps1|cmake|toml|json|yml|yaml'

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
            if (SKIP_DIRS.has(entry.name) || isRunnerOutput(full, root)) continue
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
            if (SKIP_DIRS.has(entry.name) || RECORD_DIRS.has(rel) || isRunnerOutput(full, root)) continue
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
    // A plan's strings are test data (`expect_output_contains = ["file.cml"]`), not citations;
    // a plan cites a file in a comment, like code does.
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
    const symbolCitation = new RegExp(
        `(?<![\\w./*-])((?:\.{1,2}\\/|[\\w@-]+\\/)*[\\w-][\\w.-]*\\.(?:${CITATION_EXT}))#([A-Za-z_][\\w.]*(?:::[A-Za-z_][\\w]*)*)`,
        'g',
    )
    const lineCitation = new RegExp(
        `(?<![\\w./*-])((?:\.{1,2}\\/|[\\w@-]+\\/)*[\\w-][\\w.-]*\\.(?:${CITATION_EXT}))\\b:(\\d+)(?:[-–](\\d+))?(?![\\d:])`,
        'g',
    )
    const pathCitation = /(?<![\w./*-])((?:\.{1,2}\/|[\w@-]+\/)*[\w-][\w.-]*\.md)\b/g
    const shorthand = /`:\d+(?:[-–]\d+)?`/g
    const problems = []
    let symbolCitations = 0
    let symbolVerified = 0
    let pathCitations = 0
    const relative = (file) => path.relative(root, file).replace(/\\/g, '/')
    const problem = (file, line, cite, message) => problems.push(`${relative(file)}:${line}: '${cite}': ${message}`)
    const escapeRe = (text) => text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
    const textCache = new Map()
    const textOf = (file) => {
        if (!textCache.has(file)) textCache.set(file, fs.readFileSync(file, 'utf8'))
        return textCache.get(file)
    }
    const names = (file, symbol) => {
        const parts = symbol.split('::')
        const last = parts[parts.length - 1]
        return [symbol, last].some((name) =>
            new RegExp(`(?<![A-Za-z0-9_])${escapeRe(name)}(?![A-Za-z0-9_])`).test(textOf(file)),
        )
    }
    const decodedOf = (target) => {
        try {
            return decodeURIComponent(target)
        } catch {
            return target
        }
    }
    // The exact path first, then a unique basename; a symbol can break a tie between two files that
    // share a basename, which is the one thing a line number used to do.
    const targetOf = (file, decoded, symbol) => {
        const local = path.resolve(path.dirname(file), decoded)
        const fromRoot = path.resolve(root, decoded)
        const isFile = (candidate) => fs.existsSync(candidate) && fs.statSync(candidate).isFile()
        if (isFile(local)) return { target: local }
        if (isFile(fromRoot)) return { target: fromRoot }
        const candidates = byName.get(path.basename(decoded)) ?? []
        if (candidates.length === 1) return { target: candidates[0] }
        // A definition is stronger evidence than a mention: the test file calls `footprintOf`, the
        // runtime file defines it.
        const defines = (candidate, name) => {
            const last = name.split('::').pop()
            const escaped = escapeRe(last)
            return new RegExp(
                `(?:^|\\n)\\s*(?:class|struct|enum(?:\\s+class)?|union)\\s+${escaped}\\b` +
                    `|(?:^|\\n)\\s*[\\w:<>,*&]+\\s+${escaped}\\s*\\(` +
                    `|\\b${escaped}::${escaped}\\s*\\(`,
            ).test(textOf(candidate))
        }
        if (candidates.length > 1 && symbol) {
            const defs = candidates.filter((candidate) => defines(candidate, symbol))
            if (defs.length === 1) return { target: defs[0] }
            const hits = candidates.filter((candidate) => names(candidate, symbol))
            if (hits.length === 1) return { target: hits[0] }
            return { target: null, ambiguous: candidates }
        }
        return { target: null, ambiguous: candidates.length > 1 ? candidates : null }
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
        const lineAt = (index) => text.slice(0, index).split('\n').length

        for (const m of text.matchAll(symbolCitation)) {
            if (isProseExample(m.index) || !isComment(file, text, m.index)) continue
            if (EXTERNAL_CITATIONS.some((prefix) => m[1].startsWith(prefix))) continue
            const decoded = decodedOf(m[1])
            if (decoded.startsWith('tmp/') || RUN_OUTPUTS.has(path.basename(decoded))) continue
            symbolCitations++
            const symbol = m[2]
            const { target, ambiguous } = targetOf(file, decoded, symbol)
            if (!target) {
                problem(
                    file,
                    lineAt(m.index),
                    `${m[1]}#${symbol}`,
                    ambiguous
                        ? `no such file; ${ambiguous.length} files are named '${path.basename(decoded)}' and none names '${symbol}'`
                        : 'no such file',
                )
                continue
            }
            if (names(target, symbol)) {
                symbolVerified++
                continue
            }
            problem(
                file,
                lineAt(m.index),
                `${m[1]}#${symbol}`,
                `the file exists but does not name '${symbol}'`,
            )
        }

        for (const m of text.matchAll(lineCitation)) {
            if (isProseExample(m.index) || !isComment(file, text, m.index)) continue
            // A bare name that exists nowhere is an example in prose (a diagnostic's example
            // file), not a citation of the tree; a path with a directory, or a missing .md page,
            // is a citation.
            const decoded = decodedOf(m[1])
            if (!decoded.includes('/') && !byName.has(path.basename(decoded))) continue
            problem(
                file,
                lineAt(m.index),
                `${m[1]}:${m[2]}${m[3] === undefined ? '' : `-${m[3]}`}`,
                `names a line; cite '${m[1]}#Symbol' instead`,
            )
        }

        // `path 12–20` and `path 12-20` are line ranges too, and the colon is not what makes them
        // misleading: the numbers move with the edit just the same.
        const bareRange = new RegExp('\\.(?:' + CITATION_EXT + ')`?\\s+(\\d+)\\s*[–-]\\s*(\\d+)', 'g')
        for (const m of text.matchAll(bareRange)) {
            if (isProseExample(m.index) || !isComment(file, text, m.index)) continue
            problem(file, lineAt(m.index), m[0], 'names a line range; cite a symbol instead')
        }

        for (const m of text.matchAll(shorthand)) {
            if (isProseExample(m.index) || !isComment(file, text, m.index)) continue
            problem(file, lineAt(m.index), m[0], 'is a line shorthand; cite a symbol instead')
        }

        for (const m of text.matchAll(pathCitation)) {
            const target = m[1]
            if (EXTERNAL_CITATIONS.some((prefix) => target.startsWith(prefix))) continue
            if (isProseExample(m.index)) continue
            if (!isComment(file, text, m.index)) continue
            const decoded = decodedOf(target)
            if (decoded.startsWith('tmp/') || RUN_OUTPUTS.has(path.basename(decoded))) continue
            const { target: resolved } = targetOf(file, decoded, null)
            if (!resolved) {
                problem(file, lineAt(m.index), target, 'no such file')
                continue
            }
            pathCitations++
        }
    }

    for (const item of problems) console.log(item)
    if (problems.length > 0) {
        console.log(`${problems.length} unverified citation(s)`)
        return 1
    }
    if (symbolCitations + pathCitations === 0) {
        console.log(`no citation in ${files.length} file(s): nothing was checked`)
        return 2
    }
    if (symbolCitations === 0) {
        console.log('no symbol citation: nothing was verified')
        return 2
    }
    console.log(
        `no unverified citation: ${symbolVerified} of ${symbolCitations} symbol citation(s) verified, ` +
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
