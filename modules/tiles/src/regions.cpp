/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// A connected GPU component must also be convex in the complete CPU/GPU DAG.
#include "tiles/internal/regions.h"
#include <algorithm>
#include <functional>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>

namespace camel::tiles {
std::vector<Region> partitionRegions(const std::vector<RegionNode> &nodes) {
    const auto count = nodes.size();
    std::vector<std::vector<size_t>> users(count);
    for (size_t id = 0; id < count; ++id)
        for (auto input : nodes[id].inputs) {
            if (input >= count)
                throw std::runtime_error("region dependency outside graph");
            users[input].push_back(id);
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
        pending.insert(pending.end(), nodes[id].inputs.begin(), nodes[id].inputs.end());
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
                for (auto input : nodes[id].inputs)
                    if (assignment[input] != assignment[id] &&
                        edges[assignment[input]].insert(assignment[id]).second)
                        ++incoming[assignment[id]];
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
            if (std::find(nodes[id].inputs.begin(), nodes[id].inputs.end(), id) !=
                nodes[id].inputs.end())
                throw std::runtime_error("cyclic region dependency graph");
    const auto order = topological(group);
    if (order.empty() && std::find(live.begin(), live.end(), true) != live.end())
        throw std::runtime_error("cyclic region dependency graph");
    // Greedy deterministic maximal contraction. Recheck remaining edges until no
    // legal contraction remains; global cost optimization belongs to scheduling.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto id : order)
            if (nodes[id].eligible)
                for (auto input : nodes[id].inputs) {
                    if (!nodes[input].eligible || group[id] == group[input])
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
    std::vector<Region> result;
    for (auto root : topological(group)) {
        Region region;
        std::set<size_t> inputs, outputs;
        for (auto id : order)
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
RegionTransfers
planRegionTransfers(const std::vector<RegionNode> &nodes, const std::vector<Region> &regions) {
    const size_t host = regions.size();
    std::vector<size_t> owner(nodes.size(), host);
    for (size_t r = 0; r < regions.size(); ++r)
        for (auto id : regions[r].members) {
            if (id >= nodes.size() || owner[id] != host)
                throw std::runtime_error("invalid transfer region membership");
            owner[id] = r;
        }
    std::vector<bool> live(nodes.size());
    std::vector<size_t> pending;
    for (size_t id = 0; id < nodes.size(); ++id)
        if (nodes[id].output)
            pending.push_back(id);
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (id >= nodes.size())
            throw std::runtime_error("transfer dependency outside graph");
        if (live[id])
            continue;
        live[id] = true;
        pending.insert(pending.end(), nodes[id].inputs.begin(), nodes[id].inputs.end());
    }
    std::set<size_t> uploads, downloads;
    RegionTransfers result;
    for (size_t r = 0; r < regions.size(); ++r) {
        for (auto id : regions[r].inputs) {
            if (id >= nodes.size() || owner[id] == r)
                throw std::runtime_error("invalid transfer region input");
            if (nodes[id].embedded)
                continue;
            if (owner[id] == host)
                uploads.insert(id);
            else {
                const auto &outputs = regions[owner[id]].outputs;
                if (owner[id] >= r ||
                    std::find(outputs.begin(), outputs.end(), id) == outputs.end())
                    throw std::runtime_error("resident input lacks an earlier region output");
                result.resident.push_back({id, owner[id], r});
            }
        }
        for (auto id : regions[r].outputs) {
            if (id >= nodes.size() || owner[id] != r)
                throw std::runtime_error("invalid transfer region output");
            if (nodes[id].output)
                downloads.insert(id);
        }
    }
    for (size_t id = 0; id < nodes.size(); ++id)
        if (live[id] && owner[id] == host)
            for (auto input : nodes[id].inputs)
                if (owner[input] != host) {
                    const auto &outputs = regions[owner[input]].outputs;
                    if (std::find(outputs.begin(), outputs.end(), input) == outputs.end())
                        throw std::runtime_error("host input lacks a region output");
                    downloads.insert(input);
                }
    result.uploads.assign(uploads.begin(), uploads.end());
    result.downloads.assign(downloads.begin(), downloads.end());
    return result;
}
} // namespace camel::tiles
