// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Native runtime GCGraph layout.
 *
 * Compile-time GIR remains a one-shot build artifact. Before execution starts,
 * the reachable compile graph closure is materialized into immutable runtime
 * graph objects. Runtime execution then consumes GCGraph directly.
 *
 * The frozen layout (docs/design/graph-model, section 4.1; migration step 3):
 * 1. A node is a fixed 28-byte record (GCNode) in one array; its NodeId (gc_node_ref_t) is its
 *    index in that array, from 0 to nodeCount() - 1. A sentinel record follows the last node.
 * 2. Each edge is stored once, as an entry of the input table: a node's inputs are one run of
 *    it, its Norm inputs, then its With inputs, then its Ctrl inputs; the record keeps the
 *    three starts and the next record's start ends the run.
 * 3. What a node of a kind carries beyond the record (the callee of a FUNC, the operator of an
 *    OPER, the positions of a FILL, ...) is in the payload area, in 8-byte words, at the word
 *    offset the record names.
 * 4. Users (the reverse direction) are not stored: GCGraph::uses() builds the index the first
 *    time anything asks for it and keeps it for the graph's lifetime.
 * 6. The source position of each node is a column parallel to the records (nodeOrigin): one
 *    32-bit origin id per node, 0 when the node has none. Beside it, only in a graph where some
 *    node has any, the numeric permissions the program granted with the standard permission
 *    tags: one mask per node (nodePermissions; design principles 7.5, docs/design/op-semantics
 *    6.3), and the attributes libraries attached to the nodes: one 32-bit interned set per node
 *    (nodeAttrs; camel/core/node_attrs.h, language reference 7.6).
 * 7. A graph refers to other graphs only through its reference table (refs(), design 3.7), by
 *    GraphId; the Program a graph is read in (program.h) says which version of each it means.
 *    A FUNC node names its callee by an index into it (GCFuncRef), and the graphs the function
 *    constants of the static area point to, the graphs written inside this one and the graphs
 *    it calls directly are entries of it, each marked with the roles it has (GCGraphRefRole).
 * 5. Hot-path data stays plain; cold debug metadata lives elsewhere (GCGraphDebugRecord).
 */

#pragma once

#include "camel/core/loop_kind.h"
#include "camel/core/operator.h"
#include "camel/core/operator_semantics.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/symbol.h"
#include "camel/core/type/intern.h"
#include "camel/runtime/graph_id.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace camel::core::type {
class FunctionType;
}

namespace camel::runtime {

class GraphDraft;
class Program;

} // namespace camel::runtime

namespace camel::compile::gir {
class Graph;
}

