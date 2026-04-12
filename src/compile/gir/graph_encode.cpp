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
 * Created: Apr. 11, 2026
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Compile-side Graph -> GCGraph encoding.
 *
 * This translation unit owns the one-shot pre-execution materialization from
 * mutable compile Graph objects to immutable runtime GCGraph objects. Keeping
 * the session here avoids leaking compile-GIR traversal into runtime graph
 * core.
 */

#include "camel/compile/gir.h"
#include "camel/compile/gir/static_function.h"
#include "camel/compile/gir/validate.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/runtime/draft.h"
#include "graph_builder.h"
#include "runtime/graph_build.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_set>

using camel::core::type::ArrayType;
using camel::core::type::StructType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::TypeCode;

namespace camel::compile::gir {

namespace {

using camel::core::rtdata::fromSlot;
using camel::core::rtdata::Object;
using camel::core::rtdata::toSlot;
using camel::runtime::collectStaticGraphRefsRecursive;
using camel::runtime::createGraphDebugRecord;
using camel::runtime::DraftBrchPayload;
using camel::runtime::DraftNodeHeader;
using camel::runtime::GCAccsBody;
using camel::runtime::GCBranchArm;
using camel::runtime::GCBrchBody;
using camel::runtime::GCCallBody;
using camel::runtime::GCFillBody;
using camel::runtime::GCFuncBody;
using camel::runtime::GCGraph;
using camel::runtime::GCGraphDebugRecord;
using camel::runtime::GCGraphPayloadShape;
using camel::runtime::GCJoinBody;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::GCReturnKind;
using camel::runtime::Slice;

[[noreturn]] void throwEncodeError(std::string message) {
    throw std::runtime_error(std::move(message));
}

template <typename T, typename U> T narrowIntegral(U value, const char *what) {
    if (value > static_cast<U>(std::numeric_limits<T>::max())) {
        throwEncodeError(std::format("{} exceeds the target integral width.", what));
    }
    return static_cast<T>(value);
}

Node *requireDraftSourceNode(
    std::span<Node *const> sourceNodesByDraftId, camel::runtime::gc_node_ref_t draftId,
    NodeType expectedType, std::string_view what) {
    if (draftId >= sourceNodesByDraftId.size()) {
        throwEncodeError(std::format("{} draft id {} is out of range.", what, draftId));
    }
    Node *node = sourceNodesByDraftId[draftId];
    if (!node) {
        throwEncodeError(std::format("{} draft id {} has no source node binding.", what, draftId));
    }
    if (node->type() != expectedType) {
        throwEncodeError(
            std::format(
                "{} draft id {} expected source node type {}, got {}.",
                what,
                draftId,
                static_cast<int>(expectedType),
                static_cast<int>(node->type())));
    }
    return node;
}

Type *effectiveDraftNodeType(const runtime::GraphDraft &draft, const DraftNodeHeader &header) {
    if (header.dataType != nullptr) {
        return header.dataType;
    }
    if (header.dataIndex > 0) {
        if (auto *runtimeType = draft.runtimeDataType();
            runtimeType != nullptr && static_cast<size_t>(header.dataIndex) < runtimeType->size()) {
            return runtimeType->typeAt(static_cast<size_t>(header.dataIndex));
        }
    } else if (header.dataIndex < 0) {
        const size_t staticIndex = static_cast<size_t>(-header.dataIndex);
        const auto staticTypes   = draft.staticSlotTypes();
        if (staticIndex < staticTypes.size()) {
            return staticTypes[staticIndex];
        }
    }
    return nullptr;
}

size_t draftNodeBodyBytes(
    const runtime::GraphDraft &draft, const DraftGraphBuilder &builder,
    camel::runtime::gc_node_ref_t draftId) {
    const DraftNodeHeader *header = draft.header(draftId);
    if (!header) {
        throwEncodeError("Compile encode received a dead draft node.");
    }
    switch (header->kind) {
    case GCNodeKind::Func:
        return sizeof(GCFuncBody);
    case GCNodeKind::Call:
        return sizeof(GCCallBody);
    case GCNodeKind::Accs:
        return draft.payloadOf(draftId).size_bytes();
    case GCNodeKind::Fill:
        return sizeof(GCFillBody);
    case GCNodeKind::Brch:
        return sizeof(GCBrchBody);
    case GCNodeKind::Join:
        return sizeof(GCJoinBody);
    case GCNodeKind::Oper:
        return sizeof(GCOperBody) + builder.operTarget(draftId)->uri().size();
    default:
        return 0;
    }
}

camel::runtime::gc_cnt_t draftNodeBlockCount(
    const runtime::GraphDraft &draft, const DraftGraphBuilder &builder,
    camel::runtime::gc_node_ref_t draftId) {
    return camel::runtime::blocksForBytes(
        sizeof(GCNode) + draftNodeBodyBytes(draft, builder, draftId));
}

std::byte *
mutableNodeStorage(camel::runtime::gc_block_t *blocks, camel::runtime::gc_node_ref_t ref) {
    return reinterpret_cast<std::byte *>(blocks + ref);
}

template <typename Body>
void writeBody(
    camel::runtime::gc_block_t *blocks, camel::runtime::gc_node_ref_t ref, const Body &body) {
    static_assert(
        std::is_trivially_copyable_v<Body>,
        "GC node bodies must remain trivially copyable.");
    auto *dst = mutableNodeStorage(blocks, ref) + sizeof(GCNode);
    std::memcpy(dst, &body, sizeof(Body));
}

void writeTrailingBytes(
    camel::runtime::gc_block_t *blocks, camel::runtime::gc_node_ref_t ref, size_t bodyHeaderBytes,
    std::string_view bytes) {
    if (bytes.empty()) {
        return;
    }
    auto *dst = mutableNodeStorage(blocks, ref) + sizeof(GCNode) + bodyHeaderBytes;
    std::memcpy(dst, bytes.data(), bytes.size());
}

camel::runtime::GCFillKind classifyFillKind(Type *type) {
    if (!type) {
        throwEncodeError("Runtime FILL classification requires a target type.");
    }
    switch (type->code()) {
    case TypeCode::Tuple:
        return camel::runtime::GCFillKind::Tuple;
    case TypeCode::Array:
        return camel::runtime::GCFillKind::Array;
    case TypeCode::Struct:
        return camel::runtime::GCFillKind::Struct;
    case TypeCode::Function:
        return camel::runtime::GCFillKind::FunctionClosure;
    default:
        throwEncodeError(
            std::format("Unsupported runtime FILL target type '{}'.", type->toString()));
    }
}

struct PlannedDraftNode {
    camel::runtime::gc_node_ref_t draftId = camel::runtime::kInvalidNodeRef;
    camel::runtime::gc_node_ref_t ref     = camel::runtime::kInvalidNodeRef;
    camel::runtime::gc_cnt_t blockCount   = 0;
};

struct CompileDraftPayloadPlan {
    std::vector<PlannedDraftNode> nodes;
    std::vector<camel::runtime::gc_node_ref_t> runtimeRefsByDraftId;
    camel::runtime::gc_cnt_t nodeBlockCount = 0;
    camel::runtime::gc_cnt_t edgeCount      = 0;
    camel::runtime::gc_cnt_t branchArmCount = 0;
    camel::runtime::gc_cnt_t normPortCount  = 0;
    camel::runtime::gc_cnt_t withPortCount  = 0;
    camel::runtime::gc_cnt_t closureCount   = 0;
};

std::vector<Node *> buildDraftSourceNodeMap(const graph_ptr_t &graph) {
    std::vector<Node *> sourceNodes;
    if (!graph) {
        return sourceNodes;
    }
    sourceNodes.assign(graph->builder().draft().nodeSlotCount(), nullptr);
    auto bind = [&](Node *node) {
        if (!node || !graph->hasDraftNode(node)) {
            return;
        }
        const auto draftId = graph->draftNodeId(node);
        if (draftId < sourceNodes.size()) {
            sourceNodes[draftId] = node;
        }
    };
    for (Node *node : graph->normPorts())
        bind(node);
    for (Node *node : graph->withPorts())
        bind(node);
    for (Node *node : graph->closure())
        bind(node);
    for (Node *node : graph->nodes())
        bind(node);
    bind(graph->outputNode());
    bind(graph->exitNode());
    return sourceNodes;
}

CompileDraftPayloadPlan planCompileDraftPayload(const graph_ptr_t &graph) {
    CompileDraftPayloadPlan plan;
    if (!graph) {
        return plan;
    }

    const auto &draft               = graph->builder().draft();
    const auto &builder             = graph->builder().draftBuilder();
    const auto sourceNodesByDraftId = buildDraftSourceNodeMap(graph);
    plan.runtimeRefsByDraftId.assign(draft.nodeSlotCount(), camel::runtime::kInvalidNodeRef);
    plan.normPortCount = narrowIntegral<camel::runtime::gc_cnt_t>(
        draft.normPorts().size(),
        "Compile draft norm-port count");
    plan.withPortCount = narrowIntegral<camel::runtime::gc_cnt_t>(
        draft.withPorts().size(),
        "Compile draft with-port count");
    plan.closureCount = narrowIntegral<camel::runtime::gc_cnt_t>(
        draft.closureNodes().size(),
        "Compile draft closure-node count");

    auto requireAlive = [&](camel::runtime::gc_node_ref_t owner,
                            camel::runtime::gc_node_ref_t ref,
                            std::string_view lane) {
        if (ref == camel::runtime::kInvalidNodeRef) {
            return;
        }
        if (!draft.alive(ref)) {
            throw std::runtime_error(
                std::format(
                    "Compile draft payload planning found dead node ref {} in {} list of node {}.",
                    ref,
                    lane,
                    owner));
        }
    };

    camel::runtime::gc_off_t currentNodeOffset = 0;
    size_t edgeCount                           = 0;
    size_t branchArmCount                      = 0;
    for (camel::runtime::gc_node_ref_t draftId = 0; draftId < draft.nodeSlotCount(); ++draftId) {
        if (!draft.alive(draftId)) {
            continue;
        }
        for (auto ref : draft.normInputsOf(draftId))
            requireAlive(draftId, ref, "norm-input");
        for (auto ref : draft.withInputsOf(draftId))
            requireAlive(draftId, ref, "with-input");
        for (auto ref : draft.ctrlInputsOf(draftId))
            requireAlive(draftId, ref, "ctrl-input");
        for (auto ref : draft.normUsersOf(draftId))
            requireAlive(draftId, ref, "norm-user");
        for (auto ref : draft.withUsersOf(draftId))
            requireAlive(draftId, ref, "with-user");
        for (auto ref : draft.ctrlUsersOf(draftId))
            requireAlive(draftId, ref, "ctrl-user");
        if (draft.header(draftId)->kind == GCNodeKind::Brch) {
            for (const GCBranchArm &arm : draft.branchArmsOf(draftId)) {
                requireAlive(draftId, arm.head, "branch-head");
                requireAlive(draftId, arm.tail, "branch-tail");
            }
        }

        const camel::runtime::gc_cnt_t blocks = draftNodeBlockCount(draft, builder, draftId);
        plan.nodes.push_back(
            PlannedDraftNode{.draftId = draftId, .ref = currentNodeOffset, .blockCount = blocks});
        plan.runtimeRefsByDraftId[draftId] = currentNodeOffset;
        currentNodeOffset                  = narrowIntegral<camel::runtime::gc_off_t>(
            static_cast<size_t>(currentNodeOffset) + blocks,
            "Compile draft node blob size");
        edgeCount += draft.normInputsOf(draftId).size() + draft.withInputsOf(draftId).size() +
                     draft.ctrlInputsOf(draftId).size() + draft.normUsersOf(draftId).size() +
                     draft.withUsersOf(draftId).size() + draft.ctrlUsersOf(draftId).size();
        if (draft.header(draftId)->kind == GCNodeKind::Brch) {
            if (draftId < sourceNodesByDraftId.size()) {
                Node *sourceNode = sourceNodesByDraftId[draftId];
                if (auto *brchNode = sourceNode && sourceNode->type() == NodeType::BRCH
                                         ? tt::as_ptr<BrchNode>(sourceNode)
                                         : nullptr) {
                    branchArmCount += brchNode->armCount();
                } else {
                    branchArmCount += draft.branchArmsOf(draftId).size();
                }
            } else {
                branchArmCount += draft.branchArmsOf(draftId).size();
            }
        }
    }

    for (auto ref : draft.normPorts())
        requireAlive(camel::runtime::kInvalidNodeRef, ref, "norm-port");
    for (auto ref : draft.withPorts())
        requireAlive(camel::runtime::kInvalidNodeRef, ref, "with-port");
    for (auto ref : draft.closureNodes())
        requireAlive(camel::runtime::kInvalidNodeRef, ref, "closure");
    requireAlive(camel::runtime::kInvalidNodeRef, draft.entryNode(), "entry");
    requireAlive(camel::runtime::kInvalidNodeRef, draft.exitNode(), "exit");
    requireAlive(camel::runtime::kInvalidNodeRef, draft.outputNode(), "output");
    requireAlive(camel::runtime::kInvalidNodeRef, draft.returnNode(), "return");

    plan.nodeBlockCount = narrowIntegral<camel::runtime::gc_cnt_t>(
        currentNodeOffset,
        "Compile draft node-blob block count");
    plan.edgeCount =
        narrowIntegral<camel::runtime::gc_cnt_t>(edgeCount, "Compile draft edge arena size");
    plan.branchArmCount = narrowIntegral<camel::runtime::gc_cnt_t>(
        branchArmCount,
        "Compile draft branch-arm arena size");
    return plan;
}

GCGraphPayloadShape describeNativePayload(const CompileDraftPayloadPlan &plan) {
    return GCGraphPayloadShape{
        .nodeCount =
            narrowIntegral<camel::runtime::gc_cnt_t>(plan.nodes.size(), "Runtime node count"),
        .nodeBlockCount = plan.nodeBlockCount,
        .edgeCount      = plan.edgeCount,
        .normPortCount  = plan.normPortCount,
        .withPortCount  = plan.withPortCount,
        .closureCount   = plan.closureCount,
        .branchArmCount = plan.branchArmCount,
    };
}

graph_ptr_t
requireSourceGraphHandle(Graph *graph, std::string_view context, GCGraph *owner = nullptr) {
    if (!graph) {
        throwEncodeError("Compile graph encode received a null source graph.");
    }
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

graph_ptr_t resolveGraphHandle(const graph_ptr_t &owner, const Graph *target) {
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
    StaticFunction *funcObj, const graph_ptr_t &ownerSourceGraph,
    const std::function<GCGraph *(const graph_ptr_t &)> &materializeSourceGraph,
    std::string_view context, GCGraph *owner = nullptr) {
    if (!funcObj) {
        throwEncodeError("Compile graph encode received a null static function.");
    }
    Graph *sourceGraph = funcObj->graph();
    if (!sourceGraph) {
        throwEncodeError("Compile-time static function must reference a graph.");
    }
    graph_ptr_t targetGraph = resolveGraphHandle(ownerSourceGraph, sourceGraph);
    if (!targetGraph) {
        targetGraph = requireSourceGraphHandle(sourceGraph, context, owner);
    }
    return materializeSourceGraph(targetGraph);
}

void emitCompileGraphPayload(
    const graph_ptr_t &graph, const CompileDraftPayloadPlan &plan,
    camel::runtime::GCGraphPayloadArena &payload,
    const std::function<GCGraph *(const graph_ptr_t &)> &resolveRuntimeGraph) {
    if (payload.nodeCount !=
        narrowIntegral<camel::runtime::gc_cnt_t>(plan.nodes.size(), "Runtime node count")) {
        throwEncodeError("Compile payload arena shape does not match the planned node count.");
    }

    const auto &draft               = graph->builder().draft();
    const auto &builder             = graph->builder().draftBuilder();
    const auto sourceNodesByDraftId = buildDraftSourceNodeMap(graph);

    camel::runtime::gc_off_t edgeCursor      = 0;
    camel::runtime::gc_off_t branchArmCursor = 0;
    auto appendSlice = [&](std::span<const camel::runtime::gc_node_ref_t> refs) -> Slice {
        Slice slice{.offset = edgeCursor, .count = 0};
        for (camel::runtime::gc_node_ref_t draftRef : refs) {
            if (draftRef >= plan.runtimeRefsByDraftId.size()) {
                throwEncodeError(
                    std::format(
                        "Compile payload emission encountered out-of-range draft ref {}.",
                        draftRef));
            }
            const auto runtimeRef = plan.runtimeRefsByDraftId[draftRef];
            if (runtimeRef == camel::runtime::kInvalidNodeRef) {
                throwEncodeError(
                    std::format(
                        "Compile payload emission encountered dead draft ref {}.",
                        draftRef));
            }
            payload.edges[edgeCursor++] = runtimeRef;
        }
        slice.count = narrowIntegral<camel::runtime::gc_cnt_t>(
            edgeCursor - slice.offset,
            "Runtime edge slice count");
        return slice;
    };
    auto mapRuntimeRef = [&](camel::runtime::gc_node_ref_t draftRef, std::string_view what) {
        if (draftRef >= plan.runtimeRefsByDraftId.size()) {
            throwEncodeError(
                std::format("Compile payload {} draft ref {} is out of range.", what, draftRef));
        }
        const auto runtimeRef = plan.runtimeRefsByDraftId[draftRef];
        if (runtimeRef == camel::runtime::kInvalidNodeRef) {
            throwEncodeError(
                std::format("Compile payload {} draft ref {} is dead.", what, draftRef));
        }
        return runtimeRef;
    };
    auto sourceNodeForDraft = [&](camel::runtime::gc_node_ref_t draftId) -> Node * {
        if (draftId >= sourceNodesByDraftId.size()) {
            return nullptr;
        }
        return sourceNodesByDraftId[draftId];
    };
    auto resolvedDataIndexForDraft = [&](camel::runtime::gc_node_ref_t draftId,
                                         const DraftNodeHeader &draftHeader) {
        if (Node *sourceNode = sourceNodeForDraft(draftId)) {
            return static_cast<camel::runtime::gc_slot_idx_t>(sourceNode->index());
        }
        return draftHeader.dataIndex;
    };
    auto resolvedDataTypeForDraft = [&](camel::runtime::gc_node_ref_t draftId,
                                        const DraftNodeHeader &draftHeader) -> Type * {
        if (Node *sourceNode = sourceNodeForDraft(draftId)) {
            return sourceNode->dataType();
        }
        return effectiveDraftNodeType(draft, draftHeader);
    };

    for (const PlannedDraftNode &planned : plan.nodes) {
        const DraftNodeHeader *draftHeader = draft.header(planned.draftId);
        if (!draftHeader) {
            throwEncodeError("Compile payload emission requires a live draft header.");
        }
        if (draftHeader->kind == GCNodeKind::Gate) {
            Node *sourceNode = planned.draftId < sourceNodesByDraftId.size()
                                   ? sourceNodesByDraftId[planned.draftId]
                                   : nullptr;
            if (sourceNode && sourceNode->type() == NodeType::GATE &&
                sourceNode->ctrlInputs().size() != draft.ctrlInputsOf(planned.draftId).size()) {
                throwEncodeError(
                    std::format(
                        "Compile payload GATE draft/source ctrl-input mismatch at draft id {}: "
                        "source={}, draft={}.",
                        planned.draftId,
                        sourceNode->ctrlInputs().size(),
                        draft.ctrlInputsOf(planned.draftId).size()));
            }
        }
        const camel::runtime::gc_node_ref_t ref = planned.ref;
        auto *header = reinterpret_cast<GCNode *>(mutableNodeStorage(payload.nodeBlocks, ref));
        *header      = GCNode{
            .dataIndex   = resolvedDataIndexForDraft(planned.draftId, *draftHeader),
            .blockCount  = planned.blockCount,
            .normInputs  = appendSlice(draft.normInputsOf(planned.draftId)),
            .withInputs  = appendSlice(draft.withInputsOf(planned.draftId)),
            .ctrlInputs  = appendSlice(draft.ctrlInputsOf(planned.draftId)),
            .normOutputs = appendSlice(draft.normUsersOf(planned.draftId)),
            .withOutputs = appendSlice(draft.withUsersOf(planned.draftId)),
            .ctrlOutputs = appendSlice(draft.ctrlUsersOf(planned.draftId)),
            .dataType    = resolvedDataTypeForDraft(planned.draftId, *draftHeader),
            .kind        = draftHeader->kind,
            .flags       = draftHeader->runtimeFlags,
        };

        const auto payloadBytes = draft.payloadOf(planned.draftId);
        switch (draftHeader->kind) {
        case GCNodeKind::Func: {
            const auto &target = builder.funcTarget(planned.draftId);
            if (!target) {
                throwEncodeError("Compile payload FUNC node requires a callee graph.");
            }
            writeBody(
                payload.nodeBlocks,
                ref,
                GCFuncBody{.calleeGraph = resolveRuntimeGraph(target)});
        } break;
        case GCNodeKind::Call:
            writeBody(
                payload.nodeBlocks,
                ref,
                payloadBytes.empty() ? GCCallBody{}
                                     : *reinterpret_cast<const GCCallBody *>(payloadBytes.data()));
            break;
        case GCNodeKind::Accs:
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
                ref,
                payloadBytes.empty() ? GCFillBody{}
                                     : *reinterpret_cast<const GCFillBody *>(payloadBytes.data()));
            break;
        case GCNodeKind::Brch: {
            auto *brchNode     = tt::as_ptr<BrchNode>(requireDraftSourceNode(
                sourceNodesByDraftId,
                planned.draftId,
                NodeType::BRCH,
                "Compile payload BRCH node"));
            JoinNode *joinNode = brchNode->matchedJoin();
            if (!joinNode) {
                throwEncodeError("Compile payload BRCH node requires a matched JOIN.");
            }
            const camel::runtime::gc_off_t armOffset = branchArmCursor;
            for (size_t i = 0; i < brchNode->armCount(); ++i) {
                Node *head = brchNode->armHead(i);
                Node *tail = joinNode->armTail(i);
                if (!graph->hasDraftNode(head)) {
                    throwEncodeError(
                        std::format(
                            "Compile payload branch arm {} head is not bound to draft.",
                            i));
                }
                if (!graph->hasDraftNode(tail)) {
                    throwEncodeError(
                        std::format(
                            "Compile payload branch arm {} tail is not bound to draft.",
                            i));
                }
                payload.branchArms[branchArmCursor++] = GCBranchArm{
                    .head = mapRuntimeRef(graph->draftNodeId(head), "branch-head"),
                    .tail = mapRuntimeRef(graph->draftNodeId(tail), "branch-tail"),
                };
            }
            if (!graph->hasDraftNode(joinNode)) {
                throwEncodeError("Compile payload matched JOIN is not bound to draft.");
            }
            writeBody(
                payload.nodeBlocks,
                ref,
                GCBrchBody{
                    .join      = mapRuntimeRef(graph->draftNodeId(joinNode), "branch-join"),
                    .armOffset = armOffset,
                    .armCount  = narrowIntegral<camel::runtime::gc_cnt_t>(
                        brchNode->armCount(),
                        "Compile payload branch-arm count"),
                    .defaultArm = camel::runtime::kInvalidNodeRef,
                });
        } break;
        case GCNodeKind::Join: {
            auto *joinNode     = tt::as_ptr<JoinNode>(requireDraftSourceNode(
                sourceNodesByDraftId,
                planned.draftId,
                NodeType::JOIN,
                "Compile payload JOIN node"));
            BrchNode *brchNode = joinNode->matchedBranch();
            if (brchNode && !graph->hasDraftNode(brchNode)) {
                throwEncodeError("Compile payload matched BRCH is not bound to draft.");
            }
            GCJoinBody body{
                .brch     = brchNode ? mapRuntimeRef(graph->draftNodeId(brchNode), "join-branch")
                                     : camel::runtime::kInvalidNodeRef,
                .armCount = narrowIntegral<camel::runtime::gc_cnt_t>(
                    joinNode->armCount(),
                    "Compile payload JOIN arm count"),
                .reserved = 0,
            };
            writeBody(payload.nodeBlocks, ref, body);
        } break;
        case GCNodeKind::Oper: {
            const auto &oper = builder.operTarget(planned.draftId);
            if (!oper) {
                throwEncodeError("Compile payload OPER node requires operator metadata.");
            }
            GCOperBody body{
                .op       = nullptr,
                .uriBytes = narrowIntegral<uint16_t>(oper->uri().size(), "Runtime OPER URI length"),
                .reserved = 0,
            };
            writeBody(payload.nodeBlocks, ref, body);
            writeTrailingBytes(payload.nodeBlocks, ref, sizeof(GCOperBody), oper->uri());
        } break;
        default:
            break;
        }
    }

    for (size_t i = 0; i < graph->normPorts().size(); ++i) {
        payload.normPorts[i] =
            mapRuntimeRef(graph->draftNodeId(graph->normPorts()[i]), "norm-port");
    }
    for (size_t i = 0; i < graph->withPorts().size(); ++i) {
        payload.withPorts[i] =
            mapRuntimeRef(graph->draftNodeId(graph->withPorts()[i]), "with-port");
    }
    for (size_t i = 0; i < graph->closure().size(); ++i) {
        payload.closure[i] = mapRuntimeRef(graph->draftNodeId(graph->closure()[i]), "closure-node");
    }

    payload.entryNode =
        draft.entryNode() == camel::runtime::kInvalidNodeRef
            ? (plan.nodes.empty() ? camel::runtime::kInvalidNodeRef : plan.nodes.front().ref)
            : mapRuntimeRef(draft.entryNode(), "entry");
    payload.exitNode   = draft.exitNode() == camel::runtime::kInvalidNodeRef
                             ? camel::runtime::kInvalidNodeRef
                             : mapRuntimeRef(draft.exitNode(), "exit");
    payload.outputNode = draft.outputNode() == camel::runtime::kInvalidNodeRef
                             ? payload.exitNode
                             : mapRuntimeRef(draft.outputNode(), "output");
    payload.returnNode = payload.exitNode;
    payload.returnKind = GCReturnKind::None;
    if (draft.exitNode() != camel::runtime::kInvalidNodeRef) {
        const auto exitDraftId       = draft.exitNode();
        const DraftNodeHeader *exitH = draft.header(exitDraftId);
        if (!exitH) {
            throwEncodeError("Compile payload exit node must stay alive.");
        }
        if (exitH->dataIndex != 0) {
            payload.returnNode = mapRuntimeRef(exitDraftId, "return-self");
            payload.returnKind = GCReturnKind::Self;
        } else {
            auto dataInputs = draft.normInputsOf(exitDraftId);
            if (!dataInputs.empty()) {
                payload.returnNode = mapRuntimeRef(dataInputs.back(), "return-last-norm-input");
                payload.returnKind = GCReturnKind::LastDataInput;
            } else {
                auto withInputs = draft.withInputsOf(exitDraftId);
                if (!withInputs.empty()) {
                    payload.returnNode = mapRuntimeRef(withInputs.back(), "return-last-with-input");
                    payload.returnKind = GCReturnKind::LastDataInput;
                } else {
                    auto ctrlInputs = draft.ctrlInputsOf(exitDraftId);
                    if (!ctrlInputs.empty()) {
                        payload.returnNode =
                            mapRuntimeRef(ctrlInputs.back(), "return-last-ctrl-input");
                        payload.returnKind = GCReturnKind::LastCtrlInput;
                    }
                }
            }
        }
    }

    if (edgeCursor != payload.edgeCount) {
        throwEncodeError("Compile direct payload emission did not match edge count.");
    }
    if (branchArmCursor != payload.branchArmCount) {
        throwEncodeError("Compile direct payload emission did not match branch-arm count.");
    }
}

class GraphEncodeSession {
  public:
    GCGraph *encode(const graph_ptr_t &sourceGraph) {
        if (!sourceGraph) {
            return nullptr;
        }
        collectClosure(sourceGraph);
        allocateRuntimeGraphs();
        emitRuntimeGraphs();
        return runtimeGraphs_.at(sourceGraph.get());
    }

