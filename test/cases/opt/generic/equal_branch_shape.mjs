// The branch structure of `cases/opt/generic/equal_branch_values.cml`, asserted as a comparison of
// the branches themselves rather than as counts of BRCH, JOIN and multiply nodes.
//
// The source has three conditional expressions. Each must still be one branch, recognized by the
// values its arms join over and by its condition being the runtime `env:get_env` read (a folded
// condition, a swapped arm, an extra branch and a lost branch all fail), and the two effects of the
// first branch's arms must still be controlled by that branch. The BRCH and JOIN totals follow from
// that classification; they are not the assertion.
//
// What the pipeline does to the first branch's value is the property that separates the entries:
// `std::inline` (and `std::inline std::opt::cse`) keep the source's arithmetic, so the value the
// branch prints is its product with itself; `std::opt` folds the equal arms' product into the
// constant 4.0 and keeps it ordered after the branch, leaves the branch whose arms differ (2.0,
// 3.0) its product, and passes the branch of signed zeros through the neutral law of `* 1.0`
// unchanged. The last dot-segment of `input.test_name` (`inline`, `cse` or `fold`) selects which of
// the two the run must show.

import fs from 'node:fs'

const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const assert = (condition, message) => {
    if (!condition) throw new Error(message)
}

const main = JSON.parse(input.normalized.stdout).graphs.find((graph) => graph.name === 'main')
assert(main, 'the main graph is missing from the JSON the pass printed')

const node = (id) => main.nodes.find((n) => n.id === id)
const inputs = (id, kind) =>
    main.edges
        .filter((edge) => edge.target === id && edge.kind === kind)
        .sort((a, b) => a.targetIndex - b.targetIndex)
        .map((edge) => node(edge.source))
const users = (id, kind) =>
    main.edges.filter((edge) => edge.source === id && edge.kind === kind).map((edge) => node(edge.target))

// The value an arm joins with. While the source's call is still a graph node the arm is a GATE that
// carries the arm's effect; once equal-branch folding moved that effect onto the join, the arm is
// the constant or the operator itself.
const armValue = (arm) => {
    let at = arm
    while (at && at.kind === 'GATE') {
        const below = inputs(at.id, 'normal')[0]
        if (!below) break
        at = below
    }
    return at
}

// The source's `x * x`: a product whose two operands are the branch's own value.
const productOf = (join) =>
    main.nodes
        .filter((n) => n.label === ':op/mul_d')
        .find((n) => {
            const operands = inputs(n.id, 'normal')
            return operands.length === 2 && operands[0].id === join.id && operands[1].id === join.id
        })

const readOf = (branch) => {
    const condition = inputs(branch.id, 'normal')[0]
    assert(
        condition && condition.kind === 'OPER',
        `a branch condition is not an operator: ${condition && condition.label}`,
    )
    const read = inputs(condition.id, 'normal').find((n) => n.label === 'env:get_env')
    assert(read, `a branch is no longer over the runtime read: ${condition.label}`)
    return read
}

const branches = main.nodes
    .filter((n) => n.kind === 'BRCH')
    .map((brch) => {
        const joins = users(brch.id, 'normal').filter((n) => n.kind === 'JOIN')
        assert(joins.length === 1, `a branch drives ${joins.length} joins, not one`)
        const join = joins[0]
        const arms = inputs(join.id, 'context').map(armValue)
        assert(
            arms.length === 2 && arms.every(Boolean),
            `a branch joins ${arms.length} values, not two`,
        )
        return {
            brch,
            join,
            // The two arm values in the order the join takes them (`then`, then `else`).
            arms: arms.map((n) => n.label).join('|'),
            read: readOf(brch),
        }
    })

const shape = branches
    .map((branch) => branch.arms)
    .sort()
    .join(' ; ')
assert(
    shape === '0.0|-0.0 ; 2.0|2.0 ; 2.0|3.0',
    `the branches are not the source's three conditional expressions: ${shape}`,
)
const reads = new Set(branches.map((branch) => branch.read.id))
assert(reads.size === 1, `the runtime read is duplicated: ${reads.size} env:get_env nodes`)

const equal = branches.find((branch) => branch.arms === '2.0|2.0')
const differing = branches.find((branch) => branch.arms === '2.0|3.0')
const zeros = branches.find((branch) => branch.arms === '0.0|-0.0')

// `if flag then left() else right()` prints in both arms: the branch must still carry both effects,
// whichever node the arm values ended up on.
const printText = (print) => (inputs(print.id, 'normal')[0] || {}).label
const armEffects = users(equal.brch.id, 'control').map(printText).sort()
assert(
    JSON.stringify(armEffects) === JSON.stringify(['left', 'right']),
    `the equal-armed branch no longer carries both arm effects: ${JSON.stringify(armEffects)}`,
)

const prints = main.nodes.filter((n) => n.label === ':io/println')
const printedBy = (value) => prints.find((n) => (inputs(n.id, 'normal')[0] || {}).id === value.id)

if (input.test_name.split('.').pop() === 'fold') {
    assert(!productOf(equal.join), 'the equal-armed product was not folded away')
    const ordered = users(equal.join.id, 'control')
    assert(
        ordered.length === 1,
        `the folded value is not the one effect ordered after its branch: ${ordered.length} effects`,
    )
    const folded = inputs(ordered[0].id, 'normal')[0]
    assert(
        folded && folded.kind === 'DATA' && folded.label === '4.0',
        `the equal arms' product did not fold to 4.0: ${folded && folded.label}`,
    )
    assert(productOf(differing.join), 'the branch whose arms differ lost its product')
    assert(!productOf(zeros.join), 'a product of the signed-zero branch survived the neutral law')
    assert(
        printedBy(zeros.join),
        'the signed-zero branch is no longer passed to its print unchanged',
    )
} else {
    const product = productOf(equal.join)
    assert(product, 'the equal-armed branch value is no longer multiplied')
    assert(printedBy(product), 'the product of the equal-armed branch is no longer printed')
    assert(productOf(differing.join), 'the branch whose arms differ lost its product')
}