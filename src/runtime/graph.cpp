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
 * Runtime graph materialization and native payload accessors.
 *
 * This file now targets the native runtime layout rather than the earlier
 * bridge-style node-record arrays. The payload keeps one variable-length node
 * blob, one unified edge arena, and a compact branch-arm arena. Compile GIR
 * remains attached only as cold metadata for transitional diagnostics.
 */

#include "camel/runtime/graph.h"

#include "camel/compile/gir/nodes.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/runtime/reachable.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>

namespace camel::runtime {

using camel::core::rtdata::fromSlot;
using camel::core::rtdata::Object;
using camel::core::rtdata::toSlot;
namespace mm = camel::core::mm;
using camel::core::type::ArrayType;
using camel::core::type::StructType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::TypeCode;

struct GCGraphMetadataRecord {
    GIR::graph_ptr_t sourceGraph;
    std::string stableId;
    std::string mangledName;
    std::string name;
    camel::core::type::FunctionType *funcType     = nullptr;
    camel::core::type::TupleType *runtimeDataType = nullptr;
    camel::core::type::TupleType *staticDataType  = nullptr;
    camel::core::type::TupleType *closureType     = nullptr;
    size_t frameSize                              = 0;
    bool hasFrameLayout                           = false;
    bool isMacro                                  = false;
    bool isRoot                                   = false;
    std::vector<GCGraph *> dependencies;
    std::vector<GCGraph *> subGraphs;
    std::vector<GCGraph *> staticGraphRefs;
    ::Tuple *ownedStaticArea = nullptr;
    GCGraphNativePayload nativePayload;
    ~GCGraphMetadataRecord();
};

namespace {

template <typename T> T *graphAllocArray(size_t count) {
    if (count == 0) {
        return nullptr;
    }
    void *mem = mm::graphSpace().alloc(sizeof(T) * count, alignof(slot_t));
    if (!mem) {
        throw std::bad_alloc();
    }
    auto *ptr = reinterpret_cast<T *>(mem);
    std::memset(ptr, 0, sizeof(T) * count);
    return ptr;
}

template <typename T, typename... Args> T *graphAllocObject(Args &&...args) {
    return mm::constructAt<T>(mm::graphSpace(), std::forward<Args>(args)...);
}

template <typename T> void graphFreeOwned(T *ptr) {
    if (!ptr) {
        return;
    }
    ptr->~T();
    mm::graphSpace().free(ptr);
}

template <typename T> void graphFreeArray(T *ptr) {
    if (ptr) {
        mm::graphSpace().free(ptr);
    }
}

GIR::graph_ptr_t requireOwnedSourceGraphHandle(
    GIR::Graph *graph, std::string_view context, GCGraph *owner = nullptr) {
    ASSERT(graph != nullptr, "Runtime source-graph handle request received a null graph.");
    try {
        return graph->shared_from_this();
    } catch (const std::bad_weak_ptr &) {
        throw std::runtime_error(
            std::format(
                "{} encountered non-owned source graph '{}' ({:p}) while materializing runtime "
                "owner {:p}.",
                context,
                graph->name(),
                static_cast<void *>(graph),
                static_cast<void *>(owner)));
    }
}

template <typename T> void pushUnique(std::vector<T *> &items, T *value) {
    if (value && std::find(items.begin(), items.end(), value) == items.end()) {
        items.push_back(value);
    }
}

template <typename T, typename U> T narrowIntegral(U value, const char *what) {
    ASSERT(
        value <= static_cast<U>(std::numeric_limits<T>::max()),
        std::format("{} exceeds the target integral width.", what));
    return static_cast<T>(value);
}

GCNodeKind toRuntimeNodeKind(GIR::NodeType type) {
    using GIR::NodeType;
    switch (type) {
    case NodeType::DATA:
        return GCNodeKind::Data;
    case NodeType::PORT:
        return GCNodeKind::Port;
    case NodeType::CAST:
        return GCNodeKind::Cast;
    case NodeType::COPY:
        return GCNodeKind::Copy;
    case NodeType::FILL:
        return GCNodeKind::Fill;
    case NodeType::ACCS:
        return GCNodeKind::Accs;
    case NodeType::BRCH:
        return GCNodeKind::Brch;
    case NodeType::JOIN:
        return GCNodeKind::Join;
    case NodeType::CALL:
        return GCNodeKind::Call;
    case NodeType::BIND:
        return GCNodeKind::Bind;
    case NodeType::FUNC:
        return GCNodeKind::Func;
    case NodeType::OPER:
        return GCNodeKind::Oper;
    case NodeType::SYNC:
        return GCNodeKind::Sync;
    case NodeType::GATE:
        return GCNodeKind::Gate;
    case NodeType::DREF:
        return GCNodeKind::Dref;
    }
    return GCNodeKind::Data;
}

std::vector<GIR::Node *> collectNativeNodes(const GIR::graph_ptr_t &graph) {
    std::vector<GIR::Node *> nodes;
    const auto &normPorts = graph->normPorts();
    const auto &withPorts = graph->withPorts();
    const auto &closure   = graph->closure();
    nodes.reserve(normPorts.size() + withPorts.size() + closure.size() + graph->nodes().size() + 2);
    nodes.insert(nodes.end(), normPorts.begin(), normPorts.end());
    nodes.insert(nodes.end(), withPorts.begin(), withPorts.end());
    nodes.insert(nodes.end(), closure.begin(), closure.end());
    nodes.insert(nodes.end(), graph->nodes().begin(), graph->nodes().end());

    auto pushUniqueNode = [&](GIR::Node *node) {
        if (node && std::ranges::find(nodes, node) == nodes.end()) {
            nodes.push_back(node);
        }
    };

    pushUniqueNode(graph->outputNode());
    pushUniqueNode(graph->exitNode());
    return nodes;
}

GIR::graph_ptr_t resolveGraphHandle(const GIR::graph_ptr_t &owner, const GIR::Graph *target) {
    if (!owner || !target) {
        return nullptr;
    }
    if (owner.get() == target) {
        return owner;
    }
    for (const auto &dep : owner->dependencies()) {
        if (dep.get() == target) {
            return dep;
        }
    }
    for (const auto &[_, subGraphs] : owner->subGraphs()) {
        for (const auto &subGraph : subGraphs) {
            if (subGraph.get() == target) {
                return subGraph;
            }
        }
    }
    if (auto outer = owner->outer()) {
        if (auto resolved = resolveGraphHandle(outer, target)) {
            return resolved;
        }
    }
    return nullptr;
}

size_t nodeBodyBytes(const GIR::Node *node) {
    ASSERT(node != nullptr, "Runtime node-body sizing received a null source node.");
    switch (node->type()) {
    case GIR::NodeType::FUNC:
        return sizeof(GCFuncBody);
    case GIR::NodeType::CALL:
        return sizeof(GCCallBody);
    case GIR::NodeType::ACCS: {
        auto *accsNode = tt::as_ptr<GIR::AccsNode>(const_cast<GIR::Node *>(node));
        ASSERT(accsNode != nullptr, "Runtime ACCS layout sizing requires an ACCS node.");
        return sizeof(GCAccsBody) + (accsNode->isNum() ? 0 : accsNode->strIndex().size());
    }
    case GIR::NodeType::FILL:
        return sizeof(GCFillBody);
    case GIR::NodeType::BRCH:
        return sizeof(GCBrchBody);
    case GIR::NodeType::JOIN:
        return sizeof(GCJoinBody);
    case GIR::NodeType::OPER: {
        auto *operNode = tt::as_ptr<GIR::OperNode>(const_cast<GIR::Node *>(node));
        ASSERT(operNode != nullptr, "Runtime OPER layout sizing requires an OPER node.");
        return sizeof(GCOperBody) + operNode->oper()->uri().size();
    }
    default:
        return 0;
    }
}

gc_cnt_t nodeBlockCount(const GIR::Node *node) {
    const size_t totalBytes = sizeof(GCNode) + nodeBodyBytes(node);
    return blocksForBytes(totalBytes);
}

std::byte *mutableNodeStorage(gc_block_t *blocks, gc_node_ref_t ref) {
    return reinterpret_cast<std::byte *>(blocks + ref);
}

const std::byte *nodeStorage(const gc_block_t *blocks, gc_node_ref_t ref) {
    return reinterpret_cast<const std::byte *>(blocks + ref);
}

template <typename Body> void writeBody(gc_block_t *blocks, gc_node_ref_t ref, const Body &body) {
    static_assert(
        std::is_trivially_copyable_v<Body>,
        "GC node bodies must remain trivially copyable.");
    auto *dst = mutableNodeStorage(blocks, ref) + sizeof(GCNode);
    std::memcpy(dst, &body, sizeof(Body));
}

void writeTrailingBytes(
    gc_block_t *blocks, gc_node_ref_t ref, size_t bodyHeaderBytes, std::string_view bytes) {
    if (bytes.empty()) {
        return;
    }
    auto *dst = mutableNodeStorage(blocks, ref) + sizeof(GCNode) + bodyHeaderBytes;
    std::memcpy(dst, bytes.data(), bytes.size());
}

std::span<const gc_node_ref_t>
sliceView(const gc_node_ref_t *storage, Slice slice, gc_cnt_t totalCount) {
    if (!storage || slice.count == 0) {
        return {};
    }
    ASSERT(
        static_cast<size_t>(slice.offset) + static_cast<size_t>(slice.count) <= totalCount,
        "GCGraph edge slice is out of bounds.");
    return {storage + slice.offset, slice.count};
}

std::span<const GCBranchArm>
branchSliceView(const GCBranchArm *storage, gc_off_t offset, gc_cnt_t count, gc_cnt_t totalCount) {
    if (!storage || count == 0) {
        return {};
    }
    ASSERT(
        static_cast<size_t>(offset) + static_cast<size_t>(count) <= totalCount,
        "GCGraph branch-arm slice is out of bounds.");
    return {storage + offset, count};
}

GCFillKind classifyFillKind(Type *type) {
    ASSERT(type != nullptr, "Runtime FILL classification requires a target type.");
    switch (type->code()) {
    case TypeCode::Tuple:
        return GCFillKind::Tuple;
    case TypeCode::Array:
        return GCFillKind::Array;
    case TypeCode::Struct:
        return GCFillKind::Struct;
    case TypeCode::Function:
        return GCFillKind::FunctionClosure;
    default:
        ASSERT(false, std::format("Unsupported runtime FILL target type '{}'.", type->toString()));
        return GCFillKind::Tuple;
    }
}

struct PlannedNativeNode {
    GIR::Node *source   = nullptr;
    gc_node_ref_t ref   = kInvalidNodeRef;
    gc_cnt_t blockCount = 0;
};

struct NativePayloadPlan {
    std::vector<PlannedNativeNode> nodes;
    std::unordered_map<const GIR::Node *, gc_node_ref_t> nodeRefs;
    gc_cnt_t nodeBlockCount = 0;
    gc_cnt_t edgeCount      = 0;
    gc_cnt_t branchArmCount = 0;
    gc_cnt_t normPortCount  = 0;
    gc_cnt_t withPortCount  = 0;
    gc_cnt_t closureCount   = 0;
};

NativePayloadPlan planNativePayload(const GIR::graph_ptr_t &graph) {
    NativePayloadPlan plan;
    if (!graph) {
        return plan;
    }

    std::vector<GIR::Node *> orderedNodes = collectNativeNodes(graph);
    plan.nodes.reserve(orderedNodes.size());
    plan.nodeRefs.reserve(orderedNodes.size());
    plan.normPortCount =
        narrowIntegral<gc_cnt_t>(graph->normPorts().size(), "Runtime norm-port count");
    plan.withPortCount =
        narrowIntegral<gc_cnt_t>(graph->withPorts().size(), "Runtime with-port count");
    plan.closureCount =
        narrowIntegral<gc_cnt_t>(graph->closure().size(), "Runtime closure-node count");

    gc_off_t currentNodeOffset = 0;
    size_t edgeCount           = 0;
    size_t branchArmCount      = 0;
    for (GIR::Node *node : orderedNodes) {
        const gc_cnt_t blocks = nodeBlockCount(node);
        plan.nodes.push_back(
            PlannedNativeNode{
                .source     = node,
                .ref        = currentNodeOffset,
                .blockCount = blocks,
            });
        plan.nodeRefs.emplace(node, currentNodeOffset);
        currentNodeOffset = narrowIntegral<gc_off_t>(
            static_cast<size_t>(currentNodeOffset) + blocks,
            "Runtime node blob size");
        edgeCount += node->normInputs().size() + node->withInputs().size() +
                     node->ctrlInputs().size() + node->normOutputs().size() +
                     node->withOutputs().size() + node->ctrlOutputs().size();
        if (node->type() == GIR::NodeType::BRCH) {
            auto *brchNode = tt::as_ptr<GIR::BrchNode>(node);
            ASSERT(brchNode != nullptr, "Runtime BRCH plan requires BRCH metadata.");
            branchArmCount += brchNode->armCount();
        }
    }

    plan.nodeBlockCount =
        narrowIntegral<gc_cnt_t>(currentNodeOffset, "Runtime node-blob block count");
    plan.edgeCount      = narrowIntegral<gc_cnt_t>(edgeCount, "Runtime edge arena size");
    plan.branchArmCount = narrowIntegral<gc_cnt_t>(branchArmCount, "Runtime branch-arm arena size");
    return plan;
}

GCGraphNativePayload buildNativePayload(
    const GIR::graph_ptr_t &graph,
    const std::function<GCGraph *(const GIR::Graph *)> &resolveRuntimeGraph) {
    GCGraphNativePayload payload;
    if (!graph) {
        return payload;
    }

    const NativePayloadPlan plan = planNativePayload(graph);
    payload.nodeCount      = narrowIntegral<gc_cnt_t>(plan.nodes.size(), "Runtime node count");
    payload.nodeBlockCount = plan.nodeBlockCount;
    payload.edgeCount      = plan.edgeCount;
    payload.normPortCount  = plan.normPortCount;
    payload.withPortCount  = plan.withPortCount;
    payload.closureCount   = plan.closureCount;
    payload.branchArmCount = plan.branchArmCount;

    payload.nodeBlocks = graphAllocArray<gc_block_t>(payload.nodeBlockCount);
    payload.edges      = graphAllocArray<gc_node_ref_t>(payload.edgeCount);
    payload.normPorts  = graphAllocArray<gc_node_ref_t>(payload.normPortCount);
    payload.withPorts  = graphAllocArray<gc_node_ref_t>(payload.withPortCount);
    payload.closure    = graphAllocArray<gc_node_ref_t>(payload.closureCount);
    payload.branchArms = graphAllocArray<GCBranchArm>(payload.branchArmCount);

    gc_off_t edgeCursor      = 0;
    gc_off_t branchArmCursor = 0;
    auto appendSlice         = [&](camel::compile::gir::node_span_t span) -> Slice {
        Slice slice{
            .offset = edgeCursor,
            .count  = 0,
        };
        for (GIR::Node *node : span) {
            auto it = plan.nodeRefs.find(node);
            ASSERT(
                it != plan.nodeRefs.end(),
                "Runtime edge emission encountered a node outside the payload plan.");
            payload.edges[edgeCursor++] = it->second;
        }
        slice.count =
            narrowIntegral<gc_cnt_t>(edgeCursor - slice.offset, "Runtime edge slice count");
        return slice;
    };

    for (const PlannedNativeNode &planned : plan.nodes) {
        GIR::Node *sourceNode       = planned.source;
        const gc_node_ref_t nodeRef = planned.ref;
        auto *header = reinterpret_cast<GCNode *>(mutableNodeStorage(payload.nodeBlocks, nodeRef));
        *header      = GCNode{
            .dataIndex   = static_cast<gc_slot_idx_t>(sourceNode->index()),
            .blockCount  = planned.blockCount,
            .normInputs  = appendSlice(sourceNode->normInputs()),
            .withInputs  = appendSlice(sourceNode->withInputs()),
            .ctrlInputs  = appendSlice(sourceNode->ctrlInputs()),
            .normOutputs = appendSlice(sourceNode->normOutputs()),
            .withOutputs = appendSlice(sourceNode->withOutputs()),
            .ctrlOutputs = appendSlice(sourceNode->ctrlOutputs()),
            .dataType    = sourceNode->dataType(),
            .kind        = toRuntimeNodeKind(sourceNode->type()),
            .flags       = static_cast<uint8_t>(
                (sourceNode->macro() ? kGCNodeFlagMacro : 0) |
                (sourceNode->constant() ? kGCNodeFlagConstant : 0)),
        };

        switch (sourceNode->type()) {
        case GIR::NodeType::FUNC: {
            auto *funcNode = tt::as_ptr<GIR::FuncNode>(sourceNode);
            ASSERT(
                funcNode != nullptr && funcNode->bodyGraph() != nullptr,
                "Runtime FUNC node requires a body graph.");
            writeBody(
                payload.nodeBlocks,
                nodeRef,
                GCFuncBody{.calleeGraph = resolveRuntimeGraph(funcNode->bodyGraph())});
        } break;
        case GIR::NodeType::CALL:
            writeBody(payload.nodeBlocks, nodeRef, GCCallBody{});
            break;
        case GIR::NodeType::ACCS: {
            auto *accsNode = tt::as_ptr<GIR::AccsNode>(sourceNode);
            ASSERT(accsNode != nullptr, "Runtime ACCS node layout requires ACCS metadata.");
            GCAccsBody body{
                .accsKind  = accsNode->isNum() ? GCAccsKind::TupleIndex : GCAccsKind::StructKey,
                .reserved0 = 0,
                .keyBytes  = narrowIntegral<uint16_t>(
                    accsNode->isNum() ? 0 : accsNode->strIndex().size(),
                    "Runtime ACCS key length"),
                .value = narrowIntegral<uint32_t>(
                    accsNode->isNum() ? accsNode->numIndex() : 0,
                    "Runtime ACCS numeric index"),
            };
            writeBody(payload.nodeBlocks, nodeRef, body);
            if (!accsNode->isNum()) {
                writeTrailingBytes(
                    payload.nodeBlocks,
                    nodeRef,
                    sizeof(GCAccsBody),
                    accsNode->strIndex());
            }
        } break;
        case GIR::NodeType::FILL:
            writeBody(
                payload.nodeBlocks,
                nodeRef,
                GCFillBody{.fillKind = classifyFillKind(sourceNode->dataType())});
            break;
        case GIR::NodeType::BRCH: {
            auto *brchNode = tt::as_ptr<GIR::BrchNode>(sourceNode);
            ASSERT(
                brchNode != nullptr && brchNode->matchedJoin() != nullptr,
                "Runtime BRCH node requires a matched JOIN.");
            const gc_off_t armOffset = branchArmCursor;
            const gc_cnt_t armCount =
                narrowIntegral<gc_cnt_t>(brchNode->armCount(), "Runtime branch-arm count");
            for (size_t i = 0; i < brchNode->armCount(); ++i) {
                payload.branchArms[branchArmCursor++] = GCBranchArm{
                    .head = plan.nodeRefs.at(brchNode->armHead(i)),
                    .tail = plan.nodeRefs.at(brchNode->matchedJoin()->armTail(i)),
                };
            }
            writeBody(
                payload.nodeBlocks,
                nodeRef,
                GCBrchBody{
                    .join       = plan.nodeRefs.at(brchNode->matchedJoin()),
                    .armOffset  = armOffset,
                    .armCount   = armCount,
                    .defaultArm = kInvalidNodeRef,
                });
        } break;
        case GIR::NodeType::JOIN: {
            auto *joinNode = tt::as_ptr<GIR::JoinNode>(sourceNode);
            ASSERT(joinNode != nullptr, "Runtime JOIN node layout requires JOIN metadata.");
            GCJoinBody body{
                .brch = kInvalidNodeRef,
                .armCount =
                    narrowIntegral<gc_cnt_t>(joinNode->armCount(), "Runtime JOIN arm count"),
                .reserved = 0,
            };
            for (GIR::Node *input : sourceNode->normInputs()) {
                if (input && input->type() == GIR::NodeType::BRCH) {
                    body.brch = plan.nodeRefs.at(input);
                    break;
                }
            }
            writeBody(payload.nodeBlocks, nodeRef, body);
        } break;
        case GIR::NodeType::OPER: {
            auto *operNode = tt::as_ptr<GIR::OperNode>(sourceNode);
            ASSERT(
                operNode != nullptr && operNode->oper() != nullptr,
                "Runtime OPER node layout requires operator metadata.");
            GCOperBody body{
                .op       = operNode->getCachedOp(),
                .uriBytes = narrowIntegral<uint16_t>(
                    operNode->oper()->uri().size(),
                    "Runtime OPER URI length"),
                .reserved = 0,
            };
            writeBody(payload.nodeBlocks, nodeRef, body);
            writeTrailingBytes(
                payload.nodeBlocks,
                nodeRef,
                sizeof(GCOperBody),
                operNode->oper()->uri());
        } break;
        default:
            break;
        }
    }

    ASSERT(
        edgeCursor == payload.edgeCount,
        "Runtime edge emission did not match planned edge count.");
    ASSERT(
        branchArmCursor == payload.branchArmCount,
        "Runtime branch-arm emission did not match planned branch-arm count.");

    for (size_t i = 0; i < graph->normPorts().size(); ++i) {
        payload.normPorts[i] = plan.nodeRefs.at(graph->normPorts()[i]);
    }
    for (size_t i = 0; i < graph->withPorts().size(); ++i) {
        payload.withPorts[i] = plan.nodeRefs.at(graph->withPorts()[i]);
    }
    for (size_t i = 0; i < graph->closure().size(); ++i) {
        payload.closure[i] = plan.nodeRefs.at(graph->closure()[i]);
    }

    payload.entryNode = plan.nodes.empty() ? kInvalidNodeRef : plan.nodes.front().ref;
    payload.exitNode  = graph->exitNode() ? plan.nodeRefs.at(graph->exitNode()) : kInvalidNodeRef;
    payload.outputNode =
        graph->outputNode() ? plan.nodeRefs.at(graph->outputNode()) : payload.exitNode;
    payload.returnNode = payload.exitNode;
    payload.returnKind = GCReturnKind::None;
    if (GIR::Node *exitNode = graph->exitNode()) {
        if (exitNode->index() != 0) {
            payload.returnKind = GCReturnKind::Self;
            payload.returnNode = plan.nodeRefs.at(exitNode);
        } else if (!exitNode->dataInputs().empty()) {
            payload.returnKind = GCReturnKind::LastDataInput;
            payload.returnNode = plan.nodeRefs.at(exitNode->dataInputs().back());
        } else if (!exitNode->ctrlInputs().empty()) {
            payload.returnKind = GCReturnKind::LastCtrlInput;
            payload.returnNode = plan.nodeRefs.at(exitNode->ctrlInputs().back());
        }
    }

    return payload;
}

} // namespace

GCGraphMetadataRecord::~GCGraphMetadataRecord() {
    graphFreeOwned(ownedStaticArea);
    graphFreeArray(nativePayload.nodeBlocks);
    graphFreeArray(nativePayload.edges);
    graphFreeArray(nativePayload.normPorts);
    graphFreeArray(nativePayload.withPorts);
    graphFreeArray(nativePayload.closure);
    graphFreeArray(nativePayload.branchArms);
}

GCGraph::GCGraph(GCGraphMetadataRecord *metadata) : metadata_(metadata) {}

GIR::Graph *GCGraph::compileGraph() const { return metadata_->sourceGraph.get(); }

const GIR::graph_ptr_t &GCGraph::sourceGraph() const { return metadata_->sourceGraph; }

const std::string &GCGraph::stableId() const { return metadata_->stableId; }

const std::string &GCGraph::mangledName() const { return metadata_->mangledName; }

const std::string &GCGraph::name() const { return metadata_->name; }

camel::core::type::FunctionType *GCGraph::funcType() const { return metadata_->funcType; }

const TupleType *GCGraph::runtimeDataType() const { return metadata_->runtimeDataType; }

const TupleType *GCGraph::staticDataType() const { return metadata_->staticDataType; }

const TupleType *GCGraph::closureType() const { return metadata_->closureType; }

bool GCGraph::hasFrameLayout() const { return metadata_->hasFrameLayout; }

size_t GCGraph::frameSize() const { return metadata_->frameSize; }

bool GCGraph::isMacro() const { return metadata_->isMacro; }

bool GCGraph::isRoot() const { return metadata_->isRoot; }

uintptr_t GCGraph::extraSlot(size_t index) const {
    ASSERT(index < kExtraSlotCount, "GCGraph extra slot index out of range.");
    return extraSlots_[index];
}

void GCGraph::setExtraSlot(size_t index, uintptr_t value) {
    ASSERT(index < kExtraSlotCount, "GCGraph extra slot index out of range.");
    extraSlots_[index] = value;
}

void GCGraph::clearExtraSlots() {
    for (uintptr_t &slot : extraSlots_) {
        slot = 0;
    }
}

const std::vector<GCGraph *> &GCGraph::dependencies() const { return metadata_->dependencies; }

const std::vector<GCGraph *> &GCGraph::subGraphs() const { return metadata_->subGraphs; }

const std::vector<GCGraph *> &GCGraph::staticGraphRefs() const {
    return metadata_->staticGraphRefs;
}

std::span<const slot_t> GCGraph::staticSlots() const {
    return staticArea_ ? std::span<const slot_t>(staticArea_->data(), staticArea_->size())
                       : std::span<const slot_t>{};
}

bool GCGraph::hasNodePayload() const { return !metadata_->nativePayload.empty(); }

const GCGraphNativePayload *GCGraph::nodePayload() const {
    return metadata_->nativePayload.empty() ? nullptr : &metadata_->nativePayload;
}

gc_cnt_t GCGraph::nodeCount() const { return metadata_->nativePayload.nodeCount; }

gc_cnt_t GCGraph::nodeBlockCount() const { return metadata_->nativePayload.nodeBlockCount; }

gc_cnt_t GCGraph::edgeCount() const { return metadata_->nativePayload.edgeCount; }

gc_cnt_t GCGraph::branchArmCount() const { return metadata_->nativePayload.branchArmCount; }

const GCNode *GCGraph::entryNode() const { return node(entryNodeRef()); }

gc_node_ref_t GCGraph::entryNodeRef() const { return metadata_->nativePayload.entryNode; }

const GCNode *GCGraph::exitNode() const { return node(exitNodeRef()); }

gc_node_ref_t GCGraph::exitNodeRef() const { return metadata_->nativePayload.exitNode; }

const GCNode *GCGraph::outputNode() const { return node(outputNodeRef()); }

gc_node_ref_t GCGraph::outputNodeRef() const { return metadata_->nativePayload.outputNode; }

const GCNode *GCGraph::returnNode() const { return node(returnNodeRef()); }

gc_node_ref_t GCGraph::returnNodeRef() const { return metadata_->nativePayload.returnNode; }

GCReturnKind GCGraph::returnKind() const { return metadata_->nativePayload.returnKind; }

const GCNode *GCGraph::node(gc_node_ref_t ref) const {
    const auto &payload = metadata_->nativePayload;
    if (!payload.nodeBlocks || ref == kInvalidNodeRef || ref >= payload.nodeBlockCount) {
        return nullptr;
    }
    return reinterpret_cast<const GCNode *>(nodeStorage(payload.nodeBlocks, ref));
}

bool GCGraph::containsNodeRef(gc_node_ref_t ref) const { return node(ref) != nullptr; }

gc_node_ref_t GCGraph::nodeRef(const GCNode *nodePtr) const {
    const auto &payload = metadata_->nativePayload;
    if (!payload.nodeBlocks || !nodePtr) {
        return kInvalidNodeRef;
    }

    const auto *base      = reinterpret_cast<const std::byte *>(payload.nodeBlocks);
    const auto *nodeBytes = reinterpret_cast<const std::byte *>(nodePtr);
    const auto *limit     = base + static_cast<size_t>(payload.nodeBlockCount) * kGCBlockBytes;
    if (nodeBytes < base || nodeBytes >= limit) {
        return kInvalidNodeRef;
    }

    const size_t byteOffset = static_cast<size_t>(nodeBytes - base);
    if ((byteOffset % kGCBlockBytes) != 0) {
        return kInvalidNodeRef;
    }
    return narrowIntegral<gc_node_ref_t>(byteOffset / kGCBlockBytes, "Runtime node reference");
}

GCGraph *GCGraph::directCalleeGraphOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    if (!n || n->kind != GCNodeKind::Func || !n->hasBody()) {
        return nullptr;
    }
    return n->bodyAs<GCFuncBody>()->calleeGraph;
}

