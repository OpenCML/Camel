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
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Draft-based compile graph validation.
 *
 * Compile-time GIR construction now lives on top of DraftGraphBuilder and the
 * shared GraphDraft editing core. These checks only verify invariants that are
 * still meaningful before runtime encoding: node reachability, adjacency
 * symmetry, branch/join consistency, and static function references staying
 * inside the reachable compile graph tree.
 */

#include "camel/compile/gir/validate.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/composite.h"

#include <format>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

namespace camel::compile::gir {

namespace {

using camel::core::rtdata::fromSlot;
using camel::core::rtdata::Object;
using camel::core::type::ArrayType;
using camel::core::type::StructType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::TypeCode;
using runtime::GCBranchArm;
using runtime::GCJoinBody;
using runtime::GCNodeKind;

bool shouldTraverseStaticSlotType(Type *type) {
    if (type == nullptr || !type->isGCTraced() || type->code() == TypeCode::Ref) {
        return false;
    }
    if (type->isComposite() && !static_cast<camel::core::type::CompositeType *>(type)->resolved()) {
        return false;
    }
    return true;
}

void validateExitAnchor(const DraftGraphBuilder &graph) {
    const auto exitNode = graph.draft().exitNode();
    if (exitNode == runtime::kInvalidNodeRef) {
        return;
    }
    const auto *header = graph.draft().header(exitNode);
    ASSERT(header != nullptr, "Exit node must point to a live draft node.");
    if (header->kind != GCNodeKind::Gate) {
        return;
    }
    ASSERT(
        !graph.draft().normInputsOf(exitNode).empty(),
        std::format("Output gate in graph '{}' must have at least one Norm input.", graph.name()));
    ASSERT(
        !graph.draft().ctrlInputsOf(exitNode).empty(),
        std::format("Output gate in graph '{}' must have at least one Ctrl input.", graph.name()));
}

void validateSymmetricAdjacency(
    const DraftGraphBuilder &graph, runtime::DraftEdgeKind kind, draft_node_ref_t from,
    draft_node_ref_t to) {
    auto users = [&]() -> std::span<const draft_node_ref_t> {
        switch (kind) {
        case runtime::DraftEdgeKind::Norm:
            return graph.draft().normUsersOf(from);
        case runtime::DraftEdgeKind::With:
            return graph.draft().withUsersOf(from);
        case runtime::DraftEdgeKind::Ctrl:
            return graph.draft().ctrlUsersOf(from);
        }
        ASSERT(false, "Unknown draft edge kind.");
        return {};
    }();
    (void)users;

    auto inputs = [&]() -> std::span<const draft_node_ref_t> {
        switch (kind) {
        case runtime::DraftEdgeKind::Norm:
            return graph.draft().normInputsOf(to);
        case runtime::DraftEdgeKind::With:
            return graph.draft().withInputsOf(to);
        case runtime::DraftEdgeKind::Ctrl:
            return graph.draft().ctrlInputsOf(to);
        }
        ASSERT(false, "Unknown draft edge kind.");
        return {};
    }();
    (void)inputs;

    ASSERT(
        std::ranges::find(users, to) != users.end() &&
            std::ranges::find(inputs, from) != inputs.end(),
        std::format(
            "Draft adjacency is not symmetric in graph '{}' for edge {} -> {}.",
            graph.name(),
            from,
            to));
}

void validateNodeAdjacency(const DraftGraphBuilder &graph, draft_node_ref_t nodeId) {
    for (draft_node_ref_t input : graph.draft().normInputsOf(nodeId)) {
        validateSymmetricAdjacency(graph, runtime::DraftEdgeKind::Norm, input, nodeId);
    }
    for (draft_node_ref_t input : graph.draft().withInputsOf(nodeId)) {
        validateSymmetricAdjacency(graph, runtime::DraftEdgeKind::With, input, nodeId);
    }
    for (draft_node_ref_t input : graph.draft().ctrlInputsOf(nodeId)) {
        validateSymmetricAdjacency(graph, runtime::DraftEdgeKind::Ctrl, input, nodeId);
    }
    for (draft_node_ref_t user : graph.draft().normUsersOf(nodeId)) {
        validateSymmetricAdjacency(graph, runtime::DraftEdgeKind::Norm, nodeId, user);
    }
    for (draft_node_ref_t user : graph.draft().withUsersOf(nodeId)) {
        validateSymmetricAdjacency(graph, runtime::DraftEdgeKind::With, nodeId, user);
    }
    for (draft_node_ref_t user : graph.draft().ctrlUsersOf(nodeId)) {
        validateSymmetricAdjacency(graph, runtime::DraftEdgeKind::Ctrl, nodeId, user);
    }
}

void validateFuncGraphReference(const DraftGraphBuilder &graph, draft_node_ref_t nodeId) {
    const auto *header = graph.draft().header(nodeId);
    if (!header || header->kind != GCNodeKind::Func) {
        return;
    }

    graph_ptr_t bodyGraph = graph.funcTarget(nodeId);
    ASSERT(bodyGraph != nullptr, "FUNC node target graph cannot be null.");

    ASSERT(
        graph.draft().normInputsOf(nodeId).size() == bodyGraph->draft().normPorts().size(),
        std::format(
            "FUNC node in graph '{}' has mismatched norm arity against target '{}'.",
            graph.name(),
            bodyGraph->name()));
    ASSERT(
        graph.draft().withInputsOf(nodeId).size() == bodyGraph->draft().withPorts().size(),
        std::format(
            "FUNC node in graph '{}' has mismatched with arity against target '{}'.",
            graph.name(),
            bodyGraph->name()));

    if (bodyGraph.get() == &graph) {
        return;
    }

    ASSERT(
        std::ranges::find(graph.dependencyGraphs(), bodyGraph) != graph.dependencyGraphs().end(),
        std::format(
            "FUNC node in graph '{}' refers to '{}' without dependency registration.",
            graph.name(),
            bodyGraph->name()));
}

void validateStaticValueGraphRefs(
    const DraftGraphBuilder &owner, slot_t slot, Type *type,
    const std::unordered_set<const DraftGraphBuilder *> &reachable,
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
        auto *funcObj  = fromSlot<StaticFunction *>(slot);
        auto funcGraph = funcObj ? funcObj->graph() : nullptr;
        ASSERT(funcGraph != nullptr, "Static function slot contains a null graph pointer.");
        ASSERT(
            reachable.contains(funcGraph.get()),
            std::format(
                "Static function in graph '{}' points outside the reachable graph tree.",
                owner.name()));
        if (auto *closure = funcObj->tuple()) {
            TupleType *closureType = const_cast<TupleType *>(funcObj->tupleType());
            for (size_t i = 0; i < closureType->size(); ++i) {
                if (!shouldTraverseStaticSlotType(closureType->typeAt(i))) {
                    continue;
                }
                validateStaticValueGraphRefs(
                    owner,
                    closure->get<slot_t>(i),
                    closureType->typeAt(i),
                    reachable,
                    visited);
            }
        }
    } break;
    case TypeCode::Tuple: {
        auto *tuple     = fromSlot<::Tuple *>(slot);
        auto *tupleType = static_cast<TupleType *>(type);
        for (size_t i = 0; i < tupleType->size(); ++i) {
            if (!shouldTraverseStaticSlotType(tupleType->typeAt(i))) {
                continue;
            }
            validateStaticValueGraphRefs(
                owner,
                tuple->get<slot_t>(i),
                tupleType->typeAt(i),
                reachable,
                visited);
        }
    } break;
    case TypeCode::Array: {
        auto *array     = fromSlot<::Array *>(slot);
        auto *arrayType = static_cast<ArrayType *>(type);
        if (!shouldTraverseStaticSlotType(arrayType->elemType())) {
            break;
        }
        for (size_t i = 0; i < array->size(); ++i) {
            validateStaticValueGraphRefs(
                owner,
                array->get<slot_t>(i),
                arrayType->elemType(),
                reachable,
                visited);
        }
    } break;
    case TypeCode::Struct: {
        auto *st         = fromSlot<::Struct *>(slot);
        auto *structType = static_cast<StructType *>(type);
        for (size_t i = 0; i < structType->size(); ++i) {
            if (!shouldTraverseStaticSlotType(structType->typeAt(i))) {
                continue;
            }
            validateStaticValueGraphRefs(
                owner,
                st->get<slot_t>(i),
                structType->typeAt(i),
                reachable,
                visited);
        }
    } break;
    default:
        break;
    }
}

} // namespace

