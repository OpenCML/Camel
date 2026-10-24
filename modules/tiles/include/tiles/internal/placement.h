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
#include <unordered_map>

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

/// The placement the graph's body (region 0) carries: what a tag written on the function says.
/// Host with an empty fact when the body carries none. This is the graph's placement as a unit,
/// which the runtime of a call needs to know without looking at one of its nodes.
PlacementDecision bodyPlacementOf(const runtime::GCGraph &graph);

/// Whether any node, block or body region of `graph` carries the placement fact.
bool hasPlacementFact(const runtime::GCGraph &graph);

/// Whether any graph of `program` carries the placement fact.
bool hasPlacementFact(const runtime::Program &program);

/// `TILES_PLACEMENT` lines, one JSON object per graph, for the pass that reads the fact. Empty
/// when the program has no placement fact at all, so an untagged program's output is unchanged.
std::string placementReport(const runtime::Program &program);

/// Which nodes a `#gpu` scope covers (docs/design/gpu/plan.cn.md 2.3): the body of a function
/// whose body placement is `GpuRequired`, and, transitively, the bodies of the functions it
/// calls. A node covered by a scope owes the device an answer: the compiler never runs it on the
/// host in silence, and code written in an arm or a loop of a covered graph is a segment of that
/// region rather than a reason to fall back.
class GpuScopes {
  public:
    /// The scopes of the graphs of `program`.
    explicit GpuScopes(const runtime::Program &program);

    /// Whether `node` of `graph` is in a scope. A node with a placement fact of its own is not
    /// in one when that fact is `cpu`: `#cpu` inside a scope is the written way out.
    bool covers(const runtime::GCGraph &graph, runtime::gc_node_ref_t node) const;

    /// The scope that covers `graph`, or nullptr when none does.
    const runtime::GCGraph *scopeOf(const runtime::GCGraph &graph) const;

  private:
    std::unordered_map<const runtime::GCGraph *, const runtime::GCGraph *> scopeOf_;
};

} // namespace camel::tiles