namespace camel::runtime {

using gc_node_ref_t = uint32_t;
using gc_slot_idx_t = int32_t;
using gc_data_idx_t = gc_slot_idx_t;
using gc_off_t      = uint32_t;
using gc_cnt_t      = uint32_t;

constexpr gc_node_ref_t kInvalidNodeRef = static_cast<gc_node_ref_t>(-1);
constexpr gc_off_t kInvalidOffset       = static_cast<gc_off_t>(-1);

// The kinds of node. The numeric values are part of the graph model (docs/design/graph-model,
// section 3.5) and must not be renumbered.
//
// Two values are reserved and never appear in a frozen graph (GCGraph):
//  - Bind: nothing creates it; partial application is a closure (a FILL of a function value).
//  - Dref: the front end's placeholder for a name whose target is not settled yet. It lives in
//    build-time drafts only; encoding a graph that still holds one is an error
//    (graph_encode.cpp, validateRuntimeGraphPayload).
// Consumers of frozen graphs therefore do not handle either.
enum class GCNodeKind : uint8_t {
    Data = 0,
    Port = 1,
    Cast = 2,
    Copy = 3,
    Fill = 4,
    Accs = 5,
    Brch = 6,
    Join = 7,
    Call = 8,
    Bind = 9, // reserved, see above
    Func = 10,
    Oper = 11,
    Sync = 12,
    Gate = 13,
    Dref = 14, // build-time only, see above
    // A loop (design principles 3.10; docs/design/graph-model 3.5, the LOOP card):
    // Norm inputs the array (For, ForWhile) then the k first values of the state; With inputs
    // the results of its two regions and the round port; Ctrl inputs ordering. Its condition
    // and body are regions of this graph (GCRegionKind::LoopCond and LoopBody) whose ports are
    // the state (see camel/runtime/loop.h). Its value is the
    // last state: the value when k = 1, the k-tuple when k > 1. A producer may also give it s
    // collection slots (payload collectionCount): the body then returns (state…, v…) each round
    // and the value is (state…, array…), the arrays holding the v of every round (see
    // camel/runtime/loop.h). Consumers read it through camel/runtime/loop.h (LoopView).
    Loop = 15,
    // The place of a node a rewrite removed. A graph keeps the NodeIds of the version it came
    // from, so a removed node leaves a record with this kind and no inputs, which every
    // consumer that walks the nodes skips (GCGraph::nodes() does); ids are not reused. A
    // graph without removed nodes has none.
    Dead = 255,
};

/// What a validator may assume of a graph and what it must check (docs/design/graph-store 3.7,
/// 5.4). The checks that every state shares (references, branch arms, the types of the slots, the
/// types along the edges, no cycle of inputs) are not listed; this is what differs:
///  - Executable: every value has one writing and the graph has no cycle of inputs. The state a
///    back end runs.
///  - Tabled: some value has several writings (an equivalence table on the graph). Not
///    executable until an extraction picks one writing per value; acyclic at the level of nodes.
enum class GraphState : uint8_t { Executable, Tabled };

enum class GCReturnKind : uint8_t {
    None,
    Self,
    LastDataInput,
    LastCtrlInput,
};

enum class GCCallKind : uint8_t {
    Indirect = 0,
};

enum class GCAccsKind : uint8_t {
    TupleIndex = 0,
    StructKey  = 1,
};

enum class GCFillKind : uint8_t {
    Tuple           = 0,
    Array           = 1,
    Struct          = 2,
    FunctionClosure = 3,
};

/// The role of a data port (docs/design/effects/edges.cn.md 1.5): the ordinary argument of the
/// callee's signature, or the context (`with`) argument. The role is a property of the port, not a
/// second edge kind: there is one data edge kind, and each of its ports carries one of these.
enum class PortRole : uint8_t {
    Normal,
    Context,
};

enum GCNodeFlag : uint8_t {
    kGCNodeFlagMacro    = 1 << 0,
    kGCNodeFlagConstant = 1 << 1,
    /// The node is a statement written directly in one `sync` block, or the block's value: the
    /// only nodes a sync edge may join (docs/design/effects/edges.cn.md 4.6). The seal check reads
    /// it; nothing at run time does, and a `sync` block that is inlined into another graph keeps it.
    kGCNodeFlagSyncStatement = 1 << 2,
};

/// Whether converting a value of type `source` to `target` when a CAST runs can fail: every
/// conversion the type does not call safe checks the value (an array's shape, a number's range).
bool castMayFault(const camel::core::type::Type *target, const camel::core::type::Type *source);

/// The one question a pass asks before it drops, moves, merges or reorders a node, or before it
/// decides that a node needs an order edge: is the node pure *and* unable to fault? Such a node
/// can be arranged anywhere -- off the sync chain, out of a loop body or an arm, away when a rewrite
/// drops it (design principles 3.4), and without an order edge after an inline (docs/design/effects/
/// edges.cn.md 5.7). An operator that may fault is not free: what it does when it fails is
/// observable, so its place is kept. An operator nobody declared is not known never to fault, so it
/// is not free either; neither is an operator that must run where the program writes it
/// (`OperatorTable::mayBeRecomputed`). A CAST is free when its conversion cannot fail
/// (`castMayFault`), and is asked with the types it converts between: without them it is not free.
/// `oper` is `kNoOper` for every kind but OPER. An elementwise operator (tensor arithmetic) fails
/// only when its operands' shapes do not broadcast: asked with `fixedShapes` (every operand and
/// the result have shapes the type states in full, `shapesAreFixed`) it cannot.
bool nodeIsFreelySchedulable(
    GCNodeKind kind, camel::core::OperId oper, const camel::core::type::Type *castTarget = nullptr,
    const camel::core::type::Type *castSource = nullptr, bool fixedShapes = false);

/// Whether a type states its shape in full: not an array, or an array of fixed rank whose
/// extents are all constants.
bool shapesAreFixed(const camel::core::type::Type *type);

class GCGraph;
/// The same question of a node of a graph; a CAST is asked with its result type and the type of
/// its input.
bool nodeIsFreelySchedulable(const GCGraph &graph, gc_node_ref_t ref);

/// Properties a graph has besides its nodes (GCGraph::flags).
enum GCGraphFlag : uint8_t {
    /// The function of the graph is a macro (design principles 8): `std::macro` may evaluate a
    /// call of it ahead of time and materialise the result as a constant. The fact is the
    /// standard tag `#macro` on the declaration, or, for an operator value, the operator's
    /// declared macro trait; it is recorded on the graph so that a runtime graph keeps it.
    /// Bit 0 was once the flag of a graph that carried a derivative rule as a closure value
    /// (the old `@vjp` decorator); that is a graph reference now (kGraphRefRule).
    kGraphFlagMacro = 1 << 0,
    /// A commit looked at the graph's gates: none waits on nothing (GraphDraft::
    /// dissolveUnorderedGates). A graph without it is looked at by the next commit that
    /// reaches it; a graph with it is left as it is unless it is edited.
    kGraphFlagGatesDissolved = 1 << 1,
    /// Some values of the graph have several writings (an equivalence table, writings.h). Such
    /// a graph is not in the executable state: it has no execution plan and no back end or
    /// exporter takes it; extraction brings it back.
    kGraphFlagHasWritings = 1 << 2,
    /// The graph is the semantic body of an `#impl` function (design principles 10.4): an
    /// ordinary call of the function runs its kernel operator instead, and a consumer that must
    /// see inside (autodiff, fusion, export, a rule) reads this graph. `Program::implBodies`
    /// lists the graphs that carry it.
    kGraphFlagImplBody = 1 << 3,
};

/// kWritingDerived: congruence closure found the equivalence, it need not be stored.
inline constexpr uint8_t kWritingDerived = 1 << 0;

/// One equivalence between two nodes of a graph (docs/design/graph-store 3.1): they are writings
/// of one value. A graph keeps the log of them (writings.h derives the classes from it).
struct WritingUnion {
    gc_node_ref_t a = kInvalidNodeRef;
    gc_node_ref_t b = kInvalidNodeRef;
    /// The numeric permissions the equivalence needs (0: it holds bit for bit).
    camel::core::sem::PermissionMask perms = 0;
    uint8_t flags = 0;
    /// The rule that asserted it (0: none, congruence).
    uint16_t rule = 0;
    bool operator==(const WritingUnion &) const = default;
};
static_assert(std::is_trivially_copyable_v<WritingUnion>);

/// No payload: the value of GCNode::payload for a node that carries nothing beyond its record.
inline constexpr gc_off_t kNoPayload = static_cast<gc_off_t>(-1);

/// One order edge of a graph (docs/design/effects/edges.cn.md section 1.5): `pred` runs before
/// `user` when both run, and nothing travels along it. Order edges are not data, so they are not
/// in a node's input port list, and they are not control inputs either: liveness does not follow
/// them. They live in their own array of the graph, sorted by user, so that the preds of a node
/// are one run of it. The same-resource order between two effects and the order between two
/// writes of one `var` are what a graph stores here.
struct GCGraphOrderEdge {
    gc_node_ref_t user = kInvalidNodeRef;
    gc_node_ref_t pred = kInvalidNodeRef;
    bool operator==(const GCGraphOrderEdge &) const = default;
};
static_assert(std::is_trivially_copyable_v<GCGraphOrderEdge>);

/// One sync edge of a graph (docs/design/effects/edges.cn.md section 1): `pred` is the statement
/// written before `user` in one `sync` block, and `pred` is therefore *used*: liveness follows a
/// sync edge backwards, as it follows a data port. A sync edge is not data, so it is not in a
/// node's input port list, and it is not an order edge either (an order edge carries no "used").
/// Like an order edge it lives in its own array of the graph, sorted by user, so that the sync
/// predecessors of a node are one run of it.
struct GCGraphSyncEdge {
    gc_node_ref_t user = kInvalidNodeRef;
    gc_node_ref_t pred = kInvalidNodeRef;
    bool operator==(const GCGraphSyncEdge &) const = default;
};
static_assert(std::is_trivially_copyable_v<GCGraphSyncEdge>);

/// One node of a frozen graph: a fixed 28-byte record (docs/design/graph-model, 4.1). Its inputs
/// and its payload are found through the graph (GCGraph::normInputsOf, nodeBodyAs).
struct GCNode {
    GCNodeKind kind         = GCNodeKind::Data;
    uint8_t flags           = 0;
    uint16_t reserved       = 0;
    gc_slot_idx_t dataIndex = 0;
    /// The type of the node's value (camel/core/type/intern.h); kNoTypeId when it has none.
    camel::core::type::TypeId typeId = camel::core::type::kNoTypeId;
    /// Index in the input table of the first Norm, With and Ctrl input. The Ctrl inputs end
    /// where the next record's Norm inputs begin.
    gc_off_t inBegin   = 0;
    gc_off_t withBegin = 0;
    gc_off_t ctrlBegin = 0;
    /// Offset of the payload in the payload area, in 8-byte words; kNoPayload when none.
    gc_off_t payload = kNoPayload;

    /// The type of the node's value, nullptr when it has none.
    camel::core::type::Type *dataType() const {
        return typeId == camel::core::type::kNoTypeId
                   ? nullptr
                   : camel::core::type::TypeTable::instance().get(typeId);
    }

