// What `opt::fold` (stdlib/opt.cml) leaves on the `fold_fault` corpus, read from the JSON
// `std::jgir` prints. The corpus holds a constant expression that can only fault in the value its
// function returns, and an operation over constants beside it; the property is that the pass
// evaluates the first one, leaves it where it is, and still replaces the second.
//
//   1. The division over two constants is still there, still over its two constants, and still the
//      value the function returns. It is an anchor, so `replace` refuses it; a pass that folded it
//      away would have changed the program's run, and a pass that gave up on the whole round when
//      it met it would have left the product below unfolded.
//   2. The product beside it is gone, replaced by the constant it computes: `6` reaches the print.
//   3. Nothing else was folded: the print still reads the value the graph returns' sibling — the
//      constant — and the division still orders the print, so the round left the order as it was.
//
// What the guard cannot see is the warning the fold raised about the division, and the run:
// `fold_fault.same_text` compares the warnings of both passes and `fold_fault.same_run` holds the
// program's fault to the one it has without the pass.

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
const isOperation = (n) => n.kind === 'OPER' && !n.label.startsWith(':io/') && !n.label.startsWith(':var/')

const main = graph('main')

//// 1. the division over constants is left as written, where the function returns it.

const division = main.nodes.find((n) => n.label === ':op/div_l')
assert(
    division,
    'the division over constants is gone: the pass folded a constant expression that can only fault, ' +
        'so the program no longer faults where it is written',
)
const divisionOperands = inputs(main, division.id, 'normal')
assert(
    divisionOperands.length === 2 && divisionOperands.every(isConstant),
    `the division reads ${divisionOperands.map((n) => n.label).join(', ')}, not two constants`,
)
const exit = node(main, main.exit)
assert(exit, `the graph 'main' has no exit node '${main.exit}'`)
const returned = inputs(main, exit.id, 'normal')[0]
assert(
    returned && returned.id === division.id,
    `the graph returns '${returned && returned.label}', not the division it returned before the pass`,
)

//// 2. the product beside it was replaced by the constant it computes.

assert(
    main.nodes.some((n) => n.label === ':op/mul_l') === false,
    "the product over two constants is still there: the round gave up on the node it could not replace",
)
const prints = main.nodes.filter((n) => n.label === ':io/println')
assert(prints.length === 1, `the graph 'main' has ${prints.length} prints, not the one the source wrote`)
const printed = inputs(main, prints[0].id, 'normal')
assert(printed.length > 0, 'the print reads nothing')
assert(
    isConstant(printed[0]) && printed[0].label === '6',
    `the print reads '${printed[0].label}' (${printed[0].kind}), not the constant '6' the product computes`,
)

//// 3. nothing else moved: no operation over constants is left but the division, and the order is
//// the source's. The division is the one operation over constants the pass must leave, so it is
//// the one node the sweep below skips.

for (const g of doc.graphs) {
    for (const n of g.nodes.filter(isOperation)) {
        if (n.id === division.id) {
            continue
        }
        const operands = inputs(g, n.id, 'normal')
        assert(
            !(operands.length > 0 && operands.every(isConstant)),
            `the graph '${g.name}' holds an operation over constants beside the division: '${n.label}' (${n.id})`,
        )
    }
}
assert(
    inputs(main, prints[0].id, 'control').some((n) => n.id === division.id),
    'the division no longer orders the print, so the round moved the order the source wrote',
)