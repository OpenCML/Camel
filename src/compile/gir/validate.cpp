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
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gir/validate.h"
#include "camel/compile/gir/builder.h"

#include <format>
#include <functional>
#include <ranges>
#include <unordered_set>

namespace camel::compile::gir {

namespace {

void validateExitAnchor(const Graph &graph) {
    Node *exitNode = graph.exitNode();
    ASSERT(exitNode != nullptr, std::format("Graph '{}' has no output anchor.", graph.name()));
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
    auto *funcNode = tt::as_ptr<FuncNode>(node);
    auto bodyGraph = funcNode->bodyGraph() ? funcNode->bodyGraph()->shared_from_this() : nullptr;
    ASSERT(bodyGraph != nullptr, "FUNC node refers to a null body graph.");
    if (bodyGraph.get() == &graph) {
        // 自递归不要求显式放入 dependencies，GraphBuilder::addDependency(self)
        // 只会标记 looped=true，用于表示递归环。
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

} // namespace

namespace validate {

void assertGraphSealingPreconditions(const Graph &graph) {
    // 完整性硬约束：进入 seal 的图必须先有 output（值出口锚点）。
    ASSERT(
        graph.exitNode() != nullptr,
        std::format("Graph '{}' has no output anchor.", graph.name()));

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
    validateNodeAdjacency(graph.exitNode());
    validateExitAnchor(graph);

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
}

} // namespace camel::compile::gir