    constexpr bool hasFlag(uint8_t mask) const { return (flags & mask) != 0; }
    constexpr bool isMacro() const { return hasFlag(kGCNodeFlagMacro); }
    constexpr bool isConstant() const { return hasFlag(kGCNodeFlagConstant); }
    constexpr bool hasPayload() const { return payload != kNoPayload; }
};
static_assert(sizeof(GCNode) == 28, "A frozen node record is 28 bytes.");
static_assert(std::is_trivially_copyable_v<GCNode>);

/// The unit the payload area is addressed in. Payload bodies hold pointers (the callee of a
/// FUNC, the implementation an OPER caches), so the unit is their alignment.
using gc_payload_word_t                   = uint64_t;
inline constexpr size_t kPayloadWordBytes = sizeof(gc_payload_word_t);

/// The roles of an entry of a graph's reference table (bit flags; one graph can have several).
enum GCGraphRefRole : uint8_t {
    kGraphRefCallee = 1 << 0, // called directly by a FUNC node (a dependency)
    kGraphRefNested = 1 << 1, // written inside this graph (a lambda, a branch arm)
    kGraphRefStatic = 1 << 2, // the graph of a function constant of the static area
    kGraphRefRule   = 1 << 3, // the graph a declaration tag of the graph names (a derivative rule)
};

/// "No graph" in a GCFuncRef or a GCLoopRef: a FUNC node that names no callee yet, the condition
/// of a For loop.
inline constexpr uint32_t kNoLoopGraph = UINT32_MAX;

/// Payload of a FUNC node: the index of the callee in the graph's reference table (a draft has
/// one too, GraphDraft::payloadGraphs), kNoLoopGraph for none yet.
struct GCFuncRef {
    uint32_t ref      = kNoLoopGraph;
    uint32_t reserved = 0;
};
static_assert(std::is_trivially_copyable_v<GCFuncRef>);

/// Payload of a LOOP node in a frozen graph: its form, the indexes of its condition graph
/// (kNoLoopGraph for For) and of its body graph in the graph's reference table, where both are
/// marked callee and nested (the LOOP node calls them each round; they are written inside the
/// graph), how many values the loop collects into arrays (its collection slots; 0 for a
/// loop the source wrote, which has none), and where its elements come from
/// (camel::core::LoopIterableForm: an array, or the pair of the interval's two bounds).
struct GCLoopRef {
    camel::core::LoopKind kind                        = camel::core::LoopKind::While;
    camel::core::LoopIterableForm iterableForm        = camel::core::LoopIterableForm::Array;
    uint16_t reserved1                                = 0;
    uint32_t condRef                                  = kNoLoopGraph;
    uint32_t bodyRef                                  = kNoLoopGraph;
    uint32_t collectionCount                          = 0;
};
static_assert(std::is_trivially_copyable_v<GCLoopRef>);
static_assert(sizeof(GCLoopRef) == 16, "A LOOP payload is two words.");

/// What a LOOP node names, as values: the graphs themselves instead of their reference-table
/// indexes (GraphDraft::loopGraphsOf, GCGraph's loopBodyOf).
struct GCLoopBody {
    GraphId condGraph                                 = kInvalidGraphId; // invalid for a For loop
    GraphId bodyGraph                                 = kInvalidGraphId;
    camel::core::LoopKind kind                        = camel::core::LoopKind::While;
    camel::core::LoopIterableForm iterableForm        = camel::core::LoopIterableForm::Array;
    uint16_t reserved1                                = 0;
    uint32_t collectionCount                          = 0; // the typed collection slots (LoopView)
};
static_assert(std::is_trivially_copyable_v<GCLoopBody>);

struct GCCallBody {
    GCCallKind callKind = GCCallKind::Indirect;
    uint8_t reserved0   = 0;
    uint16_t reserved1  = 0;
    uint32_t reserved2  = 0;
};
static_assert(std::is_trivially_copyable_v<GCCallBody>);

// Payload of an ACCS node: which member of its input it takes. Fixed size: a struct field is
// named by its symbol (camel/core/symbol.h), not by characters stored in the node.
struct GCAccsBody {
    GCAccsKind accsKind = GCAccsKind::TupleIndex;
    uint8_t reserved0   = 0;
    uint16_t reserved1  = 0;
    // TupleIndex: the element index. StructKey: the SymId of the field name.
    uint32_t value = 0;

    /// The field name of a StructKey access; empty for a TupleIndex access. The view is owned
    /// by the symbol table and never invalidated.
    std::string_view key() const {
        return accsKind == GCAccsKind::StructKey ? camel::core::symbolText(value)
                                                 : std::string_view{};
    }
};
static_assert(std::is_trivially_copyable_v<GCAccsBody>);

struct GCFillBody {
    GCFillKind fillKind = GCFillKind::Tuple;
    uint8_t reserved0   = 0;
    uint16_t reserved1  = 0;
    uint32_t slotCount  = 0;

    std::span<const gc_slot_idx_t> slots() const {
        const auto *data = reinterpret_cast<const gc_slot_idx_t *>(
            reinterpret_cast<const std::byte *>(this) + sizeof(GCFillBody));
        return {data, slotCount};
    }
};
static_assert(std::is_trivially_copyable_v<GCFillBody>);

inline std::vector<std::byte> makeFillPayload(GCFillKind kind, std::span<const size_t> slots) {
    GCFillBody body{
        .fillKind  = kind,
        .reserved0 = 0,
        .reserved1 = 0,
        .slotCount = static_cast<uint32_t>(slots.size()),
    };
    std::vector<std::byte> bytes(sizeof(GCFillBody) + sizeof(gc_slot_idx_t) * slots.size());
    std::memcpy(bytes.data(), &body, sizeof(body));
    auto *encodedSlots = reinterpret_cast<gc_slot_idx_t *>(bytes.data() + sizeof(GCFillBody));
    for (size_t i = 0; i < slots.size(); ++i) {
        encodedSlots[i] = static_cast<gc_slot_idx_t>(slots[i]);
    }
    return bytes;
}

struct GCBrchBody {
    gc_node_ref_t join = kInvalidNodeRef;
    gc_cnt_t armCount  = 0;
};
static_assert(std::is_trivially_copyable_v<GCBrchBody>);

struct GCJoinBody {
    gc_node_ref_t brch = kInvalidNodeRef;
    gc_cnt_t armCount  = 0;
};
static_assert(std::is_trivially_copyable_v<GCJoinBody>);

// Payload of an OPER node. Fixed size: the operator is named by its index in the operator
// table (camel/core/operator_table.h), which holds the URI and the declared traits once per
// operator instead of once per node.
struct GCOperBody {
    // The implementation, when a scheduler has resolved it; nullptr until then. A cache of what
    // the executor manager answers for `oper`, not part of the node's identity.
    operator_t op            = nullptr;
    camel::core::OperId oper = camel::core::kNoOper;
    uint32_t reserved        = 0;

