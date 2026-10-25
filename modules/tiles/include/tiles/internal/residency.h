// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The residency conclusion (docs/design/gpu/plan.cn.md 3.2): where each value's block lives, and
// what crosses a placement boundary. The conclusion is the one answer the pass reads before it
// writes a transfer node: which of a segment's inputs is written on the device side, which of its
// outputs the host observes, and which values one segment hands to the next where they lie. It is
// a reading, not a rewrite: the outline inserts the nodes the conclusion names, and no other rule
// decides a transfer.
//
// A segment's members are written by its kernels, so they live on the device, wherever the region
// was written; every other value lives where the facts say. What crosses:
//
//   * one `upload` for a host value a segment reads on the device, one `download` for a device
//     value the host observes, and nothing for a value one segment hands to the next where it lies;
//   * one transfer per value at a boundary, however many readers it has, and none for a value no
//     live node reads.

#pragma once

#include "camel/runtime/graph.h"
#include "tiles/internal/placement.h"

#include <cstddef>
#include <string>
#include <vector>

namespace camel::runtime {
class Program;
} // namespace camel::runtime

namespace camel::tiles {

struct Region;
struct RegionNode;

/// What the residency inference answered for one value of the graph, beyond the nodes' own facts:
/// the block the value lives in, and whether the node takes what it is given where it lies -- a
/// call whose callee declares device parameters travels with the argument's block.
struct ResidencyValue {
    PlacementDecision block;
    bool readsInPlace = false;
};

/// Everything the boundary decision is told about the graph as a whole: the reading's answers, in
/// node order, and whether the graph's own value is the host's. Where the reading comes from is the
/// graph reader's business (`planResidency`'s overload below): a node's own placement fact, the
/// nearest region around it, and the blocks the signature declares.
struct ResidencyFacts {
    std::vector<ResidencyValue> values;
    /// Whether the graph hands the caller an ordinary value: its exit type declares one, or it
    /// declares no value at all. A `Device<T>` exit type leaves the value on the device, so only a
    /// host reader writes it back.
    bool returnsToHost = true;
};

/// A value one segment hands to the next: both blocks are the device's, so nothing is copied.
struct ResidentHandoff {
    size_t value, producer, consumer;
};

/// One segment's boundary (docs/design/gpu/plan.cn.md 3.3): the values written on the device side
/// before it runs, the values written back for the host after it, and the values it takes from an
/// earlier segment where they lie.
struct SegmentResidency {
    size_t region = 0;
    std::vector<size_t> uploads;
    std::vector<size_t> downloads;
    std::vector<ResidentHandoff> resident;
};

/// The residency conclusion for one graph: the three answers of every segment, and once for the
/// graph, which is what a writer of transfer nodes reads.
struct ResidencyPlan {
    std::vector<SegmentResidency> segments;
    std::vector<size_t> uploads;
    std::vector<size_t> downloads;
    std::vector<ResidentHandoff> resident;
};

/// The boundary decision. `nodes` is the graph as the partition reads it (what each node reads,
/// which node is the graph's own value, which input the extraction embeds), `facts` is what the
/// residency inference answered for the values, and `regions` must be the partition of `nodes`.
/// A segment's members are the device's whatever the inference answered -- a region's kernels write
/// their values there -- and a value crosses the boundary once however many readers it has.
ResidencyPlan planResidency(
    const ResidencyFacts &facts, const std::vector<RegionNode> &nodes,
    const std::vector<Region> &regions);

/// The conclusion for one graph: the reading of the graph's own facts -- a node's placement, a
/// parameter's declaration, what the exit type declares the graph hands back (`Device<T>` asserts a
/// value that lives on the device already), and a call whose callee declares device parameters
/// (which takes its argument where it lies) -- then the decision above. Defined with the graph
/// reader, `graph_regions.cpp`, which reads a signature through the module's type table.
ResidencyPlan planResidency(
    const runtime::Program &program, const runtime::GCGraph &graph,
    const std::vector<RegionNode> &nodes, const std::vector<Region> &regions);

/// `TILES_RESIDENCY` lines, one JSON object per graph (docs/design/gpu/plan.cn.md 3.2): the same
/// conclusion at graph granularity -- each value's block, the values that change block once each,
/// and what a call between two graphs moves. Empty when the program writes no placement fact, so
/// an untagged program's output is unchanged.
std::string residencyReport(const runtime::Program &program);

} // namespace camel::tiles