  private:
    void collectClosure(const graph_ptr_t &sourceGraph) {
        if (!sourceGraph || runtimeGraphs_.contains(sourceGraph.get())) {
            return;
        }
        EXEC_WHEN_DEBUG({
            validate::assertGraphSealingPreconditions(*sourceGraph);
            validate::assertGraphTreeStaticReferences(sourceGraph);
        });
        sourceGraph->refreshDerivedLayout();
        runtimeGraphs_.emplace(sourceGraph.get(), nullptr);
        closure_.push_back(sourceGraph);

        for (const auto &dep : sourceGraph->builder().draftBuilder().dependencyGraphs()) {
            collectClosure(dep);
        }
        const auto &draft   = sourceGraph->builder().draft();
        const auto &builder = sourceGraph->builder().draftBuilder();
        for (camel::runtime::gc_node_ref_t draftId = 0; draftId < draft.nodeSlotCount();
             ++draftId) {
            if (!draft.alive(draftId) || draft.header(draftId)->kind != GCNodeKind::Func) {
                continue;
            }
            const auto &targetHandle = builder.funcTarget(draftId);
            if (!targetHandle) {
                continue;
            }
            collectClosure(targetHandle);
        }
        for (const auto &subGraph : sourceGraph->builder().draftBuilder().subGraphs()) {
            collectClosure(subGraph);
        }
        std::unordered_set<const Object *> visited;
        const TupleType *staticType = sourceGraph->staticDataType();
        if (staticType) {
            for (size_t i = 1; i < sourceGraph->staticDataSize() && i < staticType->size(); ++i) {
                visitStaticFunctionGraphs(
                    sourceGraph->getStaticDataSlot(-static_cast<data_idx_t>(i)),
                    staticType->typeAt(i),
                    sourceGraph,
                    visited);
            }
        }
    }

