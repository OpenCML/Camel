#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The corpus sweep of `std::cml`: how much of `test/cases/**` it prints, why it refuses the rest,
// and how many of the printed programs come back as the same graph.
//
// The three numbers are deliberately three numbers (`docs/design/graph-store/plan-roundtrip.cn.md`
// §0): "printed" is not "round trips", and "the printed source compiles" is not "the graph is the
// same". Every program falls in exactly one bucket:
//
//   printed   -- `std::cml` exited 0 and wrote source;
//   refused   -- it exited non-zero with `CmlUnsupportedConstruct` / `CmlBigConstant`, grouped by
//                the reason the diagnostic gives (names and numbers masked, so 40 programs that
//                name 40 different operators are one family);
//   frontend  -- the program does not compile at all, so the printer was never reached: those are
//                the diagnostic corpus, not a printer gap.
//
// For every printed program the printed source is compiled on its own and both sides are dumped as
// canonical v2 documents (`std::jgir canonical=true`) and compared by `test/tools/cml_roundtrip.py`,
// which is the authority: `equivalent`, `not_equivalent`, or `unfaithful` when the printed source
// cannot be dumped at all (so nothing was compared).
//
// Nothing here is pinned: this tool reports a reading. A plan entry that pins a number would have
// to be edited every time the printer moves, and the page above is where the numbers are argued.
//
// Usage: node test/tools/cml-sweep.mjs [--limit N] [--jobs N] [--only <substring>] [--quiet]

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const home = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
const camel = path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const CASES = path.join(ROOT, 'test', 'cases')
const CHECKER = path.join(ROOT, 'test', 'tools', 'cml_roundtrip.py')
const SCRATCH = fs.mkdtempSync(path.join(os.tmpdir(), 'cml-sweep-'))
const PYTHON = process.env.CAMEL_PYTHON || 'python3'

const argv = process.argv.slice(2)
function option(name, fallback) {
    const at = argv.indexOf(name)
    return at === -1 ? fallback : argv[at + 1]
}
const LIMIT = Number(option('--limit', '0'))
const JOBS = Number(option('--jobs', '8'))
const ONLY = option('--only', '')
const QUIET = argv.includes('--quiet')

function walk(dir, out = []) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name)
        if (entry.isDirectory()) walk(full, out)
        else if (entry.name.endsWith('.cml')) out.push(full)
    }
    return out
}

function run(executable, args, timeout = 60000) {
    return new Promise((resolve) => {
        const child = spawn(executable, args, { cwd: ROOT, env: { ...process.env, CAMEL_HOME: home } })
        let out = ''
        let err = ''
        let settled = false
        const finish = (status, timedOut) => {
            if (settled) return
            settled = true
            resolve({ status, out, err, timedOut })
        }
        const timer = setTimeout(() => {
            try {
                child.kill('SIGKILL')
            } catch {
                // already gone
            }
            finish(null, true)
        }, timeout)
        child.stdout.on('data', (data) => {
            if (out.length < 8 * 1024 * 1024) out += data
        })
        child.stderr.on('data', (data) => {
            if (err.length < 8 * 1024 * 1024) err += data
        })
        child.on('error', () => {
            clearTimeout(timer)
            finish(null, false)
        })
        child.on('close', (code) => {
            clearTimeout(timer)
            finish(code, false)
        })
    })
}

async function pool(items, width, worker) {
    const results = new Array(items.length)
    let next = 0
    const lanes = Array.from({ length: Math.max(1, Math.min(width, items.length)) }, async () => {
        for (;;) {
            const index = next++
            if (index >= items.length) return
            results[index] = await worker(items[index], index)
        }
    })
    await Promise.all(lanes)
    return results
}

