#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Checks the relative links of the Markdown documents: the target file exists and, for a link
// with a fragment into a Markdown file, a heading with that anchor exists.
//
//   node scripts/check-doc-links.mjs            the reader-facing documents (see SCOPE)
//   node scripts/check-doc-links.mjs --default-roots  the same, ignoring a script path argument
//   node scripts/check-doc-links.mjs --all      every tracked *.md of the repository
//   node scripts/check-doc-links.mjs --source-line-citations  outside docs/handover no source is cited
//                                               by line (a line moves with every edit; name the symbol)
//   node scripts/check-doc-links.mjs --questions-defaults  every page under docs/handover/questions
//                                               names a default in a heading and gives it text (the
//                                               author's rule of 2026-11-03: a question is asked with
//                                               the answer this side would start on)
//   node scripts/check-doc-links.mjs --technical-index  every page under docs/technical is listed
//                                               in docs/technical/README.md (the list is the tree)
//   node scripts/check-doc-links.mjs --citations  every *.md path cited anywhere in the tree — in a
//                                               document, a code or .cml comment, a plan — exists, and
//                                               every `path#Symbol` citation names a file that exists
//                                               and contains that symbol; a citation with a line number is a
//                                               defect, because a line number moves with every edit
//   node scripts/check-doc-links.mjs --citations --root DIR
//                                               check a fixture tree instead of the repository
//   node scripts/check-doc-links.mjs --self-test  a cache or virtual-environment directory beside
//                                               the checkout is not scanned, and the checkout is
//                                               (the trees in SKIP_DIRS, on a tree it writes itself)
//   node scripts/check-doc-links.mjs FILE DIR  the given files and directories
//
// Over a developer's checkout a `--citations` run reads the checkout and not the trees a build, a
// Python environment or a dependency download leaves beside it (they are the names in SKIP_DIRS).
// Before those names were skipped, one run of this check over such a checkout had not finished
// after 46 minutes and was taken for a hang by two lanes in one night. So: if a run over a
// developer checkout does not end in seconds, it is walking a tree that belongs in that set -- the
// answer is to add the name, never to wait an hour or to read the wait as a red check.
//
// Exit status 1 when a link is broken or a citation does not resolve, 2 when the scope is empty or
// names a missing entry: a check of nothing is refused, never reported as a pass. External links
// (http, mailto) are not fetched.

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
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
//
// A developer's checkout also carries trees that are not its content, and they were the whole cost
// of a `--citations` run on this machine: `.venv` (29,926 files, 826.4 MB) and `.cache` (15,481
// files, 1,047.0 MB) -- 45,407 files and 1.87 GB together, 24,886 of them carrying an extension
// this scan reads (2,922 `.py`, 1,166 `.cml`, 1,624 `.h` and 490 `.ts` of the packages and headers
// that live there) -- and `vendor/`, the dependency trees (744 files). A run that entered them took
// longer than ten minutes and was read as a hang or as a red check rather than as a walk of the
// repository (measured 2026-11-04).
//
// `vendor` is here for two reasons of content and one of cost. Content: git ignores it, so it is
// not content of a checkout, exactly like `sdks`; and its files cite *their own* paths, so the
// check reported citations of this repository that nobody made and the run went red for files of
// other projects (a page named `README` under the other project's `docs`, a `src/shape/...` of
// another project in Eigen's) -- two such rows on this developer checkout. Cost: two of its
// headers send the citation patterns into catastrophic backtracking --
// `nlohmann/detail/meta/type_traits.hpp` costs 208.5 s in the symbol pattern and 205.7 s in the
// line pattern, `taskflow/dsl/task_dsl.hpp` 82.7 s and 82.8 s, both for zero matches. Every one of
// those four measurements is on one 28.8 KB or 4.0 KB file, so it is the pattern and not the walk.
// (No literal path of that kind is written here: this file is scanned too, and the first draft of
// this very paragraph was reported as a citation it never made.)
//
// Git ignores every name below, so none of them is content of a checkout: with them skipped the
// run verifies the same citations and the walk is the repository's own. `venv` is named beside
// `.venv` because a checkout that a build runs in holds either name; `.cache` and `.ccache` are the
// tool and compiler caches a build leaves at the root; `__pycache__` is the bytecode of both.
const SKIP_DIRS = new Set([
    '.git',
    'node_modules',
    'third_party',
    'out',
    'build',
    'tmp',
    'dist',
    'sdks',
    'fixture',
    '.venv',
    'venv',
    '.cache',
    '.ccache',
    '__pycache__',
    'vendor'
])
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
const QUESTIONS_DIR = path.join(root, 'docs', 'handover', 'questions')
const QUESTIONS_INDEX = path.join(QUESTIONS_DIR, 'README.md')
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