namespace camel::compile::gir::validate {

void assertGraphSealingPreconditions(const DraftGraphBuilder &graph) {
    if (graph.draft().outputNode() == runtime::kInvalidNodeRef) {
        throw std::runtime_error(std::format("Graph '{}' has no output anchor.", graph.name()));
    }

    for (draft_node_ref_t nodeId = 0; nodeId < graph.draft().nodeSlotCount(); ++nodeId) {
        if (!graph.draft().alive(nodeId)) {
            continue;
        }
        validateNodeAdjacency(graph, nodeId);
        validateFuncGraphReference(graph, nodeId);

        const auto *header = graph.draft().header(nodeId);
        ASSERT(header != nullptr, "Live draft node cannot have a null header.");

        if (header->kind == GCNodeKind::Brch) {
            const auto *payload = reinterpret_cast<const runtime::DraftBrchPayload *>(
                graph.draft().payloadOf(nodeId).data());
            (void)payload;
            ASSERT(payload != nullptr, "BRCH payload cannot be null.");
            const auto arms = graph.draft().branchArmsOf(nodeId);
            (void)arms;
            ASSERT(
                payload->join != runtime::kInvalidNodeRef,
                std::format("BRCH node in graph '{}' is missing its JOIN target.", graph.name()));
            ASSERT(
                payload->armCount == arms.size(),
                std::format(
                    "BRCH node in graph '{}' has inconsistent arm metadata.",
                    graph.name()));
        }

        if (header->kind == GCNodeKind::Join) {
            const auto *joinBody =
                reinterpret_cast<const GCJoinBody *>(graph.draft().payloadOf(nodeId).data());
            (void)joinBody;
            ASSERT(joinBody != nullptr, "JOIN payload cannot be null.");
            ASSERT(
                joinBody->brch != runtime::kInvalidNodeRef,
                std::format(
                    "JOIN node in graph '{}' is missing its BRCH back-reference.",
                    graph.name()));
        }

        if (header->kind == GCNodeKind::Gate) {
            ASSERT(
                !graph.draft().normInputsOf(nodeId).empty(),
                std::format(
                    "GATE node in graph '{}' must have at least one Norm input.",
                    graph.name()));
            ASSERT(
                !graph.draft().ctrlInputsOf(nodeId).empty(),
                std::format(
                    "GATE node in graph '{}' must have at least one Ctrl input.",
                    graph.name()));
        }
    }

    validateExitAnchor(graph);
}

void assertGraphTreeSealingPreconditions(const graph_ptr_t &graph) {
    if (!graph) {
        return;
    }
    std::unordered_set<const DraftGraphBuilder *> visited;
    std::function<void(const graph_ptr_t &)> dfs = [&](const graph_ptr_t &curr) {
        if (!curr || !visited.insert(curr.get()).second) {
            return;
        }
        assertGraphSealingPreconditions(*curr);
        for (const auto &subGraph : curr->subGraphs()) {
            dfs(subGraph);
        }
        for (const auto &dep : curr->dependencyGraphs()) {
            dfs(dep);
        }
    };
    dfs(graph);
}

void assertGraphTreeStaticReferences(const graph_ptr_t &graph) {
    if (!graph) {
        return;
    }

    std::unordered_set<const DraftGraphBuilder *> reachableGraphs;
    std::function<void(const graph_ptr_t &)> collect = [&](const graph_ptr_t &curr) {
        if (!curr || !reachableGraphs.insert(curr.get()).second) {
            return;
        }
        for (const auto &subGraph : curr->subGraphs()) {
            collect(subGraph);
        }
        for (const auto &dep : curr->dependencyGraphs()) {
            collect(dep);
        }
    };
    collect(graph);

    std::unordered_set<const DraftGraphBuilder *> visitedGraphs;
    std::function<void(const graph_ptr_t &)> validateStaticRefs = [&](const graph_ptr_t &curr) {
        if (!curr || !visitedGraphs.insert(curr.get()).second) {
            return;
        }
        std::unordered_set<const Object *> visitedObjects;
        const auto staticSlots = curr->draft().staticSlots();
        const auto staticTypes = curr->draft().staticSlotTypes();
        for (size_t i = 0; i < staticSlots.size() && i < staticTypes.size(); ++i) {
            validateStaticValueGraphRefs(
                *curr,
                staticSlots[i],
                staticTypes[i],
                reachableGraphs,
                visitedObjects);
        }
        for (const auto &subGraph : curr->subGraphs()) {
            validateStaticRefs(subGraph);
        }
        for (const auto &dep : curr->dependencyGraphs()) {
            validateStaticRefs(dep);
        }
    };
    validateStaticRefs(graph);
}

} // namespace camel::compile::gir::validate
} // namespace camel::compile::gir
