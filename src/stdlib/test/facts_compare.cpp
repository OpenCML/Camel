// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The two comparisons `test::facts_compare` makes (facts_compare.h).
 */

#include "facts_compare.h"

#include "camel/core/operator_table.h"
#include "camel/execute/pass/registration.h"
#include "camel/facts/arrangeable.h"
#include "camel/facts/control_regions.h"
#include "camel/runtime/control_regions.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"
#include "camel/runtime/oper_shape.h"
#include "camel/runtime/reachable.h"

#include <array>
#include <format>
#include <map>
#include <memory>
#include <ostream>
#include <span>
#include <string>
#include <vector>

namespace {

using camel::runtime::gc_node_ref_t;
using camel::runtime::GCGraph;
using camel::runtime::GCRegion;
using camel::runtime::GCRegionKind;
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
    size_t graphs        = 0;
    size_t regions       = 0;
    size_t enclosingSame = 0;
    size_t enclosingDiff = 0;
    size_t chainSame     = 0;
    size_t chainDiff     = 0;
    size_t withinPairs   = 0;
    size_t withinSame    = 0;
    size_t withinDiff    = 0;
    size_t nodesSame     = 0;
    size_t nodesDiff     = 0;
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
    const uint32_t count                = static_cast<uint32_t>(graph.regionCount());
    ++counts.graphs;

    for (uint32_t r = 1; r < count; ++r) {
        ++counts.regions;
        // The innermost control region `r` is in, the control region itself included.
        const uint32_t beforeEnclosing = older.of(r);
        const uint32_t afterEnclosing =
            r < tree->enclosing.size() ? tree->enclosing[r] : uint32_t{0};
        ++(beforeEnclosing == afterEnclosing ? counts.enclosingSame : counts.enclosingDiff);
        // The same answer for the parent: the chain the debugger kept as `regionOuter`.
        const GCRegion *region = graph.region(r);
        const uint32_t parent  = region == nullptr ? 0 : region->parent;
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
        // The view's notion of a node, for the reason `censusArrangeable` gives below: the place a
        // JOIN holds is a place of the numbering, and the fact layer refuses an anchor on one.
        if (!view.contains(static_cast<camel::graph::NodeRef>(ref))) {
            continue;
        }
        const uint32_t before = older.of(graph.nodeRegion(ref));
        const uint32_t after  = regions.controlOf(ref);
        ++(before == after ? counts.nodesSame : counts.nodesDiff);
    }
}

struct ArrangeCounts {
    size_t graphs      = 0;
    size_t nodes       = 0;
    size_t same        = 0;
    size_t diff        = 0;
    size_t noOld       = 0;
    size_t conversions = 0;
};

/// `core.arrangeable` against the algorithm it replaced, which answered OPER nodes only: the four
/// terms of a node's operator, read from the operator itself. A node whose kind the older
/// algorithm never answered is counted as `no_old` -- the fact answers it, and that count is part
/// of what putting the question into a fact added. An OPER node the fact has no answer for is a
/// `diff`: the old algorithm answered it, so the new one must too.
///
/// A **conversion** is the one node the operator's four terms do not state on their own, and the
/// algorithm that was replaced read the pair of types the node converts between for it
/// (`castMayFault`, which the pass predicate and the compiler both ask). The older answer is
/// therefore restated here at the node, for a conversion only, instead of being taken from
/// `operatorArrangeable` -- which has an operator and no types, and could only report the
/// operator's half ("a cast may fault"). That restatement is the baseline, so it lives here beside
/// the comparison it is the baseline of, the way `OldRegionWalk` does above; and `conversions`
/// counts the nodes it applied to, so a run that walked none of them says so instead of reading
/// like a comparison that covered them.
void compareArrangeable(const GCGraph &graph, camel::facts::Store &store, ArrangeCounts &counts) {
    const camel::graph::GraphView &view = graph.view();
    const camel::facts::KindId kind     = camel::facts::arrangeableKind();
    ++counts.graphs;
    for (gc_node_ref_t ref = 0; ref < static_cast<gc_node_ref_t>(graph.nodeCount()); ++ref) {
        const camel::graph::NodeRef node = static_cast<camel::graph::NodeRef>(ref);
        // The view's notion of a node, for the reason `censusArrangeable` gives below.
        if (!view.contains(node)) {
            continue;
        }
        ++counts.nodes;
        if (view.kindOf(node) != camel::graph::NodeKind::Oper) {
            ++counts.noOld;
            continue;
        }
        const std::shared_ptr<const camel::facts::Arrangeable> fact =
            store.obtain<camel::facts::Arrangeable>(
                kind,
                camel::facts::Anchor{&graph, static_cast<camel::facts::NodeId>(ref)});
        camel::facts::Arrangeable older = camel::facts::operatorArrangeable(view.operatorOf(node));
        if (camel::runtime::shapeOfOperator(view.operatorOf(node)) ==
            camel::ops::OperShape::Conversion) {
            const std::span<const camel::graph::NodeRef> norm =
                view.inputs(node, camel::graph::PortGroup::Normal);
            older.cannotFault = !camel::runtime::castMayFault(
                view.typeOf(node),
                norm.size() == 1 ? view.typeOf(norm[0]) : nullptr);
            ++counts.conversions;
        }
        const bool equal = fact != nullptr && *fact == older;
        ++(equal ? counts.same : counts.diff);
    }
}

