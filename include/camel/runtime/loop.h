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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The LOOP node as its consumers see it (docs/design/op-semantics, section 11; the node card in
 * docs/design/graph-model 3.5). A loop is an expression with a state (design principles 3.10):
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
 * value of each slot the loop collects (see below), after the k state values. Without collection
 * slots the body returns the next state alone: the value when k = 1, the k-tuple when k > 1.
 * The LOOP node's value is the last state.
 *
 * Collection slots. A graph producer (no source syntax exists for this yet; the H4d design)
 * may give a LOOP node s typed collection slots. Each round the body then also produces one
 * value per slot, and the loop collects them into one array per slot, in round order. The
 * node's value is (state_1, ..., state_k, array_1, ..., array_s): the state components first,
 * so a consumer that does not care about the tape can read the state where it used to. The
 * payload's collectionCount is s; the element type of slot c is the s-th trailing component
 * of the body graph's exit tuple; and the node's value type is the tuple that names both. The
 * count is explicit because the state type alone cannot tell e.g. k = 2 from k = 1 plus a
 * collection slot of the same type.
 *
 * Semantics: s = (a1, ..., ak); each round: [While, ForWhile] if !c(s) stop; [For, ForWhile]
 * if no element is left stop, else x = the next element; v = b([x,] s), which is the next state
 * (the k-tuple) followed by the s collected values; s = the first k of v; the collected values
 * are appended to their arrays. The rounds run strictly one after the other; each is one
 * activation of the condition graph and one of the body graph.
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
    /// s, the number of typed values the loop collects into arrays each round (0 unless a graph
    /// producer gave the node collection slots; the source syntax never does).
    size_t collectionCount() const { return collectionCount_; }
    /// True when the loop's value is a tuple: k > 1, or the loop collects (k + s components).
    bool valueIsTuple() const { return stateCount_ > 1 || collectionCount_ > 0; }
    /// The type of the loop's value: the state type when k = 1 and s = 0; otherwise the tuple
    /// (state_0, ..., state_{k-1}, array_0, ..., array_{s-1}).
    camel::core::type::Type *valueType() const { return graph_->node(node_)->dataType(); }
    /// k + s: the number of components of the loop's value when valueIsTuple().
    size_t valueComponentCount() const { return stateCount_ + collectionCount_; }
    /// The index of the j-th state value in the loop's value tuple.
    size_t stateValueIndex(size_t j) const { return j; }
    /// The index of the array collected by slot c in the loop's value tuple.
    size_t collectionValueIndex(size_t c) const { return stateCount_ + c; }
    /// Slot c's array type (an ArrayType) and the type of the values it holds.
    camel::core::type::Type *collectionArrayType(size_t c) const;
    camel::core::type::Type *collectionElemType(size_t c) const;

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

    // ---- The next state and the collected values ----

    /// The body graph's result node: the next state, followed by the s collected values when
    /// the loop collects (so a tuple whenever valueIsTuple(); otherwise the state itself when
    /// k = 1).
    gc_node_ref_t nextState() const;
    /// True when the body builds its result with a tuple FILL: k > 1 or the loop collects.
    bool nextStateIsTuple() const { return valueIsTuple(); }
    /// The body node that fills position j of the result tuple (the j-th state value); nullopt
    /// unless nextStateIsTuple() and the body result is a tuple FILL: a consumer may otherwise
    /// project nextState() with ACCS.
    std::optional<gc_node_ref_t> nextStateComponent(size_t j) const;
    /// The body node that produces the value collected by slot c this round (result position
    /// k + c); nullopt unless the loop collects and the body result is a tuple FILL.
    std::optional<gc_node_ref_t> nextCollectionComponent(size_t c) const;
    /// The type of the body graph's result: the state type when it does not collect, the
    /// (state_0, ..., state_{k-1}, elem_0, ..., elem_{s-1}) tuple when it does.
    camel::core::type::Type *bodyResultType() const { return body_->funcType()->exitType(); }
    /// The index of the j-th state value (the c-th collected value) in the body result tuple.
    size_t bodyStateResultIndex(size_t j) const { return j; }
    size_t bodyCollectionResultIndex(size_t c) const { return stateCount_ + c; }

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
    size_t collectionCount_;
    GCGraph *cond_;
    GCGraph *body_;
};

