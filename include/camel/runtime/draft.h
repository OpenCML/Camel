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
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Runtime graph draft model.
 *
 * `GCGraph` is the immutable execution layout. `GraphDraft` is the editable
 * decode form used by runtime rewrites. The draft intentionally keeps node-
 * centric adjacency and stable 16-bit node ids so passes can mutate the graph
 * without invalidating external handles whenever node storage grows.
 *
 * The draft node header stays structurally close to `GCNode`, but the storage
 * model is different:
 * - one draft node owns its payload bytes and all six adjacency lists
 * - nodes live in a pool of small slabs plus oversize fallback blocks
 * - ids are stable while pointers may move during reallocation
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/runtime/graph.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace camel::runtime {

enum class DraftNodeStorageClass : uint8_t {
    Slab64,
    Slab96,
    Slab128,
    Oversize,
};

struct DraftBrchPayload {
    gc_node_ref_t join       = kInvalidNodeRef;
    gc_cnt_t armCount        = 0;
    gc_node_ref_t defaultArm = kInvalidNodeRef;
    uint16_t reserved        = 0;
};
static_assert(std::is_trivially_copyable_v<DraftBrchPayload>);

struct DraftNodeHeader {
    gc_slot_idx_t dataIndex           = 0;
    uint16_t payloadBytes             = 0;
    uint16_t capacityBytes            = 0;
    camel::core::type::Type *dataType = nullptr;

    gc_cnt_t normInputCount = 0;
    gc_cnt_t withInputCount = 0;
    gc_cnt_t ctrlInputCount = 0;
    gc_cnt_t normUserCount  = 0;
    gc_cnt_t withUserCount  = 0;
    gc_cnt_t ctrlUserCount  = 0;

    GCNodeKind kind                  = GCNodeKind::Data;
    uint8_t runtimeFlags             = 0;
    DraftNodeStorageClass storageCls = DraftNodeStorageClass::Slab64;
    uint8_t reserved                 = 0;
    constexpr size_t usedBytes() const {
        return static_cast<size_t>(payloadBytes) +
               (static_cast<size_t>(normInputCount) + static_cast<size_t>(withInputCount) +
                static_cast<size_t>(ctrlInputCount) + static_cast<size_t>(normUserCount) +
                static_cast<size_t>(withUserCount) + static_cast<size_t>(ctrlUserCount)) *
                   sizeof(gc_node_ref_t);
    }
};

struct DraftNode {
    DraftNodeHeader header;
};

struct DraftNodeInit {
    gc_slot_idx_t dataIndex           = 0;
    camel::core::type::Type *dataType = nullptr;
    GCNodeKind kind                   = GCNodeKind::Data;
    uint8_t runtimeFlags              = 0;
    std::span<const std::byte> payload{};
    std::span<const gc_node_ref_t> normInputs{};
    std::span<const gc_node_ref_t> withInputs{};
    std::span<const gc_node_ref_t> ctrlInputs{};
    std::span<const gc_node_ref_t> normUsers{};
    std::span<const gc_node_ref_t> withUsers{};
    std::span<const gc_node_ref_t> ctrlUsers{};
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

    static std::span<gc_node_ref_t> normUsers(DraftNode *node);
    static std::span<const gc_node_ref_t> normUsers(const DraftNode *node);
    static std::span<gc_node_ref_t> withUsers(DraftNode *node);
    static std::span<const gc_node_ref_t> withUsers(const DraftNode *node);
    static std::span<gc_node_ref_t> ctrlUsers(DraftNode *node);
    static std::span<const gc_node_ref_t> ctrlUsers(const DraftNode *node);

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

    DraftNode *alloc(size_t tailBytes, DraftNodeStorageClass *storageClass = nullptr);
    DraftNode *realloc(
        DraftNode *node, size_t oldTailBytes, size_t newTailBytes,
        DraftNodeStorageClass *storageClass = nullptr);
    void clear();

  private:
    struct SlabArena {
        size_t blockBytes = 0;
        size_t cursor     = 0;
        std::vector<std::unique_ptr<std::byte[]>> pages;
    };

    static constexpr size_t kPageBytes = 4096;

    static size_t capacityForStorageClass(DraftNodeStorageClass storageClass);
    static DraftNodeStorageClass classify(size_t totalBytes);
    DraftNode *allocFromArena(SlabArena &arena);

    SlabArena slab64_{64, 0, {}};
    SlabArena slab96_{96, 0, {}};
    SlabArena slab128_{128, 0, {}};
    std::vector<void *> oversizeBlocks_;
};

class GraphDraft {
  public:
    static std::unique_ptr<GraphDraft> decode(const GCGraph *graph);

    GraphDraft()  = default;
    ~GraphDraft() = default;

    GraphDraft(const GraphDraft &)            = delete;
    GraphDraft &operator=(const GraphDraft &) = delete;

    const GCGraph *source() const { return source_; }
    const std::string &stableId() const { return stableId_; }
    const std::string &mangledName() const { return mangledName_; }
    const std::string &name() const { return name_; }
    camel::core::type::FunctionType *funcType() const { return funcType_; }
    camel::core::type::TupleType *runtimeDataType() const { return runtimeDataType_; }
    camel::core::type::TupleType *closureType() const { return closureType_; }
    size_t frameSize() const {
        ASSERT(hasFrameLayout(), "Runtime draft frame layout is not available.");
        return sizeof(camel::core::context::Frame) + sizeof(slot_t) * runtimeDataType_->size();
    }
    bool hasFrameLayout() const { return runtimeDataType_ != nullptr && !staticSlots_.empty(); }
    bool isMacroGraph() const { return funcType_ != nullptr && funcType_->modifiers().macro(); }
    gc_cnt_t nodeCount() const { return liveNodeCount_; }
    size_t nodeSlotCount() const { return nodesById_.size(); }
    bool empty() const { return liveNodeCount_ == 0; }

    bool containsNode(gc_node_ref_t id) const;
    bool alive(gc_node_ref_t id) const;
    const DraftNode *node(gc_node_ref_t id) const;
    DraftNode *node(gc_node_ref_t id);
    const DraftNodeHeader *header(gc_node_ref_t id) const;
    gc_node_ref_t draftIdOfSourceRef(gc_node_ref_t sourceRef) const;
    gc_node_ref_t sourceRefOf(gc_node_ref_t draftId) const;
    std::span<const std::byte> payloadOf(gc_node_ref_t id) const;
    std::span<const GCBranchArm> branchArmsOf(gc_node_ref_t id) const;

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
    const std::vector<GCGraph *> &dependencies() const { return dependencies_; }
    const std::vector<GCGraph *> &subGraphs() const { return subGraphs_; }
    const std::vector<GCGraph *> &staticGraphRefs() const { return staticGraphRefs_; }

    std::span<const gc_node_ref_t> normInputsOf(gc_node_ref_t id) const;
    std::span<const gc_node_ref_t> withInputsOf(gc_node_ref_t id) const;
    std::span<const gc_node_ref_t> ctrlInputsOf(gc_node_ref_t id) const;
    std::span<const gc_node_ref_t> normUsersOf(gc_node_ref_t id) const;
    std::span<const gc_node_ref_t> withUsersOf(gc_node_ref_t id) const;
    std::span<const gc_node_ref_t> ctrlUsersOf(gc_node_ref_t id) const;
    bool isControlAnchor(gc_node_ref_t id) const;
    bool isBranchArmAnchor(gc_node_ref_t id) const;
    gc_node_ref_t resolveForwardedValueRef(gc_node_ref_t id) const;
    gc_node_ref_t resolveForwardedCtrlRef(gc_node_ref_t id) const;

    gc_node_ref_t addNode(const DraftNodeInit &init);
    void eraseNode(gc_node_ref_t id);
    void setNormInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs);
    void setWithInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs);
    void setCtrlInputs(gc_node_ref_t id, std::span<const gc_node_ref_t> inputs);
    void replaceAllNormUses(gc_node_ref_t oldId, gc_node_ref_t newId);
    void replaceAllWithUses(gc_node_ref_t oldId, gc_node_ref_t newId);
    void replaceAllCtrlUses(gc_node_ref_t oldId, gc_node_ref_t newId);
    void replaceAllValueUses(gc_node_ref_t oldId, gc_node_ref_t newId);
    void
    retargetBranchArmAnchors(gc_node_ref_t oldId, gc_node_ref_t newHeadId, gc_node_ref_t newTailId);
    void setEntryNode(gc_node_ref_t id) { entry_ = id; }
    void setExitNode(gc_node_ref_t id) { exit_ = id; }
    void setOutputNode(gc_node_ref_t id) { output_ = id; }
    void setReturnNode(gc_node_ref_t id, GCReturnKind kind) {
        returnNode_ = id;
        returnKind_ = kind;
    }
    gc_slot_idx_t allocateRuntimeSlot(camel::core::type::Type *type);
    size_t appendStaticSlot(slot_t value, camel::core::type::Type *type);
    gc_node_ref_t
    materializeStaticValue(slot_t value, camel::core::type::Type *type, uint8_t runtimeFlags = 0);
    void setStaticSlot(size_t index, slot_t value);
    void addDependency(GCGraph *graph);
    void addSubGraph(GCGraph *graph);
    void addStaticGraphRef(GCGraph *graph);

    GCGraph *encode() const;

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
    void replaceValueStructuralRefs(gc_node_ref_t oldId, gc_node_ref_t newId);
    void replaceCtrlStructuralRefs(gc_node_ref_t oldId, gc_node_ref_t newId);
    void replaceUsesInList(
        gc_node_ref_t ownerId, std::span<const gc_node_ref_t> users, gc_node_ref_t oldId,
        gc_node_ref_t newId, bool norm, bool with, bool ctrl);

    const GCGraph *source_ = nullptr;
    std::string stableId_;
    std::string mangledName_;
    std::string name_;
    camel::core::type::FunctionType *funcType_     = nullptr;
    camel::core::type::TupleType *runtimeDataType_ = nullptr;
    camel::core::type::TupleType *closureType_     = nullptr;
    DraftNodePool pool_;
    std::vector<DraftNode *> nodesById_;
    std::vector<gc_node_ref_t> sourceRefsById_;
    std::vector<gc_node_ref_t> draftIdsBySourceRef_;
    gc_cnt_t liveNodeCount_ = 0;

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
    std::vector<GCGraph *> dependencies_;
    std::vector<GCGraph *> subGraphs_;
    std::vector<GCGraph *> staticGraphRefs_;
};

} // namespace camel::runtime
