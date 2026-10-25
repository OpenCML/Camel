// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Dependency-convex region partitioning; eligibility comes from the semantic planner.

#pragma once
#include "camel/core/operator.h"
#include "camel/runtime/draft.h"
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>
class Function;
namespace camel::runtime {
class GCGraph;
namespace fact {
class FactStore;
}
} // namespace camel::runtime

namespace camel::tiles {
class GpuScopes;
struct RegionNode {
    // The value dependencies: the operands and the context arguments the node reads. These are
    // the edges a region boundary is about, and the only ones it uploads or downloads.
    std::vector<size_t> inputs;
    // The dependencies that only order the node in its block (its control edges). They carry no
    // value, so a region that holds the node reads nothing through them, but they order it and
    // keep it live exactly as any other edge does.
    std::vector<size_t> order;
    bool eligible = false;
    bool output   = false;
    bool embedded = false; // Static values embedded into extracted functions.
    // The control region the node runs in (`ControlRegions::controlOf`): 0 for the body, the Arm
    // of a BRCH or a part of a LOOP otherwise. A region never spans two of them, because the code
    // in an arm runs only when that arm is taken: each one is a segment of its own (docs/design/gpu/plan.cn.md
    // 2.5), and the node that owns it (BRCH, JOIN, LOOP) stays on the host.
    uint32_t control = 0;
};
struct Region {
    std::vector<size_t> members; // In dependency order, independent of source node numbering.
    std::vector<size_t> inputs;
    std::vector<size_t> outputs;
};
// Includes CPU nodes in cycle checks. Unreachable nodes are excluded. The caller marks
// all observable roots (including effect/control roots) and all dependency edge kinds.
// The result contains node IDs only: no GC pointers or references to the source graph.
std::vector<Region> partitionRegions(const std::vector<RegionNode> &nodes);
struct ResidentEdge {
    size_t value, producer, consumer;
};
struct RegionTransfers {
    std::vector<size_t> uploads;
    std::vector<size_t> downloads;
    std::vector<ResidentEdge> resident;
};
// All regions share one target. IDs denote immutable source values; CPU consumers
// can require a download while another region retains the same resident value.
RegionTransfers
planRegionTransfers(const std::vector<RegionNode> &nodes, const std::vector<Region> &regions);
struct RegionDraft {
    std::unique_ptr<runtime::GraphDraft> graph;
    std::vector<size_t> parameters; // Source IDs; constant boundary values are embedded.
    std::vector<runtime::gc_node_ref_t> sourceToNode;
};
// Source and static values must remain rooted; use under NoCollectionScope until
// encoding/attaching the graph. The returned draft is not a GC root by itself.
RegionDraft extractRegion(const runtime::GCGraph &source, const Region &region);

struct RegionAnalysis {
    std::vector<Region> regions;
    std::vector<std::string> exclusions;
    RegionTransfers transfers;
};
/// The regions of one graph, and why the nodes that are in none are in none. With `scopes`, a
/// node the `#gpu` scope covers is not excluded for the control edge that orders it in its block
/// (docs/design/gpu/plan.cn.md 2.4, 2.5): the arm or the loop it is written in is where it runs, so it becomes a
/// segment of that region. Without `scopes` — every graph outside a device scope — the analysis
/// is the one the pass has always taken.
RegionAnalysis analyzeRegions(
    const runtime::Program &program, const runtime::GCGraph &graph,
    runtime::fact::FactStore &facts, const GpuScopes *scopes = nullptr);
std::string loopEligibility(
    const runtime::Program &program, const runtime::GCGraph &graph, runtime::gc_node_ref_t node,
    runtime::fact::FactStore &facts);
/// Whether the device back end has an implementation of `oper`: the declared capability list,
/// the same one the region compiler lowers. A `#gpu` scope refuses an operator outside it.
bool deviceCapable(core::OperId oper);
/// The reason a `#gpu` scope must refuse `id`, as the diagnostic prints it, or an empty string
/// when it must not. `id`'s eligibility answer (`analysis.exclusions[id]`) decides: a shape or
/// element type the back end refuses is `"no device program for this shape"`; an operator with
/// no lowering at any shape is `"no device program for this operator"`; an operator no module
/// offers a device program for is `"the module provides none"`. Unresolved types and index
/// instances are not refusals (a run with the actual arguments may still produce a program), and
/// neither are host effects nor the structural host code (branches, calls, loop bodies).
const char *gpuRefusal(
    const runtime::GCGraph &graph, const RegionAnalysis &analysis, runtime::gc_node_ref_t id);
struct RegionDeviceOps {
    operator_t upload, execute, download;
    std::string uploadUri = "tiles:upload", executeUri, downloadUri = "tiles:download";
};
void registerRegionPass(RegionDeviceOps preferred);
// Rewrites the graphs reachable from `root`, a graph of `program`, and returns the program of
// the rewritten graphs (`program` itself when no region was outlined, `count` is 0 then); null
// executor produces ordinary CPU CALL nodes. Non-null executors use the supplied stable
// operator URI and packed arguments. With `refuseGpuScopes`, a node carrying the `gpu`
// tag whose operator is outside `deviceCapable` is reported as `GpuScopeUnsupported` and the
// graphs are left unrewritten (the pass then fails on the diagnostic).
const runtime::Program *outlineRegions(
    core::context::Context &context, const runtime::Program *program, runtime::GCGraph *root,
    operator_t executor, const std::string &uri, size_t &count,
    const RegionDeviceOps *device = nullptr, bool refuseGpuScopes = false);
// Register semantic-equivalence checks by naming them in a pipeline; no variable gates them.
void registerRegionTestPasses(
    operator_t run, RegionDeviceOps device, RegionDeviceOps host, RegionDeviceOps preferred);
slot_t testNativeCall(ArgsView &with, ArgsView &norm, core::context::Context &context);
slot_t testCompiledCall(ArgsView &with, ArgsView &norm, core::context::Context &context);

/// The calls this module writes for a segment of a cut scope: the operators that call the graph
/// of the function value they receive (`OperatorTraits::callsGraphs`, op-semantics 11.4), which
/// are the ones `RegionDeviceOps` names and `outlineRegions` writes. A report operator asked
/// about a graph a pass has cut reads these calls, never the wrapper: each one carries the graph
/// of its segment as its context argument, and the wrapper is not a program of its own.
bool isRegionExec(core::OperId oper);
struct RegionSegment {
    // The call to the segment, in the graph that holds it.
    runtime::gc_node_ref_t call = 0;
    // The graph the call carries. Never null: a call without one is not a segment call.
    Function *graph = nullptr;
};
// The segments a graph a pass has cut is made of, in graph order; empty for a graph no pass has
// cut. `RegionSegment::graph` is the `Function` the call carries, out of the graph's static
// values, so the caller keeps them rooted (a `NoCollectionScope` covers a report).
std::vector<RegionSegment> regionSegments(const runtime::GCGraph &graph);

// Inspect one function graph without executing it or initializing CUDA. This is
// candidate analysis with extracted drafts, not automatic graph execution. `segments` are the
// calls `regionSegments` found: a scope a pass has already cut reports them as segments of their
// own, and never as nodes the region analysis excluded.
std::string inspectRegions(
    const runtime::Program &program, const runtime::GCGraph &graph,
    runtime::fact::FactStore &facts, std::span<const RegionSegment> segments = {});
} // namespace camel::tiles
