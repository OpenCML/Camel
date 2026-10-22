// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Canonical runtime graph closure traversal.
 *
 * Runtime passes must traverse the materialized GCGraph closure rather than
 * re-discovering reachability from compile-time GIR ownership edges. The
 * compile graph remains available as metadata on each runtime graph, but the
 * closure itself is owned and defined by the runtime graph layer.
 */

#include "camel/runtime/reachable.h"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace camel::runtime {

void forEachReachableGraph(
    const Program &program, GCGraph *root, const std::function<void(GCGraph *)> &visitor) {
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
        program.traceGraphs(*graph, [&](GCGraph *next) { worklist.push(next); });
    }
}

uint64_t closureRevision(const Program &program, GCGraph *root) {
    std::vector<GCGraph *> graphs = collectReachableGraphs(program, root);
    std::sort(graphs.begin(), graphs.end(), [](const GCGraph *a, const GCGraph *b) {
        return toValue(a->id()) < toValue(b->id());
    });
    uint64_t h = 0xcbf29ce484222325ULL;
    const auto mix = [&h](uint64_t v) { h = (h ^ v) * 0x100000001b3ULL; };
    mix(toValue(root->id()));
    for (const GCGraph *graph : graphs) {
        mix(toValue(graph->id()));
        mix(graph->revision());
    }
    return h;
}

std::vector<GCGraph *> collectReachableGraphs(const Program &program) {
    return collectReachableGraphs(program, program.root());
}

std::vector<GCGraph *> collectReachableGraphs(const Program &program, GCGraph *root) {
    std::vector<GCGraph *> graphs;
    forEachReachableGraph(program, root, [&](GCGraph *graph) { graphs.push_back(graph); });
    return graphs;
}

} // namespace camel::runtime
