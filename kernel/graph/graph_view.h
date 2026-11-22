// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * GraphView: the one read-only face of a graph (core abstractions section 2: "a frozen graph and
 * the graph being edited are the same read-only view, and every predicate is written once").
 *
 * A frozen graph (`GCGraph`, camel/runtime/graph.h) and the graph a session is editing
 * (`GraphDraft`, camel/runtime/draft.h) each answer this face through an adapter --
 * `FrozenGraphAdapter` (gc_graph_view.h) and `DraftGraphAdapter` (draft_view.h) -- so a reader asks
 * the graph one API instead of learning two. Stage 3 replaces the two representations with one and
 * deletes the adapters and this class together with them.
 *
 * What it is not: a second model of a graph, a place to decide or cache anything, or a way to make
 * a graph say something it does not store. Everything derivable -- liveness, may-fault, a node's
 * position in a chain, a branch's join, an aggregate's arity -- is a fact derived from this face
 * and cached by the fact layer (section 4), never a member here. The adapter computes such an
 * answer at the call, or hands back the cache it already keeps; it does not learn a new one.
 *
 * The vocabulary is nine kinds, three kinds of edge and "which region a node is in" (section 1).
 * Four readings differ from today's graph, and every reader still using the old reading is what the
 * first migration deletes:
 *  - CAST, COPY, FILL and ACCS are not kinds: they are operators, declared in `lib/std` like any
 *    other (plan 2.3). Until 2.3 declares those operators an adapter **refuses** to answer
 *    `operatorOf` for one of them rather than answering `kNoOper`: a reader handed `kNoOper` would
 *    take the "this operator is not known" path and the semantics would change silently, where a
 *    refusal stops in the one place that has to be finished. This is the single place the
 *    transition is visible, on purpose -- a tenth kind for the gap would put it in every reader.
 *  - JOIN does not appear: a BRCH owns one region per arm and its value is the selected arm's
 *    result, so the two paired payloads (`GCBrchBody::join`, `GCJoinBody::brch`) and the arm count
 *    each of them repeats are gone.
 *  - DREF and DEAD do not appear: a dead place is a tombstone of the numbering, not a node, and a
 *    draft's placeholder is the front end's -- the encoder drops it before a graph exists.
 *  - a control input is not an edge. What it said -- "this node belongs to that arm", "this one
 *    runs after that one" -- is said by the region a node belongs to, plus the sync edge. A GATE's
 *    value is its single data input and what it waits for is a sync edge, so nothing counts "the
 *    first input is the value" any more.
 *
 * Node numbers: a NodeRef is stable on one version line (section 2), and the adapters answer
 * today's numbers unchanged, so a reader moved onto this face prints the ids it printed before.
 * Gaps are expected, because the hidden kinds leave holes: nothing may index an array by a NodeRef,
 * and the number a *program* sees is the canonical one, not this.
 */

#pragma once

#include "camel/core/loop_kind.h"
#include "camel/core/operator_table.h"
#include "camel/core/slot.h"
#include "camel/core/type/base.h"
#include "camel/runtime/graph_id.h"

#include <cstdint>
#include <span>
#include <vector>

namespace camel::core::type {

// Declared, not included: this face names a type and hands the pointer the graph stores, and it
// must not make a reader compile the type system to read a node's kind.
class FunctionType;
class TupleType;

} // namespace camel::core::type

