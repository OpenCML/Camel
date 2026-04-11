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
 * Updated: Apr. 11, 2026
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
#include "camel/runtime/draft.h"
#include "runtime/graph_build.h"

#include "camel/compile/gir/nodes.h"
#include "camel/compile/gir/static_function.h"
#include "camel/compile/gir/validate.h"
#include "camel/core/context/frame.h"
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

template <typename T> T *graphCopyArray(std::span<const T> items) {
    if (items.empty()) {
        return nullptr;
    }
    T *dst = graphAllocArray<T>(items.size());
    std::memcpy(dst, items.data(), sizeof(T) * items.size());
    return dst;
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

GCGraph *resolveCompileStaticFunctionGraph(
    GIR::StaticFunction *funcObj, const GIR::graph_ptr_t &ownerSourceGraph,
    const std::function<GCGraph *(const GIR::graph_ptr_t &)> &materializeSourceGraph,
    std::string_view context, GCGraph *owner = nullptr) {
    ASSERT(
        funcObj != nullptr,
        "Compile-time static function graph resolution received a null function.");

    GIR::Graph *sourceGraph = funcObj->graph();
    ASSERT(sourceGraph != nullptr, "Compile-time static function must reference a graph.");

    GIR::graph_ptr_t targetGraph = resolveGraphHandle(ownerSourceGraph, sourceGraph);
    if (!targetGraph) {
        targetGraph = requireOwnedSourceGraphHandle(sourceGraph, context, owner);
    }
    return materializeSourceGraph(targetGraph);
}

void validateRuntimeGraphPayloadImpl(const GCGraph *graph) {
    if (!graph) {
        return;
    }
    if (!graph->hasNodePayload()) {
        throw std::runtime_error(
            std::format("Runtime graph '{}' has no native node payload.", graph->name()));
    }

    const auto validateRef = [&](gc_node_ref_t ref, std::string_view what) {
        if (ref == kInvalidNodeRef) {
            return;
        }
        if (!graph->containsNodeRef(ref)) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has invalid {} node ref {}.",
                    graph->name(),
                    what,
                    ref));
        }
    };

    validateRef(graph->entryNodeRef(), "entry");
    validateRef(graph->exitNodeRef(), "exit");
    validateRef(graph->outputNodeRef(), "output");
    validateRef(graph->returnNodeRef(), "return");

    for (gc_node_ref_t ref = 0; ref < graph->nodeBlockCount();) {
        const GCNode *node = graph->node(ref);
        if (!node) {
            break;
        }
        if (node->blockCount == 0) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' has zero-sized node block at ref {}.",
                    graph->name(),
                    ref));
        }
        auto validateSlice = [&](Slice slice, std::string_view edgeKind) {
            const auto refs = graph->edgeSlice(slice);
            if (slice.count != refs.size()) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has out-of-range {} slice at ref {}.",
                        graph->name(),
                        edgeKind,
                        ref));
            }
            for (gc_node_ref_t edgeRef : refs) {
                validateRef(edgeRef, edgeKind);
            }
        };
        validateSlice(node->normInputs, "norm-input");
        validateSlice(node->withInputs, "with-input");
        validateSlice(node->ctrlInputs, "ctrl-input");
        validateSlice(node->normOutputs, "norm-output");
        validateSlice(node->withOutputs, "with-output");
        validateSlice(node->ctrlOutputs, "ctrl-output");
        if (node->kind == GCNodeKind::Func) {
            const auto *body = node->bodyAs<GCFuncBody>();
            if (!body || !body->calleeGraph) {
                throw std::runtime_error(
                    std::format(
                        "Runtime graph '{}' has FUNC node {} without callee graph.",
                        graph->name(),
                        ref));
            }
        }
        ref = graph->nextNodeRef(ref);
    }
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

struct PlannedDraftNode {
    gc_node_ref_t draftId = kInvalidNodeRef;
    gc_node_ref_t ref     = kInvalidNodeRef;
    gc_cnt_t blockCount   = 0;
};

