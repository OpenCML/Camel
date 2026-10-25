// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// One placement query and its report. See tiles/internal/placement.h for the rule.

#include "tiles/internal/placement.h"

#include "camel/core/node_attrs.h"
#include "camel/core/rtdata/func.h"
#include "camel/runtime/program.h"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::tiles {
namespace {

namespace attr = camel::core::attr;

/// The graph of the function constant a node holds, or nullptr when the node is not one or holds
/// no function: a DATA node whose static slot is a `Function` value (a named function, or the
/// derived graph an expansion pass leaves at a grad call site).
const runtime::GCGraph *functionGraphOf(const runtime::GCGraph &graph, runtime::gc_node_ref_t id) {
    const runtime::GCNode *node = graph.node(id);
    if (node == nullptr || node->kind != runtime::GCNodeKind::Data || node->dataIndex >= 0) {
        return nullptr;
    }
    const auto slots  = graph.staticSlots();
    const size_t slot = static_cast<size_t>(-node->dataIndex);
    if (slot >= slots.size() || slots[slot] == NullSlot) {
        return nullptr;
    }
    auto *fn = camel::core::rtdata::fromSlot<::Function *>(slots[slot]);
    return fn == nullptr ? nullptr : fn->graph();
}

attr::AttrSetId bodyAttrs(const runtime::GCGraph &graph) {
    return graph.regionCount() > 0 && graph.region(0) ? graph.region(0)->attrs : attr::kNoAttrs;
}

/// The nearest region around `region` that carries the placement fact: a node in an untagged
/// arm of a tagged block belongs to that block's row, not to a row no one prints.
uint32_t factRegion(const runtime::GCGraph &graph, uint32_t region, attr::AttrKey key) {
    while (region != 0 && attr::valueOf(graph.region(region)->attrs, key) == attr::kNoValue) {
        region = graph.region(region)->parent;
    }
    return region;
}

/// The fact as written, not its spelling as a tag: the query reads the one key both labels
/// write, so a fact that is neither "gpu" nor "cpu" is reported as written and treated as host.
std::string factText(attr::AttrValueId value) {
    if (value == attr::kNoValue) {
        return {};
    }
    const attr::Value decoded = attr::value(value);
    return decoded.kind == attr::ValueKind::Symbol ? decoded.text : attr::valueText(value);
}

Placement classify(attr::AttrValueId value, bool bodyGpu) {
    if (value == attr::symbol("gpu")) {
        return bodyGpu ? Placement::GpuRequired : Placement::Gpu;
    }
    // `cpu` is the explicit host fact; a value that is neither label cannot name a device Tiles
    // can run on, so it is answered the same way. The report still shows what was written.
    return Placement::Host;
}

const char *regionKindName(runtime::GCRegionKind kind) {
    using runtime::GCRegionKind;
    switch (kind) {
    case GCRegionKind::Root:
        return "body";
    case GCRegionKind::Arm:
        return "arm";
    case GCRegionKind::LoopCond:
        return "loop_cond";
    case GCRegionKind::LoopBody:
        return "loop_body";
    case GCRegionKind::Inline:
        return "inline";
    case GCRegionKind::Block:
        return "block";
    case GCRegionKind::Dead:
        return "dead";
    }
    return "region";
}

struct Counts {
    size_t nodes = 0, host = 0, gpu = 0, gpuRequired = 0;

    void add(Placement placement) {
        ++nodes;
        switch (placement) {
        case Placement::Host:
            ++host;
            break;
        case Placement::Gpu:
            ++gpu;
            break;
        case Placement::GpuRequired:
            ++gpuRequired;
            break;
        }
    }
};

void addCounts(nlohmann::json &entry, const Counts &counts) {
    entry["nodes"]        = counts.nodes;
    entry["host"]         = counts.host;
    entry["gpu"]          = counts.gpu;
    entry["gpu_required"] = counts.gpuRequired;
}

} // namespace

bool holdsStaticFunction(const runtime::GCGraph &graph, runtime::gc_node_ref_t node) {
    return functionGraphOf(graph, node) != nullptr;
}