    /// The URI of the operator. The view is owned by the symbol table and never invalidated;
    /// resolving it takes no lock.
    std::string_view uri() const { return camel::core::OperatorTable::instance().uri(oper); }
};
static_assert(std::is_trivially_copyable_v<GCOperBody>);

/// The columns of up to kNodeChunkNodes consecutive nodes, in one block of the graph space. A
/// graph version is a directory of chunks; versions share the chunks they did not change
/// (shares counts the directories that hold one). NodeId `ref` is node `ref & (kNodeChunkNodes
/// - 1)` of chunk `ref >> kNodeChunkShift`. Offsets in a chunk's records (the input runs, the
/// payload) are offsets in that chunk's own arrays; node references everywhere are NodeIds of
/// the graph.
inline constexpr uint32_t kNodeChunkShift = 8;
inline constexpr uint32_t kNodeChunkNodes = 1u << kNodeChunkShift;

/// What a region is (docs/design/graph-store 3.3). A control region is executed, or not, as a
/// whole, with the node that owns it; an annotation region only records where nodes came from.
/// Region 0 is the function body; the table of a graph that has no other region is empty.
enum class GCRegionKind : uint8_t {
    Root     = 0,
    Arm      = 1, // an arm of a BRCH node (owner), `index` the arm
    LoopCond = 2, // the condition of a LOOP node (owner)
    LoopBody = 3, // the body of a LOOP node (owner)
    Inline   = 4, // the body of a function inlined here: inlineOf, called at callSite
    Block    = 5, // a block that carries tags or a decorator
    Dead     = 255, // the place of a region a rewrite removed (ids are not reused)
};

struct GCRegion {
    GCRegionKind kind    = GCRegionKind::Root;
    uint8_t reserved0    = 0;
    uint16_t reserved1   = 0;
    uint32_t parent      = 0;                // the region it is in; 0 for the body
    gc_node_ref_t owner  = kInvalidNodeRef;  // control regions: the BRCH or LOOP node
    uint32_t index       = 0;                // Arm: which arm
    gc_node_ref_t result = kInvalidNodeRef;  // the node whose value the region yields, if any
    uint32_t portBegin   = 0;                // its form ports: regionPorts[portBegin ..< +portCount]
    uint32_t portCount   = 0;
    uint32_t origin      = 0;                // source origin (0: none)
    uint32_t attrs       = 0;                // the tag set of the region (camel/core/node_attrs.h)
    GraphId inlineOf     = kInvalidGraphId;  // Inline: the function whose body this is
    gc_node_ref_t callSite = kInvalidNodeRef; // Inline: the call it replaced; Arm: the call of the
                                              // graph that is the arm, until it is inlined
    // The names and types of the form ports, in order, as the arguments of a function type (the
    // loop parts; null where the ports are unnamed).
    const ::camel::core::type::FunctionType *params = nullptr;
};
static_assert(std::is_trivially_copyable_v<GCRegion>);

struct GCNodeChunk {
    std::atomic<uint32_t> shares{1};
    uint32_t nodeCount        = 0;
    uint32_t edgeCount        = 0;
    uint32_t payloadWordCount = 0;
    uint32_t *origins         = nullptr; // nodeCount source origins (0: none)
    uint32_t *regions         = nullptr; // nodeCount regions of the nodes; null when all are 0
    camel::core::sem::PermissionMask *permissions = nullptr; // nodeCount numeric permissions
    uint32_t *attrs           = nullptr; // nodeCount attribute sets; null when all are 0
    gc_node_ref_t *edges      = nullptr; // edgeCount entries: the input runs of the nodes in order
    gc_payload_word_t *payload = nullptr; // payloadWordCount words

    /// The node records, which follow the header: nodeCount of them.
    GCNode *nodes() { return reinterpret_cast<GCNode *>(this + 1); }
    const GCNode *nodes() const { return reinterpret_cast<const GCNode *>(this + 1); }
};
static_assert(sizeof(GCNodeChunk) % alignof(GCNode) == 0);

struct GCGraphNativePayload {
    // The graph's own arrays are in its block, in the graph space (see GCGraphBuildAccess); the
    // chunks are blocks of their own, released with the last graph version that holds them.
    GCNodeChunk **chunks       = nullptr; // chunkCount chunks
    gc_cnt_t chunkCount        = 0;
    bool hasPermissions        = false; // some node has numeric permissions
    bool hasAttrs              = false; // some node has attributes
    bool hasRegions            = false; // some node is in a region other than the body
    GCRegion *regionTable      = nullptr; // regionCount regions; region 0 is the body
    gc_cnt_t regionCount       = 0;       // 0 when the graph has no region but the body
    gc_node_ref_t *regionPorts = nullptr;
    gc_cnt_t regionPortCount   = 0;
    WritingUnion *writings     = nullptr; // writingCount equivalences; none: executable state
    gc_cnt_t writingCount      = 0;
    gc_node_ref_t *normPorts   = nullptr;
    gc_node_ref_t *withPorts   = nullptr;
    gc_node_ref_t *closure     = nullptr;
    // The order edges of the graph, sorted by (user, pred); null when the graph has none. Not in
    // the nodes' input runs: an order edge is not a port (GCGraph::orderPredsOf).
    GCGraphOrderEdge *orderEdges = nullptr;
    gc_cnt_t orderEdgeCount      = 0;
    // The sync edges of the graph, sorted by (user, pred); null when the graph has none. Not in
    // the nodes' input runs either (GCGraph::syncPredsOf).
    GCGraphSyncEdge *syncEdges = nullptr;
    gc_cnt_t syncEdgeCount     = 0;
    gc_cnt_t nodeCount         = 0;
    gc_cnt_t edgeCount         = 0;
    gc_cnt_t payloadWordCount  = 0;
    gc_cnt_t normPortCount     = 0;
    gc_cnt_t withPortCount     = 0;
    gc_cnt_t closureCount      = 0;
    gc_node_ref_t entryNode    = kInvalidNodeRef;
    gc_node_ref_t exitNode     = kInvalidNodeRef;
    gc_node_ref_t outputNode   = kInvalidNodeRef;
    gc_node_ref_t returnNode   = kInvalidNodeRef;
    GCReturnKind returnKind    = GCReturnKind::None;

    constexpr bool empty() const { return chunks == nullptr; }
};

/// The users of every node of one graph, by edge group, in node order (the reverse of the
/// input table). Built on demand by GCGraph::uses().
class GCUseIndex {
  public:
    explicit GCUseIndex(const class GCGraph &graph);

    std::span<const gc_node_ref_t> norm(gc_node_ref_t ref) const { return group(ref, 0); }
    std::span<const gc_node_ref_t> with(gc_node_ref_t ref) const { return group(ref, 1); }
    std::span<const gc_node_ref_t> ctrl(gc_node_ref_t ref) const { return group(ref, 2); }

  private:
    std::span<const gc_node_ref_t> group(gc_node_ref_t ref, size_t kind) const {
        const size_t at = static_cast<size_t>(ref) * 3 + kind;
        return {users_.data() + begins_[at], begins_[at + 1] - begins_[at]};
    }

