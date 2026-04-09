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
 * Created: Apr. 06, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * This file owns runtime-value-to-GIR materialization for rewrite passes.
 * The central requirement is to decouple execution identity from rewrite
 * identity: macro execution runs on sealed source graphs, while rewritten
 * DATA nodes must only reference canonical draft graphs. Nested function
 * values inside tuples, arrays, structs, and closures are normalized here.
 */

#include "camel/compile/gir/rewrite.h"

#include "camel/compile/gir/nodes.h"
#include "camel/compile/gir/reachable.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"

namespace camel::compile::gir {

using camel::core::rtdata::fromSlot;
using camel::core::rtdata::Object;
using camel::core::rtdata::toSlot;
using camel::core::type::ArrayType;
using camel::core::type::StructType;
using camel::core::type::TupleType;
using camel::core::type::TypeCode;

namespace {

using ObjectCloneCache = std::unordered_map<const Object *, Object *>;

graph_ptr_t requireOwnedGraphHandle(Graph *graph, const Graph *owner, const char *context) {
    ASSERT(graph != nullptr, "Graph handle request received a null graph.");
    try {
        return graph->shared_from_this();
    } catch (const std::bad_weak_ptr &) {
        throw std::runtime_error(
            std::format(
                "{} encountered non-owned graph '{}' ({:p}) while inspecting owner '{}'.",
                context,
                graph->name(),
                static_cast<void *>(graph),
                owner ? owner->name() : "<null>"));
    }
}

node_vec_t collectUseCarrierNodes(const graph_ptr_t &graph) {
    node_vec_t carriers;
    if (!graph) {
        return carriers;
    }
    carriers.reserve(graph->nodes().size() + 1);
    for (Node *node : graph->nodes()) {
        carriers.push_back(node);
    }
    Node *exitAnchor = graph->exitNode();
    if (exitAnchor && std::ranges::find(carriers, exitAnchor) == carriers.end()) {
        carriers.push_back(exitAnchor);
    }
    return carriers;
}

std::vector<graph_ptr_t> collectReferencedGraphs(const graph_ptr_t &graph) {
    std::vector<graph_ptr_t> refs;
    if (!graph) {
        return refs;
    }

    refs.reserve(graph->dependencies().size() + graph->nodes().size());
    for (const auto &[_, subGraphs] : graph->subGraphs()) {
        for (const auto &sub : subGraphs) {
            refs.push_back(sub);
        }
    }
    for (const auto &dep : graph->dependencies()) {
        refs.push_back(dep);
    }
    for (Node *node : collectUseCarrierNodes(graph)) {
        if (!node) {
            continue;
        }
        if (node->type() == NodeType::FUNC) {
            auto *func = tt::as_ptr<FuncNode>(node);
            if (func->bodyGraph()) {
                refs.push_back(requireOwnedGraphHandle(
                    func->bodyGraph(),
                    graph.get(),
                    "rewrite::collectReferencedGraphs"));
            }
            continue;
        }
        if (node->type() != NodeType::DATA) {
            continue;
        }
        auto *dataNode = tt::as_ptr<DataNode>(node);
        if (dataNode->dataType()->code() != TypeCode::Function) {
            continue;
        }
        auto *funcObj = fromSlot<::Function *>(dataNode->dataSlot());
        if (funcObj && funcObj->sourceGraph()) {
            refs.push_back(requireOwnedGraphHandle(
                funcObj->sourceGraph(),
                graph.get(),
                "rewrite::collectReferencedGraphs"));
        }
    }
    return refs;
}

TupleType *currentClosureTupleType(const graph_ptr_t &graph) {
    std::vector<Type *> closureTypes;
    closureTypes.reserve(graph->closure().size());
    for (Node *node : graph->closure()) {
        closureTypes.push_back(node->dataType());
    }
    return TupleType::create(std::move(closureTypes));
}

slot_t canonicalizeStaticSlot(
    GraphRewriteSession &session, const graph_ptr_t &owner, slot_t slot, Type *type,
    ObjectCloneCache &clones);

slot_t canonicalizeTuple(
    GraphRewriteSession &session, const graph_ptr_t &owner, ::Tuple *tuple, TupleType *tupleType,
    ObjectCloneCache &clones) {
    if (!tuple) {
        return NullSlot;
    }
    if (auto it = clones.find(tuple); it != clones.end()) {
        return toSlot<Object *>(it->second);
    }

    auto *cloned =
        static_cast<::Tuple *>(tuple->clone(owner->arena()->allocator(), tupleType, false));
    clones[tuple] = cloned;
    for (size_t i = 0; i < tupleType->size(); ++i) {
        if (!camel::core::type::isGCTraced(tupleType->codeAt(i))) {
            continue;
        }
        cloned->set<slot_t>(
            i,
            canonicalizeStaticSlot(
                session,
                owner,
                cloned->get<slot_t>(i),
                tupleType->typeAt(i),
                clones));
    }
    return toSlot<::Tuple *>(cloned);
}

slot_t canonicalizeArray(
    GraphRewriteSession &session, const graph_ptr_t &owner, ::Array *array, ArrayType *arrayType,
    ObjectCloneCache &clones) {
    if (!array) {
        return NullSlot;
    }
    if (auto it = clones.find(array); it != clones.end()) {
        return toSlot<Object *>(it->second);
    }

    auto *cloned =
        static_cast<::Array *>(array->clone(owner->arena()->allocator(), arrayType, false));
    clones[array] = cloned;
    if (!camel::core::type::isGCTraced(arrayType->elemTypeCode())) {
        return toSlot<::Array *>(cloned);
    }
    for (size_t i = 0; i < cloned->size(); ++i) {
        cloned->set<slot_t>(
            i,
            canonicalizeStaticSlot(
                session,
                owner,
                cloned->get<slot_t>(i),
                arrayType->elemType(),
                clones));
    }
    return toSlot<::Array *>(cloned);
}

slot_t canonicalizeStruct(
    GraphRewriteSession &session, const graph_ptr_t &owner, ::Struct *st, StructType *structType,
    ObjectCloneCache &clones) {
    if (!st) {
        return NullSlot;
    }
    if (auto it = clones.find(st); it != clones.end()) {
        return toSlot<Object *>(it->second);
    }

    auto *cloned =
        static_cast<::Struct *>(st->clone(owner->arena()->allocator(), structType, false));
    clones[st] = cloned;
    for (size_t i = 0; i < structType->size(); ++i) {
        if (!camel::core::type::isGCTraced(structType->codeAt(i))) {
            continue;
        }
        cloned->set<slot_t>(
            i,
            canonicalizeStaticSlot(
                session,
                owner,
                cloned->get<slot_t>(i),
                structType->typeAt(i),
                clones));
    }
    return toSlot<::Struct *>(cloned);
}

slot_t canonicalizeFunction(
    GraphRewriteSession &session, const graph_ptr_t &owner, ::Function *func, Type *funcType,
    ObjectCloneCache &clones) {
    (void)funcType;
    if (!func) {
        return NullSlot;
    }
    if (auto it = clones.find(func); it != clones.end()) {
        return toSlot<Object *>(it->second);
    }

    Graph *sourceGraph = func->sourceGraph();
    ASSERT(sourceGraph != nullptr, "Function static value must reference a graph.");
    graph_ptr_t sourceGraphPtr =
        requireOwnedGraphHandle(sourceGraph, owner.get(), "rewrite::cloneFunctionObject");
    graph_ptr_t canonicalGraph =
        session.ownsGraph(sourceGraph)
            ? sourceGraphPtr
            : (session.hasDraftGraph(sourceGraph) ? session.canonicalGraph(sourceGraphPtr)
                                                  : session.importDependency(
                                                        owner,
                                                        sourceGraphPtr,
                                                        GraphImportMode::CloneIntoDraft));
    auto *cloned = ::Function::create(
        canonicalGraph.get(),
        currentClosureTupleType(canonicalGraph),
        owner->arena()->allocator());
    clones[func] = cloned;
    session.importDependency(owner, canonicalGraph);

    if (::Tuple *sourceClosure = func->tuple()) {
        ::Tuple *closure       = cloned->tuple();
        TupleType *closureType = const_cast<TupleType *>(cloned->tupleType());
        for (size_t i = 0; i < closureType->size(); ++i) {
            slot_t slotValue = sourceClosure->get<slot_t>(i);
            if (camel::core::type::isGCTraced(closureType->codeAt(i))) {
                slotValue = canonicalizeStaticSlot(
                    session,
                    owner,
                    slotValue,
                    closureType->typeAt(i),
                    clones);
            }
            closure->set<slot_t>(i, slotValue);
        }
    }

    return toSlot<::Function *>(cloned);
}

slot_t canonicalizeStaticSlot(
    GraphRewriteSession &session, const graph_ptr_t &owner, slot_t slot, Type *type,
    ObjectCloneCache &clones) {
    if (!type || !type->isGCTraced() || slot == NullSlot) {
        return slot;
    }

    switch (type->code()) {
    case TypeCode::Function:
        return canonicalizeFunction(session, owner, fromSlot<::Function *>(slot), type, clones);
    case TypeCode::Tuple:
        return canonicalizeTuple(
            session,
            owner,
            fromSlot<::Tuple *>(slot),
            static_cast<TupleType *>(type),
            clones);
    case TypeCode::Array:
        return canonicalizeArray(
            session,
            owner,
            fromSlot<::Array *>(slot),
            static_cast<ArrayType *>(type),
            clones);
    case TypeCode::Struct:
        return canonicalizeStruct(
            session,
            owner,
            fromSlot<::Struct *>(slot),
            static_cast<StructType *>(type),
            clones);
    default:
        return slot;
    }
}

} // namespace

void GraphRewriteSession::collectReachableGraphs(
    const graph_ptr_t &graph, std::vector<graph_ptr_t> &out,
    std::unordered_set<const Graph *> &visited) {
    if (!graph || !visited.insert(graph.get()).second) {
        return;
    }
    out.push_back(graph);
    for (const auto &ref : collectReferencedGraphs(graph)) {
        collectReachableGraphs(ref, out, visited);
    }
}

Node *GraphRewriteSession::materializeStaticValue(
    const graph_ptr_t &owner, slot_t slot, Type *type,
    const StaticValueMaterializationOptions &options) {
    ASSERT(owner != nullptr, "Cannot materialize static value into null owner graph.");
    ASSERT(type != nullptr, "Cannot materialize static value with null type.");

    ObjectCloneCache clones;
    slot_t canonicalSlot = canonicalizeStaticSlot(*this, owner, slot, type, clones);
    Node *node           = DataNode::createStaticSlot(*owner, type, canonicalSlot);

    if (options.propagateMacro && type->code() == TypeCode::Function) {
        auto *funcObj = fromSlot<::Function *>(canonicalSlot);
        if (funcObj && funcObj->sourceGraph() && funcObj->sourceGraph()->isMacro()) {
            detail::NodeMutation::setMacro(node, true);
        }
    }

    return node;
}

} // namespace camel::compile::gir
