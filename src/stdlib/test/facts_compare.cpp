// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The two comparisons `test::facts_compare` makes (facts_compare.h).
 */

#include "facts_compare.h"

#include "camel/execute/pass/registration.h"
#include "camel/core/operator_table.h"
#include "camel/facts/arrangeable.h"
#include "camel/facts/control_regions.h"
#include "camel/runtime/control_regions.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"

#include <format>
#include <ostream>
#include <vector>

namespace {

using camel::runtime::GCGraph;
using camel::runtime::GCRegion;
using camel::runtime::GCRegionKind;
using camel::runtime::gc_node_ref_t;

/// The most control regions a graph may have for the pair-wise part of the comparison: every pair
/// is one item, and a graph of N control regions costs N*N of them. A graph over the bound still
/// compares its regions and nodes one for one; it prints `within_pairs=0`, which is what a case
/// that reads the count as a lower bound reports as a red rather than as a green over nothing.
constexpr uint32_t kMaxPairRegions = 512;

bool isControlRegion(GCRegionKind kind) {
    return kind == GCRegionKind::Arm || kind == GCRegionKind::LoopCond ||
           kind == GCRegionKind::LoopBody;
}

/// The answer the debugger computed for itself before `core.controlRegions` existed: the region
/// table read with `parent` and a kind test, and the two questions on top of it -- the innermost
/// control region a region is in, and whether one region is reached from another by walking
/// outward. It is here, and not in the debugger, because it is the baseline this comparison is
/// made against: the debugger asks `ControlRegions` now, and a baseline deleted with the copy it
/// replaced could not be compared with anything.
class OldRegionWalk {
  public:
    explicit OldRegionWalk(const GCGraph &graph) : graph_(graph) {
        outer_.assign(static_cast<size_t>(graph.regionCount()), 0);
        for (uint32_t r = 1; r < static_cast<uint32_t>(graph.regionCount()); ++r) {
            const GCRegion *region = graph.region(r);
            outer_[r]              = region == nullptr ? 0 : of(region->parent);
        }
    }

    /// The innermost arm or loop region `r` is in; 0 for the body.
    uint32_t of(uint32_t r) const {
        for (int guard = 0; r != 0 && guard < 4096; ++guard) {
            const GCRegion *region = graph_.region(r);
            if (region == nullptr) {
                return 0;
            }
            if (isControlRegion(region->kind)) {
                return r;
            }
            r = region->parent;
        }
        return 0;
    }

    /// The chain the debugger kept as `regionOuter`: by region, the control region it is in.
    uint32_t outer(uint32_t r) const { return r < outer_.size() ? outer_[r] : 0; }

    /// A name declared in `region` is there while the code runs in it or in a region inside it.
    bool visibleAt(uint32_t region, uint32_t at) const {
        for (int guard = 0; region != 0 && guard < 4096; ++guard) {
            if (at == region) {
                return true;
            }
            if (at == 0 || at >= outer_.size()) {
                return false;
            }
            at = outer_[at];
        }
        return region == 0;
    }

