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
 * Created: Sep. 05, 2025
 * Updated: Sep. 05, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "common/graph.h"

#include <queue>

inline std::vector<GIR::node_ptr_t> typoSort(GIR::graph_ptr_t &graph) {
    using namespace GIR;
    const auto &nodes = graph->nodes();
    l.in("Topo").info(
        "Starting topological sort on graph '{}' with {} nodes.",
        graph->name(),
        nodes.size());

    std::unordered_map<node_ptr_t, size_t> inDegrees;
    std::queue<node_ptr_t> zeroInDegreeQueue;
    std::vector<node_ptr_t> sortedNodes;

    for (const auto &node : nodes) {
        l.in("Topo").debug("Visiting node: {}", node->toString());
        size_t indeg = node->inDegree();
        inDegrees[node] = indeg;
        if (indeg == 0) {
            l.in("Topo").debug("Push zero in-degree node: {}", node->toString());
            zeroInDegreeQueue.push(node);
        }
    }

    while (!zeroInDegreeQueue.empty()) {
        node_ptr_t current = zeroInDegreeQueue.front();
        zeroInDegreeQueue.pop();
        l.in("Topo").debug("Append node to sorted list: {}", current->toString());
        sortedNodes.push_back(current);

        for (const auto &neighbor : current->dataOutputs()) {
            if (--inDegrees[neighbor] == 0) {
                l.in("Topo").debug("New zero in-degree node: {}", neighbor->toString());
                zeroInDegreeQueue.push(neighbor);
            }
        }

        for (const auto &neighbor : current->ctrlOutputs()) {
            if (--inDegrees[neighbor] == 0) {
                l.in("Topo").debug("New zero in-degree node: {}", neighbor->toString());
                zeroInDegreeQueue.push(neighbor);
            }
        }
    }

    return sortedNodes;
}