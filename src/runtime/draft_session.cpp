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
 * Runtime-native graph draft session.
 *
 * Commit is deliberately closure-wide rather than per-graph. A runtime rewrite
 * may change direct callee graphs, dependency registries, and static Function
 * objects in the same transaction, so the session re-encodes the whole
 * reachable closure and rewires all runtime graph references before replacing
 * the manager-owned root.
 */

#include "camel/runtime/draft_session.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"

#include <queue>
#include <unordered_set>

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

using GraphMap    = std::unordered_map<GCGraph *, GCGraph *>;
using ObjectCache = std::unordered_map<const Object *, Object *>;
using ObjectSet   = std::unordered_set<const Object *>;

std::byte *mutableNodeStorage(gc_block_t *blocks, gc_node_ref_t ref) {
    return reinterpret_cast<std::byte *>(blocks + ref);
}

GCGraph *resolveRuntimeGraphCarrier(
    const camel::core::context::context_ptr_t &context, const ::Function *func) {
    ASSERT(func != nullptr, "Runtime Function carrier resolution received a null function.");
    (void)context;
    GCGraph *runtimeGraph = func->runtimeGraph();
    ASSERT(
        runtimeGraph != nullptr,
        "Runtime rewrite encountered a Function without a GCGraph carrier.");
    return runtimeGraph;
}

void visitGraphsInStaticSlot(
    const camel::core::context::context_ptr_t &context, slot_t slot, Type *type,
    const std::function<void(GCGraph *)> &visitor, ObjectSet &visited) {
    if (!type || !type->isGCTraced() || slot == NullSlot) {
        return;
    }

    Object *object = fromSlot<Object *>(slot);
    if (!object || !visited.insert(object).second) {
        return;
    }

    switch (type->code()) {
    case TypeCode::Function: {
        auto *func = fromSlot<::Function *>(slot);
        if (!func) {
            return;
        }
        visitor(resolveRuntimeGraphCarrier(context, func));
        if (::Tuple *closure = func->tuple()) {
            auto *closureType = const_cast<TupleType *>(func->tupleType());
            for (size_t i = 0; i < closureType->size(); ++i) {
                if (!camel::core::type::isGCTraced(closureType->codeAt(i))) {
                    continue;
                }
                visitGraphsInStaticSlot(
                    context,
                    closure->get<slot_t>(i),
                    closureType->typeAt(i),
                    visitor,
                    visited);
            }
        }
        break;
    }
    case TypeCode::Tuple: {
        auto *tuple     = fromSlot<::Tuple *>(slot);
        auto *tupleType = static_cast<TupleType *>(type);
        for (size_t i = 0; i < tupleType->size(); ++i) {
            if (!camel::core::type::isGCTraced(tupleType->codeAt(i))) {
                continue;
            }
            visitGraphsInStaticSlot(
                context,
                tuple->get<slot_t>(i),
                tupleType->typeAt(i),
                visitor,
                visited);
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
            visitGraphsInStaticSlot(
                context,
                array->get<slot_t>(i),
                arrayType->elemType(),
                visitor,
                visited);
        }
        break;
    }
    case TypeCode::Struct: {
        auto *st         = fromSlot<::Struct *>(slot);
        auto *structType = static_cast<StructType *>(type);
        for (size_t i = 0; i < structType->size(); ++i) {
            if (!camel::core::type::isGCTraced(structType->codeAt(i))) {
                continue;
            }
            visitGraphsInStaticSlot(
                context,
                st->get<slot_t>(i),
                structType->typeAt(i),
                visitor,
                visited);
        }
        break;
    }
    default:
        break;
    }
}

