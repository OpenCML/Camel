#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Writing freedom, measured as **bytes**: two writings of one program must have one canonical graph.
 *
 * Two groups, both compared with `std::jgir canonical=true`, byte for byte:
 *
 *   --group var       One gradient step written two ways -- `test/cases/compile/var/step_var.cml`
 *                     assigns to a `var`, `step_let.cml` is the same step as a chain of `let`
 *                     bindings -- and then the canonical output of the derivative of each
 *                     (`std::macro std::jgir canonical=true`). This is the queue's item 1,
 *                     "a unit is lowered to SSA" (design principles 3.5). Red today, and red only
 *                     because the `var` writing still builds a unit.
 *
 *   --group spelling  The case study's training program (`examples/case-study/train.cml`) and three
 *                     writings of it that change no meaning: a renamed value, two independent
 *                     declarations swapped, one independent computation moved past a print. None of
 *                     the three depends on the `var`-to-SSA work.
 *
 * A report that says only "not identical" is useless in a document of a thousand lines, so a
 * difference is reported as a place and as a shape:
 *
 *   - the first byte at which the two documents part, its 1-based offset, and the line and column
 *     that byte sits on, with two lines of context from each side;
 *   - the shape, when both documents are the canonical JSON GIR: how many graphs and nodes each
 *     side has, the first node that differs with the operator each side put there, and the
 *     operators one side has and the other does not.
 *
 * Usage: node writing-freedom.mjs --group var|spelling [--camel <path>]
 *
 * One line per phase per case, always, so that "identical" is a reading and not an absence:
 *   [graph] let identical bytes=9179 sha256=1a2b3c4d5e6f7081
 *   [graph] let differs first_difference_byte=4083 first_difference_offset=4084 line=132 column=9
 *   [grad] let failed exit=1: error[RuntimeDiag::MacroEvaluationFailed]: ...
 *
 * The exit code is 0 when every phase of every case is identical and 1 otherwise.
 */

import { spawnSync } from 'node:child_process'
import { readFileSync } from 'node:fs'
import crypto from 'node:crypto'
import path from 'node:path'
import url from 'node:url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))

/// The repository root: the first ancestor of this tool that holds the standard library. The runner
/// resolves a case against the plan that names it, so neither `process.cwd()` nor `HERE` can be
/// assumed to be the root.
function repositoryRoot(start) {
    let dir = path.resolve(start)
    for (;;) {
        try {
            readFileSync(path.join(dir, 'stdlib', 'std.cml'), 'utf8')
            return dir
        } catch {
            const parent = path.dirname(dir)
            if (parent === dir) throw new Error(`no stdlib/std.cml above ${start}`)
            dir = parent
        }
    }
}

const ROOT = repositoryRoot(HERE)

// The runner runs `tool` with the words in `args`, so a plan entry passes the script's own path as
// `${case}`; the first bare word is that path and says nothing this tool needs, because the cases
// are found from the repository root. Everything else has to be a `--name value` pair.
const words = process.argv.slice(2)
if (words.length > 0 && !words[0].startsWith('--')) words.shift()

const argv = new Map()
for (let i = 0; i < words.length; i += 1) {
    const word = words[i]
    if (!word.startsWith('--')) throw new Error(`unexpected argument '${word}'`)
    const value = words[i + 1]
    if (value === undefined || value.startsWith('--')) throw new Error(`${word} needs a value`)
    argv.set(word, value)
    i += 1
}

const CAMEL = argv.has('--camel')
    ? path.resolve(argv.get('--camel'))
    : path.join(
          process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest'),
          'bin',
          process.platform === 'win32' ? 'camel.exe' : 'camel',
      )

/// Every group names its cases in the order it compares them: the first one is the reference, and
/// each of the others is compared against it. `phases` are the pipelines a case is run in.
const GROUPS = {
    // One gradient step, two writings: this is the acceptance for "a unit is lowered to SSA".
    var: {
        phases: [
            ['graph', ['std::jgir', 'canonical=true']],
            ['grad', ['std::macro', 'std::jgir', 'canonical=true']],
        ],
        cases: [
            ['var', 'test/cases/compile/var/step_var.cml'],
            ['let', 'test/cases/compile/var/step_let.cml'],
        ],
    },
    // The training program of the case study, and three writings of it that change no meaning.
    spelling: {
        phases: [['graph', ['std::jgir', 'canonical=true']]],
        cases: [
            ['original', 'examples/case-study/train.cml'],
            ['renamed', 'examples/case-study/writings/renamed.cml'],
            ['swapped', 'examples/case-study/writings/swapped.cml'],
            ['moved', 'examples/case-study/writings/moved.cml'],
        ],
    },
}

