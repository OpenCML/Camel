// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What a gate hands back, and what it waits on.
 *
 * A gate is `GATE(value, writes...)`: the **first** ordinary input is the value it forwards and
 * every input after it is a statement it waits on (author, `0780a83bd`: "a gate's trailing input is
 * a wait rather than a use ... that is the same reading of `front()` that resolving a forwarded
 * value reference, emitting the draft payload and asking a node for its result type already use").
 * Before that change the exit of a region that wrote something carried the writes among its inputs,
 * so a walk that took `normInputsOf(gate).back()` read **the last write** as the value -- and `split`
 * on a body that printed answered a print node where its result should be, or refused the gate for
 * having more than one input (`split.cpp`'s `GATE` case, author 2026-11-18).
 *
 * This is that one reading, so the transforms cannot disagree about it: a walk that wants the value
 * a body or a region answers calls `exitValueOf`, and one that wants the statements the gate keeps
 * alive calls `exitWaitsOf`. `exec_plan.cpp` reads the same two facts the same way when it plans a
 * gate, and `rule_expand.cpp` already walked `normInputsOf(...).front()` on its own -- this header
 * is where that walk belongs, because the reason is about gates and not about a rule.
 *
 * Both take the graph by whatever reader the caller has: a `const GCGraph &` (`split`, `transpose`,
 * `vmap`) or the `GraphRead` a draft hands out while it is being written (`jvp`). The three methods
 * they ask for -- `node`, `nodeCount`, `normInputsOf`, `withInputsOf` -- are the ones every reader
 * of a graph already has, so the walk is written once against them rather than twice against two
 * types.
 *
 * The value may sit on a `with` port instead, which is what a gate over a captured value looks
 * like; the walk takes `front()` there too, for the same reason.
 */

#pragma once

#include "camel/runtime/graph.h"

#include <span>

namespace camel::transforms {

/// The value a gate forwards: its first ordinary input, or its first context input when it has no
/// ordinary one. A ref that is not a gate answers itself. `kInvalidNodeRef` when a gate forwards
/// nothing.
template <class GraphReader>
inline camel::runtime::gc_node_ref_t exitValueOf(
    const GraphReader &graph, camel::runtime::gc_node_ref_t ref) {
    while (ref != camel::runtime::kInvalidNodeRef && ref < graph.nodeCount()) {
        const auto *node = graph.node(ref);
        if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Gate) {
            break;
        }
        const std::span<const camel::runtime::gc_node_ref_t> norm = graph.normInputsOf(ref);
        if (!norm.empty()) {
            ref = norm.front();
            continue;
        }
        const std::span<const camel::runtime::gc_node_ref_t> with = graph.withInputsOf(ref);
        ref = with.empty() ? camel::runtime::kInvalidNodeRef : with.front();
    }
    return ref < graph.nodeCount() ? ref : camel::runtime::kInvalidNodeRef;
}

/// The statements a gate waits on: every ordinary input after the value it forwards. Empty when
/// `ref` is not a gate, or a gate that waits on nothing.
template <class GraphReader>
inline std::span<const camel::runtime::gc_node_ref_t> exitWaitsOf(
    const GraphReader &graph, camel::runtime::gc_node_ref_t ref) {
    if (ref == camel::runtime::kInvalidNodeRef || ref >= graph.nodeCount()) {
        return {};
    }
    const auto *node = graph.node(ref);
    if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Gate) {
        return {};
    }
    const std::span<const camel::runtime::gc_node_ref_t> norm = graph.normInputsOf(ref);
    if (norm.size() <= 1) {
        return {};
    }
    return norm.subspan(1);
}

} // namespace camel::transforms