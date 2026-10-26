// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include "camel/runtime/reachable.h"
#include "runtime/graph_build.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/collection.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/composite.h"
#include "camel/core/type/intern.h"
#include "camel/utils/log.h"
#include "camel/utils/pointer_set.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
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
/// Receives the graph a function constant names, with the Program the function was made in.
using GraphVisitor = std::function<void(GraphId, const Program *)>;

template <typename T> void hashCombine(size_t &seed, const T &value) {
    const size_t h = std::hash<T>{}(value);
    seed ^= h + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
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

RuntimeDraftIdentity makeRuntimeDraftIdentity(GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime draft identity requires a non-null source graph.");
    return RuntimeDraftIdentity{
        .sourceGraph = graph,
        .mangledName = graph->mangledName(),
        .name        = graph->name(),
    };
}

void visitGraphsInStaticSlot(
    const camel::core::context::context_ptr_t &context, slot_t slot, Type *type,
    const GraphVisitor &visitor, ObjectSet &visited) {
    if (!shouldTraverseStaticSlotType(type) || slot == NullSlot) {
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
        visitor(func->graphId(), func->program());
        if (::Tuple *closure = func->tuple()) {
            // The layout the function was created with, not the one resolved through the graph:
            // a graph of the group being encoded is bound only after its bytes are collected,
            // and this function value names it already.
            auto *closureType = const_cast<TupleType *>(func->closureType());
            for (size_t i = 0; i < closureType->size(); ++i) {
                if (!shouldTraverseStaticSlotType(closureType->typeAt(i))) {
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
            if (!shouldTraverseStaticSlotType(tupleType->typeAt(i))) {
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
        if (!shouldTraverseStaticSlotType(arrayType->elemType())) {
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
            if (!shouldTraverseStaticSlotType(structType->typeAt(i))) {
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
        // A Map or a Set is a container of typed slots too.
        if (const auto *coll = camel::core::type::asCollectionType(type)) {
            camel::core::rtdata::table::forEachSlot(
                fromSlot<camel::core::rtdata::Table *>(slot),
                coll,
                [&](slot_t inner, Type *innerType) {
                    visitGraphsInStaticSlot(context, inner, innerType, visitor, visited);
                });
        }
        break;
    }
}

// The copy of the static value `slot` for the graphs of `program`: a function value names its
// graph by id there; one whose graph `program` does not hold stays in the program it is in.
slot_t cloneStaticSlot(
    const camel::core::context::context_ptr_t &context, slot_t slot, Type *type,
    const Program *program, ObjectCache &objectCache) {
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
        auto *func = fromSlot<::Function *>(slot);

        auto *cloned = ::Function::create(
            program->graph(func->graphId()) != nullptr ? program : func->program(),
            func->graphId(),
            func->closureType(),
            mm::autoSpace());
        objectCache.emplace(object, cloned);

        if (::Tuple *closure = func->tuple()) {
            ::Tuple *clonedClosure       = cloned->tuple();
            const TupleType *closureType = cloned->closureType();
            for (size_t i = 0; i < closureType->size(); ++i) {
                clonedClosure->set<slot_t>(
                    i,
                    shouldTraverseStaticSlotType(closureType->typeAt(i))
                        ? cloneStaticSlot(
                              context,
                              closure->get<slot_t>(i),
                              closureType->typeAt(i),
                              program,
                              objectCache)
                        : closure->get<slot_t>(i),
                    closureType);
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
                shouldTraverseStaticSlotType(tupleType->typeAt(i)) ? cloneStaticSlot(
                                                                         context,
                                                                         tuple->get<slot_t>(i),
                                                                         tupleType->typeAt(i),
                                                                         program,
                                                                         objectCache)
                                                                   : tuple->get<slot_t>(i),
                tupleType);
        }
        return toSlot<Object *>(clonedTuple);
    }
    case TypeCode::Array: {
        auto *array     = fromSlot<::Array *>(slot);
        auto *arrayType = static_cast<ArrayType *>(type);
        // A compact array is pure data (include/camel/core/rtdata/array.h: the storage class comes
        // from the element type), so its clone is the value's own element type and extents, and
        // `Array::clone` reads both from the value rather than from the type the slot is declared
        // with. The declared type may state neither: `Tensor` — the alias's transitional spelling
        // — means "numeric, the element type is not written down", and a value cloned under it came
        // out a slot array with no element type at one slot an element. A kernel that reads the
        // value by its dtype then refused (`sum: Unsupported Tensor dtype`, `matmul: ...`,
        // `subtract: ...`) and a reader that asks for the width of an element got one the value
        // has not (`to_bytes`). A native multidimensional array is its element type and its shape
        // (design principles 5.4), and an unstated element type means the type does not say which
        // one the value has — not that the value has none — so the clone takes the value's, the rule
        // the `nn`/`optim` tensor leaf already follows.
        if (array != nullptr && array->isCompact()) {
            auto *clonedArray =
                static_cast<::Array *>(array->clone(mm::autoSpace(), arrayType, false));
            objectCache.emplace(object, clonedArray);
            return toSlot<Object *>(clonedArray);
        }
        // A slot array is a container of references, whose elements are cloned with the type the
        // element states (a closure names a graph of `program`). The clone has the value's own
        // extents and element count: the size overload is the rank-1 convenience
        // (include/camel/core/rtdata/array.h): a multi-dimensional value that went through it came
        // out rank 1 with its first extent's worth of elements, so a (2, 3) file bound to a
        // parameter read back as [2] with two elements and a matmul of two such values failed with
        // "inner dimensions differ: 2 vs 3".
        auto *clonedArray = ::Array::create(mm::autoSpace(), arrayType, array->shapeSpan());
        objectCache.emplace(object, clonedArray);
        for (uint64_t i = 0; i < array->numel(); ++i) {
            clonedArray->set<slot_t>(
                i,
                shouldTraverseStaticSlotType(arrayType->elemType()) ? cloneStaticSlot(
                                                                          context,
                                                                          array->get<slot_t>(i),
                                                                          arrayType->elemType(),
                                                                          program,
                                                                          objectCache)
                                                                    : array->get<slot_t>(i),
                arrayType);
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
                shouldTraverseStaticSlotType(structType->typeAt(i)) ? cloneStaticSlot(
                                                                          context,
                                                                          st->get<slot_t>(i),
                                                                          structType->typeAt(i),
                                                                          program,
                                                                          objectCache)
                                                                    : st->get<slot_t>(i),
                structType);
        }
        return toSlot<Object *>(clonedStruct);
    }
    default:
        // A Map or a Set may hold function values, which name graphs: its slots are rewritten
        // like the elements of an array. It is copied only when a slot changed.
        if (const auto *coll = camel::core::type::asCollectionType(type)) {
            namespace rtdata = camel::core::rtdata;
            Object *cloned   = rtdata::table::withSlots(
                fromSlot<rtdata::Table *>(slot),
                coll,
                mm::autoSpace(),
                [&](slot_t inner, Type *innerType) {
                    return shouldTraverseStaticSlotType(innerType) ? cloneStaticSlot(
                                                                         context,
                                                                         inner,
                                                                         innerType,
                                                                         program,
                                                                         objectCache)
                                                                     : inner;
                });
            objectCache.emplace(object, cloned);
            return toSlot<Object *>(cloned);
        }
        // Other objects (strings, tensors, ...) refer to no graph: the rewritten graph shares
        // them. Copying would duplicate every weight a program captures on every rewrite.
        return slot;
    }
}

void pushUniqueGraph(std::vector<GraphId> &graphs, GraphId graph) {
    if (isValid(graph) && std::find(graphs.begin(), graphs.end(), graph) == graphs.end()) {
        graphs.push_back(graph);
    }
}

std::vector<GraphId> collectStaticGraphRefs(
    const camel::core::context::context_ptr_t &context, std::span<const slot_t> staticSlots,
    Type *staticType) {
    std::vector<GraphId> refs;
    if (!staticType || staticType->code() != TypeCode::Tuple) {
        return refs;
    }

    auto *tupleType = static_cast<TupleType *>(staticType);
    ObjectSet visited;
    for (size_t i = 1; i < staticSlots.size() && i < tupleType->size(); ++i) {
        if (!shouldTraverseStaticSlotType(tupleType->typeAt(i))) {
            continue;
        }
        visitGraphsInStaticSlot(
            context,
            staticSlots[i],
            tupleType->typeAt(i),
            [&](GraphId graph, const Program *) { pushUniqueGraph(refs, graph); },
            visited);
    }
    return refs;
}

void visitDraftGraphs(
    const camel::core::context::context_ptr_t &context, const GraphDraft &draft,
    const GraphVisitor &visitor) {
    for (GraphId graph : draft.dependencies()) {
        visitor(graph, nullptr);
    }
    for (GraphId graph : draft.subGraphs()) {
        visitor(graph, nullptr);
    }
    for (GraphId graph : draft.staticGraphRefs()) {
        visitor(graph, nullptr);
    }
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id)) {
            continue;
        }
        const DraftNodeHeader *header = draft.header(id);
        if (header && header->kind == GCNodeKind::Loop) {
            const GCLoopBody loop = draft.loopGraphsOf(id);
            if (isValid(loop.condGraph)) {
                visitor(loop.condGraph, nullptr);
            }
            if (isValid(loop.bodyGraph)) {
                visitor(loop.bodyGraph, nullptr);
            }
            continue;
        }
        if (!header || header->kind != GCNodeKind::Func) {
            continue;
        }
        visitor(draft.calleeIdOf(id), nullptr);
    }

    const auto staticSlotTypes = draft.staticSlotTypes();
    if (!staticSlotTypes.empty()) {
        ObjectSet visited;
        for (size_t i = 1; i < draft.staticSlots().size(); ++i) {
            camel::core::type::Type *slotType =
                i < staticSlotTypes.size() ? staticSlotTypes[i] : nullptr;
            if (!shouldTraverseStaticSlotType(slotType)) {
                continue;
            }
            visitGraphsInStaticSlot(context, draft.staticSlots()[i], slotType, visitor, visited);
        }
    }
}

std::vector<GraphId> collectDraftDependencyGraphs(const GraphDraft &draft) {
    std::vector<GraphId> dependencies;
    for (GraphId graph : draft.dependencies()) {
        pushUniqueGraph(dependencies, graph);
    }
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id)) {
            continue;
        }
        const DraftNodeHeader *header = draft.header(id);
        if (header && header->kind == GCNodeKind::Loop) {
            // A LOOP node calls its two graphs each round: they are callees too.
            const GCLoopBody loop = draft.loopGraphsOf(id);
            pushUniqueGraph(dependencies, loop.condGraph);
            pushUniqueGraph(dependencies, loop.bodyGraph);
            continue;
        }
        if (!header || header->kind != GCNodeKind::Func) {
            continue;
        }
        pushUniqueGraph(dependencies, draft.calleeIdOf(id));
    }
    return dependencies;
}

