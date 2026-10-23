// The properties of the graph `opt::fold` (stdlib/opt.cml) leaves, read from the JSON `std::jgir`
// prints. The reference of the same pipeline is the golden the plan compares against; this file is
// what a node count could not say, because a count passes on a graph that folded the wrong node.
//
//   1. No graph keeps an operation whose operands are all constants. That is the pass's fixpoint,
//      and it is what the corpus needs two rounds for: `chain`'s product reads its addition, so the
//      addition has to be a constant before the product can be folded.
//   2. What the program prints is the constant the folding computed: `chain`'s product of the sum
//      of a product, `(2 * 3 + 4) * 2`, is 20, and `compare`'s comparison of 2 and 3 is true. A
//      round that folded the wrong node, or folded in the wrong order, prints something else.
//   3. `live`'s operations are untouched and still read the parameter: folding too much fails here,
//      and the program it prints (13 for `live(4)`) is what `program_still_runs` holds.
//
// What the guard cannot see is the type a constant was folded at beyond the dump's label: the
// `std::jgir` label is what the value prints as, and the type of every node is left to the
// isomorphic comparison against the golden.

import fs from 'node:fs'

const assert = (condition, message) => {
    if (!condition) throw new Error(message)
}

const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const doc = JSON.parse(input.normalized.stdout)
const graph = (name) => {
    const found = doc.graphs.find((g) => g.name === name)
    assert(found, `the graph '${name}' is not in the dump`)
    return found
}
const node = (g, id) => g.nodes.find((n) => n.id === id)
const normal = (g, id) =>
    g.edges
        .filter((e) => e.target === id && e.kind === 'normal')
        .sort((a, b) => a.targetIndex - b.targetIndex)
        .map((e) => node(g, e.source))

// A constant of the graph, as a dump tells it: a number, a bool or a string. A port is not one.
const isConstant = (n) =>
    n.kind === 'DATA' &&
    (/^-?[0-9]+(\.[0-9]+)?$/.test(n.label) || n.label === 'true' || n.label === 'false' || /^'.*'$/.test(n.label))
// An operation the pass evaluates: a pure one. `:io/println` and the var operations are effects.
const isOperation = (n) => n.kind === 'OPER' && !n.label.startsWith(':io/') && !n.label.startsWith(':var/')
const overConstants = (g, n) => {
    const operands = normal(g, n.id)
    return operands.length > 0 && operands.every(isConstant)
}

//// 1. no operation over constants is left.

for (const g of doc.graphs) {
    for (const n of g.nodes.filter(isOperation)) {
        assert(
            overConstants(g, n) === false,
            `the graph '${g.name}' still holds an operation over constants: '${n.label}' (${n.id})`,
        )
    }
}

//// 2. what the program prints is the constant the folding computed.

/// The value a graph prints: what its `:io/println` reads.
const printedValue = (g) => {
    const prints = g.nodes.filter((n) => n.label === ':io/println')
    assert(prints.length === 1, `the graph '${g.name}' has ${prints.length} prints, not the one the source wrote`)
    const operands = normal(g, prints[0].id)
    assert(operands.length > 0, `the print of '${g.name}' reads nothing`)
    return operands[0]
}
const prints = (name, label) => {
    const value = printedValue(graph(name))
    assert(
        isConstant(value) && value.label === label,
        `the graph '${name}' prints '${value.label}' (${value.kind}), not the constant '${label}' the operator computes`,
    )
}
prints('chain', '20')
prints('compare', 'true')

//// 3. the operations over the parameter are untouched.

const live = graph('live')
const liveProducts = live.nodes.filter((n) => n.label === ':op/mul_l')
const liveAdditions = live.nodes.filter((n) => n.label === ':op/add_l')
assert(liveProducts.length === 1, `the graph 'live' has ${liveProducts.length} products, not the one the source wrote`)
assert(liveAdditions.length === 1, `the graph 'live' has ${liveAdditions.length} additions, not the one the source wrote`)
assert(
    normal(live, liveProducts[0].id).some((n) => n.kind === 'PORT'),
    "the product of 'live' no longer reads its parameter",
)
assert(
    normal(live, liveAdditions[0].id).some((n) => n.id === liveProducts[0].id),
    "the addition of 'live' no longer reads its product",
)
assert(
    normal(live, liveAdditions[0].id).some((n) => isConstant(n) && n.label === '1'),
    "the addition of 'live' no longer reads the constant it added",
)