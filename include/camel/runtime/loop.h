/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The LOOP node as its consumers see it (docs/design/op-semantics, section 11; the node card in
 * docs/design/graph-model 3.5). A loop is an expression with a state (design principles 2.10,
 * 2.12, 2.13):
 *
 *     loop s1 = a1, ..., sk = ak (for x in xs)? (while c)? do b
 *
 * is one LOOP node in the graph it is written in, with
 *   Norm inputs   xs (For, ForWhile), then a1 ... ak;
 *   With inputs   the m values the loop captures, one table for both graphs;
 *   Ctrl inputs   ordering;
 *   payload       the form, the condition graph c (While, ForWhile) and the body graph b.
 * The condition graph takes s1 ... sk (Norm ports) and the m captures (With ports) and returns
 * a bool; the body graph takes (x,) s1 ... sk and the same m captures and returns the next
 * state: the value when k = 1, the k-tuple when k > 1. The LOOP node's value is the last state.
 *
 * Semantics: s = (a1, ..., ak); each round: [While, ForWhile] if !c(s) stop; [For, ForWhile]
 * if no element is left stop, else x = the next element; s = b([x,] s). The rounds run strictly
 * one after the other; each is one activation of the condition graph and one of the body
 * graph.
 *
 * Ownership. A LoopView is a view: it holds the graph and the node, and is valid as long as the
 * frozen graph is (camel/runtime/graph.h). Graphs are named by GCGraph pointers, nodes by their
 * NodeIds (gc_node_ref_t), types by Type pointers: the design's GraphId and TypeId spelled the
 * way this runtime spells them today (graph-model step 5 brings GraphId).
 *
 * Lowering. lowerLoopsToTailRecursion (camel/runtime/loop_lowering.h) rewrites every LOOP of a
 * program into the tail recursion it means. It is applied only by a back end, on a private copy
 * of the program it runs (op-semantics 11.4): a pass never commits a lowered loop to the shared
 * program, so a consumer such as Tiles sees structured loops wherever it is in the pipeline.
 */

#pragma once

#include "camel/core/loop_kind.h"
#include "camel/runtime/effects.h"
#include "camel/runtime/graph.h"

#include <optional>
#include <span>

namespace camel::runtime {

using camel::core::LoopKind;

class LoopView {
  public:
    /// The LOOP node `node` of `graph`; asserts that it is one.
    LoopView(const GCGraph &graph, gc_node_ref_t node);

    const GCGraph &graph() const { return *graph_; }
    gc_node_ref_t node() const { return node_; }

    LoopKind kind() const { return kind_; }
    /// k, the number of values of the state.
    size_t stateCount() const { return stateCount_; }
    /// The type of the j-th value of the state (the type of the body graph's j-th state port).
    camel::core::type::Type *stateType(size_t j) const;
    /// The type of the loop's value: the state type when k = 1, the k-tuple when k > 1.
    camel::core::type::Type *valueType() const { return graph_->node(node_)->dataType(); }

    // ---- Inputs of the LOOP node (in the graph it is written in) ----

    /// For, ForWhile: the array iterated over (Norm input 0).
    std::optional<gc_node_ref_t> iterable() const;
    /// The first value of the j-th value of the state (the Norm input after the iterable).
    gc_node_ref_t initialState(size_t j) const;
    /// The captured values (With inputs), shared by the condition and the body graph.
    std::span<const gc_node_ref_t> captures() const { return graph_->withInputsOf(node_); }

    // ---- The two graphs ----

    /// While, ForWhile: the condition graph; nullptr for For.
    GCGraph *condGraph() const { return cond_; }
    GCGraph *bodyGraph() const { return body_; }

    // ---- Correspondence between the node and the ports of its graphs ----

    /// The condition graph's j-th Norm port (the j-th value of the state).
    gc_node_ref_t condStateParam(size_t j) const;
    /// For, ForWhile: the body graph's first Norm port, the element of the round.
    std::optional<gc_node_ref_t> bodyElementParam() const;
    /// The body graph's Norm port of the j-th value of the state.
    gc_node_ref_t bodyStateParam(size_t j) const;
    /// The capture port of the condition graph (body graph) that receives captures()[c].
    gc_node_ref_t condCapturePort(size_t c) const;
    gc_node_ref_t bodyCapturePort(size_t c) const;

    // ---- The next state ----

    /// The body graph's result node: the next state (a k-tuple when k > 1).
    gc_node_ref_t nextState() const;
    /// k > 1 and the body builds its result with a tuple FILL: the body node that fills
    /// position j; otherwise nullopt (the consumer projects nextState() with ACCS).
    std::optional<gc_node_ref_t> nextStateComponent(size_t j) const;

    // ---- Effects ----

    EffectSummary condEffects() const { return effectSummaryOf(cond_); }
    EffectSummary bodyEffects() const { return effectSummaryOf(body_); }
    /// The LOOP node's own: the union of the two graphs', and "may fault" and "may not
    /// terminate" (a loop may not end; design of loops 4.8).
    EffectSummary effects() const;

  private:
    const GCGraph *graph_;
    gc_node_ref_t node_;
    LoopKind kind_;
    size_t stateCount_;
    GCGraph *cond_;
    GCGraph *body_;
};

/// The LOOP node `node` of `graph` (asserts that it is one).
inline LoopView loopOf(const GCGraph &graph, gc_node_ref_t node) { return LoopView(graph, node); }

/// Checks the LOOP node `node` of `graph` against its graphs and throws std::runtime_error with
/// what is wrong (docs/design/graph-model I13; op-semantics I18, I19): the condition graph has
/// the k state ports and returns bool; the body graph has (the element and) the k
/// state ports and returns the state; both have the same capture ports, as many as the node has
/// With inputs, of the same types; each first value can be assigned to its state; the element
/// port has the iterated array's element type. I20 holds by construction (EffectSummary is
/// computed from the nodes).
void validateRuntimeLoopInterface(const GCGraph *graph, gc_node_ref_t node);

} // namespace camel::runtime