  private:
    const GCGraph &graph_;
    std::vector<uint32_t> outer_;
};

struct ControlCounts {
    size_t graphs          = 0;
    size_t regions         = 0;
    size_t enclosingSame   = 0;
    size_t enclosingDiff   = 0;
    size_t chainSame       = 0;
    size_t chainDiff       = 0;
    size_t withinPairs     = 0;
    size_t withinSame      = 0;
    size_t withinDiff      = 0;
    size_t nodesSame       = 0;
    size_t nodesDiff       = 0;
};

/// `core.controlRegions` against the walk above, item by item: the control region of every
/// region, the answer the debugger kept as the chain `regionOuter`, whether one control region is
/// within another (every pair), and the control region of every node. The node answer is compared
/// without the debugger's loop-port override -- that override says which region a port is *named*
/// in, and is the debugger's question, not the tree's.
///
/// Two of these are one answer asked twice, and both are compared because the debugger asked both:
/// `enclosing` is the innermost control region a region is in (a control region is in itself), and
/// the chain is the same answer for the *parent* -- `regionOuter[r]` was
/// `controlRegionOf(parent(r))`, which is what walking a name's visibility upward needs. Comparing
/// the chain with `enclosing[r]` would compare two questions: for a control region the two differ,
/// and a case that called that a difference would be red for the wrong reason.
///
/// The pairs are taken over the control regions and the body (`tree->order`), which is where both
/// answers are defined: a region that only annotates is looked through by the tree and has no
/// position in its walk, while the older walk would still have answered for it. Comparing there
/// would be comparing two questions, so the domain is named and the count says what it covered.
void compareControlRegions(
    const GCGraph &graph, camel::facts::Store &store, ControlCounts &counts) {
    const std::shared_ptr<const camel::facts::ControlRegionTree> tree =
        camel::facts::controlRegionsOf(store, graph);
    const camel::runtime::ControlRegions regions(graph);
    const OldRegionWalk older(graph);
    const uint32_t count = static_cast<uint32_t>(graph.regionCount());
    ++counts.graphs;

    for (uint32_t r = 1; r < count; ++r) {
        ++counts.regions;
        // The innermost control region `r` is in, the control region itself included.
        const uint32_t beforeEnclosing = older.of(r);
        const uint32_t afterEnclosing =
            r < tree->enclosing.size() ? tree->enclosing[r] : uint32_t{0};
        ++(beforeEnclosing == afterEnclosing ? counts.enclosingSame : counts.enclosingDiff);
        // The same answer for the parent: the chain the debugger kept as `regionOuter`.
        const GCRegion *region    = graph.region(r);
        const uint32_t parent     = region == nullptr ? 0 : region->parent;
        const uint32_t afterChain =
            parent == 0 || parent >= tree->enclosing.size() ? uint32_t{0} : tree->enclosing[parent];
        ++(older.outer(r) == afterChain ? counts.chainSame : counts.chainDiff);
    }

    if (tree->order.size() <= kMaxPairRegions) {
        for (size_t i = 1; i < tree->order.size(); ++i) {
            const uint32_t region = tree->order[i];
            for (const uint32_t at : tree->order) {
                ++counts.withinPairs;
                const bool before = older.visibleAt(region, at);
                const bool after  = regions.within(at, region);
                ++(before == after ? counts.withinSame : counts.withinDiff);
            }
        }
    }

    for (gc_node_ref_t ref = 0; ref < static_cast<gc_node_ref_t>(graph.nodeCount()); ++ref) {
        if (!graph.containsNodeRef(ref)) {
            continue;
        }
        const uint32_t before = older.of(graph.nodeRegion(ref));
        const uint32_t after  = regions.controlOf(ref);
        ++(before == after ? counts.nodesSame : counts.nodesDiff);
    }
}

struct ArrangeCounts {
    size_t graphs = 0;
    size_t nodes  = 0;
    size_t same   = 0;
    size_t diff   = 0;
    size_t noOld  = 0;
};

/// `core.arrangeable` against the algorithm it replaced, which answered OPER nodes only: the four
/// terms of a node's operator, read from the operator itself. A node whose kind the older
/// algorithm never answered is counted as `no_old` -- the fact answers it, and that count is part
/// of what putting the question into a fact added. An OPER node the fact has no answer for is a
/// `diff`: the old algorithm answered it, so the new one must too.
void compareArrangeable(
    const GCGraph &graph, camel::facts::Store &store, ArrangeCounts &counts) {
    const camel::graph::GraphView &view  = graph.view();
    const camel::facts::KindId kind      = camel::facts::arrangeableKind();
    ++counts.graphs;
    for (gc_node_ref_t ref = 0; ref < static_cast<gc_node_ref_t>(graph.nodeCount()); ++ref) {
        if (!graph.containsNodeRef(ref)) {
            continue;
        }
        ++counts.nodes;
        const camel::graph::NodeRef node = static_cast<camel::graph::NodeRef>(ref);
        if (view.kindOf(node) != camel::graph::NodeKind::Oper) {
            ++counts.noOld;
            continue;
        }
        const std::shared_ptr<const camel::facts::Arrangeable> fact =
            store.obtain<camel::facts::Arrangeable>(
                kind, camel::facts::Anchor{&graph, static_cast<camel::facts::NodeId>(ref)});
        const camel::facts::Arrangeable older =
            camel::facts::operatorArrangeable(view.operatorOf(node));
        const bool equal = fact != nullptr && *fact == older;
        ++(equal ? counts.same : counts.diff);
    }
}

struct ReaderCounts {
    size_t nodes  = 0;
    size_t agree  = 0;
    size_t diff   = 0;
    size_t noFact = 0;
};

/// The name of a node kind, as the reading prints it.
const char *nodeKindName(camel::graph::NodeKind kind) {
    switch (kind) {
    case camel::graph::NodeKind::Data:
        return "data";
    case camel::graph::NodeKind::Port:
        return "port";
    case camel::graph::NodeKind::Oper:
        return "oper";
    case camel::graph::NodeKind::Func:
        return "func";
    case camel::graph::NodeKind::Call:
        return "call";
    case camel::graph::NodeKind::Bind:
        return "bind";
    case camel::graph::NodeKind::Brch:
        return "brch";
    case camel::graph::NodeKind::Loop:
        return "loop";
    case camel::graph::NodeKind::Gate:
        return "gate";
    }
    return "?";
}

/// `core.arrangeable` against the predicate a pass asks before it drops, moves, merges or reorders
/// a node (`runtime::nodeIsFreelySchedulable`, camel/runtime/graph.h). The two answer one question
/// -- may a pass arrange this node anywhere -- so a node they answer differently is a node that has
/// two answers, and this is the reading that counts them. A node the kind answers nothing for is a
/// `no_fact`, separately from a disagreement: "the fact declined" and "the fact and the reader
/// disagree" are different states and one total would hide which happened.
void compareReaders(
    const GCGraph &graph, camel::facts::Store &store, ReaderCounts &counts, std::ostream &os) {
    const camel::graph::GraphView &view = graph.view();
    const camel::facts::KindId kind     = camel::facts::arrangeableKind();
    for (gc_node_ref_t ref = 0; ref < static_cast<gc_node_ref_t>(graph.nodeCount()); ++ref) {
        if (!graph.containsNodeRef(ref)) {
            continue;
        }
        ++counts.nodes;
        const camel::graph::NodeRef node = static_cast<camel::graph::NodeRef>(ref);
        const std::shared_ptr<const camel::facts::Arrangeable> fact =
            store.obtain<camel::facts::Arrangeable>(
                kind, camel::facts::Anchor{&graph, static_cast<camel::facts::NodeId>(ref)});
        if (fact == nullptr) {
            ++counts.noFact;
            continue;
        }
        const bool moved = camel::runtime::nodeIsFreelySchedulable(graph, ref);
        if (fact->all() == moved) {
            ++counts.agree;
            continue;
        }
        ++counts.diff;
        const camel::core::OperId oper = view.operatorOf(node);
        os << std::format(
            "FACTS_COMPARE reader_diff graph={} node={} node_kind={} oper={} kind={} moved={} "
            "terms={}{}{}{} waits={} orders={} syncs={}\n",
            graph.name(),
            ref,
            nodeKindName(view.kindOf(node)),
            oper == camel::core::kNoOper
                ? std::string("-")
                : std::string(camel::core::OperatorTable::instance().uri(oper)),
            fact->all() ? 1 : 0,
            moved ? 1 : 0,
            fact->pure ? 1 : 0,
            fact->noExternalEffect ? 1 : 0,
            fact->cannotFault ? 1 : 0,
            fact->terminates ? 1 : 0,
            graph.waitInputsOf(ref).size(),
            graph.orderPredsOf(ref).size(),
            graph.syncPredsOf(ref).size());
    }
}

} // namespace

