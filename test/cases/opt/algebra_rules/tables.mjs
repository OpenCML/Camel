// The measurement's table, one entry per corpus. Two tables are compared, and neither is a
// snapshot of a pass that no longer exists:
//
//   * the **reference** column, read from the plan's `by_cpp` golden: `std::opt::algebra
//     neutral_only=true` followed by the same `std::opt::dce values_only=true` the library side
//     runs. The mode is in this tree (`src/passes/opt/generic/algebra.cpp`), so this column is
//     re-recordable and the plan's `*.by_cpp` entries pin it for itself.
//   * the **library** column, the run being verified: `rules::simplify` with the same pass list,
//     read from this entry's own stdout. It is live on the side that can change, and nothing about
//     it is recorded anywhere.
//
// The corpus is a property, not a golden. What is asserted is what the library does to these
// graphs, form by form, and never the node count or the position of a node:
//
//   `no graph grows`        every graph the two tables share answers with at most the reference's
//                           nodes, at most its edges and at most its count of every operator. The
//                           one exception the tables name is the doubling, whose whole content is
//                           to write one product over the sum it replaces: `x + x` reads `x` twice
//                           and `x * 2` reads it once, so that form answers with exactly one node
//                           more and with that product where the reference had none. Every other
//                           form must not grow at all.
//   `the library rewrites`  exactly the recorded number of forms come out strictly smaller, so a
//                           library that stopped rewriting a form is red and a function that is
//                           simply absent cannot hide in the per-form rules.
//   `the corpus is whole`   both tables examine the same graphs, in the same order, and not a
//                           different number of them.
//
// A graph that grew is reported by name and by measure, so the reader is sent to the form that
// moved rather than to a line number.

import fs from 'node:fs'
import { applyNormalizers } from '../../../tools/normalizers.mjs'

const NORMALIZERS = ['strip_ansi', 'normalize_newlines', 'trim_trailing_space', 'normalize_paths', 'normalize_origins']

const TABLES = {
    // The ten integer neutral forms both sides perform, form by form.
    'algebra_rules.agreement.table': {
        reference: 'algebra_rules.agreement.cpp.txt',
        graphs: 11,
        rewritten: 0,
        doubling: [],
    },
    // The nineteen forms of this corpus. The reference mode performs the five integer neutral
    // identities only, so it leaves the combine, the coefficient fold, the doubling and every
    // floating-point form as written; the library carries the laws for them.
    'algebra_rules.library_only.table': {
        reference: 'algebra_rules.library_only.cpp.txt',
        graphs: 19,
        rewritten: 17,
        doubling: ['double', 'double_d'],
    },
    // The element-wise forms. One form both sides rewrite, one the library rewrites alone, and
    // two neither reaches.
    'algebra_rules.tensor.table': {
        reference: 'algebra_rules.tensor.cpp.txt',
        graphs: 5,
        rewritten: 2,
        doubling: ['tensor_double'],
    },
}

const assert = (condition, message) => {
    if (!condition) throw new Error(message)
}

const isTableLine = (line) => /^\S+ nodes \d+ edges \d+ add_l \d+/.test(line)

const parse = (line) => {
    const tokens = line.split(' ')
    const counts = {}
    for (let i = 5; i + 1 < tokens.length; i += 2) counts[tokens[i]] = Number(tokens[i + 1])
    return { name: tokens[0], nodes: Number(tokens[2]), edges: Number(tokens[4]), counts }
}

const nonzero = (counts) => Object.fromEntries(Object.entries(counts).filter(([, n]) => n !== 0).sort())

const show = (parsed) => `${parsed.name} nodes ${parsed.nodes} edges ${parsed.edges} ${JSON.stringify(nonzero(parsed.counts))}`

