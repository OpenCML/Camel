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
#include "camel/runtime/graph_txn.h"
#include "camel/runtime/reachable.h"

#include <array>
#include <format>
#include <memory>
#include <ostream>
#include <vector>

namespace {

using camel::runtime::GCGraph;
using camel::runtime::GCRegion;
using camel::runtime::GCRegionKind;
using camel::runtime::gc_node_ref_t;
using camel::runtime::kInvalidNodeRef;

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
    const camel::graph::GraphView &view = graph.view();
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
        // The view's notion of a node, not the graph's: the place a JOIN or a dropped node holds is
        // a place of the numbering, and the fact layer refuses an anchor on one (`checkAnchors`:
        // "node N is not a node of its graph"). Counting those places here made the whole
        // comparison give up on any program that carries a branch.
        if (!view.contains(static_cast<camel::graph::NodeRef>(ref))) {
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
        if (!view.contains(static_cast<camel::graph::NodeRef>(ref))) {
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

/// The two answers one node got, counted: the four combinations of "the kind says freely
/// arrangeable" and "the predicate a pass asks says it may be moved", plus the nodes the kind had no
/// answer for. The four are separate and not two, because "they agree" has two states a total would
/// merge: a node both readers call free and a node both refuse are different answers that agree.
struct KindCounts {
    size_t nodes             = 0;
    size_t bothFree          = 0;
    size_t bothKept          = 0;
    size_t kindFreePredKept  = 0;
    size_t kindKeptPredFree  = 0;
    size_t noFact            = 0;
};

/// The nine node kinds, in the order the vocabulary lists them, with the name the reading prints,
/// and one row past them: a place whose kind the vocabulary does not name is counted there rather
/// than indexing past the table, which is what an answer to `kindOf` this code does not know about
/// would otherwise do.
constexpr size_t kNodeKindCount = 9;

/// Where one node kind sits in a `KindCounts` table. One function, so the table's order and the
/// name a line prints cannot drift apart.
size_t nodeKindIndex(camel::graph::NodeKind kind) {
    using camel::graph::NodeKind;
    switch (kind) {
    case NodeKind::Data:
        return 0;
    case NodeKind::Port:
        return 1;
    case NodeKind::Oper:
        return 2;
    case NodeKind::Func:
        return 3;
    case NodeKind::Call:
        return 4;
    case NodeKind::Bind:
        return 5;
    case NodeKind::Brch:
        return 6;
    case NodeKind::Loop:
        return 7;
    case NodeKind::Gate:
        return 8;
    }
    return kNodeKindCount;
}

/// The fourth reading: the same kind asked of a **draft**. `core.arrangeable` is a fact about the
/// graph an editing session is building as much as about the one a program holds (`facts.h`: "the
/// graph itself -- the frozen graph a program holds, or the graph an editing session is building"),
/// and until the framework could hold a draft only the frozen half could be asked. This comparison
/// decodes each graph into a transaction, asks the kind at every node of the *draft*, and reads:
///
///   `no_store`  graphs the framework answered no store for -- a draft it cannot hold, which is the
///               thing this comparison exists to catch, because a count of zero asked of a draft
///               that was never held would pass silently;
///   `same`/`diff`  the draft's answer at a node against the frozen graph's answer at the same node.
///               A decoded draft keeps the graph's numbering (`GraphDraft::keepsNodeIds`), so the
///               two are the same node and the count is per node, not per pair;
///   `kept`      nodes whose second ask was answered from what the store kept (`Status::Valid`), so
///               a store that recomputed every time would be visible here;
///   `stale`     graphs where an edit to the draft dropped the stored answer (`Status::Stale`). The
///               answer a draft gives must go away when the draft changes, and this is the only
///               reading that says the stamp is the draft's revision and not the constant a frozen
///               graph answers.
struct DraftCounts {
    size_t graphs = 0;
    size_t nodes  = 0;
    size_t same   = 0;
    size_t diff   = 0;
    size_t noFact = 0;
    size_t noStore = 0;
    size_t kept   = 0;
    size_t stale  = 0;
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

struct ReaderCounts {
    size_t nodes  = 0;
    size_t agree  = 0;
    size_t diff   = 0;
    size_t noFact = 0;
    /// The same counts, by the kind of node they were read at: one row per node kind, indexed by
    /// `nodeKindIndex`, plus the row past the nine for a place whose kind the vocabulary does not
    /// name. This is the table that says which kinds the two readers agree about; a total alone
    /// cannot say whether a kind was never read or was read and agreed.
    std::array<KindCounts, kNodeKindCount + 1> byKind{};
};

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
        const camel::graph::NodeRef node = static_cast<camel::graph::NodeRef>(ref);
        if (!view.contains(node)) {
            continue;
        }
        ++counts.nodes;
        const camel::graph::NodeKind kindOfNode = view.kindOf(node);
        KindCounts &row = counts.byKind[nodeKindIndex(kindOfNode)];
        ++row.nodes;
        const std::shared_ptr<const camel::facts::Arrangeable> fact =
            store.obtain<camel::facts::Arrangeable>(
                kind, camel::facts::Anchor{&graph, static_cast<camel::facts::NodeId>(ref)});
        if (fact == nullptr) {
            ++counts.noFact;
            ++row.noFact;
            continue;
        }
        const bool moved = camel::runtime::nodeIsFreelySchedulable(graph, ref);
        if (moved) {
            ++(fact->all() ? row.bothFree : row.kindKeptPredFree);
        } else {
            ++(fact->all() ? row.kindFreePredKept : row.bothKept);
        }
        if (fact->all() == moved) {
            ++counts.agree;
            continue;
        }
        ++counts.diff;
        const camel::core::OperId oper = view.operatorOf(node);
        os << std::format(
            "FACTS_COMPARE reader_diff graph={} node={} node_kind={} oper={} kind={} moved={} "
            "terms={}{}{}{} waits={} orders={} syncs={} reads={}\n",
            graph.name(),
            ref,
            nodeKindName(kindOfNode),
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
            graph.syncPredsOf(ref).size(),
            graph.normInputsOf(ref).size());
    }
}

/// The fourth reading (see `DraftCounts`): `core.arrangeable` asked of the draft this graph decodes
/// into, at every node, against the answer the frozen graph gave at the same node. A decoded draft
/// keeps the graph's numbering (`GraphDraft::keepsNodeIds`), so the two are one node and the count
/// is per node; a draft that does not keep them is counted as a `no_store` graph and named, because
/// pairing two different nodes and reporting agreement about nothing is the failure this reading
/// exists to catch.
void compareDraft(
    const camel::runtime::Program &program, const GCGraph &graph, camel::facts::Store &frozen,
    DraftCounts &counts, std::ostream &os) {
    const camel::facts::KindId kind = camel::facts::arrangeableKind();
    const std::unique_ptr<camel::runtime::GraphTransaction> transaction =
        camel::runtime::GraphTransaction::decode(program, &graph);
    if (!transaction) {
        ++counts.noStore;
        os << std::format("FACTS_COMPARE draft graph={} state=no-transaction\n", graph.name());
        return;
    }
    const camel::runtime::GraphDraft &draft = transaction->graph();
    ++counts.graphs;
    if (!draft.keepsNodeIds()) {
        ++counts.noStore;
        os << std::format("FACTS_COMPARE draft graph={} state=no-kept-ids\n", graph.name());
        return;
    }
    camel::facts::Store *store = camel::facts::storeHolding(draft);
    if (store == nullptr) {
        // The framework answered no store for the draft: the kind is not askable where only a
        // draft is at hand, which is the thing this reading was added to make visible.
        ++counts.noStore;
        os << std::format("FACTS_COMPARE draft graph={} state=no-store\n", graph.name());
        return;
    }
    gc_node_ref_t lastRef = kInvalidNodeRef;
    for (gc_node_ref_t ref = 0; ref < static_cast<gc_node_ref_t>(draft.nodeSlotCount()); ++ref) {
        if (!draft.alive(ref) || ref >= static_cast<gc_node_ref_t>(graph.nodeCount())) {
            continue;
        }
        ++counts.nodes;
        lastRef = ref;
        const camel::facts::Anchor anchor{&draft, static_cast<camel::facts::NodeId>(ref)};
        const std::shared_ptr<const camel::facts::Arrangeable> here =
            store->obtain<camel::facts::Arrangeable>(kind, anchor);
        const std::shared_ptr<const camel::facts::Arrangeable> there =
            frozen.obtain<camel::facts::Arrangeable>(
                kind, camel::facts::Anchor{&graph, static_cast<camel::facts::NodeId>(ref)});
        if (here == nullptr) {
            ++counts.noFact;
            os << std::format(
                "FACTS_COMPARE draft_diff graph={} node={} state=no-fact node_kind={}\n",
                graph.name(),
                ref,
                nodeKindName(draft.view().kindOf(static_cast<camel::graph::NodeRef>(ref))));
            continue;
        }
        // The second ask must be answered from what the store kept: a store whose stamp never
        // matched would recompute here, and this count is what would say so (facts.h, Status).
        if (store->probe(kind, anchor) == camel::facts::Status::Valid) {
            ++counts.kept;
        }
        const bool same = there != nullptr && *here == *there;
        ++(same ? counts.same : counts.diff);
        if (!same) {
            os << std::format(
                "FACTS_COMPARE draft_diff graph={} node={} node_kind={} draft={}{}{}{} "
                "frozen={}{}{}{}\n",
                graph.name(),
                ref,
                nodeKindName(draft.view().kindOf(static_cast<camel::graph::NodeRef>(ref))),
                here->pure ? 1 : 0,
                here->noExternalEffect ? 1 : 0,
                here->cannotFault ? 1 : 0,
                here->terminates ? 1 : 0,
                there == nullptr ? 0 : (there->pure ? 1 : 0),
                there == nullptr ? 0 : (there->noExternalEffect ? 1 : 0),
                there == nullptr ? 0 : (there->cannotFault ? 1 : 0),
                there == nullptr ? 0 : (there->terminates ? 1 : 0));
        }
    }
    // What the store answered about the draft must not outlive the draft's content: an edit drops
    // it. Adding one untyped value node is the cheapest edit that is a change of content
    // (`revision`, `GraphDraft::addNode`), and this transaction is the reading's own -- nothing
    // reads the draft afterwards.
    gc_node_ref_t added = kInvalidNodeRef;
    if (lastRef != kInvalidNodeRef) {
        added = transaction->edit().addDataNode(nullptr, 0);
    }
    if (added != kInvalidNodeRef &&
        store->probe(
            kind, camel::facts::Anchor{&draft, static_cast<camel::facts::NodeId>(lastRef)}) ==
            camel::facts::Status::Stale) {
        ++counts.stale;
    }
}

} // namespace

const camel::runtime::Program *
FactsCompareTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    ControlCounts control;
    ArrangeCounts arrange;
    ReaderCounts readers;
    DraftCounts draft;
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
        compareDraft(*program, *graph, *store, draft, os);
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
    // The table the total cannot give: which node kinds the two readers agreed about, and which
    // combination of their two answers each kind got. Printed for every kind the program carries,
    // and for the row past the nine only when something was counted there -- a kind that is absent
    // from the program prints no line, so a case that reads this table names the kinds it expects
    // instead of reading a missing line as agreement.
    static const char *kNodeKindNames[kNodeKindCount] = {
        "data", "port", "oper", "func", "call", "bind", "brch", "loop", "gate"};
    for (size_t row = 0; row < readers.byKind.size(); ++row) {
        const KindCounts &counts = readers.byKind[row];
        if (counts.nodes == 0) {
            continue;
        }
        os << std::format(
            "FACTS_COMPARE readers_kind kind={} nodes={} both_free={} both_kept={} "
            "kind_free_pred_kept={} kind_kept_pred_free={} no_fact={}\n",
            row < kNodeKindCount ? kNodeKindNames[row] : "?",
            counts.nodes,
            counts.bothFree,
            counts.bothKept,
            counts.kindFreePredKept,
            counts.kindKeptPredFree,
            counts.noFact);
    }
    os << std::format(
        "FACTS_COMPARE kind=core.arrangeable draft graphs={} nodes={} same={} diff={} no_fact={} "
        "no_store={} kept={} stale={}\n",
        draft.graphs,
        draft.nodes,
        draft.same,
        draft.diff,
        draft.noFact,
        draft.noStore,
        draft.kept,
        draft.stale);
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The pass this file implements; the name is the constant facts_compare.h declares.
const PassKernel factsCompareKernel(kFactsCompareTestPass, makePass<FactsCompareTestPass>());

} // namespace