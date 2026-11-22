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
 *   --group loop      The same accumulation written two ways: a `var` written inside a loop body
 *                     (`loop_var.cml`) and the loop's own carried value (`loop_let.cml`). Red today
 *                     and red only because a write inside a loop body keeps its unit.
 *
 *   --group arm       The same choice written two ways: a `var` written inside an arm of an `if`
 *                     (`arm_var.cml`) and the branch's own value (`arm_let.cml`). It was red only
 *                     because an arm's write kept its unit; it is green since the branch carries the
 *                     write as its own value and the unit nothing reads is dropped.
 *
 * The `loop` and `arm` groups are the two halves the `var` group cannot see: on a straight line the
 * whole variable folds away, so that group is green whether or not a unit is lowered; a write inside
 * a loop or an arm kept its `:var/get`, so those two pairs still separate the two writings. The arm
 * pair is now one writing, and the loop pair is the half still to be lowered.
 *
 *   --group chainpure        `sync` blocks and what the chain carries (design principles 3.4). One
 *                            program shape, two writings of it (a call of a pure function that
 *                            cannot fault, then a print; and the two statements swapped). The call
 *                            has no order to keep, so the two documents must be one.
 *   --group chainpure_edges  The same two writings, read for the other half of that claim: the call
 *                            and the statement beside it are joined by no edge of any kind, while
 *                            the call stays in the graph and its value is still read. The byte
 *                            comparison is repeated here so that this group's one reading is
 *                            "apart, and still one document", not "apart" alone.
 *   --group chainpure_effect The same shape with a callee that prints: an effect, so the call keeps
 *                            its place on the chain and the two writings must part.
 *   --group chainpure_fault  The same shape with a callee that divides: pure, and able to fault, so
 *                            it is not "freely arranged" either and the two writings must part.
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
 * A group says what its two writings must be. `expect = 'identical'`, the default, is the reading a
 * group takes when the two writings change no meaning: one canonical document, byte for byte.
 * `expect = 'same-graph'` is the same claim about a pair whose two sides **cannot** share bytes,
 * because the dump numbers its nodes in the order they are written: swapping two statements renumbers
 * them, so the documents part even when the graph is one. The reading there is the graph itself --
 * the same nodes by what they say, and the same edges between them under that correspondence -- and a
 * byte difference is accepted only when that reading holds; when the byte comparison does match it is
 * still reported as `identical`, because the stronger reading was met. A group with
 * `expect = 'differs'` is the counterexample -- a writing whose two sides order something observable
 * -- and its reading is the opposite one: the same two statements swapped must part. Without one of
 * each, "identical" proves nothing: an implementation that put nothing on any chain would pass every
 * `identical` case.
 *
 * Usage: node writing-freedom.mjs --group var|spelling|chainpure|... [--camel <path>]
 *
 * One line per phase per case, always, so that "identical" is a reading and not an absence:
 *   [graph] let identical bytes=9179 sha256=1a2b3c4d5e6f7081
 *   [graph] let differs first_difference_byte=4083 first_difference_offset=4084 line=132 column=9
 *   [grad] let failed exit=1: error[RuntimeDiag::MacroEvaluationFailed]: ...
 *
 * A group that says `disjoint` reads a second line per case and per graph: no edge of any kind
 * joins the two nodes the selectors name, and the node the first selector names still has its value
 * read (so "no edge" is not reached by dropping the call). Either side missing is a failure too.
 *   [edges] first apart FUNC n1 <-> :io/println n3 graph=g1 edges=0 reads_of_the_call=1
 *   [edges] first joined FUNC n1 <-> :io/println n3 graph=g1 by control, sync
 *
 * The exit code is 0 when every phase of every case reads what its group says it must -- identical
 * where the writing changes no meaning, parted where it does -- and 1 otherwise.
 */

import { spawnSync } from 'node:child_process'
import { readFileSync } from 'node:fs'
import crypto from 'node:crypto'
import path from 'node:path'
import url from 'node:url'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))

/// The repository root: the first ancestor of this tool that holds the standard library. The runner
/// resolves a case against the plan that names it, so neither `process.cwd()` nor `HERE` can be
/// assumed to be the root. The library is `stdlib/std.cml` before the layout move and
/// `lib/std/std.cml` after it, and this tool runs against both trees, so either marker answers.
const ROOT_MARKERS = [
    ['stdlib', 'std.cml'],
    ['lib', 'std', 'std.cml']
]
const holdsLibrary = (dir) =>
    ROOT_MARKERS.some((parts) => {
        try {
            readFileSync(path.join(dir, ...parts), 'utf8')
            return true
        } catch {
            return false
        }
    })
