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
 * Created: Sep. 08, 2025
 * Updated: Sep. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "fallback.h"
#include "builtin/algo/topo.h"

using namespace std;
using namespace GIR;

data_ptr_t FallbackExecSchedPass::evalGraph(const graph_ptr_t &graph, arena_ptr_t &arena) {
    shared_ptr<node_vec_t> sortedNodesPtr;
    if (graphTNS_.find(graph.get()) == graphTNS_.end()) {
        auto sortedNodes = topoSort(
            graph->nodes().begin(),
            graph->nodes().end(),
            [](const auto &n) { return n->inDegree(); },
            [](const auto &n) {
                vector<node_ptr_t> outs;
                outs.insert(outs.end(), n->dataOutputs().begin(), n->dataOutputs().end());
                outs.insert(outs.end(), n->ctrlOutputs().begin(), n->ctrlOutputs().end());
                return outs;
            });
        sortedNodesPtr = std::make_shared<node_vec_t>(std::move(sortedNodes));
        graphTNS_[graph.get()] = sortedNodesPtr;
    } else {
        sortedNodesPtr = graphTNS_[graph.get()];
    }
    // 按拓扑序执行
    for (const auto &n : *sortedNodesPtr) {
        switch (n->type()) {
        case NodeType::Function: {
            func_ptr_t func = tt::as_shared<FunctionNode>(n)->func();
            auto arena = func->arena()->clone();
            auto subGraph = func->graph();
            data_ptr_t res = evalGraph(subGraph, arena);
            break;
        }
        case NodeType::Operator: {
            auto oper = tt::as_shared<OperatorNode>(n);
            break;
        }
        case NodeType::Select: {
            auto selectNode = tt::as_shared<SelectNode>(n);
            break;
        }
        default:
        }
    }
}

void FallbackExecSchedPass::evalNode(const node_ptr_t &node) {}

any FallbackExecSchedPass::apply(const graph_ptr_t &graph) {
    // evalGraph(graph);
    return nullptr;
}