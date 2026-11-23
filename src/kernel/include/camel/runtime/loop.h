// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The LOOP node as its consumers see it (docs/design/op-semantics, section 11; the node card in
 * docs/design/graph-model 3.5). A loop is an expression with a state (design principles 3.10):
 *
 *     loop s1 = a1, ..., sk = ak (for x in xs)? (while c)? do b
 *
 * is one LOOP node in the graph it is written in, with
 *   Norm inputs   xs (For, ForWhile), then a1 ... ak;
 *   With inputs   the results of its regions (the body's, the condition's) and the round port;
 *   Ctrl inputs   ordering;
 *   payload       the form, its collection count, and the regions (LoopCond, LoopBody).
 * The condition region takes s1 ... sk (its ports) and gives a bool; the body region takes
 * (x,) s1 ... sk and gives the next value of each slot the loop collects (see below), after the
 * k state values. Without collection slots the body gives the next state alone: the value when
 * k = 1, the k-tuple when k > 1. The LOOP node's value is the last state.
 *
 * Collection slots. A graph producer (no source syntax exists for this yet; the design)
 * may give a LOOP node s typed collection slots. Each round the body then also produces one
 * value per slot, and the loop collects them into one array per slot, in round order. The
 * node's value is (state_1, ..., state_k, array_1, ..., array_s): the state components first,
 * so a consumer that does not care about the tape can read the state at its usual position. The
 * payload's collectionCount is s; the element type of slot c is the s-th trailing component
 * of the body region's result tuple; and the node's value type is the tuple that names both. The
 * count is explicit because the state type alone cannot tell e.g. k = 2 from k = 1 plus a
 * collection slot of the same type.
 *
 * Semantics: s = (a1, ..., ak); each round: [While, ForWhile] if !c(s) stop; [For, ForWhile]
 * if no element is left stop, else x = the next element; v = b([x,] s), which is the next state
 * (the k-tuple) followed by the s collected values; s = the first k of v; the collected values
 * are appended to their arrays. The rounds run strictly one after the other; each is one
 * activation of the condition region and one of the body region, in the frame of the graph.
 *
 * Ownership. A LoopView is a view of one graph in either form: a frozen graph or the draft a
 * builder holds. It holds the read-only face of that graph (`camel::graph::GraphView`) and the
 * node, and is valid as long as the graph it was made in is -- the view keeps the adapter it
 * built for the graph, not the graph itself (camel/runtime/graph.h, draft.h). The condition and
 * the body are regions of that graph; nodes are named by their ids (gc_node_ref_t), types by Type
 * pointers.
 */

#pragma once

#include "camel/core/loop_kind.h"
#include "camel/runtime/effects.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"
#include "graph/graph_view.h"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace camel::runtime {

using camel::core::LoopKind;

class GraphDraft;

class LoopView {
  public:
    /// The LOOP node `node` of a frozen graph, and of the draft a builder holds; asserts that the
    /// node is one. Each builds the adapter the view reads (`FrozenGraphAdapter`,
    /// `DraftGraphAdapter`) and keeps it: the answers below are spans and ids of that graph, so the
    /// adapter has to outlive the view.
    LoopView(const GCGraph &graph, gc_node_ref_t node);
    LoopView(const GraphDraft &draft, gc_node_ref_t node);
    /// The node of a graph already read through a view: the caller keeps the adapter alive.
    explicit LoopView(const camel::graph::GraphView &view, gc_node_ref_t node);

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
    camel::core::type::Type *valueType() const;
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

    // ---- The two regions ----

    /// While, ForWhile: the condition region; 0 for For.
    uint32_t condRegion() const { return condRegion_; }
    uint32_t bodyRegion() const { return bodyRegion_; }

    // ---- What the regions hold and read ----