/// The type of the draft's static area. A commit asks twice for every graph of the closure
/// (for the size, then to encode), and most of those graphs are not changed by the rewrite:
/// when the members are the ones the graph the draft came from already has, that graph's type
/// is the answer, and nothing is added to the permanent space, which never releases.
TupleType *draftStaticDataType(const GraphDraft &draft, const GCGraph *fallback) {
    auto *known          = fallback ? const_cast<TupleType *>(fallback->staticDataType()) : nullptr;
    const auto slotTypes = draft.staticSlotTypes();
    if (slotTypes.empty()) {
        return known;
    }
    if (known && known->size() == slotTypes.size()) {
        bool same = true;
        for (size_t i = 0; same && i < slotTypes.size(); ++i) {
            same = known->typeAt(i) == slotTypes[i];
        }
        if (same) {
            return known;
        }
    }
    return TupleType::layout(slotTypes);
}

std::vector<GraphId> collectDraftStaticGraphRefs(
    const camel::core::context::context_ptr_t &context, const GraphDraft &draft,
    const TupleType *staticDataType) {
    std::vector<GraphId> staticGraphRefs;
    for (GraphId graph : draft.staticGraphRefs()) {
        pushUniqueGraph(staticGraphRefs, graph);
    }
    for (GraphId graph : collectStaticGraphRefs(
             context,
             draft.staticSlots(),
             const_cast<TupleType *>(staticDataType))) {
        pushUniqueGraph(staticGraphRefs, graph);
    }
    return staticGraphRefs;
}

uint64_t draftGraphOrigin(
    const camel::core::context::context_ptr_t &context, const RuntimeDraftIdentity &identity,
    const GraphDraft &draft);

// Encodes `draft` into `target` (`bytes` reserved), rewriting every graph reference through
// `rewritten`, which must cover all graphs the draft refers to.
GCGraph *encodeDraftGraph(
    const camel::core::context::context_ptr_t &context, const RuntimeDraftIdentity &identity,
    const GraphDraft &draft, const Program *program, GCGraph *target, size_t bytes) {
    TupleType *staticDataType = draftStaticDataType(draft, identity.sourceGraph);

    const std::vector<GraphId> dependencies = collectDraftDependencyGraphs(draft);
    const std::vector<GraphId> subGraphs(draft.subGraphs().begin(), draft.subGraphs().end());
    const std::vector<GraphId> staticGraphRefs =
        collectDraftStaticGraphRefs(context, draft, staticDataType);
    const std::vector<GraphId> ruleGraphs(draft.ruleGraphs().begin(), draft.ruleGraphs().end());

    std::vector<slot_t> staticSlots(draft.staticSlots().begin(), draft.staticSlots().end());
    if (auto *tupleType = staticDataType) {
        if (staticSlots.size() < tupleType->size()) {
            staticSlots.resize(tupleType->size(), NullSlot);
        }
        ObjectCache objectCache;
        for (size_t i = 1; i < tupleType->size(); ++i) {
            if (!shouldTraverseStaticSlotType(tupleType->typeAt(i))) {
                continue;
            }
            staticSlots[i] = cloneStaticSlot(
                context,
                staticSlots[i],
                tupleType->typeAt(i),
                program,
                objectCache);
        }
    }

    const auto payloadShape = describeDraftNativePayload(draft);
    // The node chunks are blocks of their own: the graph space can run out while they are made.
    try {
    return GCGraphBuildAccess::constructInPlace(
        target,
        bytes,
        draft.id(),
        GCGraphDerivation{draft.typeSpecializationRoot(), draft.flags()},
        [&] {
            auto *record = createGraphDebugRecord(identity.mangledName, identity.name);
            recordDraftDebugData(record, draft);
            return record;
        }(),
        draft.funcType(),
        draft.frameSlotTypes(),
        staticDataType,
        draft.closureType(),
        draft.outerGraphId(),
        dependencies,
        subGraphs,
        staticGraphRefs,
        ruleGraphs,
        payloadShape,
        [&](GCGraphPayloadArena &payload) { emitDraftNativePayload(draft, payload); },
        staticSlots);
    } catch (const mm::MemoryExhausted &e) {
        throw GraphSpaceExhausted(
            e,
            bytes,
            identity.name,
            draftGraphOrigin(context, identity, draft));
    }
}

size_t draftGraphBytes(
    const camel::core::context::context_ptr_t &context, const GraphDraft &draft,
    const GCGraph *fallback) {
    TupleType *staticDataType = draftStaticDataType(draft, fallback);
    const std::vector<GraphId> dependencyPlaceholders(
        collectDraftDependencyGraphs(draft).size(),
        kInvalidGraphId);
    const std::vector<GraphId> subGraphPlaceholders(draft.subGraphs().size(), kInvalidGraphId);
    const std::vector<GraphId> staticRefPlaceholders(
        collectDraftStaticGraphRefs(context, draft, staticDataType).size(),
        kInvalidGraphId);
    const std::vector<GraphId> ruleGraphPlaceholders(
        draft.ruleGraphs().size(),
        kInvalidGraphId);
    std::vector<slot_t> staticSlots(draft.staticSlots().begin(), draft.staticSlots().end());
    if (staticDataType && staticSlots.size() < staticDataType->size()) {
        staticSlots.resize(staticDataType->size(), NullSlot);
    }
    return GCGraphBuildAccess::requiredBytes(
        dependencyPlaceholders,
        subGraphPlaceholders,
        staticRefPlaceholders,
        ruleGraphPlaceholders,
        describeDraftNativePayload(draft),
        staticSlots,
        draft.frameSlotTypes());
}

/// The source position a failure to encode the graph `identity` is reported at, which only
/// the session knows: the definition of the function the graph was compiled from; a graph a
/// rewrite created (a specialization, a copy) has none and is reported at its first node that
/// has one.
uint64_t draftGraphOrigin(
    const camel::core::context::context_ptr_t &context, const RuntimeDraftIdentity &identity,
    const GraphDraft &draft) {
    uint64_t origin = 0;
    if (const auto source = context ? context->sourceContext() : nullptr) {
        origin = source->debugMap().graphOrigin(draft.id());
    }
    return origin != 0 ? origin : draft.firstNodeOrigin();
}

/// Room for the runtime graph of the draft `identity`. A graph space at its limit is reported
/// with the graph's name and source position.
GCGraph *allocateGraph(
    const camel::core::context::context_ptr_t &context, const RuntimeDraftIdentity &identity,
    const GraphDraft &draft, size_t bytes) {
    try {
        return reinterpret_cast<GCGraph *>(mm::graphSpace().alloc(bytes, alignof(GCGraph)));
    } catch (const mm::MemoryExhausted &e) {
        throw GraphSpaceExhausted(
            e,
            bytes,
            identity.name,
            draftGraphOrigin(context, identity, draft));
    }
}

