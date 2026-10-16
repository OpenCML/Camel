/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Dependency-convex region partitioning; eligibility comes from the semantic planner.
#pragma once
#include "camel/core/operator.h"
#include "camel/runtime/draft.h"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
namespace camel::runtime {
class GCGraph;
namespace fact {
class FactStore;
}
} // namespace camel::runtime

namespace camel::tiles {
struct RegionNode {
    std::vector<size_t> inputs;
    bool eligible = false;
    bool output   = false;
    bool embedded = false; // Static values embedded into extracted functions.
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
RegionAnalysis analyzeRegions(const runtime::GCGraph &graph, runtime::fact::FactStore &facts);
struct RegionDeviceOps {
    operator_t upload, execute, download;
};
// Rewrites a reachable graph set; null executor produces ordinary CPU CALL nodes.
// Non-null executors use the supplied stable operator URI and packed arguments.
runtime::GCGraph *outlineRegions(
    core::context::Context &context, runtime::GCGraph *root, operator_t executor,
    const std::string &uri, size_t &count, const RegionDeviceOps *device = nullptr);
// Register semantic-equivalence checks only when CAMEL_ENABLE_TEST_PASSES is enabled.
void registerRegionTestPasses(operator_t run, RegionDeviceOps device);
slot_t testNativeCall(ArgsView &with, ArgsView &norm, core::context::Context &context);

// Inspect one function graph without executing it or initializing CUDA. This is
// candidate analysis with extracted drafts, not automatic graph execution.
std::string inspectRegions(const runtime::GCGraph &graph, runtime::fact::FactStore &facts);
} // namespace camel::tiles
