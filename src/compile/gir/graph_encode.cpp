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
 * Created: Apr. 12, 2026
 * Updated: May. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * DraftGraphBuilder -> GCGraph encoding.
 *
 * Compile-time GIR now lives entirely on DraftGraphBuilder and GraphDraft.
 * Runtime materialization therefore becomes a direct draft-to-GCGraph export:
 * - collect the reachable compile graph tree
 * - clone every compile draft into a runtime draft view
 * - preallocate all GCGraph memory so self/cyclic references have stable addresses
 * - canonicalize static slots from compile StaticFunction objects to runtime Function objects
 * - emit final immutable GCGraph payloads in one pass
 */

#include "camel/compile/gir/encode.h"

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/reachable.h"
#include "camel/compile/gir/static_function.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/composite.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/log.h"
#include "runtime/graph_build.h"

#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::compile::gir {

const void *compileGraphOpaque(const graph_ptr_t &graph) { return graph ? graph.get() : nullptr; }

namespace {

using camel::core::rtdata::fromSlot;
using camel::core::rtdata::Object;
using camel::core::rtdata::toSlot;
using camel::core::type::ArrayType;
using camel::core::type::StructType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::TypeCode;
using camel::runtime::createGraphDebugRecord;
using camel::runtime::describeDraftNativePayload;
using camel::runtime::DraftBrchPayload;
using camel::runtime::DraftNodeInit;
using camel::runtime::emitDraftNativePayload;
using camel::runtime::GCAccsBody;
using camel::runtime::GCBranchArm;
using camel::runtime::GCBrchBody;
using camel::runtime::GCCallBody;
using camel::runtime::GCFillBody;
using camel::runtime::GCFuncBody;
using camel::runtime::GCGraph;
using camel::runtime::GCGraphBuildAccess;
using camel::runtime::GCGraphPayloadShape;
using camel::runtime::GCJoinBody;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::GraphDraft;
using camel::runtime::validateRuntimeGraphPayload;

TupleType *ensureRuntimeTupleType(TupleType *type) { return type ? type : TupleType::create(); }

bool canEncodeRuntimeStaticSlotType(Type *type) {
    if (type == nullptr || type->code() == TypeCode::Ref) {
        return false;
    }
    if (type->isComposite() && !static_cast<camel::core::type::CompositeType *>(type)->resolved()) {
        return false;
    }
    return true;
}

bool shouldTraverseStaticSlotType(Type *type) {
    if (type == nullptr || !type->isGCTraced() || type->code() == TypeCode::Ref) {
        return false;
    }
    if (type->isComposite() && !static_cast<camel::core::type::CompositeType *>(type)->resolved()) {
        return false;
    }
    return true;
}

camel::runtime::GCFillKind classifyFillKind(Type *type) {
    ASSERT(type != nullptr, "FILL node requires a target type.");
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
        ASSERT(false, std::format("Unsupported FILL type '{}'.", type->toString()));
        return camel::runtime::GCFillKind::Tuple;
    }
}

template <typename Body> std::vector<std::byte> toPayloadBytes(const Body &body) {
    static_assert(std::is_trivially_copyable_v<Body>);
    std::vector<std::byte> bytes(sizeof(Body));
    std::memcpy(bytes.data(), &body, sizeof(Body));
    return bytes;
}

std::vector<std::byte> toOperPayloadBytes(const oper_idx_ptr_t &oper) {
    GCOperBody body{
        .op       = nullptr,
        .uriBytes = static_cast<uint16_t>(oper->uri().size()),
        .reserved = 0,
    };
    std::vector<std::byte> bytes(sizeof(GCOperBody) + oper->uri().size());
    std::memcpy(bytes.data(), &body, sizeof(body));
    if (!oper->uri().empty()) {
        std::memcpy(bytes.data() + sizeof(GCOperBody), oper->uri().data(), oper->uri().size());
    }
    return bytes;
}