/// The LOOP node `node` of `graph` (asserts that it is one).
inline LoopView loopOf(const GCGraph &graph, gc_node_ref_t node) { return LoopView(graph, node); }

class GraphDraft;

/// The LOOP node `node` of a draft: what a graph producer (autodiff) reads while it rewrites the
/// graph a LOOP node is in, before it is encoded. The same facts as LoopView, from the same
/// layout: the draft holds the node's inputs and its value type, the node's payload holds the two
/// graphs (camel/runtime/graph.h, GCLoopBody), and the graphs' own ports are read as LoopView
/// reads them. One place, so a producer never re-derives the layout either.
class LoopDraftView {
  public:
    /// The LOOP node `node` of `draft`; asserts that it is one.
    LoopDraftView(const GraphDraft &draft, gc_node_ref_t node);

    gc_node_ref_t node() const { return node_; }

    LoopKind kind() const { return kind_; }
    size_t stateCount() const { return stateCount_; }
    size_t collectionCount() const { return collectionCount_; }
    bool valueIsTuple() const { return stateCount_ > 1 || collectionCount_ != 0; }
    /// The type of the loop's value in the draft.
    camel::core::type::Type *valueType() const;
    size_t valueComponentCount() const { return stateCount_ + collectionCount_; }
    size_t stateValueIndex(size_t j) const { return j; }
    size_t collectionValueIndex(size_t c) const { return stateCount_ + c; }
    camel::core::type::Type *stateType(size_t j) const;
    camel::core::type::Type *collectionArrayType(size_t c) const;
    camel::core::type::Type *collectionElemType(size_t c) const;

    std::optional<gc_node_ref_t> iterable() const;
    gc_node_ref_t initialState(size_t j) const;
    std::span<const gc_node_ref_t> captures() const;

    GCGraph *condGraph() const { return cond_; }
    GCGraph *bodyGraph() const { return body_; }

    gc_node_ref_t condStateParam(size_t j) const;
    std::optional<gc_node_ref_t> bodyElementParam() const;
    gc_node_ref_t bodyStateParam(size_t j) const;
    gc_node_ref_t condCapturePort(size_t c) const;
    gc_node_ref_t bodyCapturePort(size_t c) const;

    gc_node_ref_t nextState() const;
    /// The type of the body graph's result (the state, or the state and the collected values).
    camel::core::type::Type *bodyResultType() const;
    size_t bodyStateResultIndex(size_t j) const { return j; }
    size_t bodyCollectionResultIndex(size_t c) const { return stateCount_ + c; }

  private:
    const GraphDraft *draft_;
    gc_node_ref_t node_;
    LoopKind kind_;
    size_t stateCount_;
    size_t collectionCount_;
    GCGraph *cond_;
    GCGraph *body_;
};

/// Reads component `index` of a finished loop's value (the layout LoopView describes): the value
/// itself for a loop that does not wrap it, else the tuple's component. One place so consumers
/// such as autodiff never re-derive the layout.
slot_t loopValueComponent(const LoopView &loop, slot_t value, size_t index);
/// The j-th state value of a finished loop, and the array collected by slot c.
slot_t loopStateValue(const LoopView &loop, slot_t value, size_t j);
slot_t loopCollectedValue(const LoopView &loop, slot_t value, size_t c);

/// Checks the LOOP node `node` of `graph` against its graphs and throws std::runtime_error with
/// what is wrong (docs/design/graph-model I13; op-semantics I18, I19): the condition graph has
/// the k state ports and returns bool; the body graph has (the element and) the k
/// state ports and returns the state; both have the same capture ports, as many as the node has
/// With inputs, of the same types; each first value can be assigned to its state; the element
/// port has the iterated array's element type. I20 holds by construction (EffectSummary is
/// computed from the nodes).
void validateRuntimeLoopInterface(const GCGraph *graph, gc_node_ref_t node);

} // namespace camel::runtime