    void visitStaticFunctionGraphs(
        slot_t slot, Type *type, const graph_ptr_t &ownerSourceGraph,
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
            auto *funcObj = fromSlot<StaticFunction *>(slot);
            if (!funcObj) {
                return;
            }
            auto target = resolveGraphHandle(ownerSourceGraph, funcObj->graph());
            if (!target) {
                target = requireSourceGraphHandle(
                    funcObj->graph(),
                    "Graph::encode(static-slot-function)");
            }
            collectClosure(target);
            if (::Tuple *closure = funcObj->tuple()) {
                TupleType *closureType = const_cast<TupleType *>(funcObj->tupleType());
                for (size_t i = 0; i < closureType->size(); ++i) {
                    if (!camel::core::type::isGCTraced(closureType->codeAt(i))) {
                        continue;
                    }
                    visitStaticFunctionGraphs(
                        closure->get<slot_t>(i),
                        closureType->typeAt(i),
                        ownerSourceGraph,
                        visited);
                }
            }
            break;
        }
        case TypeCode::Tuple: {
            auto *tuple     = fromSlot<::Tuple *>(slot);
            auto *tupleType = static_cast<TupleType *>(type);
            for (size_t i = 0; i < tupleType->size(); ++i) {
                if (camel::core::type::isGCTraced(tupleType->codeAt(i))) {
                    visitStaticFunctionGraphs(
                        tuple->get<slot_t>(i),
                        tupleType->typeAt(i),
                        ownerSourceGraph,
                        visited);
                }
            }
            break;
        }
        case TypeCode::Array: {
            auto *array     = fromSlot<::Array *>(slot);
            auto *arrayType = static_cast<ArrayType *>(type);
            if (!camel::core::type::isGCTraced(arrayType->elemTypeCode())) {
                return;
            }
            for (size_t i = 0; i < array->size(); ++i) {
                visitStaticFunctionGraphs(
                    array->get<slot_t>(i),
                    arrayType->elemType(),
                    ownerSourceGraph,
                    visited);
            }
            break;
        }
        case TypeCode::Struct: {
            auto *st         = fromSlot<::Struct *>(slot);
            auto *structType = static_cast<StructType *>(type);
            for (size_t i = 0; i < structType->size(); ++i) {
                if (camel::core::type::isGCTraced(structType->codeAt(i))) {
                    visitStaticFunctionGraphs(
                        st->get<slot_t>(i),
                        structType->typeAt(i),
                        ownerSourceGraph,
                        visited);
                }
            }
            break;
        }
        default:
            break;
        }
    }

