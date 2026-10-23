// The corpus the property guard of `opt::dce` is about, one pass before it: after `std::opt::inline`
// runs and before the sweep, `chain` holds the values nobody reads. A guard that examined nothing
// would pass on a graph that has nothing to remove, so this entry runs the same case with the
// inlining and without the sweep, and asserts the dead chain is there: a product no node reads,
// whose operand is a constant the product is the only user of.
//
// That second step is what the corpus is for: a sweep that removed one round and stopped would
// leave the constant behind, and this is the node it would leave.

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
const valueUsers = (g, id) => g.edges.filter((e) => e.source === id && e.kind === 'normal')

const chain = graph('chain')
const dead = chain.nodes.filter((n) => n.label === ':op/mul_l' && valueUsers(chain, n.id).length === 0)
assert(
    dead.length === 1,
    `the graph 'chain' has ${dead.length} products nobody reads, not the one the inlining leaves behind`,
)
const operands = normal(chain, dead[0].id)
const constant = operands.find((n) => n.kind === 'DATA' && n.label === '2')
assert(constant, `the product nobody reads does not read the constant 2: ${operands.map((n) => `${n.label}@${n.id}`).join(', ')}`)
assert(
    valueUsers(chain, constant.id).length === 1,
    `the constant 2 is read by ${valueUsers(chain, constant.id).length} nodes, not only by the product that is dead`,
)

// The other side of the corpus is alive here: `kept`'s product is read by its sum.
const kept = graph('kept')
const keptProducts = kept.nodes.filter((n) => n.label === ':op/mul_l')
assert(keptProducts.length === 1, `the graph 'kept' has ${keptProducts.length} products, not the one the source wrote`)
assert(valueUsers(kept, keptProducts[0].id).length > 0, "the product of 'kept' has no user before the sweep")