gc_node_ref_t GCGraph::matchedJoinOutputOf(gc_node_ref_t ref) const {
    const auto outputs = withOutputsOf(ref);
    if (outputs.size() != 1) {
        return kInvalidNodeRef;
    }
    const gc_node_ref_t candidate = outputs.front();
    const GCNode *joinNode        = node(candidate);
    if (!joinNode || joinNode->kind != GCNodeKind::Join) {
        return kInvalidNodeRef;
    }
    return candidate;
}

gc_node_ref_t GCGraph::nextNodeRef(gc_node_ref_t ref) const {
    const GCNode *current = node(ref);
    if (!current) {
        return nodeBlockCount();
    }
    return narrowIntegral<gc_node_ref_t>(
        static_cast<size_t>(ref) + current->blockCount,
        "Runtime next-node reference");
}

const GCNode *GCGraph::nextNode(gc_node_ref_t ref) const { return node(nextNodeRef(ref)); }

std::span<const gc_node_ref_t> GCGraph::edgeSlice(Slice slice) const {
    const auto &payload = metadata_->nativePayload;
    return sliceView(payload.edges, slice, payload.edgeCount);
}

std::span<const gc_node_ref_t> GCGraph::normInputsOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    return n ? edgeSlice(n->normInputs) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GCGraph::withInputsOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    return n ? edgeSlice(n->withInputs) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GCGraph::ctrlInputsOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    return n ? edgeSlice(n->ctrlInputs) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GCGraph::normOutputsOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    return n ? edgeSlice(n->normOutputs) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GCGraph::withOutputsOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    return n ? edgeSlice(n->withOutputs) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GCGraph::ctrlOutputsOf(gc_node_ref_t ref) const {
    const GCNode *n = node(ref);
    return n ? edgeSlice(n->ctrlOutputs) : std::span<const gc_node_ref_t>{};
}

