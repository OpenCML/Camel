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
 * Created: Apr. 10, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Runtime graph draft model.
 *
 * `GCGraph` is the immutable execution layout. `GraphDraft` is the editable
 * decode form used by
 * runtime rewrites. The draft intentionally keeps node-
 * centric adjacency and stable node
 * ids so passes can mutate the graph
 * without invalidating external node pointers.
 * The draft
 * node header stays structurally close to `GCNode`, but the storage model is different:
 * - one draft node owns its payload bytes and all six adjacency lists
 * - the node header address
 * is stable for the lifetime of the draft
 * - payload bytes and adjacency arrays live in a tail
 * block that may be
 *   replaced internally without moving the node header
 * - both node headers
 * and tail blocks come from slab-backed arenas with
 *   oversize fallback blocks
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/node_ref.h"
#include "camel/runtime/program.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::runtime {

enum class DraftEdgeKind : uint8_t {
    Norm,
    With,
    Ctrl,
};

enum class DraftNodeStorageClass : uint8_t {
    Slab64,
    Slab96,
    Slab128,
    Oversize,
};

struct DraftBrchPayload {
    gc_node_ref_t join = kInvalidNodeRef;
    gc_cnt_t armCount  = 0;
};
static_assert(std::is_trivially_copyable_v<DraftBrchPayload>);

struct DraftNodeHeader {
    class GraphDraft *owner           = nullptr;
    gc_node_ref_t selfId              = kInvalidNodeRef;
    gc_slot_idx_t dataIndex           = 0;
    uint32_t payloadBytes             = 0;
    uint32_t capacityBytes            = 0;
    camel::core::type::Type *dataType = nullptr;

    gc_cnt_t normInputCount = 0;
    gc_cnt_t withInputCount = 0;
    gc_cnt_t ctrlInputCount = 0;

    GCNodeKind kind                  = GCNodeKind::Data;
    uint8_t runtimeFlags             = 0;
    DraftNodeStorageClass storageCls = DraftNodeStorageClass::Slab64;
    // The numeric permissions the program granted to the node with @numeric(...)
    // (camel/core/operator_semantics.h, PermissionMask; design principles 7.5), 0 when none.
    // Graph content, not a derived fact: rewrites keep it as they keep the origin (a node that
    // replaces another inherits it, inlined and cloned nodes keep their own), and the encoder
    // writes it to the frozen graph's permission column (GCGraph::nodePermissions).
    uint8_t permissions = 0;
    // The node's attributes (camel/core/node_attrs.h, AttrSetId), 0 when none. Graph content,
    // kept by rewrites like the permissions above; the encoder writes it to the frozen graph's
    // attribute column (GCGraph::nodeAttrs).
    uint32_t attrs = 0;
    // Source origin (camel::source::origin_id_t) of the node, 0 when unknown. Rewrites keep it:
    // a node that replaces another inherits its origin, inlined nodes keep the callee's.
    uint64_t origin = 0;
    // The region the node is in (GCRegion; 0 for the body of the function). Moved with the node
    // by rewrites that keep it in place; a node a rewrite adds is in the region it names.
    uint32_t region = 0;
    constexpr size_t usedBytes() const {
        return static_cast<size_t>(payloadBytes) +
               (static_cast<size_t>(normInputCount) + static_cast<size_t>(withInputCount) +
                static_cast<size_t>(ctrlInputCount)) *
                   sizeof(gc_node_ref_t);
    }
};

struct DraftNode {
    DraftNodeHeader header;
    std::byte *tail = nullptr;
};

struct DraftNodeInit {
    gc_slot_idx_t dataIndex           = 0;
    camel::core::type::Type *dataType = nullptr;
    GCNodeKind kind                   = GCNodeKind::Data;
    uint8_t runtimeFlags              = 0;
    uint64_t origin                   = 0;
    uint8_t permissions               = 0; // see DraftNodeHeader::permissions
    uint32_t attrs                    = 0; // see DraftNodeHeader::attrs
    uint32_t region                   = 0; // see DraftNodeHeader::region; 0 on a rewrite: kept
    std::span<const std::byte> payload{};
    std::span<const gc_node_ref_t> normInputs{};
    std::span<const gc_node_ref_t> withInputs{};
    std::span<const gc_node_ref_t> ctrlInputs{};
};

class DraftNodeView {
  public:
    static std::span<std::byte> payload(DraftNode *node);
    static std::span<const std::byte> payload(const DraftNode *node);

