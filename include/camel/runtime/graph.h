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
 * Author: Zhenjie Wei
 * Created: Apr. 07, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
 *    node has any, the numeric permissions the program granted with @numeric(...): one byte per
 *    node (nodePermissions; design principles 7.5, docs/design/op-semantics 6.3), and the
 *    attributes libraries attached to the nodes: one 32-bit interned set per node
 *    (nodeAttrs; camel/core/node_attrs.h, language reference 7.6).
 * 7. A graph refers to other graphs only through its reference table (refs(), design 3.7): a
 *    FUNC node names its callee by an index into it (GCFuncRef), and the graphs the function
 *    constants of the static area point to, the graphs written inside this one and the graphs
 *    it calls directly are entries of it, each marked with the roles it has (GCGraphRefRole).
 * 5. Hot-path data stays plain; cold debug metadata lives elsewhere (GCGraphDebugRecord).
 */

#pragma once

#include "camel/core/loop_kind.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/symbol.h"
#include "camel/core/type/intern.h"
#include "camel/runtime/graph_id.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

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
    // the m captured values, shared by the condition graph and the body graph; Ctrl inputs
    // ordering. Its payload names the two graphs (GCLoopRef). Its value is the
    // last state: the value when k = 1, the k-tuple when k > 1. A producer may also give it s
    // collection slots (payload collectionCount): the body then returns (state…, v…) each round
    // and the value is (state…, array…), the arrays holding the v of every round (see
    // camel/runtime/loop.h). Consumers read it through camel/runtime/loop.h (LoopView); those
    // that do not run it natively lower it to a tail recursion first
    // (lowerLoopsToTailRecursion), never in the shared program.
    Loop = 15,
};

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

enum GCNodeFlag : uint8_t {
    kGCNodeFlagMacro    = 1 << 0,
    kGCNodeFlagConstant = 1 << 1,
};

struct GCBranchArm {
    gc_node_ref_t head = kInvalidNodeRef;
    gc_node_ref_t tail = kInvalidNodeRef;
};

/// The operator a back end's private copy of the program applies to a function value before an
/// operator that reads graphs gets it (Program::original, OperatorTraits::inspectsGraphs):
/// the same function over the program's own graph. Implemented by the built-in executor
/// (src/builtin/operators/graph.h); a program cannot name it.
inline constexpr std::string_view kGraphOriginalUri = ":graph/original";

/// Properties a graph has besides its nodes (GCGraph::flags).
enum GCGraphFlag : uint8_t {
    /// The graph is a function that carries a vjp rule as its last closure value
    /// (autodiff::attachRule).
    kGraphFlagCarriesRule = 1 << 0,
};

/// No payload: the value of GCNode::payload for a node that carries nothing beyond its record.
inline constexpr gc_off_t kNoPayload = static_cast<gc_off_t>(-1);

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
/// graph), and how many values the loop collects into arrays (its collection slots; 0 for a
/// loop the source wrote, which has none).
struct GCLoopRef {
    camel::core::LoopKind kind = camel::core::LoopKind::While;
    uint8_t reserved0          = 0;
    uint16_t reserved1         = 0;
    uint32_t condRef           = kNoLoopGraph;
    uint32_t bodyRef           = kNoLoopGraph;
    uint32_t collectionCount   = 0;
};
static_assert(std::is_trivially_copyable_v<GCLoopRef>);
static_assert(sizeof(GCLoopRef) == 16, "A LOOP payload is two words.");

