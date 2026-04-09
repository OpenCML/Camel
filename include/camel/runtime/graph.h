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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Native runtime GCGraph layout.
 *
 * Compile-time GIR remains a one-shot build artifact. Before execution starts,
 * the reachable compile graph closure is materialized into immutable runtime
 * graph objects. Runtime execution then consumes GCGraph directly.
 *
 * The native runtime payload follows four rules:
 * 1. Nodes live in one variable-length node blob.
 * 2. Every node begins with a fixed GCNode header.
 * 3. All adjacency references use node-blob offsets, not dense ordinals.
 * 4. Hot-path payload stays plain; cold debug metadata lives elsewhere.
 */

#pragma once

#include "camel/compile/gir.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/tuple.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::runtime {

using gc_node_ref_t = uint16_t;
using gc_slot_idx_t = int16_t;
using gc_off_t      = uint16_t;
using gc_cnt_t      = uint16_t;
using gc_block_t    = uint64_t;

constexpr gc_node_ref_t kInvalidNodeRef = static_cast<gc_node_ref_t>(-1);
constexpr gc_off_t kInvalidOffset       = static_cast<gc_off_t>(-1);

inline constexpr size_t kGCBlockBytes = sizeof(gc_block_t);

inline constexpr gc_cnt_t blocksForBytes(size_t bytes) {
    return static_cast<gc_cnt_t>((bytes + kGCBlockBytes - 1) / kGCBlockBytes);
}

enum class GCNodeKind : uint8_t {
    Data,
    Port,
    Cast,
    Copy,
    Fill,
    Accs,
    Brch,
    Join,
    Call,
    Bind,
    Func,
    Oper,
    Sync,
    Gate,
    Dref,
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

struct Slice {
    gc_off_t offset = 0;
    gc_cnt_t count  = 0;

    constexpr bool empty() const { return count == 0; }
};

struct GCBranchArm {
    gc_node_ref_t head = kInvalidNodeRef;
    gc_node_ref_t tail = kInvalidNodeRef;
};

struct GCNode {
    gc_slot_idx_t dataIndex = 0;
    gc_cnt_t blockCount     = 0;

    Slice normInputs;
    Slice withInputs;
    Slice ctrlInputs;
    Slice normOutputs;
    Slice withOutputs;
    Slice ctrlOutputs;

    camel::core::type::Type *dataType = nullptr;

    GCNodeKind kind = GCNodeKind::Data;
    uint8_t flags   = 0;

    static constexpr gc_cnt_t headerBlocks() { return blocksForBytes(sizeof(GCNode)); }

    constexpr bool hasFlag(uint8_t mask) const { return (flags & mask) != 0; }
    constexpr bool isMacro() const { return hasFlag(kGCNodeFlagMacro); }
    constexpr bool isConstant() const { return hasFlag(kGCNodeFlagConstant); }
    constexpr bool hasBody() const { return blockCount > headerBlocks(); }

    constexpr gc_cnt_t bodyBlocks() const {
        return blockCount >= headerBlocks() ? static_cast<gc_cnt_t>(blockCount - headerBlocks())
                                            : 0;
    }

    constexpr size_t bodyBytes() const { return static_cast<size_t>(bodyBlocks()) * kGCBlockBytes; }

    template <typename Body> const Body *bodyAs() const {
        static_assert(
            std::is_trivially_copyable_v<Body>,
            "GC node bodies must be trivially copyable.");
        ASSERT(
            bodyBytes() >= sizeof(Body),
            "GC node body is smaller than the requested body layout.");
        return reinterpret_cast<const Body *>(
            reinterpret_cast<const std::byte *>(this) + sizeof(GCNode));
    }

    template <typename Body> Body *bodyAs() {
        static_assert(
            std::is_trivially_copyable_v<Body>,
            "GC node bodies must be trivially copyable.");
        ASSERT(
            bodyBytes() >= sizeof(Body),
            "GC node body is smaller than the requested body layout.");
        return reinterpret_cast<Body *>(reinterpret_cast<std::byte *>(this) + sizeof(GCNode));
    }
};
static_assert(
    sizeof(GCNode) % kGCBlockBytes == 0, "GCNode must align to the node-blob block size.");

struct GCFuncBody {
    class GCGraph *calleeGraph = nullptr;
};
static_assert(std::is_trivially_copyable_v<GCFuncBody>);

struct GCCallBody {
    GCCallKind callKind = GCCallKind::Indirect;
    uint8_t reserved0   = 0;
    uint16_t reserved1  = 0;
    uint32_t reserved2  = 0;
};
static_assert(std::is_trivially_copyable_v<GCCallBody>);

struct GCAccsBody {
    GCAccsKind accsKind = GCAccsKind::TupleIndex;
    uint8_t reserved0   = 0;
    uint16_t keyBytes   = 0;
    uint32_t value      = 0;