const runtime::GCGraph *calleeGraphOf(
    const runtime::Program &program, const runtime::GCGraph &graph, runtime::gc_node_ref_t id) {
    // A caller reaches this for every node of a scope graph, not only for the calls it looks at:
    // a node that is not one of the two call kinds reaches no graph.
    if (graph.node(id) == nullptr || (graph.node(id)->kind != runtime::GCNodeKind::Func &&
                                      graph.node(id)->kind != runtime::GCNodeKind::Call)) {
        return nullptr;
    }
    if (graph.node(id)->kind == runtime::GCNodeKind::Func) {
        return program.calleeOf(graph, id);
    }
    // The call reads its callee as a value: `std::macro` replaces the grad operator at the call
    // site with the function constant of the derived graph (docs/design/gpu/plan.cn.md 2.6), and a
    // call through it is a call to that graph, exactly as a written call to a named function is.
    // A value that is not a constant names no one graph: the run chooses the callee.
    for (const runtime::gc_node_ref_t operand : graph.withInputsOf(id)) {
        if (const runtime::GCGraph *callee = functionGraphOf(graph, operand)) {
            return callee;
        }
    }
    return nullptr;
}

const char *placementName(Placement placement) {
    switch (placement) {
    case Placement::Host:
        return "host";
    case Placement::Gpu:
        return "gpu";
    case Placement::GpuRequired:
        return "gpu-required";
    }
    return "host";
}

const char *placementScopeName(PlacementScope scope) {
    switch (scope) {
    case PlacementScope::Default:
        return "default";
    case PlacementScope::Node:
        return "node";
    case PlacementScope::Region:
        return "region";
    case PlacementScope::Body:
        return "body";
    }
    return "default";
}

PlacementDecision placementOf(const runtime::GCGraph &graph, runtime::gc_node_ref_t node) {
    PlacementDecision decision;
    // `attr::key` interns the name the core registered as a standard fact; asking for it here
    // does not depend on which module loaded first, and the id is the registered key's.
    const attr::AttrKey key = attr::key("tiles.placement");
    if (key == attr::kNoKey) {
        return decision;
    }
    const attr::AttrSetId body        = bodyAttrs(graph);
    const attr::AttrValueId bodyValue = attr::valueOf(body, key);
    const bool bodyGpu                = bodyValue == attr::symbol("gpu");

    const auto decide = [&](attr::AttrValueId value, PlacementScope scope, uint32_t region) {
        decision.placement = classify(value, bodyGpu);
        decision.scope     = scope;
        decision.fact      = factText(value);
        decision.region    = region;
        return decision;
    };

    // 1. The fact the node carries itself. The builder stamps every node written in a tagged
    //    scope, and a rewrite that moves a node keeps its attributes, so this is the placement
    //    the node had where it was written even after the block region is gone.
    const attr::AttrValueId own = attr::valueOf(graph.nodeAttrs(node), key);
    if (own != attr::kNoValue) {
        return decide(own, PlacementScope::Node, kNoPlacementRegion);
    }
    // 2. The nearest tagged region: a tagged block, an arm or a loop body written inside the
    //    node's scope. Annotation regions are looked through by walking their parent chain.
    for (uint32_t region = graph.nodeRegion(node); region != 0;
         region          = graph.region(region)->parent) {
        const attr::AttrValueId value = attr::valueOf(graph.region(region)->attrs, key);
        if (value != attr::kNoValue) {
            return decide(value, PlacementScope::Region, region);
        }
    }
    // 3. The function body: a tag written on the function is a fact of region 0.
    if (bodyValue != attr::kNoValue) {
        return decide(bodyValue, PlacementScope::Body, 0);
    }
    // 4. Nothing says otherwise: the host.
    return decision;
}

PlacementDecision bodyPlacementOf(const runtime::GCGraph &graph) {
    PlacementDecision decision;
    const attr::AttrKey key = attr::key("tiles.placement");
    if (key == attr::kNoKey) {
        return decision;
    }
    const attr::AttrValueId value = attr::valueOf(bodyAttrs(graph), key);
    if (value == attr::kNoValue) {
        return decision;
    }
    // A body fact is the graph's own scope, so a "gpu" here is GpuRequired, not Gpu.
    decision.placement = classify(value, /*bodyGpu=*/true);
    decision.scope     = PlacementScope::Body;
    decision.fact      = factText(value);
    decision.region    = 0;
    return decision;
}

bool hasPlacementFact(const runtime::GCGraph &graph) {
    const attr::AttrKey key = attr::key("tiles.placement");
    if (key == attr::kNoKey) {
        return false;
    }
    if (attr::valueOf(bodyAttrs(graph), key) != attr::kNoValue) {
        return true;
    }
    for (uint32_t region = 1; region < graph.regionCount(); ++region) {
        if (attr::valueOf(graph.region(region)->attrs, key) != attr::kNoValue) {
            return true;
        }
    }
    for (runtime::gc_node_ref_t node = 0; node < graph.nodeCount(); ++node) {
        if (attr::valueOf(graph.nodeAttrs(node), key) != attr::kNoValue) {
            return true;
        }
    }
    return false;
}