std::span<const gc_node_ref_t> GCGraph::normPorts() const {
    const auto &payload = metadata_->nativePayload;
    if (!payload.normPorts) {
        return {};
    }
    return {payload.normPorts, payload.normPortCount};
}

std::span<const gc_node_ref_t> GCGraph::withPorts() const {
    const auto &payload = metadata_->nativePayload;
    if (!payload.withPorts) {
        return {};
    }
    return {payload.withPorts, payload.withPortCount};
}

std::span<const gc_node_ref_t> GCGraph::closureNodes() const {
    const auto &payload = metadata_->nativePayload;
    if (!payload.closure) {
        return {};
    }
    return {payload.closure, payload.closureCount};
}

std::span<const GCBranchArm> GCGraph::branchArmsOf(gc_node_ref_t brchRef) const {
    const auto &payload = metadata_->nativePayload;
    const GCNode *n     = node(brchRef);
    if (!n || n->kind != GCNodeKind::Brch) {
        return {};
    }
    const auto *body = n->bodyAs<GCBrchBody>();
    return branchSliceView(
        payload.branchArms,
        body->armOffset,
        body->armCount,
        payload.branchArmCount);
}

void GCGraph::addDependency(GCGraph *graph) { pushUnique(metadata_->dependencies, graph); }