    std::vector<gc_node_ref_t> users_;
    std::vector<uint32_t> begins_; // 3 per node, then the end
};

struct GCGraphDebugRecord;
struct GCGraphDerivation;
struct GCGraphBuildAccess;
class GCGraph;

class GCGraph : public camel::core::rtdata::Object {
  public:
    // These accessors expose cold compile/debug metadata only. Runtime passes
    // should derive execution semantics from the native payload instead.
    /// The graph's identity within its program, the same for every version of the graph
    /// (graph_id.h).
    GraphId id() const { return id_; }
    const std::string &mangledName() const;
    const std::string &name() const;
    /// Source origin (camel::source::origin_id_t) of node `ref`, 0 when unknown.
    uint64_t nodeOrigin(gc_node_ref_t ref) const;
    /// The source name of the value at node `ref` (`let x`, `var x`), empty when it has none
    /// (camel::core::kEmptySym). Cold debug data: it is not a column of the graph.
    std::string_view nodeName(gc_node_ref_t ref) const;
    /// The symbol of the source name of node `ref` (kEmptySym when it has none).
    camel::core::SymId nodeNameSym(gc_node_ref_t ref) const;
    /// True when some node of the graph has a source name.
    bool hasNodeNames() const;
    /// Numeric permissions (camel/core/operator_semantics.h, PermissionMask) the program granted
    /// to node `ref`, 0 when none; the graph has no permission column when no node has any.
    camel::core::sem::PermissionMask nodePermissions(gc_node_ref_t ref) const;
    /// True when some node of the graph has numeric permissions.
    bool hasNodePermissions() const { return nativePayload_.hasPermissions; }
    /// The attributes (camel/core/node_attrs.h, AttrSetId) of node `ref`, 0 when none; the
    /// graph has no attribute column when no node has any.
    uint32_t nodeAttrs(gc_node_ref_t ref) const;
    /// The number of regions; 0 for a graph with the body only. Region ids are 0 .. count - 1.
    gc_cnt_t regionCount() const { return nativePayload_.regionCount; }
    /// Region `id` (0 is the body); nullptr when the graph has no such region.
    const GCRegion *region(uint32_t id) const {
        return id < nativePayload_.regionCount ? nativePayload_.regionTable + id : nullptr;
    }
    /// The region node `ref` is in: the innermost; 0 for the body.
    uint32_t nodeRegion(gc_node_ref_t ref) const {
        if (!containsNodeRef(ref)) {
            return 0;
        }
        const GCNodeChunk &c = chunkOf(ref);
        return c.regions ? c.regions[ref & (kNodeChunkNodes - 1)] : 0;
    }
    /// The LoopCond or LoopBody region (`part`) of the LOOP node `ref`; 0 when it has none: a For
    /// loop has no condition, and a loop in graph form has no regions.
    uint32_t loopRegionOf(gc_node_ref_t ref, GCRegionKind part) const {
        for (uint32_t r = 1; r < nativePayload_.regionCount; ++r) {
            const GCRegion &region = nativePayload_.regionTable[r];
            if (region.kind == part && region.owner == ref) {
                return r;
            }
        }
        return 0;
    }
    /// The form ports of region `id`, in order.
    std::span<const gc_node_ref_t> regionPorts(uint32_t id) const {
        const GCRegion *r = region(id);
        return r ? std::span<const gc_node_ref_t>(
                       nativePayload_.regionPorts + r->portBegin,
                       r->portCount)
                 : std::span<const gc_node_ref_t>();
    }
    /// True when some node of the graph has attributes.
    bool hasNodeAttrs() const { return nativePayload_.hasAttrs; }
    /// The names of the values the function captures from where it was written, in the order
    /// it captured them (symbols of camel/core/symbol.h). Compile and debug metadata: what a
    /// capture is bound to at run time is the closure tuple and the with-inputs of a call.
    /// Not part of the function type (camel/core/type/composite/func.h).
    std::span<const camel::core::SymId> captureNames() const;
    camel::core::type::FunctionType *funcType() const;
    const camel::core::type::TupleType *runtimeDataType() const;
    const camel::core::type::TupleType *staticDataType() const;
    const camel::core::type::TupleType *closureType() const;
    bool hasFrameLayout() const;
    size_t frameSize() const;
    bool isMacro() const;
    /// The semantic body of an `#impl` function (kGraphFlagImplBody): an ordinary call of the
    /// function is a call of its kernel operator, and this graph is what an expansion reads
    /// (design principles 10.4).
    bool isImplBody() const { return (flags_ & kGraphFlagImplBody) != 0; }
    /// The kernel the semantic body of an `#impl` function implements (design principles 10.4):
    /// the operator URI a direct call of the function is compiled to, empty for every other
    /// graph. A consumer that must see inside the function (autodiff, fusion, export, a rule)
    /// finds the body from the OPER at a call site through it, instead of the kernel's own
    /// derivative or lowering rule, which is execution and not meaning.
    const std::string &implKernelUri() const;
    /// The facts of the declaration this graph is (design principles 6.2): the interned attribute
    /// set the module keeps for it (`camel/core/node_attrs.h`), 0 for a graph that is not a
    /// declaration. A declaration fact stays on the declaration (no node carries one: `#vjp(rule)`
    /// names a graph and `attr::fateOf` records that it is not a node attribute), so this is what
    /// a pass reads it through — the function it was given, without the module that declares it.
    /// Cold metadata: it says nothing about the graph's content.
    uint32_t declarationFacts() const;
    /// The graph this one is written in, invalid for a root graph.
    GraphId outerGraphId() const { return outerGraphId_; }
    /// The graph's revision (graph facts, camel/runtime/facts.h): a process-wide number. A
    /// commit keeps it for a graph it did not change and gives a new one to a graph a pass edited
    /// into different content. It says nothing about the graphs this one refers to
    /// (closureRevision in reachable.h does). Equal revisions of one GraphId mean equal content.
    uint64_t revision() const { return revision_; }
    /// The function this graph is a type-only specialization of, or was derived from a
    /// specialization of (std::opt's recursion bound counts them): the first function in the
    /// chain that is not one itself. kInvalidGraphId for a graph that has no such ancestor.
    GraphId typeSpecializationRoot() const { return typeSpecializationRoot_; }
    /// GCGraphFlag bits.
    uint8_t flags() const { return flags_; }
    /// The executable state (graph store design 3.7): every value has exactly one writing.
    bool executable() const { return (flags_ & kGraphFlagHasWritings) == 0; }
    /// The equivalences between the nodes of the graph: empty in the executable state.
    std::span<const WritingUnion> writings() const {
        return {nativePayload_.writings, nativePayload_.writingCount};
    }
    bool isRoot() const;
    ::Tuple *staticArea() const { return staticArea_; }

    /// The reference table: every graph this one refers to, once each (see the file comment).
    /// Program::resolve turns it into graphs.
    std::span<const GraphId> refs() const { return {refs_, refCount_}; }
    /// The roles of entry `index` of the reference table (GCGraphRefRole bits).
    uint8_t refRoles(size_t index) const { return refRoles_[index]; }
    /// True when a function constant of the static area names a graph (kGraphRefStatic).
    bool hasStaticGraphRefs() const {
        for (gc_cnt_t i = 0; i < refCount_; ++i) {
            if (refRoles_[i] & kGraphRefStatic) {
                return true;
            }
        }
        return false;
    }
    /// The entries of the reference table with role `role`, in table order.
    std::vector<GraphId> refsWithRole(uint8_t role) const;
    /// The graphs called directly, written inside this one, referred to by a function
    /// constant, and named by a declaration tag of the graph (a derivative rule): views of the
    /// reference table by role.
    std::vector<GraphId> dependencies() const { return refsWithRole(kGraphRefCallee); }
    std::vector<GraphId> subGraphs() const { return refsWithRole(kGraphRefNested); }
    std::vector<GraphId> staticGraphRefs() const { return refsWithRole(kGraphRefStatic); }
    std::vector<GraphId> ruleGraphs() const { return refsWithRole(kGraphRefRule); }
    std::span<const slot_t> staticSlots() const;

    bool hasNodePayload() const;
    const GCGraphNativePayload *nodePayload() const;
    /// Number of node places: NodeIds are 0 .. nodeCount() - 1, and some of them may be Dead.
    gc_cnt_t nodeCount() const { return nativePayload_.nodeCount; }
    /// Number of nodes that are not Dead.
    gc_cnt_t liveNodeCount() const;
    /// Number of edges (input table entries).
    gc_cnt_t edgeCount() const { return nativePayload_.edgeCount; }

    const GCNode *entryNode() const;
    gc_node_ref_t entryNodeRef() const;
    const GCNode *exitNode() const;
    gc_node_ref_t exitNodeRef() const;
    const GCNode *outputNode() const;
    gc_node_ref_t outputNodeRef() const;
    const GCNode *returnNode() const;
    gc_node_ref_t returnNodeRef() const;
    GCReturnKind returnKind() const;