bool hasPlacementFact(const runtime::Program &program) {
    for (const runtime::GCGraph *graph : program.graphs()) {
        if (hasPlacementFact(*graph)) {
            return true;
        }
    }
    return false;
}

std::string placementReport(const runtime::Program &program) {
    if (!hasPlacementFact(program)) {
        return {};
    }
    const attr::AttrKey key = attr::key("tiles.placement");
    std::ostringstream out;
    for (const runtime::GCGraph *graph : program.graphs()) {
        const attr::AttrSetId body        = bodyAttrs(*graph);
        const attr::AttrValueId bodyValue = attr::valueOf(body, key);
        const bool bodyGpu                = bodyValue == attr::symbol("gpu");

        Counts total;
        std::unordered_map<uint32_t, Counts> perRegion;
        for (runtime::gc_node_ref_t node = 0; node < graph->nodeCount(); ++node) {
            const PlacementDecision decision = placementOf(*graph, node);
            total.add(decision.placement);
            perRegion[factRegion(*graph, graph->nodeRegion(node), key)].add(decision.placement);
        }

        nlohmann::json entry;
        entry["graph"]     = graph->name();
        entry["body"]      = placementName(bodyGpu ? Placement::GpuRequired : Placement::Host);
        entry["body_fact"] = factText(bodyValue);
        addCounts(entry, total);

        nlohmann::json regions = nlohmann::json::array();
        for (uint32_t id = 0; id < graph->regionCount(); ++id) {
            const runtime::GCRegion &region = *graph->region(id);
            const attr::AttrValueId value   = attr::valueOf(region.attrs, key);
            if (value == attr::kNoValue) {
                continue;
            }
            const auto found    = perRegion.find(id);
            const Counts counts = found == perRegion.end() ? Counts{} : found->second;
            nlohmann::json row;
            row["id"]     = id;
            row["kind"]   = regionKindName(region.kind);
            row["parent"] = region.parent;
            row["fact"]   = factText(value);
            addCounts(row, counts);
            regions.push_back(std::move(row));
        }
        entry["regions"] = std::move(regions);
        out << "TILES_PLACEMENT " << entry.dump() << '\n';
    }
    return out.str();
}

GpuScopes::GpuScopes(const runtime::Program &program) {
    // A body tag makes the whole graph a device unit.
    std::vector<const runtime::GCGraph *> scopes;
    for (const runtime::GCGraph *graph : program.graphs()) {
        if (bodyPlacementOf(*graph).placement == Placement::GpuRequired) {
            scopeOf_.emplace(graph, graph);
            scopes.push_back(graph);
        }
    }
    // A call written in a device scope (a `#gpu` body or block) makes the callee one too, even
    // when the code around the block is the host's: that call is still a device call.
    const auto addCall = [&](const runtime::GCGraph &graph, runtime::gc_node_ref_t id) {
        if (placementOf(graph, id).fact == "cpu")
            return;
        const runtime::GCGraph *callee = calleeGraphOf(program, graph, id);
        if (callee == nullptr || scopeOf_.contains(callee))
            return;
        const auto found = scopeOf_.find(&graph);
        scopeOf_.emplace(callee, found == scopeOf_.end() ? &graph : found->second);
        scopes.push_back(callee);
    };
    for (const runtime::GCGraph *graph : program.graphs())
        for (runtime::gc_node_ref_t id = 0; id < graph->nodeCount(); ++id)
            if (placementOf(*graph, id).onDevice())
                addCall(*graph, id);
    // Transitive: a helper of a helper is called by the scope as well.
    for (size_t next = 0; next < scopes.size(); ++next) {
        const runtime::GCGraph *graph = scopes[next];
        for (runtime::gc_node_ref_t id = 0; id < graph->nodeCount(); ++id)
            addCall(*graph, id);
    }
}

bool GpuScopes::covers(const runtime::GCGraph &graph, runtime::gc_node_ref_t node) const {
    const PlacementDecision decision = placementOf(graph, node);
    // A fact of the node's own wins, in both directions: `#cpu` is the written way out of a
    // scope, and a nested `#gpu` block is on the device.
    if (decision.onDevice())
        return true;
    return decision.scope == PlacementScope::Default && scopeOf_.contains(&graph);
}

const runtime::GCGraph *GpuScopes::scopeOf(const runtime::GCGraph &graph) const {
    const auto found = scopeOf_.find(&graph);
    return found == scopeOf_.end() ? nullptr : found->second;
}

} // namespace camel::tiles