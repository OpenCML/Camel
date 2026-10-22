// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// One placement query and its report. See tiles/internal/placement.h for the rule.

#include "tiles/internal/placement.h"

#include "camel/core/node_attrs.h"
#include "camel/runtime/program.h"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::tiles {
namespace {

namespace attr = camel::core::attr;

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

} // namespace camel::tiles