void GCGraph::addSubGraph(GCGraph *graph) { pushUnique(metadata_->subGraphs, graph); }

void GCGraph::addStaticGraphRef(GCGraph *graph) { pushUnique(metadata_->staticGraphRefs, graph); }

void GCGraph::setStaticSlots(camel::compile::gir::static_slot_vec_t slots) {
    const TupleType *staticType = staticDataType();
    if (!staticType) {
        metadata_->ownedStaticArea = nullptr;
        staticArea_                = nullptr;
        return;
    }

    staticArea_                = ::Tuple::create(staticType->size(), mm::graphSpace());
    metadata_->ownedStaticArea = staticArea_;
    for (size_t i = 1; i < staticType->size() && i < slots.size(); ++i) {
        staticArea_->set<slot_t>(i, slots[i]);
    }
}

bool GCGraph::equals(
    const camel::core::rtdata::Object *other, const camel::core::type::Type *type,
    bool deep) const {
    (void)type;
    (void)deep;
    auto *otherGraph = dynamic_cast<const GCGraph *>(other);
    return otherGraph != nullptr && otherGraph->metadata_ == metadata_;
}

camel::core::rtdata::Object *GCGraph::clone(
    camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type, bool deep) const {
    (void)type;
    (void)deep;
    void *mem = allocator.alloc(sizeof(GCGraph), alignof(GCGraph));
    if (!mem) {
        throw std::bad_alloc();
    }
    auto *graph        = new (mem) GCGraph(metadata_);
    graph->staticArea_ = staticArea_;
    for (size_t i = 0; i < kExtraSlotCount; ++i) {
        graph->extraSlots_[i] = extraSlots_[i];
    }
    return graph;
}

