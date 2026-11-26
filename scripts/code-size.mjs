// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The size of each part of src/, in code lines (comments and blank lines counted apart), beside the
// range a healthy implementation of that part's duties would take (docs/design/architecture,
// "规模参照"). The ranges are a reference for slimming, not a target: a part more than 30% above
// its upper bound is a reason to look at its abstractions, and nothing here fails a build.
//
//   node scripts/code-size.mjs            the table
//   node scripts/code-size.mjs --json     the same as JSON

import fs from 'node:fs'
import path from 'node:path'
import { execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

// Each part, the path prefixes it owns (the first match wins, so the narrower prefixes come first),
// and its healthy range in thousands of code lines. `null` marks a part kept outside the core: the
// frozen C++ references, the tools and the test passes.
const PARTS = [
    { name: 'frozen references (C++ autodiff engine, C++ optimizer passes)', prefixes: ['src/stdlib/autodiff/', 'src/stdlib/std/cxx/', 'src/compile/src/gir/rule_templates.cpp'], range: null },
    { name: 'test passes inside src', prefixes: ['src/stdlib/test/', 'src/stdlib/std/selftest/'], range: [0, 0] },
    { name: 'tools', prefixes: ['src/tools/'], range: null },
    { name: 'parse', prefixes: ['src/parse/'], range: [8, 12] },
    { name: 'compile (syntax tree to graph, semantics)', prefixes: ['src/compile/'], range: [15, 20] },
    { name: 'kernel (types, graph store and commit, operators, facts, runtime, modules)', prefixes: ['src/kernel/'], range: [30, 40] },
    { name: 'backends (nvm, fvm, parallel, C++ codegen, builtin operators, printers)', prefixes: ['src/stdlib/std/'], range: [30, 35] },
    { name: 'transform skeleton (forward, transpose, batching)', prefixes: ['src/stdlib/transforms/'], range: [6, 8] },
    { name: 'libraries (tensor, tiles, ONNX, rewrite, graph editing, small libraries)', prefixes: ['src/stdlib/'], range: [30, 35] },
    { name: 'other', prefixes: ['src/'], range: null },
]
const CORE = new Set(PARTS.filter((part) => part.range && part.range[1] > 0).map((part) => part.name))

function count(text) {
    let code = 0
    let comment = 0
    let blank = 0
    let inBlock = false
    for (const raw of text.split('\n')) {
        const line = raw.trim()
        if (inBlock) {
            comment++
            if (line.includes('*/')) inBlock = false
            continue
        }
        if (!line) blank++
        else if (line.startsWith('//')) comment++
        else if (line.startsWith('/*')) {
            comment++
            if (!line.includes('*/')) inBlock = true
        } else code++
    }
    return { code, comment, blank }
}

const files = execFileSync('git', ['ls-files', 'src'], { cwd: root, encoding: 'utf8', maxBuffer: 64 << 20 })
    .split('\n')
    .filter((file) => /\.(cpp|cc|h|hpp|inc|def)$/.test(file))
const totals = new Map(PARTS.map((part) => [part.name, { code: 0, comment: 0, blank: 0, files: 0 }]))
for (const file of files) {
    const part = PARTS.find((candidate) => candidate.prefixes.some((prefix) => file === prefix || file.startsWith(prefix)))
    const counted = count(fs.readFileSync(path.join(root, file), 'utf8'))
    const row = totals.get(part.name)
    row.code += counted.code
    row.comment += counted.comment
    row.blank += counted.blank
    row.files += 1
}

const rows = PARTS.map((part) => {
    const row = totals.get(part.name)
    const over = part.range ? row.code > part.range[1] * 1000 * 1.3 : false
    return { part: part.name, ...row, range: part.range, over }
})
const core = rows.filter((row) => CORE.has(row.part)).reduce((sum, row) => sum + row.code, 0)

if (process.argv.includes('--json')) {
    process.stdout.write(`${JSON.stringify({ rows, core, coreRange: [120, 150] }, null, 2)}\n`)
} else {
    const k = (n) => `${(n / 1000).toFixed(1)}k`
    for (const row of rows) {
        const range = row.range ? `${row.range[0]}-${row.range[1]}k` : '-'
        process.stdout.write(`${k(row.code).padStart(7)} code ${k(row.comment).padStart(6)} comment  range ${range.padEnd(7)}${row.over ? '  OVER' : '      '}  ${row.part}\n`)
    }
    process.stdout.write(`core ${k(core)} code lines (healthy 120-150k)\n`)
}
