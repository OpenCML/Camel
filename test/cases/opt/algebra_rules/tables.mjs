// The measurement's table, one entry per corpus: the legacy C++ algebraic pass's table is the
// golden, the rule library's table is the run being verified, and every line of the two is compared.
//
// The golden of every entry here is the **deleted** C++ law table's output: the table that
// `e8c290554:src/passes/opt/generic/algebra.cpp` held, recorded once with that file restored in the
// working tree (the pass that remains, `std::opt::algebra neutral_only=true`, is the five integer
// neutral elements, and its table is recorded beside this one by the `by_cpp` snapshot entries of
// the plan). The `.deleted_table.` goldens are a reference from the past and cannot be produced from
// this tree again; the library side is re-run on every plan run, so the comparison is live on the
// side that can change.
//
// The corpus is a property, not a single golden: for every graph of the golden the script asks for
// one of three recorded answers, and a graph in neither record must agree character for character.
//
//   `differs`  the graphs the two sides answer differently: the line is parsed and held to the
//              recorded pair, so the node count, the edge count and every operator count of each
//              side are asserted.
//   `agreed`   the graphs the two sides answer the same way, recorded so that a rewrite that starts
//              or stops firing on either side is red. Two of them are the forms *neither* side
//              rewrites (`fold_d` needs the `no_infs` grant no node carries, `tensor_zero` needs a
//              zero tensor of the matched shape, which a rule cannot write).
//
// The lines that are not table lines are the program's own output: they are compared as text, so the
// values the program prints after the library's pass must be the values it printed after the C++
// pass (the laws are exact).

import fs from 'node:fs'
import { applyNormalizers } from '../../../tools/normalizers.mjs'

const NORMALIZERS = ['strip_ansi', 'normalize_newlines', 'trim_trailing_space', 'normalize_paths', 'normalize_origins']

// `[nodes, edges, {operator: count}]` counts only the operators that are not zero.
const TABLES = {
    // The ten integer neutral forms both sides perform: eleven graphs, every line the same.
    'algebra_rules.agreement.table': {
        golden: 'algebra_rules.agreement.deleted_table.txt',
        graphs: 11,
        differs: {},
        agreed: {},
    },
    // The seventeen forms of this corpus. Three are performed by both sides (`fold`, `double`,
    // `double_d`), fourteen by the library alone, and one by neither (`fold_d`).
    'algebra_rules.library_only.table': {
        golden: 'algebra_rules.library_only.deleted_table.txt',
        graphs: 19,
        differs: {
            // The library alone: the deleted table never had a zero, self-subtraction,
            // division-by-one, double-negation or floating-point neutral law.
            mul_zero: [[6, 5, { mul_l: 1, println: 1 }], [5, 3, { println: 1 }]],
            zero_mul: [[6, 5, { mul_l: 1, println: 1 }], [5, 3, { println: 1 }]],
            sub_self: [[5, 5, { sub_l: 1, println: 1 }], [4, 3, { println: 1 }]],
            div_one: [[6, 5, { div_l: 1, println: 1 }], [4, 3, { println: 1 }]],
            neg_neg: [[6, 5, { neg_l: 2, println: 1 }], [4, 3, { println: 1 }]],
            mul_zero32: [[6, 5, { mul_i: 1, println: 1 }], [5, 3, { println: 1 }]],
            sub_self32: [[5, 5, { sub_i: 1, println: 1 }], [4, 3, { println: 1 }]],
            mul_one_d: [[6, 5, { mul_d: 1, println: 1 }], [4, 3, { println: 1 }]],
            add_neg_zero_d: [[6, 5, { add_d: 1, println: 1 }], [4, 3, { println: 1 }]],
            sub_zero_d: [[6, 5, { sub_d: 1, println: 1 }], [4, 3, { println: 1 }]],
            div_one_d: [[6, 5, { div_d: 1, println: 1 }], [4, 3, { println: 1 }]],
            neg_neg_d: [[6, 5, { neg_d: 2, println: 1 }], [4, 3, { println: 1 }]],
            // The C++ table performs both and the library does not: the difference is a condition,
            // not a law. `combine` reads `x * 3` and `x * 5` in a `sync` body, where the second
            // product is control-dependent on the first; the deleted table's `exclusive()` guard
            // (`algebra.cpp#simplify` at `e8c290554`) refuses a product with any other user, control
            // users included, so `term()` lowers to no product and the add is left. `combine_d` is
            // the same shape with float coefficients, refused by the permission the deleted table's
            // float path demands (`algebra.cpp#simplify`, `kReassociate` in the default policy).
            // The rule library's `int_combine`/`float_combine` see neither users nor policy:
            // `simplify` grants the permission the C++ default withholds and no rule can ask who
            // else reads the node.
            combine: [[9, 10, { add_l: 1, mul_l: 2, println: 1 }], [6, 5, { mul_l: 1, println: 1 }]],
            combine_d: [[9, 10, { add_d: 1, mul_d: 2, println: 1 }], [6, 5, { mul_d: 1, println: 1 }]],
        },
        agreed: {
            // Both sides: the like-term combine's two neighbours, and the float doubling.
            fold: [6, 5, { mul_l: 1, println: 1 }],
            double: [6, 5, { mul_l: 1, println: 1 }],
            double_d: [6, 5, { mul_d: 1, println: 1 }],
            // Neither side: the float coefficient fold needs `no_infs`, and the default policy of
            // neither implementation grants it.
            fold_d: [8, 7, { mul_d: 2, println: 1 }],
        },
    },
    // The element-wise forms. One performed by both sides, one by the library alone, two by neither.
    'algebra_rules.tensor.table': {
        golden: 'algebra_rules.tensor.deleted_table.txt',
        graphs: 5,
        differs: {
            tensor_combine: [[8, 10, { println: 1, tensor_add: 1, tensor_mul: 2 }], [5, 5, { println: 1, tensor_mul: 1 }]],
        },
        agreed: {
            tensor_double: [5, 5, { println: 1, tensor_mul: 1 }],
            // Neither side: `tensor_fold` needs the same `no_infs` grant its scalar form needs, and
            // `tensor_zero` would have to write a zero tensor of the matched shape, which the rule
            // language cannot write (the missing piece is a language feature, not a rule).
            tensor_fold: [7, 7, { println: 1, tensor_mul: 2 }],
            tensor_zero: [5, 5, { println: 1, tensor_mul: 1 }],
        },
    },
}