void GCGraph::print(std::ostream &os, const camel::core::type::Type *type) const {
    (void)type;
    os << "GCGraph(name=" << name() << ", nodeBlocks=" << nodeBlockCount() << ")";
}

void GCGraph::updateRefs(
    const std::function<camel::core::rtdata::Object *(camel::core::rtdata::Object *)> &relocate,
    const camel::core::type::Type *type) {
    (void)type;
    if (staticArea_ && staticDataType()) {
        staticArea_->updateRefs(relocate, staticDataType());
    }
}

GCGraph *GCGraphManager::materializeRoot(const GIR::graph_ptr_t &rootGraph) {
    clear();
    if (!rootGraph) {
        return nullptr;
    }
    std::unordered_map<const GIR::Graph *, GCGraph *> cache;
    root_ = materializeGraph(rootGraph, cache);
    gcRoots_.reserve(graphs_.size());
    for (GCGraph *graph : graphs_) {
        gcRoots_.push_back(graph);
    }
    return root_;
}

GCGraphManager::~GCGraphManager() { clear(); }

GCGraph *GCGraphManager::find(const GIR::Graph *sourceGraph) const {
    auto it = bySource_.find(sourceGraph);
    return it == bySource_.end() ? nullptr : it->second;
}

std::vector<GCGraph *> GCGraphManager::roots() const {
    if (!root_) {
        return {};
    }
    return {root_};
}

