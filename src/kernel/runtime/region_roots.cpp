// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Which region decides whether a node runs (camel/runtime/region_roots.h), over the view: one walk
 * for both representations.
 *
 * What a region owes its writes is not answered here: it is an edge laid when the graph is
 * built (`Builder::layRegionWriteEdges`).
 */

#include "camel/runtime/region_roots.h"

namespace camel::runtime {
namespace {

using camel::graph::GraphView;
using camel::graph::kNoNode;
using camel::graph::kNoRegion;
using camel::graph::RegionId;
using camel::graph::RegionKind;
using camel::graph::RegionRecord;

/// The kinds of region the program chooses to run or not. Everything else -- the body, an inlined
/// body, a tagged block, a region a rewrite removed -- is transparent: a node in it runs with the
/// region around it, which is why the walk below goes past it instead of stopping there. The view
/// answers those three as a body, so they are `Root` here.
bool isDeciding(RegionKind kind) {
    switch (kind) {
    case RegionKind::Arm:
    case RegionKind::LoopCond:
    case RegionKind::LoopBody:
        return true;
    case RegionKind::Root:
        break;
    }
    return false;
}

} // namespace

RegionId decidingRegionOf(const GraphView &graph, camel::graph::NodeRef ref) {
    // A place the view holds is one the graph has a node at, whether or not the view prints that
    // node: a JOIN is a place of its own (the view folds it into the BRCH) and it still runs
    // somewhere, so the walk starts from its region like any other node's. A ref the graph does not
    // hold has no region to start from.
    if (ref == kNoNode || ref >= graph.nodeSlotCount()) {
        return 0;
    }
    // Region ids are not reused and a region's parent is always a region the graph holds (0 is
    // every region's ancestor), so the walk ends: at the body, at a region of a kind the program
    // does not choose, or at an id the view does not hold (which is not a place it can choose).
    RegionId at = graph.regionOf(ref);
    for (std::uint32_t depth = 0; depth <= graph.regionCount(); ++depth) {
        if (at == 0) {
            return 0;
        }
        if (at == kNoRegion || at >= graph.regionCount()) {
            return 0;
        }
        const RegionRecord one = graph.region(at);
        if (isDeciding(one.kind)) {
            return at;
        }
        if (one.parent == at || one.parent == kNoRegion) {
            return 0; // the body is in nothing, and a region that is its own parent would not end
        }
        at = one.parent;
    }
    return 0;
}

camel::graph::NodeRef decidingOwnerOf(const GraphView &graph, RegionId region) {
    if (region == 0 || region == kNoRegion || region >= graph.regionCount()) {
        return kNoNode;
    }
    return graph.region(region).owner;
}

} // namespace camel::runtime