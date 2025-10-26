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
 * Created: Oct. 25, 2025
 * Updated: Oct. 26, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "inline.h"

#include "builtin/algo/topo.h"
#include "utils/log.h"

using namespace std;
using namespace GraphIR;

graph_ptr_t InlineRewritePass::apply(graph_ptr_t &graph, ostream &os) {
    std::vector<graph_ptr_t> allGraphs;
    allGraphs.push_back(graph);
    for (const auto &[_, gSet] : graph->subGraphs()) {
        for (const auto &g : gSet) {
            auto sortedSubGraphs =
                findReachable(g, [](const graph_ptr_t &g) { return g->dependencies(); });
            allGraphs.insert(allGraphs.end(), sortedSubGraphs.begin(), sortedSubGraphs.end());
        }
    }

    std::unordered_set<graph_ptr_t> visited;

    for (const auto &g : allGraphs) {
        if (visited.count(g))
            continue;
        visited.insert(g);

        std::vector<std::pair<node_ptr_t, node_ptr_t>> targets;

        for (const auto &brch : g->nodes()) {
            if (brch->type() == NodeType::BRCH) {
                for (const auto &path : brch->ctrlOutputs()) {
                    if (path->withOutputs().front()->type() == NodeType::JOIN &&
                        path->type() == NodeType::FUNC) {
                        targets.emplace_back(brch, path);
                    }
                }
            }
        }

        for (const auto &[brch, path] : targets) {
            const auto &pathGraph =
                tt::as_shared<FuncNode>(path)->func()->graph().shared_from_this();
            node_ptr_t syncNode = g->inlineNode(path, true);

            if (!syncNode) {
                context_->rtmDiags()
                    ->of(RuntimeDiag::GraphInliningFailed)
                    .commit(brch->toString(), g->name());
                return GraphIR::Graph::null();
            }

            if (syncNode->ctrlOutputs().size() > 1) {
                Node::link(LinkType::Ctrl, brch, syncNode);
            } else {
                for (const auto &out : syncNode->ctrlOutputs()) {
                    Node::link(LinkType::Ctrl, brch, out);
                }
                syncNode->detach();
                g->delNode(syncNode);
            }

            path->detach();
            g->delNode(path);

            g->delSubGraph(pathGraph);
            g->delDependency(pathGraph);

            g->rearrange();

            l.in("InlinePass")
                .info(
                    "Inlined FUNC node {} (graph {}) between BRCH and JOIN node {} in graph {}.",
                    path->toString(),
                    pathGraph->name(),
                    brch->toString(),
                    g->name());
        }
    }

    return graph;
}