// The pending table of docs/handover/questions lists the pages in that directory, and its header
// says how many rows it has. Hand-kept counts went wrong three times in one night: a merge resolved
// to their side dropped a row and the header still carried the old number. So both are derived here:
// the rows of the pending table are the *.md files beside it (README.md aside), one row each, and
// the header's number is the number of those pages.
//
// `--write` generates that table instead of checking it, and the author's 11:10 ruling is why the
// generator exists at all: an index kept by hand and then compared against the directory is two
// sources of truth for one fact, and the second one costs a merge conflict every night. One page,
// one row, the page's own title as the sentence. With `--write` the file is written, and a caller
// that wants a gate runs the generator and then `git diff --exit-code`: a diff means somebody
// edited the page by hand.
//
// THE FACT SOURCE IS THE UNION OF TWO SETS: the rows the file already has, and the pages in the
// directory. It was the directory alone until 2026-11-05, and that read deletes rows: a page that
// leaves `questions/` -- answered into `reviews/`, or dropped by a revert -- takes its row with it
// on the next `--write`, and the check then passes on the smaller table, because the file and the
// generator agree again. That is exactly what commit 56c0bf43e did (65 rows to 17, the deleted
// rows still unanswered), and on the live tree at 43b775401 the same read gave "2 page(s) in the
// directory" against 62 rows in the file: one `--write` would have deleted 61 rows and reported a
// green check. So the generator only ever adds. A row whose page is gone is kept -- retargeted to
// the page of the same name in `reviews/` when that is where it moved, and left as a bare name
// with no link when the page is nowhere in the tree -- because "this question was asked" does not
// stop being true when its page moves. Deleting a row is a human edit and it sticks: a row that is
// neither in the file nor backed by a page in the directory has no source to come back from. The
// cost is that a held row's sentence is not compared against a page (there is no page); every row
// that is backed by a page is still rebuilt from that page, so a hand edit there is still a red.
function pendingPageSentence(pageFile) {
    const text = fs.readFileSync(path.join(QUESTIONS_DIR, pageFile), 'utf8')
    const heading = text.match(/^#\s+(.+)$/m)
    let sentence = heading === null ? '' : heading[1].trim()
    sentence = sentence.replace(/^问[:：]\s*/, '')
    if (sentence === '') {
        const line = text
            .split('\n')
            .map((l) => l.trim())
            .find((l) => l !== '' && !l.startsWith('#') && !l.startsWith('```'))
        sentence = line === undefined ? '' : line
    }
    sentence = sentence.replace(/\|/g, '\\|').replace(/\s+/g, ' ')
    return sentence.length > 120 ? `${sentence.slice(0, 117)}...` : sentence
}

// The rows the file already has, in the order it writes them. Two shapes are read, because the
// generator emits two: a pending row whose first cell is the stem linked to the page beside it, and
// a kept row with a third cell saying where the page is. The first cell of a kept row whose page is
// nowhere in the tree is a bare stem and no link, so a broken link is never written. A cell never
// carries an unescaped `|` (the sentence escapes it), so the cells are read from both ends of the
// line instead of by splitting on the pipe. (No example path is written here: this file is scanned
// as a citation source too, and the first draft of this paragraph was reported as a citation it
// never made -- a path needs only the backticks.)
function questionsRowsInFile() {
    const rows = []
    if (!fs.existsSync(QUESTIONS_INDEX)) return rows
    const text = fs.readFileSync(QUESTIONS_INDEX, 'utf8').replace(/\r\n/g, '\n')
    for (const line of text.split('\n')) {
        const linked = line.match(/^\|\s+\[`([^`]+)`\]\(([^)]+)\)\s+\|\s+(.*?)\s+\|(?:\s+.*?\s+\|)?$/)
        if (linked !== null) {
            rows.push({ stem: linked[1], target: linked[2], sentence: linked[3] })
            continue
        }
        const bare = line.match(/^\|\s+`([^`]+)`\s+\|\s+(.*?)\s+\|\s+.*?\s+\|$/)
        if (bare !== null) rows.push({ stem: bare[1], target: null, sentence: bare[2] })
    }
    return rows
}

function renderQuestionsIndex() {
    const pages = fs
        .readdirSync(QUESTIONS_DIR)
        .filter((name) => name.endsWith('.md') && name !== 'README.md')
        .sort()
    const onDisk = new Set(pages.map(pageStem))
    // Where a page that left the directory went. A same-name page in `reviews/` is the author's own
    // record that he read it (the 18:30 ruling below), and the held row points at it rather than at
    // a file that is not there; nothing else is guessed at, so a row for a page that is nowhere in
    // the tree keeps its name and gets no link.
    const movedTo = new Map()
    if (fs.existsSync(REVIEWS_DIR)) {
        for (const name of fs.readdirSync(REVIEWS_DIR).filter((n) => n.endsWith('.md')).sort()) {
            const stem = pageStem(name)
            if (!movedTo.has(stem)) movedTo.set(stem, name)
        }
    }
    // The union, file first: a row whose page is back in the directory is rebuilt from that page
    // (its title is the sentence, as before), every other row is carried over unchanged. One row
    // per page: the file this replaced held two table bodies, and a duplicate stem is written once.
    const kept = []
    const seen = new Set()
    for (const row of questionsRowsInFile()) {
        if (onDisk.has(row.stem) || seen.has(row.stem)) continue
        seen.add(row.stem)
        const moved = movedTo.get(row.stem)
        kept.push({
            stem: row.stem,
            sentence: row.sentence,
            target: moved === undefined ? null : `../reviews/${moved}`,
        })
    }
    kept.sort((a, b) => (a.stem < b.stem ? -1 : a.stem > b.stem ? 1 : 0))
    const lines = [
        '# 待裁索引',
        '',
        '> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。',
        '>',
        '> 这一页由 `node scripts/check-doc-links.mjs --questions-index --write` 从目录生成，不要手改：',
        '> 规矩在 `docs/handover/reviews/` 的裁定里，落没落地看代码和提交，不看这张表。',
        '',
        `## 现在在等作者：${pages.length} 页`,
        '',
    ]
    if (pages.length === 0) {
        lines.push('（目录为空：没有页在等回答。）')
    } else {
        lines.push('| 页 | 问什么 |', '| --- | --- |')
        for (const page of pages) {
            const stem = pageStem(page)
            lines.push(`| [\`${stem}\`](${page}) | ${pendingPageSentence(page)} |`)
        }
    }
    lines.push('')
    if (kept.length > 0) {
        lines.push(`## 生成器不会删的行：${kept.length} 行（页已不在 \`questions/\` 里）`, '')
        lines.push(
            '> 这些行是从上一版的表里原样带过来的：**生成器只增不删**，页走了行也留着。',
            '> 页搬进了 `reviews/` 的行指到那一页；树里已经没有这一页的行只留名字，不编一个指不到的目标。',
            '> 要删哪一行是作者的编辑，删掉之后生成器不会再把它写回来。',
            '',
            '| 页 | 问什么 | 页在哪 |',
            '| --- | --- | --- |',
        )
        for (const row of kept) {
            const cell = row.target === null ? `\`${row.stem}\`` : `[\`${row.stem}\`](${row.target})`
            const where = row.target === null ? '页不在（`questions/` 与 `reviews/` 都没有）' : '在 `reviews/`'
            lines.push(`| ${cell} | ${row.sentence} | ${where} |`)
        }
        lines.push('')
    }
    return { text: lines.join('\n'), pages: pages.length, kept: kept.length, rows: pages.length + kept.length }
}

function writeQuestionsIndex() {
    const { text, pages, kept } = renderQuestionsIndex()
    fs.writeFileSync(QUESTIONS_INDEX, text, 'utf8')
    console.log(`wrote the pending table: ${pages} page(s) in the directory, ${kept} kept row(s), one row each`)
    return 0
}

// The check is the generator run against the file: the same function builds what the page should
// say and the page is compared with it. There is no second opinion to hold -- a count kept by hand
// beside a directory is the ledger the author retired at 08:10, and a comparator that only knows
// how to count rows is a weaker version of the generator that already knows the whole answer.
function checkQuestionsIndex() {
    if (!fs.existsSync(QUESTIONS_INDEX)) {
        console.log('docs/handover/questions/README.md: no such file')
        return 2
    }
    const { text: expected, pages, kept, rows } = renderQuestionsIndex()
    const actual = fs.readFileSync(QUESTIONS_INDEX, 'utf8').replace(/\r\n/g, '\n')
    if (actual === expected) {
        console.log(
            `the pending table lists ${rows} row(s): ${pages} page(s) in the directory and ${kept} kept row(s), one row each, as the header says`,
        )
        return 0
    }
    const actualLines = actual.split('\n')
    const expectedLines = expected.split('\n')
    const limit = Math.max(actualLines.length, expectedLines.length)
    for (let i = 0; i < limit; i += 1) {
        if (actualLines[i] !== expectedLines[i]) {
            console.log(`docs/handover/questions/README.md:${i + 1}: the page is not what the generator writes`)
            console.log(`  generated: ${expectedLines[i] === undefined ? '(no line)' : expectedLines[i]}`)
            console.log(`  on disk:   ${actualLines[i] === undefined ? '(no line)' : actualLines[i]}`)
            break
        }
    }
    console.log(
        `the pending table is hand-edited: run "node scripts/check-doc-links.mjs --questions-index --write" (${pages} page(s) in the directory, ${kept} kept row(s))`,
    )
    return 1
}

// The author's 18:30 ruling: whether a page has been answered is decided by `reviews/`, not by
// what is left in `questions/` -- a merge can leave the answered page behind, and a leftover reads
// as an unanswered question nobody is working on. So a page in `questions/` that already has a
// page of its own in `reviews/` is a stale copy, and this check names the two files.
const REVIEWS_DIR = path.join(root, 'docs', 'handover', 'reviews')

function pageStem(name) {
    return name.replace(/\.cn\.md$|\.md$/, '')
}

function checkAnsweredPages() {
    if (!fs.existsSync(QUESTIONS_DIR) || !fs.existsSync(REVIEWS_DIR)) return 0
    const reviews = fs.readdirSync(REVIEWS_DIR).filter((name) => name.endsWith('.md'))
    // A page is answered when the reviewer's index names it, whichever way that reads: a page of
    // its own in reviews/, or its name inside the index of a batch of questions
    // (`reviews/questions-<date>.md`), where the reply is a line and not a file. Both are the
    // author's rule of 18:30, and reading only the first would leave every page of a batch
    // looking unanswered.
    // The two halves do different jobs and must not be merged: a file of the same name is strong
    // evidence, so per-question replies (`questions-<slug>.md`) are matched by NAME only and their
    // text is never read; a name inside a batch index (`questions-<date>[-word].md`) is strong
    // because that is where a batch's replies are lines rather than files. A word found in the
    // body of some other page's reply is weak evidence and would report a question nobody answered.
    const indexFiles = reviews.filter((name) => /^questions-\d{4}-\d{2}-\d{2}(-[A-Za-z0-9_]+)?\.md$/.test(name))
    const indexLines = indexFiles.map((name) => ({
        name,
        lines: fs.readFileSync(path.join(REVIEWS_DIR, name), 'utf8').split(/\r?\n/),
    }))
    const problems = []
    for (const page of fs.readdirSync(QUESTIONS_DIR)) {
        if (!page.endsWith('.md') || page === 'README.md') continue
        const stem = pageStem(page)
        const answered = reviews.filter((review) => {
            if (review.startsWith('questions-')) return false // matched by name only; see indexFiles
            const reviewStem = pageStem(review)
            return reviewStem === stem || reviewStem.endsWith(`-${stem}`) || reviewStem.endsWith(stem)
        })
        // A stem shorter than this is a word rather than a page name, and matching one would
        // report a page nobody answered; a wrong report here blocks the next real question, which
        // is worse than a miss.
        const namedIn =
            stem.length < 20
                ? []
                : indexLines
                      .map(({ name, lines }) => {
                          const hits = []
                          lines.forEach((line, i) => {
                              if (line.includes(stem)) hits.push(i + 1)
                          })
                          return { name, hits }
                      })
                      .filter((entry) => entry.hits.length > 0)
        const where = [
            ...answered.map((review) => `docs/handover/reviews/${review}`),
            ...namedIn.map((entry) => `docs/handover/reviews/${entry.name}:${entry.hits.join(',')}`),
        ]
        if (where.length > 0) {
            problems.push(
                `docs/handover/questions/${page}: already answered by ${where.join('; ')}` +
                    ' -- the page was read and the reply is in reviews/, so this copy is left over',
            )
        }
    }
    for (const problem of problems) console.log(problem.replace(/\\/g, '/'))
    if (problems.length > 0) {
        console.log(
            `${problems.length} answered page(s) still in questions/: the reviewer's ruling lives in ` +
                'reviews/, and a page left behind reads as a question nobody is answering',
        )
        return 1
    }
    console.log('no answered page is left in questions/: every page there is still waiting')
    return 0
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
// repository, ECOOP2027 is that same repository under the name its rules file now lives in, and
// docs/debugger-migration.md is in OpenCML-Ext.
const RECORD_DIRS = new Set(['docs/handover', 'docs/reviews'])
const C_LIKE = new Set(['.c', '.cc', '.cpp', '.h', '.hpp', '.cml', '.js', '.mjs', '.ts'])
const HASH_COMMENT = new Set(['.py', '.sh', '.ps1'])
const SCANNED = new Set(['.md', '.toml', ...C_LIKE, ...HASH_COMMENT])
const CITATION_EXT = 'c|cc|cpp|cxx|h|hpp|inl|cml|mjs|js|ts|py|sh|ps1|cmake|toml|json|yml|yaml'

const EXTERNAL_CITATIONS = ['TSE2027/', 'ECOOP2027/', 'docs/debugger-migration.md']
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
//
// This function and `lineAt` below both read the whole prefix of a document, which looks quadratic
// and was twice mistaken for the reason this check is slow: it is not, and the numbers are here so
// that nobody pays for them again (measured 2026-11-03). `lineAt` is called *only when a problem is
// reported*, so on a tree whose citations all resolve it is called zero times. `inFence` is called
// only for a match inside a markdown document, which is a small part of the walk. Neither is on the
// main path: a run over this tree is 1.43 s on the build host and about 3 s locally, of which
// roughly half is reading the 33.7 MB of the 6027 scanned files and the rest is the regexes. An
// index of line starts and of fence ends, asked by binary search, was written and measured: the
// report was byte-identical, the wall clock did not move (2.94 s before, 3.04 s after, with 240 ms
// of the "after" being the index build itself), so it was dropped. What that index did buy is the
// red tree -- 487 KB holding 4000 unresolvable citations went from 1.00 s to 0.41 s -- and a red
// tree is a report, not a check. If this ever does need to be fast, the walk is the thing to
// measure, not this.
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
// The trees `SKIP_DIRS` names are the reason a `--citations` run on a developer's checkout was
// reported as a hang, so "a name in that set is not walked into" is checked here rather than
// promised by the set. This mode writes a tree that is a checkout in miniature -- one page, the
// source file it cites by symbol, another page cited by path -- and one directory per name that
// must not be read, each holding a file whose comment cites a file that does not exist. The run it
// then starts is this script over that tree: a name that leaves `SKIP_DIRS` is a reported citation
// of the missing file (exit 1), and a walk that stops reading real files shows up in the same
// reading, because the count says which files were scanned. Both halves matter: the guard must not
// read a cache, and it must still read the checkout.
//
// The citation in that file is joined from parts, because this script is itself scanned: a literal
// `path#Symbol` on a line that also carries a `//` is a citation of this repository, and the first
// version of this case failed exactly that way -- the guard reported its own test data as an
// unpaid citation, naming the absent source file twice in this file. For the same reason this
// paragraph names no such literal either. One ghost file per name below, and the names are written
// down here rather than read from `SKIP_DIRS`: a list the case derives from the set it tests cannot
// report a name that left that set, which is the one failure it exists for (measured: a first
// version of it built the tree out of `[...SKIP_DIRS]` and stayed green with the set emptied).
const ghostCitation = (marker) =>
    `${marker} cites ${['src/absent', 'c'].join('.')}#Absent and ${['gone/missing', 'md'].join('.')}\n`

const MUST_SKIP = [
    '.git',
    'node_modules',
    'third_party',
    'out',
    'build',
    'tmp',
    'dist',
    'sdks',
    'fixture',
    '.venv',
    'venv',
    '.cache',
    '.ccache',
    '__pycache__',
    'vendor'
]

function selfTestSkipTrees() {
    return MUST_SKIP.map((tree) => [tree, path.join('lib', 'ghost.ts'), ghostCitation('//')])
}

function selfTestSkipDirs() {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-doc-links-skip-'))
    const write = (relative, text) => {
        const file = path.join(directory, relative)
        fs.mkdirSync(path.dirname(file), { recursive: true })
        fs.writeFileSync(file, text)
    }
    try {
        write('notes.md', '# notes\n\nThe walk reads `src/thing.c#Thing`, and `docs/page.md` is a page.\n')
        write('src/thing.c', 'int Thing(void) { return 0; }\n')
        write('docs/page.md', '# page\n')
        const trees = selfTestSkipTrees()
        for (const [tree, relative, text] of trees) write(path.join(tree, relative), text)

        const run = spawnSync(process.execPath, [fileURLToPath(import.meta.url), '--citations', '--root', directory], {
            encoding: 'utf8'
        })
        const reading = (run.stdout ?? '').trim().split('\n').filter((line) => line !== '')
        const expected =
            'no unverified citation: 1 of 1 symbol citation(s) verified, 1 path citation(s) across 3 file(s)'
        // One line and one count: a problem line means a skipped tree was walked, and a different
        // file count means the three files of the miniature checkout were not all read.
        if (run.status === 0 && reading.length === 1 && reading[0] === expected) {
            console.log(
                `the ${trees.length} tree(s) a checkout carries and SKIP_DIRS must name were not scanned: ${reading[0]}`
            )
            return 0
        }
        for (const line of reading) console.log(`  ${line}`)
        console.log(`the walk read a tree SKIP_DIRS names, or stopped reading a real file: ${run.status}`)
        return 1
    } finally {
        fs.rmSync(directory, { recursive: true, force: true })
    }
}
if (args.includes('--self-test')) {
    process.exit(selfTestSkipDirs())
}
// A rule about the pages that only a reader of the rule could keep: "我方新问题一律一页一问放进
// docs/handover/questions/" and "每页都要写默认建议，并当轮按默认开工" (author, 2026-11-03). The second
// half is a property of the page, so it is checked on the page rather than remembered: a section
// whose heading names a default, and at least one line of text under it. The report names which of
// the two is missing, because "this page is not compliant" sends the reader back to the rule to work
// out which half it broke.
function checkQuestionsHaveDefaults() {
    if (!fs.existsSync(QUESTIONS_DIR)) {
        console.log(`${QUESTIONS_DIR}: no such directory`)
        return 2
    }
    const pages = fs
        .readdirSync(QUESTIONS_DIR)
        .filter((name) => name.endsWith('.cn.md'))
        .sort()
    if (pages.length === 0) {
        // An empty queue is the normal end state, not a hole in the check: the author's process
        // moves a page they have read whole into `reviews/`, so `questions/` empty while
        // `reviews/` holds the pages that were read is the state this directory is meant to
        // reach. A check that reds in its own success state is a permanent red -- and this one
        // hangs off `scripts/release-gate.mjs`, so it would red the release on the day the work
        // finished. Only "neither directory has a page" means nothing was ever written, which is
        // the state the second half of the rule is about, and that stays a failure.
        const reviewed = fs.existsSync(REVIEWS_DIR)
            ? fs.readdirSync(REVIEWS_DIR).filter((name) => name.endsWith('.md')).length
            : 0
        if (reviewed > 0) {
            console.log(
                `0 pages: nothing is waiting for an answer, and ${reviewed} page(s) under reviews/ are the ones that were read`,
            )
            return 0
        }
        console.log(
            `${QUESTIONS_DIR}: no page to check and no page under ${REVIEWS_DIR} either, so no question was ever written`,
        )
        return 2
    }
    let checked = 0
    for (const page of pages) {
        const file = path.posix.join(QUESTIONS_DIR, page)
        const lines = fs.readFileSync(path.join(QUESTIONS_DIR, page), 'utf8').replace(/\r\n/g, '\n').split('\n')
        const isHeading = (line) => /^#{1,6}\s+\S/.test(line)
        const start = lines.findIndex((line) => isHeading(line) && /(默认|default)/i.test(line))
        if (start === -1) {
            console.log(`${file}: no section names a default: a heading has to hold 默认 or default, and the answer goes under it`)
            continue
        }
        let body = 0
        for (let i = start + 1; i < lines.length && !isHeading(lines[i]); i += 1) {
            if (lines[i].trim() !== '') body += 1
        }
        if (body === 0) {
            console.log(`${file}: the default section "${lines[start].replace(/^#{1,6}\s+/, '')}" has no text, so there is no default to start on`)
            continue
        }
        checked += 1
    }
    if (checked !== pages.length) {
        console.log(`${checked} of ${pages.length} pending page(s) give a default; a page without one is a question nobody can start on`)
        return 1
    }
    console.log(`every pending page names a default and gives it: ${checked} page(s) in ${QUESTIONS_DIR}`)
    return 0
}
// The rule the handover index states: a citation names a file and a symbol (`file#Symbol`), never a
// line, because a line moves with every edit. Inside docs/handover/ the line numbers are that day's
// reading and stay; outside it, a line number on a repository source is a citation that will rot.
// The match is deliberately narrow: a path that starts at one of the source roots and ends in a
// position. A `.cml` name with a position is excluded: it is a tool's output or an example program's
// line, and rewriting one would falsify the transcript it is quoted in. Two transcripts made this
// narrow -- a debugger session stepping through `test/cases/tools/debugger/fixtures/folded.cml` and printing its
// own positions, and a timing line from docs/cli.md that names a case file of
// `test/cases/perf` with a position after it. (Written this way because the citation check reads
// comments too: a comment that spells either out with a line number is itself an unpaid citation.)
const SOURCE_LINE_CITATION = /(?<![\w/])(?:src|include|modules|stdlib|scripts|test)\/[A-Za-z0-9_./-]*\.(?:cpp|h|hpp|cc|ts|mjs|js|toml|py):\d+/
function checkSourceLineCitations() {
    const files = markdownFiles('docs').filter((f) => !f.includes(`${path.sep}handover${path.sep}`))
    let found = 0
    for (const file of files) {
        const lines = fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n').split('\n')
        lines.forEach((line, index) => {
            const hit = SOURCE_LINE_CITATION.exec(line)
            if (hit === null) return
            found += 1
            console.log(`${path.relative(root, file).split(path.sep).join('/')}:${index + 1}: \`${hit[0]}\` cites a source by line; name the symbol instead (file#Symbol)`)
        })
    }
    if (found > 0) {
        console.log(`${found} source citation(s) outside docs/handover give a line number, which moves with every edit`)
        return 1
    }
    console.log(`no source citation outside docs/handover gives a line number: ${files.length} page(s) checked`)
    return 0
}
if (args.includes('--source-line-citations')) {
    process.exit(checkSourceLineCitations())
}
if (args.includes('--questions-defaults')) {
    process.exit(checkQuestionsHaveDefaults())
}
if (args.includes('--questions-index')) {
    if (args.includes('--write')) process.exit(writeQuestionsIndex())
    const indexCode = checkQuestionsIndex()
    const answeredCode = checkAnsweredPages()
    process.exit(indexCode !== 0 ? indexCode : answeredCode)
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
