// The rewritten shape of every function of `cases/opt/generic/algebra.cml`, asserted as the graph's
// expression shapes rather than as `:op/add_*` and `:op/mul_*` totals.
//
// For each function the graph must hold exactly these operators, each rendered as its own
// subexpression (the operator URI, then its operands in input order, recursively). A total of
// multiplications and additions cannot tell `x*3.0 + x*5.0` from `x*2.0 + x*6.0`, and cannot see a
// folded coefficient: the shapes below do, and they are what the laws promise — `t + t` doubles the
// coefficient (6.0, 0.2), the coefficient law combines `x*3.0 + x*5.0` into `x*8.0` when the
// permissions allow it, `((x + 0) * 3) * 2` becomes `x*6`, and `mixed_permissions` keeps its two
// products because the annotations differ. `shared_product` keeps two products as well: the one its
// print reads (`x*3.0`) and the folded one it returns.
//
// `safe_fp=true` leaves underflow-sensitive regrouping alone: the doubling of `x*0.1` stays a
// product of the product by 2.0, and the coefficient law does not combine the two products of
// `x*3.0 + x*5.0` (`relaxed_coefficients` keeps the reassociation's permission but the safe mode
// still declines the fold). The last dot-segment of `input.test_name` (`default` or `safe`) selects
// the mode. `branch` is not listed, as before: its arms call `strict` instead of holding its body.

import fs from 'node:fs'

const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const assert = (condition, message) => {
    if (!condition) throw new Error(message)
}

const EXPECTED = {
    default: {
        strict: [':op/mul_d(x,6.0)'],
        underflow_sensitive: [':op/mul_d(x,0.2)'],
        strict_coefficients: [':op/mul_d(x,8.0)'],
        relaxed_coefficients: [':op/mul_d(x,8.0)'],
        mixed_permissions: [
            ':op/add_d(:op/mul_d(x,3.0),:op/mul_d(x,5.0))',
            ':op/mul_d(x,3.0)',
            ':op/mul_d(x,5.0)',
        ],
        integer_coefficients: [':op/mul_l(x,8)'],
        integer_chain: [':op/mul_l(x,6)'],
        shared_product: [':io/println(:op/mul_d(x,3.0))', ':op/mul_d(x,3.0)', ':op/mul_d(x,6.0)'],
        strict32: [':op/mul_f(x,6.0)'],
        integer32: [':op/mul_i(x,8)'],
    },
    safe: {
        strict: [':op/mul_d(x,6.0)'],
        underflow_sensitive: [':op/mul_d(:op/mul_d(x,0.1),2.0)', ':op/mul_d(x,0.1)'],
        strict_coefficients: [
            ':op/add_d(:op/mul_d(x,3.0),:op/mul_d(x,5.0))',
            ':op/mul_d(x,3.0)',
            ':op/mul_d(x,5.0)',
        ],
        relaxed_coefficients: [
            ':op/add_d(:op/mul_d(x,3.0),:op/mul_d(x,5.0))',
            ':op/mul_d(x,3.0)',
            ':op/mul_d(x,5.0)',
        ],
        mixed_permissions: [
            ':op/add_d(:op/mul_d(x,3.0),:op/mul_d(x,5.0))',
            ':op/mul_d(x,3.0)',
            ':op/mul_d(x,5.0)',
        ],
        integer_coefficients: [':op/mul_l(x,8)'],
        integer_chain: [':op/mul_l(x,6)'],
        shared_product: [':io/println(:op/mul_d(x,3.0))', ':op/mul_d(x,3.0)', ':op/mul_d(x,6.0)'],
        strict32: [':op/mul_f(x,6.0)'],
        integer32: [':op/mul_i(x,8)'],
    },
}

const mode = input.test_name.split('.').pop()
const expected = EXPECTED[mode]
assert(expected, `no shapes recorded for ${input.test_name}`)

const document = JSON.parse(input.normalized.stdout)
const graphOf = (name) => {
    const graph = document.graphs.find((g) => g.name === name)
    assert(graph, `missing ${name}`)
    return graph
}

// Every operator of the graph as its own subexpression, recursing through the normal inputs in
// their declared order; a leaf is the node's own label (a port, a constant, a function).
const shapesOf = (graph) => {
    const nodes = new Map(graph.nodes.map((n) => [n.id, n]))
    const inputs = new Map()
    for (const edge of graph.edges) {
        if (edge.kind !== 'normal') continue
        if (!inputs.has(edge.target)) inputs.set(edge.target, new Map())
        inputs.get(edge.target).set(edge.targetIndex, edge.source)
    }
    const at = (id) => {
        const node = nodes.get(id)
        assert(node, `a referenced node is not in the graph: ${id}`)
        const operands = [...(inputs.get(id) || new Map()).keys()]
            .sort((a, b) => a - b)
            .map((slot) => at(inputs.get(id).get(slot)))
        return node.kind === 'OPER' ? `${node.label}(${operands.join(',')})` : node.label
    }
    return graph.nodes
        .filter((n) => n.kind === 'OPER')
        .map((n) => at(n.id))
        .sort()
}

for (const [name, shapes] of Object.entries(expected)) {
    const graph = graphOf(name)
    const found = shapesOf(graph)
    assert(
        JSON.stringify(found) === JSON.stringify(shapes),
        `${name}: the rewritten shapes are not the expected ones\n  found    ${JSON.stringify(found)}\n  expected ${JSON.stringify(shapes)}`,
    )
}

// The permissions a function declares are part of what the laws may use, and the source's own
// consumer is still recorded on the node the algebra rewrote.
const relaxed = graphOf('relaxed_coefficients').nodes.filter((n) => n.kind === 'OPER')
assert(
    relaxed.every((n) => n.numeric === 'reassociate'),
    'permissions changed',
)
const product = graphOf('strict').nodes.find((n) => n.label === ':op/mul_d')
assert(product.origin && product.source, 'consumer source lost')