/// One cell of the answer table: how many nodes of one row -- a node kind, a family, or a kind and
/// an operator together -- the fact answered "freely arrangeable", how many it answered "no", and
/// how many it had no answer for at all. `free` and `kept` are kept apart rather than summed: "a
/// pass may move it anywhere" and "a pass must leave it where the program wrote it" are different
/// readings of one row, and one total would merge them. `keptExample` is the first node of the row
/// the fact kept, as `<graph>:<node>`: a count says a row is split, an example says which node to
/// look at.
struct AnswerCell {
    size_t nodes  = 0;
    size_t free   = 0;
    size_t kept   = 0;
    size_t noFact = 0;
    std::string keptExample;

    void add(const camel::facts::Arrangeable *fact, const std::string &at) {
        ++nodes;
        if (fact == nullptr) {
            ++noFact;
            return;
        }
        if (fact->all()) {
            ++free;
            return;
        }
        ++kept;
        if (keptExample.empty()) {
            keptExample = at;
        }
    }
};

/// The nine node kinds, in the order the vocabulary lists them, with the name the reading prints,
/// and one row past them: a place whose kind the vocabulary does not name is counted there rather
/// than indexing past the table, which is what an answer to `kindOf` this code does not know about
/// would otherwise do.
constexpr size_t kNodeKindCount = 9;

/// Where one node kind sits in a kind table. One function, so the table's order and the name a line
/// prints cannot drift apart.
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

/// The families the table counts, and the index each one has. What a family *means* -- which term
/// of design principles 3.4 a row reports, and which node kinds answer without an operator record
/// -- is the roster in `facts_compare.h`, kept there and not repeated here so the two cannot say
/// different things. What this enumeration fixes is the order the lines print in and the index
/// `familyIndex` returns: `value` first for the kind that is most of every program, then the
/// operator families in the order 3.4 states its terms, then the kinds whose answer is not a
/// record's.
constexpr size_t kFamilyCount                    = 9;
constexpr const char *kFamilyNames[kFamilyCount] = {
    "value",
    "oper",
    "oper_may_fault",
    "oper_not_free",
    "oper_no_record",
    "call",
    "brch",
    "loop",
    "gate"};

/// Where a node sits in the family table. The node kind decides for every kind but OPER, where the
/// fact's own terms say which one fails: a fault is the first term a placement decision acts on,
/// and a write or a reach outside the program the second. A node the fact had no answer for is
/// placed by its operator alone, so that a family never loses a node because the fact declined.
size_t familyIndex(
    camel::graph::NodeKind kind, camel::core::OperId oper, const camel::facts::Arrangeable *fact) {
    using camel::graph::NodeKind;
    switch (kind) {
    case NodeKind::Data:
    case NodeKind::Port:
    case NodeKind::Bind:
        return 0;
    case NodeKind::Oper: {
        // An OPER node the view can name no operator for is a FILL or an ACCS: it is its own family
        // rather than a fault or a write, because the answer is not an operator record's.
        if (oper == camel::core::kNoOper) {
            return 4;
        }
        if (fact == nullptr) {
            return 1;
        }
        if (!fact->cannotFault) {
            return 2;
        }
        return fact->all() ? 1 : 3;
    }
    case NodeKind::Call:
    case NodeKind::Func:
        return 5;
    case NodeKind::Brch:
        return 6;
    case NodeKind::Loop:
        return 7;
    case NodeKind::Gate:
        return 8;
    }
    return kFamilyCount;
}

/// The table keyed by (node kind name, operator URI or "-"). A `std::map` and not a hash table: the
/// reading is printed, and two runs that print the same rows in a different order are two readings
/// nobody can diff.
using AnswerTally = std::map<std::pair<std::string, std::string>, AnswerCell>;