// A graph came out of the library no larger than the reference: at most its nodes, at most its
// edges, and at most its count of every operator. The named doublings are the one exception --
// `x + x => x * 2` writes a product the reference's sum did not have and reads its operand once
// instead of twice, so that form is allowed exactly one node more and any product count.
const dominance = (reference, library, doubling) => {
    const trouble = []
    const allowed = doubling ? 1 : 0
    if (library.nodes > reference.nodes + allowed) {
        trouble.push(`nodes ${library.nodes} against the reference's ${reference.nodes}`)
    }
    if (library.edges > reference.edges) {
        trouble.push(`edges ${library.edges} against the reference's ${reference.edges}`)
    }
    const operators = [...new Set([...Object.keys(reference.counts), ...Object.keys(library.counts)])]
    for (const operator of operators) {
        if (doubling && operator.includes('mul')) continue
        if ((library.counts[operator] || 0) > (reference.counts[operator] || 0)) {
            trouble.push(`${operator} ${library.counts[operator]} against the reference's ${reference.counts[operator] || 0}`)
        }
    }
    return trouble
}

// The library did something to this graph: fewer nodes, fewer edges, or one operator the
// reference left with a smaller count.
const strictlySmaller = (reference, library) => {
    if (library.nodes < reference.nodes || library.edges < reference.edges) return true
    const operators = [...new Set([...Object.keys(reference.counts), ...Object.keys(library.counts)])]
    return operators.some((operator) => (library.counts[operator] || 0) < (reference.counts[operator] || 0))
}

const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const recorded = TABLES[input.test_name]
assert(recorded, `no table is recorded for the entry '${input.test_name}'`)

const read = (name) => {
    const path = new URL(`../../../plans/feat/opt/golden/${name}`, import.meta.url)
    return applyNormalizers(fs.readFileSync(path, 'utf8').replace(/\r\n/g, '\n'), NORMALIZERS).split('\n')
}

const referenceLines = read(recorded.reference)
// The library column is this run's own table: the entry runs the library with `measure::report`,
// so its stdout is what the library answers now, and nothing about it is frozen. The reference
// column is the plan's `by_cpp` golden, which the entry beside it re-records from this tree.
const actualLines = input.normalized.stdout.split('\n')

let examined = 0
let rewritten = 0
const grown = []
for (let i = 0; i < referenceLines.length; i++) {
    const left = referenceLines[i]
    const right = actualLines[i]
    assert(
        isTableLine(left) === isTableLine(right),
        `line ${i + 1} is a table line on one side only:\n  reference: ${JSON.stringify(left)}\n  library:   ${JSON.stringify(right)}`,
    )
    if (!isTableLine(left)) {
        assert(
            left === right,
            `line ${i + 1} is the program's own output and differs:\n  reference: ${left}\n  library:   ${right}`,
        )
        continue
    }
    examined++
    const reference = parse(left)
    const library = parse(right)
    assert(
        reference.name === library.name,
        `line ${i + 1} names two graphs: '${reference.name}' and '${library.name}'`,
    )
    const doubling = recorded.doubling.includes(reference.name)
    const trouble = dominance(reference, library, doubling)
    if (trouble.length > 0) grown.push(`'${reference.name}' grew: ${trouble.join(', ')}`)
    if (strictlySmaller(reference, library)) rewritten++
    else if (!doubling) {
        assert(
            JSON.stringify(reference) === JSON.stringify(library),
            `'${reference.name}' is not a recorded difference and the two sides differ:\n  reference: ${show(reference)}\n  library:   ${show(library)}`,
        )
    }
}

assert(examined > 0, 'the corpus held no graph to examine: the measurement would have passed on nothing')
assert(
    examined === recorded.graphs,
    `the corpus is not the one recorded: ${examined} graphs examined, ${recorded.graphs} recorded`,
)
assert(grown.length === 0, `the library grew a graph it rewrote:\n${grown.join('\n')}`)
assert(
    rewritten === recorded.rewritten,
    `the library rewrote ${rewritten} forms, not the recorded ${recorded.rewritten}: a rule that stopped firing, or a form the reference no longer reaches, moves this number`,
)
