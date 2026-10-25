// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A connected GPU component must also be convex in the complete CPU/GPU DAG.

#include "tiles/internal/regions.h"
#include <algorithm>
#include <functional>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>

namespace camel::tiles {
namespace {
/// Both kinds of dependency keep a node live and both order the quotient graph; only `inputs`
/// carry a value, and a region boundary is about values.
template <class Visit> void forEachDependency(const RegionNode &node, Visit &&visit) {
    for (auto id : node.inputs)
        visit(id);
    for (auto id : node.order)
        visit(id);
}
} // namespace

std::vector<Segment> partitionRegions(const std::vector<RegionNode> &nodes) {
    const auto count = nodes.size();
    std::vector<std::vector<size_t>> users(count);
    for (size_t id = 0; id < count; ++id) {
        for (auto input : nodes[id].inputs) {
            if (input >= count)
                throw std::runtime_error("region dependency outside graph");
            users[input].push_back(id);
        }
        for (auto predecessor : nodes[id].order)
            if (predecessor >= count)
                throw std::runtime_error("region dependency outside graph");
    }
    std::vector<bool> live(count);
    std::vector<size_t> pending;
    for (size_t id = 0; id < count; ++id)
        if (nodes[id].output)
            pending.push_back(id);
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (live[id])
            continue;
        live[id] = true;
        forEachDependency(nodes[id], [&](size_t input) { pending.push_back(input); });
    }
    std::vector<size_t> group(count);
    std::iota(group.begin(), group.end(), 0);
    // Check the quotient DAG after a proposed contraction. Parallel boundary edges
    // count once; their multiplicity is retained only in the original value graph.
    auto topological = [&](const std::vector<size_t> &assignment) {
        std::vector<std::set<size_t>> edges(count);
        std::vector<size_t> incoming(count);
        std::vector<bool> present(count);
        size_t groups = 0;
        for (size_t id = 0; id < count; ++id)
            if (live[id]) {
                if (!present[assignment[id]]) {
                    present[assignment[id]] = true;
                    ++groups;
                }
                forEachDependency(nodes[id], [&](size_t input) {
                    if (assignment[input] != assignment[id] &&
                        edges[assignment[input]].insert(assignment[id]).second)
                        ++incoming[assignment[id]];
                });
            }
        std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready;
        for (size_t id = 0; id < count; ++id)
            if (present[id] && !incoming[id])
                ready.push(id);
        std::vector<size_t> order;
        while (!ready.empty()) {
            auto id = ready.top();
            ready.pop();
            order.push_back(id);
            for (auto user : edges[id])
                if (--incoming[user] == 0)
                    ready.push(user);
        }
        if (order.size() != groups)
            order.clear();
        return order;
    };
    // Self-edges must not be mistaken for the legitimate internal edges of a group.
    for (size_t id = 0; id < count; ++id)
        if (live[id])
            forEachDependency(nodes[id], [&](size_t input) {
                if (input == id)
                    throw std::runtime_error("cyclic region dependency graph");
            });
    const auto walk = topological(group);
    if (walk.empty() && std::find(live.begin(), live.end(), true) != live.end())
        throw std::runtime_error("cyclic region dependency graph");
    // Greedy deterministic maximal contraction. Recheck remaining edges until no
    // legal contraction remains; global cost optimization belongs to scheduling.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto id : walk)
            if (nodes[id].eligible)
                for (auto input : nodes[id].inputs) {
                    if (!nodes[input].eligible || group[id] == group[input])
                        continue;
                    // A region never spans two control regions: an arm runs only when it is
                    // taken, so merging it with the code around it would run that code too.
                    if (nodes[id].control != nodes[input].control)
                        continue;
                    const auto a = std::min(group[id], group[input]),
                               b = std::max(group[id], group[input]);
                    auto trial   = group;
                    for (auto &g : trial)
                        if (g == b)
                            g = a;
                    if (topological(trial).empty())
                        continue;
                    group   = std::move(trial);
                    changed = true;
                }
    }
    std::vector<Segment> result;
    for (auto root : topological(group)) {
        Segment region;
        std::set<size_t> inputs, outputs;
        for (auto id : walk)
            if (group[id] == root && nodes[id].eligible) {
                region.members.push_back(id);
                for (auto input : nodes[id].inputs)
                    if (group[input] != root)
                        inputs.insert(input);
                if (nodes[id].output)
                    outputs.insert(id);
                for (auto user : users[id])
                    if (live[user] && group[user] != root)
                        outputs.insert(id);
            }
        if (region.members.empty())
            continue;
        region.inputs.assign(inputs.begin(), inputs.end());
        region.outputs.assign(outputs.begin(), outputs.end());
        result.push_back(std::move(region));
    }
    return result;
}
} // namespace camel::tiles
