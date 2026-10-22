#!/usr/bin/env node
// Checks the relative links of the Markdown documents: the target file exists and, for a link
// with a fragment into a Markdown file, a heading with that anchor exists.
//
//   node scripts/check-doc-links.mjs            the reader-facing documents (see SCOPE)
//   node scripts/check-doc-links.mjs --default-roots  the same, ignoring a script path argument
//   node scripts/check-doc-links.mjs --all      every tracked *.md of the repository
//   node scripts/check-doc-links.mjs --technical-index  every page under docs/technical is listed
//                                               in docs/technical/README.md (the list is the tree)
//   node scripts/check-doc-links.mjs --citations  every *.md path cited anywhere in the tree — in a
//                                               document, a code or .cml comment, a plan — exists
//   node scripts/check-doc-links.mjs FILE DIR  the given files and directories
//
// Exit status 1 when a link is broken, 2 when the scope is empty or names a missing entry: a
// check of nothing is refused, never reported as a pass. External links (http, mailto) are not fetched.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

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

const SKIP_DIRS = new Set(['.git', 'node_modules', 'third_party', 'out', 'build', 'tmp', 'dist'])

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
// Not every `*.md` string is a citation: a script that writes its report names a run output. Only
// the files listed above are read, and in code only a comment line is (a string literal that names
// an output is not a citation). The records (docs/handover, docs/reviews, changes.log) are history
// and are not scanned. EXTERNAL_CITATIONS names files of other repositories: TSE2027 is the paper
// repository, docs/debugger-migration.md is in OpenCML-Ext.
const RECORD_DIRS = new Set(['docs/handover', 'docs/reviews'])
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
            if (SKIP_DIRS.has(entry.name)) continue
            out.push(...allFiles(full))
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
            if (SKIP_DIRS.has(entry.name) || RECORD_DIRS.has(rel)) continue
            out.push(...scannedFiles(full))
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
    if (HASH_COMMENT.has(extension)) return prefix.includes('#')
    return true
}

// A fenced block shows an example, often of a file that a plan *will* create; it is not a citation.
function inFence(text, index) {
    const fences = (text.slice(0, index).match(/^(?:```|~~~)/gm) || []).length
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
    const citation = /(?<![\w./*-])((?:\.{1,2}\/|[\w@-]+\/)*[\w-][\w.-]*\.md)\b/g
    const problems = []
    let examined = 0
    for (const file of files) {
        const text = fs.readFileSync(file, 'utf8')
        const markdown = path.extname(file).toLowerCase() === '.md'
        for (const m of text.matchAll(citation)) {
            const target = m[1]
            if (EXTERNAL_CITATIONS.some((prefix) => target.startsWith(prefix))) continue
            let start = m.index
            // A path inside a URL is not a citation of a file of this repository.
            while (start > 0 && /[\w:/?=&%#+.-]/.test(text[start - 1])) start--
            if (/^[a-z][a-z0-9+.-]*:\/\//i.test(text.slice(start, m.index))) continue
            if (!isComment(file, text, m.index)) continue
            if (markdown && inFence(text, m.index)) continue
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
                problems.push(`${path.relative(root, file).replace(/\\/g, '/')}:${line}: '${target}': no such file`)
            }
            examined++
        }
    }
    if (examined === 0) {
        console.log(`no .md citation in ${files.length} file(s): nothing was checked`)
        return 2
    }
    for (const problem of problems) console.log(problem)
    if (problems.length > 0) {
        console.log(`${problems.length} missing citation target(s) in ${examined} citation(s)`)
        return 1
    }
    console.log(`no missing citation target in ${examined} citation(s) across ${files.length} file(s)`)
    return 0
}

const args = process.argv.slice(2)
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
