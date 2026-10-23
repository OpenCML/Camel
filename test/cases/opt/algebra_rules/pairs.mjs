// The graphs the C++ algebraic pass and the rule library do NOT answer the same way, named node by
// node and edge by edge. The golden is the C++ pass's `std::jgir`: the deleted full law table
// (`e8c290554:src/passes/opt/generic/algebra.cpp`), whose dump on this corpus is identical to the
// dump of the reference mode that remains (`std::opt::algebra neutral_only=true`, the `by_cpp`
// snapshot beside this entry), so the three differences below are the ones the full table had as
// well. The run being verified is the library's. Every graph of the corpus is examined, and each one
// is either the same graph on the two sides (compared by the terms `compareGirDotIsomorphic` uses: a
// node is its kind, its label, its type and its in/out degree, an edge is its endpoints, its kind
// and its index) or one of the three recorded differences below.
//
// The three differences are the findings, and they are not normalized away:
//
//   `anchor_add`, `anchor_mul32`  the graph's named node. The C++ pass's `isReplaceable` refuses to
//       rewrite the node the graph returns, so the add (the product at i32) is still the graph's
//       exit and its two operands are still its inputs; the rule engine retargets the anchor onto
//       the port, and the operation, its constant and its two edges are gone. The ruling of
//       2026-10-23 (docs/handover/reviews/questions-2026-10-23.md, `rule-conditions-and-policy`
//       item 3) makes this the engine's job and not a rule condition: when a rule replaces the node
//       the graph returns, the engine moves the anchor onto the replacement, as the C++ pass's
//       `replaceFoldResult` does. It lands with the effect refusal below.
//
//   `effect_mul_zero`  the effect. The C++ law table has no `x * 0` law, so the product over the
//       print stands; the library's `int_mul_zero` replaces it by the constant 0 and the print the
//       product read is left with no user. Design principles 9.7 and the rule language's second
//       version section 8 forbid exactly that: the part a rule's right-hand side drops must be
//       pure. The same ruling (docs/handover/reviews/questions-2026-10-23.md, `rule-drops-effect`)
//       decrees the check rather than withdrawing the rule: before a rule is applied, the subgraph
//       the pattern variables that its right-hand side does not use bound is examined, and an
//       effect node -- or a node carrying a sync or order edge -- there refuses the application.
//       This entry is the measurement of the violation, and it carries the correct five-line
//       expectation as a known-fail entry until the engine refuses instead.

import fs from 'node:fs'
import { applyNormalizers } from '../../../tools/normalizers.mjs'

const NORMALIZERS = ['strip_ansi', 'normalize_newlines', 'trim_trailing_space', 'normalize_paths', 'normalize_origins']
const GOLDEN = 'algebra_rules.divergence.deleted_table.jgir'
const GRAPHS = ['anchor_add', 'anchor_mul32', 'effect_mul_zero', 'main']

const assert = (condition, message) => {
    if (!condition) throw new Error(message)
}

const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const path = new URL(`../../../plans/feat/opt/golden/${GOLDEN}`, import.meta.url)
const reference = JSON.parse(applyNormalizers(fs.readFileSync(path, 'utf8').replace(/\r\n/g, '\n'), NORMALIZERS))
const library = JSON.parse(input.normalized.stdout)

const graphOf = (doc, name) => {
    const found = doc.graphs.find((g) => g.name === name)
    assert(found, `the graph '${name}' is not in the dump`)
    return found
}

const signature = (g) => {
    const node = (id) => g.nodes.find((n) => n.id === id)
    const degree = (id) => [
        g.edges.filter((e) => e.source === id).length,
        g.edges.filter((e) => e.target === id).length,
    ]
    const sig = (n) => `${n.kind}|${n.label}|${n.outputType || ''}|${degree(n.id).join('/')}`
    const nodes = g.nodes.map(sig).sort()
    const edges = g.edges
        .map((e) => `${sig(node(e.source))}=>${sig(node(e.target))}|${e.kind}|${e.targetIndex}`)
        .sort()
    return JSON.stringify({ nodes, edges })
}

