// The corpus the property guard of `opt::cse` is about, before any pass runs: the values written
// twice are still two nodes here. A guard that examined nothing would pass on a graph that has
// nothing to merge, so this entry runs the same case with no pass and asserts that the duplicates
// the sweep is about are there -- and, for `written`, that the two products are *not* duplicates
// of one another, because the name between them is another value.
//
// The compiler folds constant arithmetic while it builds the graph, so `literals` holds its
// duplicate as constants (two separate `7`s) and the operation over two separate literals is a
// separate node too. `written` is the other half: a product written twice over two different
// values, which the sweep has to leave as two computations.

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
const constants = (g, label) => g.nodes.filter((n) => n.kind === 'DATA' && n.label === label)

// `shared`: `let a = x * 7; let b = x * 7; return a + b` — two products, over two separate `7`s.
const shared = graph('shared')
const sharedProducts = shared.nodes.filter((n) => n.label === ':op/mul_l')
assert(sharedProducts.length === 2, `the graph 'shared' has ${sharedProducts.length} products, not the two the source wrote`)
const sevenOf = (product) => normal(shared, product.id).find((n) => n.label === '7')
const sevens = sharedProducts.map(sevenOf)
assert(
    sevens.every(Boolean) && sevens[0].id !== sevens[1].id,
    `the two products of 'shared' do not read two separate literals of 7: ${sevens.map((n) => (n && n.id) || 'none').join(', ')}`,
)

// `literals`: `let a = 3 + 4; let b = 3 + 4; return a * b` — two constants of the value 7, and one
// product reading both of them.
const literals = graph('literals')
const literalsSevens = constants(literals, '7')
assert(literalsSevens.length === 2, `the graph 'literals' has ${literalsSevens.length} constants of 7, not the two the source wrote`)
const literalsProducts = literals.nodes.filter((n) => n.label === ':op/mul_l')
assert(literalsProducts.length === 1, `the graph 'literals' has ${literalsProducts.length} products, not the one the source wrote`)
const literalsOperands = normal(literals, literalsProducts[0].id)
assert(
    literalsOperands.length === 2 && literalsOperands[0].id === literalsSevens[0].id && literalsOperands[1].id === literalsSevens[1].id,
    `the product of 'literals' does not read its two separate constants of 7: ${literalsOperands.map((n) => `${n.label}@${n.id}`).join(', ')}`,
)

// `written`: `cell * 3` is written twice, and `cell = cell + 1` between the two writes rebinds the
// name to another value, so the two products are two computations over two different values --
// one over the parameter, one over the sum the rebinding made. The `3` they both read is written
// twice, and that is the duplicate this entry is about here.
const written = graph('written')
const writtenProducts = written.nodes.filter((n) => n.label === ':op/mul_l')
assert(writtenProducts.length === 2, `the graph 'written' has ${writtenProducts.length} products, not the two the source wrote`)
const firstOperand = (product) => normal(written, product.id)[0]
const firstOperands = writtenProducts.map(firstOperand)
assert(
    firstOperands.every(Boolean) && firstOperands[0].id !== firstOperands[1].id,
    `the two products of 'written' do not read two different values: ${firstOperands.map((n) => (n && n.id) || 'none').join(', ')}`,
)
const valueRead = (n) => (n.kind === 'PORT' ? 'the parameter' : n.label)
const readShapes = firstOperands.map(valueRead).sort()
assert(
    readShapes.join(' ; ') === ':op/add_l ; the parameter',
    `the two products of 'written' do not read the parameter and the value the rebinding made: ${readShapes.join(' ; ')}`,
)
const threes = constants(written, '3')
assert(threes.length === 2, `the graph 'written' has ${threes.length} constants of 3, not the two the source wrote`)