    /// The record of node `ref`, nullptr when `ref` is not a node of this graph.
    const GCNode *node(gc_node_ref_t ref) const {
        return ref < nativePayload_.nodeCount ? chunkOf(ref).nodes() + (ref & (kNodeChunkNodes - 1))
                                              : nullptr;
    }
    bool containsNodeRef(gc_node_ref_t ref) const { return ref < nativePayload_.nodeCount; }
    /// The NodeId of a record of this graph, kInvalidNodeRef for any other pointer.
    gc_node_ref_t nodeRef(const GCNode *node) const;
    /// FUNC node `ref`: the callee (invalid when `ref` is not a FUNC node).
    GraphId calleeIdOf(gc_node_ref_t ref) const;
    /// LOOP node `ref`: its condition graph (invalid for a For loop, or when `ref` is not a
    /// LOOP node) and its body graph (invalid when `ref` is not a LOOP node).
    GraphId loopCondIdOf(gc_node_ref_t ref) const;
    GraphId loopBodyIdOf(gc_node_ref_t ref) const;
    /// LOOP node `ref`: its form. The node must be a LOOP node.
    camel::core::LoopKind loopKindOf(gc_node_ref_t ref) const {
        return nodeBodyAs<GCLoopRef>(ref)->kind;
    }
    /// LOOP node `ref`: how many values it collects into arrays each round (0 for a loop the
    /// source wrote). The node must be a LOOP node.
    uint32_t loopCollectionCountOf(gc_node_ref_t ref) const {
        return nodeBodyAs<GCLoopRef>(ref)->collectionCount;
    }
    /// LOOP node `ref`: where its elements come from (camel::core::LoopIterableForm). The node
    /// must be a LOOP node.
    camel::core::LoopIterableForm loopIterableFormOf(gc_node_ref_t ref) const {
        return nodeBodyAs<GCLoopRef>(ref)->iterableForm;
    }
    /// The two bounds of the interval a loop iterates (LoopIterableForm::Interval), the first
    /// bound first: the pair of integers the front end built for `a..b`, whose components 0 and 1
    /// are the bounds (the FILL's Norm input is the static template of the pair). Nothing when
    /// `ref` is not such a pair. The interval's value in any other position is an `int[]`
    /// (design principles 4.2), not this pair.
    std::optional<std::pair<gc_node_ref_t, gc_node_ref_t>> intervalBoundsOf(
        gc_node_ref_t ref) const {
        const GCNode *n = node(ref);
        if (n == nullptr || n->kind != GCNodeKind::Fill) {
            return std::nullopt;
        }
        const auto *fill = nodeBodyAs<GCFillBody>(ref);
        if (fill == nullptr || fill->slots().size() != 2) {
            return std::nullopt;
        }
        std::optional<gc_node_ref_t> from, to;
        size_t seen = 0;
        for (gc_node_ref_t input : withInputsOf(ref)) {
            const size_t component = fill->slots()[seen++];
            if (component == 0) {
                from = input;
            } else if (component == 1) {
                to = input;
            }
        }
        if (!from.has_value() || !to.has_value()) {
            return std::nullopt;
        }
        return std::pair{*from, *to};
    }
    /// The two integers of the interval pair `intervalBoundsOf` reads, when a pass wrote that pair
    /// as the constant holding the same two values instead of the FILL that builds it: the pair is
    /// the value, not the form, so a loop's bounds are read the same from either. The first bound
    /// first. Nothing when `ref` is not such a constant.
    std::optional<std::pair<int64_t, int64_t>> intervalPairValuesOf(gc_node_ref_t ref) const;
    gc_node_ref_t matchedJoinOutputOf(gc_node_ref_t ref) const;

    /// The payload of node `ref` as a `Body` (see the payload structs above). The node must
    /// have one: every node of the kinds FUNC, CALL, ACCS, FILL, BRCH, JOIN, OPER and LOOP does.
    template <typename Body> const Body *nodeBodyAs(gc_node_ref_t ref) const {
        static_assert(
            std::is_trivially_copyable_v<Body>,
            "GC node bodies must be trivially copyable.");
        const GCNode *n = node(ref);
        ASSERT(n != nullptr && n->hasPayload(), "GCGraph node-body lookup of a node without one.");
        return reinterpret_cast<const Body *>(chunkOf(ref).payload + n->payload);
    }
    /// The payload bytes of node `ref` (rounded up to whole words); empty when it has none.
    std::span<const std::byte> nodePayloadBytes(gc_node_ref_t ref) const;
    /**
     * The version of node `ref`, as a hash of what it is: its record (kind, flags, slot, type),
     * the nodes it consumes (Norm, then With, then Ctrl, in order) and its payload. Two versions
     * of a graph that carry the same node at the same place hash the same, whatever else of the
     * graph changed, and a node whose record moved to another chunk hashes the same as well (the
     * offsets it holds are read as counts). 0 for a ref that is not a node.
     *
     * This is what a node-level fact is stamped with (facts.h, Dependency::Node): a fact about a
     * node holds while the node does not change, though the graph's revision did. It is the
     * version a stored mark would say, without the 8 bytes per node of the whole graph space
     * (design graph-store 4.4).
     */
    uint64_t nodeContentHash(gc_node_ref_t ref) const;

    std::span<const gc_node_ref_t> normInputsOf(gc_node_ref_t ref) const {
        const GCNode *n = node(ref);
        return n ? inputRun(ref, n->inBegin, n->withBegin) : std::span<const gc_node_ref_t>();
    }
    std::span<const gc_node_ref_t> withInputsOf(gc_node_ref_t ref) const {
        const GCNode *n = node(ref);
        return n ? inputRun(ref, n->withBegin, n->ctrlBegin) : std::span<const gc_node_ref_t>();
    }
    std::span<const gc_node_ref_t> ctrlInputsOf(gc_node_ref_t ref) const {
        const GCNode *n = node(ref);
        if (!n) {
            return {};
        }
        // The run ends where the next node's begins; the last node of a chunk ends at its end.
        const GCNodeChunk &chunk = chunkOf(ref);
        const gc_off_t end = (ref & (kNodeChunkNodes - 1)) + 1 < chunk.nodeCount ? (n + 1)->inBegin
                                                                                 : chunk.edgeCount;
        return inputRun(ref, n->ctrlBegin, end);
    }

    /// The ports of the one data edge kind, in port order: every normal-role port, then the
    /// context-role ones (docs/design/effects/edges.cn.md 1.5). The run is one piece of the input
    /// table -- `inBegin .. withBegin` are the normal ports and `withBegin .. ctrlBegin` the
    /// context ones -- so the whole list is `inBegin .. ctrlBegin`.
    std::span<const gc_node_ref_t> dataPortsOf(gc_node_ref_t ref) const {
        const GCNode *n = node(ref);
        if (!n) {
            return {};
        }
        return inputRun(ref, n->inBegin, n->ctrlBegin);
    }
    /// The role of the port at `port` of `dataPortsOf(ref)`.
    PortRole portRoleOf(gc_node_ref_t ref, size_t port) const {
        const GCNode *n = node(ref);
        if (!n) {
            return PortRole::Normal;
        }
        const size_t normalPorts = static_cast<size_t>(n->withBegin - n->inBegin);
        return port < normalPorts ? PortRole::Normal : PortRole::Context;
    }

