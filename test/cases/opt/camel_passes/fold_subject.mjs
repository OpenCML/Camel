// The corpus the property guard of `opt::fold` is about, one pass before it: the operations whose
// operands are constants are here, as the compiler leaves them. A guard that examined nothing would
// pass on a graph with nothing to fold, so this entry asserts the corpus holds what the pass is
// about, and holds the two things the property guard needs the corpus for:
//
//   1. `chain` needs two rounds. `let a = 2 * 3` is one constant by the time the graph exists (the
//      front end folds the two literals of one expression), `let b = a + 4` is an addition over
//      constants, and `let c = b * 2` is a product over that addition. A pass that folded one round
//      and stopped would leave the product reading an operation, and that operand is what says so.
//   2. `live` has operations over a parameter, which no round may touch: one of them reads the
//      graph's port.
//
// What the guard cannot see is *which* value a folded node will hold: what the operator computes is
// the pass's answer, and the property entry is where the constants it produced are read.

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

//// 1. `chain` holds an addition over a constant and a product over that addition.

const chain = graph('chain')
const additions = chain.nodes.filter((n) => n.label === ':op/add_l')
const products = chain.nodes.filter((n) => n.label === ':op/mul_l')
assert(additions.length === 1, `the graph 'chain' has ${additions.length} additions, not the one the source wrote`)
assert(products.length === 1, `the graph 'chain' has ${products.length} products, not the one the source wrote`)
assert(
    overConstants(chain, additions[0]),
    `the addition of 'chain' does not read constants: ${normal(chain, additions[0].id).map((n) => `${n.label}@${n.id}`).join(', ')}`,
)
assert(
    overConstants(chain, products[0]) === false,
    "the product of 'chain' reads constants already: the corpus no longer needs a second round",
)
const productOperands = normal(chain, products[0].id)
assert(
    productOperands.length === 2 && productOperands[0].id === additions[0].id,
    `the product of 'chain' does not read its addition: ${productOperands.map((n) => `${n.label}@${n.id}`).join(', ')}`,
)
assert(isConstant(productOperands[1]), `the product's second operand is no constant: ${productOperands[1].label}`)

//// 2. `compare` holds a comparison over two constants.

const compare = graph('compare')
const comparisons = compare.nodes.filter((n) => n.label === ':op/lt_l')
assert(comparisons.length === 1, `the graph 'compare' has ${comparisons.length} comparisons, not the one the source wrote`)
assert(
    overConstants(compare, comparisons[0]),
    `the comparison of 'compare' does not read constants: ${normal(compare, comparisons[0].id).map((n) => n.label).join(', ')}`,
)

//// 3. `live` holds operations over the parameter, and no operation over constants.

const live = graph('live')
const liveOperations = live.nodes.filter(isOperation)
const ports = live.nodes.filter((n) => n.kind === 'PORT')
assert(ports.length === 1, `the graph 'live' has ${ports.length} ports, not the one parameter`)
assert(liveOperations.length > 0, "the graph 'live' holds no operation at all")
for (const n of liveOperations) {
    assert(overConstants(live, n) === false, `the operation '${n.label}' of 'live' reads constants: nothing there may fold`)
}
assert(
    liveOperations.some((n) => normal(live, n.id).some((operand) => operand.id === ports[0].id)),
    "no operation of 'live' reads the parameter: the corpus no longer holds the case that must not fold",
)