// The ports of the graph, sorted, so an edge into a port can be named by its input index.
const portOf = (g, label) => g.nodes.filter((n) => n.kind === 'PORT' && n.label === label)

for (const name of GRAPHS) {
    const cpp = graphOf(reference, name)
    const lib = graphOf(library, name)
    const ports = portOf(lib, 'x')
    assert(ports.length <= 1, `'${name}' holds ${ports.length} ports named x`)
    if (name === 'anchor_add' || name === 'anchor_mul32') {
        const operation = name === 'anchor_add' ? ':op/add_l' : ':op/mul_i'
        const cppExit = cpp.nodes.find((n) => n.id === cpp.exit)
        assert(cpp.nodes.length === 3 && cpp.edges.length === 2, `'${name}' on the C++ side is ${cpp.nodes.length} nodes and ${cpp.edges.length} edges, not the recorded 3 and 2`)
        assert(cppExit.label === operation, `'${name}' on the C++ side does not exit at ${operation}: it exits at ${cppExit.label}`)
        const inputs = cpp.edges.filter((e) => e.target === cpp.exit).map((e) => `${cpp.nodes.find((n) => n.id === e.source).label}@${e.targetIndex}`).sort()
        const constant = name === 'anchor_add' ? '0' : '1'
        assert(inputs.join(',') === `${constant}@1,x@0`, `'${name}' on the C++ side does not read the port and the constant ${constant}: ${inputs.join(',')}`)
        assert(lib.nodes.length === 1 && lib.edges.length === 0, `'${name}' on the library side is ${lib.nodes.length} nodes and ${lib.edges.length} edges, not the recorded sole port`)
        const libExit = lib.nodes.find((n) => n.id === lib.exit)
        assert(libExit.kind === 'PORT' && libExit.label === 'x', `'${name}' on the library side does not exit at the port: ${libExit.kind} ${libExit.label}`)
        continue
    }
    if (name === 'effect_mul_zero') {
        assert(cpp.nodes.length === 7 && cpp.edges.length === 6, `'effect_mul_zero' on the C++ side is ${cpp.nodes.length} nodes and ${cpp.edges.length} edges, not the recorded 7 and 6`)
        const products = cpp.nodes.filter((n) => n.label === ':op/mul_l')
        assert(products.length === 1, `'effect_mul_zero' on the C++ side has ${products.length} products, not the one the source wrote`)
        const read = cpp.edges.filter((e) => e.target === products[0].id).map((e) => cpp.nodes.find((n) => n.id === e.source).label).sort()
        assert(read.join(',') === '0,:io/println', `the product of the C++ side does not read the print and the zero: ${read.join(',')}`)
        assert(lib.nodes.length === 6 && lib.edges.length === 4, `'effect_mul_zero' on the library side is ${lib.nodes.length} nodes and ${lib.edges.length} edges, not the recorded 6 and 4`)
        const arithmetic = lib.nodes.filter((n) => (n.label || '').startsWith(':op/'))
        assert(arithmetic.length === 0, `'effect_mul_zero' on the library side still holds arithmetic: ${arithmetic.map((n) => n.label).join(',')}`)
        const prints = lib.nodes.filter((n) => n.label === ':io/println')
        assert(prints.length === 2, `'effect_mul_zero' on the library side holds ${prints.length} prints, not the two the source wrote`)
        const orphan = prints.find((n) => lib.edges.every((e) => e.source !== n.id))
        assert(orphan, `'effect_mul_zero' on the library side keeps both prints read by something: no print was left without a user`)
        const feeds = lib.edges.filter((e) => e.target === orphan.id).map((e) => lib.nodes.find((n) => n.id === e.source).label).sort()
        assert(feeds.join(',') === '5', `the orphaned print of the library side does not read the constant 5: ${feeds.join(',')}`)
        continue
    }
    assert(signature(cpp) === signature(lib), `'${name}' is not a recorded difference and the two sides differ`)
}