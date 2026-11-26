// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Writes the index page that sits beside the report pages and answers "what is in this directory".
 *
 * The directory had no index. Every page there is found by listing it, which works for the person who
 * wrote the page and not for the person who needs one they have not seen. This generator reads the
 * directory and writes one row per page: the day the page's name carries, and the page's own title,
 * linked. Nothing is inferred from the page body beyond its first heading, and no page is dropped:
 * a page named `<day>-<topic>.cn.md` whose name carries no day or whose text opens with no `#` heading
 * is listed in the format section with the reason, and a page with an older name gets its own section,
 * so the sections together name every file in the directory exactly once.
 *
 * It is a generator and not a check: no gate, merge step or promotion step calls it. Two runs over
 * the same directory write the same bytes, which is how a run that changes nothing is recognised.
 *
 * Usage:
 *   node scripts/reports-index.mjs            write the index page
 *   node scripts/reports-index.mjs --check    write nothing; exit 1 if the page differs from what
 *                                             this run would write, 2 if the page is missing
 */

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const reportsDir = path.join(root, 'internal', 'reports')
const indexFile = path.join(reportsDir, 'README.md')
const index = 'README.md'
const dayPattern = /^(\d{4}-\d{2}-\d{2})-(.+)\.md$/

function pagesInDirectory() {
    return fs
        .readdirSync(reportsDir)
        .filter((name) => name.endsWith('.md') && name !== index)
        .sort()
}

// The title is the page's first `#` heading, and only that: a page that opens with a blockquote or a
// table of contents is not asked to say what it is in this file. A heading is looked for in the
// opening lines only, so that a page whose first heading sits in the middle of a long body reads as
// having none, rather than making the reader believe the middle of the page is its title.
function titleOf(name) {
    const lines = fs.readFileSync(path.join(reportsDir, name), 'utf8').split('\n').slice(0, 40)
    for (const line of lines) {
        const match = line.match(/^#\s+(.+?)\s*#*\s*$/)
        if (match !== null) return match[1].trim()
    }
    return null
}

// A `|` in a title ends a table cell early and a `[` opens a link; both are escaped so that a title
// with either still reads as its own text in the cell.
function cell(text) {
    return text.replace(/\\/g, '\\\\').replace(/\|/g, '\\|').replace(/\[/g, '\\[').replace(/\]/g, '\\]')
}

function render() {
    const pages = pagesInDirectory()
    // The pages whose name is `<day>-<topic>.cn.md` are the ones this directory's convention names;
    // they are the index. The pages with an older name are not dropped -- a directory listing is
    // still the only other way to find them -- so they get their own section, counted separately.
    const rows = []
    const malformed = []
    const legacy = []
    for (const name of pages) {
        const day = name.match(dayPattern)
        const title = titleOf(name)
        const named = name.endsWith('.cn.md')
        if (day !== null && title !== null && named) {
            rows.push({ name, day: day[1], title })
            continue
        }
        const why =
            day === null
                ? title === null
                    ? '文件名既没有日期，正文开头也不是 `#` 标题'
                    : '文件名里没有日期'
                : '正文里（前 40 行）没有 `#` 标题'
        if (named) malformed.push({ name, day: day === null ? null : day[1], why })
        else legacy.push({ name, day: day === null ? null : day[1], title })
    }
    const byDay = (a, b) => (a.day === null ? 1 : b.day === null ? -1 : a.day < b.day ? 1 : a.day > b.day ? -1 : a.name < b.name ? -1 : 1)
    // Newest first, then by name, so that the page reads like the directory's own chronology and a
    // rewrite of the same directory puts the same rows in the same places.
    rows.sort(byDay)
    malformed.sort((a, b) => (a.name < b.name ? -1 : 1))
    legacy.sort(byDay)

    const lines = [
        '# 报告索引',
        '',
        '> `internal/reports/` 里的页。**这一页由 `node scripts/reports-index.mjs` 从目录生成，不要手改**；',
        '> 页的正文才是记录，这里只回答"这个目录里有什么、哪一天写的、叫什么"。',
        '>',
        '> 加了一页之后跑一次同一条命令；`--check` 只比较、不写，用来发现有人手改过这一页。',
        '',
        `## 索引：${rows.length} 页（\`<日期>-<主题>.cn.md\`）`,
        '',
    ]
    if (rows.length === 0) {
        lines.push('（目录里没有带日期与标题的页。）')
    } else {
        lines.push('| 日期 | 主题 |', '| --- | --- |')
        for (const row of rows) {
            const stem = row.name.replace(/\.md$/, '')
            lines.push(`| ${row.day} | [\`${stem}\`](${row.name}) — ${cell(row.title)} |`)
        }
    }
    lines.push('', `## 格式不合：${malformed.length} 页`, '')
    if (malformed.length === 0) {
        lines.push('（没有：每个页的文件名都带日期，正文都以 `#` 标题开头。）')
    } else {
        lines.push(
            '> 文件名里没有日期、或正文开头没有 `#` 标题的 `.cn.md` 页。它们照样列在这里，没有静默跳过；',
            '> 上面的索引与这一节合起来，恰好各点一次目录里每一个 `.cn.md` 页。',
            '',
        )
        for (const row of malformed) {
            const day = row.day === null ? '' : `日期 ${row.day}，`
            lines.push(`- \`${row.name}\` — ${day}${row.why}`)
        }
    }
    lines.push('', `## 其余页：${legacy.length} 页（文件名不是 \`<日期>-<主题>.cn.md\`）`, '')
    if (legacy.length === 0) {
        lines.push('（没有。）')
    } else {
        lines.push(
            '> 目录里的旧命名页。它们不在上面的两节里，但也不该只靠 `ls` 找，所以一并列出、不并入索引：',
            '> 索引的条数是 `.cn.md` 页数，这一节单独计数。',
            '',
        )
        for (const row of legacy) {
            const day = row.day === null ? '' : `${row.day} `
            const title = row.title === null ? '' : ` — ${cell(row.title)}`
            lines.push(`- ${day}\`${row.name}\`${title}`)
        }
    }
    lines.push('')
    const named = rows.length + malformed.length
    return { text: lines.join('\n'), pages: pages.length, named, rows: rows.length, malformed: malformed.length, legacy: legacy.length }
}

const { text, pages, named, rows, malformed, legacy } = render()
const reading =
    `reports index: ${pages} page(s) = ${named} \`.cn.md\` (${rows} in the index, ${malformed} in the format section)` +
    ` + ${legacy} page(s) with other names, one row each`

if (process.argv.includes('--check')) {
    if (!fs.existsSync(indexFile)) {
        console.log(`internal/reports/${index}: no such file`)
        process.exit(2)
    }
    const actual = fs.readFileSync(indexFile, 'utf8').replace(/\r\n/g, '\n')
    if (actual !== text) {
        console.log('internal/reports/README.md: the page is not what the generator writes')
        process.exit(1)
    }
    console.log(reading + ', as the page says')
    process.exit(0)
}

fs.writeFileSync(indexFile, text, 'utf8')
console.log('wrote internal/reports/README.md: ' + reading)