// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/runtime/graph.h"

#include <cstdint>
#include <string>
#include <string_view>

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
 * The OPER arm of `nodeOperatorOf(Graph, ref)` answers from the operator record: it reads the
 * node's URI out of `GCOperBody` and names the four by it, so a conversion the front end hands over
 * as an ordinary operator is a conversion here too. What is left for the day the front end stops
 * building the four node kinds is the deletion of the `kind` arm of `nodeOperatorOf(GCNodeKind)`;
 * the eight files do not change again.
 *
 * **One face, one question.** Every caller reads a frozen graph (`GCGraph`): the four kinds and
 * their payloads are what the stored graph has, and the two readers at the bottom hand out a
 * pointer into the node's bytes, which only a graph that stores its payload in one piece can do. A
 * draft keeps its payload at an arbitrary offset and is read through its own accessors; nothing in
 * this directory asks these questions of one.
 *
 * **Still transitional, and deliberately small.** `NotOne` means "not one of the four", so a caller
 * that only wants a yes/no keeps reading as a predicate. `fillBodyOf` / `accsBodyOf` are the
 * payload side of the same question, and they ask the node's *kind*, not its operator: a
 * construction's slot list and a member read's key are the payload of the FILL and ACCS kinds, so a
 * call that names `:op/fill` -- whose parameters are its own operands -- has no slot list to read
 * here, and the reader that needs one reads the call's operands. When that payload moves into the
 * operator, these two readings change here and nowhere else; today they also answer nullptr for a
 * node of another kind, instead of reading those bytes as a fill body (`transpose/walk.cpp` used to
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

/// Which of the four operators a URI names, or `NotOne`. This is what a graph says about a node
/// that names an operator instead of wearing a kind: `:op/fill` is a construction, `:op/cast` a
/// conversion, `:op/copy` a copy, and a member read is written two ways because the two reads ask
/// the value different questions (a position, a name) -- `:op/access` and `:op/access_key` are both
/// a member read. Any other URI, including the empty one a node without a record answers, is
/// `NotOne`: an operator is one of the four by what it says it is, not by being an operator.
inline NodeOperator nodeOperatorOf(std::string_view uri) noexcept {
    if (uri == ":op/cast") {
        return NodeOperator::Cast;
    }
    if (uri == ":op/copy") {
        return NodeOperator::Copy;
    }
    if (uri == ":op/fill") {
        return NodeOperator::Fill;
    }
    if (uri == ":op/access" || uri == ":op/access_key") {
        return NodeOperator::Accs;
    }
    return NodeOperator::NotOne;
}

/// Which of the four operators node `ref` of `graph` is. A ref that is not a node is `NotOne`.
inline NodeOperator nodeOperatorOf(const runtime::GCGraph &graph, runtime::gc_node_ref_t ref) noexcept {
    if (ref >= graph.nodeCount()) {
        return NodeOperator::NotOne;
    }
    const auto *node = graph.node(ref);
    if (node == nullptr) {
        return NodeOperator::NotOne;
    }
    if (node->kind == runtime::GCNodeKind::Oper) {
        // An operator node is one of the four by the URI its record holds, and every other operator
        // -- the overwhelming majority -- is `NotOne`, which is what makes a reader that only wants
        // a yes/no keep working.
        if (!node->hasPayload()) {
            return NodeOperator::NotOne;
        }
        const auto *body = graph.nodeBodyAs<runtime::GCOperBody>(ref);
        return body == nullptr ? NodeOperator::NotOne : nodeOperatorOf(body->uri());
    }
    return nodeOperatorOf(node->kind);
}

/// Which case node `ref` is. A ref that is not a node is `Other`, the case a walk's `default:`
/// already answered.
inline NodeCase nodeCaseOf(const runtime::GCGraph &graph, runtime::gc_node_ref_t ref) noexcept {
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
/// The payload is asked of the *kind*, not of the operator: the slot list is the FILL kind's bytes.
/// A construction a call names (`:op/fill`) passes its fields as the call's own operands and has no
/// slot list here, so this answers nullptr for it rather than reading a `GCOperBody` as a fill
/// body.
///
/// A frozen graph's payload is the graph's own bytes, so the answer points into them (that is what
/// the readers copy from).
inline const runtime::GCFillBody *
fillBodyOf(const runtime::GCGraph &graph, runtime::gc_node_ref_t ref) noexcept {
    const runtime::GCNode *node = graph.node(ref);
    if (node == nullptr || node->kind != runtime::GCNodeKind::Fill || !node->hasPayload()) {
        return nullptr;
    }
    return graph.nodeBodyAs<runtime::GCFillBody>(ref);
}

/// The member read `ref` is, as its payload -- which member, by place or by name -- or nullptr when
/// `ref` is not one (or a node without a payload). The payload is the ACCS kind's, for the reason
/// `fillBodyOf` gives: a read a call names passes its position or key as an operand.
inline const runtime::GCAccsBody *
accsBodyOf(const runtime::GCGraph &graph, runtime::gc_node_ref_t ref) noexcept {
    const runtime::GCNode *node = graph.node(ref);
    if (node == nullptr || node->kind != runtime::GCNodeKind::Accs || !node->hasPayload()) {
        return nullptr;
    }
    return graph.nodeBodyAs<runtime::GCAccsBody>(ref);
}

} // namespace camel::transforms