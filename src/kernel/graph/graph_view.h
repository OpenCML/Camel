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
 * a graph say something it does not store. Everything derivable -- liveness, may-fault, whether a
 * node is a macro, whether it is a constant, a node's position in a chain, a branch's join, an
 * aggregate's arity, which slot a value sits in -- is a fact derived from this face and cached by
 * the fact layer (section 4), never a member here. The adapter computes such an answer at the call,
 * or hands back the cache it already keeps; it does not learn a new one.
 *
 * Four readings left this face in the interface review, and each says where it went
 * so that a reader does not look for it here: a macro is read from the record of the callee or of
 * the operator (2.2 keeps macros in the records only), a constant is the node's kind and its
 * inputs, which slot a value sits in belongs to the execution plan (section 13), and the static
 * area is not handed out at all -- a reader asks the DATA node for its value, and the adapter reads
 * the area it keeps.
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
 * A fifth one was added by the interface review: a callee is the graph it names, by
 * name (an entry of the environment table) or by value, where the stored graph names it by a
 * `GraphId` (`CalleeRef`).
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
#include "camel/core/symbol.h"
#include "camel/core/type/base.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace camel::core::type {

// Declared, not included: this face names a type and hands the pointer the graph stores, and it
// must not make a reader compile the type system to read a node's kind.
class FunctionType;
class TupleType;

} // namespace camel::core::type

namespace camel::graph {

/**
 * The graph, as a value (design principles 2.3: in the process a graph's identity is the value
 * itself, not a number; graph data model 4.4). The frozen graph this stage's programs hold and the
 * graph an editing session is building are its two representations, each version of a graph is one
 * such value, and everything that names a graph -- a callee referred to by value, the anchor of a
 * fact (`kernel/facts`) -- names one of these.
 *
 * Declared here, by the lane that owns the graph, and only declared: this stage has two
 * representations and answers both through the two adapters, so nothing here may name either of
 * them (`GCGraph`, `GraphDraft`). When `Graph` gets members it moves to a header of its own; this
 * is where its name and meaning live until then. A reader that wants to read one asks its owner
 * for a `GraphView` -- the face below is how a graph is read, and the identity is the value.
 */
class Graph;

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
 * Who migrates: every reader of a node's kind -- `lib/test/graph_faces.cpp`, the ~37 `switch`es
 * section 1 counts, and the two backends once they read the view instead of the payload.
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
/// input table into: `with` is a role here, and the third run is not a port at all. `Capture` is
/// the third role a port has (interface review): the values a graph's body captured are
/// the graph's own ports, and not a list beside them.
/// Who migrates: `normInputsOf`/`withInputsOf`/`ctrlInputsOf`, every `portRoleOf`, and
/// `closureNodes`.
enum class PortGroup : std::uint8_t {
    /// The ordinary argument of the callee's signature.
    Normal = 0,
    /// The context (`with`) argument.
    Context = 1,
    /// A value the body captured from where its function value was made, in the order it captured
    /// it. Nothing at a call site names a capture -- it is bound from the closure tuple -- and it
    /// is a port of the graph all the same: a reader that walks a graph's ports asks one query
    /// three times instead of two plus a list of its own. The tuple those ports are read out of is
    /// their types, which `captureType()` used to answer and which is not a query any more.
    Capture = 2,
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
/// `LoopView`, and the loop readers in `lib/transforms` and `modules`.
struct LoopForm {
    camel::core::LoopKind kind                 = camel::core::LoopKind::While;
    camel::core::LoopIterableForm iterableForm = camel::core::LoopIterableForm::Array;
    /// The values the loop collects into arrays each round; 0 for a loop the source wrote.
    std::uint32_t collectionCount = 0;
};

/**
 * The callee a FUNC node names (core abstractions section 2; data model 2.3). A graph refers to
 * another graph only by a call, and there are exactly two ways to write that reference:
 *
 *  - by name, for a top-level function and its generic instances: the entry of the environment
 *    table of the function value the call is made from (2.3: a function value is its graph, its
 *    captured values, and an environment table that binds each name it refers to);
 *  - by value, for everything else -- a lambda, a graph a pass built, a graph the run made.
 *
 * Neither is a number. `GraphId` is the label a debugger and a dump name a graph by (data model
 * 4.4: a graph's identity in the process is the value itself), so it does not say how a call refers
 * to its callee and is not what this face answers. Replaces `calleeIdOf` and the `GCFuncRef`
 * payload a reader decodes to reach it. Both adapters still translate the id they store into this
 * reference, so every callee they answer today is by value; a name is answered once a function
 * value's environment table exists to answer from.
 */
struct CalleeRef {
    /// How the reference names its callee.
    enum class Kind : std::uint8_t {
        /// This node calls nothing: it is not a FUNC node, or it names no callee yet.
        None = 0,
        /// `name`: an entry of the function value's environment table.
        Name = 1,
        /// `graph`: the graph itself.
        Value = 2,
    };

