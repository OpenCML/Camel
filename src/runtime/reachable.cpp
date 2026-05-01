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
 * Created: Apr. 08, 2026
 * Updated: Apr. 08, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Canonical runtime graph closure traversal.
 *
 * Runtime passes must traverse the materialized GCGraph closure rather than
 * re-discovering reachability from compile-time GIR ownership edges. The
 * compile graph remains available as metadata on each runtime graph, but the
 * closure itself is owned and defined by the runtime graph layer.
 */

#include "camel/runtime/reachable.h"

#include <queue>
#include <unordered_set>

namespace camel::runtime {

void forEachReachableGraph(GCGraph *root, const std::function<void(GCGraph *)> &visitor) {
    if (!root) {
        return;
    }

    std::queue<GCGraph *> worklist;
    std::unordered_set<GCGraph *> visited;
    worklist.push(root);

    while (!worklist.empty()) {
        GCGraph *graph = worklist.front();
        worklist.pop();
        if (!graph || !visited.insert(graph).second) {
            continue;
        }

        visitor(graph);
        graph->traceGraphs([&](GCGraph *next) { worklist.push(next); });
    }
}

std::vector<GCGraph *> collectReachableGraphs(GCGraph *root) {
    std::vector<GCGraph *> graphs;
    forEachReachableGraph(root, [&](GCGraph *graph) { graphs.push_back(graph); });
    return graphs;
}

} // namespace camel::runtime