/// What a LOOP node names, as values: the graphs themselves instead of their reference-table
/// indexes (GraphDraft::loopGraphsOf, GCGraph's loopBodyOf).
struct GCLoopBody {
    class GCGraph *condGraph   = nullptr; // nullptr for a For loop
    class GCGraph *bodyGraph   = nullptr;
    camel::core::LoopKind kind = camel::core::LoopKind::While;
    uint8_t reserved0          = 0;
    uint16_t reserved1         = 0;
    uint32_t collectionCount   = 0; // the typed collection slots of the loop (see LoopView)
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
    gc_node_ref_t join       = kInvalidNodeRef;
    gc_off_t armOffset       = 0;
    gc_cnt_t armCount        = 0;
    gc_node_ref_t defaultArm = kInvalidNodeRef;
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

struct GCGraphNativePayload {
    // Arrays of the graph's own block, in the graph space (see GCGraphBuildAccess). Released
    // with the graph.
    GCNode *nodes              = nullptr; // nodeCount + 1 records (the last is a sentinel)
    uint32_t *origins          = nullptr; // nodeCount source origins (0: none)
    uint8_t *permissions       = nullptr; // nodeCount numeric permissions; null when all are 0
    uint32_t *attrs            = nullptr; // nodeCount attribute sets; null when all are 0
    gc_node_ref_t *inputs      = nullptr; // edgeCount entries
    gc_payload_word_t *payload = nullptr; // payloadWordCount words
    gc_node_ref_t *normPorts   = nullptr;
    gc_node_ref_t *withPorts   = nullptr;
    gc_node_ref_t *closure     = nullptr;
    GCBranchArm *branchArms    = nullptr;
    gc_cnt_t nodeCount         = 0;
    gc_cnt_t edgeCount         = 0;
    gc_cnt_t payloadWordCount  = 0;
    gc_cnt_t normPortCount     = 0;
    gc_cnt_t withPortCount     = 0;
    gc_cnt_t closureCount      = 0;
    gc_cnt_t branchArmCount    = 0;
    gc_node_ref_t entryNode    = kInvalidNodeRef;
    gc_node_ref_t exitNode     = kInvalidNodeRef;
    gc_node_ref_t outputNode   = kInvalidNodeRef;
    gc_node_ref_t returnNode   = kInvalidNodeRef;
    GCReturnKind returnKind    = GCReturnKind::None;

    constexpr bool empty() const { return nodes == nullptr; }
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
    /// Numeric permissions (camel/core/operator_semantics.h, PermissionMask) the program granted
    /// to node `ref`, 0 when none; the graph has no permission column when no node has any.
    uint8_t nodePermissions(gc_node_ref_t ref) const;
    /// True when some node of the graph has numeric permissions.
    bool hasNodePermissions() const { return nativePayload_.permissions != nullptr; }
    /// The attributes (camel/core/node_attrs.h, AttrSetId) of node `ref`, 0 when none; the
    /// graph has no attribute column when no node has any.
    uint32_t nodeAttrs(gc_node_ref_t ref) const;
    /// True when some node of the graph has attributes.
    bool hasNodeAttrs() const { return nativePayload_.attrs != nullptr; }
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
    GCGraph *outerGraph() const;
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
    bool isRoot() const;
    ::Tuple *staticArea() const { return staticArea_; }

    /// The reference table: every graph this one refers to, once each (see the file comment).
    std::span<GCGraph *const> refs() const { return {refs_, refCount_}; }
    /// The roles of entry `index` of the reference table (GCGraphRefRole bits).
    uint8_t refRoles(size_t index) const { return refRoles_[index]; }
    /// The entries of the reference table with role `role`, in table order.
    std::vector<GCGraph *> refsWithRole(uint8_t role) const;
    /// The graphs called directly, written inside this one, referred to by a function
    /// constant: views of the reference table by role.
    std::vector<GCGraph *> dependencies() const { return refsWithRole(kGraphRefCallee); }
    std::vector<GCGraph *> subGraphs() const { return refsWithRole(kGraphRefNested); }
    std::vector<GCGraph *> staticGraphRefs() const { return refsWithRole(kGraphRefStatic); }
    std::span<const slot_t> staticSlots() const;

