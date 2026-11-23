// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The computation of `core.controlRegions` (camel/facts/control_regions.h), and the store of the
 * facts of the frozen graphs a process holds.
 *
 * The tree is built from the view's region records: regions whose kind only annotates (Inline,
 * Block) are looked through, and every other control region is placed in the one around it. It is
 * the same walk for every reader, and it is here rather than in the reader because a tree belongs
 * to a graph, not to whoever asked for it.
 *
 * A frozen graph's content never changes, so the store's version for one is the constant 0 and a
 * stored fact about one is valid until the graph is freed; `forgetGraph` is that end. The store is
 * never destroyed: it outlives every graph, and a store destroyed before the graphs it answers
 * about would turn a graph's last act into a use of a dead object.
 */

#include "camel/facts/control_regions.h"

#include "camel/graph/graph.h"
#include "graph/graph_view.h"

#include <format>

namespace camel::facts {

namespace {

bool isControl(camel::graph::RegionKind kind) {
    return kind == camel::graph::RegionKind::Arm ||
           kind == camel::graph::RegionKind::LoopCond ||
           kind == camel::graph::RegionKind::LoopBody;
}

/// The control region around `control`, from the enclosing control of every region.
std::uint32_t parentControlOf(
    const camel::graph::GraphView &view, const ControlRegionTree &tree, std::uint32_t control) {
    const std::uint32_t parent = view.region(control).parent;
    return parent == 0 || parent >= tree.enclosing.size() ? 0u : tree.enclosing[parent];
}

} // namespace

/// The store of the frozen graphs' facts. See the file comment for why its version is constant and
/// why it is never destroyed.
Store &frozenGraphFacts() {
    static Store *store = new Store(
        GraphAccess{
            .view = [](const graph::Graph &graph) { return &graph.view(); },
            .version = [](const graph::Graph &) { return std::uint64_t(0); },
        },
        true);
    return *store;
}

namespace {

/// The tree of `view`, reading every region record it is made of.
ControlRegionTree buildControlRegionTree(ReadLog &log, const graph::GraphView &view) {
    ControlRegionTree tree;
    const std::uint32_t count = view.regionCount();
    tree.enclosing.assign(count, 0);
    tree.tin.assign(count, 0);
    tree.last.assign(count, 0);
    tree.order.push_back(0);
    for (std::uint32_t region = 1; region < count; ++region) {
        log.region(region);
    }
    if (count == 0) {
        return tree;
    }
    // Parents have smaller ids than the regions in them only by accident of construction, so the
    // enclosing control region is resolved by walking up, once per region.
    std::vector<std::uint8_t> done(count, 0);
    std::vector<std::uint32_t> chain;
    for (std::uint32_t r = 1; r < count; ++r) {
        chain.clear();
        std::uint32_t at = r;
        while (at != 0 && at < count && !done[at]) {
            chain.push_back(at);
            if (isControl(view.region(at).kind)) {
                break;
            }
            at = view.region(at).parent;
        }
        std::uint32_t found = 0;
        if (at != 0 && at < count) {
            found = done[at] ? tree.enclosing[at] : (isControl(view.region(at).kind) ? at : 0);
        }
        for (const std::uint32_t id : chain) {
            tree.enclosing[id] = isControl(view.region(id).kind) ? id : found;
            done[id]           = 1;
        }
    }
    // The tree of the control regions, and a walk of it that visits a region before the regions in
    // it: a region and everything in it are the positions [tin, last].
    std::vector<std::vector<std::uint32_t>> children(count);
    for (std::uint32_t r = 1; r < count; ++r) {
        const camel::graph::RegionRecord region = view.region(r);
        if (!isControl(region.kind)) {
            continue;
        }
        children[parentControlOf(view, tree, r)].push_back(r);
        if (region.kind == camel::graph::RegionKind::Arm) {
            auto &arms = tree.arms[region.owner];
            if (arms.size() <= region.index) {
                arms.resize(region.index + 1, 0);
            }
            arms[region.index] = r;
        } else {
            auto &parts =
                tree.loops.try_emplace(region.owner, std::array<std::uint32_t, 2>{0, 0}).first->second;
            parts[region.kind == camel::graph::RegionKind::LoopCond ? 0 : 1] = r;
        }
    }
    std::vector<std::pair<std::uint32_t, std::size_t>> stack{{0, 0}};
    while (!stack.empty()) {
        auto &[region, next] = stack.back();
        if (next == 0 && region != 0) {
            tree.tin[region] = static_cast<std::uint32_t>(tree.order.size());
            tree.order.push_back(region);
        }
        if (next < children[region].size()) {
            const std::uint32_t child = children[region][next++];
            stack.push_back({child, 0});
            continue;
        }
        tree.last[region] = static_cast<std::uint32_t>(tree.order.size() - 1);
        stack.pop_back();
    }
    return tree;
}

} // namespace

KindId controlRegionKind() {
    static const KindId id = registerKind<ControlRegionTree>(Kind<ControlRegionTree>{
        .key        = "core.controlRegions",
        .owner      = "core",
        .version    = 1,
        .cls        = Class::Derived,
        .scope      = Scope::Graph,
        .dependence = Dependence::Reported,
        .monotone   = false,
        .compute =
            [](std::span<const Anchor> anchors, const GraphAccess &graphs, ReadLog &log)
            -> std::optional<ControlRegionTree> {
            if (anchors.size() != 1 || anchors[0].graph == nullptr ||
                anchors[0].node != kWholeGraph || !graphs.view) {
                return std::nullopt;
            }
            const graph::GraphView *view = graphs.view(*anchors[0].graph);
            if (view == nullptr) {
                return std::nullopt;
            }
            return buildControlRegionTree(log, *view);
        },
        // No merge: the tree of the union of two graphs is not the trees of the parts made one
        // (facts.h, "Merging and monotonicity"). The kind is a whole-graph answer, cached by
        // version alone.
        .merge = {},
        .print = [](const ControlRegionTree &tree) { return toString(tree); },
    });
    return id;
}

std::shared_ptr<const ControlRegionTree> controlRegionsOf(const graph::Graph &graph) {
    const Anchor anchor{&graph, kWholeGraph};
    return frozenGraphFacts().obtain<ControlRegionTree>(controlRegionKind(), anchor);
}

void forgetGraph(const graph::Graph *graph) {
    if (graph != nullptr) {
        frozenGraphFacts().invalidateGraph(graph);
    }
}

std::string toString(const ControlRegionTree &tree) {
    const std::size_t count = tree.order.empty() ? 0 : tree.order.size() - 1;
    return std::format("{} control region(s) in the body", count);
}

} // namespace camel::facts