    std::vector<graph_ptr_t> collectDependencyHandles(const graph_ptr_t &sourceGraph) {
        std::vector<graph_ptr_t> dependencies;
        for (const auto &dep : sourceGraph->builder().draftBuilder().dependencyGraphs()) {
            if (dep) {
                dependencies.push_back(dep);
            }
        }
        const auto &draft   = sourceGraph->builder().draft();
        const auto &builder = sourceGraph->builder().draftBuilder();
        for (camel::runtime::gc_node_ref_t draftId = 0; draftId < draft.nodeSlotCount();
             ++draftId) {
            if (!draft.alive(draftId) || draft.header(draftId)->kind != GCNodeKind::Func) {
                continue;
            }
            const auto &targetHandle = builder.funcTarget(draftId);
            if (!targetHandle) {
                continue;
            }
            auto targetGraph = targetHandle;
            if (targetGraph && std::find(dependencies.begin(), dependencies.end(), targetGraph) ==
                                   dependencies.end()) {
                dependencies.push_back(targetGraph);
            }
        }
        return dependencies;
    }

    std::vector<graph_ptr_t> collectSubGraphHandles(const graph_ptr_t &sourceGraph) {
        std::vector<graph_ptr_t> subGraphs;
        for (const auto &subGraph : sourceGraph->builder().draftBuilder().subGraphs()) {
            if (subGraph) {
                subGraphs.push_back(subGraph);
            }
        }
        return subGraphs;
    }