    static std::span<gc_node_ref_t> normInputs(DraftNode *node);
    static std::span<const gc_node_ref_t> normInputs(const DraftNode *node);
    static std::span<gc_node_ref_t> withInputs(DraftNode *node);
    static std::span<const gc_node_ref_t> withInputs(const DraftNode *node);
    static std::span<gc_node_ref_t> ctrlInputs(DraftNode *node);
    static std::span<const gc_node_ref_t> ctrlInputs(const DraftNode *node);

    template <typename Body> static Body *payloadAs(DraftNode *node) {
        auto bytes = payload(node);
        ASSERT(bytes.size() >= sizeof(Body), "Draft payload is smaller than requested body.");
        return reinterpret_cast<Body *>(bytes.data());
    }

    template <typename Body> static const Body *payloadAs(const DraftNode *node) {
        auto bytes = payload(node);
        ASSERT(bytes.size() >= sizeof(Body), "Draft payload is smaller than requested body.");
        return reinterpret_cast<const Body *>(bytes.data());
    }
};

class DraftNodePool {
  public:
    DraftNodePool() = default;
    ~DraftNodePool();

    DraftNodePool(const DraftNodePool &)            = delete;
    DraftNodePool &operator=(const DraftNodePool &) = delete;

    DraftNode *allocNode();
    std::byte *allocTail(size_t tailBytes, DraftNodeStorageClass *storageClass = nullptr);
    void clear();

  private:
    struct SlabArena {
        size_t blockBytes = 0;
        size_t cursor     = 0;
        std::vector<std::unique_ptr<std::byte[]>> pages;
    };

    static constexpr size_t kPageBytes = 4096;

    static size_t capacityForStorageClass(DraftNodeStorageClass storageClass);
    static DraftNodeStorageClass classify(size_t tailBytes);
    std::byte *allocFromArena(SlabArena &arena);

    SlabArena nodeArena_{sizeof(DraftNode), 0, {}};
    SlabArena slab64_{64, 0, {}};
    SlabArena slab96_{96, 0, {}};
    SlabArena slab128_{128, 0, {}};
    std::vector<void *> oversizeBlocks_;
};

/// The nodes that read a node, by edge group. A user appears once per group however often it
/// reads the node. Kept apart from the node's storage so that adding a user costs the user alone.
///
/// Order: users are listed in the order they were linked. A list that has grown past
/// kIndexedUsers also has an index from user to place, so that a user is found, added and
/// removed at constant cost; there, removing a user moves the last one into its place. The
/// order is a function of the sequence of edits alone, so equal edits give equal lists.
struct DraftUserLists {
    static constexpr size_t kIndexedUsers = 32;

    std::vector<gc_node_ref_t> norm;
    std::vector<gc_node_ref_t> with;
    std::vector<gc_node_ref_t> ctrl;
    std::unique_ptr<std::unordered_map<gc_node_ref_t, uint32_t>> places[3];

    std::vector<gc_node_ref_t> &of(DraftEdgeKind kind) {
        return kind == DraftEdgeKind::Norm ? norm : kind == DraftEdgeKind::With ? with : ctrl;
    }
    const std::vector<gc_node_ref_t> &of(DraftEdgeKind kind) const {
        return kind == DraftEdgeKind::Norm ? norm : kind == DraftEdgeKind::With ? with : ctrl;
    }
    /// The index of the `kind` list, built when the list is long enough to want one.
    std::unordered_map<gc_node_ref_t, uint32_t> *index(DraftEdgeKind kind) {
        auto &items = of(kind);
        auto &place = places[static_cast<size_t>(kind)];
        if (!place && items.size() >= kIndexedUsers) {
            place = std::make_unique<std::unordered_map<gc_node_ref_t, uint32_t>>();
            place->reserve(items.size() * 2);
            for (uint32_t i = 0; i < items.size(); ++i) {
                place->emplace(items[i], i);
            }
        }
        return place.get();
    }
};

class GraphDraft {
  public:
    /// A draft of `graph`, a graph of `program`: the draft reads the graphs it refers to (the
    /// callees of its FUNC nodes) in `program`. Graphs are named in a draft by GraphId.
    static std::unique_ptr<GraphDraft> decode(const Program &program, const GCGraph *graph);

    /// The program the draft resolves the graphs it refers to in; null for a draft the front
    /// end builds, whose graphs do not exist yet.
    const Program *program() const { return program_; }
    void setProgram(const Program *program) { program_ = program; }
    /// The graph the draft's graph is written in (GCGraph::outerGraphId).
    GraphId outerGraphId() const { return outerGraphId_; }
    void setOuterGraphId(GraphId outer) {
        ++mutations_;
        outerGraphId_ = outer;
    }