/// A static value of `before` and the value the commit put in its place in `after` mean the
/// same: the same slot, or (a function value, a tuple of them) the copy the commit made, over the
/// same graph. Anything else that differs counts as a change.
bool sameStaticValue(slot_t before, slot_t after, Type *type) {
    if (before == after) {
        return true;
    }
    if (type == nullptr || before == NullSlot || after == NullSlot) {
        return false;
    }
    if (type->code() == TypeCode::Function) {
        auto *a = fromSlot<::Function *>(before);
        auto *b = fromSlot<::Function *>(after);
        if (a->graphId() != b->graphId() || a->tupleType() != b->tupleType()) {
            return false;
        }
        const ::Tuple *ca = a->tuple(), *cb = b->tuple();
        if (ca == nullptr || cb == nullptr) {
            return ca == cb;
        }
        for (size_t i = 0; i < a->tupleType()->size(); ++i) {
            if (!sameStaticValue(
                    ca->get<slot_t>(i),
                    cb->get<slot_t>(i),
                    a->tupleType()->typeAt(i))) {
                return false;
            }
        }
        return true;
    }
    if (type->code() == TypeCode::Tuple) {
        auto *tupleType = static_cast<TupleType *>(type);
        auto *a         = fromSlot<::Tuple *>(before);
        auto *b         = fromSlot<::Tuple *>(after);
        for (size_t i = 0; i < tupleType->size(); ++i) {
            if (!sameStaticValue(a->get<slot_t>(i), b->get<slot_t>(i), tupleType->typeAt(i))) {
                return false;
            }
        }
        return true;
    }
    return false;
}

/**
 * The commit encoded `after` from the draft of `before`, and they have the same content: the
 * same records, edges, payloads, ports, anchors, permissions, attributes, frame and static
 * layout, the same
 * static values and the same graphs referred to. Source positions do not
 * count. A pass that asked to edit a graph and changed nothing leaves it the same graph to the
 * fact store (GCGraph::revision). Conservative: what it cannot compare counts as a change.
 */