const assert = (condition, message) => {
    if (!condition) throw new Error(message)
}

const isTableLine = (line) => /^\S+ nodes \d+ edges \d+ add_l \d+/.test(line)

const parse = (line) => {
    const tokens = line.split(' ')
    const counts = {}
    for (let i = 5; i + 1 < tokens.length; i += 2) counts[tokens[i]] = Number(tokens[i + 1])
    return { name: tokens[0], nodes: Number(tokens[2]), edges: Number(tokens[4]), counts }
}

const nonzero = (counts) => Object.fromEntries(Object.entries(counts).filter(([, n]) => n !== 0).sort())

const show = (parsed) => `${parsed.name} nodes ${parsed.nodes} edges ${parsed.edges} ${JSON.stringify(nonzero(parsed.counts))}`

const same = (parsed, recorded) =>
    recorded !== undefined &&
    parsed.nodes === recorded[0] &&
    parsed.edges === recorded[1] &&
    JSON.stringify(nonzero(parsed.counts)) === JSON.stringify(Object.fromEntries(Object.entries(recorded[2]).sort()))

const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const recorded = TABLES[input.test_name]
assert(recorded, `no table is recorded for the entry '${input.test_name}'`)
const path = new URL(`../../../plans/feat/opt/golden/${recorded.golden}`, import.meta.url)
const expected = applyNormalizers(fs.readFileSync(path, 'utf8').replace(/\r\n/g, '\n'), NORMALIZERS)
const actual = input.normalized.stdout
assert(actual.trim() !== '', `the run printed nothing: ${JSON.stringify(input.command)}`)

const expectedLines = expected.split('\n')
const actualLines = actual.split('\n')
assert(
    expectedLines.length === actualLines.length,
    `the two runs printed a different number of lines: the recorded C++ table ${expectedLines.length}, the library ${actualLines.length}`,
)

let examined = 0
for (let i = 0; i < expectedLines.length; i++) {
    const left = expectedLines[i]
    const right = actualLines[i]
    if (!isTableLine(left)) {
        assert(left === right, `line ${i + 1} is the program's own output and differs:\n  C++:     ${left}\n  library: ${right}`)
        continue
    }
    examined++
    const cpp = parse(left)
    const lib = parse(right)
    assert(cpp.name === lib.name, `line ${i + 1} names two graphs: '${cpp.name}' and '${lib.name}'`)
    if (recorded.differs[cpp.name]) {
        const [cppWanted, libWanted] = recorded.differs[cpp.name]
        assert(same(cpp, cppWanted), `'${cpp.name}' on the C++ side is not the recorded graph:\n  recorded: ${JSON.stringify(cppWanted)}\n  measured: ${show(cpp)}`)
        assert(same(lib, libWanted), `'${cpp.name}' on the library side is not the recorded graph:\n  recorded: ${JSON.stringify(libWanted)}\n  measured: ${show(lib)}`)
        continue
    }
    if (recorded.agreed[cpp.name]) {
        assert(same(cpp, recorded.agreed[cpp.name]), `'${cpp.name}' is recorded as agreed on the C++ side and is not:\n  measured: ${show(cpp)}`)
        assert(same(lib, recorded.agreed[cpp.name]), `'${cpp.name}' is recorded as agreed and the library differs:\n  measured: ${show(lib)}`)
        continue
    }
    assert(
        JSON.stringify(cpp) === JSON.stringify(lib),
        `'${cpp.name}' is not a recorded difference and the two sides differ:\n  C++:     ${show(cpp)}\n  library: ${show(lib)}`,
    )
}

assert(examined > 0, 'the corpus held no graph to examine: the measurement would have passed on nothing')
assert(
    examined === recorded.graphs,
    `the corpus is not the one recorded: ${examined} graphs examined, ${recorded.graphs} recorded`,
)