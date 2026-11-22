// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/runtime/graph.h"

#include <cstdint>

namespace camel::transforms {

/*
 * The four node kinds that become **ordinary operators** -- a conversion (CAST), a copy (COPY), a
 * construction (FILL) and a member read (ACCS) -- read as the operators they are becoming. The
 * refactor plan (docs/design/architecture/refactor-plan.cn.md, stage 2) turns these four from kinds
 * of node into operators declared in `lib/std` and built by the front end as OPER nodes.
 *
 * **Why one place.** A reader that has to know "is this node a construction" used to say
 * `node->kind == rt::GCNodeKind::Fill`, and this directory said it, in its own words, in eight
 * files: `jvp.cpp`, `module.cpp`, `rule_expand.cpp`, `split.cpp`, `split_loop_cut.cpp`,
 * `split_internal.h`, `transpose/walk.cpp` and `transpose/walkmachinery.cpp`. Once the front end
 * builds these four as OPER nodes, every one of those tests is *wrong*: not by a little, but by
 * asking a question the node no longer answers (a construction is then an OPER node whose operator
 * says it constructs). So the question is asked here instead, and the eight files ask it:
 *
 *     if (nodeOperatorOf(graph, ref) == NodeOperator::Fill) { ... }
 *
 * When the declarations and the front end's OPER nodes land, exactly one thing changes: the `kind`
 * arm of `nodeOperatorOf(GCNodeKind)` is deleted, and the OPER arm below answers from the operator
 * record (`GCOperBody::oper` through the table) instead of answering `NotOne`. The eight files do
 * not change again.
 *
 * **Two faces, one question.** `GraphRead` (`compile/gir/graph_read.h`) is the face the lift in
 * `jvp.cpp` reads; a `FrozenGraphRead` and a draft answer it. The overloads below are templates
 * over the face, because both a `GCGraph` and a `GraphRead` answer `nodeCount()` and
 * `node(ref)->kind` -- the one thing this file asks. What is *not* templated is the two payload
 * readers at the bottom: they hand out a pointer into the node's bytes, which a frozen graph can
 * promise and a draft cannot (a draft keeps its payload at an arbitrary offset, so `GraphRead`
 * copies a body out instead of reinterpreting it in place). A reader of a `GraphRead` keeps reading
 * its own payload, exactly as `jvp.cpp` does.
 *
 * **Still transitional, and deliberately small.** `NotOne` means "not one of the four", so a caller
 * that only wants a yes/no keeps reading as a predicate. `fillBodyOf` / `accsBodyOf` exist for the
 * same reason as the identity question: when a construction's slot list and a member read's key
 * move into the operator's instance bytes, those two readings change here and nowhere else -- and
 * until then they do something the direct `nodeBodyAs` call did not: they answer nullptr for a node
 * of another family, instead of reading those bytes as a fill body (`transpose/walk.cpp` used to
 * have to guard that by hand: "the kind is asked before the payload is read -- an OPER's bytes read
 * as a FILL are some other number").
 *
 * **What is deliberately not here.** `static_cast<int>(node->kind)` in the refusal readings stays
 * reading the kind: it is a reading of the graph as stored, quoted in a diagnostic, and changing it
 * would change what a program prints.
 */

/// Which of the four operators a node is, when it is one of them.
enum class NodeOperator : uint8_t {
    NotOne = 0,
    Cast,
    Copy,
    Fill,
    Accs,
};

/// The case a copy walk switches a node on: the four families by their operator, every other kind
/// by its kind. `Other` is what a walk's `default:` already covered (a `Dead` place, a draft's
/// `Dref` and the reserved `Bind` never reach a frozen graph, and a walk that meets one has no
/// reading for it).
enum class NodeCase : uint8_t {
    Data,
    Port,
    Brch,
    Join,
    Call,
    Func,
    Oper,
    Gate,
    Loop,
    Cast,
    Copy,
    Fill,
    Accs,
    Other,
};

/// The operator a kind stands for. **This is the arm stage 2.3 deletes**: after it, a construction is an
/// OPER and there is no kind that means "a cast".
inline NodeOperator nodeOperatorOf(runtime::GCNodeKind kind) noexcept {
    switch (kind) {
    case runtime::GCNodeKind::Cast:
        return NodeOperator::Cast;
    case runtime::GCNodeKind::Copy:
        return NodeOperator::Copy;
    case runtime::GCNodeKind::Fill:
        return NodeOperator::Fill;
    case runtime::GCNodeKind::Accs:
        return NodeOperator::Accs;
    default:
        return NodeOperator::NotOne;
    }
}

/// The same question in the form a copy walk's `switch` needs.
inline NodeCase nodeCaseOf(runtime::GCNodeKind kind) noexcept {
    switch (kind) {
    case runtime::GCNodeKind::Data:
        return NodeCase::Data;
    case runtime::GCNodeKind::Port:
        return NodeCase::Port;
    case runtime::GCNodeKind::Brch:
        return NodeCase::Brch;
    case runtime::GCNodeKind::Join:
        return NodeCase::Join;
    case runtime::GCNodeKind::Call:
        return NodeCase::Call;
    case runtime::GCNodeKind::Func:
        return NodeCase::Func;
    case runtime::GCNodeKind::Oper:
        return NodeCase::Oper;
    case runtime::GCNodeKind::Gate:
        return NodeCase::Gate;
    case runtime::GCNodeKind::Loop:
        return NodeCase::Loop;
    case runtime::GCNodeKind::Cast:
        return NodeCase::Cast;
    case runtime::GCNodeKind::Copy:
        return NodeCase::Copy;
    case runtime::GCNodeKind::Fill:
        return NodeCase::Fill;
    case runtime::GCNodeKind::Accs:
        return NodeCase::Accs;
    default:
        return NodeCase::Other;
    }
}

/// Which of the four operators node `ref` of `graph` is. A ref that is not a node is `NotOne`.
///
/// `Graph` is a frozen graph or a `GraphRead`: both answer `nodeCount()` and the record
/// `node(ref)` hands out, and the record's `kind` is the same field in both (see the file comment).
template <typename Graph>
inline NodeOperator nodeOperatorOf(const Graph &graph, runtime::gc_node_ref_t ref) noexcept {
    if (ref >= graph.nodeCount()) {
        return NodeOperator::NotOne;
    }
    const auto *node = graph.node(ref);
    if (node == nullptr) {
        return NodeOperator::NotOne;
    }
    if (node->kind == runtime::GCNodeKind::Oper) {
        // stage 2.3's first step -- the four declared in `lib/std` -- and its second -- the front end
        // building OPER -- have not landed here yet, so an OPER is one of today's operators and
        // not one of the four. When they land this arm reads the operator off the node's
        // `GCOperBody` (`oper`, or its URI through the table) and answers which of the four it is;
        // until then it must answer `NotOne`, because answering otherwise would claim a node is a
        // construction that nobody built.
        return NodeOperator::NotOne;
    }
    return nodeOperatorOf(node->kind);
}

/// Which case node `ref` is. A ref that is not a node is `Other`, the case a walk's `default:`
/// already answered.
template <typename Graph>
inline NodeCase nodeCaseOf(const Graph &graph, runtime::gc_node_ref_t ref) noexcept {
    switch (nodeOperatorOf(graph, ref)) {
    case NodeOperator::Cast:
        return NodeCase::Cast;
    case NodeOperator::Copy:
        return NodeCase::Copy;
    case NodeOperator::Fill:
        return NodeCase::Fill;
    case NodeOperator::Accs:
        return NodeCase::Accs;
    case NodeOperator::NotOne:
        break;
    }
    if (ref >= graph.nodeCount()) {
        return NodeCase::Other;
    }
    const auto *node = graph.node(ref);
    return node == nullptr ? NodeCase::Other : nodeCaseOf(node->kind);
}

/// The construction `ref` is, as its payload -- the fill kind and the slot list that follows the
/// header in the node's bytes -- or nullptr when `ref` is not one (or a node without a payload).
///
/// A frozen graph's payload is the graph's own bytes, so the answer points into them (that is what
/// the readers copy from, and why this takes a `GCGraph` and not a `GraphRead`).
inline const runtime::GCFillBody *
fillBodyOf(const runtime::GCGraph &graph, runtime::gc_node_ref_t ref) noexcept {
    if (nodeOperatorOf(graph, ref) != NodeOperator::Fill) {
        return nullptr;
    }
    const runtime::GCNode *node = graph.node(ref);
    if (node == nullptr || !node->hasPayload()) {
        return nullptr;
    }
    return graph.nodeBodyAs<runtime::GCFillBody>(ref);
}

/// The member read `ref` is, as its payload -- which member, by place or by name -- or nullptr when
/// `ref` is not one (or a node without a payload).
inline const runtime::GCAccsBody *
accsBodyOf(const runtime::GCGraph &graph, runtime::gc_node_ref_t ref) noexcept {
    if (nodeOperatorOf(graph, ref) != NodeOperator::Accs) {
        return nullptr;
    }
    const runtime::GCNode *node = graph.node(ref);
    if (node == nullptr || !node->hasPayload()) {
        return nullptr;
    }
    return graph.nodeBodyAs<runtime::GCAccsBody>(ref);
}

} // namespace camel::transforms