    GraphDraft()  = default;
    ~GraphDraft() = default;

    GraphDraft(const GraphDraft &)            = delete;
    GraphDraft &operator=(const GraphDraft &) = delete;

    /// The graph the draft is a draft of: decode takes the id of the graph it decodes, and the
    /// graph that encode builds carries it (a draft keeps the id of the graph it edits). A draft
    /// of a new graph is given one with setId (Context::graphIds) before it is encoded.
    GraphId id() const { return id_; }
    void setId(GraphId id) {
        ++mutations_;
        id_ = id;
    }
    /// GCGraph::typeSpecializationRoot and GCGraph::flags of the graph the draft encodes to;
    /// decode takes them from the graph it decodes.
    GraphId typeSpecializationRoot() const { return typeSpecializationRoot_; }
    void setTypeSpecializationRoot(GraphId root) {
        ++mutations_;
        typeSpecializationRoot_ = root;
    }
    uint8_t flags() const { return flags_; }
    void setFlags(uint8_t flags) {
        ++mutations_;
        flags_ = flags;
    }
    camel::core::type::FunctionType *funcType() const { return funcType_; }
    /// The names the function captures, in capture order (GCGraph::captureNames).
    std::span<const camel::core::SymId> captureNames() const { return captureNames_; }
    /// The frame layout: the type of each frame slot, slot 0 (reserved) included. Empty when the
    /// graph has no frame layout, or one without slots (hasFrameLayout tells them apart).
    std::span<camel::core::type::Type *const> runtimeSlotTypes() const { return runtimeSlotTypes_; }
    /// The frame layout as the encoder takes it: nullopt when the graph has none.
    std::optional<std::span<camel::core::type::Type *const>> frameSlotTypes() const {
        return hasFrameLayout_ ? std::optional(runtimeSlotTypes()) : std::nullopt;
    }
    camel::core::type::TupleType *closureType() const { return closureType_; }
    size_t frameSize() const {
        ASSERT(hasFrameLayout(), "Runtime draft frame layout is not available.");
        return sizeof(camel::core::context::Frame) + sizeof(slot_t) * runtimeSlotTypes_.size();
    }
    bool hasFrameLayout() const { return hasFrameLayout_; }
    bool isMacroGraph() const { return funcType_ != nullptr && funcType_->modifiers().macro(); }
    gc_cnt_t nodeCount() const { return liveNodeCount_; }
    size_t nodeSlotCount() const { return nodesById_.size(); }
    bool empty() const { return liveNodeCount_ == 0; }
    /// Changes whenever the graphs this draft refers to may have changed: a node was added,
    /// erased or rewritten, a payload was handed out for writing, a constant or a dependency
    /// was added. Equal revisions of one draft mean the same references.
    uint64_t revision() const { return revision_; }
    /// The graph this draft was decoded from; nullptr for a draft that was built.
    const GCGraph *baseGraph() const { return base_; }
    /// Whether an encoding of this draft numbers the nodes as they are, leaving the place of
    /// an erased node as a Dead record: a draft decoded from a graph keeps its ids, so that
    /// the version it becomes names its nodes as the graph did. Past half the places dead,
    /// or for a draft that was built and not decoded, the nodes are numbered anew.
    bool keepsNodeIds() const {
        return decoded_ && 2 * static_cast<size_t>(nodesById_.size() - liveNodeCount_) <=
                               nodesById_.size();
    }
    /// Counts the calls that may have changed the draft since it was decoded (every call of a
    /// member that is not const): a draft of none is the graph it was decoded from.
    uint64_t mutations() const { return mutations_; }
    /// A counter the draft also advances whenever its revision() does: a session keeps one
    /// for all its drafts, to know whether any of them changed what it refers to.
    void setRefsEpoch(uint64_t *epoch) { refsEpoch_ = epoch; }

    bool containsNode(gc_node_ref_t id) const;
    bool containsNode(const DraftNode *node) const;
    bool alive(gc_node_ref_t id) const;
    bool alive(const DraftNode *node) const;
    const DraftNode *node(gc_node_ref_t id) const;
    DraftNode *node(gc_node_ref_t id);
    gc_node_ref_t nodeId(const DraftNode *node) const;
    /// The handle of live node `id`; the null handle when `id` is not a live node.
    DraftNodeRef ref(gc_node_ref_t id) const {
        return alive(id) ? DraftNodeRef(const_cast<GraphDraft *>(this), id) : DraftNodeRef();
    }
    const DraftNodeHeader *header(gc_node_ref_t id) const;
    const DraftNodeHeader *header(const DraftNode *node) const;
    gc_node_ref_t draftIdOfSourceRef(gc_node_ref_t sourceRef) const;
    gc_node_ref_t sourceRefOf(gc_node_ref_t draftId) const;
    std::span<const std::byte> payloadOf(gc_node_ref_t id) const;
    std::span<const std::byte> payloadOf(const DraftNode *node) const;
    /// The number of arms of the BRCH `id` (0 for another node).
    gc_cnt_t armCountOf(gc_node_ref_t id) const;

