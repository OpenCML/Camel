// The graphs of the divergence corpus, node by node and edge by edge, against the reference mode
// this tree runs: `std::opt::algebra neutral_only=true` (`src/passes/opt/generic/algebra.cpp`), the
// `by_cpp` snapshot beside this entry, whose `std::jgir` dump this reads. The run being verified is
// the library's. Every graph of the corpus is examined; `main` and `effect_mul_zero` are the same
// graph on the two sides (compared by the terms `compareGirDotIsomorphic` uses: a node is its kind,
// its label, its type and its in/out degree, an edge is its endpoints, its kind and its index), and
// the two anchored forms are held to the relation between the two graphs rather than to a size.
//
//   `anchor_add`, `anchor_mul32`  the graph's named node, and the one place the two sides answer
//       differently. The C++ pass keeps the add (the product at i32) as the graph's exit, its output
//       and its returned value, because `isReplaceable` refuses to rewrite those three nodes
//       (`src/passes/opt/generic/rewrite.cpp`), so its exit is the operation and that operation
//       reads the port and the constant. The rule engine rewrites the matched node and **moves the
//       anchor onto the replacement**: this is
//       (the engine moves the anchor onto the node the rule's right side wrote, as the C++
//       pass's `replaceFoldResult` does), and it is not a rule
//       condition,
//       so `DraftWritings::rewrite` does it in one place for every law. Here the replacement the
//       rule's right side wrote is the port itself (`x + 0 => x`), so the library's exit is the
//       port, the operation the reference kept and the constant it read are gone from the graph --
//       not merely unreached -- and what a comparison sees is the port alone. The rule engine
//       folding the graph's return expression is what the whole `rules` lane holds too
//       (`test/plans/feat/modules/rules.plan.toml`: `coefficients` and `chain` are folded at their
//       return node), so the two sides differ here by design, not by an unapplied check.
//
//   `effect_mul_zero`  was the other difference and is an agreement now. The C++ law table has no
//       `x * 0` law, so the product over the print stands there; the library's `int_mul_zero` is
//       still there too, and the engine refuses its match, because the part the right side drops is
//        the print and not a pure computation. The same rule
//        decrees the check
//       rather than withdrawing the rule: before a rule is applied, the subgraph the pattern
//       variables that its right-hand side does not use bound is examined, and an effect node -- or
//       a node carrying a sync or order edge -- there refuses the application. Design principles 9.7
//       forbids the rewrite the same way ("例：`x * 0 => 0` 不作用于 `println(5) * 0`"), the refusal
//       is `dropsOrdered`/`pureComputation` in src/runtime/writings.cpp, and the graph the library
//       used to lose is the graph both sides draw now. The value side of the same change is
//       `algebra_rules.divergence.library_runs`.

import fs from 'node:fs'
import { applyNormalizers } from '../../../tools/normalizers.mjs'

const NORMALIZERS = ['strip_ansi', 'normalize_newlines', 'trim_trailing_space', 'normalize_paths', 'normalize_origins']
const GOLDEN = 'algebra_rules.divergence.cpp.jgir'
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

// The ports of the graph, named by their label, so an edge into a port can be named by its input
// index; the anchored forms below read the port and the constant the operation was written with.
const portOf = (g, label) => g.nodes.filter((n) => n.kind === 'PORT' && n.label === label)

for (const name of GRAPHS) {
    const cpp = graphOf(reference, name)
    const lib = graphOf(library, name)
    const ports = portOf(lib, 'x')
    assert(ports.length <= 1, `'${name}' holds ${ports.length} ports named x`)
    if (name === 'anchor_add' || name === 'anchor_mul32') {
        // What the C++ pass's form is: it keeps the named operation, so the graph exits at the add
        // (the product at i32) and that exit reads the port and the constant the operation was
        // written with. `isReplaceable` is why: those three nodes -- exit, output and returned
        // value -- are not rewritten there. The assertion is the relation, not a size: the exit is
        // the operation, and the operation reads exactly the port and its constant.
        const operation = name === 'anchor_add' ? ':op/add_l' : ':op/mul_i'
        const constant = name === 'anchor_add' ? '0' : '1'
        const cppExit = cpp.nodes.find((n) => n.id === cpp.exit)
        assert(cppExit && cppExit.label === operation, `'${name}' on the C++ side does not exit at ${operation}: it exits at ${cppExit && cppExit.label}`)
        const inputs = cpp.edges.filter((e) => e.target === cpp.exit).map((e) => `${cpp.nodes.find((n) => n.id === e.source).label}@${e.targetIndex}`).sort()
        assert(inputs.join(',') === `${constant}@1,x@0`, `'${name}' on the C++ side does not read the port and the constant ${constant}: ${inputs.join(',')}`)
        // What the engine's form is: the rule fired on that same node and the anchor was moved onto
        // what its right side wrote, which for `x + 0 => x` is the port. The library's graph
        // therefore still holds the port and the exit is it, while the operation the C++ side kept
        // is gone from the graph -- not merely unreached. This is the rule's own sentence about the
        // rule, and the `rules` lane holds the same rewriting of a graph's return expression
        // (`coefficients` and `chain` fold there).
        const libExit = lib.nodes.find((n) => n.id === lib.exit)
        assert(libExit && libExit.kind === 'PORT' && libExit.label === 'x', `'${name}' on the library side does not exit at the port: ${libExit && libExit.kind} ${libExit && libExit.label}`)
        assert(ports.length === 1, `'${name}' on the library side holds ${ports.length} ports named x, not the one the exit is`)
        assert(!lib.nodes.some((n) => n.label === operation), `'${name}' on the library side still holds ${operation}, so the rule did not fire on the graph's named node`)
        assert(!lib.nodes.some((n) => n.kind === 'DATA' && n.label === constant), `'${name}' on the library side still holds the neutral operand ${constant} the rewrite replaced`)
        continue
    }
    // Every graph that is not one of the two anchored forms is the same graph on the two sides,
    // `effect_mul_zero` included: it was a difference until the engine refused the match that
    // dropped the print (design principles 9.7, the `rule-drops-effect` rule), so this comparison
    // is what keeps it an agreement, and a rule or an engine change that drops a live effect again
    // fails here. The assertions this corpus used to make about the library side -- its sole
    // arithmetic gone, two prints with one of them left without a user -- described the violation
    // and are gone with it.
    assert(signature(cpp) === signature(lib), `'${name}' is not one of the two anchored forms and the two sides differ`)
}