// The graphs the C++ algebraic pass and the rule library do NOT answer the same way, named node by
// node and edge by edge. The golden is the C++ pass's `std::jgir`: the deleted full law table
// (`e8c290554:src/passes/opt/generic/algebra.cpp`), whose dump on this corpus is identical to the
// dump of the reference mode that remains (`std::opt::algebra neutral_only=true`, the `by_cpp`
// snapshot beside this entry), so the two differences below are the ones the full table had as
// well. The run being verified is the library's. Every graph of the corpus is examined, and each one
// is either the same graph on the two sides (compared by the terms `compareGirDotIsomorphic` uses: a
// node is its kind, its label, its type and its in/out degree, an edge is its endpoints, its kind
// and its index) or one of the two recorded differences below.
//
// The two differences are the findings, and they are not normalized away:
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
//   `effect_mul_zero`  was the third difference and is an agreement now. The C++ law table has no
//       `x * 0` law, so the product over the print stands there; the library's `int_mul_zero` is
//       still there too, and the engine refuses its match, because the part the right side drops is
//       the print and not a pure computation. The same ruling
//       (docs/handover/reviews/questions-2026-10-23.md, `rule-drops-effect`) decrees the check
//       rather than withdrawing the rule: before a rule is applied, the subgraph the pattern
//       variables that its right-hand side does not use bound is examined, and an effect node -- or
//       a node carrying a sync or order edge -- there refuses the application. Design principles 9.7
//       forbids the rewrite the same way ("例：`x * 0 => 0` 不作用于 `println(5) * 0`"), the refusal
//       is `dropsOrdered`/`pureComputation` in src/runtime/writings.cpp, and the graph the library
//       used to lose is the graph both sides draw now, so this corpus compares it with the rest
//       instead of recording it: a rule or an engine change that drops a live effect again fails
//       that comparison. The value side of the same change is
//       `algebra_rules.divergence.library_runs`.

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
    // Every graph that is not one of the two recorded differences must be the same graph on the
    // two sides, `effect_mul_zero` included: it was the third difference until the engine refused
    // the match that dropped the print (design principles 9.7, the `rule-drops-effect` ruling), so
    // this comparison is what keeps it an agreement, and a rule or an engine change that drops a
    // live effect again fails here. The assertions this corpus used to make about the library side
    // -- its sole arithmetic gone, two prints with one of them left without a user -- described the
    // violation and are gone with it.
    assert(signature(cpp) === signature(lib), `'${name}' is not a recorded difference and the two sides differ`)
}