function repositoryRoot(start) {
    let dir = path.resolve(start)
    for (;;) {
        if (holdsLibrary(dir)) return dir
        const parent = path.dirname(dir)
        if (parent === dir) throw new Error(`no stdlib/std.cml or lib/std/std.cml above ${start}`)
        dir = parent
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
/// each of the others is compared against it. `phases` are the pipelines a case is run in, and
/// `expect` is `identical` (the default: the other writings change no meaning, so the documents are
/// the same bytes) or `differs` (the counterexample: this group's two writings order something
/// observable, so the documents must part).
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
    // The same accumulation written two ways: a `var` written inside a loop body, and the loop's
    // own carried value. The second writing has no unit at all, so this pair says whether a loop
    // assignment became the loop's state (the third of the three places a unit is lowered).
    loop: {
        phases: [['graph', ['std::jgir', 'canonical=true']]],
        cases: [
            ['var', 'test/cases/compile/var/loop_var.cml'],
            ['let', 'test/cases/compile/var/loop_let.cml'],
        ],
    },
    // The same choice written two ways: a `var` written inside an arm of an `if`, and the branch's
    // own value. Says whether a branch assignment became the branch's result (the second place).
    arm: {
        phases: [['graph', ['std::jgir', 'canonical=true']]],
        cases: [
            ['var', 'test/cases/compile/var/arm_var.cml'],
            ['let', 'test/cases/compile/var/arm_let.cml'],
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
    // What a `sync` block's chain carries (design principles 3.4). The three groups are one program
    // shape with three callees, so the reading separates what the order of two statements depends
    // on: the pure and infallible call is arranged freely and the two writings are one document;
    // the call that prints and the call that divides are not, and the two writings part.
    chainpure: {
        expect: 'identical',
        phases: [['graph', ['std::jgir', 'canonical=true']]],
        cases: [
            ['first', 'test/cases/compile/chain/pure_call_first.cml'],
            ['second', 'test/cases/compile/chain/pure_call_second.cml'],
        ],
    },
    // The same two writings, read for what "the chain does not carry the call" means in the
    // document rather than in the bytes: the call and the statement beside it are joined by no edge
    // of any kind -- the block's chain is what used to join them -- while the call stays in the
    // graph and its value is still read, so "apart" is not reached by dropping the call. `op`
    // matches the operator's name with or without the version the dump appends (`:io/println@1.0`).
    chainpure_edges: {
        expect: 'identical',
        phases: [['graph', ['std::jgir', 'canonical=true']]],
        cases: [
            ['first', 'test/cases/compile/chain/pure_call_first.cml'],
            ['second', 'test/cases/compile/chain/pure_call_second.cml'],
        ],
        disjoint: [{ from: { kind: 'FUNC' }, to: { op: ':io/println' } }],
    },
    chainpure_effect: {
        expect: 'differs',
        phases: [['graph', ['std::jgir', 'canonical=true']]],
        cases: [
            ['first', 'test/cases/compile/chain/effect_call_first.cml'],
            ['second', 'test/cases/compile/chain/effect_call_second.cml'],
        ],
    },
    chainpure_fault: {
        expect: 'differs',
        phases: [['graph', ['std::jgir', 'canonical=true']]],
        cases: [
            ['first', 'test/cases/compile/chain/fault_call_first.cml'],
            ['second', 'test/cases/compile/chain/fault_call_second.cml'],
        ],
    },
    // The region rule of the author's ruling of 2026-11-17 (design principles 3.1): a region's exit
    // is one `GATE` whose inputs are the value the region hands out and the nodes in it that write
    // outside the program, and **no edge joins the writes to one another** ("这些写之间不加边"). Two
    // writes to two different resources, written in the two possible orders, must therefore be one
    // graph: the order of two writes on two resources is not an order the program can observe, and a
    // graph that said it was would be the reading this rule replaced.
    //
    // `same-graph` and not `identical`, and the reason is in the dump and not in the front end: the
    // canonical document numbers its nodes in the order they are written, so swapping the two
    // statements renumbers two of them and no two dumps of two writings can share bytes while they
    // are also the *same* document. What the reading keeps is what the ruling is about: the same
    // nodes by kind, operator and value, and the same edges between them (there is no edge between
    // the two writes in either), under the correspondence that survives renumbering.
    region_write: {
        expect: 'same-graph',
        phases: [['graph', ['std::jgir', 'canonical=true']]],
        cases: [
            ['print_first', 'test/cases/compile/region_roots/write_order_print_first.cml'],
            ['seed_first', 'test/cases/compile/region_roots/write_order_seed_first.cml'],
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

/// What a node says, with nothing about *where* it stands: its kind, its payload, its value. `id` is
/// the node's position in the dump and `type` is an index into the document's type table, and both
/// follow the order the nodes are written in, so neither can be part of a reading that is not about
/// that order. Everything else is kept, so two nodes with different kinds, operators or values do not
/// read as the same node.
function nodeContentKey(node) {
    const content = {}
    for (const key of Object.keys(node).sort()) {
        if (key === 'id' || key === 'type') continue
        content[key] = sortedDeep(node[key])
    }
    return JSON.stringify(content)
}

/// A graph with no node numbers in it: the nodes by what they say (sorted, so the order the dump
/// wrote them in is not read), and every edge as the pair of node contents it joins plus its kind.
/// Two documents spell the same graph when these agree, whatever the two dumps numbered the nodes.
function renumberedShape(document) {
    return (document.graphs || []).map((graph) => {
        const nodes = graph.nodes || []
        const byId = new Map(nodes.map((node) => [node.id, nodeContentKey(node)]))
        const edges = edgesOf(graph)
            .map((edge) => `${byId.get(edge.source)} --${edge.kind}--> ${byId.get(edge.target)}`)
            .sort()
        return { nodes: nodes.map(nodeContentKey).sort(), edges }
    })
}

/// Whether two canonical documents are one graph up to the numbers the dump gave its nodes, and the
/// one line to say so. Only the graphs are read: the rest of the document is the dump's own header.
function sameGraphReading(referenceDocument, document) {
    if (referenceDocument === null || document === null) return { same: false, line: '' }
    const reference = renumberedShape(referenceDocument)
    const other = renumberedShape(document)
    const same = JSON.stringify(reference) === JSON.stringify(other)
    const nodes = reference.reduce((sum, graph) => sum + graph.nodes.length, 0)
    const edges = reference.reduce((sum, graph) => sum + graph.edges.length, 0)
    return {
        same,
        line: `nodes=${nodes} edges=${edges} graphs=${reference.length}`,
    }
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

/// One phase of one case, compared against the reference case: `'identical'` when the two documents
/// are the same bytes, `'same-graph'` when they part only in the numbers the dump gave its nodes (and
/// the group reads the graph, `expect = 'same-graph'`), `'differs'` when they part for a reason that
/// survives that reading, and `null` when a side did not finish (nothing was compared, so no reading
/// of the group's expectation is possible).
function report(phase, passes, referenceLabel, referenceRun, label, caseRun, expectation = 'identical') {
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
        return null
    }
    const offset = firstDifference(referenceRun.stdout, caseRun.stdout)
    if (offset < 0) {
        console.log(
            `[${phase}] ${label} identical bytes=${caseRun.stdout.length} ` +
                `sha256=${sha256(caseRun.stdout).slice(0, 16)}`,
        )
        return 'identical'
    }
    // The bytes part. For a group that reads the graph, the documents may still be one graph: a dump
    // that numbers its nodes in the order they are written numbers two swapped statements differently,
    // and that is not a difference in the graph. The reading is taken before the difference is
    // reported, because a difference that does not survive it is not a difference this group has an
    // opinion about.
    if (expectation === 'same-graph') {
        const reading = sameGraphReading(canonicalJson(referenceRun.stdout), canonicalJson(caseRun.stdout))
        if (reading.same) {
            console.log(
                `[${phase}] ${label} same_graph first_difference_byte=${offset} ${reading.line} ` +
                    `(the two dumps number their nodes differently; the graph is one)`,
            )
            return 'same-graph'
        }
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
    return 'differs'
}

/// Whether a canonical node is the one a selector names: `kind` is the node's kind, `op` its
/// operator (the dump writes the version beside it, as `:io/println@1.0`).
function matchesSelector(node, selector) {
    if (selector.kind !== undefined && node.kind !== selector.kind) return false
    if (selector.op !== undefined) {
        const op = node.payload && node.payload.op
        if (typeof op !== 'string') return false
        if (op !== selector.op && !op.startsWith(`${selector.op}@`)) return false
    }
    return true
}

/// A graph's edges, whichever way the dump writes them: the canonical document keeps every edge as
/// one entry in the input list of the node it arrives at, and a plain `std::jgir` document may carry
/// a list of its own. A reading that silently saw no edges would call every pair "apart".
function edgesOf(graph) {
    if (Array.isArray(graph.edges)) return graph.edges
    const out = []
    for (const node of graph.nodes || []) {
        for (const input of node.inputs || []) {
            out.push({ source: input.source, target: node.id, kind: input.kind })
        }
    }
    return out
}

/// A group's `disjoint` readings, on one case's canonical document: for every graph holding a node
/// the first selector names and one the second names, no edge of any kind may join them, and the
/// first one's value must still be read. A reading line is written either way.
function checkDisjoint(phase, label, document) {
    if (document === null) {
        console.log(`[edges] ${label} no document to read`)
        return false
    }
    let ok = true
    for (const pair of group.disjoint) {
        let read = 0
        for (const graph of document.graphs || []) {
            const from = (graph.nodes || []).filter((node) => matchesSelector(node, pair.from))
            const to = (graph.nodes || []).filter((node) => matchesSelector(node, pair.to))
            if (from.length === 0 || to.length === 0) continue
            const edges = edgesOf(graph)
            for (const a of from) {
                for (const b of to) {
                    read += 1
                    const shape = `${pair.from.kind} ${a.id} <-> ${pair.to.op} ${b.id} graph=${graph.id}`
                    const joined = edges.filter(
                        (edge) =>
                            (edge.source === a.id && edge.target === b.id) ||
                            (edge.source === b.id && edge.target === a.id),
                    )
                    if (joined.length > 0) {
                        ok = false
                        const kinds = [...new Set(joined.map((edge) => edge.kind))].sort()
                        console.log(`[edges] ${label} joined ${shape} by ${kinds.join(', ')}`)
                        continue
                    }
                    const reads = edges.filter((edge) => edge.source === a.id && edge.kind === 'normal')
                    if (reads.length === 0) {
                        ok = false
                        console.log(
                            `[edges] ${label} apart ${shape} edges=0 reads_of_the_call=0: the call ` +
                                `is in the graph and nothing reads it`,
                        )
                        continue
                    }
                    console.log(
                        `[edges] ${label} apart ${shape} edges=0 reads_of_the_call=${reads.length}`,
                    )
                }
            }
        }
        if (read === 0) {
            ok = false
            console.log(
                `[edges] ${label} nothing to read: no graph holds both a ${pair.from.kind} and a ` +
                    `${pair.to.op} node`,
            )
        }
    }
    return ok
}

const [referenceLabel, referenceCase] = cases[0]
// What this group's readings must be, and the reading it is checked against: `identical` for a
// writing that changes no meaning, `same-graph` for one whose two sides cannot share bytes because
// the dump numbers its nodes in the order they are written, `differs` for the counterexample that
// orders something observable. A group says one and never two, so "the two documents are the same"
// is a claim about this pair and not a claim about every pair.
const expectation = group.expect || 'identical'
const expectVerdicts =
    expectation === 'differs' ? ['differs'] : expectation === 'same-graph' ? ['identical', 'same-graph'] : ['identical']
console.log(`writing-freedom: group=${groupName} reference=${referenceLabel} expect=${expectation}`)
for (const [label, file] of cases) {
    console.log(`  case ${label}: ${path.relative(ROOT, file)}`)
}

let ok = true
for (const [phase, passes] of group.phases) {
    const runs = cases.map(([label, file]) => [label, run(file, passes)])
    const referenceRun = runs[0][1]
    for (const [label, caseRun] of runs.slice(1)) {
        const verdict = report(phase, passes, referenceLabel, referenceRun, label, caseRun, expectation)
        if (verdict === null) {
            // A side did not finish: the reading above says which one and why, and nothing was
            // compared, so the group's expectation is neither met nor broken -- it is not read.
            ok = false
            continue
        }
        if (!expectVerdicts.includes(verdict)) {
            ok = false
            console.log(
                `[${phase}] ${label} reads ${verdict}, and this group expects ${expectation}`,
            )
        }
    }
    if (group.disjoint) {
        for (const [label, caseRun] of runs) {
            if (caseRun.status !== 0) continue
            if (!checkDisjoint(phase, label, canonicalJson(caseRun.stdout))) ok = false
        }
    }
}

// `process.exit` would drop what is still queued for a standard output that is a pipe, and the
// runner reads exactly that: the first run of this tool reported an empty stdout under the plan
// while printing its report on a terminal. The exit code is set and the process ends by itself.
process.exitCode = ok ? 0 : 1