slot_t cloneStaticSlot(
    const camel::core::context::context_ptr_t &context, slot_t slot, Type *type,
    const GraphMap &rewritten, ObjectCache &objectCache) {
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
        auto *func        = fromSlot<::Function *>(slot);
        GCGraph *oldGraph = resolveRuntimeGraphCarrier(context, func);
        auto graphIt      = rewritten.find(oldGraph);
        ASSERT(
            graphIt != rewritten.end(),
            "Runtime static Function references a graph outside the rewrite closure.");

        auto *cloned = ::Function::create(graphIt->second, func->tupleType(), mm::autoSpace());
        objectCache.emplace(object, cloned);

        if (::Tuple *closure = func->tuple()) {
            ::Tuple *clonedClosure       = cloned->tuple();
            const TupleType *closureType = cloned->tupleType();
            for (size_t i = 0; i < closureType->size(); ++i) {
                clonedClosure->set<slot_t>(
                    i,
                    camel::core::type::isGCTraced(closureType->codeAt(i))
                        ? cloneStaticSlot(
                              context,
                              closure->get<slot_t>(i),
                              closureType->typeAt(i),
                              rewritten,
                              objectCache)
                        : closure->get<slot_t>(i));
            }
        }
        return toSlot<Object *>(cloned);
    }
    case TypeCode::Tuple: {
        auto *tuple       = fromSlot<::Tuple *>(slot);
        auto *tupleType   = static_cast<TupleType *>(type);
        auto *clonedTuple = ::Tuple::create(tupleType->size(), mm::autoSpace());
        objectCache.emplace(object, clonedTuple);
        for (size_t i = 0; i < tupleType->size(); ++i) {
            clonedTuple->set<slot_t>(
                i,
                camel::core::type::isGCTraced(tupleType->codeAt(i)) ? cloneStaticSlot(
                                                                          context,
                                                                          tuple->get<slot_t>(i),
                                                                          tupleType->typeAt(i),
                                                                          rewritten,
                                                                          objectCache)
                                                                    : tuple->get<slot_t>(i));
        }
        return toSlot<Object *>(clonedTuple);
    }
    case TypeCode::Array: {
        auto *array       = fromSlot<::Array *>(slot);
        auto *arrayType   = static_cast<ArrayType *>(type);
        auto *clonedArray = ::Array::create(mm::autoSpace(), array->size());
        objectCache.emplace(object, clonedArray);
        for (size_t i = 0; i < array->size(); ++i) {
            clonedArray->set<slot_t>(
                i,
                camel::core::type::isGCTraced(arrayType->elemTypeCode())
                    ? cloneStaticSlot(
                          context,
                          array->get<slot_t>(i),
                          arrayType->elemType(),
                          rewritten,
                          objectCache)
                    : array->get<slot_t>(i));
        }
        return toSlot<Object *>(clonedArray);
    }
    case TypeCode::Struct: {
        auto *st           = fromSlot<::Struct *>(slot);
        auto *structType   = static_cast<StructType *>(type);
        auto *clonedStruct = ::Struct::create(structType->size(), mm::autoSpace());
        objectCache.emplace(object, clonedStruct);
        for (size_t i = 0; i < structType->size(); ++i) {
            clonedStruct->set<slot_t>(
                i,
                camel::core::type::isGCTraced(structType->codeAt(i)) ? cloneStaticSlot(
                                                                           context,
                                                                           st->get<slot_t>(i),
                                                                           structType->typeAt(i),
                                                                           rewritten,
                                                                           objectCache)
                                                                     : st->get<slot_t>(i));
        }
        return toSlot<Object *>(clonedStruct);
    }
    default: {
        Object *cloned = object->clone(mm::autoSpace(), type, false);
        objectCache.emplace(object, cloned);
        return toSlot<Object *>(cloned);
    }
    }
}

void collectStaticGraphRefs(
    const camel::core::context::context_ptr_t &context, GCGraph *owner,
    std::span<const slot_t> staticSlots, Type *staticType) {
    if (!owner || !staticType || staticType->code() != TypeCode::Tuple) {
        return;
    }

    auto *tupleType = static_cast<TupleType *>(staticType);
    ObjectSet visited;
    for (size_t i = 1; i < staticSlots.size() && i < tupleType->size(); ++i) {
        if (!camel::core::type::isGCTraced(tupleType->codeAt(i))) {
            continue;
        }
        visitGraphsInStaticSlot(
            context,
            staticSlots[i],
            tupleType->typeAt(i),
            [&](GCGraph *graph) { owner->addStaticGraphRef(graph); },
            visited);
    }
}