    /// The Port node, of type int64, whose slot a back end counts the rounds of the loop in (the
    /// last With input of the node).
    gc_node_ref_t roundPort() const { return view_.inputs(node_, camel::graph::PortGroup::Context).back(); }
    /// The nodes of the condition and of the body region, the regions inside them included, and
    /// the round port, in node order.
    std::vector<gc_node_ref_t> regionNodes() const;
    /// The nodes outside the two regions that nodes inside them read, in the order they are first
    /// read: besides the round's ports (the element and the state), what a round uses and what a
    /// consumer that lifts the loop into another representation has to pass in.
    ///
    /// A constant is not a capture. A node whose value is known at build time -- a `DATA` of the
    /// static area (a literal, a folded literal, a static value the builder materialized) or any
    /// node the graph marks `constant` -- is evaluated where it is read, in place, so it is left
    /// out unless `constants` asks for it. A value that is merely loop-invariant (a parameter, a
    /// module value read through a capture port, a variable's cell) is not constant and stays an
    /// input (design principles 3.10).
    std::vector<gc_node_ref_t> externalInputs(bool constants = false) const;

    // ---- Correspondence between the node and the ports of its regions ----

    /// The condition region's j-th port (the j-th value of the state).
    gc_node_ref_t condStateParam(size_t j) const;
    /// For, ForWhile: the body region's first port, the element of the round.
    std::optional<gc_node_ref_t> bodyElementParam() const;
    /// The body region's port of the j-th value of the state.
    gc_node_ref_t bodyStateParam(size_t j) const;

    // ---- The next state and the collected values ----

    /// The body's result node: the next state, followed by the s collected values when the loop
    /// collects (so a tuple whenever valueIsTuple(); otherwise the state itself when k = 1). It
    /// is the value the result of the body region forwards.
    gc_node_ref_t nextState() const;
    /// The node the body (condition) ends in: nextState(), or a GATE in front of it that orders
    /// the value after the statements of the body.
    gc_node_ref_t bodyResult() const;
    gc_node_ref_t condResult() const;
    /// True when the body builds its result with a tuple FILL: k > 1 or the loop collects.
    bool nextStateIsTuple() const { return valueIsTuple(); }
    /// The type of the body graph's result: the state type when it does not collect, the
    /// (state_0, ..., state_{k-1}, elem_0, ..., elem_{s-1}) tuple when it does.
    camel::core::type::Type *bodyResultType() const;
    /// The index of the j-th state value (the c-th collected value) in the body result tuple.
    size_t bodyStateResultIndex(size_t j) const { return j; }
    size_t bodyCollectionResultIndex(size_t c) const { return stateCount_ + c; }

  private:
    /// The facts every constructor takes from the graph: that the node is a LOOP, the form it
    /// declares, the two parts it owns, and how many values of state it has.
    void adopt();

    // Declared before the view it holds: the adapter is what the view reads, so it is built first
    // and destroyed last (`view_` binds into it).
    std::unique_ptr<camel::graph::GraphView> owned_;
    const camel::graph::GraphView &view_;
    gc_node_ref_t node_;
    LoopKind kind_;
    size_t stateCount_;
    size_t collectionCount_;
    uint32_t condRegion_ = 0;
    uint32_t bodyRegion_ = 0;
};

/// The LOOP node `node` of a frozen graph, and of the draft a builder holds (asserts that it is
/// one).
inline LoopView loopOf(const GCGraph &graph, gc_node_ref_t node) { return LoopView(graph, node); }
inline LoopView loopOf(const GraphDraft &draft, gc_node_ref_t node) { return LoopView(draft, node); }

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
void validateRuntimeLoopInterface(const Program &program, const GCGraph *graph, gc_node_ref_t node);

/// Gives up the in-place write of a loop body's array state: every `:struct/append_arr_owned`
/// in `body` (the nodes of its region `region`, when the body is one) (the form Builder::markLoopStateInPlace leaves, design principles 10.5) becomes the
/// copying `:struct/append_arr`, and the count is returned.
///
/// A party that needs the loop's tape (its collection slots) calls this first: an in-place loop
/// appends into one array, so every tape entry would be the same final array. This is a
/// fallback, never a refusal — the program is legal in both forms — and it changes neither the
/// loop's value nor its derivatives, only the allocations per round.
size_t fallBackToCopyingAppend(GraphDraft &body, uint32_t region = 0);

} // namespace camel::runtime
