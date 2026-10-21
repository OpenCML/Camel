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
 * Updated: Oct. 19, 2026
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
#include "camel/core/rtdata/collection.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/composite.h"
#include "camel/runtime/control.h"
#include "camel/runtime/program.h"
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
using camel::runtime::GCBrchBody;
using camel::runtime::GCCallBody;
using camel::runtime::GCFillBody;
using camel::runtime::GCGraph;
using camel::runtime::GCGraphBuildAccess;
using camel::runtime::GCGraphPayloadShape;
using camel::runtime::GCJoinBody;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::GraphDraft;
using camel::runtime::validateRuntimeGraphPayload;

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
        // A Map or a Set is a container of typed slots too.
        if (const auto *coll = camel::core::type::asCollectionType(type)) {
            camel::core::rtdata::table::forEachSlot(
                fromSlot<camel::core::rtdata::Table *>(slot),
                coll,
                [&](slot_t inner, Type *innerType) {
                    collectCompileStaticGraphRefsRecursive(refs, inner, innerType, visited);
                });
        }
        break;
    }
}

/// The graphs a compile graph refers to statically: its declared static refs plus the graphs of
/// Function values held in its static slots. Sizing and emission must agree on this list.
std::vector<graph_ptr_t> staticGraphRefsOf(const graph_ptr_t &source) {
    std::vector<graph_ptr_t> refs = source->staticGraphRefs();
    std::unordered_set<const Object *> visited;
    const auto staticTypes = source->draft().staticSlotTypes();
    const auto staticSlots = source->draft().staticSlots();
    for (size_t i = 1; i < staticSlots.size() && i < staticTypes.size(); ++i) {
        collectCompileStaticGraphRefsRecursive(refs, staticSlots[i], staticTypes[i], visited);
    }
    return refs;
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
        if (header->kind == GCNodeKind::Loop) {
            // The ports of its regions are part of it, whether the code reads them or not.
            for (auto part : {runtime::GCRegionKind::LoopCond, runtime::GCRegionKind::LoopBody}) {
                if (const uint32_t region = draft.loopRegionOf(current, part); region != 0) {
                    for (draft_node_ref_t port : draft.regionPortsOf(region)) {
                        pushRoot(port);
                    }
                }
            }
        }
        if (header->kind == GCNodeKind::Brch) {
            const auto *payload =
                reinterpret_cast<const DraftBrchPayload *>(draft.payloadOf(current).data());
            if (payload != nullptr) {
                pushRoot(payload->join);
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
    EncodeSession(
        graph_ptr_t rootGraph, camel::runtime::Program &program,
        camel::runtime::GraphIdAllocator *freshIds)
        : rootGraph_(std::move(rootGraph)), program_(program), freshIds_(freshIds) {}

    void run() {
        ASSERT(rootGraph_ != nullptr, "Cannot encode a null compile graph.");
        const auto reachable = collectReachableGraphs(rootGraph_);
        CAMEL_LOG_INFO_S("GIREncode", "Encoding {} reachable compile graphs.", reachable.size());
        prepared_.reserve(reachable.size());
        for (const auto &graph : reachable) {
            prepared_.push_back(prepareGraph(graph));
        }
        for (auto &prepared : prepared_) {
            preparedBySource_.emplace(prepared.source.get(), &prepared);
            if (runtime::isValid(prepared.source->graphId())) {
                preparedById_.emplace(prepared.source->graphId(), &prepared);
            }
        }
        preallocateRuntimeGraphs();
        for (auto &prepared : prepared_) {
            program_.bind(prepared.draft->id(), prepared.runtime);
        }
        program_.setRoot(resolvePreparedGraph(rootGraph_)->runtime);
        for (auto &prepared : prepared_) {
            patchPreparedFuncPayloads(prepared);
        }
        for (auto &prepared : prepared_) {
            CAMEL_LOG_INFO_S("GIREncode", "Finalize runtime graph '{}'.", prepared.source->name());
            finalizeRuntimeGraph(prepared);
        }
        for (auto &prepared : prepared_) {
            camel::runtime::validateRuntimeGraphCalls(program_, prepared.runtime);
        }
    }

  private:
    /// The prepared graph of `graph` when it is part of the encoded set; nullptr otherwise.
    PreparedGraph *findPreparedGraph(const graph_ptr_t &graph) {
        if (auto it = preparedBySource_.find(graph.get()); it != preparedBySource_.end()) {
            return it->second;
        }
        if (runtime::isValid(graph->graphId())) {
            if (auto it = preparedById_.find(graph->graphId()); it != preparedById_.end()) {
                return it->second;
            }
        }
        return nullptr;
    }

    PreparedGraph *resolvePreparedGraph(const graph_ptr_t &graph) {
        ASSERT(graph != nullptr, "Cannot resolve a null compile graph.");
        if (auto it = preparedBySource_.find(graph.get()); it != preparedBySource_.end()) {
            return it->second;
        }
        if (runtime::isValid(graph->graphId())) {
            if (auto it = preparedById_.find(graph->graphId()); it != preparedById_.end()) {
                return it->second;
            }
        }
        for (auto &prepared : prepared_) {
            if (prepared.source && prepared.source->name() == graph->name()) {
                CAMEL_LOG_WARN_S(
                    "GIREncode",
                    "Compile graph identity fallback matched by name only: requested='{}' "
                    "id={}.",
                    graph->name(),
                    runtime::toValue(graph->graphId()));
                return &prepared;
            }
        }
        ASSERT(
            false,
            std::format(
                "Compile graph '{}' (id={}) is not part of the prepared encode set.",
                graph->name(),
                runtime::toValue(graph->graphId())));
        return nullptr;
    }

    PreparedGraph prepareGraph(const graph_ptr_t &graph) {
        PreparedGraph prepared;
        prepared.source = graph;
        prepared.draft  = std::make_unique<GraphDraft>();
        prepared.draft->setInheritsRegions(false);
        // Graph properties fixed before encoding: the macro flag of a `#macro` function is the
        // one that matters here; a runtime graph keeps it for std::macro (design principles 8).
        prepared.draft->setFlags(graph->draft().flags());
        CAMEL_LOG_INFO_S("GIREncode", "Prepare compile graph '{}'.", graph->name());
        prepared.draft->setId(freshIds_ ? freshIds_->allocate() : graph->graphId());
        prepared.draft->setFuncType(graph->funcType());
        prepared.draft->setCaptureNames(graph->draft().captureNames());
        // Every compiled graph has a frame layout, one without slots included.
        prepared.draft->setRuntimeSlotTypes(graph->draft().runtimeSlotTypes());
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
                .origin       = header->origin,
                .permissions  = header->permissions,
                .attrs        = header->attrs,
                .region       = header->region,
            };
            std::vector<std::byte> payloadStorage;

            switch (header->kind) {
            case GCNodeKind::Func: {
                payloadStorage = toPayloadBytes(camel::runtime::GCFuncRef{});
            } break;
            case GCNodeKind::Loop: {
                // The graphs are patched in once every runtime graph has its place
                // (patchPreparedFuncPayloads), as for a FUNC node.
                const auto *source = reinterpret_cast<const camel::runtime::GCLoopRef *>(
                    graph->draft().payloadOf(sourceId).data());
                payloadStorage = toPayloadBytes(
                    camel::runtime::GCLoopRef{
                        .kind            = source->kind,
                        .collectionCount = source->collectionCount,
                    });
            } break;
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
                std::vector<std::byte> bytes(sizeof(DraftBrchPayload));
                DraftBrchPayload body{
                    .join     = remapNodeRef(sourceBody->join, sourceToPrepared),
                    .armCount = sourceBody->armCount,
                };
                std::memcpy(bytes.data(), &body, sizeof(body));
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

        // Later statements may make an earlier control edge redundant. Clean up once before
        // freezing, with a smaller work cap than the explicit optimization stage.
        camel::runtime::reduceControlDependencies(*prepared.draft, 8192);
        // The regions keep their ids; one whose owner is not reachable is dead.
        const GraphDraft &built = graph->draft();
        const auto mapped       = [&](runtime::gc_node_ref_t ref) {
            return ref == runtime::kInvalidNodeRef ? ref : sourceToPrepared[ref];
        };
        for (uint32_t r = 1; r < built.regionCount(); ++r) {
            const runtime::GCRegion &region = built.region(r);
            const bool controls             = region.kind == runtime::GCRegionKind::Arm ||
                                  region.kind == runtime::GCRegionKind::LoopCond ||
                                  region.kind == runtime::GCRegionKind::LoopBody;
            const runtime::gc_node_ref_t owner = mapped(region.owner);
            const bool dead           = controls && owner == runtime::kInvalidNodeRef;
            const uint32_t id         = prepared.draft->addRegion(
                dead ? runtime::GCRegionKind::Dead : region.kind,
                region.parent,
                owner,
                region.index);
            prepared.draft->setRegionResult(id, mapped(region.result));
            prepared.draft->setRegionOrigin(id, region.origin);
            prepared.draft->setRegionAttrs(id, region.attrs);
            prepared.draft->setRegionParams(id, region.params);
            std::vector<runtime::gc_node_ref_t> ports;
            for (runtime::gc_node_ref_t port : built.regionPortsOf(r)) {
                ports.push_back(mapped(port));
            }
            prepared.draft->setRegionPorts(id, ports);
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
            const auto &deps                          = prepared.source->dependencyGraphs();
            const auto &subs                          = prepared.source->subGraphs();
            const std::vector<graph_ptr_t> staticRefs = staticGraphRefsOf(prepared.source);
            const std::vector<graph_ptr_t> ruleRefs   = prepared.source->ruleGraphs();
            std::vector<camel::runtime::GraphId> depPtrs(
                deps.size(),
                camel::runtime::kInvalidGraphId);
            std::vector<camel::runtime::GraphId> subPtrs(
                subs.size(),
                camel::runtime::kInvalidGraphId);
            std::vector<camel::runtime::GraphId> staticGraphPtrs(
                staticRefs.size(),
                camel::runtime::kInvalidGraphId);
            std::vector<camel::runtime::GraphId> ruleGraphPtrs(
                ruleRefs.size(),
                camel::runtime::kInvalidGraphId);
            prepared.bytes = GCGraphBuildAccess::requiredBytes(
                depPtrs,
                subPtrs,
                staticGraphPtrs,
                ruleGraphPtrs,
                prepared.payloadShape,
                prepared.staticSlots,
                prepared.draft->frameSlotTypes());
            try {
                prepared.memory =
                    camel::core::mm::graphSpace().alloc(prepared.bytes, alignof(GCGraph));
            } catch (const camel::core::mm::MemoryExhausted &e) {
                throw e.during(
                    std::format("encoding graph '{}'", prepared.source->name()),
                    prepared.draft->firstNodeOrigin());
            }
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
                prepared.draft->setCalleeGraph(
                    preparedId,
                    resolvePreparedGraph(prepared.source->funcTarget(sourceId))->draft->id());
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
                &program_,
                resolvePreparedGraph(funcObj->graph())->draft->id(),
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
                            objectCache),
                        runtimeClosureType);
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
                        objectCache),
                    tupleType);
            }
            return toSlot<Object *>(cloned);
        }
        case TypeCode::Array: {
            auto *array     = fromSlot<::Array *>(slot);
            auto *arrayType = static_cast<ArrayType *>(type);
            auto *cloned    = ::Array::create(camel::core::mm::autoSpace(), arrayType, array->size());
            objectCache.emplace(object, cloned);
            for (size_t i = 0; i < array->size(); ++i) {
                cloned->set<slot_t>(
                    i,
                    canonicalizeStaticSlot(
                        array->get<slot_t>(i),
                        arrayType->elemType(),
                        sourceGraph,
                        objectCache),
                    arrayType);
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
                        objectCache),
                    structType);
            }
            return toSlot<Object *>(cloned);
        }
        default: {
            // A Map or a Set: its keys and values are canonicalized like the elements of an
            // array, so that nothing in the runtime copy points into the compile-time data.
            if (const auto *coll = camel::core::type::asCollectionType(type)) {
                namespace rtdata = camel::core::rtdata;
                auto *table      = fromSlot<rtdata::Table *>(slot);
                Object *cloned   = rtdata::table::withSlots(
                    table,
                    coll,
                    camel::core::mm::autoSpace(),
                    [&](slot_t inner, Type *innerType) {
                        return canonicalizeStaticSlot(inner, innerType, sourceGraph, objectCache);
                    });
                if (cloned == object) {
                    cloned = object->clone(camel::core::mm::autoSpace(), type, false);
                }
                objectCache.emplace(object, cloned);
                return toSlot<Object *>(cloned);
            }
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
                if (!camel::runtime::isValid(prepared.draft->calleeIdOf(nodeId))) {
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
        auto *debugRecord = createGraphDebugRecord(source->name(), source->name());
        recordDraftDebugData(debugRecord, *prepared.draft);

        const auto &deps = source->dependencyGraphs();
        const auto &subs = source->subGraphs();
        std::vector<camel::runtime::GraphId> depPtrs;
        std::vector<camel::runtime::GraphId> subPtrs;
        depPtrs.reserve(deps.size());
        subPtrs.reserve(subs.size());
        for (const auto &dep : deps) {
            depPtrs.push_back(resolvePreparedGraph(dep)->draft->id());
        }
        for (const auto &sub : subs) {
            subPtrs.push_back(resolvePreparedGraph(sub)->draft->id());
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
        std::vector<graph_ptr_t> compileStaticRefs = staticGraphRefsOf(prepared.source);
        std::vector<camel::runtime::GraphId> staticGraphRefs;
        staticGraphRefs.reserve(compileStaticRefs.size());
        CAMEL_LOG_INFO_S(
            "GIREncode",
            "Static graph-ref remap begins for graph '{}' count={}.",
            source->name(),
            compileStaticRefs.size());
        for (const auto &graph : compileStaticRefs) {
            staticGraphRefs.push_back(resolvePreparedGraph(graph)->draft->id());
        }
        std::vector<graph_ptr_t> compileRuleRefs = prepared.source->ruleGraphs();
        std::vector<camel::runtime::GraphId> ruleGraphs;
        ruleGraphs.reserve(compileRuleRefs.size());
        for (const auto &graph : compileRuleRefs) {
            ruleGraphs.push_back(resolvePreparedGraph(graph)->draft->id());
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
            staticDataType = TupleType::layout(types);
            CAMEL_LOG_INFO_S(
                "GIREncode",
                "Static runtime tuple type built for graph '{}'.",
                source->name());
        }
        camel::runtime::GraphId runtimeOuterGraph = camel::runtime::kInvalidGraphId;
        if (const auto &outer = source->outerGraph(); outer != nullptr) {
            // A graph encoded without the graph it is written in (a function of a library
            // encoded on its own, camel/runtime/definitions.h) has no outer graph at runtime.
            auto *outerPrepared = findPreparedGraph(outer);
            CAMEL_LOG_INFO_S(
                "GIREncode",
                "Resolve outer graph for '{}': outer='{}' prepared={}.",
                source->name(),
                outer->name(),
                outerPrepared != nullptr);
            runtimeOuterGraph = outerPrepared != nullptr ? outerPrepared->draft->id()
                                                         : camel::runtime::kInvalidGraphId;
        }
        CAMEL_LOG_INFO_S(
            "GIREncode",
            "Construct runtime graph '{}' in place: bytes={}, nodes={}, payloadWords={}, edges={}.",
            source->name(),
            prepared.bytes,
            prepared.payloadShape.nodeCount,
            prepared.payloadShape.payloadWordCount,
            prepared.payloadShape.edgeCount);
        prepared.runtime = GCGraphBuildAccess::constructInPlace(
            prepared.memory,
            prepared.bytes,
            prepared.draft->id(),
            camel::runtime::GCGraphDerivation{
                camel::runtime::kInvalidGraphId, prepared.draft->flags()},
            debugRecord,
            prepared.draft->funcType(),
            prepared.draft->frameSlotTypes(),
            staticDataType,
            prepared.draft->closureType(),
            runtimeOuterGraph,
            depPtrs,
            subPtrs,
            staticGraphRefs,
            ruleGraphs,
            prepared.payloadShape,
            [&](camel::runtime::GCGraphPayloadArena &arena) {
                CAMEL_LOG_INFO_S(
                    "GIREncode",
                    "Emit runtime payload for graph '{}': nodeCount={}, payloadWords={}, "
                    "edgeCount={}.",
                    source->name(),
                    arena.nodeCount,
                    arena.payloadWordCount,
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
        validateRuntimeGraphPayload(program_, prepared.runtime);
    }

    graph_ptr_t rootGraph_;
    camel::runtime::Program &program_;
    camel::runtime::GraphIdAllocator *freshIds_;
    std::vector<PreparedGraph> prepared_;
    std::unordered_map<DraftGraphBuilder *, PreparedGraph *> preparedBySource_;
    std::unordered_map<runtime::GraphId, PreparedGraph *> preparedById_;
};

} // namespace

void encodeToRuntimeProgram(
    const graph_ptr_t &rootGraph, camel::runtime::Program &program,
    camel::runtime::GraphIdAllocator *freshIds) {
    EncodeSession(rootGraph, program, freshIds).run();
    CAMEL_LOG_INFO_S(
        "GIREncode",
        "Encoded runtime root '{}' with {} runtime graphs.",
        program.root() ? program.root()->name() : std::string{"<null>"},
        program.size());
}

} // namespace camel::compile::gir
