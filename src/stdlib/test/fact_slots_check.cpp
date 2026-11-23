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
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using camel::facts::Anchor;
using camel::facts::KindId;
using camel::facts::Status;
using camel::runtime::FactSlotTable;
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
/// the check registers nothing for them and reads what the pipeline before it computed.
constexpr const char *kGraphFactKey = "core.controlRegions";
constexpr const char *kNodeFactKey  = "core.arrangeable";

/// The id a fixed table would have stopped below: a kind registered at or past it is a kind the
/// table has to grow to hold, and a slot published for it is the evidence that it grew.
constexpr std::uint32_t kPastAFixedTable = 32;

/// The value of a kind this check registers for itself. Nothing reads it but the slot it lands in.
struct SlotProbe {
    std::uint32_t id = 0;
    bool operator==(const SlotProbe &) const = default;
};

/// Registers a Graph-scoped kind of this check's own. The registry is process-wide and append-only,
/// so these kinds stay registered for the process; each check names the kind it wrote, and none of
/// them is read by anything but this pass.
KindId registerProbeKind(const std::string &key) {
    return camel::facts::registerKind<SlotProbe>(camel::facts::Kind<SlotProbe>{
        .key        = key,
        .owner      = "test",
        .version    = 1,
        .cls        = camel::facts::Class::Derived,
        .scope      = camel::facts::Scope::Graph,
        .dependence = camel::facts::Dependence::Reported,
        .monotone   = false,
        .compute =
            [](std::span<const Anchor>, const camel::facts::GraphAccess &, camel::facts::ReadLog &) {
                return std::optional<SlotProbe>(SlotProbe{});
            },
        .merge     = {},
        .print     = [](const SlotProbe &) { return std::string("slot-probe"); },
        .anchorsIn = {},
    });
}

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
        std::vector<GCGraph *> graphs;
        for (GCGraph *graph : collectReachableGraphs(*program)) {
            if (graph != nullptr) {
                graphs.push_back(graph);
            }
        }

        // What a graph pays for slots is what its facts made. The state the design calls "no
        // table" -- a frozen graph nothing has asked a fact about -- cannot be built from a pass:
        // the freeze itself writes one into every graph it encodes (`core.controlRegions`, through
        // `validateRuntimeGraphPayload`'s branch-arm check in graph_encode.cpp), so a graph of a
        // compiled program has a table before any pass sees it. What is read here instead is the
        // size of that table: it is the kinds registered when its first fact was written, and
        // registering more kinds does not make it larger.
        std::vector<std::uint32_t> atPassStart;
        for (GCGraph *graph : graphs) {
            const std::uint32_t capacity = graph->factSlotCapacity();
            atPassStart.push_back(capacity);
            os << std::format(
                "FACTS_SLOT graph={} capacity={} registered={}\n",
                graph->name(),
                capacity,
                camel::facts::registeredKinds().size());
            check(
                os,
                counts,
                "table_smaller_than_the_fixed_thirty_two",
                capacity < kPastAFixedTable);
        }

        // A fixed table stopped below thirty-two kinds. The registry is raised past that number
        // here, so that a slot published for a kind at or past `kPastAFixedTable` is a slot a
        // fixed table could not have had, whatever the core registered before this pass ran.
        std::uint32_t registered = 0;
        const auto nextProbe     = [&registered]() {
            const std::string key = std::format("test.fact_slots_probe_{}", registered);
            ++registered;
            return registerProbeKind(key);
        };
        while (camel::facts::registeredKinds().size() <= kPastAFixedTable) {
            (void)nextProbe();
        }
        const KindId pastAFixedTable = static_cast<KindId>(camel::facts::registeredKinds().size() - 1);
        check(os, counts, "kind_registered_past_thirty_two", pastAFixedTable >= kPastAFixedTable);

        for (size_t index = 0; index < graphs.size(); ++index) {
            GCGraph *graph             = graphs[index];
            camel::facts::Store *store = camel::facts::storeHolding(*graph);
            if (store == nullptr) {
                // A graph no store holds keeps no slots, and saying so per graph is the
                // difference between "no slot was wrong" and "no slot was read" (facts.h).
                os << std::format("FACTS_SLOT graph={} state=no-store\n", graph->name());
                continue;
            }
            // Registration alone does not grow a graph's table, and a kind nobody has written has
            // no slot: the read that finds no slot is the one that asks the store, and this is
            // that read before the write that will make one.
            check(
                os,
                counts,
                "table_not_grown_by_registration",
                graph->factSlotCapacity() == atPassStart[index]);
            check(
                os,
                counts,
                "kind_without_a_fact_has_no_slot",
                graph->factSlot(pastAFixedTable) == nullptr);

            checkGraph(*graph, *store, graphKind, nodeKind, counts, os);

            const Anchor whole{graph, camel::facts::kWholeGraph};
            const std::span<const Anchor> wholeAnchors(&whole, 1);

            // A slot for a kind past thirty-two: the table grew past where a fixed one stopped,
            // and the read finds the value there instead of falling back to the store.
            std::shared_ptr<const void> probe = store->obtainValue(pastAFixedTable, wholeAnchors);
            check(
                os,
                counts,
                "slot_past_thirty_two",
                probe != nullptr && graph->factSlot(pastAFixedTable) == probe.get());
            check(
                os,
                counts,
                "table_covers_registered_kinds",
                graph->factSlotCapacity() > pastAFixedTable);

            // A growth, and a reader that took the table before it. The kind written next is
            // registered after this graph's table was made, so writing it makes a larger table and
            // retires the one the check is holding; the read through the held table is the read a
            // reader that loaded it a moment earlier makes.
            const FactSlotTable *before = graph->factSlotTable();
            const KindId later          = nextProbe();
            std::shared_ptr<const void> grown = store->obtainValue(later, wholeAnchors);
            check(
                os,
                counts,
                "table_grown_for_later_kind",
                before != nullptr && graph->factSlotTable() != before &&
                    graph->factSlotCapacity() > later && graph->factSlot(later) == grown.get());
            check(
                os,
                counts,
                "old_table_read_after_growth",
                before != nullptr && graph->factSlotInTable(before, pastAFixedTable) == probe.get() &&
                    graph->factSlotInTable(before, later) == nullptr);
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