    std::vector<slot_t> buildStaticSlots(const graph_ptr_t &sourceGraph) {
        std::vector<slot_t> runtimeStaticSlots(sourceGraph->staticDataSize(), NullSlot);
        const TupleType *staticType = sourceGraph->staticDataType();
        if (!staticType) {
            return runtimeStaticSlots;
        }

        std::unordered_map<const Object *, Object *> objectCache;
        for (size_t i = 1; i < sourceGraph->staticDataSize() && i < staticType->size(); ++i) {
            runtimeStaticSlots[i] = canonicalizeStaticSlot(
                sourceGraph->getStaticDataSlot(-static_cast<data_idx_t>(i)),
                staticType->typeAt(i),
                sourceGraph,
                objectCache);
        }
        return runtimeStaticSlots;
    }

    slot_t canonicalizeStaticSlot(
        slot_t slot, Type *type, const graph_ptr_t &sourceGraph,
        std::unordered_map<const Object *, Object *> &objectCache) {
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
            auto *funcObj = fromSlot<StaticFunction *>(slot);
            ASSERT(funcObj != nullptr, "Function static slot payload is null.");
            auto target = resolveGraphHandle(sourceGraph, funcObj->graph());
            if (!target) {
                target = requireSourceGraphHandle(
                    funcObj->graph(),
                    "Graph::encode(canonicalize-static-function)");
            }
            auto *runtimeFunc = ::Function::create(
                runtimeGraphs_.at(target.get()),
                funcObj->tupleType(),
                camel::core::mm::autoSpace());
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
                            sourceGraph,
                            objectCache));
                }
            }
            return toSlot<Object *>(runtimeFunc);
        }
        case TypeCode::Tuple: {
            auto *tuple        = fromSlot<::Tuple *>(slot);
            auto *tupleType    = static_cast<TupleType *>(type);
            auto *runtimeTuple = ::Tuple::create(tupleType->size(), camel::core::mm::autoSpace());
            objectCache.emplace(object, runtimeTuple);
            for (size_t i = 0; i < tupleType->size(); ++i) {
                runtimeTuple->set<slot_t>(
                    i,
                    camel::core::type::isGCTraced(tupleType->codeAt(i)) ? canonicalizeStaticSlot(
                                                                              tuple->get<slot_t>(i),
                                                                              tupleType->typeAt(i),
                                                                              sourceGraph,
                                                                              objectCache)
                                                                        : tuple->get<slot_t>(i));
            }
            return toSlot<Object *>(runtimeTuple);
        }
        case TypeCode::Array: {
            auto *array        = fromSlot<::Array *>(slot);
            auto *arrayType    = static_cast<ArrayType *>(type);
            auto *runtimeArray = ::Array::create(camel::core::mm::autoSpace(), array->size());
            objectCache.emplace(object, runtimeArray);
            for (size_t i = 0; i < array->size(); ++i) {
                runtimeArray->set<slot_t>(
                    i,
                    camel::core::type::isGCTraced(arrayType->elemTypeCode())
                        ? canonicalizeStaticSlot(
                              array->get<slot_t>(i),
                              arrayType->elemType(),
                              sourceGraph,
                              objectCache)
                        : array->get<slot_t>(i));
            }
            return toSlot<Object *>(runtimeArray);
        }
        case TypeCode::Struct: {
            auto *st         = fromSlot<::Struct *>(slot);
            auto *structType = static_cast<StructType *>(type);
            auto *runtimeStruct =
                ::Struct::create(structType->size(), camel::core::mm::autoSpace());
            objectCache.emplace(object, runtimeStruct);
            for (size_t i = 0; i < structType->size(); ++i) {
                runtimeStruct->set<slot_t>(
                    i,
                    camel::core::type::isGCTraced(structType->codeAt(i))
                        ? canonicalizeStaticSlot(
                              st->get<slot_t>(i),
                              structType->typeAt(i),
                              sourceGraph,
                              objectCache)
                        : st->get<slot_t>(i));
            }
            return toSlot<Object *>(runtimeStruct);
        }
        default: {
            Object *cloned = object->clone(camel::core::mm::autoSpace(), type, false);
            objectCache.emplace(object, cloned);
            return toSlot<Object *>(cloned);
        }
        }
    }

    std::vector<GCGraph *>
    collectStaticGraphRefs(const graph_ptr_t &sourceGraph, std::span<const slot_t> runtimeSlots) {
        std::vector<GCGraph *> staticGraphRefs;
        const TupleType *staticType = sourceGraph->staticDataType();
        std::unordered_set<const Object *> visited;
        if (staticType) {
            for (size_t i = 1; i < runtimeSlots.size() && i < staticType->size(); ++i) {
                collectStaticGraphRefsRecursive(
                    staticGraphRefs,
                    runtimeSlots[i],
                    staticType->typeAt(i),
                    visited);
            }
        }
        return staticGraphRefs;
    }

    GCGraph *runtimeGraphOf(const graph_ptr_t &graph) const {
        if (!graph) {
            return nullptr;
        }
        auto it = runtimeGraphs_.find(graph.get());
        ASSERT(it != runtimeGraphs_.end(), "Runtime graph closure is missing a planned graph.");
        return it->second;
    }

    GCGraph *runtimeOuterGraphOf(const graph_ptr_t &graph) const {
        if (!graph) {
            return nullptr;
        }
        graph_ptr_t outerHandle = graph->outer();
        if (!outerHandle) {
            return nullptr;
        }
        return runtimeGraphOf(outerHandle);
    }

    std::vector<GCGraph *> mapRuntimeGraphs(const std::vector<graph_ptr_t> &graphs) const {
        std::vector<GCGraph *> result;
        result.reserve(graphs.size());
        for (const auto &graph : graphs) {
            if (graph) {
                result.push_back(runtimeGraphOf(graph));
            }
        }
        return result;
    }

    void allocateRuntimeGraphs() {
        if (allocated_) {
            return;
        }
        for (const auto &sourceGraph : closure_) {
            auto depHandles      = collectDependencyHandles(sourceGraph);
            auto subGraphHandles = collectSubGraphHandles(sourceGraph);
            std::vector<GCGraph *> depPlaceholders(depHandles.size(), nullptr);
            std::vector<GCGraph *> subPlaceholders(subGraphHandles.size(), nullptr);
            std::vector<GCGraph *> staticRefPlaceholders(
                countStaticGraphHandleRefs(sourceGraph),
                nullptr);
            std::vector<slot_t> staticSlots(sourceGraph->staticDataSize(), NullSlot);
            auto [planIt, inserted] =
                payloadPlans_.try_emplace(sourceGraph.get(), planCompileDraftPayload(sourceGraph));
            ASSERT(inserted, "Compile payload plan should be recorded exactly once per graph.");
            const auto payloadShape = describeNativePayload(planIt->second);
            size_t bytes            = camel::runtime::GCGraphBuildAccess::requiredBytes(
                depPlaceholders,
                subPlaceholders,
                staticRefPlaceholders,
                payloadShape,
                staticSlots);
            void *mem = camel::core::mm::graphSpace().alloc(bytes, alignof(GCGraph));
            if (!mem) {
                throw std::bad_alloc();
            }
            runtimeGraphs_[sourceGraph.get()]  = reinterpret_cast<GCGraph *>(mem);
            allocatedBytes_[sourceGraph.get()] = bytes;
        }
        allocated_ = true;
    }

    size_t countStaticGraphHandleRefs(const graph_ptr_t &sourceGraph) {
        std::vector<graph_ptr_t> refs;
        std::unordered_set<const Object *> visited;
        const TupleType *staticType = sourceGraph->staticDataType();
        if (staticType) {
            for (size_t i = 1; i < sourceGraph->staticDataSize() && i < staticType->size(); ++i) {
                collectStaticFunctionRefHandles(
                    refs,
                    sourceGraph->getStaticDataSlot(-static_cast<data_idx_t>(i)),
                    staticType->typeAt(i),
                    sourceGraph,
                    visited);
            }
        }
        return refs.size();
    }

    void collectStaticFunctionRefHandles(
        std::vector<graph_ptr_t> &refs, slot_t slot, Type *type, const graph_ptr_t &sourceGraph,
        std::unordered_set<const Object *> &visited) {
        if (!type || !type->isGCTraced() || slot == NullSlot) {
            return;
        }
        Object *object = fromSlot<Object *>(slot);
        if (!object || !visited.insert(object).second) {
            return;
        }
        auto pushUniqueHandle = [&](const graph_ptr_t &graph) {
            if (graph && std::find(refs.begin(), refs.end(), graph) == refs.end()) {
                refs.push_back(graph);
            }
        };
        switch (type->code()) {
        case TypeCode::Function: {
            auto *funcObj = fromSlot<StaticFunction *>(slot);
            if (!funcObj) {
                return;
            }
            auto target = resolveGraphHandle(sourceGraph, funcObj->graph());
            if (!target) {
                target = requireSourceGraphHandle(
                    funcObj->graph(),
                    "Graph::encode(static-slot-ref-count)");
            }
            pushUniqueHandle(target);
            if (::Tuple *closure = funcObj->tuple()) {
                TupleType *closureType = const_cast<TupleType *>(funcObj->tupleType());
                for (size_t i = 0; i < closureType->size(); ++i) {
                    if (camel::core::type::isGCTraced(closureType->codeAt(i))) {
                        collectStaticFunctionRefHandles(
                            refs,
                            closure->get<slot_t>(i),
                            closureType->typeAt(i),
                            sourceGraph,
                            visited);
                    }
                }
            }
            break;
        }
        case TypeCode::Tuple: {
            auto *tuple     = fromSlot<::Tuple *>(slot);
            auto *tupleType = static_cast<TupleType *>(type);
            for (size_t i = 0; i < tupleType->size(); ++i) {
                if (camel::core::type::isGCTraced(tupleType->codeAt(i))) {
                    collectStaticFunctionRefHandles(
                        refs,
                        tuple->get<slot_t>(i),
                        tupleType->typeAt(i),
                        sourceGraph,
                        visited);
                }
            }
            break;
        }
        case TypeCode::Array: {
            auto *array     = fromSlot<::Array *>(slot);
            auto *arrayType = static_cast<ArrayType *>(type);
            if (!camel::core::type::isGCTraced(arrayType->elemTypeCode())) {
                return;
            }
            for (size_t i = 0; i < array->size(); ++i) {
                collectStaticFunctionRefHandles(
                    refs,
                    array->get<slot_t>(i),
                    arrayType->elemType(),
                    sourceGraph,
                    visited);
            }
            break;
        }
        case TypeCode::Struct: {
            auto *st         = fromSlot<::Struct *>(slot);
            auto *structType = static_cast<StructType *>(type);
            for (size_t i = 0; i < structType->size(); ++i) {
                if (camel::core::type::isGCTraced(structType->codeAt(i))) {
                    collectStaticFunctionRefHandles(
                        refs,
                        st->get<slot_t>(i),
                        structType->typeAt(i),
                        sourceGraph,
                        visited);
                }
            }
            break;
        }
        default:
            break;
        }
    }

    void emitRuntimeGraphs() {
        if (emitted_) {
            return;
        }
        for (auto it = closure_.rbegin(); it != closure_.rend(); ++it) {
            const auto &sourceGraph = *it;
            auto depHandles         = collectDependencyHandles(sourceGraph);
            auto subGraphHandles    = collectSubGraphHandles(sourceGraph);
            auto dependencies       = mapRuntimeGraphs(depHandles);
            auto subGraphs          = mapRuntimeGraphs(subGraphHandles);
            auto runtimeSlots       = buildStaticSlots(sourceGraph);
            auto staticGraphRefs    = collectStaticGraphRefs(sourceGraph, runtimeSlots);
            const auto planIt       = payloadPlans_.find(sourceGraph.get());
            ASSERT(planIt != payloadPlans_.end(), "Compile payload plan must exist before emit.");
            const auto &payloadPlan = planIt->second;
            const auto payloadShape = describeNativePayload(payloadPlan);
            auto *graph             = camel::runtime::GCGraphBuildAccess::constructInPlace(
                runtimeGraphs_.at(sourceGraph.get()),
                allocatedBytes_.at(sourceGraph.get()),
                createGraphDebugRecord(
                    sourceGraph->stableId(),
                    sourceGraph->mangledName(),
                    sourceGraph->name()),
                sourceGraph->funcType(),
                const_cast<TupleType *>(sourceGraph->runtimeDataType()),
                const_cast<TupleType *>(sourceGraph->staticDataType()),
                const_cast<TupleType *>(sourceGraph->closureType()),
                runtimeOuterGraphOf(sourceGraph),
                dependencies,
                subGraphs,
                staticGraphRefs,
                payloadShape,
                [&](camel::runtime::GCGraphPayloadArena &payload) {
                    emitCompileGraphPayload(
                        sourceGraph,
                        payloadPlan,
                        payload,
                        [&](const graph_ptr_t &target) -> GCGraph             *{
                            if (!target) {
                                return nullptr;
                            }
                            return runtimeGraphs_.at(target.get());
                        });
                },
                runtimeSlots);
            runtimeGraphs_[sourceGraph.get()] = graph;
            camel::runtime::validateRuntimeGraphPayload(graph);
        }
        emitted_ = true;
    }

    std::vector<graph_ptr_t> closure_;
    std::unordered_map<const Graph *, GCGraph *> runtimeGraphs_;
    std::unordered_map<const Graph *, size_t> allocatedBytes_;
    std::unordered_map<const Graph *, CompileDraftPayloadPlan> payloadPlans_;
    bool allocated_ = false;
    bool emitted_   = false;
};

} // namespace

camel::runtime::GCGraph *encodeToRuntimeGraph(const graph_ptr_t &rootGraph) {
    GraphEncodeSession session;
    return rootGraph ? session.encode(rootGraph) : nullptr;
}

} // namespace camel::compile::gir