    bool hasNodePayload() const;
    const GCGraphNativePayload *nodePayload() const;
    /// Number of nodes; NodeIds are 0 .. nodeCount() - 1.
    gc_cnt_t nodeCount() const { return nativePayload_.nodeCount; }
    /// Number of edges (input table entries).
    gc_cnt_t edgeCount() const { return nativePayload_.edgeCount; }
    gc_cnt_t branchArmCount() const;

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
        return ref < nativePayload_.nodeCount ? nativePayload_.nodes + ref : nullptr;
    }
    bool containsNodeRef(gc_node_ref_t ref) const { return ref < nativePayload_.nodeCount; }
    /// The NodeId of a record of this graph, kInvalidNodeRef for any other pointer.
    gc_node_ref_t nodeRef(const GCNode *node) const;
    GCGraph *directCalleeGraphOf(gc_node_ref_t ref) const;
    /// LOOP node `ref`: its condition graph (nullptr for a For loop, or when `ref` is not a
    /// LOOP node) and its body graph (nullptr when `ref` is not a LOOP node).
    GCGraph *loopCondGraphOf(gc_node_ref_t ref) const;
    GCGraph *loopBodyGraphOf(gc_node_ref_t ref) const;
    /// LOOP node `ref`: its form. The node must be a LOOP node.
    camel::core::LoopKind loopKindOf(gc_node_ref_t ref) const {
        return nodeBodyAs<GCLoopRef>(ref)->kind;
    }
    /// LOOP node `ref`: how many values it collects into arrays each round (0 for a loop the
    /// source wrote). The node must be a LOOP node.
    uint32_t loopCollectionCountOf(gc_node_ref_t ref) const {
        return nodeBodyAs<GCLoopRef>(ref)->collectionCount;
    }
    gc_node_ref_t matchedJoinOutputOf(gc_node_ref_t ref) const;

    /// The payload of node `ref` as a `Body` (see the payload structs above). The node must
    /// have one: every node of the kinds FUNC, CALL, ACCS, FILL, BRCH, JOIN, OPER and LOOP does.
    template <typename Body> const Body *nodeBodyAs(gc_node_ref_t ref) const {
        static_assert(
            std::is_trivially_copyable_v<Body>,
            "GC node bodies must be trivially copyable.");
        const GCNode *n = node(ref);
        ASSERT(n != nullptr && n->hasPayload(), "GCGraph node-body lookup of a node without one.");
        return reinterpret_cast<const Body *>(nativePayload_.payload + n->payload);
    }
    /// The payload bytes of node `ref` (rounded up to whole words); empty when it has none.
    std::span<const std::byte> nodePayloadBytes(gc_node_ref_t ref) const;

    std::span<const gc_node_ref_t> normInputsOf(gc_node_ref_t ref) const {
        const GCNode *n = node(ref);
        return n ? inputRun(n->inBegin, n->withBegin) : std::span<const gc_node_ref_t>();
    }
    std::span<const gc_node_ref_t> withInputsOf(gc_node_ref_t ref) const {
        const GCNode *n = node(ref);
        return n ? inputRun(n->withBegin, n->ctrlBegin) : std::span<const gc_node_ref_t>();
    }
    std::span<const gc_node_ref_t> ctrlInputsOf(gc_node_ref_t ref) const {
        const GCNode *n = node(ref);
        // The sentinel record after the last node ends the last node's run.
        return n ? inputRun(n->ctrlBegin, (n + 1)->inBegin) : std::span<const gc_node_ref_t>();
    }

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
    std::span<const GCBranchArm> branchArmsOf(gc_node_ref_t brchRef) const;

    class NodeIterator {
      public:
        NodeIterator(const GCGraph *graph, gc_node_ref_t ref) : graph_(graph), ref_(ref) {}

        const GCNode *operator*() const { return graph_->node(ref_); }
        NodeIterator &operator++() {
            ++ref_;
            return *this;
        }
        bool operator==(const NodeIterator &other) const {
            return graph_ == other.graph_ && ref_ == other.ref_;
        }
        bool operator!=(const NodeIterator &other) const { return !(*this == other); }
        gc_node_ref_t ref() const { return ref_; }

      private:
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

    /// Calls `visitor` for every graph of the reference table.
    template <typename Visitor> void traceGraphs(Visitor &&visitor) const {
        for (GCGraph *graph : refs()) {
            if (graph) {
                visitor(graph);
            }
        }
    }

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

    GCGraph(
        GraphId id, const GCGraphDerivation &derivation, GCGraphDebugRecord *debugRecord, camel::core::type::FunctionType *funcType,
        camel::core::type::TupleType *runtimeDataType, camel::core::type::TupleType *staticDataType,
        camel::core::type::TupleType *closureType, GCGraph *outerGraph, GCGraph **refs,
        uint8_t *refRoles, gc_cnt_t refCount, const GCGraphNativePayload &payload,
        ::Tuple *staticArea);