namespace camel::graph {

/// A node of the graph, as the face numbers it. The same integer `gc_node_ref_t` is today; spelled
/// here so that a reader of the view does not include a representation's header to name a node.
/// Replaces `gc_node_ref_t` for readers of the view; `camel/runtime/graph.h` keeps the name for the
/// two adapters and for the backends, which read the frozen payload directly and are not moved.
using NodeRef = std::uint32_t;

/// "no node": the answer of a query that names none (an absent exit, an absent owner).
inline constexpr NodeRef kNoNode = static_cast<NodeRef>(-1);

/// A region of the graph. Region 0 is the function's own body.
using RegionId = std::uint32_t;

/// "no region": a LOOP with no condition, a node that owns nothing.
inline constexpr RegionId kNoRegion = static_cast<RegionId>(-1);

/**
 * The nine kinds of node (section 1; graph model 3.4.1). Replaces `GCNodeKind` for readers of the
 * view: every `switch` over the sixteen today kinds collapses onto these, and the four kinds named
 * in the margin are what it collapses *from*, by the rule in the file comment.
 *
 *   Data, Port                 unchanged: a value, and a port of the graph's own body.
 *   Oper       <- CAST, COPY, FILL, ACCS, OPER
 *   Func, Call, Bind           unchanged (Bind stays reserved: nothing creates one).
 *   Brch       <- BRCH and JOIN
 *   Loop                       unchanged.
 *   Gate                       unchanged, but its value is its only data input.
 *
 * Who migrates: every reader of a node's kind -- `compile/gir/graph_read.h` (`NodeFace::kind` and
 * its two subclasses), `lib/test/graph_faces.cpp`, the ~37 `switch`es section 1 counts, and the two
 * backends once they read the view instead of the payload.
 */
enum class NodeKind : std::uint8_t {
    Data = 0,
    Port = 1,
    Oper = 2,
    Func = 3,
    Call = 4,
    Bind = 5,
    Brch = 6,
    Loop = 7,
    Gate = 8,
};

/// The three kinds of edge (section 1; design principles 3.1). Replaces the split between a node's
/// input run and the two side arrays of the frozen graph (`GCGraphOrderEdge`, `GCGraphSyncEdge`)
/// together with the `DraftEdgeKind`/`DraftEdgeSlot` pair a draft keeps: an edge is one notion
/// asked one way, and which array it lives in is the adapter's problem. Who migrates:
/// `orderPredsOf`, `syncPredsOf`, `orderEdges`, `syncEdges`, `waitInputsOf`, and the draft's five
/// user lists.
enum class EdgeKind : std::uint8_t {
    /// A value consumed by a node -- the same edges `inputs` answers, without the groups.
    Data = 0,
    /// The statement written before this one in one `sync` block. Carries "used".
    Sync = 1,
    /// Orders the two nodes where both run, and carries no "used".
    Order = 2,
};

/// The role of a data port (section 1: a port carries a role; there is one data edge kind).
/// Replaces `PortRole` unchanged, and the three runs `inBegin`/`withBegin`/`ctrlBegin` cut a node's
/// input table into: `with` is a role here, and the third run is not a port at all.
/// Who migrates: `normInputsOf`/`withInputsOf`/`ctrlInputsOf` and every `portRoleOf`.
enum class PortGroup : std::uint8_t {
    /// The ordinary argument of the callee's signature.
    Normal = 0,
    /// The context (`with`) argument.
    Context = 1,
};

/// What a region is, as far as a reader of the view cares. Replaces `GCRegionKind`: the three kinds
/// a structural node *owns* are answered by `regionsOwnedBy`, and `Inline`, `Block` and `Dead` do
/// not appear -- an inlined body is an ordinary region of this graph, a block is a tag set and not
/// a place, and a removed region is a tombstone. Who migrates: `ControlRegions`,
/// `NodeRoleRegistry`, `decidingRegionOf`, `loopRegionOf`, `armRegionOf` and `armCountOf`.
enum class RegionKind : std::uint8_t {
    /// The function's own body (region 0).
    Root = 0,
    /// One arm of a BRCH; `RegionRecord::index` is which arm.
    Arm = 1,
    /// The condition of a LOOP.
    LoopCond = 2,
    /// The body of a LOOP.
    LoopBody = 3,
};

/**
 * One region, as the graph stores it: what it is, who owns it and what it yields.
 *
 * Replaces `GCRegion` minus the two fields that are answers to other questions -- its form ports
 * (a separate query, `regionParameters`) and its tag set (a fact, not a place). A value, not an
 * interface, because every field here is stored once in the representation and copying five words
 * cannot disagree with itself the way a lazily built flyweight can.
 *
 * A region's ports are its parameters: the state of a loop, the values an arm is given. An arm that
 * only calls another graph is a region holding one FUNC node -- there is no second "subgraph form"
 * of an arm to handle. Who migrates: everything that walks `regionCount()`/`region()` today, and
 * `ControlRegions` first, because it is the one the other three build on.
 */
struct RegionRecord {
    RegionKind kind = RegionKind::Root;
    /// The BRCH or LOOP node that owns this region; `kNoNode` for the body.
    NodeRef owner = kNoNode;
    /// Which arm, for `RegionKind::Arm`; 0 otherwise.
    std::uint32_t index = 0;
    /// The region this one sits in; `kNoRegion` for the body, which is in nothing.
    RegionId parent = kNoRegion;
    /// The node whose value the region yields, `kNoNode` when it yields none. Replaces
    /// `GCRegion::result`; it is also the whole of "which arm the BRCH hands on".
    NodeRef result = kNoNode;
};

/// What a LOOP is, as values: its form, where its elements come from, and how many values it
/// collects into arrays. Replaces `GCLoopRef` (the frozen payload), `GCLoopBody`, the three
/// `loopKindOf`/`loopIterableFormOf`/`loopCollectionCountOf` accessors and `loopBodyOf`, which are
/// four spellings of one record. Who migrates: `camel/runtime/loop.h` (`LoopView`),
/// `LoopDraftView`, and the loop readers in `lib/transforms` and `modules`.
struct LoopForm {
    camel::core::LoopKind kind                 = camel::core::LoopKind::While;
    camel::core::LoopIterableForm iterableForm = camel::core::LoopIterableForm::Array;
    /// The values the loop collects into arrays each round; 0 for a loop the source wrote.
    std::uint32_t collectionCount = 0;
};

/**
 * The graph a reader holds, whatever it is made of. Replaces, together, `GCGraph` and `GraphDraft`
 * as *read* by anything but an adapter, and `compile/gir/graph_read.h`'s `GraphRead` with both of
 * its subclasses -- that face was the first half of this one and stops at the facts the lift asks.
 *
 * Every method answers what the graph stores. None of them decides anything: a reader that finds
 * `mayFault` or `isLive` here is looking in the wrong layer (section 4).
 *
 * The parameters are a virtual interface on purpose. The first readers moved onto it -- the lift,
 * the encoder's self-check, `LoopView`, `decidingRegionOf`, `ControlRegions`, `NodeRoleRegistry` --
 * are not inner loops, and the two representations are what stage 3 deletes: a seam that costs a
 * virtual call for one stage is worth less than a template that would put both representations'
 * types into every reader this stage exists to shrink. When one representation is left, the virtual
 * dispatch goes with the adapters and the predicates stay.
 */
class GraphView {
  public:
    virtual ~GraphView() = default;