void visitDraftGraphs(
    const camel::core::context::context_ptr_t &context, const GraphDraft &draft,
    const std::function<void(GCGraph *)> &visitor) {
    for (GCGraph *graph : draft.dependencies()) {
        visitor(graph);
    }
    for (GCGraph *graph : draft.subGraphs()) {
        visitor(graph);
    }
    for (GCGraph *graph : draft.staticGraphRefs()) {
        visitor(graph);
    }
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id)) {
            continue;
        }
        const DraftNodeHeader *header = draft.header(id);
        if (!header || header->kind != GCNodeKind::Func) {
            continue;
        }
        auto payload = draft.payloadOf(id);
        if (payload.size_bytes() < sizeof(GCFuncBody)) {
            continue;
        }
        auto *body = reinterpret_cast<const GCFuncBody *>(payload.data());
        visitor(body->calleeGraph);
    }

    const auto staticSlotTypes = draft.staticSlotTypes();
    if (!staticSlotTypes.empty()) {
        ObjectSet visited;
        for (size_t i = 1; i < draft.staticSlots().size(); ++i) {
            camel::core::type::Type *slotType =
                i < staticSlotTypes.size() ? staticSlotTypes[i] : nullptr;
            if (!slotType || !slotType->isGCTraced()) {
                continue;
            }
            visitGraphsInStaticSlot(context, draft.staticSlots()[i], slotType, visitor, visited);
        }
    }
}

void retargetEncodedGraph(
    const camel::core::context::context_ptr_t &context, const GraphDraft &draft, GCGraph *encoded,
    const GraphMap &rewritten) {
    ASSERT(encoded != nullptr, "Runtime draft commit cannot retarget a null graph.");
    encoded->clearGraphRefs();
    if (!draft.staticSlotTypes().empty()) {
        std::vector<camel::core::type::Type *> staticTypes(
            draft.staticSlotTypes().begin(),
            draft.staticSlotTypes().end());
        encoded->setStaticDataType(TupleType::create(std::move(staticTypes)));
    }

    auto remapGraph = [&](GCGraph *graph) -> GCGraph * {
        if (!graph) {
            return nullptr;
        }
        auto it = rewritten.find(graph);
        ASSERT(it != rewritten.end(), "Runtime draft commit lost a referenced graph.");
        return it->second;
    };

    for (GCGraph *graph : draft.dependencies()) {
        encoded->addDependency(remapGraph(graph));
    }
    for (GCGraph *graph : draft.subGraphs()) {
        encoded->addSubGraph(remapGraph(graph));
    }
    for (GCGraph *graph : draft.staticGraphRefs()) {
        encoded->addStaticGraphRef(remapGraph(graph));
    }

    const GCGraphNativePayload *payload = encoded->nodePayload();
    ASSERT(payload != nullptr, "Runtime draft commit produced a graph without native payload.");
    auto *nodeBlocks = const_cast<gc_block_t *>(payload->nodeBlocks);
    for (auto it = encoded->nodes().begin(); it != encoded->nodes().end(); ++it) {
        const GCNode *node = *it;
        if (!node || node->kind != GCNodeKind::Func) {
            continue;
        }
        auto *body = reinterpret_cast<GCFuncBody *>(
            mutableNodeStorage(nodeBlocks, it.ref()) + sizeof(GCNode));
        body->calleeGraph = remapGraph(body->calleeGraph);
        encoded->addDependency(body->calleeGraph);
    }

    std::vector<slot_t> staticSlots(draft.staticSlots().begin(), draft.staticSlots().end());
    if (auto *tupleType = const_cast<TupleType *>(encoded->staticDataType())) {
        if (staticSlots.size() < tupleType->size()) {
            staticSlots.resize(tupleType->size(), NullSlot);
        }
        ObjectCache objectCache;
        for (size_t i = 1; i < tupleType->size(); ++i) {
            if (!camel::core::type::isGCTraced(tupleType->codeAt(i))) {
                continue;
            }
            staticSlots[i] = cloneStaticSlot(
                context,
                staticSlots[i],
                tupleType->typeAt(i),
                rewritten,
                objectCache);
        }
    }
    encoded->setStaticSlots(std::move(staticSlots));
    collectStaticGraphRefs(
        context,
        encoded,
        encoded->staticSlots(),
        const_cast<TupleType *>(encoded->staticDataType()));
}

} // namespace