    GraphId id_                                    = kInvalidGraphId;
    GraphId typeSpecializationRoot_                = kInvalidGraphId; // see typeSpecializationRoot()
    uint8_t flags_                                 = 0;               // see flags()
    GCGraphDebugRecord *debug_                     = nullptr;
    camel::core::type::FunctionType *funcType_     = nullptr;
    camel::core::type::TupleType *runtimeDataType_ = nullptr;
    camel::core::type::TupleType *staticDataType_  = nullptr;
    camel::core::type::TupleType *closureType_     = nullptr;
    GCGraph *outerGraph_                           = nullptr;
    uint64_t revision_                             = 0; // see revision()
    GCGraph **refs_                                = nullptr;
    uint8_t *refRoles_                             = nullptr;
    gc_cnt_t refCount_                             = 0;
    std::span<const gc_node_ref_t> inputRun(gc_off_t begin, gc_off_t end) const {
        return {nativePayload_.inputs + begin, static_cast<size_t>(end - begin)};
    }

    GCGraphNativePayload nativePayload_{};
    ::Tuple *staticArea_ = nullptr;
    // Built by uses() on first demand, owned by the graph.
    mutable std::atomic<GCUseIndex *> uses_{nullptr};
    // Built by execPlan() on first demand, owned by the graph.
    mutable std::atomic<class ExecPlan *> execPlan_{nullptr};
};

/// The draft payload of the LOOP node `ref` of a frozen graph: the graphs its GCLoopRef names.
inline GCLoopBody loopBodyOf(const GCGraph &graph, gc_node_ref_t ref) {
    return GCLoopBody{
        .condGraph       = graph.loopCondGraphOf(ref),
        .bodyGraph       = graph.loopBodyGraphOf(ref),
        .kind            = graph.loopKindOf(ref),
        .collectionCount = graph.loopCollectionCountOf(ref),
    };
}

/// Owns the Programs of a context and keeps the graphs they name alive (program.cpp).
class GCGraphManager {
  public:
    GCGraphManager() = default;
    ~GCGraphManager();

    /// A new, empty Program, owned by the manager for its lifetime; the caller binds its graphs
    /// (Program::bind) while it builds them.
    Program *reserve();
    /// Installs `program` as the program of the context, the first one or a replacement: the
    /// graphs of the previous program that `program` does not hold are freed.
    void adoptRoot(const Program *program);
    void replaceRoot(const Program *program);
    /// The installed program, nullptr when there is none.
    const Program *root() const;
    // The collector reads this stable vector only when collections are allowed.
    // Runtime publishers hold NoCollectionScope while changing it.
    std::vector<camel::core::rtdata::Object *> &gcRoots() { return gcRoots_; }
    const std::vector<camel::core::rtdata::Object *> &gcRoots() const { return gcRoots_; }

    void clear();

    /// Keeps a program that a rewrite of a detached root produced (a function being exported,
    /// see RuntimeGraphDraftSession::commit), and its graphs, alive and traced until
    /// releaseDetached().
    void trackDetached(const Program *program);
    /// Frees the graphs of the tracked detached programs that the program does not hold.
    void releaseDetached();
    /// Keeps a program that belongs to no context program alive and traced for as long as the
    /// manager: the definition graphs of operators (camel/runtime/definitions.h). Neither
    /// clear() nor releaseDetached() frees it; a rewrite that uses one encodes a copy into the
    /// program.
    void pin(const Program *program);

  private:
    void rebuildGcRoots();
    static void freeGraph(GCGraph *graph);

    // Publishing a detached graph can happen on different execution workers.
    // Recursive because the lifecycle operations below compose (adopt/replace,
    // destruction/release/clear); the lock is never held through graph execution.
    mutable std::recursive_mutex mutex_;
    std::vector<std::unique_ptr<Program>> programs_;
    const Program *current_ = nullptr;
    /// The graphs the manager frees when the program is replaced: those of the installed
    /// program and of the programs its detached copies were made from.
    std::vector<GCGraph *> owned_;
    std::vector<const Program *> detached_;
    std::vector<const Program *> pinned_;
    std::vector<camel::core::rtdata::Object *> gcRoots_;
};

} // namespace camel::runtime