    // -- the graph's own facts ---------------------------------------------------------------
    /// The graph's type: its parameters and what it answers. Replaces `GCGraph::funcType`.
    virtual camel::core::type::FunctionType *funcType() const = 0;
    /// The graph's own ports, by group: the parameters of its body. Replaces `normPorts` and
    /// `withPorts`; the values the body captured past them are `captures()`, because nothing at a
    /// call site names those and they are bound from the closure tuple.
    virtual std::vector<NodeRef> ports(PortGroup group) const = 0;
    /// The values the body captured, in the order it captured them. Replaces `closureNodes`.
    virtual std::vector<NodeRef> captures() const = 0;
    /// The tuple those captures are read out of at a call, nullptr when the graph captures nothing.
    virtual const camel::core::type::TupleType *captureType() const = 0;
    /// The constants the graph was given. Replaces `staticSlots`/`staticSlotsOf`.
    virtual std::span<const ::slot_t> staticArea() const = 0;
    /// The node the graph's value comes from, `kNoNode` when it has none. Replaces
    /// `exitNodeRef`/`exitRefOf`; a frozen graph stores it, a draft names the node it holds, and
    /// which of the two is a field is the adapter's business and not a reader's.
    virtual NodeRef exit() const = 0;

    // -- nodes -------------------------------------------------------------------------------
    /// How many node places the numbering has: NodeRefs are `0 .. nodeSlotCount() - 1`, gaps
    /// included. Replaces `nodeCount()` (frozen) and `nodeSlotCount()` (draft), which are the same
    /// number under two names.
    virtual std::uint32_t nodeSlotCount() const = 0;
    /// True when `ref` is a node of this view. Replaces `alive()` plus `containsNodeRef()`/
    /// `containsNode()`; a tombstone and a placeholder are both false here.
    virtual bool contains(NodeRef ref) const = 0;

    virtual NodeKind kindOf(NodeRef ref) const = 0;
    /// The type of the node's value, nullptr when it has none. Types are interned, so both adapters
    /// answer the same pointer for the same type.
    virtual const camel::core::type::Type *typeOf(NodeRef ref) const = 0;
    /// The slot the node's value lives in; negative for a constant of the static area.
    virtual std::int32_t dataIndexOf(NodeRef ref) const = 0;
    /// The two node flags that exist. Replaces `GCNode::hasFlag` and `NodeFace::flags`, which hand
    /// a reader a bit mask it has to know the meaning of.
    virtual bool isMacro(NodeRef ref) const    = 0;
    virtual bool isConstant(NodeRef ref) const = 0;
    /// Where the node was written, 0 when unknown. Compile and debug metadata, not content.
    virtual std::uint64_t originOf(NodeRef ref) const = 0;

    // -- ports, by group ---------------------------------------------------------------------
    /// The values the node takes in `group`, in port order. Replaces `normInputsOf`/`withInputsOf`.
    virtual std::span<const NodeRef> inputs(NodeRef ref, PortGroup group) const = 0;
    /// The nodes that read the node's value in `group`, in the order the adapter stores them.
    /// Replaces `normUsersOf`/`withUsersOf`/`normOutputsOf`/`withOutputsOf`, four names of one
    /// answer, and the draft's `normUsersOf`/`withUsersOf` beside them.
    virtual std::span<const NodeRef> users(NodeRef ref, PortGroup group) const = 0;

