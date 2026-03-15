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
 * Updated: Mar. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "inline.h"

#include "camel/common/algo/topo.h"
#include "camel/compile/gir/rewrite.h"
#include "camel/core/error/runtime.h"
#include "camel/utils/log.h"

using namespace std;
using namespace GIR;
using namespace camel::core::error;

graph_ptr_t InlineRewritePass::apply(graph_ptr_t &graph, ostream &os) {
    GraphRewriteSession session(graph);
    graph_ptr_t workingRoot = session.root();

    std::vector<graph_ptr_t> allGraphs;
    allGraphs.push_back(workingRoot);
    for (const auto &[_, gSet] : workingRoot->subGraphs()) {
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

        std::vector<std::pair<Node *, Node *>> targets;

        for (const auto &brch : g->nodes()) {
            if (brch->type() == NodeType::BRCH) {
                auto *brchNode = tt::as_ptr<BrchNode>(brch);
                for (size_t i = 0; i < brchNode->armCount(); ++i) {
                    Node *path = brchNode->armHead(i);
                    if (path->type() == NodeType::FUNC &&
                        tt::as_ptr<FuncNode>(path)->hasMatchedJoin()) {
                        targets.emplace_back(brch, path);
                    }
                }
            }
        }

        for (const auto &[brch, path] : targets) {
            auto *funcPath        = tt::as_ptr<FuncNode>(path);
            auto *joinNode        = funcPath->matchedJoin();
            const auto &pathGraph = funcPath->func()->graph().shared_from_this();
            Node *syncNode        = session.inlineNode(path, true);

            if (!syncNode) {
                throwRuntimeFault(RuntimeDiag::GraphInliningFailed, brch->toString(), g->name());
            }
            session.markChanged();

            Node *branchHead = nullptr;
            if (syncNode->ctrlOutputs().size() > 1) {
                session.link(LinkType::Ctrl, brch, syncNode);
                branchHead = syncNode;
            } else {
                for (const auto &out : syncNode->ctrlOutputs()) {
                    session.link(LinkType::Ctrl, brch, out);
                    branchHead = out;
                }
                session.eraseNode(syncNode);
            }
            ASSERT(branchHead != nullptr, "Inlined branch head is null.");
            Node *branchTail = joinNode->armTail(joinNode->armCount() - 1);

            session.replaceOutput(LinkType::Ctrl, brch, path, branchHead);
            session.replaceInput(LinkType::With, joinNode, path, branchTail);
            session.eraseNode(path);

            session.eraseSubGraph(g, pathGraph);
            session.eraseDependency(g, pathGraph);

            GetDefaultLogger()
                .in("InlinePass")
                .info(
                    "Inlined FUNC node {} (graph {}) between BRCH and JOIN node {} in graph {}.",
                    path->toString(),
                    pathGraph->name(),
                    brch->toString(),
                    g->name());
        }
    }

    auto result = session.finish();
    graph       = result.graph;
    return graph;
}