std::vector<GCGraph *> GCGraphManager::reachableFromRoots() const {
    return collectReachableGraphs(root_);
}

void GCGraphManager::clear() {
    gcRoots_.clear();
    for (GCGraph *graph : graphs_) {
        graphFreeOwned(graph);
    }
    graphs_.clear();
    for (auto *record : metadataRecords_) {
        delete record;
    }
    metadataRecords_.clear();
    bySource_.clear();
    root_ = nullptr;
}

GCGraph *GCGraphManager::materializeGraph(
    const GIR::graph_ptr_t &sourceGraph, std::unordered_map<const GIR::Graph *, GCGraph *> &cache) {
    if (!sourceGraph) {
        return nullptr;
    }
    if (auto it = cache.find(sourceGraph.get()); it != cache.end()) {
        return it->second;
    }

    auto *metadataRecord            = new GCGraphMetadataRecord();
    metadataRecord->sourceGraph     = sourceGraph;
    metadataRecord->stableId        = sourceGraph->stableId();
    metadataRecord->mangledName     = sourceGraph->mangledName();
    metadataRecord->name            = sourceGraph->name();
    metadataRecord->funcType        = sourceGraph->funcType();
    metadataRecord->runtimeDataType = const_cast<TupleType *>(sourceGraph->runtimeDataType());
    metadataRecord->staticDataType  = const_cast<TupleType *>(sourceGraph->staticDataType());
    metadataRecord->closureType     = const_cast<TupleType *>(sourceGraph->closureType());
    metadataRecord->frameSize       = sourceGraph->frameSize();
    metadataRecord->hasFrameLayout  = sourceGraph->hasFrameLayout();
    metadataRecord->isMacro         = sourceGraph->isMacro();
    metadataRecord->isRoot          = sourceGraph->isRoot();

    GCGraph *runtimeGraphRaw = graphAllocObject<GCGraph>(metadataRecord);
    metadataRecords_.push_back(metadataRecord);
    graphs_.push_back(runtimeGraphRaw);
    cache[sourceGraph.get()]     = runtimeGraphRaw;
    bySource_[sourceGraph.get()] = runtimeGraphRaw;

    for (const auto &dep : sourceGraph->dependencies()) {
        runtimeGraphRaw->addDependency(materializeGraph(dep, cache));
    }
    for (const auto &[_, subGraphs] : sourceGraph->subGraphs()) {
        for (const auto &subGraph : subGraphs) {
            runtimeGraphRaw->addSubGraph(materializeGraph(subGraph, cache));
        }
    }

    metadataRecord->nativePayload =
        buildNativePayload(sourceGraph, [&](const GIR::Graph *target) -> GCGraph * {
            if (!target) {
                return nullptr;
            }
            auto targetHandle = resolveGraphHandle(sourceGraph, target);
            if (!targetHandle) {
                try {
                    targetHandle = std::const_pointer_cast<GIR::Graph>(target->shared_from_this());
                } catch (const std::bad_weak_ptr &) {
                    targetHandle = nullptr;
                }
            }
            return targetHandle ? materializeGraph(targetHandle, cache) : nullptr;
        });

    for (GIR::Node *node : collectNativeNodes(sourceGraph)) {
        if (!node) {
            continue;
        }
        if (node->type() == GIR::NodeType::FUNC) {
            auto *funcNode = tt::as_ptr<GIR::FuncNode>(node);
            if (funcNode && funcNode->bodyGraph()) {
                auto targetGraph = resolveGraphHandle(sourceGraph, funcNode->bodyGraph());
                if (!targetGraph) {
                    try {
                        targetGraph = funcNode->bodyGraph()->shared_from_this();
                    } catch (const std::bad_weak_ptr &) {
                        targetGraph = nullptr;
                    }
                }
                if (targetGraph) {
                    runtimeGraphRaw->addDependency(materializeGraph(targetGraph, cache));
                }
            }
            continue;
        }
        if (node->type() != GIR::NodeType::DATA) {
            continue;
        }
        auto *dataNode = tt::as_ptr<GIR::DataNode>(node);
        if (dataNode->dataType()->code() != TypeCode::Function) {
            continue;
        }
        auto *funcObj = fromSlot<::Function *>(dataNode->dataSlot());
        if (funcObj && funcObj->sourceGraph()) {
            GCGraph *runtimeRef = find(funcObj->sourceGraph());
            if (!runtimeRef) {
                auto targetGraph = resolveGraphHandle(sourceGraph, funcObj->sourceGraph());
                if (!targetGraph) {
                    try {
                        targetGraph = funcObj->sourceGraph()->shared_from_this();
                    } catch (const std::bad_weak_ptr &) {
                        targetGraph = nullptr;
                    }
                }
                if (targetGraph) {
                    runtimeRef = materializeGraph(targetGraph, cache);
                }
            }
            if (runtimeRef) {
                runtimeGraphRaw->addStaticGraphRef(runtimeRef);
            }
        }
    }

    camel::compile::gir::static_slot_vec_t runtimeStaticSlots(
        sourceGraph->staticDataSize(),
        NullSlot);
    const TupleType *staticType = sourceGraph->staticDataType();
    std::unordered_map<const Object *, Object *> objectCache;
    if (staticType) {
        for (size_t i = 1; i < sourceGraph->staticDataSize() && i < staticType->size(); ++i) {
            runtimeStaticSlots[i] = canonicalizeStaticSlot(
                sourceGraph->getStaticDataSlot(-static_cast<GIR::data_idx_t>(i)),
                staticType->typeAt(i),
                cache,
                objectCache,
                sourceGraph.get());
        }
    }
    runtimeGraphRaw->setStaticSlots(std::move(runtimeStaticSlots));

    std::unordered_set<const Object *> visited;
    if (staticType) {
        for (size_t i = 1; i < runtimeGraphRaw->staticSlots().size() && i < staticType->size();
             ++i) {
            collectStaticGraphRefs(
                runtimeGraphRaw,
                runtimeGraphRaw->staticSlots()[i],
                staticType->typeAt(i),
                cache,
                visited);
        }
    }

    return runtimeGraphRaw;
}