    /// The order predecessors of `ref`, in the order they are stored (the source order they were
    /// written in): the nodes that must run before it when both run, along an edge that carries no
    /// "used" (GCGraphOrderEdge). Empty for a node with none.
    std::span<const GCGraphOrderEdge> orderPredsOf(gc_node_ref_t ref) const {
        return sideRunOf(nativePayload_.orderEdges, nativePayload_.orderEdgeCount, ref);
    }
    /// Every order edge of the graph, sorted by (user, pred).
    std::span<const GCGraphOrderEdge> orderEdges() const {
        return {nativePayload_.orderEdges, nativePayload_.orderEdgeCount};
    }
    /// True when some node of the graph is ordered by an order edge.
    bool hasOrderEdges() const { return nativePayload_.orderEdgeCount != 0; }

    /// The sync predecessors of `ref`, in the order they are stored (the written order of the
    /// sync block): the statements before it, whose "used" reaches it. Empty for a node with none.
    std::span<const GCGraphSyncEdge> syncPredsOf(gc_node_ref_t ref) const {
        return sideRunOf(nativePayload_.syncEdges, nativePayload_.syncEdgeCount, ref);
    }
    /// Every sync edge of the graph, sorted by (user, pred).
    std::span<const GCGraphSyncEdge> syncEdges() const {
        return {nativePayload_.syncEdges, nativePayload_.syncEdgeCount};
    }
    /// True when some node of the graph is a statement of a `sync` block after the first.
    bool hasSyncEdges() const { return nativePayload_.syncEdgeCount != 0; }

    /// The users of every node, built the first time it is asked for and kept with the graph
    /// (the frozen block stores each edge once, as an input). Thread-safe.
    const GCUseIndex &uses() const;
    /// How to run this version: the order of its nodes, its branches and calls (exec_plan.h).
    /// Built the first time it is asked for and kept with the graph. Thread-safe.
    const class ExecPlan &execPlan() const;
    std::span<const gc_node_ref_t> normOutputsOf(gc_node_ref_t ref) const;
    std::span<const gc_node_ref_t> withOutputsOf(gc_node_ref_t ref) const;
    std::span<const gc_node_ref_t> ctrlOutputsOf(gc_node_ref_t ref) const;

    std::span<const gc_node_ref_t> normPorts() const;
    std::span<const gc_node_ref_t> withPorts() const;
    std::span<const gc_node_ref_t> closureNodes() const;
    /// The number of arms of the BRCH `brchRef` (0 for another node).
    gc_cnt_t armCountOf(gc_node_ref_t brchRef) const;

    class NodeIterator {
      public:
        NodeIterator(const GCGraph *graph, gc_node_ref_t ref) : graph_(graph), ref_(ref) {
            skipDead();
        }

        const GCNode *operator*() const { return graph_->node(ref_); }
        NodeIterator &operator++() {
            ++ref_;
            skipDead();
            return *this;
        }
        bool operator==(const NodeIterator &other) const {
            return graph_ == other.graph_ && ref_ == other.ref_;
        }
        bool operator!=(const NodeIterator &other) const { return !(*this == other); }
        gc_node_ref_t ref() const { return ref_; }

      private:
        void skipDead() {
            while (graph_ && ref_ < graph_->nodeCount() &&
                   graph_->node(ref_)->kind == GCNodeKind::Dead) {
                ++ref_;
            }
        }

        const GCGraph *graph_ = nullptr;
        gc_node_ref_t ref_    = 0;
    };

    class NodeRange {
      public:
        explicit NodeRange(const GCGraph *graph) : graph_(graph) {}
        NodeIterator begin() const { return NodeIterator(graph_, 0); }
        NodeIterator end() const { return NodeIterator(graph_, graph_ ? graph_->nodeCount() : 0); }

      private:
        const GCGraph *graph_ = nullptr;
    };

    /// Every node, in NodeId order.
    NodeRange nodes() const { return NodeRange(this); }

    bool equals(
        const camel::core::rtdata::Object *other, const camel::core::type::Type *type,
        bool deep = false) const override;
    camel::core::rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type,
        bool deep = false) const override;
    void print(std::ostream &os, const camel::core::type::Type *type) const override;
    void onMoved() override {}
    ~GCGraph() override;
    void updateRefs(
        const camel::core::rtdata::Object::RefRelocator &relocate,
        const camel::core::type::Type *type) override;

  private:
    friend class GCGraphManager;
    friend class GraphDraft;
    friend struct GCGraphBuildAccess;

    /// Gives up this graph's share of each node chunk (the graph is being freed).
    void releaseNodeChunks();

    /// The entries of a side array (order edges, sync edges) whose user is `ref`: the array is
    /// sorted by (user, pred), so they are one run of it.
    template <typename Edge>
    static std::span<const Edge> sideRunOf(const Edge *edges, gc_cnt_t count, gc_node_ref_t ref) {
        if (count == 0 || edges == nullptr) {
            return {};
        }
        const Edge *begin = edges;
        const Edge *end   = begin + count;
        const auto lower  = std::lower_bound(
            begin, end, ref, [](const Edge &edge, gc_node_ref_t user) { return edge.user < user; });
        const auto upper = std::lower_bound(
            lower, end, ref, [](const Edge &edge, gc_node_ref_t user) { return edge.user <= user; });
        return {lower, static_cast<size_t>(upper - lower)};
    }

    GCGraph(
        GraphId id, const GCGraphDerivation &derivation, GCGraphDebugRecord *debugRecord,
        camel::core::type::FunctionType *funcType, camel::core::type::TupleType *runtimeDataType,
        camel::core::type::TupleType *staticDataType, camel::core::type::TupleType *closureType,
        GraphId outerGraph, GraphId *refs, uint8_t *refRoles, gc_cnt_t refCount,
        const GCGraphNativePayload &payload, ::Tuple *staticArea);

    GraphId id_                                    = kInvalidGraphId;
    GraphId typeSpecializationRoot_                = kInvalidGraphId; // see typeSpecializationRoot()
    uint8_t flags_                                 = 0;               // see flags()
    GCGraphDebugRecord *debug_                     = nullptr;
    camel::core::type::FunctionType *funcType_     = nullptr;
    camel::core::type::TupleType *runtimeDataType_ = nullptr;
    camel::core::type::TupleType *staticDataType_  = nullptr;
    camel::core::type::TupleType *closureType_     = nullptr;
    GraphId outerGraphId_                          = kInvalidGraphId;
    uint64_t revision_                             = 0; // see revision()
    GraphId *refs_                                 = nullptr;
    uint8_t *refRoles_                             = nullptr;
    gc_cnt_t refCount_                             = 0;
    const GCNodeChunk &chunkOf(gc_node_ref_t ref) const {
        return *nativePayload_.chunks[ref >> kNodeChunkShift];
    }
    std::span<const gc_node_ref_t> inputRun(gc_node_ref_t ref, gc_off_t begin, gc_off_t end) const {
        return {chunkOf(ref).edges + begin, static_cast<size_t>(end - begin)};
    }

    GCGraphNativePayload nativePayload_{};
    ::Tuple *staticArea_ = nullptr;
    // Built by uses() on first demand, owned by the graph.
    mutable std::atomic<GCUseIndex *> uses_{nullptr};
    // Built by execPlan() on first demand, owned by the graph.
    mutable std::atomic<class ExecPlan *> execPlan_{nullptr};
};

/// Which of a FUNC node's callee's captured variables the call writes, as a bit per capture of
/// the callee's capture table in the order its captures were made (GCFuncRef::reserved). It is
/// what the value the call hands on carries the new version of, so the builder that made the
/// node and every reader of it agree on the same set. 0 for a node without a FUNC payload.
uint32_t funcWriteMaskOf(const GCGraph &graph, gc_node_ref_t ref);