struct DraftPayloadPlan {
    std::vector<PlannedDraftNode> nodes;
    std::vector<gc_node_ref_t> runtimeRefsByDraftId;
    gc_cnt_t nodeBlockCount = 0;
    gc_cnt_t edgeCount      = 0;
    gc_cnt_t branchArmCount = 0;
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

gc_cnt_t draftNodeBlockCount(const DraftNodeHeader &header) {
    size_t bodyBytes = 0;
    switch (header.kind) {
    case GCNodeKind::Func:
        bodyBytes = sizeof(GCFuncBody);
        break;
    case GCNodeKind::Call:
        bodyBytes = sizeof(GCCallBody);
        break;
    case GCNodeKind::Accs:
    case GCNodeKind::Oper:
        bodyBytes = header.payloadBytes;
        break;
    case GCNodeKind::Fill:
        bodyBytes = sizeof(GCFillBody);
        break;
    case GCNodeKind::Brch:
        bodyBytes = sizeof(GCBrchBody);
        break;
    case GCNodeKind::Join:
        bodyBytes = sizeof(GCJoinBody);
        break;
    default:
        bodyBytes = 0;
        break;
    }
    return blocksForBytes(sizeof(GCNode) + bodyBytes);
}

DraftPayloadPlan planDraftPayload(const GraphDraft &draft) {
    DraftPayloadPlan plan;
    plan.runtimeRefsByDraftId.assign(draft.nodeSlotCount(), kInvalidNodeRef);

    gc_off_t currentNodeOffset = 0;
    size_t edgeCount           = 0;
    size_t branchArmCount      = 0;
    for (gc_node_ref_t draftId = 0; draftId < draft.nodeSlotCount(); ++draftId) {
        if (!draft.alive(draftId)) {
            continue;
        }
        const DraftNodeHeader *header = draft.header(draftId);
        ASSERT(header != nullptr, "Draft payload plan requires a non-null node header.");
        const gc_cnt_t blockCount = draftNodeBlockCount(*header);
        plan.nodes.push_back(
            PlannedDraftNode{
                .draftId    = draftId,
                .ref        = currentNodeOffset,
                .blockCount = blockCount,
            });
        plan.runtimeRefsByDraftId[draftId] = currentNodeOffset;
        currentNodeOffset                  = narrowIntegral<gc_off_t>(
            static_cast<size_t>(currentNodeOffset) + blockCount,
            "Draft runtime node blob size");
        edgeCount += draft.normInputsOf(draftId).size() + draft.withInputsOf(draftId).size() +
                     draft.ctrlInputsOf(draftId).size() + draft.normUsersOf(draftId).size() +
                     draft.withUsersOf(draftId).size() + draft.ctrlUsersOf(draftId).size();
        if (header->kind == GCNodeKind::Brch) {
            branchArmCount += draft.branchArmsOf(draftId).size();
        }
    }

    plan.nodeBlockCount =
        narrowIntegral<gc_cnt_t>(currentNodeOffset, "Draft runtime node-blob block count");
    plan.edgeCount = narrowIntegral<gc_cnt_t>(edgeCount, "Draft runtime edge arena size");
    plan.branchArmCount =
        narrowIntegral<gc_cnt_t>(branchArmCount, "Draft runtime branch-arm arena size");
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

GCGraphNativePayload buildNativePayload(const GraphDraft &draft) {
    GCGraphNativePayload payload;
    const DraftPayloadPlan plan = planDraftPayload(draft);
    payload.nodeCount = narrowIntegral<gc_cnt_t>(plan.nodes.size(), "Draft runtime node count");
    payload.nodeBlockCount = plan.nodeBlockCount;
    payload.edgeCount      = plan.edgeCount;
    payload.normPortCount =
        narrowIntegral<gc_cnt_t>(draft.normPorts().size(), "Draft runtime norm-port count");
    payload.withPortCount =
        narrowIntegral<gc_cnt_t>(draft.withPorts().size(), "Draft runtime with-port count");
    payload.closureCount =
        narrowIntegral<gc_cnt_t>(draft.closureNodes().size(), "Draft runtime closure count");
    payload.branchArmCount = plan.branchArmCount;

    payload.nodeBlocks = graphAllocArray<gc_block_t>(payload.nodeBlockCount);
    payload.edges      = graphAllocArray<gc_node_ref_t>(payload.edgeCount);
    payload.normPorts  = graphAllocArray<gc_node_ref_t>(payload.normPortCount);
    payload.withPorts  = graphAllocArray<gc_node_ref_t>(payload.withPortCount);
    payload.closure    = graphAllocArray<gc_node_ref_t>(payload.closureCount);
    payload.branchArms = graphAllocArray<GCBranchArm>(payload.branchArmCount);

    gc_off_t edgeCursor      = 0;
    gc_off_t branchArmCursor = 0;
    auto appendSlice         = [&](std::span<const gc_node_ref_t> refs) -> Slice {
        Slice slice{.offset = edgeCursor, .count = 0};
        for (gc_node_ref_t draftRef : refs) {
            ASSERT(
                draftRef < plan.runtimeRefsByDraftId.size(),
                "Draft edge emission encountered a node outside the payload plan.");
            const gc_node_ref_t runtimeRef = plan.runtimeRefsByDraftId[draftRef];
            ASSERT(runtimeRef != kInvalidNodeRef, "Draft edge emission encountered a dead node.");
            payload.edges[edgeCursor++] = runtimeRef;
        }
        slice.count = narrowIntegral<gc_cnt_t>(edgeCursor - slice.offset, "Draft edge slice count");
        return slice;
    };

    for (const PlannedDraftNode &planned : plan.nodes) {
        const DraftNodeHeader *draftHeader = draft.header(planned.draftId);
        ASSERT(draftHeader != nullptr, "Draft payload emission requires a non-null node header.");
        auto *header =
            reinterpret_cast<GCNode *>(mutableNodeStorage(payload.nodeBlocks, planned.ref));
        *header = GCNode{
            .dataIndex   = draftHeader->dataIndex,
            .blockCount  = planned.blockCount,
            .normInputs  = appendSlice(draft.normInputsOf(planned.draftId)),
            .withInputs  = appendSlice(draft.withInputsOf(planned.draftId)),
            .ctrlInputs  = appendSlice(draft.ctrlInputsOf(planned.draftId)),
            .normOutputs = appendSlice(draft.normUsersOf(planned.draftId)),
            .withOutputs = appendSlice(draft.withUsersOf(planned.draftId)),
            .ctrlOutputs = appendSlice(draft.ctrlUsersOf(planned.draftId)),
            .dataType    = draftHeader->dataType,
            .kind        = draftHeader->kind,
            .flags       = draftHeader->runtimeFlags,
        };

        const auto payloadBytes = draft.payloadOf(planned.draftId);
        switch (draftHeader->kind) {
        case GCNodeKind::Func:
            if (!payloadBytes.empty()) {
                writeBody(
                    payload.nodeBlocks,
                    planned.ref,
                    *reinterpret_cast<const GCFuncBody *>(payloadBytes.data()));
            }
            break;
        case GCNodeKind::Call:
            writeBody(
                payload.nodeBlocks,
                planned.ref,
                payloadBytes.empty() ? GCCallBody{}
                                     : *reinterpret_cast<const GCCallBody *>(payloadBytes.data()));
            break;
        case GCNodeKind::Accs:
        case GCNodeKind::Oper:
            if (!payloadBytes.empty()) {
                std::memcpy(
                    mutableNodeStorage(payload.nodeBlocks, planned.ref) + sizeof(GCNode),
                    payloadBytes.data(),
                    payloadBytes.size_bytes());
            }
            break;
        case GCNodeKind::Fill:
            writeBody(
                payload.nodeBlocks,
                planned.ref,
                payloadBytes.empty() ? GCFillBody{}
                                     : *reinterpret_cast<const GCFillBody *>(payloadBytes.data()));
            break;
        case GCNodeKind::Brch: {
            const auto *draftBody = reinterpret_cast<const DraftBrchPayload *>(payloadBytes.data());
            ASSERT(draftBody != nullptr, "Draft BRCH payload is missing.");
            const gc_off_t armOffset = branchArmCursor;
            const auto arms          = draft.branchArmsOf(planned.draftId);
            for (const GCBranchArm &arm : arms) {
                payload.branchArms[branchArmCursor++] = GCBranchArm{
                    .head = plan.runtimeRefsByDraftId[arm.head],
                    .tail = plan.runtimeRefsByDraftId[arm.tail],
                };
            }
            writeBody(
                payload.nodeBlocks,
                planned.ref,
                GCBrchBody{
                    .join       = plan.runtimeRefsByDraftId[draftBody->join],
                    .armOffset  = armOffset,
                    .armCount   = draftBody->armCount,
                    .defaultArm = draftBody->defaultArm == kInvalidNodeRef
                                      ? kInvalidNodeRef
                                      : plan.runtimeRefsByDraftId[draftBody->defaultArm],
                });
        } break;
        case GCNodeKind::Join: {
            GCJoinBody body = payloadBytes.empty()
                                  ? GCJoinBody{}
                                  : *reinterpret_cast<const GCJoinBody *>(payloadBytes.data());
            body.brch       = body.brch == kInvalidNodeRef ? kInvalidNodeRef
                                                           : plan.runtimeRefsByDraftId[body.brch];
            writeBody(payload.nodeBlocks, planned.ref, body);
        } break;
        default:
            break;
        }
    }

    for (size_t i = 0; i < draft.normPorts().size(); ++i) {
        payload.normPorts[i] = plan.runtimeRefsByDraftId[draft.normPorts()[i]];
    }
    for (size_t i = 0; i < draft.withPorts().size(); ++i) {
        payload.withPorts[i] = plan.runtimeRefsByDraftId[draft.withPorts()[i]];
    }
    for (size_t i = 0; i < draft.closureNodes().size(); ++i) {
        payload.closure[i] = plan.runtimeRefsByDraftId[draft.closureNodes()[i]];
    }

    payload.entryNode  = draft.entryNode() == kInvalidNodeRef
                             ? (plan.nodes.empty() ? kInvalidNodeRef : plan.nodes.front().ref)
                             : plan.runtimeRefsByDraftId[draft.entryNode()];
    payload.exitNode   = draft.exitNode() == kInvalidNodeRef
                             ? kInvalidNodeRef
                             : plan.runtimeRefsByDraftId[draft.exitNode()];
    payload.outputNode = draft.outputNode() == kInvalidNodeRef
                             ? payload.exitNode
                             : plan.runtimeRefsByDraftId[draft.outputNode()];
    payload.returnNode = draft.returnNode() == kInvalidNodeRef
                             ? kInvalidNodeRef
                             : plan.runtimeRefsByDraftId[draft.returnNode()];
    payload.returnKind = draft.returnKind();

    return payload;
}

} // namespace

slot_t canonicalizeStaticSlotRecursive(
    slot_t slot, Type *type, std::unordered_map<const GIR::Graph *, GCGraph *> &cache,
    std::unordered_map<const Object *, Object *> &objectCache, const GIR::Graph *sourceOwner);
void collectStaticGraphRefsRecursive(
    std::vector<GCGraph *> &refs, slot_t slot, Type *type,
    std::unordered_set<const Object *> &visited);

GCGraphDebugRecord *
createGraphDebugRecord(std::string stableId, std::string mangledName, std::string name) {
    auto *record        = new GCGraphDebugRecord();
    record->stableId    = std::move(stableId);
    record->mangledName = std::move(mangledName);
    record->name        = std::move(name);
    return record;
}

GCGraph **copyGraphRefsToGraphHeap(std::span<GCGraph *const> graphs) {
    return graphCopyArray<GCGraph *>(graphs);
}

::Tuple *buildStaticAreaInGraphHeap(const TupleType *staticType, std::span<const slot_t> slots) {
    if (!staticType) {
        return nullptr;
    }
    ::Tuple *staticArea = ::Tuple::create(staticType->size(), mm::graphSpace());
    for (size_t i = 1; i < staticType->size() && i < slots.size(); ++i) {
        staticArea->set<slot_t>(i, slots[i]);
    }
    return staticArea;
}

GCGraph *GCGraphBuildAccess::create(
    GCGraphDebugRecord *debugRecord, camel::core::type::FunctionType *funcType,
    camel::core::type::TupleType *runtimeDataType, camel::core::type::TupleType *staticDataType,
    camel::core::type::TupleType *closureType, GCGraph *outerGraph,
    std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
    std::span<GCGraph *const> staticGraphRefs, const GCGraphNativePayload &payload,
    std::span<const slot_t> staticSlots) {
    void *mem = mm::graphSpace().alloc(sizeof(GCGraph), alignof(GCGraph));
    if (!mem) {
        throw std::bad_alloc();
    }

    auto *graph = new (mem) GCGraph(
        debugRecord,
        funcType,
        runtimeDataType,
        staticDataType,
        closureType,
        outerGraph,
        copyGraphRefsToGraphHeap(dependencies),
        copyGraphRefsToGraphHeap(subGraphs),
        copyGraphRefsToGraphHeap(staticGraphRefs),
        narrowIntegral<gc_cnt_t>(dependencies.size(), "Runtime dependency count"),
        narrowIntegral<gc_cnt_t>(subGraphs.size(), "Runtime subgraph count"),
        narrowIntegral<gc_cnt_t>(staticGraphRefs.size(), "Runtime static-graph-ref count"),
        payload,
        buildStaticAreaInGraphHeap(staticDataType, staticSlots));

    for (GCGraph *subGraph : subGraphs) {
        if (subGraph) {
            subGraph->outerGraph_ = graph;
        }
    }
    return graph;
}

void GCGraphBuildAccess::rewrite(
    GCGraph *graph, TupleType *staticDataType, GCGraph *outerGraph,
    std::span<GCGraph *const> dependencies, std::span<GCGraph *const> subGraphs,
    std::span<GCGraph *const> staticGraphRefs, std::span<const slot_t> staticSlots,
    const GCGraphNativePayload *payload) {
    ASSERT(graph != nullptr, "GCGraph overwrite requires a valid graph.");
    graph->staticDataType_ = staticDataType;
    graph->outerGraph_     = outerGraph;
    if (payload) {
        graph->nativePayload_ = *payload;
    }
    graph->dependencies_    = copyGraphRefsToGraphHeap(dependencies);
    graph->subGraphs_       = copyGraphRefsToGraphHeap(subGraphs);
    graph->staticGraphRefs_ = copyGraphRefsToGraphHeap(staticGraphRefs);
    graph->dependencyCount_ =
        narrowIntegral<gc_cnt_t>(dependencies.size(), "Runtime dependency count");
    graph->subGraphCount_ = narrowIntegral<gc_cnt_t>(subGraphs.size(), "Runtime subgraph count");
    graph->staticGraphRefCount_ =
        narrowIntegral<gc_cnt_t>(staticGraphRefs.size(), "Runtime static-graph-ref count");
    graph->staticArea_ = buildStaticAreaInGraphHeap(staticDataType, staticSlots);
    for (GCGraph *subGraph : subGraphs) {
        if (subGraph) {
            subGraph->outerGraph_ = graph;
        }
    }
}

GCGraph::GCGraph(
    GCGraphDebugRecord *debugRecord, camel::core::type::FunctionType *funcType,
    camel::core::type::TupleType *runtimeDataType, camel::core::type::TupleType *staticDataType,
    camel::core::type::TupleType *closureType, GCGraph *outerGraph, GCGraph **dependencies,
    GCGraph **subGraphs, GCGraph **staticGraphRefs, gc_cnt_t dependencyCount,
    gc_cnt_t subGraphCount, gc_cnt_t staticGraphRefCount, const GCGraphNativePayload &payload,
    ::Tuple *staticArea)
    : debug_(debugRecord), funcType_(funcType), runtimeDataType_(runtimeDataType),
      staticDataType_(staticDataType), closureType_(closureType), outerGraph_(outerGraph),
      dependencies_(dependencies), subGraphs_(subGraphs), staticGraphRefs_(staticGraphRefs),
      dependencyCount_(dependencyCount), subGraphCount_(subGraphCount),
      staticGraphRefCount_(staticGraphRefCount), nativePayload_(payload), staticArea_(staticArea) {}

GCGraphNativePayload buildCompileGraphNativePayload(
    const camel::compile::gir::graph_ptr_t &graph,
    const std::function<GCGraph *(const camel::compile::gir::Graph *)> &resolveRuntimeGraph) {
    return buildNativePayload(graph, resolveRuntimeGraph);
}

GCGraphNativePayload buildDraftNativePayload(const GraphDraft &draft) {
    return buildNativePayload(draft);
}

void validateRuntimeGraphPayload(const GCGraph *graph) { validateRuntimeGraphPayloadImpl(graph); }

const std::string &GCGraph::stableId() const {
    static const std::string kEmpty;
    return debug_ ? debug_->stableId : kEmpty;
}

const std::string &GCGraph::mangledName() const {
    static const std::string kEmpty;
    return debug_ ? debug_->mangledName : kEmpty;
}

const std::string &GCGraph::name() const {
    static const std::string kEmpty;
    return debug_ ? debug_->name : kEmpty;
}

camel::core::type::FunctionType *GCGraph::funcType() const { return funcType_; }

const TupleType *GCGraph::runtimeDataType() const { return runtimeDataType_; }

const TupleType *GCGraph::staticDataType() const { return staticDataType_; }

const TupleType *GCGraph::closureType() const { return closureType_; }

bool GCGraph::hasFrameLayout() const {
    return staticArea_ != nullptr && runtimeDataType() != nullptr;
}

size_t GCGraph::frameSize() const {
    ASSERT(hasFrameLayout(), "Runtime graph frame layout is not available.");
    return sizeof(camel::core::context::Frame) + sizeof(slot_t) * runtimeDataType()->size();
}

bool GCGraph::isMacro() const {
    auto *type = funcType();
    return type != nullptr && type->modifiers().macro();
}

GCGraph *GCGraph::outerGraph() const { return outerGraph_; }

bool GCGraph::isRoot() const { return outerGraph_ == nullptr; }

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

std::span<GCGraph *const> GCGraph::dependencies() const {
    return {dependencies_, dependencyCount_};
}

std::span<GCGraph *const> GCGraph::subGraphs() const { return {subGraphs_, subGraphCount_}; }

std::span<GCGraph *const> GCGraph::staticGraphRefs() const {
    return {staticGraphRefs_, staticGraphRefCount_};
}

std::span<const slot_t> GCGraph::staticSlots() const {
    return staticArea_ ? std::span<const slot_t>(staticArea_->data(), staticArea_->size())
                       : std::span<const slot_t>{};
}

bool GCGraph::hasNodePayload() const { return !nativePayload_.empty(); }

const GCGraphNativePayload *GCGraph::nodePayload() const {
    return nativePayload_.empty() ? nullptr : &nativePayload_;
}

gc_cnt_t GCGraph::nodeCount() const { return nativePayload_.nodeCount; }

gc_cnt_t GCGraph::nodeBlockCount() const { return nativePayload_.nodeBlockCount; }

gc_cnt_t GCGraph::edgeCount() const { return nativePayload_.edgeCount; }

gc_cnt_t GCGraph::branchArmCount() const { return nativePayload_.branchArmCount; }

const GCNode *GCGraph::entryNode() const { return node(entryNodeRef()); }

gc_node_ref_t GCGraph::entryNodeRef() const { return nativePayload_.entryNode; }

const GCNode *GCGraph::exitNode() const { return node(exitNodeRef()); }

gc_node_ref_t GCGraph::exitNodeRef() const { return nativePayload_.exitNode; }

const GCNode *GCGraph::outputNode() const { return node(outputNodeRef()); }

gc_node_ref_t GCGraph::outputNodeRef() const { return nativePayload_.outputNode; }

const GCNode *GCGraph::returnNode() const { return node(returnNodeRef()); }

gc_node_ref_t GCGraph::returnNodeRef() const { return nativePayload_.returnNode; }

GCReturnKind GCGraph::returnKind() const { return nativePayload_.returnKind; }

const GCNode *GCGraph::node(gc_node_ref_t ref) const {
    const auto &payload = nativePayload_;
    if (!payload.nodeBlocks || ref == kInvalidNodeRef || ref >= payload.nodeBlockCount) {
        return nullptr;
    }
    return reinterpret_cast<const GCNode *>(nodeStorage(payload.nodeBlocks, ref));
}

bool GCGraph::containsNodeRef(gc_node_ref_t ref) const { return node(ref) != nullptr; }

gc_node_ref_t GCGraph::nodeRef(const GCNode *nodePtr) const {
    const auto &payload = nativePayload_;
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
    const auto &payload = nativePayload_;
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
    const auto &payload = nativePayload_;
    if (!payload.normPorts) {
        return {};
    }
    return {payload.normPorts, payload.normPortCount};
}

std::span<const gc_node_ref_t> GCGraph::withPorts() const {
    const auto &payload = nativePayload_;
    if (!payload.withPorts) {
        return {};
    }
    return {payload.withPorts, payload.withPortCount};
}

std::span<const gc_node_ref_t> GCGraph::closureNodes() const {
    const auto &payload = nativePayload_;
    if (!payload.closure) {
        return {};
    }
    return {payload.closure, payload.closureCount};
}

std::span<const GCBranchArm> GCGraph::branchArmsOf(gc_node_ref_t brchRef) const {
    const auto &payload = nativePayload_;
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

bool GCGraph::equals(
    const camel::core::rtdata::Object *other, const camel::core::type::Type *type,
    bool deep) const {
    (void)type;
    (void)deep;
    auto *otherGraph = dynamic_cast<const GCGraph *>(other);
    return otherGraph != nullptr &&
           otherGraph->nativePayload_.nodeBlocks == nativePayload_.nodeBlocks &&
           otherGraph->funcType_ == funcType_;
}

camel::core::rtdata::Object *GCGraph::clone(
    camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type, bool deep) const {
    (void)type;
    (void)deep;
    void *mem = allocator.alloc(sizeof(GCGraph), alignof(GCGraph));
    if (!mem) {
        throw std::bad_alloc();
    }
    auto *graph = new (mem) GCGraph(
        debug_,
        funcType_,
        runtimeDataType_,
        staticDataType_,
        closureType_,
        outerGraph_,
        dependencies_,
        subGraphs_,
        staticGraphRefs_,
        dependencyCount_,
        subGraphCount_,
        staticGraphRefCount_,
        nativePayload_,
        staticArea_);
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

void GCGraphManager::replaceRoot(GCGraph *rootGraph) {
    clear();
    adoptRoot(rootGraph);
}

void GCGraphManager::adoptRoot(GCGraph *rootGraph) {
    root_ = rootGraph;
    if (!root_) {
        return;
    }

    graphs_ = collectReachableGraphs(root_);
    gcRoots_.reserve(graphs_.size());
    for (GCGraph *graph : graphs_) {
        if (!graph) {
            continue;
        }
        gcRoots_.push_back(graph);
        if (graph->debug_) {
            debugRecords_.push_back(graph->debug_);
        }
    }
}

GCGraphManager::~GCGraphManager() { clear(); }

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
    graphs_.clear();
    for (auto *record : debugRecords_) {
        delete record;
    }
    debugRecords_.clear();
    root_ = nullptr;
    mm::graphSpace().reset();
}

slot_t canonicalizeStaticSlotRecursive(
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
        auto *funcObj = fromSlot<GIR::StaticFunction *>(slot);
        ASSERT(funcObj != nullptr, "Function static slot payload is null.");
        GCGraph *runtimeGraph = resolveCompileStaticFunctionGraph(
            funcObj,
            sourceOwner ? requireOwnedSourceGraphHandle(
                              const_cast<GIR::Graph *>(sourceOwner),
                              "GCGraphManager::canonicalizeStaticSlot(owner-source)")
                        : GIR::graph_ptr_t{},
            [&](const GIR::graph_ptr_t &target) { return target ? target->encode() : nullptr; },
            std::format(
                "GCGraphManager::canonicalizeStaticSlot(owner='{}')",
                sourceOwner ? sourceOwner->name() : "<null>"),
            nullptr);
        auto *runtimeFunc = ::Function::create(runtimeGraph, funcObj->tupleType(), mm::autoSpace());
        objectCache.emplace(object, runtimeFunc);

        if (::Tuple *closure = funcObj->tuple()) {
            ::Tuple *runtimeClosure             = runtimeFunc->tuple();
            const TupleType *runtimeClosureType = runtimeFunc->tupleType();
            for (size_t i = 0; i < runtimeClosureType->size(); ++i) {
                runtimeClosure->set<slot_t>(
                    i,
                    canonicalizeStaticSlotRecursive(
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
                camel::core::type::isGCTraced(tupleType->codeAt(i))
                    ? canonicalizeStaticSlotRecursive(
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
                    ? canonicalizeStaticSlotRecursive(
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
                camel::core::type::isGCTraced(structType->codeAt(i))
                    ? canonicalizeStaticSlotRecursive(
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

void collectStaticGraphRefsRecursive(
    std::vector<GCGraph *> &refs, slot_t slot, Type *type,
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
        auto *funcObj = fromSlot<::Function *>(slot);
        if (!funcObj) {
            return;
        }
        GCGraph *runtimeGraph = funcObj->runtimeGraph();
        ASSERT(
            runtimeGraph != nullptr,
            "Runtime static graph references must already point to GCGraph carriers.");
        pushUnique(refs, runtimeGraph);

        if (::Tuple *closure = funcObj->tuple()) {
            TupleType *closureType = const_cast<TupleType *>(funcObj->tupleType());
            for (size_t i = 0; i < closureType->size(); ++i) {
                if (!camel::core::type::isGCTraced(closureType->codeAt(i))) {
                    continue;
                }
                collectStaticGraphRefsRecursive(
                    refs,
                    closure->get<slot_t>(i),
                    closureType->typeAt(i),
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
            collectStaticGraphRefsRecursive(
                refs,
                tuple->get<slot_t>(i),
                tupleType->typeAt(i),
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
            collectStaticGraphRefsRecursive(
                refs,
                array->get<slot_t>(i),
                arrayType->elemType(),
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
            collectStaticGraphRefsRecursive(
                refs,
                st->get<slot_t>(i),
                structType->typeAt(i),
                visited);
        }
    } break;
    default:
        break;
    }
}

} // namespace camel::runtime
