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
 * Created: Mar. 29, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gir/validate.h"
#include "camel/compile/gir/builder.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"

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
using camel::core::type::TypeCode;

void validateExitAnchor(const Graph &graph) {
    Node *exitNode = graph.exitNode();
    if (exitNode == nullptr) {
        return;
    }
    if (exitNode->type() != NodeType::GATE) {
        return;
    }
    ASSERT(
        !exitNode->normInputs().empty(),
        std::format(
            "Output anchor GATE '{}' in graph '{}' must have at least one Norm input.",
            exitNode->toString(),
            graph.name()));
    ASSERT(
        !exitNode->ctrlInputs().empty(),
        std::format(
            "Output anchor GATE '{}' in graph '{}' must have at least one Ctrl input.",
            exitNode->toString(),
            graph.name()));
}

void validateSymmetricAdjacency(LinkType type, Node *from, Node *to) {
    switch (type) {
    case LinkType::Norm:
        ASSERT(
            std::ranges::find(to->normInputs(), from) != to->normInputs().end(),
            "NORM adjacency is not symmetric.");
        break;
    case LinkType::With:
        ASSERT(
            std::ranges::find(to->withInputs(), from) != to->withInputs().end(),
            "WITH adjacency is not symmetric.");
        break;
    case LinkType::Ctrl:
        ASSERT(
            std::ranges::find(to->ctrlInputs(), from) != to->ctrlInputs().end(),
            "CTRL adjacency is not symmetric.");
        break;
    }
}

void validateNodeAdjacency(Node *node) {
    for (Node *out : node->normOutputs()) {
        validateSymmetricAdjacency(LinkType::Norm, node, out);
    }
    for (Node *out : node->withOutputs()) {
        validateSymmetricAdjacency(LinkType::With, node, out);
    }
    for (Node *out : node->ctrlOutputs()) {
        validateSymmetricAdjacency(LinkType::Ctrl, node, out);
    }
    for (Node *input : node->normInputs()) {
        bool linkedBack =
            std::ranges::find(input->normOutputs(), node) != input->normOutputs().end();
        (void)linkedBack;
        ASSERT(linkedBack, "NORM reverse adjacency is not symmetric.");
    }
    for (Node *input : node->withInputs()) {
        bool linkedBack =
            std::ranges::find(input->withOutputs(), node) != input->withOutputs().end();
        (void)linkedBack;
        ASSERT(linkedBack, "WITH reverse adjacency is not symmetric.");
    }
    for (Node *input : node->ctrlInputs()) {
        bool linkedBack =
            std::ranges::find(input->ctrlOutputs(), node) != input->ctrlOutputs().end();
        (void)linkedBack;
        ASSERT(linkedBack, "CTRL reverse adjacency is not symmetric.");
    }
}

void validateFuncGraphReference(const Graph &graph, Node *node) {
    if (node->type() != NodeType::FUNC) {
        return;
    }
    auto *funcNode        = tt::as_ptr<FuncNode>(node);
    graph_ptr_t bodyGraph = nullptr;
    if (funcNode->bodyGraph()) {
        try {
            bodyGraph = funcNode->bodyGraph()->shared_from_this();
        } catch (const std::bad_weak_ptr &) {
            throw std::runtime_error(
                std::format(
                    "validateFuncGraphReference encountered non-owned body graph '{}' ({:p}) in "
                    "owner '{}' .",
                    funcNode->bodyGraph()->name(),
                    static_cast<void *>(funcNode->bodyGraph()),
                    graph.name()));
        }
    }
    ASSERT(bodyGraph != nullptr, "FUNC node refers to a null body graph.");
    if (!bodyGraph) {
        throw std::runtime_error(
            std::format(
                "FUNC node '{}' in graph '{}' refers to a null body graph.",
                node->toString(),
                graph.name()));
    }
    if (node->normInputs().size() != bodyGraph->normPorts().size()) {
        throw std::runtime_error(
            std::format(
                "FUNC node '{}' in graph '{}' has {} norm args, but target '{}' expects {}.",
                node->toString(),
                graph.name(),
                node->normInputs().size(),
                bodyGraph->name(),
                bodyGraph->normPorts().size()));
    }
    if (node->withInputs().size() != bodyGraph->withPorts().size()) {
        throw std::runtime_error(
            std::format(
                "FUNC node '{}' in graph '{}' has {} with args, but target '{}' expects {}.",
                node->toString(),
                graph.name(),
                node->withInputs().size(),
                bodyGraph->name(),
                bodyGraph->withPorts().size()));
    }
    if (bodyGraph.get() == &graph) {
        // Self-recursive calls do not need an explicit dependency entry.
        // addDependency(self) only marks the graph as looped to encode recursion.
        ASSERT(
            graph.looped(),
            std::format("Recursive graph '{}' is not marked looped.", graph.name()));
        return;
    }
    ASSERT(
        graph.dependencies().contains(bodyGraph),
        std::format(
            "FUNC node '{}' refers to graph '{}' without dependency registration in '{}'.",
            node->toString(),
            bodyGraph->name(),
            graph.name()));
}