/// The fact's answers over one graph: the totals, and the same answers cut by node kind, by the
/// family the answer comes from, and by (kind, operator). This is the reading the plans pin -- what
/// the fact says about each node, in the terms the fact itself states. There is nothing here to
/// compare it with: `core.arrangeable` is the one answer to "may a pass arrange this node
/// anywhere", and a second copy of it kept for a test to agree with would be a second statement of
/// the same answer.
struct AnswerCounts {
    size_t graphs = 0;
    size_t nodes  = 0;
    size_t free   = 0;
    size_t kept   = 0;
    size_t noFact = 0;
    std::array<AnswerCell, kNodeKindCount + 1> byKind{};
    std::array<AnswerCell, kFamilyCount + 1> byFamily{};
    AnswerTally byRow{};
};

/// `core.arrangeable` asked, through the store, at every node the view calls a node: one node is
/// one row of each table above. The three tables answer three questions a single total cannot: the
/// kind rows say *what* the program carries, the family rows say *which term of design
/// principles 3.4* the kept nodes failed, and the (kind, operator) rows say which operator a kept
/// node was running.
///
/// The walk goes over the view's nodes and not over the graph's numbering: a JOIN holds a place of
/// the numbering, and the fact layer refuses an anchor on one (`checkAnchors`: "node N is not a
/// node of its graph"), so counting those places made the pass give up on any program that carries
/// a branch -- and a program it gave up on is a program no reading covered.
void censusArrangeable(const GCGraph &graph, camel::facts::Store &store, AnswerCounts &counts) {
    const camel::graph::GraphView &view = graph.view();
    const camel::facts::KindId kind     = camel::facts::arrangeableKind();
    ++counts.graphs;
    for (gc_node_ref_t ref = 0; ref < static_cast<gc_node_ref_t>(graph.nodeCount()); ++ref) {
        const camel::graph::NodeRef node = static_cast<camel::graph::NodeRef>(ref);
        if (!view.contains(node)) {
            continue;
        }
        const camel::graph::NodeKind kindOfNode = view.kindOf(node);
        const camel::core::OperId operOfNode    = view.operatorOf(node);
        const std::shared_ptr<const camel::facts::Arrangeable> fact =
            store.obtain<camel::facts::Arrangeable>(
                kind,
                camel::facts::Anchor{&graph, static_cast<camel::facts::NodeId>(ref)});
        ++counts.nodes;
        if (fact == nullptr) {
            ++counts.noFact;
        } else if (fact->all()) {
            ++counts.free;
        } else {
            ++counts.kept;
        }
        const std::string at = std::format("{}:{}", graph.name(), ref);
        counts.byKind[nodeKindIndex(kindOfNode)].add(fact.get(), at);
        counts.byFamily[familyIndex(kindOfNode, operOfNode, fact.get())].add(fact.get(), at);
        const std::string operName =
            operOfNode == camel::core::kNoOper
                ? std::string("-")
                : std::string(camel::core::OperatorTable::instance().uri(operOfNode));
        counts.byRow[{nodeKindName(kindOfNode), operName}].add(fact.get(), at);
    }
}
/// The fourth reading: the same kind asked of a **draft**. `core.arrangeable` is a fact about the
/// graph an editing session is building as much as about the one a program holds (`facts.h`: "the
/// graph itself -- the frozen graph a program holds, or the graph an editing session is building"),
/// and until the framework could hold a draft only the frozen half could be asked. This reading
/// decodes each graph into a transaction, asks the kind at every node of the *draft*, and reads:
///
///   `no_store`  graphs the framework answered no store for -- a draft it cannot hold, which is the
///               thing this reading exists to catch, because a count of zero asked of a draft that
///               was never held would pass silently;
///   `same`/`diff`  the draft's answer at a node against the frozen graph's answer at the same
///   node.
///               A decoded draft keeps the graph's numbering (`GraphDraft::keepsNodeIds`), so the
///               two are the same node and the count is per node, not per pair;
///   `kept`      nodes whose second ask was answered from what the store kept (`Status::Valid`), so
///   a
///               store that recomputed every time would be visible here;
///   `stale`     graphs where an edit to the draft dropped the stored answer (`Status::Stale`). The
///               answer a draft gives must go away when the draft changes, and this is the only
///               reading that says the stamp is the draft's revision and not the constant a frozen
///               graph answers.
struct DraftCounts {
    size_t graphs  = 0;
    size_t nodes   = 0;
    size_t same    = 0;
    size_t diff    = 0;
    size_t noFact  = 0;
    size_t noStore = 0;
    size_t kept    = 0;
    size_t stale   = 0;
};

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
    gc_node_ref_t lastRef                     = kInvalidNodeRef;
    const camel::graph::GraphView &frozenView = graph.view();
    const camel::graph::GraphView &draftView  = draft.view();
    for (gc_node_ref_t ref = 0; ref < static_cast<gc_node_ref_t>(draft.nodeSlotCount()); ++ref) {
        // The places the view calls nodes, on both sides, for the reason the three walks above
        // give: a JOIN holds a place of the numbering, the fact layer refuses an anchor on one
        // ("node N is not a node of its graph"), and a walk that counted those places made the
        // whole pass give up with an internal error on every program that carries a branch.
        const camel::graph::NodeRef node = static_cast<camel::graph::NodeRef>(ref);
        if (ref >= static_cast<gc_node_ref_t>(graph.nodeCount()) || !frozenView.contains(node) ||
            !draft.alive(ref) || !draftView.contains(node)) {
            continue;
        }
        ++counts.nodes;
        lastRef = ref;
        const camel::facts::Anchor anchor{&draft, static_cast<camel::facts::NodeId>(ref)};
        const std::shared_ptr<const camel::facts::Arrangeable> here =
            store->obtain<camel::facts::Arrangeable>(kind, anchor);
        const std::shared_ptr<const camel::facts::Arrangeable> there =
            frozen.obtain<camel::facts::Arrangeable>(
                kind,
                camel::facts::Anchor{&graph, static_cast<camel::facts::NodeId>(ref)});
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
            kind,
            camel::facts::Anchor{&draft, static_cast<camel::facts::NodeId>(lastRef)}) ==
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
    AnswerCounts answers;
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
            os << std::format("FACTS_COMPARE graph={} state=no-store\n", graph->name());
            continue;
        }
        compareControlRegions(*graph, *store, control);
        compareArrangeable(*graph, *store, arrange);
        censusArrangeable(*graph, *store, answers);
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
        "FACTS_COMPARE kind=core.arrangeable graphs={} nodes={} same={} diff={} no_old={} "
        "conversions={}\n",
        arrange.graphs,
        arrange.nodes,
        arrange.same,
        arrange.diff,
        arrange.noOld,
        arrange.conversions);
    os << std::format(
        "FACTS_COMPARE kind=core.arrangeable answers graphs={} nodes={} free={} kept={} "
        "no_fact={}\n",
        answers.graphs,
        answers.nodes,
        answers.free,
        answers.kept,
        answers.noFact);
    // The tables the total cannot give. One row per node kind and one per family the program
    // carries: a kind or a family there is no line for is one the program does not carry, which is
    // why a case that reads this table asks for the rows it expects by name rather than reading a
    // missing line as agreement. `kept_example` names the first node of the row the fact kept, so a
    // split row carries a node to look at and not only a count.
    static const char *kKindNames[kNodeKindCount] =
        {"data", "port", "oper", "func", "call", "bind", "brch", "loop", "gate"};
    for (size_t row = 0; row < answers.byKind.size(); ++row) {
        const AnswerCell &cell = answers.byKind[row];
        if (cell.nodes == 0) {
            continue;
        }
        os << std::format(
            "FACTS_COMPARE answers_kind kind={} nodes={} free={} kept={} no_fact={} "
            "kept_example={}\n",
            row < kNodeKindCount ? kKindNames[row] : "?",
            cell.nodes,
            cell.free,
            cell.kept,
            cell.noFact,
            cell.keptExample.empty() ? std::string("-") : cell.keptExample);
    }
    for (size_t row = 0; row < answers.byFamily.size(); ++row) {
        const AnswerCell &cell = answers.byFamily[row];
        if (cell.nodes == 0) {
            continue;
        }
        os << std::format(
            "FACTS_COMPARE answers_family family={} nodes={} free={} kept={} no_fact={} "
            "kept_example={}\n",
            row < kFamilyCount ? kFamilyNames[row] : "?",
            cell.nodes,
            cell.free,
            cell.kept,
            cell.noFact,
            cell.keptExample.empty() ? std::string("-") : cell.keptExample);
    }
    // The (kind, operator) table, sorted by the map. `rows` is printed beside `nodes` so a table
    // that covered nothing cannot read like a table with no kept node in it.
    size_t rowNodes = 0;
    for (const auto &[key, cell] : answers.byRow) {
        rowNodes += cell.nodes;
        os << std::format(
            "FACTS_COMPARE answers_row node_kind={} oper={} nodes={} free={} kept={} no_fact={}\n",
            key.first,
            key.second,
            cell.nodes,
            cell.free,
            cell.kept,
            cell.noFact);
    }
    os << std::format(
        "FACTS_COMPARE answers_rows rows={} nodes={}\n",
        answers.byRow.size(),
        rowNodes);
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