    std::string_view key() const {
        const char *data = reinterpret_cast<const char *>(
            reinterpret_cast<const std::byte *>(this) + sizeof(GCAccsBody));
        return {data, keyBytes};
    }
};
static_assert(std::is_trivially_copyable_v<GCAccsBody>);

struct GCFillBody {
    GCFillKind fillKind = GCFillKind::Tuple;
    uint8_t reserved0   = 0;
    uint16_t reserved1  = 0;
    uint32_t reserved2  = 0;
};
static_assert(std::is_trivially_copyable_v<GCFillBody>);

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
    uint32_t reserved  = 0;
};
static_assert(std::is_trivially_copyable_v<GCJoinBody>);

struct GCOperBody {
    operator_t op     = nullptr;
    uint16_t uriBytes = 0;
    uint16_t reserved = 0;

    std::string_view uri() const {
        const char *data = reinterpret_cast<const char *>(
            reinterpret_cast<const std::byte *>(this) + sizeof(GCOperBody));
        return {data, uriBytes};
    }
};
static_assert(std::is_trivially_copyable_v<GCOperBody>);

struct GCGraphNativePayload {
    // The payload itself stays plain and pointer-based. Cold metadata owns the
    // backing arenas and republishes them through this view.
    gc_block_t *nodeBlocks   = nullptr;
    gc_node_ref_t *edges     = nullptr;
    gc_node_ref_t *normPorts = nullptr;
    gc_node_ref_t *withPorts = nullptr;
    gc_node_ref_t *closure   = nullptr;
    GCBranchArm *branchArms  = nullptr;
    gc_cnt_t nodeCount       = 0;
    gc_cnt_t nodeBlockCount  = 0;
    gc_cnt_t edgeCount       = 0;
    gc_cnt_t normPortCount   = 0;
    gc_cnt_t withPortCount   = 0;
    gc_cnt_t closureCount    = 0;
    gc_cnt_t branchArmCount  = 0;
    gc_node_ref_t entryNode  = kInvalidNodeRef;
    gc_node_ref_t exitNode   = kInvalidNodeRef;
    gc_node_ref_t outputNode = kInvalidNodeRef;
    gc_node_ref_t returnNode = kInvalidNodeRef;
    GCReturnKind returnKind  = GCReturnKind::None;

    constexpr bool empty() const { return nodeBlocks == nullptr || nodeBlockCount == 0; }
};

struct GCGraphMetadataRecord;

class GCGraph : public camel::core::rtdata::Object {
  public:
    static constexpr size_t kExtraSlotCount = 8;

    explicit GCGraph(GCGraphMetadataRecord *metadata);

    // These accessors expose cold compile/debug metadata only. Runtime passes
    // should derive execution semantics from the native payload instead.
    GIR::Graph *compileGraph() const;
    const GIR::graph_ptr_t &sourceGraph() const;
    const GIR::graph_ptr_t &compileGraphMetadata() const;
    const std::string &stableId() const;
    const std::string &mangledName() const;
    const std::string &name() const;
    camel::core::type::FunctionType *funcType() const;
    const camel::core::type::TupleType *runtimeDataType() const;
    const camel::core::type::TupleType *staticDataType() const;
    const camel::core::type::TupleType *closureType() const;
    bool hasFrameLayout() const;
    size_t frameSize() const;
    bool isMacro() const;
    bool isRoot() const;
    ::Tuple *staticArea() const { return staticArea_; }
    uintptr_t extraSlot(size_t index) const;
    void setExtraSlot(size_t index, uintptr_t value);
    void clearExtraSlots();

    const std::vector<GCGraph *> &dependencies() const;
    const std::vector<GCGraph *> &subGraphs() const;
    const std::vector<GCGraph *> &staticGraphRefs() const;
    std::span<const slot_t> staticSlots() const;