const camel::runtime::Program *
FactsCompareTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    ControlCounts control;
    ArrangeCounts arrange;
    ReaderCounts readers;
    for (auto *graph : collectReachableGraphs(*program)) {
        if (graph == nullptr) {
            continue;
        }
        camel::facts::Store *store = camel::facts::storeHolding(*graph);
        if (store == nullptr) {
            // A graph no store holds has no tree to compare with, and saying so per graph is the
            // difference between "the comparison found nothing" and "the comparison could not
            // read this graph" (facts.h, storeHolding).
            os << std::format(
                "FACTS_COMPARE graph={} state=no-store\n", graph->name());
            continue;
        }
        compareControlRegions(*graph, *store, control);
        compareArrangeable(*graph, *store, arrange);
        compareReaders(*graph, *store, readers, os);
    }
    os << std::format(
        "FACTS_COMPARE kind=core.controlRegions graphs={} regions={} enclosing_same={} "
        "enclosing_diff={} chain_same={} chain_diff={} within_pairs={} within_same={} "
        "within_diff={} nodes_same={} nodes_diff={}\n",
        control.graphs,
        control.regions,
        control.enclosingSame,
        control.enclosingDiff,
        control.chainSame,
        control.chainDiff,
        control.withinPairs,
        control.withinSame,
        control.withinDiff,
        control.nodesSame,
        control.nodesDiff);
    os << std::format(
        "FACTS_COMPARE kind=core.arrangeable graphs={} nodes={} same={} diff={} no_old={}\n",
        arrange.graphs,
        arrange.nodes,
        arrange.same,
        arrange.diff,
        arrange.noOld);
    os << std::format(
        "FACTS_COMPARE kind=core.arrangeable readers nodes={} agree={} diff={} no_fact={}\n",
        readers.nodes,
        readers.agree,
        readers.diff,
        readers.noFact);
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The pass this file implements; the name is the constant facts_compare.h declares.
const PassKernel factsCompareKernel(kFactsCompareTestPass, makePass<FactsCompareTestPass>());

} // namespace