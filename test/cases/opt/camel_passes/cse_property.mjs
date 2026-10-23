// The properties of the graph `opt::cse` (stdlib/opt.cml) leaves, read from the JSON `std::jgir`
// prints. The reference of the same corpus is the golden the plan compares against; this file is
// what a node count could not say, because a count passes on a graph that merged the wrong pair.
//
//   1. No two nodes of a graph are one value. A constant of a type whose `==` is bit equality,
//      and an operation with no effect and no variable read, is a value; two values whose
//      operator, type and operands in order are the same are one node. This is the sweep's
//      fixpoint, and it is the property the corpus is built for: `shared` writes `x * 7` twice
//      over two literals, and the two literals have to be one node before the two products can.
//   2. The two reads of a cell with a write between them are two values. The two products over
//      them stay two nodes and read two different nodes: nothing is merged across a write.
//   3. The sums the program computes are over what the source wrote: `shared`'s sum reads its one
//      product twice, `literals`' product reads its one sum twice, and `apart`'s two sums keep
//      their own constants (1 and 2). A pass that merged too much fails these.
//
// What the guard cannot see is the permissions and the region of a node: `std::jgir` does not
// carry them, and the corpus is a graph whose nodes are all in the function body, so the pass's
// comparison of them is left to the isomorphic comparison against the golden.

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
const edgesInto = (g, id, kind) =>
    g.edges
        .filter((e) => e.target === id && e.kind === kind)
        .sort((a, b) => a.targetIndex - b.targetIndex)
const normal = (g, id) => edgesInto(g, id, 'normal').map((e) => node(g, e.source))
const withs = (g, id) => edgesInto(g, id, 'context').map((e) => node(g, e.source))
const valueUsers = (g, id) => g.edges.filter((e) => e.source === id && e.kind === 'normal')

// A constant of the graph, as its label reads: a number or a bool. A string constant, a tensor
// and the function value of a call are not compared here, and neither is a node of a shape this
// corpus does not use.
const isConstant = (n) =>
    n.kind === 'DATA' && (/^-?[0-9]+(\.[0-9]+)?$/.test(n.label) || n.label === 'true' || n.label === 'false')

// An operation that computes a value: not one that reads or writes a variable, and not one that
// reaches the outside (`:io/`), which the pass leaves alone for the same reason.
const isOperation = (n) =>
    n.kind === 'OPER' && !n.label.startsWith(':var/') && !n.label.startsWith(':io/') && !n.label.startsWith(':op/assn')

const signature = (g, n) =>
    [n.kind, n.label, n.outputType, normal(g, n.id).map((m) => m.id).join(','), withs(g, n.id).map((m) => m.id).join(',')].join('|')

//// 1. no graph keeps two nodes that are one value.

for (const g of doc.graphs) {
    const seen = new Map()
    for (const n of g.nodes.filter((n) => isConstant(n) || isOperation(n))) {
        const key = signature(g, n)
        const first = seen.get(key)
        if (first) {
            assert(
                false,
                `the graph '${g.name}' still has two nodes that are one value: '${first.label}' (${first.id}) and '${n.label}' (${n.id}) are the same computation over the same operands`,
            )
        }
        seen.set(key, n)
    }
}

//// 2. nothing was merged across a write.

const written = graph('written')
const writtenProducts = written.nodes.filter((n) => n.label === ':op/mul_l')
assert(
    writtenProducts.length === 2,
    `the two products over the reads of the cell are not two nodes: ${writtenProducts.length}`,
)
const readOfProduct = (product) => normal(written, product.id)[0]
assert(
    readOfProduct(writtenProducts[0]).id !== readOfProduct(writtenProducts[1]).id,
    `the two products over the reads of the cell share their operand (${readOfProduct(writtenProducts[0]).id}): the pass merged across a write`,
)
assert(
    valueUsers(written, readOfProduct(writtenProducts[0]).id).length === 1 &&
        valueUsers(written, readOfProduct(writtenProducts[1]).id).length === 1,
    'a read of the cell is read by more than the one product it was written for',
)

//// 3. the sums the program computes are over what the source wrote.

/// The graph's one product, which the guard then asks about: its two operands are one node.
const oneProduct = (name) => {
    const g = graph(name)
    const products = g.nodes.filter((n) => n.label === ':op/mul_l')
    assert(products.length === 1, `the graph '${name}' has ${products.length} products, not the one the source wrote`)
    return products[0]
}

// `shared`: `let a = x * 7; let b = x * 7; return a + b` — one product, and the sum reads it twice.
const shared = graph('shared')
const sharedProduct = oneProduct('shared')
const sharedSums = shared.nodes.filter((n) => n.label === ':op/add_l')
assert(sharedSums.length === 1, `the graph 'shared' has ${sharedSums.length} sums, not the one the source wrote`)
const sharedOperands = normal(shared, sharedSums[0].id)
assert(
    sharedOperands.length === 2 && sharedOperands[0].id === sharedProduct.id && sharedOperands[1].id === sharedProduct.id,
    `the sum of 'shared' does not read its one product twice: ${sharedOperands.map((n) => n.id).join(', ')}`,
)

// `literals`: `let a = 3 + 4; let b = 3 + 4; return a * b` — the compiler folds each sum into the
// constant 7, so the product has to read one and the same constant twice.
const literalsProduct = oneProduct('literals')
const literalsOperands = normal(graph('literals'), literalsProduct.id)
assert(
    literalsOperands.length === 2 && literalsOperands[0].id === literalsOperands[1].id,
    `the product of 'literals' does not read one value twice: ${literalsOperands.map((n) => n.id).join(', ')}`,
)
assert(
    isConstant(literalsOperands[0]),
    `the value the product of 'literals' reads twice is not a constant: ${literalsOperands[0].label}`,
)

const apart = graph('apart')
const apartSums = apart.nodes.filter((n) => n.label === ':op/add_l')
assert(apartSums.length === 2, `the graph 'apart' has ${apartSums.length} sums, not the two the source wrote`)
const constantOf = (sum) => normal(apart, sum.id).filter(isConstant).map((n) => n.label)
const wrote = apartSums.map(constantOf).map((labels) => labels.join(',')).sort()
assert(
    wrote.join(' ; ') === '1 ; 2',
    `the two sums of 'apart' do not keep the constants the source wrote: ${wrote.join(' ; ')}`,
)