    // -- the edges that are not ports --------------------------------------------------------
    /// The predecessors of `ref` along one kind of edge, in the order the adapter stores them.
    ///
    /// One query for the three kinds, because section 1 makes an edge one notion: a liveness walk
    /// that follows a data port and a sync edge asks the same question twice rather than calling
    /// two APIs. `Sync` replaces `syncPredsOf`/`syncEdges` and the draft's `syncInputsOf`, and it
    /// is also what a node waits for -- the control inputs are gone (file comment), so nothing has
    /// to union two kinds. `Order` replaces `orderPredsOf`/`orderEdges`, the one kind no reader of
    /// the old face could ask for at all.
    ///
    /// A vector, where a port list is a span: the frozen graph stores a sync edge as a (user, pred)
    /// pair, so the predecessor list is a projection of it and cannot be handed out where it lies.
    virtual std::vector<NodeRef> predecessors(NodeRef ref, EdgeKind kind) const = 0;

    // -- regions -----------------------------------------------------------------------------
    /// How many regions the graph has, the body included. Replaces `regionCount`/`regionCountOf`.
    virtual std::uint32_t regionCount() const = 0;
    /// Region `id` as a value; a default record when the graph has no such region. Replaces
    /// `region()` and the `GCRegion` fields a reader picks out of it.
    virtual RegionRecord region(RegionId id) const = 0;
    /// The region's parameters, in port order: the values it is entered with (a loop's state, an
    /// arm's arguments). Replaces `GCGraph::regionPorts` and `GCRegion::portBegin/portCount`.
    virtual std::span<const NodeRef> regionParameters(RegionId id) const = 0;
    /// The nodes directly in the region, in node order. A vector rather than a span for the reason
    /// `GraphRead` returns vectors: a graph stores the region *of* a node, not the nodes of a
    /// region, so an adapter answering a span would keep a list it does not store.
    virtual std::vector<NodeRef> regionNodes(RegionId id) const = 0;
    /// The innermost region a node is in. Replaces `nodeRegion`/`regionOfNode`; a node that is not
    /// in the graph has no region, and is not answered as being in the body.
    virtual RegionId regionOf(NodeRef ref) const = 0;
    /// True when `ancestor` is `region` itself or a region it sits in. Replaces `isAncestorOf`,
    /// which every reader of `nodeRegion` re-implements.
    virtual bool isAncestorOf(RegionId ancestor, RegionId region) const = 0;
    /// The regions a structural node owns, in order: a BRCH's arms, a LOOP's condition then body.
    /// Empty for every other kind. Replaces `loopRegionOf(ref, part)` and the arm/join pairing, and
    /// `armCountOf` is this list's size -- a count is a query, not a second stored field.
    virtual std::vector<RegionId> regionsOwnedBy(NodeRef ref) const = 0;

    // -- the payloads that are typed, not bytes ----------------------------------------------
    /// The graph a FUNC node calls, `kInvalidGraphId` for another kind or one that names no callee.
    /// Replaces `calleeIdOf` and the `GCFuncRef` payload a reader decodes to reach it.
    virtual camel::runtime::GraphId calleeOf(NodeRef ref) const = 0;
    /// The operator an OPER node names, `kNoOper` for another kind. Replaces `operUriOf` and the
    /// `GCOperBody` payload; the URI, the traits and the implementation are read from
    /// `camel/core/operator_table.h`, where they are stored once per operator.
    virtual camel::core::OperId operatorOf(NodeRef ref) const = 0;
    /// The form of a LOOP node; a default `LoopForm` for another kind. Replaces three accessors and
    /// one free function (`loopBodyOf`).
    virtual LoopForm loopFormOf(NodeRef ref) const = 0;

    /// The nodes the view answers, in NodeRef order. Defined here rather than in an adapter: it is
    /// one loop over `contains`, and writing it once is the whole point of the face.
    class NodeRange {
      public:
        class Iterator {
          public:
            Iterator(const GraphView *view, NodeRef ref) : view_(view), ref_(ref) { skip(); }
            NodeRef operator*() const { return ref_; }
            Iterator &operator++() {
                ++ref_;
                skip();
                return *this;
            }
            bool operator!=(const Iterator &other) const { return ref_ != other.ref_; }
            bool operator==(const Iterator &other) const { return ref_ == other.ref_; }

          private:
            void skip() {
                while (view_ != nullptr && ref_ < view_->nodeSlotCount() &&
                       !view_->contains(ref_)) {
                    ++ref_;
                }
            }
            const GraphView *view_ = nullptr;
            NodeRef ref_           = 0;
        };

        explicit NodeRange(const GraphView *view) : view_(view) {}
        Iterator begin() const { return Iterator(view_, 0); }
        Iterator end() const { return Iterator(view_, view_ ? view_->nodeSlotCount() : 0); }

      private:
        const GraphView *view_ = nullptr;
    };

    NodeRange nodes() const { return NodeRange(this); }
};

} // namespace camel::graph