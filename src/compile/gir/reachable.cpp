/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Apr. 08, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * This file owns the canonical compile-time GIR graph reachability rules.
 * Any tool that needs to enumerate a graph tree should reuse this traversal
 * instead of open-coding another DFS with slightly different edge semantics.
 */

#include "camel/compile/gir/reachable.h"

#include "camel/compile/gir/graph.h"
#include "camel/compile/gir/nodes.h"
#include "camel/core/rtdata/func.h"

#include <unordered_set>

namespace camel::compile::gir {

namespace {

graph_ptr_t requireOwnedGraphHandle(Graph *graph, const Graph *owner, const char *context) {
    ASSERT(graph != nullptr, "Graph handle request received a null graph.");
    try {
        return graph->shared_from_this();
    } catch (const std::bad_weak_ptr &) {
        throw std::runtime_error(
            std::format(
                "{} encountered non-owned graph '{}' ({:p}) while traversing owner '{}'.",
                context,
                graph->name(),
                static_cast<void *>(graph),
                owner ? owner->name() : "<null>"));
    }
}

} // namespace

void forEachReachableGraph(
    const graph_ptr_t &root, const std::function<void(const graph_ptr_t &)> &visitor) {
    std::unordered_set<Graph *> visited;

    std::function<void(const graph_ptr_t &)> visit = [&](const graph_ptr_t &curr) {
        if (!curr || !visited.insert(curr.get()).second) {
            return;
        }

        visitor(curr);

        for (const auto &[_, subGraphs] : curr->subGraphs()) {
            for (const auto &subGraph : subGraphs) {
                visit(subGraph);
            }
        }

        for (const auto &dep : curr->dependencies()) {
            visit(dep);
        }

        for (Node *node : curr->nodes()) {
            if (node->type() == NodeType::FUNC) {
                auto *funcNode = tt::as_ptr<FuncNode>(node);
                if (funcNode->bodyGraph()) {
                    visit(requireOwnedGraphHandle(
                        funcNode->bodyGraph(),
                        curr.get(),
                        "compile::gir::reachable"));
                }
                continue;
            }

            if (node->type() != NodeType::DATA) {
                continue;
            }

            auto *dataNode = tt::as_ptr<DataNode>(node);
            if (dataNode->dataType()->code() != camel::core::type::TypeCode::Function) {
                continue;
            }

            auto *funcObj = camel::core::rtdata::fromSlot<::Function *>(dataNode->dataSlot());
            if (funcObj && funcObj->sourceGraph()) {
                visit(requireOwnedGraphHandle(
                    funcObj->sourceGraph(),
                    curr.get(),
                    "compile::gir::reachable"));
            }
        }
    };

    visit(root);
}

std::vector<graph_ptr_t> collectReachableGraphs(const graph_ptr_t &root) {
    std::vector<graph_ptr_t> graphs;
    forEachReachableGraph(root, [&](const graph_ptr_t &graph) { graphs.push_back(graph); });
    return graphs;
}

} // namespace camel::compile::gir