bool sameContent(const GCGraph &before, const GCGraph &after) {
    const GCGraphNativePayload *a = before.nodePayload();
    const GCGraphNativePayload *b = after.nodePayload();
    if (a == nullptr || b == nullptr) {
        return false;
    }
    const auto sameArray = [](const auto *x, const auto *y, size_t n) {
        return n == 0 || (x != nullptr && y != nullptr && std::memcmp(x, y, n * sizeof(*x)) == 0);
    };
    // A node record's own typeId counts by its canonical id: two records whose types are one type
    // under `equals` (the names in a type's key erased) are the same content, which is the whole
    // point of a type having no identity but its structure.
    const auto &table      = camel::core::type::TypeTable::instance();
    const auto sameNodes = [&sameArray, &table](
                               const GCNode *x, const GCNode *y, size_t n) {
        if (n == 0) {
            return true;
        }
        if (x == nullptr || y == nullptr) {
            return false;
        }
        constexpr size_t kTypeAt    = offsetof(GCNode, typeId);
        constexpr size_t kTypeBytes = sizeof(GCNode::typeId);
        for (size_t i = 0; i < n; ++i) {
            const auto *rx = reinterpret_cast<const unsigned char *>(&x[i]);
            const auto *ry = reinterpret_cast<const unsigned char *>(&y[i]);
            if (std::memcmp(rx, ry, kTypeAt) != 0 ||
                table.canonicalIdOf(x[i].typeId) != table.canonicalIdOf(y[i].typeId) ||
                std::memcmp(
                    rx + kTypeAt + kTypeBytes, ry + kTypeAt + kTypeBytes,
                    sizeof(GCNode) - kTypeAt - kTypeBytes) != 0) {
                return false;
            }
        }
        return true;
    };
    if (a->nodeCount != b->nodeCount || a->edgeCount != b->edgeCount ||
        a->payloadWordCount != b->payloadWordCount || a->normPortCount != b->normPortCount ||
        a->withPortCount != b->withPortCount || a->closureCount != b->closureCount ||
        a->entryNode != b->entryNode ||
        a->exitNode != b->exitNode || a->outputNode != b->outputNode ||
        a->returnNode != b->returnNode || a->returnKind != b->returnKind ||
        table.canonicalId(before.funcType()) != table.canonicalId(after.funcType()) ||
        table.canonicalId(before.closureType()) != table.canonicalId(after.closureType()) ||
        table.canonicalId(before.staticDataType()) != table.canonicalId(after.staticDataType()) ||
        before.hasNodePermissions() != after.hasNodePermissions() ||
        before.hasNodeAttrs() != after.hasNodeAttrs() || a->hasRegions != b->hasRegions) {
        return false;
    }
    // The columns, chunk by chunk: a chunk both graphs hold is the same chunk.
    for (gc_cnt_t c = 0; c < a->chunkCount; ++c) {
        const GCNodeChunk &x = *a->chunks[c];
        const GCNodeChunk &y = *b->chunks[c];
        if (&x == &y) {
            continue;
        }
        if (x.nodeCount != y.nodeCount || x.edgeCount != y.edgeCount ||
            !sameNodes(x.nodes(), y.nodes(), x.nodeCount) ||
            !sameArray(x.edges, y.edges, x.edgeCount) ||
            (a->hasPermissions && !sameArray(x.permissions, y.permissions, x.nodeCount)) ||
            (a->hasAttrs && !sameArray(x.attrs, y.attrs, x.nodeCount)) ||
            (a->hasRegions && !sameArray(x.regions, y.regions, x.nodeCount))) {
            return false;
        }
    }
    // The equivalences between nodes are content: a saturated graph is not the graph it was.
    if (a->writingCount != b->writingCount || !sameArray(a->writings, b->writings, a->writingCount)) {
        return false;
    }
    if (a->regionCount != b->regionCount || a->regionPortCount != b->regionPortCount ||
        !sameArray(a->regionTable, b->regionTable, a->regionCount) ||
        !sameArray(a->regionPorts, b->regionPorts, a->regionPortCount)) {
        return false;
    }
    if (!sameArray(a->normPorts, b->normPorts, a->normPortCount) ||
        !sameArray(a->withPorts, b->withPorts, a->withPortCount) ||
        !sameArray(a->closure, b->closure, a->closureCount)) {
        return false;
    }
    // Payloads, node by node: an OPER's cached implementation pointer is not content.
    for (gc_node_ref_t ref = 0; ref < before.nodeCount(); ++ref) {
        if (before.node(ref)->kind == GCNodeKind::Oper) {
            if (before.nodeBodyAs<GCOperBody>(ref)->oper !=
                after.nodeBodyAs<GCOperBody>(ref)->oper) {
                return false;
            }
            continue;
        }
        const auto pa = before.nodePayloadBytes(ref), pb = after.nodePayloadBytes(ref);
        if (pa.size() != pb.size() || !sameArray(pa.data(), pb.data(), pa.size())) {
            return false;
        }
    }
    // The frame: the same slot types.
    const TupleType *fa = before.runtimeDataType(), *fb = after.runtimeDataType();
    if ((fa == nullptr) != (fb == nullptr) || (fa && fa->size() != fb->size())) {
        return false;
    }
    for (size_t i = 0; fa && i < fa->size(); ++i) {
        // By canonical id: two frames whose slot types are one type are the same frame.
        if (table.canonicalId(fa->typeAt(i)) != table.canonicalId(fb->typeAt(i))) {
            return false;
        }
    }
    // The graphs referred to, with the same roles.
    if (before.refs().size() != after.refs().size() ||
        before.outerGraphId() != after.outerGraphId()) {
        return false;
    }
    for (size_t i = 0; i < before.refs().size(); ++i) {
        if (before.refs()[i] != after.refs()[i] || before.refRoles(i) != after.refRoles(i)) {
            return false;
        }
    }
    // The static values.
    if (const TupleType *statics = before.staticDataType()) {
        const auto sa = before.staticSlots(), sb = after.staticSlots();
        if (sa.size() != sb.size()) {
            return false;
        }
        for (size_t i = 1; i < sa.size() && i < statics->size(); ++i) {
            if (!sameStaticValue(sa[i], sb[i], statics->typeAt(i))) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

#ifndef NDEBUG
// ---------------------------------------------------------------------------------------------
// The Debug-only safepoint draft-root check.
//
// The red line: while a session holds a draft open, every safepoint the session
// reaches must keep the draft's static values relocatable, or a collection at that safepoint
// leaves the raw slots in the draft pointing at where the objects were. The root registration
// itself stays where it is: the session's RootScope registers an external tracer, and a
// DraftWritings's EngineRootScope covers the one draft it is made for. This section keeps only
// the record the check needs: which sessions are alive, which drafts each has open, and which of
// the existing scopes cover them. It registers no root of its own, so a path cannot satisfy the
// check by registering somewhere else than the collector.
//
// The check runs at the start of every safepoint (gen.h, gen.cpp), not only at a collecting one,
// so a path that forgets a scope is reported at the first safepoint it reaches rather than at the
// first collection: in a Debug build a test fails even when nothing would have moved yet.

namespace {

std::mutex &draftRootsMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<DraftRootsDebug *> &draftRootsRegistry() {
    static std::vector<DraftRootsDebug *> registry;
    return registry;
}

/// Sessions with at least one open draft; the check returns without the lock while it is zero.
std::atomic<size_t> &draftRootSessionsWithDrafts() {
    static std::atomic<size_t> count{0};
    return count;
}

size_t nextDraftRootSessionId() {
    static std::atomic<size_t> next{0};
    return next.fetch_add(1, std::memory_order_relaxed) + 1;
}

void noteDraftOpened(DraftRootsDebug &state, const GraphDraft *draft) {
    std::lock_guard<std::mutex> lock(draftRootsMutex());
    if (state.drafts.empty()) {
        draftRootSessionsWithDrafts().fetch_add(1, std::memory_order_relaxed);
    }
    state.drafts.push_back(DraftRootsDebug::Entry{.draft = draft, .roots = 0});
}

void noteDraftClosed(DraftRootsDebug &state, const GraphDraft *draft) {
    std::lock_guard<std::mutex> lock(draftRootsMutex());
    if (state.drafts.empty()) {
        return;
    }
    std::erase_if(state.drafts, [draft](const DraftRootsDebug::Entry &entry) {
        return entry.draft == draft;
    });
    if (state.drafts.empty()) {
        draftRootSessionsWithDrafts().fetch_sub(1, std::memory_order_relaxed);
    }
}

void noteDraftsCleared(DraftRootsDebug &state) {
    std::lock_guard<std::mutex> lock(draftRootsMutex());
    if (state.drafts.empty()) {
        return;
    }
    state.drafts.clear();
    draftRootSessionsWithDrafts().fetch_sub(1, std::memory_order_relaxed);
}

void noteRootScopeOpened(DraftRootsDebug &state) {
    std::lock_guard<std::mutex> lock(draftRootsMutex());
    ++state.sessionRoots;
}

void noteRootScopeClosed(DraftRootsDebug &state) {
    std::lock_guard<std::mutex> lock(draftRootsMutex());
    ASSERT(state.sessionRoots > 0, "A session RootScope was closed more often than it was opened.");
    --state.sessionRoots;
}

/// The first draft of `state` no scope covers, and what it would lose, or an empty string when
/// every draft is covered. Called with the registry lock held; it reads the draft it names.
std::string describeUncoveredDraft(const DraftRootsDebug &state, std::string_view reason) {
    const DraftRootsDebug::Entry *uncovered = nullptr;
    for (const DraftRootsDebug::Entry &entry : state.drafts) {
        if (entry.roots == 0) {
            uncovered = &entry;
            break;
        }
    }
    if (uncovered == nullptr) {
        return {};
    }
    const GraphDraft &draft     = *uncovered->draft;
    const GCGraph *source       = draft.baseGraph();
    const std::string graphName = source != nullptr ? source->name() : std::string{"<built>"};
    const auto slots            = draft.staticSlots();
    const auto types            = draft.staticSlotTypes();
    const size_t slotCount      = std::min(slots.size(), types.size());
    // Slot 0 is the static tuple itself (see traceStaticSlots); the values start at 1.
    std::string holds;
    for (size_t i = 1; i < slotCount; ++i) {
        if (!shouldTraverseStaticSlotType(types[i]) || slots[i] == NullSlot) {
            continue;
        }
        holds =
            std::format("its static slot {} holds a value of type '{}'", i, types[i]->toString());
        break;
    }
    if (holds.empty()) {
        holds = "it holds no GC-traced static value yet";
    }
    return std::format(
        "GC draft-root invariant violated at safepoint '{}': {} has {} open draft(s) and no "
        "RootScope covers the session; draft '{}' (graph {}), at draft address {}, is not rooted: "
        "{}. A collection at a safepoint would leave that raw slot pointing at where the object "
        "was. Open RuntimeGraphDraftSession::RootScope before the first draft is decoded and keep "
        "it until the commit reads the drafts again (modules/rewrite/module.cpp); a DraftWritings "
        "EngineRootScope covers only the draft it is made for.",
        reason.empty() ? std::string_view{"safepoint"} : reason,
        state.label,
        state.drafts.size(),
        graphName,
        toValue(draft.id()),
        static_cast<const void *>(&draft),
        holds);
}

void checkDraftRootsAtSafepoint(std::string_view reason) {
    if (draftRootSessionsWithDrafts().load(std::memory_order_relaxed) == 0) {
        return;
    }
    std::string message;
    {
        std::lock_guard<std::mutex> lock(draftRootsMutex());
        for (const DraftRootsDebug *state : draftRootsRegistry()) {
            if (state->drafts.empty() || state->sessionRoots > 0) {
                continue;
            }
            message = describeUncoveredDraft(*state, reason);
            if (!message.empty()) {
                break;
            }
        }
    }
    if (!message.empty()) {
        // An assertion, not an exception: the rewrite kernels catch std::exception and turn it
        // into an error value, and a red-line violation must not become an ordinary rule error.
        ASSERT(false, message);
    }
}

} // namespace

DraftRootsDebug::DraftRootsDebug(std::string label) : label(std::move(label)) {
    std::lock_guard<std::mutex> lock(draftRootsMutex());
    draftRootsRegistry().push_back(this);
}

DraftRootsDebug::~DraftRootsDebug() {
    std::lock_guard<std::mutex> lock(draftRootsMutex());
    std::erase_if(draftRootsRegistry(), [this](const DraftRootsDebug *entry) {
        return entry == this;
    });
    if (!drafts.empty()) {
        draftRootSessionsWithDrafts().fetch_sub(1, std::memory_order_relaxed);
    }
}

void debugDraftRootOpened(const GraphDraft &draft) {
    std::lock_guard<std::mutex> lock(draftRootsMutex());
    for (DraftRootsDebug *state : draftRootsRegistry()) {
        for (DraftRootsDebug::Entry &entry : state->drafts) {
            if (entry.draft == &draft) {
                ++entry.roots;
                return;
            }
        }
    }
}

void debugDraftRootClosed(const GraphDraft &draft) {
    std::lock_guard<std::mutex> lock(draftRootsMutex());
    for (DraftRootsDebug *state : draftRootsRegistry()) {
        for (DraftRootsDebug::Entry &entry : state->drafts) {
            if (entry.draft == &draft) {
                if (entry.roots > 0) {
                    --entry.roots;
                }
                return;
            }
        }
    }
}
#endif

size_t RuntimeSpecializationKeyHasher::operator()(const RuntimeSpecializationKey &key) const {
    size_t seed = 0;
    hashCombine(seed, key.baseGraph);
    for (const RuntimeSpecializationBindingKey &binding : key.bindings) {
        hashCombine(seed, static_cast<uint8_t>(binding.kind));
        hashCombine(seed, binding.index);
        hashCombine(seed, binding.value);
        // The type enters as its canonical id, the same relation the key compares by
        // (specializations.h): hashing the object would put two equal keys in two buckets.
        hashCombine(
            seed, camel::core::type::TypeTable::instance().canonicalId(binding.type));
        hashCombine(seed, binding.runtimeFlags);
    }
    return seed;
}

RuntimeGraphDraftSession::RuntimeGraphDraftSession(
    const camel::core::context::context_ptr_t &context, const Program *program,
    std::source_location location)
    : RuntimeGraphDraftSession(context, program, program ? program->root() : nullptr, location) {}

RuntimeGraphDraftSession::RuntimeGraphDraftSession(
    const camel::core::context::context_ptr_t &context, const Program *program,
    GCGraph *runtimeRoot, [[maybe_unused]] std::source_location location)
    : context_(context), program_(program), runtimeRoot_(runtimeRoot) {
    ASSERT(context_ != nullptr, "Runtime graph draft session requires a valid context.");
    ASSERT(program_ != nullptr, "Runtime graph draft session requires a program.");
    ASSERT(runtimeRoot_ != nullptr, "Runtime graph draft session requires a runtime root graph.");
    // The session may replace the context's program only while the program it rewrites is one of
    // that program's (mayPublish_ in the header): remembered here, because a later commit cannot
    // tell an overtaken session from one that was never over the context's program.
    mayPublish_ = context_->currentRuntimeProgram() == program_;
    work_ = std::make_unique<Program>(*program_);
#ifndef NDEBUG
    // The safepoint check finds this session through DraftRootsDebug and reports the call site
    // when a draft is left unrooted. The session is a cell of its own so that a move of the
    // session (macro.cpp) keeps the drafts and this record together.
    debugRoots_ = std::make_unique<DraftRootsDebug>(std::format(
        "session #{} opened at {}:{} over program root '{}'",
        nextDraftRootSessionId(),
        location.file_name(),
        location.line(),
        runtimeRoot_->name()));
    // Installed once, on the first session: a safepoint before there is a session has no draft
    // to check. A Release build has neither the observer nor the call (debug_hook.h).
    static const bool draftRootCheckInstalled = [] {
        mm::setSafepointObserver(&checkDraftRootsAtSafepoint);
        return true;
    }();
    (void)draftRootCheckInstalled;
#endif
}

void RuntimeGraphDraftSession::adoptProgram(const Program &program) const {
    work_->adoptMissing(program);
}

void RuntimeGraphDraftSession::adoptGraph(GCGraph *graph) const {
    if (graph == nullptr) {
        return;
    }
    // A graph the session encoded itself (a specialization) is a version of the program that no
    // program binds yet: the drafts reach it through the work table, which the collector does not
    // trace. Telling the manager about it lets the collector free it once the pass is over when
    // the commit did not bind it (a commit that replaced it with a copy of its own).
    context_->noteUnboundRuntimeGraph(graph);
    if (work_->graph(graph->id()) == nullptr) {
        work_->bind(graph->id(), graph);
    }
}

bool RuntimeGraphDraftSession::hasDraft(const GCGraph *graph) const {
    return graph != nullptr && drafts_.contains(const_cast<GCGraph *>(graph));
}

GraphDraft *RuntimeGraphDraftSession::tryDraft(const GCGraph *graph) {
    if (!graph) {
        return nullptr;
    }
    if (auto it = drafts_.find(const_cast<GCGraph *>(graph)); it != drafts_.end()) {
        return it->second->draft.get();
    }
    return nullptr;
}

const GraphDraft *RuntimeGraphDraftSession::tryDraft(const GCGraph *graph) const {
    if (!graph) {
        return nullptr;
    }
    if (auto it = drafts_.find(const_cast<GCGraph *>(graph)); it != drafts_.end()) {
        return it->second->draft.get();
    }
    return nullptr;
}

GraphDraft &RuntimeGraphDraftSession::ensureDraft(GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime graph draft decode requires a non-null graph.");
    auto [it, inserted] = drafts_.try_emplace(graph);
    if (inserted) {
        auto entry      = std::make_unique<DraftEntry>();
        entry->identity = makeRuntimeDraftIdentity(graph);
        CAMEL_LOG_INFO_S(
            "DraftSession",
            "Decoding runtime graph '{}' into editable draft.",
            graph->name());
        entry->draft = GraphDraft::decode(*work_, graph);
        entry->draft->setRefsEpoch(refsEpoch_.get());
        CAMEL_LOG_INFO_S(
            "DraftSession",
            "Decoded runtime graph '{}' into editable draft.",
            graph->name());
        it->second = std::move(entry);
        const size_t index = toValue(graph->id());
        if (index >= draftedIds_.size()) {
            draftedIds_.resize(index + 1, 0);
        }
        draftedIds_[index] = 1;
#ifndef NDEBUG
        noteDraftOpened(*debugRoots_, it->second->draft.get());
#endif
    }
    return *it->second->draft;
}

GraphDraft &RuntimeGraphDraftSession::edit(GCGraph *graph) {
    edited_.insert(graph);
    return ensureDraft(graph);
}

GraphDraft &RuntimeGraphDraftSession::rootDraft() { return edit(runtimeRoot_); }

RuntimeGraphDraftSession::RootScope::RootScope(RuntimeGraphDraftSession &session)
    : session_(session) {
    mm::autoSpace().registerExternalRootTracer(
        this,
        "runtime.draftRoots",
        [this](const camel::core::rtdata::Object::RefRelocator &relocate) {
            session_.traceStaticSlots(relocate, nullptr, "Draft");
        });
#ifndef NDEBUG
    ASSERT(
        session_.debugRoots_ != nullptr,
        "A session RootScope requires the session's Debug record.");
    noteRootScopeOpened(*session_.debugRoots_);
#endif
}

RuntimeGraphDraftSession::RootScope::~RootScope() {
#ifndef NDEBUG
    noteRootScopeClosed(*session_.debugRoots_);
#endif
    mm::autoSpace().unregisterExternalRootTracer(this);
}

void RuntimeGraphDraftSession::traceStaticSlots(
    const camel::core::rtdata::Object::RefRelocator &relocate, const Type *ownerType,
    std::string_view ownerKind) {
    // The objects the collector moved, old slot to new: a specialization is cached under the
    // value it binds, and for an object that is the slot (its address), which must follow the
    // object. Without it a key names whatever is at the old address next.
    std::vector<std::pair<slot_t, slot_t>> moves;
    for (auto &[graph, entry] : drafts_) {
        GraphDraft &draft        = *entry->draft;
        const auto slots         = draft.staticSlots();
        const auto types         = draft.staticSlotTypes();
        const size_t slotCount   = std::min(slots.size(), types.size());
        // Slot 0 is the static tuple itself (appendStaticSlot reserves it as Void); the values
        // start at 1.
        for (size_t i = 1; i < slotCount; ++i) {
            Type *type = types[i];
            if (!shouldTraverseStaticSlotType(type) || slots[i] == NullSlot) {
                continue;
            }
            Object *value = fromSlot<Object *>(slots[i]);
            if (value == nullptr) {
                continue;
            }
            Object *moved = relocate(
                value,
                type,
                camel::core::rtdata::RefTraceInfo{
                    .owner     = nullptr,
                    .ownerType = ownerType,
                    .slotType  = type,
                    .ownerKind = ownerKind,
                    .slotName  = "draft",
                    .slotIndex = i,
                });
            if (moved != value) {
                moves.emplace_back(slots[i], toSlot<Object *>(moved));
            }
            draft.relocateStaticSlot(i, toSlot<Object *>(moved));
        }
    }
    if (moves.empty() || specializationCache_.empty()) {
        return;
    }
    std::unordered_map<RuntimeSpecializationKey, GCGraph *, RuntimeSpecializationKeyHasher> kept;
    kept.reserve(specializationCache_.size());
    for (auto &[key, graph] : specializationCache_) {
        RuntimeSpecializationKey retargeted = key;
        for (RuntimeSpecializationBindingKey &binding : retargeted.bindings) {
            if (binding.value == NullSlot || binding.type == nullptr || !binding.type->isGCTraced()) {
                continue;
            }
            for (const auto &[from, to] : moves) {
                if (binding.value == from) {
                    binding.value = to;
                    break;
                }
            }
        }
        kept.insert_or_assign(std::move(retargeted), graph);
    }
    specializationCache_ = std::move(kept);
}

bool RuntimeGraphDraftSession::specializationLivesInProgram(
    const RuntimeSpecializationKey &key) const {
    if (key.baseGraph == nullptr) {
        return false;
    }
    for (const RuntimeSpecializationBindingKey &binding : key.bindings) {
        // A type-only binding names a type (permanent, never moved); a value binding is a value
        // in the language's sense only when the type does not hold a GC-managed object.
        if (binding.value == NullSlot) {
            continue;
        }
        if (binding.type == nullptr || binding.type->isGCTraced()) {
            return false;
        }
    }
    return true;
}

ProgramSpecializationKey
RuntimeGraphDraftSession::programKeyOf(const RuntimeSpecializationKey &key) const {
    return ProgramSpecializationKey{
        .baseGraph    = key.baseGraph->id(),
        .baseRevision = key.baseGraph->revision(),
        .bindings     = key.bindings,
    };
}

GCGraph *RuntimeGraphDraftSession::findSpecialization(const RuntimeSpecializationKey &key) const {
    if (auto it = specializationCache_.find(key); it != specializationCache_.end()) {
        return it->second;
    }
    // What a session over the program the session before it produced already made (the export and
    // std::opt simplify in rounds): the program keeps the specializations of its graphs.
    if (!specializationLivesInProgram(key)) {
        return nullptr;
    }
    const ProgramSpecializationKey programKey = programKeyOf(key);
    GraphId specializedId                     = kInvalidGraphId;
    if (!work_->specializations().find(programKey, specializedId)) {
        return nullptr;
    }
    GCGraph *specialized = work_->graph(specializedId);
    if (specialized == nullptr) {
        return nullptr; // the program no longer holds that graph: make the specialization again
    }
    GCGraph *base = work_->graph(programKey.baseGraph);
    if (base == nullptr || base != key.baseGraph || base->revision() != programKey.baseRevision) {
        return nullptr; // the callee has another version now; the entry was made for that one
    }
    // The commit binds the graphs the edited graph reaches, so a specialization whose callees the
    // program does not hold could not be used (and would make the commit fail its validation).
    for (const GraphId ref : specialized->refs()) {
        if (work_->graph(ref) == nullptr) {
            return nullptr;
        }
    }

    return specialized;
}

void RuntimeGraphDraftSession::rememberSpecialization(
    RuntimeSpecializationKey key, GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime specialization cache cannot store a null graph.");
    if (specializationLivesInProgram(key)) {
        work_->specializations().remember(programKeyOf(key), graph->id());
    }
    specializationCache_.insert_or_assign(std::move(key), graph);
}

uint64_t RuntimeGraphDraftSession::nextSpecializationNonce() {
    return work_->specializations().nextSpecializationNonce();
}

uint64_t RuntimeGraphDraftSession::nextLiftNonce() {
    return work_->specializations().nextLiftNonce();
}

size_t RuntimeGraphDraftSession::typeSpecializationCount(GraphId root) {
    if (!typeSpecializationCountsReady_) {
        // Earlier sessions (and earlier passes) leave their specializations in the program; the
        // bound is on what the program holds, so it is read from the program, not remembered.
        typeSpecializationCountsReady_ = true;
        for (const GCGraph *graph : collectCommitClosure()) {
            if (isValid(graph->typeSpecializationRoot())) {
                ++typeSpecializationCounts_[graph->typeSpecializationRoot()];
            }
        }
    }
    const auto it = typeSpecializationCounts_.find(root);
    return it == typeSpecializationCounts_.end() ? 0 : it->second;
}

void RuntimeGraphDraftSession::noteTypeSpecialization(GraphId root) {
    (void)typeSpecializationCount(root);
    ++typeSpecializationCounts_[root];
}

std::vector<GCGraph *> RuntimeGraphDraftSession::collectCommitClosure() const {
    std::vector<GCGraph *> closure;
    if (!runtimeRoot_) {
        return closure;
    }
    // The closure depends on what the drafts refer to and on the table the ids are read in:
    // while neither changed it is the one collected last.
    if (closureCache_.valid && closureCache_.epoch == *refsEpoch_ &&
        closureCache_.table == work_->version() && closureCache_.drafts == drafts_.size() &&
        closureCache_.root == runtimeRoot_) {
        return closureCache_.graphs;
    }

    // Breadth first: the closure is the graphs in the order they are first reached. The queue is
    // of ids (a table holds one version of each), marked by id.
    std::vector<uint8_t> visited(work_->tableSize(), 0);
    std::vector<GraphId> queue;
    const auto reach = [&](GraphId id) {
        const size_t index = toValue(id);
        if (index >= visited.size()) {
            visited.resize(index + 1, 0);
        }
        if (!visited[index]) {
            visited[index] = 1;
            queue.push_back(id);
        }
    };
    // The graph an id names: in the work program, else (a function value of another program)
    // in the program the function was made in, which the session then adopts.
    const auto resolve = [&](GraphId id, const Program *home) -> GCGraph * {
        GCGraph *found = work_->graph(id);
        if (found == nullptr && home != nullptr) {
            adoptProgram(*home);
            found = work_->graph(id);
        }
        return found;
    };
    reach(runtimeRoot_->id());
    for (size_t head = 0; head < queue.size(); ++head) {
        const GraphId id = queue[head];
        GCGraph *graph   = work_->graph(id);
        if (graph == nullptr) {
            graph = runtimeRoot_; // the root before the table knows it
        }
        const size_t index = toValue(id);
        if (index < draftedIds_.size() && draftedIds_[index]) {
            for (const auto &[next, home] : draftRefs(graph)) {
                if (resolve(next, home) != nullptr) {
                    reach(next);
                }
            }
            continue;
        }
        if (index >= frozenKids_.size()) {
            frozenKids_.resize(index + 1);
        }
        FrozenKids &kids = frozenKids_[index];
        if (kids.graph != graph) {
            kids.graph = graph;
            kids.ids.assign(graph->refs().begin(), graph->refs().end());
        }
        for (const GraphId next : kids.ids) {
            if (work_->graph(next) != nullptr) {
                reach(next);
            }
        }
    }
    closure.reserve(queue.size());
    for (const GraphId id : queue) {
        GCGraph *graph = work_->graph(id);
        closure.push_back(graph != nullptr ? graph : runtimeRoot_);
    }

    closureCache_ = ClosureCache{
        .valid  = true,
        .epoch  = *refsEpoch_,
        .table  = work_->version(),
        .drafts = drafts_.size(),
        .root   = runtimeRoot_,
        .graphs = closure,
    };
    return closure;
}

const std::vector<std::pair<GraphId, const Program *>> &
RuntimeGraphDraftSession::draftRefs(GCGraph *graph) const {
    const GraphDraft &draft = *drafts_.at(graph)->draft;
    DraftRefs &refs         = draftRefs_[graph];
    if (!refs.valid || refs.revision != draft.revision()) {
        refs.graphs.clear();
        visitDraftGraphs(context_, draft, [&](GraphId ref, const Program *home) {
            refs.graphs.emplace_back(ref, home);
        });
        refs.revision = draft.revision();
        refs.valid    = true;
    }
    return refs.graphs;
}

std::vector<GCGraph *> RuntimeGraphDraftSession::collectReachableRuntimeGraphs() const {
    return collectCommitClosure();
}

std::vector<GraphId> RuntimeGraphDraftSession::conflicts() const {
    const Program *current = context_->currentRuntimeProgram();
    // A conflict is a lost race for the context's program, and only a session whose commit could
    // replace that program can lose one: it was opened over it (mayPublish_), or it works on the
    // program its owner installed from an earlier commit (current == program_). Then the graph
    // ids name the same versions in both programs, and a version another commit replaced is a
    // real conflict. A session over a private program — the ONNX export's bound copy, a function
    // value's program, a subgraph root, a detached rewrite — holds a program that nobody else
    // writes, whose ids are not versions of the context's program at all: comparing the two
    // reports every graph the copy made or replaced as a conflict. There is nothing to
    // check for those, and a checked commit must not refuse work it owns.
    const bool contextProgram = mayPublish_ || current == program_;
    if (!contextProgram || runtimeRoot_ != program_->root()) {
        return {};
    }
    return conflictsWithProgram(current);
}

std::vector<GraphId> RuntimeGraphDraftSession::conflictsWithProgram(const Program *current) const {
    std::vector<GraphId> differing;
    if (current == nullptr || current == program_) {
        return differing;
    }
    // The graphs the session read or wrote: it reads a graph when it decodes it (a draft) and
    // writes the ones it was handed for editing. A graph the drafts only name (the callee of a
    // FUNC node) is not read: the commit validates the calls of what it encodes against the
    // program it publishes, so a new version of such a graph cannot make the commit wrong.
    const auto compare = [&](GraphId id) {
        if (current->graph(id) != program_->graph(id) &&
            std::find(differing.begin(), differing.end(), id) == differing.end()) {
            differing.push_back(id);
        }
    };
    for (const auto &[graph, entry] : drafts_) {
        compare(graph->id());
    }
    for (GCGraph *graph : edited_) {
        compare(graph->id());
    }
    return differing;
}

std::vector<gc_node_ref_t> RuntimeGraphDraftSession::committedNodeRefs(GCGraph *graph) const {
    const auto found = drafts_.find(graph);
    if (found == drafts_.end() || found->second->draft == nullptr) {
        return {};
    }
    return draftRuntimeNodeRefs(*found->second->draft);
}

const Program *RuntimeGraphDraftSession::commitChecked() {
    std::vector<GraphId> differing = conflicts();
    if (!differing.empty()) {
        std::string names;
        for (GraphId id : differing) {
            const GCGraph *graph = program_->graph(id);
            names += (names.empty() ? "'" : ", '") + (graph ? graph->name() : std::string("?")) + "'";
        }
        const std::string message = std::format(
            "The commit conflicts with a newer program: {} graph(s) the session used have "
            "another version there ({}).",
            differing.size(),
            names);
        throw GraphCommitConflict(std::move(differing), message);
    }
    return commit();
}

const Program *RuntimeGraphDraftSession::commit(bool detached) {
    if (!runtimeRoot_) {
        return nullptr;
    }

    GCGraph *oldRuntimeRoot = runtimeRoot_;

    // A session replaces the context's program only if it is one of that program's: opened over
    // the program the context held then (`mayPublish_`: it may have been overtaken since, and
    // then the commit merges), or working on the program the context holds now (its owner
    // installed what an earlier commit returned). A session over a function value's program (a
    // detached program: the ONNX export, a tile region, a gradient) rewrites that program and
    // hands the commit back to its owner; publishing it would replace the program that is
    // running with somebody's private copy.
    const Program *current = context_->currentRuntimeProgram();
    const bool contextProgram = mayPublish_ || current == program_;
    // A session over the program's root replaces it. A session over another root (a detached
    // program, e.g. a function being exported) only returns the rewritten root: the running
    // program is left as it is.
    const bool installing = !detached && contextProgram && program_->root() == oldRuntimeRoot;
    // A session that is stale and found no conflict merges into the program the context has now
    // instead of forking the history (design 4.3.1): the graph versions it read and wrote are
    // unchanged there, so the commit is made on top of that program.
    const bool merging =
        installing && current != nullptr && current != program_ && conflicts().empty();
    const Program *base = merging ? current : program_;
    if (merging) {
        // The graphs of the session are the versions this session read; every version the
        // program now holds of them is the same content (that is what no conflict means). The
        // drafts are edits of this session, so they are kept as they are, and are re-keyed to
        // the graphs of that program: the graphs this commit decodes from here on (to dissolve
        // gates, to name a new version in a function value) must be decoded from it, not from
        // the version the session started with, or the commit would write back the old content
        // over another session's change.
        std::unordered_map<GCGraph *, std::unique_ptr<DraftEntry>> drafts;
        drafts.reserve(drafts_.size());
        for (auto &[graph, entry] : drafts_) {
            GCGraph *held = current->graph(graph->id());
            drafts.emplace(held != nullptr ? held : graph, std::move(entry));
        }
        drafts_ = std::move(drafts);
        std::unordered_set<GCGraph *> edited;
        edited.reserve(edited_.size());
        for (GCGraph *graph : edited_) {
            GCGraph *held = current->graph(graph->id());
            edited.insert(held != nullptr ? held : graph);
        }
        edited_ = std::move(edited);
        draftRefs_.clear();
        // The graphs the session holds may be versions the context's program does not have: the
        // specializations the session made itself (they live in the work table only), and the
        // graphs of a private program a pass made, which the graphs of this commit refer to.
        // They come along; for every id the context holds, its version is the one this commit
        // works on.
        const std::vector<GCGraph *> own = work_->graphs();
        // The specializations made so far are keyed by graph identity and content, so they stay
        // valid over the program that is there now, which holds the same versions (no conflict).
        RuntimeSpecializationTable ownSpecializations = std::move(work_->specializations());
        *work_                                        = *current;
        work_->specializations().adoptMissingFrom(ownSpecializations);
        for (GCGraph *graph : own) {
            if (work_->graph(graph->id()) == nullptr) {
                work_->bind(graph->id(), graph);
            }
        }
        work_->setRoot(current->root());
        runtimeRoot_        = work_->root();
        program_            = current;
        closureCache_.valid = false;
    }

    const std::vector<GCGraph *> closure = collectCommitClosure();
    ASSERT(!closure.empty(), "Runtime draft commit received an empty closure.");
    for (GCGraph *graph : closure) {
        // Rewrites may leave gates that wait on nothing; they are plain forwarding. A graph
        // nobody has edited since a commit looked at its gates is not decoded again.
        const bool drafted = drafts_.contains(graph);
        if (!drafted && (graph->flags() & kGraphFlagGatesDissolved)) {
            continue;
        }
        if (ensureDraft(graph).dissolveUnorderedGates() > 0) {
            edited_.insert(graph);
        } else if (!drafted) {
            GCGraphBuildAccess::addFlags(graph, kGraphFlagGatesDissolved);
#ifndef NDEBUG
            // Before the draft is destroyed: the Debug record must never name a freed draft.
            noteDraftClosed(*debugRoots_, drafts_.at(graph)->draft.get());
#endif
            drafts_.erase(graph);
            draftedIds_[toValue(graph->id())] = 0;
            draftRefs_.erase(graph);
        }
    }

    // The program after the commit holds every graph of the closure: the version it has now, or
    // the one this commit makes of it. A graph names the others by id, so another graph's new
    // version needs none of its own. A graph gets one when it was edited, when the program did
    // not hold it, and when it holds a function value of a graph that got a new version (a
    // function value names the program it resolves in, which must give that version). A commit
    // that merges starts from the newest program, so that its graph versions are the ones the
    // context holds.
    Program *committed = context_->reserveProgram();
    if (merging) {
        *committed = *current;
    }
    for (GCGraph *graph : closure) {
        if (committed->graph(graph->id()) == nullptr) {
            committed->bind(graph->id(), graph);
        }
    }
    // The program this commit produces carries the specializations the session made (and the ones
    // it found in the program it started from), so the next session over it finds them again.
    committed->specializations() = work_->specializations();
    std::unordered_map<GCGraph *, GCGraph *> fresh; // a graph -> the commit's encoding of it
    std::unordered_map<GCGraph *, size_t> allocatedBytes;
    std::vector<GCGraph *> order; // the graphs encoded, in the order they were allocated
    const auto allocate = [&](GCGraph *graph) {
        ensureDraft(graph);
        const DraftEntry &entry = *drafts_.at(graph);
        const size_t bytes      = draftGraphBytes(context_, *entry.draft, graph);
        GCGraph *copy           = allocateGraph(context_, entry.identity, *entry.draft, bytes);
        fresh.emplace(graph, copy);
        allocatedBytes.emplace(graph, bytes);
        order.push_back(graph);
        committed->bind(graph->id(), copy);
    };
    const auto encode = [&](GCGraph *graph) {
        const DraftEntry &entry = *drafts_.at(graph);
        entry.draft->setFlags(entry.draft->flags() | kGraphFlagGatesDissolved);
        encodeDraftGraph(
            context_,
            entry.identity,
            *entry.draft,
            committed,
            fresh.at(graph),
            allocatedBytes.at(graph));
    };
    const auto heldBefore = [&](GCGraph *graph) { return program_->graph(graph->id()) == graph; };

    std::vector<GCGraph *> first;
    for (GCGraph *graph : closure) {
        // A graph that was asked for editing and not changed is the graph it was.
        if (heldBefore(graph) && edited_.contains(graph)) {
            const auto draft = drafts_.find(graph);
            if (draft != drafts_.end() && draft->second->draft->mutations() == 0) {
                edited_.erase(graph);
            }
        }
        if (!heldBefore(graph) || edited_.contains(graph)) {
            first.push_back(graph);
            allocate(graph);
        }
    }
    for (auto it = first.rbegin(); it != first.rend(); ++it) {
        encode(*it);
    }

    // The graphs that have a new version where the program had one. An edited graph encoded
    // back to its own content is left as it is, unless it holds function values.
    std::vector<GCGraph *> replaced;
    for (GCGraph *graph : first) {
        if (!heldBefore(graph)) {
            continue;
        }
        GCGraph *copy = fresh.at(graph);
        if (graph->staticGraphRefs().empty() && sameContent(*graph, *copy)) {
            committed->bind(graph->id(), graph);
            fresh.erase(graph);
            context_->releaseRuntimeGraph(copy);
            edited_.erase(graph);
            continue;
        }
        replaced.push_back(graph);
    }
    if (!replaced.empty()) {
        // A function value names a graph in the program it was made in, and runs what that
        // program holds of everything the graph calls. So a graph that holds a function value
        // of a graph that reaches a new version (by calls or by function values) needs a new
        // version too, which names the commit's program. The graphs that reach a new version
        // are found backwards over the calls and the function values of the closure.
        constexpr uint32_t kNone = UINT32_MAX;
        uint32_t maxId           = 0;
        for (GCGraph *graph : closure) {
            maxId = std::max(maxId, static_cast<uint32_t>(toValue(graph->id())));
        }
        std::vector<uint32_t> indexOf(static_cast<size_t>(maxId) + 1, kNone);
        for (size_t i = 0; i < closure.size(); ++i) {
            indexOf[toValue(closure[i]->id())] = static_cast<uint32_t>(i);
        }
        const auto indexOfId = [&](GraphId id) {
            return toValue(id) < indexOf.size() ? indexOf[toValue(id)] : kNone;
        };
        constexpr uint8_t kRoles = kGraphRefCallee | kGraphRefStatic | kGraphRefRule;
        std::vector<uint32_t> first(closure.size() + 1, 0), callers;
        for (GCGraph *graph : closure) {
            for (size_t r = 0; r < graph->refs().size(); ++r) {
                if (graph->refRoles(r) & kRoles) {
                    if (const uint32_t to = indexOfId(graph->refs()[r]); to != kNone) {
                        ++first[to + 1];
                    }
                }
            }
        }
        for (size_t i = 0; i < closure.size(); ++i) {
            first[i + 1] += first[i];
        }
        callers.resize(first.back());
        std::vector<uint32_t> fill(first.begin(), first.end() - 1);
        for (size_t from = 0; from < closure.size(); ++from) {
            const GCGraph *graph = closure[from];
            for (size_t r = 0; r < graph->refs().size(); ++r) {
                if (graph->refRoles(r) & kRoles) {
                    if (const uint32_t to = indexOfId(graph->refs()[r]); to != kNone) {
                        callers[fill[to]++] = static_cast<uint32_t>(from);
                    }
                }
            }
        }
        std::vector<uint8_t> reaches(closure.size(), 0);
        std::vector<uint32_t> work;
        for (GCGraph *graph : replaced) {
            const uint32_t at = indexOfId(graph->id());
            if (at != kNone && !reaches[at]) {
                reaches[at] = 1;
                work.push_back(at);
            }
        }
        while (!work.empty()) {
            const uint32_t at = work.back();
            work.pop_back();
            for (uint32_t k = first[at]; k < first[at + 1]; ++k) {
                if (!reaches[callers[k]]) {
                    reaches[callers[k]] = 1;
                    work.push_back(callers[k]);
                }
            }
        }
        for (GCGraph *holder : closure) {
            if (fresh.contains(holder)) {
                continue;
            }
            for (size_t r = 0; r < holder->refs().size(); ++r) {
                if (!(holder->refRoles(r) & kGraphRefStatic)) {
                    continue;
                }
                const uint32_t held = indexOfId(holder->refs()[r]);
                if (held != kNone && reaches[held]) {
                    allocate(holder);
                    encode(holder);
                    break;
                }
            }
        }
    }

    for (GCGraph *graph : order) {
        if (auto it = fresh.find(graph); it != fresh.end()) {
            validateRuntimeGraphPayload(*committed, it->second);
        }
    }
    for (GCGraph *graph : order) {
        if (auto it = fresh.find(graph); it != fresh.end()) {
            validateRuntimeGraphCalls(*committed, it->second, graphState_);
        }
    }
    // The copy is the same graph to the fact store; a new revision only for a graph that was
    // edited and not encoded back to the same content (what a graph refers to is the fact
    // engine's business: closureRevision).
    for (const auto &[graph, copy] : fresh) {
        const bool changed = edited_.contains(graph) && !sameContent(*graph, *copy);
        GCGraphBuildAccess::inheritRevision(copy, graph, changed);
    }
    committed->setRoot(committed->graph(oldRuntimeRoot->id()));

    // The table is a cache, not an owner (specializations.h): an entry whose specialization the
    // program does not hold names a graph the snapshot collected, and an entry whose callee got a
    // new version names one this program no longer has. Neither resolves in a later session, so
    // the program forgets them: it keeps the specializations its root reaches, and a session that
    // needs another makes that one again. Nothing here binds a graph the closure does not reach,
    // so a specialization nothing calls is collected with the snapshot it was in.
    committed->specializations().eraseIf(
        [&](const ProgramSpecializationKey &key, GraphId id) {
            const GCGraph *specialized = committed->graph(id);
            if (specialized == nullptr) {
                return false;
            }
            const GCGraph *base = committed->graph(key.baseGraph);
            return base != nullptr && base->revision() == key.baseRevision;
        });

    // Publishing is a compare-and-swap against the program the commit was made on top of: the
    // graphs are encoded for that program, and the table replaces it only while it is still the
    // context's (design 4.3.1). A merge that lost the race to another commit remakes its table
    // over the program that is there now while the graphs it read and wrote keep their versions
    // there, and otherwise the commit stays private (it still hands back the program it made).
    bool published = false;
    if (installing) {
        constexpr int kMaxPublishAttempts = 4;
        const Program *expected           = base;
        for (int attempt = 0; attempt < kMaxPublishAttempts; ++attempt) {
            if (context_->installRuntimeProgramIfCurrent(expected, committed)) {
                published = true;
                break;
            }
            const Program *now = context_->currentRuntimeProgram();
            if (now == nullptr || now == program_ || !conflictsWithProgram(now).empty()) {
                break;
            }
            Program *rebased = context_->reserveProgram();
            *rebased         = *now;
            // Only the versions this commit made are its own: every other graph of the program
            // is the one that is there now, which another commit may have given a new version.
            for (const auto &[graph, copy] : fresh) {
                rebased->bind(graph->id(), copy);
            }
            rebased->setRoot(rebased->graph(committed->root()->id()));
            rebased->specializations() = committed->specializations();
            committed                  = rebased;
            expected  = now;
        }
    }
    if (!published) {
        // A private copy of a program: tracking it keeps it alive for the caller and out of the
        // context's root until something installs it.
        context_->trackDetachedRuntimeProgram(committed);
    }
    // A graph left as it was names, in its function values, the program before: they name the
    // program this commit produced, which holds the same versions of everything they reach.
    for (GCGraph *graph : closure) {
        if (!fresh.contains(graph) && graph->hasStaticGraphRefs()) {
            retargetFunctions(graph, *committed);
        }
    }
// The commit decided above whether this session installs (a session over the program's root,
    // a shape some back end runs, nothing detached) and published with a compare-and-swap; a
    // commit that did not publish stays a private program that remembers what it was made from.
    program_            = committed;
    runtimeRoot_        = program_->root();
    work_               = std::make_unique<Program>(*program_);
    closureCache_.valid = false;
#ifndef NDEBUG
    noteDraftsCleared(*debugRoots_);
#endif
    drafts_.clear();
    draftedIds_.clear();
    draftRefs_.clear();
    edited_.clear();
    return program_;
}

GraphDraftGroup::GraphDraftGroup(
    const camel::core::context::context_ptr_t &context, const Program *base)
    : context_(context) {
    ASSERT(context_ != nullptr, "Graph draft group requires a valid context.");
    ASSERT(base != nullptr, "Graph draft group requires the program it is made over.");
    program_  = context_->reserveProgram();
    *program_ = *base;
    program_->makeSatellite();
}

GraphDraftGroup::~GraphDraftGroup() = default;

GraphId GraphDraftGroup::reserve(std::string mangledName, std::string name) {
    auto member      = std::make_unique<Member>();
    member->draft    = std::make_unique<GraphDraft>();
    const GraphId id = context_->graphIds().allocate();
    member->draft->setId(id);
    member->draft->setProgram(program_);
    member->identity = RuntimeDraftIdentity{
        .sourceGraph = nullptr,
        .mangledName = std::move(mangledName),
        .name        = std::move(name),
    };
    members_.emplace(id, std::move(member));
    order_.push_back(id);
    return id;
}

GraphDraft &GraphDraftGroup::draft(GraphId id) { return *member(id).draft; }

void GraphDraftGroup::define(GraphId id, std::unique_ptr<GraphDraft> draft) {
    ASSERT(draft != nullptr, "Graph draft group member draft cannot be null.");
    draft->setId(id);
    draft->setProgram(program_);
    member(id).draft = std::move(draft);
}

void GraphDraftGroup::adopt(const Program &program) { program_->adoptMissing(program); }

void GraphDraftGroup::encode() {
    ASSERT(!encoded_, "Graph draft group was already encoded.");
    std::unordered_map<GraphId, GCGraph *> graphs;
    std::unordered_map<GraphId, size_t> bytes;
    for (GraphId id : order_) {
        Member &m = member(id);
        bytes.emplace(id, draftGraphBytes(context_, *m.draft, nullptr));
        graphs.emplace(id, allocateGraph(context_, m.identity, *m.draft, bytes.at(id)));
    }
    // The members form a program with the graphs of the one the group is made over.
    for (GraphId id : order_) {
        program_->bindOwn(id, graphs.at(id));
    }
    if (!order_.empty()) {
        program_->setRoot(graphs.at(order_.front()));
    }
    for (GraphId id : order_) {
        const Member &m = member(id);
        GCGraph *graph =
            encodeDraftGraph(context_, m.identity, *m.draft, program_, graphs.at(id), bytes.at(id));
        validateRuntimeGraphPayload(*program_, graph);
    }
    for (GraphId id : order_) {
        validateRuntimeGraphCalls(*program_, graphs.at(id));
    }
    for (GraphId id : order_) {
        member(id).encoded = graphs.at(id);
    }
    encoded_ = true;
}

GCGraph *GraphDraftGroup::encoded(GraphId id) const {
    auto it = members_.find(id);
    ASSERT(it != members_.end(), "Graph draft group does not contain the requested graph.");
    ASSERT(encoded_, "Graph draft group was not encoded yet.");
    return it->second->encoded;
}

GraphDraftGroup::Member &GraphDraftGroup::member(GraphId id) {
    auto it = members_.find(id);
    ASSERT(it != members_.end(), "Graph draft group does not contain the requested graph.");
    return *it->second;
}

const GraphDraftGroup::Member &GraphDraftGroup::member(GraphId id) const {
    auto it = members_.find(id);
    ASSERT(it != members_.end(), "Graph draft group does not contain the requested graph.");
    return *it->second;
}

} // namespace camel::runtime
