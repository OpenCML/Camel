#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The readings hand-written pages cite as premises, measured again here.
//
// A page that says "1019 `case TypeCode::` branches" or "`jvp.cpp` is 319 lines" is load-bearing:
// another lane plans against it, quotes it in a question, or copies the segments it numbers.
// Nothing re-reads those numbers when the tree moves, so the page goes on describing a tree that
// is gone, and the next lane starts from a premise that was never true.
//
// Each reading below is one sentence on one page and one measurement here. The check fails when
// the two disagree, naming the page, the reading and both numbers; it never rewrites the page,
// because the person who moved the number is the one who can say in a commit why the reading
// moved. That is the same rule the exception tables carry: a recorded number moves in the commit
// that moves it, with the reason.
//
// The sentence is part of the contract. Every reading is matched by the canonical form it was
// given here (`复核 <date>：…`), so rewording one without updating this file fails the check --
// which is the point: a page that stops saying the number is a page nobody can re-measure.
//
// What this does not cover: the readings that need a build (a test count, a compile verdict, a
// timing) are not measured here -- a run states those, and a doc quoting a run states the commit
// it was taken at. This file reads sources and pages only, so it runs without a binary and
// without the build host.
//
// Usage: node test/tools/check-doc-readings.mjs [--verbose]

import { readdirSync, readFileSync, existsSync } from 'node:fs'
import { join } from 'node:path'

const VERBOSE = process.argv.includes('--verbose')

// The tree as it sits, not the index: a file that is not staged yet counts, because the page
// describes the tree a reader has.
const SOURCE_DIRS = ['src', 'include', 'modules']
const DOC_DIRS = ['docs']
const SOURCE = /\.(cpp|h)$/

function walk(dir, filter, out = []) {
    if (!existsSync(dir)) return out
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
        const path = join(dir, entry.name)
        if (entry.isDirectory()) walk(path, filter, out)
        else if (filter(path)) out.push(path)
    }
    return out
}

const sources = () => SOURCE_DIRS.flatMap((dir) => walk(dir, (path) => SOURCE.test(path)))
const documents = () => DOC_DIRS.flatMap((dir) => walk(dir, (path) => path.endsWith('.md')))

const occurrences = (files, pattern) =>
    files.reduce((total, file) => total + (readFileSync(file, 'utf8').match(pattern) || []).length, 0)

const filesMatching = (files, pattern) => files.filter((file) => pattern.test(readFileSync(file, 'utf8'))).length

const newlines = (file) => (readFileSync(file, 'utf8').match(/\n/g) || []).length

