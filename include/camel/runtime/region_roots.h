// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The roots a region has (docs/design-principles.cn.md 3.1; docs/design/effects/analysis.cn.md
 * 3.4, `regionRoot`): the nodes a running region runs although no value of theirs is read.
 *
 * 3.1 decides it: "对外部资源的写由所在区域用到 ... 一个区域运行时，区域里对外部资源的写都被用到".
 * A read of an outside resource is a dependency like a read of a variable and stays as it was --
 * it runs when something needs its value -- so the predicate here is a **write**, not "has an
 * effect". That is also why this file is apart from the effect summary of a graph
 * (`camel/runtime/effects.h`): that one asks whether anything outside is touched, and a read of
 * the console touches it too.
 *
 * Which region a node's write belongs to: the innermost arm or loop region it sits in. A region
 * runs because its owner (the BRCH or the LOOP) runs -- the rule the plan already uses to seed a
 * region's *result* (camel::execute::computeExecutionOrder) -- so a write in an arm that is never
 * taken, or in a loop nothing needs, does not run, and this file says so by naming the region and
 * its owner rather than the node alone. An inlined body and a tagged block are transparent: the
 * program cannot choose to run them or not, so their nodes run with the region around them.
 *
 * What is *not* here: the wires. 3.1 keeps "the region uses them" out of the graph -- "图上不加边"
 * (analysis.cn.md 3.4) -- so the walks that ask what a region runs (the liveness walk of a graph,
 * the encode that keeps only what the roots reach, and the execution order) seed these nodes as
 * roots instead of finding an edge to them.
 */

#pragma once

#include "camel/core/operator_table.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"

#include <cstdint>
#include <vector>

namespace camel::runtime {

/// A node of a graph that writes a resource outside the program, and the region whose running
/// decides whether it runs (`decidingRegionOf`).
struct RegionRootCandidate {
    gc_node_ref_t node = kInvalidNodeRef;
    uint32_t region    = 0;

    bool operator==(const RegionRootCandidate &) const = default;
};

/// Whether running a node of `kind` whose operator is `oper` writes a resource outside the
/// program. Read from the operator's declaration and never from the operator's name (design
/// principles 3.3): an access list with a write of a resource whose class reaches outside, or the
/// conservative reading of an operator that itemized none -- "写全局资源", which is a write. A
/// node that is not an operator writes nothing by itself: what a call does is its callee's, and a
/// branch or a loop is its regions'.
bool writesOutsideResource(GCNodeKind kind, camel::core::OperId oper);

/// The region whose running decides whether `ref` runs: the innermost arm or loop region the node
/// sits in, and 0 -- the body -- for a node of the body and for one in a region the program cannot
/// choose to run or not. Both spellings read the region table the graph already carries, so the
/// question has one answer for a draft and for a frozen graph.
uint32_t decidingRegionOf(const GCGraph &graph, gc_node_ref_t ref);
uint32_t decidingRegionOf(const GraphDraft &draft, gc_node_ref_t ref);

/// The node whose running is the running of region `region`: the BRCH or the LOOP that region is
/// a part of (GCRegion::owner), and kInvalidNodeRef for the body, which runs whenever the graph
/// does.
gc_node_ref_t decidingOwnerOf(const GCGraph &graph, uint32_t region);
gc_node_ref_t decidingOwnerOf(const GraphDraft &draft, uint32_t region);

/// Every node of `graph` that writes outside the program, in node order, each with the region that
/// decides whether it runs. The nodes of the body are the ones with `region == 0`.
std::vector<RegionRootCandidate> regionRootCandidatesOf(const GCGraph &graph);
std::vector<RegionRootCandidate> regionRootCandidatesOf(const GraphDraft &draft);

} // namespace camel::runtime