RuntimeGraphDraftSession::RuntimeGraphDraftSession(
    const camel::core::context::context_ptr_t &context, GCGraph *runtimeRoot)
    : context_(context), runtimeRoot_(runtimeRoot) {
    ASSERT(context_ != nullptr, "Runtime graph draft session requires a valid context.");
    ASSERT(runtimeRoot_ != nullptr, "Runtime graph draft session requires a runtime root graph.");
}

bool RuntimeGraphDraftSession::hasDraft(const GCGraph *graph) const {
    return graph != nullptr && drafts_.contains(const_cast<GCGraph *>(graph));
}

GraphDraft *RuntimeGraphDraftSession::tryDraft(const GCGraph *graph) {
    if (!graph) {
        return nullptr;
    }
    if (auto it = drafts_.find(const_cast<GCGraph *>(graph)); it != drafts_.end()) {
        return it->second.get();
    }
    return nullptr;
}

const GraphDraft *RuntimeGraphDraftSession::tryDraft(const GCGraph *graph) const {
    if (!graph) {
        return nullptr;
    }
    if (auto it = drafts_.find(const_cast<GCGraph *>(graph)); it != drafts_.end()) {
        return it->second.get();
    }
    return nullptr;
}

GraphDraft &RuntimeGraphDraftSession::ensureDraft(GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime graph draft decode requires a non-null graph.");
    auto [it, inserted] = drafts_.try_emplace(graph);
    if (inserted) {
        it->second = GraphDraft::decode(graph);
    }
    return *it->second;
}

GraphDraft &RuntimeGraphDraftSession::edit(GCGraph *graph) { return ensureDraft(graph); }

GraphDraft &RuntimeGraphDraftSession::rootDraft() { return ensureDraft(runtimeRoot_); }

std::vector<GCGraph *> RuntimeGraphDraftSession::collectCommitClosure() const {
    std::vector<GCGraph *> closure;
    if (!runtimeRoot_) {
        return closure;
    }

    std::queue<GCGraph *> worklist;
    std::unordered_set<GCGraph *> visited;
    worklist.push(runtimeRoot_);

    while (!worklist.empty()) {
        GCGraph *graph = worklist.front();
        worklist.pop();
        if (!graph || !visited.insert(graph).second) {
            continue;
        }

        closure.push_back(graph);
        if (const GraphDraft *draft = tryDraft(graph)) {
            visitDraftGraphs(context_, *draft, [&](GCGraph *next) {
                if (next) {
                    worklist.push(next);
                }
            });
        } else {
            graph->traceGraphs([&](GCGraph *next) { worklist.push(next); });
        }
    }

    return closure;
}

std::vector<GCGraph *> RuntimeGraphDraftSession::collectReachableRuntimeGraphs() const {
    return collectCommitClosure();
}

GCGraph *RuntimeGraphDraftSession::commit() {
    if (!runtimeRoot_) {
        return nullptr;
    }

    const std::vector<GCGraph *> closure = collectCommitClosure();
    ASSERT(!closure.empty(), "Runtime draft commit received an empty closure.");
    for (GCGraph *graph : closure) {
        (void)ensureDraft(graph);
    }

    GCGraph *oldRuntimeRoot = runtimeRoot_;

    GraphMap rewritten;
    rewritten.reserve(closure.size());
    for (GCGraph *graph : closure) {
        rewritten.emplace(graph, drafts_.at(graph).get()->encode());
    }
    for (GCGraph *graph : closure) {
        retargetEncodedGraph(context_, *drafts_.at(graph), rewritten.at(graph), rewritten);
    }

    runtimeRoot_ = context_->installRuntimeRoot(rewritten.at(oldRuntimeRoot));
    drafts_.clear();
    return runtimeRoot_;
}

} // namespace camel::runtime