slot_t GCGraphManager::canonicalizeStaticSlot(
    slot_t slot, Type *type, std::unordered_map<const GIR::Graph *, GCGraph *> &cache,
    std::unordered_map<const Object *, Object *> &objectCache, const GIR::Graph *sourceOwner) {
    if (!type || !type->isGCTraced() || slot == NullSlot) {
        return slot;
    }

    Object *object = fromSlot<Object *>(slot);
    if (!object) {
        return slot;
    }
    if (auto it = objectCache.find(object); it != objectCache.end()) {
        return toSlot<Object *>(it->second);
    }

    switch (type->code()) {
    case TypeCode::Function: {
        auto *funcObj = fromSlot<::Function *>(slot);
        ASSERT(funcObj != nullptr, "Function static slot payload is null.");
        GIR::Graph *sourceGraph = funcObj->sourceGraph();
        ASSERT(sourceGraph != nullptr, "Function static slot must reference a graph.");
        GCGraph *runtimeGraph =
            funcObj->runtimeGraph()
                ? funcObj->runtimeGraph()
                : materializeGraph(
                      requireOwnedSourceGraphHandle(
                          sourceGraph,
                          std::format(
                              "GCGraphManager::canonicalizeStaticSlot(owner='{}')",
                              sourceOwner ? sourceOwner->name() : "<null>")),
                      cache);
        auto *runtimeFunc = ::Function::create(runtimeGraph, funcObj->tupleType(), mm::autoSpace());
        objectCache.emplace(object, runtimeFunc);

        if (::Tuple *closure = funcObj->tuple()) {
            ::Tuple *runtimeClosure             = runtimeFunc->tuple();
            const TupleType *runtimeClosureType = runtimeFunc->tupleType();
            for (size_t i = 0; i < runtimeClosureType->size(); ++i) {
                runtimeClosure->set<slot_t>(
                    i,
                    canonicalizeStaticSlot(
                        closure->get<slot_t>(i),
                        runtimeClosureType->typeAt(i),
                        cache,
                        objectCache,
                        sourceOwner));
            }
        }

        return toSlot<Object *>(runtimeFunc);
    }
    case TypeCode::Tuple: {
        auto *tuple = fromSlot<::Tuple *>(slot);
        ASSERT(tuple != nullptr, "Tuple static slot payload is null.");
        auto *tupleType    = static_cast<TupleType *>(type);
        auto *runtimeTuple = ::Tuple::create(tupleType->size(), mm::autoSpace());
        objectCache.emplace(object, runtimeTuple);
        for (size_t i = 0; i < tupleType->size(); ++i) {
            runtimeTuple->set<slot_t>(
                i,
                camel::core::type::isGCTraced(tupleType->codeAt(i)) ? canonicalizeStaticSlot(
                                                                          tuple->get<slot_t>(i),
                                                                          tupleType->typeAt(i),
                                                                          cache,
                                                                          objectCache,
                                                                          sourceOwner)
                                                                    : tuple->get<slot_t>(i));
        }
        return toSlot<Object *>(runtimeTuple);
    }
    case TypeCode::Array: {
        auto *array = fromSlot<::Array *>(slot);
        ASSERT(array != nullptr, "Array static slot payload is null.");
        auto *arrayType    = static_cast<ArrayType *>(type);
        auto *runtimeArray = ::Array::create(mm::autoSpace(), array->size());
        objectCache.emplace(object, runtimeArray);
        for (size_t i = 0; i < array->size(); ++i) {
            runtimeArray->set<slot_t>(
                i,
                camel::core::type::isGCTraced(arrayType->elemTypeCode())
                    ? canonicalizeStaticSlot(
                          array->get<slot_t>(i),
                          arrayType->elemType(),
                          cache,
                          objectCache,
                          sourceOwner)
                    : array->get<slot_t>(i));
        }
        return toSlot<Object *>(runtimeArray);
    }
    case TypeCode::Struct: {
        auto *st = fromSlot<::Struct *>(slot);
        ASSERT(st != nullptr, "Struct static slot payload is null.");
        auto *structType    = static_cast<StructType *>(type);
        auto *runtimeStruct = ::Struct::create(structType->size(), mm::autoSpace());
        objectCache.emplace(object, runtimeStruct);
        for (size_t i = 0; i < structType->size(); ++i) {
            runtimeStruct->set<slot_t>(
                i,
                camel::core::type::isGCTraced(structType->codeAt(i)) ? canonicalizeStaticSlot(
                                                                           st->get<slot_t>(i),
                                                                           structType->typeAt(i),
                                                                           cache,
                                                                           objectCache,
                                                                           sourceOwner)
                                                                     : st->get<slot_t>(i));
        }
        return toSlot<Object *>(runtimeStruct);
    }
    default: {
        Object *cloned = object->clone(mm::autoSpace(), type, false);
        objectCache.emplace(object, cloned);
        return toSlot<Object *>(cloned);
    }
    }
}