    gc_node_ref_t entryNode() const { return entry_; }
    gc_node_ref_t exitNode() const { return exit_; }
    gc_node_ref_t outputNode() const { return output_; }
    gc_node_ref_t returnNode() const { return returnNode_; }
    GCReturnKind returnKind() const { return returnKind_; }

    std::span<const gc_node_ref_t> normPorts() const { return normPorts_; }
    std::span<const gc_node_ref_t> withPorts() const { return withPorts_; }
    std::span<const gc_node_ref_t> closureNodes() const { return closureNodes_; }

    std::span<const slot_t> staticSlots() const { return staticSlots_; }
    std::span<camel::core::type::Type *const> staticSlotTypes() const { return staticSlotTypes_; }
    const std::vector<GraphId> &dependencies() const { return dependencies_; }
    const std::vector<GraphId> &subGraphs() const { return subGraphs_; }
    /// Forgets the graphs in `graphs` that no node calls any more (the graphs that held code now
    /// spliced into this one).
    void dropUncalledGraphs(std::span<const GraphId> graphs);
    const std::vector<GraphId> &staticGraphRefs() const { return staticGraphRefs_; }

    std::span<const gc_node_ref_t> normInputsOf(gc_node_ref_t id) const;
    std::span<const gc_node_ref_t> normInputsOf(const DraftNode *node) const;
    std::span<const gc_node_ref_t> withInputsOf(gc_node_ref_t id) const;
    std::span<const gc_node_ref_t> withInputsOf(const DraftNode *node) const;
    std::span<const gc_node_ref_t> ctrlInputsOf(gc_node_ref_t id) const;
    std::span<const gc_node_ref_t> ctrlInputsOf(const DraftNode *node) const;
    std::span<const gc_node_ref_t> normUsersOf(gc_node_ref_t id) const;
    std::span<const gc_node_ref_t> normUsersOf(const DraftNode *node) const;
    std::span<const gc_node_ref_t> withUsersOf(gc_node_ref_t id) const;
    std::span<const gc_node_ref_t> withUsersOf(const DraftNode *node) const;
    std::span<const gc_node_ref_t> ctrlUsersOf(gc_node_ref_t id) const;
    std::span<const gc_node_ref_t> ctrlUsersOf(const DraftNode *node) const;
    bool isControlAnchor(gc_node_ref_t id) const;
    bool isControlAnchor(const DraftNode *node) const;
    gc_node_ref_t resolveForwardedValueRef(gc_node_ref_t id) const;
    gc_node_ref_t resolveForwardedCtrlRef(gc_node_ref_t id) const;
    std::span<std::byte> mutablePayloadOf(gc_node_ref_t id);
    std::span<std::byte> mutablePayloadOf(DraftNode *node);
    /// Points the OPER node `id` at the operator `uri` names: it runs that operator from now on,
    /// and the operator pointer the node was created with is dropped (the URI is interned).
    void retargetOperNode(gc_node_ref_t id, std::string_view uri);

    void setFuncType(camel::core::type::FunctionType *funcType) {
        ++mutations_;
        funcType_ = funcType;
    }
    void setCaptureNames(std::span<const camel::core::SymId> names) {
        ++mutations_;
        captureNames_.assign(names.begin(), names.end());
    }
    /// Records that the function captures `name` (one entry per capture port, in order).
    void addCaptureName(std::string_view name);
    /// Gives the graph this frame layout (possibly one without slots).
    void setRuntimeSlotTypes(std::span<camel::core::type::Type *const> slotTypes) {
        ++mutations_;
        runtimeSlotTypes_.assign(slotTypes.begin(), slotTypes.end());
        hasFrameLayout_ = true;
    }
    void setClosureType(camel::core::type::TupleType *closureType) {
        ++mutations_;
        closureType_ = closureType;
    }