    Kind kind = Kind::None;
    /// `Kind::Name`: the entry's name, interned (`camel/core/symbol.h`); `kEmptySym` otherwise.
    camel::core::SymId name = camel::core::kEmptySym;
    /// `Kind::Value`: the callee itself; nullptr otherwise. A reader that wants to read it asks its
    /// owner for a `GraphView` -- the value is the identity, the view is only how it is read.
    const Graph *graph = nullptr;
};

/**
 * The graph a reader holds, whatever it is made of. Replaces, together, `GCGraph` and `GraphDraft`
 * as *read* by anything but an adapter.
 *
 * A view is a way of reading a graph, not the graph's identity: the graph is a value (`Graph`
 * above), an adapter is made on demand, and the same graph read through another adapter is still
 * the same graph. So whatever has to name a graph past the call -- a callee referred to by value,
 * the anchor of a fact (`kernel/facts`) -- names the value, and the anchor survives the view.
 *
 * Every method answers what the graph stores. None of them decides anything: a reader that finds
 * `mayFault` here, or a node's macro-ness or constant-ness, is looking in the wrong layer (section
 * 4, and the paragraph above that says where each of the three went).
 *
 * The parameters are a virtual interface on purpose. The readers on it -- the encoder's self-check,
 * `LoopView`, `decidingRegionOf`, `ControlRegions`, `NodeRoleRegistry` --
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
    /// The graph's own ports, by group: the parameters of its body, and at `Capture` the values it
    /// captured, in the order it captured them. Replaces `normPorts`, `withPorts` and
    /// `closureNodes`: a capture is not a port at a call site, but it is a port of the graph, and
    /// answering it as one group of the same query is what keeps a reader from learning a second
    /// list. The tuple the captures are read out of is the types of these ports, so `captureType()`
    /// is not a query either (interface review).
    virtual std::vector<NodeRef> ports(PortGroup group) const = 0;
    // The graph's static area is not a query of this face (interface review): a span of
    // raw slots hands a reader the representation of values and makes it index one, which is the
    // execution plan's business and not the graph's content. A constant is asked of the DATA node
    // that stands for it (`constantOf`), and the adapter reads the static area it keeps.
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
    ///
    /// This is **not** the question "can this ref be asked about at all", and the difference is
    /// deliberate: a JOIN, a DREF and a dead place hold a node record and answer their ports
    /// (`inputs`, `users`, `predecessors`) while answering false here and no kind at all. A port
    /// list is a fact about the graph's edges; a kind is one of the nine, and there is no tenth for
    /// a place this vocabulary does not carry.
    ///
    /// Do not "fix" that by letting a JOIN into `nodes()`: `nodes()` answers what nodes the graph
    /// has, and whether a JOIN -- the merge point of a branch, not a value the graph computes -- is
    /// one of them is a question about what the view is for, not about whether its ports can be
    /// counted. `test::view_ports` pins the port answer node by node against the graph's own; that
    /// is the property the readers of this stage need, and it holds without a tenth kind.
    virtual bool contains(NodeRef ref) const = 0;

    virtual NodeKind kindOf(NodeRef ref) const = 0;
    /// The type of the node's value, nullptr when it has none. Types are interned, so both adapters
    /// answer the same pointer for the same type.
    virtual const camel::core::type::Type *typeOf(NodeRef ref) const = 0;
    /// The value of a DATA node that is a constant, `std::nullopt` for every other node. Replaces
    /// `staticArea()`: a reader asks the node it already has for its value, and where that value is
    /// kept -- one word of the static area -- stays the representation's, read by the adapter. The
    /// word is a value of the representation the graph and the back ends share (`slot_t`), so a
    /// reader decodes it through the node's `typeOf`.
    virtual std::optional<::slot_t> constantOf(NodeRef ref) const = 0;
    // Three more readings are not here (interface review), and a reader that wants one
    // asks where it is derived rather than this face:
    //
    //   isMacro      the record of the callee, or of the operator the node names (2.2 keeps macros
    //                in the records only, so a copy of the answer on a node would be a second
    //                truth).
    //   isConstant   the node's kind and its inputs: a DATA node whose value the graph was given,
    //                which is what `constantOf` answers.
    //   dataIndexOf  which slot the value sits in is the execution plan's (section 13), not the
    //                graph's content. The back ends keep reading the frozen payload; stage 3 moves
    //                the slot into the execution plan.
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
    /// The nodes directly in the region, in node order. A vector rather than a span because a graph
    /// stores the region *of* a node, not the nodes of a region: an adapter answering a span would
    /// keep a list it does not store.
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
    /// The callee of a FUNC node, by name or by value; `Kind::None` for another kind, and for a
    /// function that names no callee yet. Replaces `calleeIdOf` and the `GCFuncRef` payload a
    /// reader decodes to reach it -- and `GraphId`, which labels a graph for a debugger and does
    /// not say how a call refers to one. See `CalleeRef`.
    virtual CalleeRef calleeOf(NodeRef ref) const = 0;
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

    // Additions, and not part of this rework (interface review, item 6): the successors
    // of an order edge and of a sync edge -- the scheduler needs them, where `predecessors` answers
    // the one direction the readers of this stage ask for -- and an entry that reads a node's tags,
    // which are content of the graph and which a pass has to read. Both arrive as additions;
    // nothing here reserves a name for them.
};

} // namespace camel::graph