void validateStaticValueGraphRefs(
    const Graph &owner, slot_t slot, Type *type, const std::unordered_set<const Graph *> &reachable,
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
        auto *funcObj                     = fromSlot<::Function *>(slot);
        [[maybe_unused]] Graph *funcGraph = funcObj ? funcObj->sourceGraph() : nullptr;
        ASSERT(funcGraph != nullptr, "DATA(Function) contains a null graph pointer.");
        ASSERT(
            reachable.contains(funcGraph),
            std::format(
                "Static function value in graph '{}' points outside the reachable graph tree: "
                "{:p}.",
                owner.name(),
                static_cast<const void *>(funcGraph)));
        ASSERT(
            funcGraph->finalized(),
            std::format(
                "Static function value in graph '{}' points to unsealed graph '{}'.",
                owner.name(),
                funcGraph->name()));
        ASSERT(
            funcGraph->hasFrameLayout(),
            std::format(
                "Static function value in graph '{}' points to graph '{}' without frame layout.",
                owner.name(),
                funcGraph->name()));
        ASSERT(
            !funcGraph->name().empty(),
            std::format(
                "Static function value in graph '{}' points to an anonymous graph.",
                owner.name()));
        ASSERT(
            funcGraph == &owner ||
                [&]() {
                    try {
                        return owner.dependencies().contains(funcGraph->shared_from_this());
                    } catch (const std::bad_weak_ptr &) {
                        throw std::runtime_error(
                            std::format(
                                "validateStaticValueGraphRefs encountered non-owned static "
                                "function graph '{}' ({:p}) in owner '{}' .",
                                funcGraph->name(),
                                static_cast<void *>(funcGraph),
                                owner.name()));
                    }
                }(),
            std::format(
                "Static function value in graph '{}' refers to '{}' without dependency "
                "registration.",
                owner.name(),
                funcGraph->name()));

        if (::Tuple *closure = funcObj->tuple()) {
            TupleType *closureType = const_cast<TupleType *>(funcObj->tupleType());
            for (size_t i = 0; i < closureType->size(); ++i) {
                if (!camel::core::type::isGCTraced(closureType->codeAt(i))) {
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
            if (!camel::core::type::isGCTraced(tupleType->codeAt(i))) {
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
        if (!camel::core::type::isGCTraced(arrayType->elemTypeCode())) {
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
            if (!camel::core::type::isGCTraced(structType->codeAt(i))) {
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

namespace validate {

void assertGraphSealingPreconditions(const Graph &graph) {
    // Every graph must have an output anchor before sealing. Builder::build writes a placeholder
    // for library __root__ graphs without main.
    if (graph.exitNode() == nullptr) {
        throw std::runtime_error(std::format("Graph '{}' has no output anchor.", graph.name()));
    }

    for (Node *node : graph.normPorts()) {
        validateNodeAdjacency(node);
    }
    for (Node *node : graph.withPorts()) {
        validateNodeAdjacency(node);
    }
    for (Node *node : graph.closure()) {
        validateNodeAdjacency(node);
    }
    for (Node *node : graph.nodes()) {
        validateNodeAdjacency(node);
        validateFuncGraphReference(graph, node);
        if (node->type() == NodeType::BRCH) {
            auto *br = tt::as_ptr<BrchNode>(node);
            if (br->hasMatchedJoin() && br->armCount() != br->matchedJoin()->armCount()) {
                ASSERT(
                    false,
                    std::format(
                        "BRCH/JOIN arm count mismatch in graph '{}': brch={}, brArms={}, join={}, "
                        "joinArms={}.",
                        graph.name(),
                        br->toString(),
                        br->armCount(),
                        br->matchedJoin()->toString(),
                        br->matchedJoin()->armCount()));
            }
        }
        if (node->type() == NodeType::JOIN) {
            ASSERT(
                !tt::as_ptr<JoinNode>(node)->hasMatchedBranch() ||
                    tt::as_ptr<JoinNode>(node)->armCount() ==
                        tt::as_ptr<JoinNode>(node)->matchedBranch()->armCount(),
                std::format("JOIN/BRCH arm count mismatch in graph '{}'.", graph.name()));
        }
        if (node->type() == NodeType::GATE) {
            ASSERT(
                !node->normInputs().empty(),
                std::format(
                    "GATE node '{}' in graph '{}' must have at least one Norm input.",
                    node->toString(),
                    graph.name()));
            ASSERT(
                !node->ctrlInputs().empty(),
                std::format(
                    "GATE node '{}' in graph '{}' must have at least one Ctrl input.",
                    node->toString(),
                    graph.name()));
        }
    }
    if (graph.exitNode() != nullptr) {
        validateNodeAdjacency(graph.exitNode());
        validateExitAnchor(graph);
    }

    for (const auto &[name, subGraphs] : graph.subGraphs()) {
        for (const auto &subGraph : subGraphs) {
            (void)subGraph;
            ASSERT(subGraph != nullptr, "Graph subgraph registry contains null graph.");
            ASSERT(
                subGraph->name() == name,
                std::format(
                    "Subgraph registry key '{}' mismatches graph '{}'.",
                    name,
                    subGraph->name()));
        }
    }
}

void assertGraphTreeSealingPreconditions(const graph_ptr_t &graph) {
    if (!graph) {
        return;
    }
    std::unordered_set<const Graph *> visited;
    std::function<void(const graph_ptr_t &)> dfs = [&](const graph_ptr_t &curr) {
        if (!curr || !visited.insert(curr.get()).second) {
            return;
        }
        validate::assertGraphSealingPreconditions(*curr);
        for (const auto &[_, subGraphs] : curr->subGraphs()) {
            for (const auto &subGraph : subGraphs) {
                dfs(subGraph);
            }
        }
        for (const auto &dep : curr->dependencies()) {
            dfs(dep);
        }
    };
    dfs(graph);
}

} // namespace validate

void GraphBuilder::validateGraph(const Graph &graph) {
    validate::assertGraphSealingPreconditions(graph);
}

void GraphBuilder::validateGraphRecursively(const graph_ptr_t &graph) {
    validate::assertGraphTreeSealingPreconditions(graph);

    if (!graph) {
        return;
    }

    std::unordered_set<const Graph *> reachableGraphs;
    std::function<void(const graph_ptr_t &)> collect = [&](const graph_ptr_t &curr) {
        if (!curr || !reachableGraphs.insert(curr.get()).second) {
            return;
        }
        for (const auto &[_, subGraphs] : curr->subGraphs()) {
            for (const auto &subGraph : subGraphs) {
                collect(subGraph);
            }
        }
        for (const auto &dep : curr->dependencies()) {
            collect(dep);
        }
    };
    collect(graph);

    std::unordered_set<const Graph *> visitedGraphs;
    std::function<void(const graph_ptr_t &)> validateStaticRefs = [&](const graph_ptr_t &curr) {
        if (!curr || !visitedGraphs.insert(curr.get()).second) {
            return;
        }
        std::unordered_set<const Object *> visitedObjects;
        for (Node *node : curr->nodes()) {
            if (node->type() != NodeType::DATA) {
                continue;
            }
            auto *dataNode = tt::as_ptr<DataNode>(node);
            validateStaticValueGraphRefs(
                *curr,
                dataNode->dataSlot(),
                dataNode->dataType(),
                reachableGraphs,
                visitedObjects);
        }
        for (const auto &[_, subGraphs] : curr->subGraphs()) {
            for (const auto &subGraph : subGraphs) {
                validateStaticRefs(subGraph);
            }
        }
        for (const auto &dep : curr->dependencies()) {
            validateStaticRefs(dep);
        }
    };
    validateStaticRefs(graph);
}

} // namespace camel::compile::gir
