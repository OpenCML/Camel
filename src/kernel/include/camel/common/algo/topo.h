// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/utils/assert.h"
#include "camel/utils/debug.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <queue>
#include <unordered_map>
#include <unordered_set>
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

    for (auto it = first; it != last; ++it) {
        NodeType node   = *it;
        size_t inDeg    = getInDegree(node);
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

    EXEC_WHEN_DEBUG({
        size_t count = std::distance(first, last);
        ASSERT(sortedNodes.size() <= count, "Graph has at least one cycle.");
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
    });

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