// The reason a program was refused, with the names and numbers masked: `std::cml cannot print
// main: the operator ':io/println'` and the same sentence about `:op/add_d` are one family, and so
// are `128 bytes, over max_literal_bytes (64)` and its siblings.
function reasonOf(text) {
    const match = text.match(/error\[[^\]]*(CmlUnsupportedConstruct|CmlBigConstant)\]:\s*(.+)/)
    if (match === null) return 'unclassified'
    return match[2]
        .replace(/std::cml cannot print [^']*?: /, 'std::cml cannot print <graph>: ')
        .replace(/'[^']*'/g, "'<name>'")
        .replace(/`[^`]*`/g, '`<name>`')
        .replace(/\b\d+\b/g, '<n>')
        .trim()
}

const files = walk(CASES)
    .filter((file) => ONLY === '' || file.includes(ONLY))
    .sort()
const selected = LIMIT > 0 ? files.slice(0, LIMIT) : files

const tally = { printed: 0, refused: 0, frontend: 0, timeout: 0 }
const reasons = new Map()
const printed = []

await pool(selected, JOBS, async (file) => {
    const result = await run(camel, [file, 'std::cml'])
    const text = `${result.out}${result.err}`
    const printedSource = result.out.trim()
    if (result.timedOut) {
        tally.timeout += 1
        return
    }
    if (result.status === 0 && printedSource !== '') {
        tally.printed += 1
        printed.push({ file, source: result.out })
        return
    }
    if (/CmlUnsupportedConstruct|CmlBigConstant/.test(text)) {
        tally.refused += 1
        const reason = reasonOf(text)
        reasons.set(reason, (reasons.get(reason) ?? 0) + 1)
        return
    }
    tally.frontend += 1
})

const roundtrip = { checked: 0, equivalent: 0, not_equivalent: 0, over_budget: 0, unfaithful: 0 }
const bad = []
const printedDir = path.join(SCRATCH, 'printed')
fs.mkdirSync(printedDir, { recursive: true })

await pool(printed, JOBS, async (entry, index) => {
    const left = path.join(SCRATCH, `left-${index}.json`)
    const right = path.join(SCRATCH, `right-${index}.json`)
    const copy = path.join(printedDir, `printed-${index}.cml`)
    fs.writeFileSync(copy, entry.source)
    const before = await run(camel, [entry.file, 'std::jgir', 'canonical=true'])
    const after = await run(camel, [copy, 'std::jgir', 'canonical=true'])
    // A canonical document is one JSON object and nothing else; anything else is not a document.
    if (before.status !== 0 || after.status !== 0) {
        roundtrip.unfaithful += 1
        bad.push({ file: entry.file, why: 'the printed source does not produce a canonical document' })
        return
    }
    fs.writeFileSync(left, before.out)
    fs.writeFileSync(right, after.out)
    const verdict = await run(PYTHON, [CHECKER, left, right], 120000)
    roundtrip.checked += 1
    if (verdict.status === 0) {
        roundtrip.equivalent += 1
        return
    }
    // The checker's exit code is part of the reading: 1 is "the documents differ", 2 is "the
    // budget ran out", and a check that ran out of budget is neither equal nor different
    // (`cml_roundtrip.py#main`). Reading 2 as "different" would inflate the gap; reading it as
    // "equal" is what the checker refuses to say.
    const first = (verdict.out || verdict.err).split('\n').find((line) => line.trim() !== '') ?? ''
    if (verdict.status === 2) {
        roundtrip.over_budget += 1
        bad.push({ file: entry.file, why: `over budget (exit 2): ${first.trim()}` })
        return
    }
    roundtrip.not_equivalent += 1
    bad.push({ file: entry.file, why: `exit ${verdict.status}: ${first.trim()}` })
})

const relative = (file) => path.relative(ROOT, file).split(path.sep).join('/')
console.log(
    `SWEEP files=${selected.length} printed=${tally.printed} refused=${tally.refused} ` +
        `frontend=${tally.frontend} timeout=${tally.timeout}`,
)
const sorted = [...reasons.entries()].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]))
console.log(`REFUSALS families=${sorted.length}`)
for (const [reason, count] of sorted) console.log(`REFUSAL ${count} ${reason}`)
console.log(
    `ROUNDTRIP checked=${roundtrip.checked} equivalent=${roundtrip.equivalent} ` +
        `not_equivalent=${roundtrip.not_equivalent} over_budget=${roundtrip.over_budget} ` +
        `unfaithful=${roundtrip.unfaithful}`,
)
if (!QUIET) {
    for (const entry of bad) console.log(`DIFFERENT ${relative(entry.file)} ${entry.why}`)
}
// Which programs printed, one path each: a printer change is argued by the set that moved, not
// only by how many moved (one count can hide a program gained and another lost).
if (argv.includes('--printed')) {
    for (const entry of printed) console.log(`PRINTED ${relative(entry.file)}`)
}
if (fs.existsSync(SCRATCH)) fs.rmSync(SCRATCH, { recursive: true, force: true })
process.exit(0)