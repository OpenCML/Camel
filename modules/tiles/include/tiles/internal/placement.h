// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The placement query (docs/design/gpu/plan.cn.md 2.2): one node in, one answer out.
//
// A node's placement is the fact it carries itself; when it carries none, the fact of the
// nearest region around it (a tagged block, an arm, a loop body); when that carries none, the
// fact of the function body (region 0); when nothing says either, the host. The answer is the
// only way a Tiles pass asks where code runs: reading `tiles.placement` directly is a defect.

#pragma once

#include "camel/runtime/graph.h"

#include <cstdint>
#include <string>

namespace camel::runtime {
class Program;
}

namespace camel::tiles {

/// No region was consulted: the answer came from the node itself, or from nothing at all.
inline constexpr uint32_t kNoPlacementRegion = static_cast<uint32_t>(-1);

/// What the query answers for one node.
///
/// Host and the two device answers are the three outcomes the plan names. `GpuRequired` is
/// the function body's own `#gpu`: the graph, as a unit, runs on the device, so a caller may
/// not treat it as a host helper. `Gpu` is a device fact written on the node or a nested
/// region of a function whose body is not `#gpu`: the code is on the device, but the graph's
/// placement as a unit is still the host's. Both are device answers; the distinction records
/// which scope asked, so a later pass does not have to search the region chain again.
enum class Placement : uint8_t { Host, Gpu, GpuRequired };

/// Where the answer came from.
enum class PlacementScope : uint8_t { Default, Node, Region, Body };

struct PlacementDecision {
    Placement placement  = Placement::Host;
    PlacementScope scope = PlacementScope::Default;
    /// The `tiles.placement` Symbol as written: "gpu", "cpu", or empty when no fact was found.
    /// A value that is neither label is reported here and answered as Host.
    std::string fact;
    /// The region the fact was read from: kNoPlacementRegion for a node fact or no fact, 0 for
    /// the function body, a block/arm/loop region id otherwise.
    uint32_t region = kNoPlacementRegion;

    bool onDevice() const { return placement != Placement::Host; }
};

const char *placementName(Placement placement);
const char *placementScopeName(PlacementScope scope);

/// The placement of `node` in `graph` (docs/design/gpu/plan.cn.md 2.2).
PlacementDecision placementOf(const runtime::GCGraph &graph, runtime::gc_node_ref_t node);

/// Whether any node, block or body region of `graph` carries the placement fact.
bool hasPlacementFact(const runtime::GCGraph &graph);

/// Whether any graph of `program` carries the placement fact.
bool hasPlacementFact(const runtime::Program &program);

/// `TILES_PLACEMENT` lines, one JSON object per graph, for the pass that reads the fact. Empty
/// when the program has no placement fact at all, so an untagged program's output is unchanged.
std::string placementReport(const runtime::Program &program);

} // namespace camel::tiles