void collectCompileStaticGraphRefsRecursive(
    std::vector<graph_ptr_t> &refs, slot_t slot, Type *type,
    std::unordered_set<const Object *> &visited) {
    if (!shouldTraverseStaticSlotType(type) || slot == NullSlot) {
        return;
    }

    Object *object = fromSlot<Object *>(slot);
    if (!object || !visited.insert(object).second) {
        return;
    }

    switch (type->code()) {
    case TypeCode::Function: {
        auto *func = fromSlot<StaticFunction *>(slot);
        if (func && func->graph() && std::ranges::find(refs, func->graph()) == refs.end()) {
            refs.push_back(func->graph());
        }
        if (func && func->tuple()) {
            auto *tupleType = const_cast<TupleType *>(func->tupleType());
            for (size_t i = 0; i < tupleType->size(); ++i) {
                collectCompileStaticGraphRefsRecursive(
                    refs,
                    func->tuple()->get<slot_t>(i),
                    tupleType->typeAt(i),
                    visited);
            }
        }
    } break;
    case TypeCode::Tuple: {
        auto *tuple     = fromSlot<::Tuple *>(slot);
        auto *tupleType = static_cast<TupleType *>(type);
        for (size_t i = 0; i < tupleType->size(); ++i) {
            collectCompileStaticGraphRefsRecursive(
                refs,
                tuple->get<slot_t>(i),
                tupleType->typeAt(i),
                visited);
        }
    } break;
    case TypeCode::Array: {
        auto *array     = fromSlot<::Array *>(slot);
        auto *arrayType = static_cast<ArrayType *>(type);
        for (size_t i = 0; i < array->size(); ++i) {
            collectCompileStaticGraphRefsRecursive(
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
            collectCompileStaticGraphRefsRecursive(
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

struct PreparedGraph {
    graph_ptr_t source;
    std::unique_ptr<GraphDraft> draft;
    std::vector<slot_t> staticSlots;
    GCGraphPayloadShape payloadShape;
    void *memory     = nullptr;
    size_t bytes     = 0;
    GCGraph *runtime = nullptr;
};

std::vector<draft_node_ref_t> collectLiveDraftNodes(const GraphDraft &draft) {
    std::unordered_set<draft_node_ref_t> visited;
    std::vector<draft_node_ref_t> worklist;
    auto pushRoot = [&](draft_node_ref_t ref) {
        if (ref != runtime::kInvalidNodeRef && visited.insert(ref).second) {
            worklist.push_back(ref);
        }
    };

    pushRoot(draft.entryNode());
    pushRoot(draft.exitNode());
    pushRoot(draft.outputNode());
    pushRoot(draft.returnNode());
    for (draft_node_ref_t ref : draft.normPorts()) {
        pushRoot(ref);
    }
    for (draft_node_ref_t ref : draft.withPorts()) {
        pushRoot(ref);
    }
    for (draft_node_ref_t ref : draft.closureNodes()) {
        pushRoot(ref);
    }

    while (!worklist.empty()) {
        const draft_node_ref_t current = worklist.back();
        worklist.pop_back();
        for (draft_node_ref_t input : draft.normInputsOf(current)) {
            pushRoot(input);
        }
        for (draft_node_ref_t input : draft.withInputsOf(current)) {
            pushRoot(input);
        }
        for (draft_node_ref_t input : draft.ctrlInputsOf(current)) {
            pushRoot(input);
        }
        const runtime::DraftNodeHeader *header = draft.header(current);
        if (header == nullptr) {
            continue;
        }
        if (header->kind == GCNodeKind::Brch) {
            const auto *payload =
                reinterpret_cast<const DraftBrchPayload *>(draft.payloadOf(current).data());
            if (payload != nullptr) {
                pushRoot(payload->join);
                pushRoot(payload->defaultArm);
            }
            for (const GCBranchArm &arm : draft.branchArmsOf(current)) {
                pushRoot(arm.head);
                pushRoot(arm.tail);
            }
        } else if (header->kind == GCNodeKind::Join) {
            const auto *payload =
                reinterpret_cast<const GCJoinBody *>(draft.payloadOf(current).data());
            if (payload != nullptr) {
                pushRoot(payload->brch);
            }
        }
    }

    std::vector<draft_node_ref_t> live(visited.begin(), visited.end());
    std::sort(live.begin(), live.end());
    return live;
}

draft_node_ref_t
remapNodeRef(draft_node_ref_t sourceRef, const std::vector<draft_node_ref_t> &mapping) {
    if (sourceRef == runtime::kInvalidNodeRef) {
        return runtime::kInvalidNodeRef;
    }
    ASSERT(sourceRef < mapping.size(), "Compile encode encountered an out-of-range node ref.");
    const draft_node_ref_t mapped = mapping[sourceRef];
    if (mapped == runtime::kInvalidNodeRef) {
        throw std::runtime_error(
            std::format("Compile encode encountered an unmapped live node ref {}.", sourceRef));
    }
    return mapped;
}

class EncodeSession {
  public:
    explicit EncodeSession(graph_ptr_t rootGraph) : rootGraph_(std::move(rootGraph)) {}

    GCGraph *run() {
        ASSERT(rootGraph_ != nullptr, "Cannot encode a null compile graph.");
        const auto reachable = collectReachableGraphs(rootGraph_);
        CAMEL_LOG_INFO_S("GIREncode", "Encoding {} reachable compile graphs.", reachable.size());
        prepared_.reserve(reachable.size());
        for (const auto &graph : reachable) {
            prepared_.push_back(prepareGraph(graph));
        }
        for (auto &prepared : prepared_) {
            preparedBySource_.emplace(prepared.source.get(), &prepared);
            if (!prepared.source->stableId().empty()) {
                preparedByStableId_.emplace(prepared.source->stableId(), &prepared);
            }
        }
        preallocateRuntimeGraphs();
        for (auto &prepared : prepared_) {
            patchPreparedFuncPayloads(prepared);
        }
        for (auto &prepared : prepared_) {
            CAMEL_LOG_INFO_S("GIREncode", "Finalize runtime graph '{}'.", prepared.source->name());
            finalizeRuntimeGraph(prepared);
        }

        return resolvePreparedGraph(rootGraph_)->runtime;
    }

  private:
    PreparedGraph *resolvePreparedGraph(const graph_ptr_t &graph) {
        ASSERT(graph != nullptr, "Cannot resolve a null compile graph.");
        if (auto it = preparedBySource_.find(graph.get()); it != preparedBySource_.end()) {
            return it->second;
        }
        if (!graph->stableId().empty()) {
            if (auto it = preparedByStableId_.find(graph->stableId());
                it != preparedByStableId_.end()) {
                return it->second;
            }
        }
        for (auto &prepared : prepared_) {
            if (prepared.source && prepared.source->name() == graph->name()) {
                CAMEL_LOG_WARN_S(
                    "GIREncode",
                    "Compile graph identity fallback matched by name only: requested='{}' "
                    "stableId='{}'.",
                    graph->name(),
                    graph->stableId());
                return &prepared;
            }
        }
        ASSERT(
            false,
            std::format(
                "Compile graph '{}' (stableId='{}') is not part of the prepared encode set.",
                graph->name(),
                graph->stableId()));
        return nullptr;
    }

    PreparedGraph prepareGraph(const graph_ptr_t &graph) {
        PreparedGraph prepared;
        prepared.source = graph;
        prepared.draft  = std::make_unique<GraphDraft>();
        CAMEL_LOG_INFO_S("GIREncode", "Prepare compile graph '{}'.", graph->name());
        prepared.draft->setFuncType(graph->funcType());
        prepared.draft->setRuntimeDataType(
            ensureRuntimeTupleType(graph->draft().runtimeDataType()));
        prepared.draft->setClosureType(graph->closureType());

        const auto sourceStaticSlots = graph->draft().staticSlots();
        const auto sourceStaticTypes = graph->draft().staticSlotTypes();
        prepared.staticSlots.assign(sourceStaticSlots.begin(), sourceStaticSlots.end());
        for (size_t i = 1; i < sourceStaticSlots.size() && i < sourceStaticTypes.size(); ++i) {
            prepared.draft->appendStaticSlot(sourceStaticSlots[i], sourceStaticTypes[i]);
        }

        const std::vector<draft_node_ref_t> liveNodes = collectLiveDraftNodes(graph->draft());
        CAMEL_LOG_INFO_S(
            "GIREncode",
            "Prepare compile graph '{}' reachable nodes={}.",
            graph->name(),
            liveNodes.size());
        std::unordered_set<draft_node_ref_t> liveSet(liveNodes.begin(), liveNodes.end());
        std::vector<draft_node_ref_t> sourceToPrepared(
            graph->draft().nodeSlotCount(),
            runtime::kInvalidNodeRef);
        std::vector<std::vector<draft_node_ref_t>> pendingNormInputs;
        std::vector<std::vector<draft_node_ref_t>> pendingWithInputs;
        std::vector<std::vector<draft_node_ref_t>> pendingCtrlInputs;
        draft_node_ref_t nextPreparedId = 0;
        for (draft_node_ref_t sourceId = 0; sourceId < graph->draft().nodeSlotCount(); ++sourceId) {
            if (graph->draft().alive(sourceId) && liveSet.contains(sourceId)) {
                sourceToPrepared[sourceId] = nextPreparedId++;
            }
        }
        pendingNormInputs.reserve(nextPreparedId);
        pendingWithInputs.reserve(nextPreparedId);
        pendingCtrlInputs.reserve(nextPreparedId);
        for (draft_node_ref_t sourceId = 0; sourceId < graph->draft().nodeSlotCount(); ++sourceId) {
            if (!graph->draft().alive(sourceId) || !liveSet.contains(sourceId)) {
                continue;
            }
            const auto *header = graph->draft().header(sourceId);
            ASSERT(header != nullptr, "Live compile draft node cannot have a null header.");
            if (header->kind == GCNodeKind::Dref) {
                throw std::runtime_error(
                    std::format(
                        "Compile graph '{}' still contains a live DREF node at draft ref {}.",
                        graph->name(),
                        sourceId));
            }
            DraftNodeInit init{
                .dataIndex    = header->dataIndex,
                .dataType     = header->dataType,
                .kind         = header->kind,
                .runtimeFlags = header->runtimeFlags,
            };
            std::vector<std::byte> payloadStorage;

            switch (header->kind) {
            case GCNodeKind::Func: {
                GCFuncBody body{
                    .calleeGraph = nullptr,
                };
                payloadStorage = toPayloadBytes(body);
            } break;
            case GCNodeKind::Oper:
                payloadStorage = toOperPayloadBytes(graph->operTarget(sourceId));
                break;
            case GCNodeKind::Call:
                payloadStorage.assign(
                    graph->draft().payloadOf(sourceId).begin(),
                    graph->draft().payloadOf(sourceId).end());
                if (payloadStorage.empty()) {
                    payloadStorage = toPayloadBytes(GCCallBody{});
                }
                break;
            case GCNodeKind::Fill: {
                if (!graph->draft().payloadOf(sourceId).empty()) {
                    payloadStorage.assign(
                        graph->draft().payloadOf(sourceId).begin(),
                        graph->draft().payloadOf(sourceId).end());
                } else {
                    std::vector<size_t> slots;
                    slots.reserve(graph->draft().withInputsOf(sourceId).size());
                    for (size_t i = 0; i < graph->draft().withInputsOf(sourceId).size(); ++i) {
                        slots.push_back(i);
                    }
                    payloadStorage =
                        camel::runtime::makeFillPayload(classifyFillKind(header->dataType), slots);
                }
            } break;
            case GCNodeKind::Brch: {
                const auto *sourceBody = reinterpret_cast<const DraftBrchPayload *>(
                    graph->draft().payloadOf(sourceId).data());
                ASSERT(sourceBody != nullptr, "Compile BRCH node is missing payload.");
                std::vector<std::byte> bytes(
                    sizeof(DraftBrchPayload) + graph->draft().branchArmsOf(sourceId).size_bytes());
                DraftBrchPayload body{
                    .join       = remapNodeRef(sourceBody->join, sourceToPrepared),
                    .armCount   = sourceBody->armCount,
                    .defaultArm = remapNodeRef(sourceBody->defaultArm, sourceToPrepared),
                    .reserved   = 0,
                };
                std::memcpy(bytes.data(), &body, sizeof(body));
                auto *arms = reinterpret_cast<GCBranchArm *>(bytes.data() + sizeof(body));
                const auto sourceArms = graph->draft().branchArmsOf(sourceId);
                for (size_t i = 0; i < sourceArms.size(); ++i) {
                    arms[i].head = remapNodeRef(sourceArms[i].head, sourceToPrepared);
                    arms[i].tail = remapNodeRef(sourceArms[i].tail, sourceToPrepared);
                }
                payloadStorage = std::move(bytes);
            } break;
            case GCNodeKind::Join: {
                const auto sourcePayload = graph->draft().payloadOf(sourceId);
                if (sourcePayload.size_bytes() < sizeof(GCJoinBody)) {
                    throw std::runtime_error(
                        std::format(
                            "Source JOIN node {} in graph '{}' has truncated payload ({} bytes).",
                            sourceId,
                            graph->name(),
                            sourcePayload.size_bytes()));
                }
                const auto *sourceBody = reinterpret_cast<const GCJoinBody *>(sourcePayload.data());
                if (sourceBody->armCount != graph->draft().withInputsOf(sourceId).size()) {
                    throw std::runtime_error(
                        std::format(
                            "Source JOIN node {} in graph '{}' has mismatched arm count: body={}, "
                            "inputs={}.",
                            sourceId,
                            graph->name(),
                            sourceBody->armCount,
                            graph->draft().withInputsOf(sourceId).size()));
                }
                auto bytes = std::vector<std::byte>(
                    graph->draft().payloadOf(sourceId).begin(),
                    graph->draft().payloadOf(sourceId).end());
                if (bytes.size() >= sizeof(GCJoinBody)) {
                    auto *body = reinterpret_cast<GCJoinBody *>(bytes.data());
                    body->brch = remapNodeRef(body->brch, sourceToPrepared);
                }
                payloadStorage = std::move(bytes);
            } break;
            default:
                payloadStorage.assign(
                    graph->draft().payloadOf(sourceId).begin(),
                    graph->draft().payloadOf(sourceId).end());
                break;
            }

            init.payload = std::span<const std::byte>(payloadStorage.data(), payloadStorage.size());
            pendingNormInputs.emplace_back();
            pendingWithInputs.emplace_back();
            pendingCtrlInputs.emplace_back();
            auto remapInputs = [&](std::span<const draft_node_ref_t> sourceInputs,
                                   std::vector<draft_node_ref_t> &target) {
                target.reserve(sourceInputs.size());
                for (draft_node_ref_t input : sourceInputs) {
                    target.push_back(remapNodeRef(input, sourceToPrepared));
                }
            };
            remapInputs(graph->draft().normInputsOf(sourceId), pendingNormInputs.back());
            remapInputs(graph->draft().withInputsOf(sourceId), pendingWithInputs.back());
            remapInputs(graph->draft().ctrlInputsOf(sourceId), pendingCtrlInputs.back());

            sourceToPrepared[sourceId] = prepared.draft->addNode(init);
        }

        size_t preparedOrdinal = 0;
        for (draft_node_ref_t sourceId = 0; sourceId < graph->draft().nodeSlotCount(); ++sourceId) {
            if (!graph->draft().alive(sourceId) || !liveSet.contains(sourceId)) {
                continue;
            }
            const auto preparedId = sourceToPrepared[sourceId];
            prepared.draft->setNormInputs(preparedId, pendingNormInputs[preparedOrdinal]);
            prepared.draft->setWithInputs(preparedId, pendingWithInputs[preparedOrdinal]);
            prepared.draft->setCtrlInputs(preparedId, pendingCtrlInputs[preparedOrdinal]);
            ++preparedOrdinal;
        }
        for (draft_node_ref_t nodeId : graph->draft().normPorts()) {
            prepared.draft->appendNormPort(sourceToPrepared[nodeId]);
        }
        for (draft_node_ref_t nodeId : graph->draft().withPorts()) {
            prepared.draft->appendWithPort(sourceToPrepared[nodeId]);
        }
        for (draft_node_ref_t nodeId : graph->draft().closureNodes()) {
            prepared.draft->appendClosureNode(sourceToPrepared[nodeId]);
        }

        if (graph->draft().entryNode() != runtime::kInvalidNodeRef) {
            prepared.draft->setEntryNode(sourceToPrepared[graph->draft().entryNode()]);
        }
        if (graph->draft().exitNode() != runtime::kInvalidNodeRef) {
            prepared.draft->setExitNode(sourceToPrepared[graph->draft().exitNode()]);
        }
        if (graph->draft().outputNode() != runtime::kInvalidNodeRef) {
            prepared.draft->setOutputNode(sourceToPrepared[graph->draft().outputNode()]);
        }
        if (graph->draft().returnNode() != runtime::kInvalidNodeRef) {
            prepared.draft->setReturnNode(
                sourceToPrepared[graph->draft().returnNode()],
                graph->draft().returnKind());
        }

        prepared.payloadShape = describeDraftNativePayload(*prepared.draft);
        CAMEL_LOG_INFO_S(
            "GIREncode",
            "Prepared compile graph '{}' with {} draft nodes.",
            graph->name(),
            prepared.payloadShape.nodeCount);
        return prepared;
    }

    void preallocateRuntimeGraphs() {
        for (auto &prepared : prepared_) {
            const auto &deps                    = prepared.source->dependencyGraphs();
            const auto &subs                    = prepared.source->subGraphs();
            std::vector<graph_ptr_t> staticRefs = prepared.source->staticGraphRefs();
            std::unordered_set<const Object *> visited;
            const auto staticTypes = prepared.source->draft().staticSlotTypes();
            const auto staticSlots = prepared.source->draft().staticSlots();
            for (size_t i = 1; i < staticSlots.size() && i < staticTypes.size(); ++i) {
                collectCompileStaticGraphRefsRecursive(
                    staticRefs,
                    staticSlots[i],
                    staticTypes[i],
                    visited);
            }
            std::vector<GCGraph *> depPtrs(deps.size(), nullptr);
            std::vector<GCGraph *> subPtrs(subs.size(), nullptr);
            std::vector<GCGraph *> staticGraphPtrs(staticRefs.size(), nullptr);
            prepared.bytes = GCGraphBuildAccess::requiredBytes(
                depPtrs,
                subPtrs,
                staticGraphPtrs,
                prepared.payloadShape,
                prepared.staticSlots);
            prepared.memory = camel::core::mm::graphSpace().alloc(prepared.bytes, alignof(GCGraph));
            prepared.runtime = reinterpret_cast<GCGraph *>(prepared.memory);
            CAMEL_LOG_INFO_S(
                "GIREncode",
                "Preallocated runtime graph '{}' bytes={}.",
                prepared.source->name(),
                prepared.bytes);
        }
    }

    void patchPreparedFuncPayloads(PreparedGraph &prepared) {
        const std::vector<draft_node_ref_t> liveNodes =
            collectLiveDraftNodes(prepared.source->draft());
        std::unordered_set<draft_node_ref_t> liveSet(liveNodes.begin(), liveNodes.end());
        draft_node_ref_t preparedId = 0;
        for (draft_node_ref_t sourceId = 0; sourceId < prepared.source->draft().nodeSlotCount();
             ++sourceId) {
            if (!prepared.source->draft().alive(sourceId) || !liveSet.contains(sourceId)) {
                continue;
            }
            const auto *header = prepared.source->draft().header(sourceId);
            ASSERT(header != nullptr, "Live source draft node cannot have a null header.");
            if (header->kind == GCNodeKind::Func) {
                GCFuncBody *body = reinterpret_cast<GCFuncBody *>(
                    prepared.draft->mutablePayloadOf(preparedId).data());
                ASSERT(body != nullptr, "Prepared FUNC payload cannot be null.");
                body->calleeGraph =
                    resolvePreparedGraph(prepared.source->funcTarget(sourceId))->runtime;
            }
            ++preparedId;
        }
    }

    slot_t canonicalizeStaticSlot(
        slot_t slot, Type *type, const graph_ptr_t &sourceGraph,
        std::unordered_map<const Object *, Object *> &objectCache) {
        if (!shouldTraverseStaticSlotType(type) || slot == NullSlot) {
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
            ASSERT(funcObj != nullptr, "Compile static function slot payload is null.");
            auto *runtimeFunc = ::Function::create(
                resolvePreparedGraph(funcObj->graph())->runtime,
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
                            funcObj->graph(),
                            objectCache));
                }
            }
            return toSlot<Object *>(runtimeFunc);
        }
        case TypeCode::Tuple: {
            auto *tuple     = fromSlot<::Tuple *>(slot);
            auto *tupleType = static_cast<TupleType *>(type);
            auto *cloned    = ::Tuple::create(tupleType->size(), camel::core::mm::autoSpace());
            objectCache.emplace(object, cloned);
            for (size_t i = 0; i < tupleType->size(); ++i) {
                cloned->set<slot_t>(
                    i,
                    canonicalizeStaticSlot(
                        tuple->get<slot_t>(i),
                        tupleType->typeAt(i),
                        sourceGraph,
                        objectCache));
            }
            return toSlot<Object *>(cloned);
        }
        case TypeCode::Array: {
            auto *array     = fromSlot<::Array *>(slot);
            auto *arrayType = static_cast<ArrayType *>(type);
            auto *cloned    = ::Array::create(camel::core::mm::autoSpace(), array->size());
            objectCache.emplace(object, cloned);
            for (size_t i = 0; i < array->size(); ++i) {
                cloned->set<slot_t>(
                    i,
                    canonicalizeStaticSlot(
                        array->get<slot_t>(i),
                        arrayType->elemType(),
                        sourceGraph,
                        objectCache));
            }
            return toSlot<Object *>(cloned);
        }
        case TypeCode::Struct: {
            auto *st         = fromSlot<::Struct *>(slot);
            auto *structType = static_cast<StructType *>(type);
            auto *cloned     = ::Struct::create(structType->size(), camel::core::mm::autoSpace());
            objectCache.emplace(object, cloned);
            for (size_t i = 0; i < structType->size(); ++i) {
                cloned->set<slot_t>(
                    i,
                    canonicalizeStaticSlot(
                        st->get<slot_t>(i),
                        structType->typeAt(i),
                        sourceGraph,
                        objectCache));
            }
            return toSlot<Object *>(cloned);
        }
        default: {
            Object *cloned = object->clone(camel::core::mm::autoSpace(), type, false);
            objectCache.emplace(object, cloned);
            return toSlot<Object *>(cloned);
        }
        }
    }

    void finalizeRuntimeGraph(PreparedGraph &prepared) {
        auto *source = prepared.source.get();
        ASSERT(source != nullptr, "Prepared graph finalization requires a source graph.");
        for (draft_node_ref_t nodeId = 0; nodeId < prepared.draft->nodeSlotCount(); ++nodeId) {
            const auto *header = prepared.draft->header(nodeId);
            if (header == nullptr) {
                continue;
            }
            const auto payload = prepared.draft->payloadOf(nodeId);
            if (header->kind == GCNodeKind::Func) {
                if (payload.size_bytes() < sizeof(GCFuncBody)) {
                    throw std::runtime_error(
                        std::format(
                            "Prepared FUNC node {} in graph '{}' has truncated payload ({} bytes).",
                            nodeId,
                            source->name(),
                            payload.size_bytes()));
                }
                const auto *body = reinterpret_cast<const GCFuncBody *>(payload.data());
                if (body->calleeGraph == reinterpret_cast<GCGraph *>(-1)) {
                    throw std::runtime_error(
                        std::format(
                            "Prepared FUNC node {} in graph '{}' carries invalid callee "
                            "placeholder -1.",
                            nodeId,
                            source->name()));
                }
                if (body->calleeGraph == nullptr) {
                    throw std::runtime_error(
                        std::format(
                            "Prepared FUNC node {} in graph '{}' carries a null callee "
                            "placeholder.",
                            nodeId,
                            source->name()));
                }
            }
            if (header->kind == GCNodeKind::Join) {
                if (payload.size_bytes() < sizeof(GCJoinBody)) {
                    throw std::runtime_error(
                        std::format(
                            "Prepared JOIN node {} in graph '{}' has truncated payload ({} bytes).",
                            nodeId,
                            source->name(),
                            payload.size_bytes()));
                }
                const auto *body = reinterpret_cast<const GCJoinBody *>(payload.data());
                if (body->armCount != prepared.draft->withInputsOf(nodeId).size()) {
                    throw std::runtime_error(
                        std::format(
                            "Prepared JOIN node {} in graph '{}' has mismatched arm count: "
                            "body={}, inputs={}.",
                            nodeId,
                            source->name(),
                            body->armCount,
                            prepared.draft->withInputsOf(nodeId).size()));
                }
            }
        }
        auto *debugRecord =
            createGraphDebugRecord(source->stableId(), source->name(), source->name());

        const auto &deps = source->dependencyGraphs();
        const auto &subs = source->subGraphs();
        std::vector<GCGraph *> depPtrs;
        std::vector<GCGraph *> subPtrs;
        depPtrs.reserve(deps.size());
        subPtrs.reserve(subs.size());
        for (const auto &dep : deps) {
            depPtrs.push_back(resolvePreparedGraph(dep)->runtime);
        }
        for (const auto &sub : subs) {
            subPtrs.push_back(resolvePreparedGraph(sub)->runtime);
        }
        std::vector<Type *> runtimeStaticTypes(
            prepared.draft->staticSlotTypes().begin(),
            prepared.draft->staticSlotTypes().end());
        CAMEL_LOG_INFO_S(
            "GIREncode",
            "Graph '{}' static-slot counts: values={}, types={}.",
            source->name(),
            prepared.staticSlots.size(),
            runtimeStaticTypes.size());
        std::unordered_map<const Object *, Object *> objectCache;
        for (size_t i = 1; i < prepared.staticSlots.size() && i < runtimeStaticTypes.size(); ++i) {
            Type *slotType = runtimeStaticTypes[i];
            CAMEL_LOG_INFO_S(
                "GIREncode",
                "Canonicalize static slot {} for graph '{}' type={}.",
                i,
                source->name(),
                slotType ? slotType->toString() : std::string{"<null>"});
            const bool canEncode      = canEncodeRuntimeStaticSlotType(slotType);
            const bool shouldTraverse = shouldTraverseStaticSlotType(slotType);
            CAMEL_LOG_INFO_S(
                "GIREncode",
                "Static slot {} for graph '{}' encode={} traverse={}.",
                i,
                source->name(),
                canEncode,
                shouldTraverse);
            if (!canEncode) {
                CAMEL_LOG_INFO_S(
                    "GIREncode",
                    "Static slot {} for graph '{}' stays runtime-null because its type is "
                    "compile-only.",
                    i,
                    source->name());
                prepared.staticSlots[i] = NullSlot;
                runtimeStaticTypes[i]   = Type::Void();
                prepared.draft->setStaticSlot(i, NullSlot);
                CAMEL_LOG_INFO_S(
                    "GIREncode",
                    "Static slot {} for graph '{}' nulling completed.",
                    i,
                    source->name());
                continue;
            }
            if (shouldTraverse) {
                prepared.staticSlots[i] = canonicalizeStaticSlot(
                    prepared.staticSlots[i],
                    slotType,
                    prepared.source,
                    objectCache);
                CAMEL_LOG_INFO_S(
                    "GIREncode",
                    "Canonicalized static slot {} for graph '{}' completed.",
                    i,
                    source->name());
            }
            prepared.draft->setStaticSlot(i, prepared.staticSlots[i]);
        }
        CAMEL_LOG_INFO_S(
            "GIREncode",
            "Static-slot canonicalization finished for graph '{}'.",
            source->name());
        std::vector<graph_ptr_t> compileStaticRefs = source->staticGraphRefs();
        std::vector<GCGraph *> staticGraphRefs;
        staticGraphRefs.reserve(compileStaticRefs.size());
        CAMEL_LOG_INFO_S(
            "GIREncode",
            "Static graph-ref remap begins for graph '{}' count={}.",
            source->name(),
            compileStaticRefs.size());
        for (const auto &graph : compileStaticRefs) {
            staticGraphRefs.push_back(resolvePreparedGraph(graph)->runtime);
        }
        CAMEL_LOG_INFO_S(
            "GIREncode",
            "Static graph-ref remap finished for graph '{}'.",
            source->name());

        TupleType *staticDataType = nullptr;
        if (!runtimeStaticTypes.empty()) {
            std::vector<Type *> types(runtimeStaticTypes.begin(), runtimeStaticTypes.end());
            for (Type *&type : types) {
                if (type == nullptr) {
                    type = Type::Void();
                }
            }
            staticDataType = TupleType::create(types);
            CAMEL_LOG_INFO_S(
                "GIREncode",
                "Static runtime tuple type built for graph '{}'.",
                source->name());
        }
        GCGraph *runtimeOuterGraph = nullptr;
        if (const auto &outer = source->outerGraph(); outer != nullptr) {
            auto *outerPrepared = resolvePreparedGraph(outer);
            CAMEL_LOG_INFO_S(
                "GIREncode",
                "Resolve outer graph for '{}': outer='{}' prepared={}.",
                source->name(),
                outer->name(),
                outerPrepared != nullptr);
            runtimeOuterGraph = outerPrepared->runtime;
        }
        CAMEL_LOG_INFO_S(
            "GIREncode",
            "Construct runtime graph '{}' in place: bytes={}, nodes={}, blocks={}, edges={}.",
            source->name(),
            prepared.bytes,
            prepared.payloadShape.nodeCount,
            prepared.payloadShape.nodeBlockCount,
            prepared.payloadShape.edgeCount);
        prepared.runtime = GCGraphBuildAccess::constructInPlace(
            prepared.memory,
            prepared.bytes,
            debugRecord,
            prepared.draft->funcType(),
            prepared.draft->runtimeDataType(),
            staticDataType,
            prepared.draft->closureType(),
            runtimeOuterGraph,
            depPtrs,
            subPtrs,
            staticGraphRefs,
            prepared.payloadShape,
            [&](camel::runtime::GCGraphPayloadArena &arena) {
                CAMEL_LOG_INFO_S(
                    "GIREncode",
                    "Emit runtime payload for graph '{}': nodeCount={}, blockCount={}, "
                    "edgeCount={}.",
                    source->name(),
                    arena.nodeCount,
                    arena.nodeBlockCount,
                    arena.edgeCount);
                emitDraftNativePayload(*prepared.draft, arena);
                CAMEL_LOG_INFO_S(
                    "GIREncode",
                    "Emit runtime payload finished for graph '{}'.",
                    source->name());
            },
            prepared.staticSlots);
        CAMEL_LOG_INFO_S(
            "GIREncode",
            "Constructed runtime graph '{}', validating payload.",
            source->name());
        validateRuntimeGraphPayload(prepared.runtime);
    }

    graph_ptr_t rootGraph_;
    std::vector<PreparedGraph> prepared_;
    std::unordered_map<DraftGraphBuilder *, PreparedGraph *> preparedBySource_;
    std::unordered_map<std::string, PreparedGraph *> preparedByStableId_;
};

} // namespace

camel::runtime::GCGraph *encodeToRuntimeGraph(const graph_ptr_t &rootGraph) {
    auto *runtimeRoot = EncodeSession(rootGraph).run();
    auto reachable    = camel::runtime::collectReachableGraphs(runtimeRoot);
    CAMEL_LOG_INFO_S(
        "GIREncode",
        "Encoded runtime root '{}' with {} reachable runtime graphs.",
        runtimeRoot ? runtimeRoot->name() : std::string{"<null>"},
        reachable.size());
    return runtimeRoot;
}

} // namespace camel::compile::gir
