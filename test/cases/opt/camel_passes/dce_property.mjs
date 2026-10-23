// The properties of the graph `opt::dce` (stdlib/opt.cml) leaves, read from the JSON `std::jgir`
// prints. The reference of the same pipeline is the golden the plan compares against; this file
// is what a node count could not say, because a count passes on a graph that removed the wrong
// node.
//
//   1. No graph keeps a value nobody reads. A value is a constant, a conversion, a copy, an
//      access, a fill, or an operation that reads and writes nothing outside the graph — the
//      `isValueOnly` of the C++ pass, as far as a dump can tell. This is the sweep's fixpoint,
//      and it is what the corpus needs two rounds for: `std::opt::inline` takes the only user of
//      `chain`'s product away, so the product is dead, and the constant it reads becomes dead
//      only after the product is gone.
//   2. The value the program uses is still there, and still read: `kept`'s product is read by its
//      sum, which reaches the graph's exit. A pass that removed too much fails this.
//
// What the guard cannot see is the node kinds of a dump: `std::jgir` writes a node's kind and its
// operator, not the shape of its value, so a cast, a copy, an access and a fill are recognised by
// their operator text and the corpus does not use them.

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

// A value of the graph, as a dump tells it: a constant (a number, a bool or a string), or an
// operation that neither reads nor writes a variable and does not reach the outside. A port, an
// argument list, a call, a function value and a gate are not values of this kind, and the exit of
// a graph is what the C++ pass calls an anchor and refuses to erase.
const isConstant = (n) =>
    n.kind === 'DATA' &&
    (/^-?[0-9]+(\.[0-9]+)?$/.test(n.label) || n.label === 'true' || n.label === 'false' || /^'.*'$/.test(n.label))
const isValueOnly = (n) =>
    isConstant(n) ||
    (n.kind === 'OPER' &&
        !n.label.startsWith(':var/') &&
        !n.label.startsWith(':io/') &&
        !n.label.startsWith(':op/assn'))

//// 1. no graph keeps a value nobody reads.

for (const g of doc.graphs) {
    for (const n of g.nodes.filter(isValueOnly)) {
        if (n.id === g.exit) continue
        assert(
            valueUsers(g, n.id).length > 0,
            `the graph '${g.name}' still keeps a value nobody reads: '${n.label}' (${n.id}) has no user`,
        )
    }
}

//// 2. the value the program uses is still there, and still read.

const kept = graph('kept')
const keptProducts = kept.nodes.filter((n) => n.label === ':op/mul_l')
assert(keptProducts.length === 1, `the graph 'kept' has ${keptProducts.length} products, not the one the source wrote`)
const keptSums = kept.nodes.filter((n) => n.label === ':op/add_l')
assert(keptSums.length === 1, `the graph 'kept' has ${keptSums.length} sums, not the one the source wrote`)
const keptOperands = normal(kept, keptSums[0].id)
assert(
    keptOperands.length === 2 && keptOperands[0].id === keptProducts[0].id,
    `the sum of 'kept' no longer reads its product: ${keptOperands.map((n) => `${n.label}@${n.id}`).join(', ')}`,
)
assert(
    normal(kept, keptProducts[0].id).length === 2,
    `the product of 'kept' lost one of its operands: ${normal(kept, keptProducts[0].id).map((n) => n.label).join(', ')}`,
)