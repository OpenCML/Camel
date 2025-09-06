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

#include <functional>
#include <iterator>
#include <queue>
#include <unordered_map>
#include <vector>

template <typename InputIt, typename GetInDegreeFunc, typename GetOutputsFunc>
auto topoSort(InputIt first, InputIt last, GetInDegreeFunc getInDegree, GetOutputsFunc getOutputs)
    -> std::vector<typename std::iterator_traits<InputIt>::value_type> {
    using NodeType = typename std::iterator_traits<InputIt>::value_type;

    std::unordered_map<NodeType, size_t> inDegrees;
    std::queue<NodeType> zeroInDegreeQueue;
    std::vector<NodeType> sortedNodes;

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

    return sortedNodes;
}