// A reading: the page, the canonical sentence (whose capture groups are the numbers it states),
// and the measurement. `where` says what is measured, so a failure names the tree side too.
const READINGS = [
    {
        id: 'typecode.branches',
        page: 'docs/handover/reviews/questions-2026-11-01-closures-as-values-block-the-transposed-backward-half.md',
        sentence: /复核 2026-11-01：现在是 \*\*(\d+) 处 `case TypeCode::`/,
        where: 'occurrences of `case TypeCode::` under src, include and modules',
        measure: () => [occurrences(sources(), /case TypeCode::/g)],
    },
    {
        id: 'typecode.files',
        page: 'docs/handover/reviews/questions-2026-11-01-closures-as-values-block-the-transposed-backward-half.md',
        sentence: /、(\d+) 个文件\*\* —— `eeb02d2d4`/,
        where: 'source files holding a `case TypeCode::`',
        measure: () => [filesMatching(sources(), /case TypeCode::/)],
    },
    {
        id: 'principle.5.10.code',
        page: 'docs/handover/reviews/questions-2026-11-01-closures-as-values-block-the-transposed-backward-half.md',
        sentence: /原则 5\.10 在代码里仍是 \*\*(\d+)\*\* 处/,
        where: 'mentions of 5.10 under src, include and modules',
        measure: () => [occurrences(sources(), /5\.10/g)],
    },
    {
        id: 'jvp.lines',
        page: 'docs/handover/m3-transpose-scaffold-notes.cn.md',
        sentence: /`modules\/transforms\/jvp\.cpp` 现在 \*\*(\d+) 行\*\*/,
        where: 'lines of modules/transforms/jvp.cpp',
        measure: () => [newlines('modules/transforms/jvp.cpp')],
    },
    {
        id: 'transpose.lines',
        page: 'docs/handover/m3-transpose-scaffold-notes.cn.md',
        sentence: /现在读作 `transpose\.cpp` \*\*(\d+) 行\*\*/,
        where: 'lines of modules/transforms/transpose.cpp',
        measure: () => [newlines('modules/transforms/transpose.cpp')],
    },
    {
        id: 'transpose-operator.lines',
        page: 'docs/handover/m3-transpose-scaffold-notes.cn.md',
        sentence: /、`transpose_operator\.cpp` \*\*(\d+) 行\*\*/,
        where: 'lines of modules/transforms/transpose_operator.cpp',
        measure: () => [newlines('modules/transforms/transpose_operator.cpp')],
    },
    {
        id: 'scaffold.segments',
        page: 'docs/handover/m3-transpose-scaffold-notes.cn.md',
        sentence: /14 段里 (\d+) 段照抄、(\d+) 段改写、(\d+) 段删/,
        where: 'the segment table of the page itself, by the 转置版 column',
        measure: () => {
            const rows = readFileSync('docs/handover/m3-transpose-scaffold-notes.cn.md', 'utf8')
                .split('\n')
                .filter((line) => /^\| \d+ \| \d/.test(line))
            const count = (word) => rows.filter((line) => line.includes(`**${word}**`) || line.includes(`**${word}`)).length
            return [count('照抄'), count('改写'), count('删')]
        },
    },
    {
        id: 'shape-words',
        page: 'docs/design-principles.cn.md',
        sentence: /形状词就是这(\S{1,2})个/,
        where: 'the shape cases of `match type` (every TypeCaseKind but Identity and Wildcard)',
        measure: () => {
            const kinds = new Set()
            for (const file of sources()) {
                for (const match of readFileSync(file, 'utf8').matchAll(/TypeCaseKind::([A-Za-z]+)/g)) kinds.add(match[1])
            }
            kinds.delete('Identity')
            kinds.delete('Wildcard')
            return [kinds.size]
        },
        spoken: (value) => (value === 9 ? '九' : String(value)),
    },
    {
        id: 'status-rows',
        page: 'docs/handover/batches-2026-10-29.cn.md',
        sentence: /状态表 (\d+) 行）\*\*：进行中 \*\*`(\d+)`\*\* \/ 已合 \*\*`(\d+)`\*\* \/ 待合 \*\*`(\d+)`\*\* \/ 未开始 \*\*`(\d+)`\*\* \/ 受阻 \*\*`(\d+)`\*\*/,
        where: 'the rows of the page\'s status tables and their status column',
        measure: () => {
            const rows = readFileSync('docs/handover/batches-2026-10-29.cn.md', 'utf8')
                .split('\n')
                .filter((line) => /^\|\s*[^|]+\|\s*[^|]+\|\s*[^|]+\|\s*[^|]+\|\s*(进行中|已合|待合|未开始|受阻)\s*\|/.test(line))
            const count = (status) => rows.filter((line) => line.split('|')[5].trim() === status).length
            return [rows.length, count('进行中'), count('已合'), count('待合'), count('未开始'), count('受阻')]
        },
    },
    {
        id: 'transforms-module.lines',
        page: 'docs/handover/reviews/questions-2026-11-01-closures-as-values-block-the-transposed-backward-half.md',
        sentence: /`split_operator\.cpp` \*\*(\d+)\*\*、`jvp_operator\.cpp` \*\*(\d+)\*\*、`transforms\.cml` \*\*(\d+)\*\*、`vjp\.cml` \*\*(\d+)\*\*，`split\.cpp` \*\*(\d+)\*\*、`jvp\.cpp` \*\*(\d+)\*\*/,
        where: 'lines of the operator halves and of the declarations the page counts',
        measure: () =>
            [
                'modules/transforms/split_operator.cpp',
                'modules/transforms/jvp_operator.cpp',
                'stdlib/signatures/transforms.cml',
                'stdlib/vjp.cml',
                'modules/transforms/split.cpp',
                'modules/transforms/jvp.cpp',
            ].map(newlines),
    },
]

const problems = []
let verified = 0

for (const reading of READINGS) {
    const text = readFileSync(reading.page, 'utf8')
    const match = text.match(reading.sentence)
    if (match === null) {
        problems.push(`${reading.id}: ${reading.page} no longer states the reading in the form this check reads (the sentence was reworded or removed); restore the sentence or update the pattern here (${reading.where})`)
        continue
    }
    const stated = match.slice(1).map((value) => (/^\d+$/.test(value) ? Number(value) : value))
    const measured = reading.measure()
    // A page may state a number in words (`这九个`): the reading is compared in the form the page
    // uses, so `spoken` maps the measurement onto it.
    const asWritten = reading.spoken ? measured.map(reading.spoken) : measured
    const agrees = stated.length === measured.length && stated.every((value, index) => String(value) === String(asWritten[index]))
    if (!agrees) {
        problems.push(`${reading.id}: ${reading.page} states ${JSON.stringify(stated)}, and ${reading.where} gives ${JSON.stringify(asWritten)}: update the page in the same commit and say why the reading moved`)
        continue
    }
    verified++
    console.log(`ok ${reading.id}: ${measured.join(' / ')} (${reading.page})`)
    if (VERBOSE) console.log(`   measured: ${reading.where}`)
}

if (problems.length > 0) {
    for (const problem of problems) console.error(`stale ${problem}`)
    console.error(`${problems.length} reading(s) are not the ones the tree has`)
    process.exit(1)
}

console.log(`the readings are the ones the pages state: ${verified} of ${READINGS.length}`)