// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "node_order.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>

namespace camel::transforms {

namespace rt = camel::runtime;

using rt::gc_node_ref_t;
using rt::GCGraph;
using rt::GCNode;
using rt::GCNodeKind;

namespace {

/// The data inputs of a node: the ordinary ports and the context ones. The effect runs are not
/// here on purpose -- a sync or order edge orders two statements but does not make one's *value*
/// the other's input, and a copy walk needs values.
void appendDataInputsOf(
    const GCGraph &graph, gc_node_ref_t ref, std::vector<gc_node_ref_t> &out) {
    const std::span<const gc_node_ref_t> norm = graph.normInputsOf(ref);
    out.insert(out.end(), norm.begin(), norm.end());
    const std::span<const gc_node_ref_t> with = graph.withInputsOf(ref);
    out.insert(out.end(), with.begin(), with.end());
}

} // namespace

std::vector<gc_node_ref_t> dependencyOrderOf(const GCGraph &graph) {
    const size_t places = graph.nodeCount();
    std::vector<gc_node_ref_t> order;
    order.reserve(places);
    if (places == 0) {
        return order;
    }
    // Only a node the graph has is in the order: a place a rewrite removed is `Dead`, and a ref
    // past the places is not a node at all.
    const auto isNode = [&graph, places](gc_node_ref_t ref) {
        if (ref >= places) {
            return false;
        }
        const GCNode *node = graph.node(ref);
        return node != nullptr && node->kind != GCNodeKind::Dead;
    };

    // How many of a node's data inputs are still to be placed, and who reads each node: Kahn's
    // walk, with the count kept per node rather than by searching the readers back out.
    std::vector<uint32_t> waiting(places, 0);
    std::vector<std::vector<gc_node_ref_t>> readers(places);
    std::vector<gc_node_ref_t> inputs;
    for (gc_node_ref_t ref = 0; ref < places; ++ref) {
        if (!isNode(ref)) {
            continue;
        }
        inputs.clear();
        appendDataInputsOf(graph, ref, inputs);
        for (gc_node_ref_t input : inputs) {
            if (!isNode(input)) {
                continue; // not a node: it places nothing and waits for nothing
            }
            ++waiting[ref];
            readers[input].push_back(ref);
        }
    }

    // Ready nodes, smallest place first: on a graph whose ids are a topological order this walk
    // reproduces the id order exactly (the smallest id still waiting is always the next one), so a
    // body the front end built is read in the order it used to be.
    std::priority_queue<gc_node_ref_t, std::vector<gc_node_ref_t>, std::greater<>> ready;
    for (gc_node_ref_t ref = 0; ref < places; ++ref) {
        if (isNode(ref) && waiting[ref] == 0) {
            ready.push(ref);
        }
    }
    while (!ready.empty()) {
        const gc_node_ref_t ref = ready.top();
        ready.pop();
        order.push_back(ref);
        for (gc_node_ref_t reader : readers[ref]) {
            if (waiting[reader] > 0 && --waiting[reader] == 0) {
                ready.push(reader);
            }
        }
    }
    // A cycle in the data edges is not a shape this graph model can honestly describe (a loop's
    // round feeds back through the region, not through a node input), and a walk that dropped the
    // nodes a cycle holds would answer a graph with holes in it. So the rest go in id order: the
    // order is then as good as the id order was, and the transform's own reading of the node says
    // what it can do with what it finds rather than the order deciding for it.
    if (order.size() < places) {
        std::vector<bool> placed(places, false);
        for (gc_node_ref_t ref : order) {
            placed[ref] = true;
        }
        for (gc_node_ref_t ref = 0; ref < places; ++ref) {
            if (isNode(ref) && !placed[ref]) {
                order.push_back(ref);
            }
        }
    }
    return order;
}

} // namespace camel::transforms