    gc_node_ref_t addNode(const DraftNodeInit &init);
    gc_node_ref_t
    addDataNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex, uint8_t runtimeFlags = 0);
    gc_node_ref_t
    addPortNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex, uint8_t runtimeFlags = 0);
    gc_node_ref_t addCastNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex = 0);
    gc_node_ref_t addCopyNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex = 0);
    gc_node_ref_t
    addFillNode(camel::core::type::Type *type, const GCFillBody &body, gc_slot_idx_t dataIndex = 0);
    gc_node_ref_t addFillNode(
        camel::core::type::Type *type, std::span<const std::byte> payload,
        gc_slot_idx_t dataIndex = 0);
    gc_node_ref_t
    addAccsNode(camel::core::type::Type *type, uint32_t tupleIndex, gc_slot_idx_t dataIndex = 0);
    gc_node_ref_t addAccsNode(
        camel::core::type::Type *type, std::string_view structKey, gc_slot_idx_t dataIndex = 0);
    gc_node_ref_t addBrchNode(
        camel::core::type::Type *type, gc_node_ref_t joinRef, gc_cnt_t armCount,
        gc_slot_idx_t dataIndex = 0);
    gc_node_ref_t addJoinNode(
        camel::core::type::Type *type, gc_node_ref_t brchRef, gc_cnt_t armCount,
        gc_slot_idx_t dataIndex = 0);
    gc_node_ref_t addCallNode(
        camel::core::type::Type *type, const GCCallBody &body = {}, gc_slot_idx_t dataIndex = 0);
    /// The graph FUNC node `id` calls: its id (invalid while it names none), and the graph the
    /// program of the draft holds under it (nullptr when none).
    GraphId calleeIdOf(gc_node_ref_t id) const;
    GCGraph *calleeGraphOf(gc_node_ref_t id) const;
    void setCalleeGraph(gc_node_ref_t id, GraphId callee);
    /// What LOOP node `id` names: its condition graph (invalid for a For loop), its body graph,
    /// its form and its collection count.
    GCLoopBody loopGraphsOf(gc_node_ref_t id) const;
    void setLoopGraphs(gc_node_ref_t id, GraphId cond, GraphId body);
    /// The graphs the payloads of FUNC and LOOP nodes index (GCFuncRef, GCLoopRef): the
    /// reference table of the graph the draft was decoded from, then the graphs added since.
    std::span<const GraphId> payloadGraphs() const { return payloadGraphs_; }
    /// The index of `graph` in payloadGraphs(), added when it is not there; kNoLoopGraph for
    /// an invalid id.
    uint32_t payloadGraphIndexOf(GraphId graph);
    gc_node_ref_t addFuncNode(
        GraphId calleeGraph, camel::core::type::Type *type, uint8_t runtimeFlags = 0,
        gc_slot_idx_t dataIndex = 0);
    /// A LOOP node calling `body.condGraph` and `body.bodyGraph` (camel/runtime/loop.h); the
    /// caller links its inputs and keeps the two graphs among the draft's dependencies.
    gc_node_ref_t
    addLoopNode(const GCLoopBody &body, camel::core::type::Type *type, gc_slot_idx_t dataIndex = 0);
    gc_node_ref_t addOperNode(
        camel::core::type::Type *type, operator_t op, std::string_view uri,
        gc_slot_idx_t dataIndex = 0);
    gc_node_ref_t addSyncNode();
    gc_node_ref_t addGateNode(camel::core::type::Type *type, gc_slot_idx_t dataIndex = 0);
    gc_node_ref_t addDrefNode(camel::core::type::Type *type = nullptr, gc_slot_idx_t dataIndex = 0);

    void eraseNode(gc_node_ref_t id);
    void setNormInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs);
    void setWithInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs);
    void setCtrlInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs);
    void appendInput(DraftEdgeKind kind, gc_node_ref_t userId, gc_node_ref_t inputId);
    bool unlinkInput(DraftEdgeKind kind, gc_node_ref_t userId, gc_node_ref_t inputId);
    void replaceInput(
        DraftEdgeKind kind, gc_node_ref_t userId, gc_node_ref_t oldInputId,
        gc_node_ref_t newInputId);
    void replaceAllNormUses(gc_node_ref_t oldId, gc_node_ref_t newId);
    void replaceAllWithUses(gc_node_ref_t oldId, gc_node_ref_t newId);
    void replaceAllCtrlUses(gc_node_ref_t oldId, gc_node_ref_t newId);
    void replaceAllValueUses(gc_node_ref_t oldId, gc_node_ref_t newId);
    /// Removes GATEs that wait on nothing (no control input): such a gate only forwards its value,
    /// so its users read the value directly. Rewrites that remove what a gate waited on (a pruned
    /// branch, a constant an inlined gate was ordered after) leave them behind. A gate that is the
    /// head or tail of a branch arm is kept; when it waits on nothing it is made to wait on its
    /// BRCH. Returns the number removed.
    size_t dissolveUnorderedGates();
    void setNodeDataType(gc_node_ref_t id, camel::core::type::Type *type);
    void setNodeDataIndex(gc_node_ref_t id, gc_slot_idx_t dataIndex);
    void setNodeRuntimeFlags(gc_node_ref_t id, uint8_t runtimeFlags);
    /// Gives LOOP node `id` `count` collection slots (camel/runtime/loop.h): its body must then
    /// return the next state followed by the values to collect, and its value type must name the
    /// state and one array per slot. A graph producer that builds such a node calls this; the
    /// source syntax never does. Asserts that `id` is a LOOP node.
    void setLoopCollectionCount(gc_node_ref_t id, uint32_t count);
    /// Regions (GCRegion). A draft has region 0, the body, once it has any other. Ids are not
    /// reused.
    size_t regionCount() const { return regions_.size(); }
    const GCRegion &region(uint32_t id) const { return regions_[id]; }
    size_t regionPortCount() const { return regionPorts_.size(); }
    gc_node_ref_t regionPort(size_t index) const { return regionPorts_[index]; }
    std::span<const gc_node_ref_t> regionPortsOf(uint32_t id) const {
        return {regionPorts_.data() + regions_[id].portBegin, regions_[id].portCount};
    }
    /// The equivalences between the nodes of the draft (writings.h): the draft is not in the
    /// executable state while there are any. Until they are extracted, the nodes can only be
    /// added to: every member that changes or removes a node refuses (std::logic_error),
    /// except inside a WritingScope, which the engine of writings.h opens.
    std::span<const WritingUnion> writings() const { return writings_; }
    void setWritings(std::vector<WritingUnion> log);
    class WritingScope {
      public:
        explicit WritingScope(GraphDraft &draft) : draft_(draft) { ++draft_.writingScope_; }
        ~WritingScope() { --draft_.writingScope_; }
        WritingScope(const WritingScope &)            = delete;
        WritingScope &operator=(const WritingScope &) = delete;

      private:
        GraphDraft &draft_;
    };
    /// The region node `id` is in, 0 when it is in none but the body.
    uint32_t nodeRegion(gc_node_ref_t id) const;
    void setNodeRegion(gc_node_ref_t id, uint32_t region);
    /// A new region in `parent`; for a control region the node that owns it and its index.
    uint32_t addRegion(
        GCRegionKind kind, uint32_t parent, gc_node_ref_t owner = kInvalidNodeRef,
        uint32_t index = 0);
    /// An Inline region: the body of `callee`, which replaced the call `callSite`.
    uint32_t addInlineRegion(uint32_t parent, GraphId callee, gc_node_ref_t callSite);
    void setRegionResult(uint32_t id, gc_node_ref_t result);
    void setRegionPorts(uint32_t id, std::span<const gc_node_ref_t> ports);
    void setRegionOrigin(uint32_t id, uint32_t origin);
    void setRegionInline(uint32_t id, GraphId callee, gc_node_ref_t callSite);
    void setRegionAttrs(uint32_t id, uint32_t attrs);
    void setRegionParams(uint32_t id, const camel::core::type::FunctionType *params);
    /// The control regions that node `id` owns end with it: what they held is in the region
    /// around them, and the regions inside them are inside that one. (Done by eraseNode.)
    void dissolveRegionsOf(gc_node_ref_t id);
    /// Ends the region `id` (unless `keepRegion`) and the regions inside it; nodes that are still
    /// in them go to the region around.
    void retireRegion(uint32_t id, bool keepRegion = false);
    /// Gives each arm that is the call of a graph (the form the front end builds) its region, the
    /// call alone in it.
    void regionizeArmCalls();
    /// True when `id` is the call of the graph that is an arm (GCRegion::callSite): the arm is
    /// that call until lowerBranchArms inlines it.
    bool isArmCall(gc_node_ref_t id) const;
    /// The region a node is put in when it is added without one (0: the body). The front end
    /// sets it to the arm it is writing; passes leave it alone.
    uint32_t insertRegion() const { return insertRegion_; }
    void setInsertRegion(uint32_t region) { insertRegion_ = region; }
    /// Whether a node that reads a node of a deeper control region moves into that region
    /// (inheritRegion). The front end places every node itself, so a read across the border
    /// of an arm is left for the validator to report.
    void setInheritsRegions(bool inherits) { inheritsRegions_ = inherits; }
    /// Sets the owner of a control region made before its BRCH or LOOP node was.
    void setRegionOwner(uint32_t id, gc_node_ref_t owner, uint32_t index);
    /// The region of arm `index` of the BRCH `brch`; 0 when there is none.
    uint32_t armRegionOf(gc_node_ref_t brch, size_t index) const;
    /// The LoopCond or LoopBody region (`part`) of the LOOP node `loop`; 0 when it has none.
    uint32_t loopRegionOf(gc_node_ref_t loop, GCRegionKind part) const;
    /// Makes the region `id` the body of the graph: its nodes are in region 0, the regions in it
    /// are in region 0, and the regions around it end. For a draft that is only the content of
    /// that region (outlining).
    void flattenRegion(uint32_t id);
    /// Drops every region: all nodes are in the body.
    void clearRegions();
    /// True when `inner` is `outer` or lies in it.
    bool regionWithin(uint32_t inner, uint32_t outer) const;
    /// Source origin of node `id` (see DraftNodeHeader::origin).
    uint64_t nodeOrigin(gc_node_ref_t id) const;
    /// The origin of the first live node that has one, 0 when none has: where to report
    /// something about the graph as a whole when its definition is not known.
    uint64_t firstNodeOrigin() const;
    void setNodeOrigin(gc_node_ref_t id, uint64_t origin);
    /// Numeric permissions of node `id` (see DraftNodeHeader::permissions).
    uint8_t nodePermissions(gc_node_ref_t id) const;
    void setNodePermissions(gc_node_ref_t id, uint8_t permissions);
    /// Attributes of node `id` (camel/core/node_attrs.h; see DraftNodeHeader::attrs).
    uint32_t nodeAttrs(gc_node_ref_t id) const;
    void setNodeAttrs(gc_node_ref_t id, uint32_t attrs);
    void rewriteNode(gc_node_ref_t id, const DraftNodeInit &init);
    void appendNormPort(gc_node_ref_t id);
    void appendWithPort(gc_node_ref_t id);
    void appendClosureNode(gc_node_ref_t id);
    void removeNormPort(gc_node_ref_t id);
    void removeWithPort(gc_node_ref_t id);
    void removeClosureNode(gc_node_ref_t id);
    void setEntryNode(gc_node_ref_t id) {
        ++mutations_;
        entry_ = id;
    }
    void setExitNode(gc_node_ref_t id) {
        ++mutations_;
        exit_ = id;
    }
    void setOutputNode(gc_node_ref_t id) {
        ++mutations_;
        output_ = id;
    }
    void setReturnNode(gc_node_ref_t id, GCReturnKind kind) {
        ++mutations_;
        returnNode_ = id;
        returnKind_ = kind;
    }
    gc_slot_idx_t allocateRuntimeSlot(camel::core::type::Type *type);
    size_t appendStaticSlot(slot_t value, camel::core::type::Type *type);
    gc_node_ref_t
    materializeStaticValue(slot_t value, camel::core::type::Type *type, uint8_t runtimeFlags = 0);
    void setStaticSlot(size_t index, slot_t value);
    void addDependency(GraphId graph);
    void addSubGraph(GraphId graph);
    void addStaticGraphRef(GraphId graph);

    GCGraph *encode(const std::string &mangledName, const std::string &name) const;

  private:
    static gc_node_ref_t
    translateNodeRef(gc_node_ref_t sourceRef, const std::vector<gc_node_ref_t> &sourceToDraft);
    static size_t payloadBytesForSourceNode(const GCGraph *graph, gc_node_ref_t sourceRef);

    DraftNode *createDecodedNode(
        const GCGraph *graph, gc_node_ref_t sourceRef,
        const std::vector<gc_node_ref_t> &sourceToDraft);
    DraftNode *rebuildNode(gc_node_ref_t id, const DraftNodeInit &init);
    void replaceNodeStorage(gc_node_ref_t id, DraftNode *node);
    void removeUserRef(gc_node_ref_t id, gc_node_ref_t userId, bool norm, bool with, bool ctrl);
    void appendUserRef(gc_node_ref_t id, gc_node_ref_t userId, bool norm, bool with, bool ctrl);
    /// A node that reads a node of a control region (an arm, a loop part) is in that region, or in
    /// one inside it: it cannot run before what it reads. A JOIN reads the results of its arms and
    /// a LOOP those of its parts; they stay where the branch or the loop is.
    void inheritRegion(gc_node_ref_t input, gc_node_ref_t user);
    std::span<const gc_node_ref_t> usersOf(gc_node_ref_t id, DraftEdgeKind kind) const;
    std::vector<gc_node_ref_t> &userListOf(gc_node_ref_t id, DraftEdgeKind kind);
    std::span<gc_node_ref_t> mutableInputs(DraftNode *node, DraftEdgeKind kind);
    /// Adds `inputId` at the end of the `kind` inputs of `userId`, in place when the node's
    /// storage has room.
    void appendInputEntry(gc_node_ref_t userId, DraftEdgeKind kind, gc_node_ref_t inputId);
    void replaceValueStructuralRefs(gc_node_ref_t oldId, gc_node_ref_t newId);
    void replaceCtrlStructuralRefs(gc_node_ref_t oldId, gc_node_ref_t newId);
    void replaceUsesInList(
        gc_node_ref_t ownerId, std::span<const gc_node_ref_t> users, gc_node_ref_t oldId,
        gc_node_ref_t newId, bool norm, bool with, bool ctrl);
    GraphId payloadGraphAt(uint32_t index) const;
    void appendUniqueNodeRef(std::vector<gc_node_ref_t> &refs, gc_node_ref_t id);
    /// Records node `id` in the list of its kind when that kind has one (see gateIds_).
    void noteNodeKind(gc_node_ref_t id, GCNodeKind kind);

    GraphId id_                                = kInvalidGraphId;
    GraphId outerGraphId_                      = kInvalidGraphId;
    const Program *program_                    = nullptr;
    GraphId typeSpecializationRoot_            = kInvalidGraphId;
    uint8_t flags_                             = 0;
    camel::core::type::FunctionType *funcType_ = nullptr;
    std::vector<camel::core::SymId> captureNames_;
    // The frame layout, by slot. It becomes a tuple type only when the draft is encoded, inside
    // the graph's own block (TupleType::constructFrameLayout).
    std::vector<camel::core::type::Type *> runtimeSlotTypes_;
    bool hasFrameLayout_                       = false;
    camel::core::type::TupleType *closureType_ = nullptr;
    DraftNodePool pool_;
    std::vector<GraphId> payloadGraphs_;
    std::unordered_map<GraphId, uint32_t> payloadGraphIndex_;
    std::vector<DraftNode *> nodesById_;
    std::vector<std::unique_ptr<DraftUserLists>> users_; // by node id; null: no users
    std::vector<GCRegion> regions_; // empty, or region 0 (the body) then the others
    uint32_t insertRegion_    = 0;
    bool inheritsRegions_     = true;
    std::vector<gc_node_ref_t> regionPorts_;
    std::vector<WritingUnion> writings_;
    uint32_t writingScope_ = 0;
    void requireOneWriting(const char *what) const {
        if (!writings_.empty() && writingScope_ == 0) [[unlikely]] {
            refuseWithWritings(what);
        }
    }
    [[noreturn]] void refuseWithWritings(const char *what) const;
    std::vector<gc_node_ref_t> sourceRefsById_;
    std::vector<gc_node_ref_t> draftIdsBySourceRef_;
    gc_cnt_t liveNodeCount_ = 0;
    uint64_t revision_      = 0;
    bool decoded_           = false; // see keepsNodeIds()
    const GCGraph *base_    = nullptr;
    uint64_t mutations_     = 0;
    uint64_t *refsEpoch_    = nullptr;
    void touchRefs() {
        ++revision_;
        if (refsEpoch_) {
            ++*refsEpoch_;
        }
    }
    // The nodes that are, or were, a GATE, a BRCH or a JOIN, by id, each once. Erased nodes stay
    // in the lists (ids are not reused) and are skipped by whoever walks them. The rewrites that
    // concern only these kinds walk the list instead of every node.
    std::vector<gc_node_ref_t> gateIds_;
    std::vector<gc_node_ref_t> brchIds_;
    std::vector<gc_node_ref_t> joinIds_;

    gc_node_ref_t entry_      = kInvalidNodeRef;
    gc_node_ref_t exit_       = kInvalidNodeRef;
    gc_node_ref_t output_     = kInvalidNodeRef;
    gc_node_ref_t returnNode_ = kInvalidNodeRef;
    GCReturnKind returnKind_  = GCReturnKind::None;

    std::vector<gc_node_ref_t> normPorts_;
    std::vector<gc_node_ref_t> withPorts_;
    std::vector<gc_node_ref_t> closureNodes_;
    std::vector<slot_t> staticSlots_;
    std::vector<camel::core::type::Type *> staticSlotTypes_;
    std::vector<GraphId> dependencies_;
    std::vector<GraphId> subGraphs_;
    std::vector<GraphId> staticGraphRefs_;
};

} // namespace camel::runtime