/// The type of the value a FUNC node hands on: the callee's exit type alone when the callee
/// writes no captured variable, and otherwise the tuple of the exit value (absent for a `void`
/// exit) and the new version of each written variable, in the callee's capture order (the
/// callee's capture ports, after its declared `with` ports). A `void` callee that writes
/// exactly one variable has that version for its value, not a tuple of one element: the version
/// of a variable is a cell, and that is what a read of it takes. Builder::callValueTypeOf
/// decides the same thing where the node is made; this is the same rule read back from a
/// frozen graph, which is what the box check and the backends have.
camel::core::type::Type *callValueTypeOf(const GCGraph &callee, uint32_t writtenCaptures);

/// The draft payload of the LOOP node `ref` of a frozen graph: the graphs its GCLoopRef names.
inline GCLoopBody loopBodyOf(const GCGraph &graph, gc_node_ref_t ref) {
    return GCLoopBody{
        .condGraph       = graph.loopCondIdOf(ref),
        .bodyGraph       = graph.loopBodyIdOf(ref),
        .kind            = graph.loopKindOf(ref),
        .iterableForm    = graph.loopIterableFormOf(ref),
        .collectionCount = graph.loopCollectionCountOf(ref),
    };
}

/// The Programs and graph versions of a context (program.cpp). Both live in the graph space and
/// are freed when nothing reaches them: a graph version when no live Program holds it, a Program
/// when no root, no function value and no Program that copies it names it.
///
/// What roots a Program: being the installed program of the context, a detached program a back
/// end is running (trackDetached), a pinned one (operator definitions), a program the pipeline
/// has made and not yet settled (reserve ... settle), and a program a running call was entered
/// through (rootForRun). Everything else is reached from those: a Program traces its graphs, a
/// graph its static values, a function value its Program.
///
/// Two collections free what is unreached, and both mark through the heap, so that a function
/// value of a Program keeps it. Between passes (nothing runs) collect() has the heap marked
/// without sweeping it (GenerationalAllocatorWithGC::collectGraphSpace). While a program runs
/// the collector of run-time values does it with a major collection (sweepMarked). The manager
/// frees the Programs and graphs nothing marked.
/// The floor of the graph-space collection (`--graph-gc-mb <megabytes>`, default 64): the graph
/// space asks for a major collection when it holds this much more than it did after the last
/// one. Chosen by the command line before the first program exists; a manager made later reads
/// it. The size after the last collection replaces it once it is larger.
void setGraphCollectionFloorBytes(size_t bytes);

class GCGraphManager {
  public:
    GCGraphManager();
    ~GCGraphManager();

    /// A new, empty Program of the graph space, owned by the manager and rooted until settle();
    /// the caller binds its graphs (Program::bind) while it builds them.
    Program *reserve();
    /// Installs `program` as the program of the context, the first one or a replacement; what
    /// the previous program held and nothing else holds is freed.
    void adoptRoot(const Program *program);
    void replaceRoot(const Program *program);
    /// replaceRoot, but only while `expected` is still the installed program: a commit that
    /// rewrote a program that is no longer the context's publishes only if nothing overtook it
    /// (design 4.3.1, which asks for one compare-and-swap instead of a lock). Answers whether it
    /// replaced it.
    bool replaceRootIfCurrent(const Program *expected, const Program *program);
    /// The installed program, nullptr when there is none.
    const Program *root() const;
    // The collector reads this stable vector only when collections are allowed.
    // Runtime publishers hold NoCollectionScope while changing it.
    std::vector<camel::core::rtdata::Object *> &gcRoots() { return gcRoots_; }
    const std::vector<camel::core::rtdata::Object *> &gcRoots() const { return gcRoots_; }

    /// Drops the installed program.
    void clear();

    /// Keeps a program that a rewrite of a detached root produced (a function being exported,
    /// see RuntimeGraphDraftSession::commit) alive until releaseDetached().
    void trackDetached(const Program *program);
    /// Lets go of the tracked detached programs.
    void releaseDetached();
    /// Ends the rooting of the programs the pipeline made since the last settle: from here they
    /// live as long as something reaches them.
    void settle(const Program *keep = nullptr);
    /// Roots `program` while a call that was entered through it runs (and unroots it).
    void rootForRun(const Program *program);
    void unrootForRun(const Program *program);
    /// While programs run, the explicit collection is off: the heap may hold their function
    /// values, which only the collector of run-time values sees.
    void enterRun() { ++running_; }
    void leaveRun() { --running_; }
    /// Frees a graph no program holds (a commit's encoding it did not need).
    void release(GCGraph *graph);
    /// The newest program of the manager that holds this version of `graph`, nullptr when none
    /// does. A version a commit left unchanged is held by the program before it too, and names
    /// the graphs it calls by id: what it calls is what the program it is read in holds. This
    /// answers for a view of the latest program (a dump, the debugger); whoever runs or
    /// rewrites a graph has the program it was given.
    const Program *programOf(const GCGraph *graph) const;
    /// Keeps a program that belongs to no context program alive for as long as the manager:
    /// the definition graphs of operators (camel/runtime/definitions.h).
    void pin(const Program *program);

    /// Frees the Programs and graph versions nothing reaches, marking from the roots and the
    /// heap. Does nothing while a program runs.
    void collect();
    /// The collector of run-time values has marked what it reaches: frees the marked-not Programs
    /// and graph versions.
    void sweepMarked();

    /// Called by Program::bind for a program the manager owns.
    void noteGraph(GCGraph *graph);
    /// Called for a graph a rewrite encoded but has not bound into a program: a graph version a
    /// session makes on the way (a specialization) is used by the drafts of the session before a
    /// commit binds it, or not at all. The collector knows it from here on, but does not free it
    /// until the pass settles, so that the session's work table can still reach it; a graph no
    /// program binds by then is freed like any other.
    void noteUnbound(GCGraph *graph);
    /// Takes `graph` out of the manager's books without destructing it: the memory of a graph
    /// that was bound before it was built.
    void forget(GCGraph *graph);

  private:
    friend class Program;
    void rebuildGcRoots();
    static void freeGraph(GCGraph *graph);
    static void freeProgram(Program *program);
    void
    freeDead(const std::vector<Program *> &deadPrograms, const std::vector<GCGraph *> &deadGraphs);

    mutable std::recursive_mutex mutex_;
    std::vector<Program *> programs_;
    std::unordered_set<GCGraph *> graphs_;
    /// Graphs a rewrite encoded and did not bind yet (noteUnbound): the sweep leaves them alone
    /// until the pass settles, then they are freed unless a program holds them.
    std::unordered_set<GCGraph *> unbound_;
    const Program *current_ = nullptr;
    std::vector<const Program *> detached_;
    std::vector<const Program *> pinned_;
    std::vector<const Program *> provisional_;
    std::vector<const Program *> runRoots_;
    std::vector<camel::core::rtdata::Object *> gcRoots_;
    int running_ = 0;
    /// The graph space is collected when it holds this much more than it did after the last
    /// collection (the floor, or its size then).
    size_t liveBytes_       = 0;
    size_t collectionFloor_ = size_t{64} << 20;
    void noteGrowth();
    void collectIfGrown();
    void noteSwept();
};

} // namespace camel::runtime