const groupName = argv.get('--group')
if (!groupName) throw new Error('--group is required: one of ' + Object.keys(GROUPS).join(', '))
const group = GROUPS[groupName]
if (!group) throw new Error(`unknown group '${groupName}': one of ${Object.keys(GROUPS).join(', ')}`)
const cases = group.cases.map(([label, file]) => [label, path.resolve(ROOT, file)])

/// What a case is run in: the environment the runner gave us, with the installation root the binary
/// beside us needs.
const env = {
    ...process.env,
    CAMEL_HOME: process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest'),
}

function run(file, passes) {
    const result = spawnSync(CAMEL, [file, ...passes], {
        cwd: ROOT,
        env,
        encoding: 'buffer',
        maxBuffer: 256 * 1024 * 1024,
    })
    if (result.error) throw result.error
    return {
        status: result.status,
        stdout: result.stdout || Buffer.alloc(0),
        stderr: result.stderr || Buffer.alloc(0),
    }
}

function sha256(buffer) {
    return crypto.createHash('sha256').update(buffer).digest('hex')
}

/// The index of the first byte two buffers disagree on, or -1. A buffer that is a prefix of the
/// other differs at its own length: one document ended and the other went on.
function firstDifference(a, b) {
    const shorter = Math.min(a.length, b.length)
    for (let i = 0; i < shorter; i += 1) {
        if (a[i] !== b[i]) return i
    }
    return a.length === b.length ? -1 : shorter
}

/// The 1-based line and column a 0-based byte offset sits at. The documents are UTF-8 and the
/// canonical dump writes one byte per row byte for ASCII, but a payload may hold any bytes, so the
/// line is counted on the decoded prefix up to the offset.
function lineAndColumn(buffer, offset) {
    const before = buffer.subarray(0, offset).toString('utf8')
    const lines = before.split('\n')
    return { line: lines.length, column: lines[lines.length - 1].length + 1 }
}

function contextAt(buffer, line, radius) {
    const lines = buffer.toString('utf8').split('\n')
    const from = Math.max(1, line - radius)
    const to = Math.min(lines.length, line + radius)
    const out = []
    for (let i = from; i <= to; i += 1) {
        out.push({ number: i, text: lines[i - 1], match: i === line })
    }
    return out
}

function sortedDeep(value) {
    if (Array.isArray(value)) return value.map(sortedDeep)
    if (value && typeof value === 'object') {
        const out = {}
        for (const key of Object.keys(value).sort()) out[key] = sortedDeep(value[key])
        return out
    }
    return value
}

function canonicalJson(buffer) {
    try {
        return JSON.parse(buffer.toString('utf8'))
    } catch {
        return null
    }
}

/// What a node says, for a report: the operator it runs, or its kind when it runs none.
function nodeShape(node) {
    if (node && node.payload && typeof node.payload.op === 'string') return node.payload.op
    if (node && node.kind === 'DATA') return `DATA ${JSON.stringify(node.value)}`
    return String(node && node.kind)
}

function operatorCounts(document) {
    const counts = new Map()
    for (const graph of document.graphs || []) {
        for (const node of graph.nodes || []) {
            if (!node.payload || typeof node.payload.op !== 'string') continue
            counts.set(node.payload.op, (counts.get(node.payload.op) || 0) + 1)
        }
    }
    return counts
}

