// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The checks `test::fact_slots` makes (fact_slots_check.h).
 */

#include "fact_slots_check.h"

#include "camel/execute/pass/registration.h"
#include "camel/facts/facts.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"

#include <format>
#include <memory>
#include <ostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using camel::facts::Anchor;
using camel::facts::KindId;
using camel::facts::Status;
using camel::runtime::GCGraph;
using camel::runtime::gc_node_ref_t;

struct Counts {
    size_t graphs = 0;
    size_t checks = 0;
    size_t wrong  = 0;
};

void check(std::ostream &os, Counts &counts, std::string_view what, bool ok) {
    ++counts.checks;
    if (!ok) {
        ++counts.wrong;
    }
    os << std::format("FACTS_SLOT {} {}\n", what, ok ? "ok" : "wrong");
}

/// The facts this pass reads: `core.controlRegions` is a Graph fact (its slot holds the tree) and
/// `core.arrangeable` a Node fact (its slot holds the dense array). Both are the core's kinds, so
/// the check registers nothing and reads what the pipeline before it computed.
constexpr const char *kGraphFactKey = "core.controlRegions";
constexpr const char *kNodeFactKey  = "core.arrangeable";

/// One graph, both shapes of slot: published when the store computes, cleared when that one fact
/// is invalidated, cleared when every fact about the graph goes, and published again afterwards.
void checkGraph(
    const GCGraph &graph,
    camel::facts::Store &store,
    KindId graphKind,
    KindId nodeKind,
    Counts &counts,
    std::ostream &os) {
    ++counts.graphs;
    const Anchor whole{&graph, camel::facts::kWholeGraph};
    const std::span<const Anchor> wholeAnchors(&whole, 1);
    bool haveNode = false;
    gc_node_ref_t at = 0;
    for (gc_node_ref_t ref = 0; ref < static_cast<gc_node_ref_t>(graph.nodeCount()); ++ref) {
        if (graph.containsNodeRef(ref)) {
            at       = ref;
            haveNode = true;
            break;
        }
    }
    const Anchor nodeAnchor{&graph, at};
    const std::span<const Anchor> nodeAnchors(&nodeAnchor, 1);

    // A Graph fact: the value itself goes in the slot.
    std::shared_ptr<const void> tree = store.obtainValue(graphKind, wholeAnchors);
    check(os, counts, "graph_slot_published", tree != nullptr && graph.factSlot(graphKind) == tree.get());

    store.invalidate(graphKind, wholeAnchors);
    check(os, counts, "graph_slot_cleared_on_invalidate", graph.factSlot(graphKind) == nullptr);
    check(
        os,
        counts,
        "graph_fact_absent_after_invalidate",
        store.probe(graphKind, wholeAnchors) == Status::Absent);

    tree = store.obtainValue(graphKind, wholeAnchors);
    check(os, counts, "graph_slot_republished", tree != nullptr && graph.factSlot(graphKind) == tree.get());

    // A Node fact: the dense array is the slot, and one node's entry is what the store computed.
    std::shared_ptr<const void> fact;
    if (haveNode) {
        fact = store.obtainValue(nodeKind, nodeAnchors);
        check(
            os,
            counts,
            "node_slot_published",
            fact != nullptr && graph.nodeFactSlot(nodeKind, at) == fact.get());
        store.invalidate(nodeKind, nodeAnchors);
        check(os, counts, "node_slot_cleared_on_invalidate", graph.nodeFactSlot(nodeKind, at) == nullptr);
        check(
            os,
            counts,
            "node_fact_absent_after_invalidate",
            store.probe(nodeKind, nodeAnchors) == Status::Absent);
        fact = store.obtainValue(nodeKind, nodeAnchors);
        check(
            os,
            counts,
            "node_slot_republished",
            fact != nullptr && graph.nodeFactSlot(nodeKind, at) == fact.get());
    }

    // Every fact about the graph goes, which is the call `~GCGraph` makes through
    // `facts::forgetGraph`: no slot of the graph may still name a value.
    store.invalidateGraph(&graph);
    check(os, counts, "graph_slot_cleared_on_invalidateGraph", graph.factSlot(graphKind) == nullptr);
    check(
        os,
        counts,
        "graph_fact_absent_after_invalidateGraph",
        store.probe(graphKind, wholeAnchors) == Status::Absent);
    if (haveNode) {
        check(
            os,
            counts,
            "node_slot_cleared_on_invalidateGraph",
            graph.nodeFactSlot(nodeKind, at) == nullptr);
        check(
            os,
            counts,
            "node_fact_absent_after_invalidateGraph",
            store.probe(nodeKind, nodeAnchors) == Status::Absent);
    }

    // Cleared is not "never written": both are published once more, and the store is left holding
    // what the pipeline after this pass would have asked for.
    tree = store.obtainValue(graphKind, wholeAnchors);
    check(os, counts, "graph_slot_republished_after_invalidateGraph", tree != nullptr && graph.factSlot(graphKind) == tree.get());
    if (haveNode) {
        fact = store.obtainValue(nodeKind, nodeAnchors);
        check(
            os,
            counts,
            "node_slot_republished_after_invalidateGraph",
            fact != nullptr && graph.nodeFactSlot(nodeKind, at) == fact.get());
    }
}

} // namespace

const camel::runtime::Program *
FactSlotsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    Counts counts;
    const KindId graphKind = camel::facts::findKind(kGraphFactKey);
    const KindId nodeKind  = camel::facts::findKind(kNodeFactKey);
    check(os, counts, "graph_kind_registered", graphKind != camel::facts::kNoKind);
    check(os, counts, "node_kind_registered", nodeKind != camel::facts::kNoKind);
    if (graphKind != camel::facts::kNoKind && nodeKind != camel::facts::kNoKind) {
        for (GCGraph *graph : collectReachableGraphs(*program)) {
            if (graph == nullptr) {
                continue;
            }
            camel::facts::Store *store = camel::facts::storeHolding(*graph);
            if (store == nullptr) {
                // A graph no store holds keeps no slots, and saying so per graph is the
                // difference between "no slot was wrong" and "no slot was read" (facts.h).
                os << std::format("FACTS_SLOT graph={} state=no-store\n", graph->name());
                continue;
            }
            checkGraph(*graph, *store, graphKind, nodeKind, counts, os);
        }
    }
    check(os, counts, "graphs_read", counts.graphs > 0);
    os << std::format(
        "FACTS_SLOT summary graphs={} checks={} wrong={}\n", counts.graphs, counts.checks, counts.wrong);
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The pass this file implements; the name is the constant fact_slots_check.h declares.
const PassKernel factSlotsKernel(kFactSlotsTestPass, makePass<FactSlotsTestPass>());

} // namespace