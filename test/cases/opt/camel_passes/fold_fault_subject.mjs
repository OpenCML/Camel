// What the `fold_fault` corpus has to hold *before* any pass, read from the JSON `std::jgir`
// prints: the guard `fold_fault.property` must not pass on a graph that had nothing to evaluate.
//
//   1. The graph's returned value is an operation over two constants — the anchor the pass may not
//      replace, and the expression that can only fault. Without it, a pass that skipped anchors
//      entirely would pass the property entry for the wrong reason.
//   2. Beside it, an operation over two constants the pass *does* replace, so one round both
//      evaluates a node it leaves and replaces one it keeps.
//   3. The two are in one round: the division is a control input of the product, which is what the
//      source's order (`let a = 1 / 0` above `let b = x * y`) puts in the graph.
//
// A `std::gir` golden cannot say this: it records what is there, not that the guard looked at it.

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
const inputs = (g, id, kind) =>
    g.edges
        .filter((e) => e.target === id && e.kind === kind)
        .sort((a, b) => a.targetIndex - b.targetIndex)
        .map((e) => node(g, e.source))

// A constant of the graph, as a dump tells it: a number, a bool or a string. A port is not one.
const isConstant = (n) =>
    n.kind === 'DATA' &&
    (/^-?[0-9]+(\.[0-9]+)?$/.test(n.label) || n.label === 'true' || n.label === 'false' || /^'.*'$/.test(n.label))

const main = graph('main')

/// The value the graph returns: what its exit reads on its own value input.
const returned = (g) => {
    const exit = node(g, g.exit)
    assert(exit, `the graph '${g.name}' has no exit node '${g.exit}'`)
    const values = inputs(g, exit.id, 'normal')
    assert(values.length > 0, `the exit of '${g.name}' reads no value`)
    return values[0]
}

//// 1. the returned value is the division over two constants.

const division = main.nodes.find((n) => n.label === ':op/div_l')
assert(division, "the corpus no longer holds the division ':op/div_l' the pass cannot replace")
const divisionOperands = inputs(main, division.id, 'normal')
assert(
    divisionOperands.length === 2 && divisionOperands.every(isConstant),
    `the division reads ${divisionOperands.map((n) => n.label).join(', ')}, not two constants`,
)
assert(
    returned(main).id === division.id,
    `the graph returns '${returned(main).label}', not the division: the corpus no longer holds a ` +
        'constant expression the pass may not replace',
)

//// 2. beside it, an operation over two constants that the round does replace.

const product = main.nodes.find((n) => n.label === ':op/mul_l')
assert(product, "the corpus no longer holds the product ':op/mul_l' the pass replaces")
const productOperands = inputs(main, product.id, 'normal')
assert(
    productOperands.length === 2 && productOperands.every(isConstant),
    `the product reads ${productOperands.map((n) => n.label).join(', ')}, not two constants`,
)

//// 3. one round holds both: the division orders the product.

assert(
    inputs(main, product.id, 'control').some((n) => n.id === division.id),
    'the division no longer orders the product, so the two nodes are not in one round together',
)