    bool hasNodePayload() const;
    const GCGraphNativePayload *nodePayload() const;
    gc_cnt_t nodeCount() const;
    gc_cnt_t nodeBlockCount() const;
    gc_cnt_t edgeCount() const;
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

    const GCNode *node(gc_node_ref_t ref) const;
    bool containsNodeRef(gc_node_ref_t ref) const;
    gc_node_ref_t nodeRef(const GCNode *node) const;
    GCGraph *directCalleeGraphOf(gc_node_ref_t ref) const;
    gc_node_ref_t matchedJoinOutputOf(gc_node_ref_t ref) const;
    gc_node_ref_t nextNodeRef(gc_node_ref_t ref) const;
    const GCNode *nextNode(gc_node_ref_t ref) const;

    template <typename Body> const Body *nodeBodyAs(gc_node_ref_t ref) const {
        const GCNode *n = node(ref);
        ASSERT(n != nullptr, "GCGraph node-body lookup resolved to a null node.");
        return n->bodyAs<Body>();
    }

    std::span<const gc_node_ref_t> edgeSlice(Slice slice) const;
    std::span<const gc_node_ref_t> normInputsOf(gc_node_ref_t ref) const;
    std::span<const gc_node_ref_t> withInputsOf(gc_node_ref_t ref) const;
    std::span<const gc_node_ref_t> ctrlInputsOf(gc_node_ref_t ref) const;
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
            ref_ = graph_->nextNodeRef(ref_);
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
        NodeIterator end() const {
            return NodeIterator(graph_, graph_ ? graph_->nodeBlockCount() : 0);
        }

      private:
        const GCGraph *graph_ = nullptr;
    };

    NodeRange nodes() const { return NodeRange(this); }

    void addDependency(GCGraph *graph);
    void addSubGraph(GCGraph *graph);
    void addStaticGraphRef(GCGraph *graph);
    void setStaticSlots(camel::compile::gir::static_slot_vec_t slots);

    template <typename Visitor> void traceGraphs(Visitor &&visitor) const {
        for (GCGraph *graph : dependencies()) {
            if (graph) {
                visitor(graph);
            }
        }
        for (GCGraph *graph : subGraphs()) {
            if (graph) {
                visitor(graph);
            }
        }
        for (GCGraph *graph : staticGraphRefs()) {
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
    void updateRefs(
        const std::function<camel::core::rtdata::Object *(camel::core::rtdata::Object *)> &relocate,
        const camel::core::type::Type *type) override;

  private:
    GCGraphMetadataRecord *metadata_ = nullptr;
    ::Tuple *staticArea_             = nullptr;
    uintptr_t extraSlots_[kExtraSlotCount]{};
};

class GCGraphManager {
  public:
    GCGraphManager() = default;
    ~GCGraphManager();

    GCGraph *materializeRoot(const GIR::graph_ptr_t &rootGraph);
    GCGraph *find(const GIR::Graph *sourceGraph) const;
    GCGraph *root() const { return root_; }
    std::vector<camel::core::rtdata::Object *> &gcRoots() { return gcRoots_; }
    const std::vector<camel::core::rtdata::Object *> &gcRoots() const { return gcRoots_; }

    std::vector<GCGraph *> roots() const;
    std::vector<GCGraph *> reachableFromRoots() const;
    void clear();

  private:
    GCGraph *materializeGraph(
        const GIR::graph_ptr_t &sourceGraph,
        std::unordered_map<const GIR::Graph *, GCGraph *> &cache);
    slot_t canonicalizeStaticSlot(
        slot_t slot, camel::core::type::Type *type,
        std::unordered_map<const GIR::Graph *, GCGraph *> &cache,
        std::unordered_map<const camel::core::rtdata::Object *, camel::core::rtdata::Object *>
            &objectCache,
        const GIR::Graph *sourceOwner = nullptr);
    void collectStaticGraphRefs(
        GCGraph *owner, slot_t slot, camel::core::type::Type *type,
        std::unordered_map<const GIR::Graph *, GCGraph *> &cache,
        std::unordered_set<const camel::core::rtdata::Object *> &visited);

    std::vector<GCGraph *> graphs_;
    std::vector<GCGraphMetadataRecord *> metadataRecords_;
    std::unordered_map<const GIR::Graph *, GCGraph *> bySource_;
    std::vector<camel::core::rtdata::Object *> gcRoots_;
    GCGraph *root_ = nullptr;
};

} // namespace camel::runtime