void GCGraphManager::collectStaticGraphRefs(
    GCGraph *owner, slot_t slot, Type *type,
    std::unordered_map<const GIR::Graph *, GCGraph *> &cache,
    std::unordered_set<const Object *> &visited) {
    if (!type || !type->isGCTraced() || slot == NullSlot) {
        return;
    }

    Object *object = fromSlot<Object *>(slot);
    if (!object || !visited.insert(object).second) {
        return;
    }

    switch (type->code()) {
    case TypeCode::Function: {
        auto *funcObj           = fromSlot<::Function *>(slot);
        GIR::Graph *sourceGraph = funcObj ? funcObj->sourceGraph() : nullptr;
        if (!funcObj || !sourceGraph) {
            return;
        }
        GCGraph *runtimeGraph = funcObj->runtimeGraph()
                                    ? funcObj->runtimeGraph()
                                    : materializeGraph(
                                          requireOwnedSourceGraphHandle(
                                              sourceGraph,
                                              "GCGraphManager::collectStaticGraphRefs",
                                              owner),
                                          cache);
        owner->addStaticGraphRef(runtimeGraph);

        if (::Tuple *closure = funcObj->tuple()) {
            TupleType *closureType = const_cast<TupleType *>(funcObj->tupleType());
            for (size_t i = 0; i < closureType->size(); ++i) {
                if (!camel::core::type::isGCTraced(closureType->codeAt(i))) {
                    continue;
                }
                collectStaticGraphRefs(
                    owner,
                    closure->get<slot_t>(i),
                    closureType->typeAt(i),
                    cache,
                    visited);
            }
        }
    } break;
    case TypeCode::Tuple: {
        auto *tuple     = fromSlot<::Tuple *>(slot);
        auto *tupleType = static_cast<TupleType *>(type);
        for (size_t i = 0; i < tupleType->size(); ++i) {
            if (!camel::core::type::isGCTraced(tupleType->codeAt(i))) {
                continue;
            }
            collectStaticGraphRefs(
                owner,
                tuple->get<slot_t>(i),
                tupleType->typeAt(i),
                cache,
                visited);
        }
    } break;
    case TypeCode::Array: {
        auto *array     = fromSlot<::Array *>(slot);
        auto *arrayType = static_cast<ArrayType *>(type);
        if (!camel::core::type::isGCTraced(arrayType->elemTypeCode())) {
            return;
        }
        for (size_t i = 0; i < array->size(); ++i) {
            collectStaticGraphRefs(
                owner,
                array->get<slot_t>(i),
                arrayType->elemType(),
                cache,
                visited);
        }
    } break;
    case TypeCode::Struct: {
        auto *st         = fromSlot<::Struct *>(slot);
        auto *structType = static_cast<StructType *>(type);
        for (size_t i = 0; i < structType->size(); ++i) {
            if (!camel::core::type::isGCTraced(structType->codeAt(i))) {
                continue;
            }
            collectStaticGraphRefs(
                owner,
                st->get<slot_t>(i),
                structType->typeAt(i),
                cache,
                visited);
        }
    } break;
    default:
        break;
    }
}

} // namespace camel::runtime