/// The shape of a difference between two canonical documents, as lines of a report. Empty when a
/// document is not the canonical JSON GIR -- the byte report above it is then the whole answer.
function shapeOfDifference(referenceLabel, referenceDocument, label, document) {
    if (referenceDocument === null || document === null) return []
    const lines = []
    const referenceGraphs = referenceDocument.graphs || []
    const graphs = document.graphs || []
    lines.push(
        `  shape: graphs ${referenceLabel}=${referenceGraphs.length} ${label}=${graphs.length}`,
    )
    for (let g = 0; g < Math.min(referenceGraphs.length, graphs.length); g += 1) {
        const referenceNodes = referenceGraphs[g].nodes || []
        const nodes = graphs[g].nodes || []
        const referenceById = new Map(referenceNodes.map((node) => [node.id, node]))
        const byId = new Map(nodes.map((node) => [node.id, node]))
        const ids = [...new Set([...referenceById.keys(), ...byId.keys()])]
        const differing = ids.filter((id) => {
            const a = referenceById.get(id)
            const b = byId.get(id)
            return JSON.stringify(sortedDeep(a)) !== JSON.stringify(sortedDeep(b))
        })
        if (differing.length === 0) continue
        lines.push(
            `  shape: graph ${g} nodes ${referenceLabel}=${referenceNodes.length} ${label}=${nodes.length}`,
        )
        for (const id of differing.slice(0, 4)) {
            lines.push(
                `  shape: graph ${g} node ${id} ${referenceLabel}=${nodeShape(referenceById.get(id))} ` +
                    `${label}=${nodeShape(byId.get(id))}`,
            )
        }
        if (differing.length > 4) {
            lines.push(`  shape: graph ${g} ${differing.length - 4} further node(s) differ`)
        }
        break
    }
    const referenceOps = operatorCounts(referenceDocument)
    const ops = operatorCounts(document)
    const onlyReference = [...referenceOps.keys()].filter((op) => !ops.has(op)).sort()
    const onlyOther = [...ops.keys()].filter((op) => !referenceOps.has(op)).sort()
    if (onlyReference.length > 0) {
        lines.push(
            `  shape: operators only in the ${referenceLabel} document: ` +
                onlyReference.map((op) => `${op} x${referenceOps.get(op)}`).join(', '),
        )
    }
    if (onlyOther.length > 0) {
        lines.push(
            `  shape: operators only in the ${label} document: ` +
                onlyOther.map((op) => `${op} x${ops.get(op)}`).join(', '),
        )
    }
    return lines
}

/// One phase of one case, compared against the reference case.
function report(phase, passes, referenceLabel, referenceRun, label, caseRun) {
    // A side that did not finish has no document to compare: say which side, why, and the first line
    // of the diagnostic it wrote. The write reports a diagnostic to standard output, so the reason is
    // read there first and on standard error second.
    for (const [side, run_] of [
        [referenceLabel, referenceRun],
        [label, caseRun],
    ]) {
        if (run_.status === 0) continue
        const first =
            (run_.stdout.toString('utf8').split('\n').find((l) => l.trim().length > 0) || '').trim() ||
            (run_.stderr.toString('utf8').split('\n').find((l) => l.trim().length > 0) || '').trim()
        console.log(`[${phase}] ${label} failed side=${side} exit=${run_.status}: ${first}`)
        return false
    }
    const offset = firstDifference(referenceRun.stdout, caseRun.stdout)
    if (offset < 0) {
        console.log(
            `[${phase}] ${label} identical bytes=${caseRun.stdout.length} ` +
                `sha256=${sha256(caseRun.stdout).slice(0, 16)}`,
        )
        return true
    }
    const at = lineAndColumn(referenceRun.stdout, offset)
    const atCase = lineAndColumn(caseRun.stdout, offset)
    console.log(
        `[${phase}] ${label} differs first_difference_byte=${offset} ` +
            `first_difference_offset=${offset + 1} line=${at.line} column=${at.column} ` +
            `${referenceLabel}_bytes=${referenceRun.stdout.length} ${label}_bytes=${caseRun.stdout.length}`,
    )
    const radius = 2
    for (const line of contextAt(referenceRun.stdout, at.line, radius)) {
        console.log(
            `  ${referenceLabel} ${String(line.number).padStart(4)} ${line.match ? '>' : '|'} ${line.text}`,
        )
    }
    for (const line of contextAt(caseRun.stdout, atCase.line, radius)) {
        console.log(`  ${label} ${String(line.number).padStart(4)} ${line.match ? '>' : '|'} ${line.text}`)
    }
    for (const line of shapeOfDifference(
        referenceLabel,
        canonicalJson(referenceRun.stdout),
        label,
        canonicalJson(caseRun.stdout),
    )) {
        console.log(line)
    }
    console.log(`  phase: ${phase} (${passes.join(' ')})`)
    return false
}

const [referenceLabel, referenceCase] = cases[0]
console.log(`writing-freedom: group=${groupName} reference=${referenceLabel}`)
for (const [label, file] of cases) {
    console.log(`  case ${label}: ${path.relative(ROOT, file)}`)
}

let ok = true
for (const [phase, passes] of group.phases) {
    const referenceRun = run(referenceCase, passes)
    for (const [label, file] of cases.slice(1)) {
        ok = report(phase, passes, referenceLabel, referenceRun, label, run(file, passes)) && ok
    }
}

// `process.exit` would drop what is still queued for a standard output that is a pipe, and the
// runner reads exactly that: the first run of this tool reported an empty stdout under the plan
// while printing its report on a terminal. The exit code is set and the process ends by itself.
process.exitCode = ok ? 0 : 1