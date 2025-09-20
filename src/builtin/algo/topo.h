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

#include "utils/assert.h"
#include "utils/debug.h"

#include <functional>
#include <iterator>
#include <queue>
#include <unordered_map>
#include <vector>

template <typename InputIt, typename GetInDegreeFunc, typename GetOutputsFunc>
auto topoSort(
    InputIt first, InputIt last, GetInDegreeFunc getInDegree, GetOutputsFunc getOutputs,
    bool allowUnreachable = false)
    -> std::vector<typename std::iterator_traits<InputIt>::value_type> {
    using NodeType = typename std::iterator_traits<InputIt>::value_type;

    std::unordered_map<NodeType, size_t> inDegrees;
    std::queue<NodeType> zeroInDegreeQueue;
    std::vector<NodeType> sortedNodes;

#ifndef NDEBUG
    size_t count = std::distance(first, last);
#endif

    for (auto it = first; it != last; ++it) {
        NodeType node = *it;
        size_t inDeg = getInDegree(node);
        inDegrees[node] = inDeg;
        if (inDeg == 0) {
            zeroInDegreeQueue.push(node);
        }
    }

    while (!zeroInDegreeQueue.empty()) {
        NodeType current = zeroInDegreeQueue.front();
        zeroInDegreeQueue.pop();
        sortedNodes.push_back(current);

        for (const auto &neighbor : getOutputs(current)) {
            if (--inDegrees[neighbor] == 0) {
                zeroInDegreeQueue.push(neighbor);
            }
        }
    }

    ASSERT(sortedNodes.size() <= count, "Graph has at least one cycle.");
    EXEC_WHEN_DEBUG([&]() {
        if (!allowUnreachable) {
            if (sortedNodes.size() != count) {
                // Find unreachable nodes
                std::vector<NodeType> unreachableNodes;
                for (auto it = first; it != last; ++it) {
                    NodeType node = *it;
                    if (inDegrees[node] > 0) {
                        unreachableNodes.push_back(node);
                    }
                }
                std::string msg = "Unreachable nodes detected: ";
                for (const auto &node : unreachableNodes) {
                    msg += node->toString() + ", ";
                }
                ASSERT(false, msg);
            }
        }
    }());

    return sortedNodes;
}

template <typename NodeType, typename GetInputsFunc>
std::vector<NodeType> findReachable(
    const NodeType &startNode, GetInputsFunc getInputs, bool skipStart = false,
    bool reverse = false) {
    std::unordered_set<NodeType> visited;
    std::vector<NodeType> reachableNodes;

    // DFS to find all upstream nodes
    std::function<void(const NodeType &)> dfs = [&](const NodeType &node) {
        for (const auto &input : getInputs(node)) {
            if (visited.insert(input).second) {
                dfs(input);
            }
        }
        // Add to result after processing dependencies
        reachableNodes.push_back(node);
    };

    visited.insert(startNode);

    if (skipStart) {
        // Start DFS from its inputs only (skip adding startNode itself)
        for (const auto &input : getInputs(startNode)) {
            if (visited.insert(input).second) {
                dfs(input);
            }
        }
    } else {
        dfs(startNode);
    }

    if (reverse) {
        std::reverse(reachableNodes.begin(), reachableNodes.end());
    }

    return reachableNodes;
}
