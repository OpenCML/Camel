// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The residency conclusion (docs/design/gpu/plan.cn.md 3.2), read off the graph:
//
//   * a value's block is decided by the node that produces it -- a device node makes a device
//     block, a host node a host block (`upload`/a device operator/an `exec` output, or a host
//     operator/a constant);
//   * a value that a node of the other class consumes crosses the boundary once, however many
//     operators read it: one `download` for a device value a host node reads, one `upload` for a
//     host value a device node reads;
//   * a call's parameters follow their arguments' blocks -- the same block, no copy -- so a call
//     between two device graphs transfers nothing;
//   * a loop's state stays where it was made, so only values that really change class appear in a
//     round's transfers.
//
// The conclusion is a reading, not a rewrite: it says which transfers the program needs and where
// they belong. `TILES_RESIDENCY` prints it next to the placement report so a test can pin the
// numbers, and the pass that inserts the nodes takes its answer from here.

#include "tiles/internal/residency.h"

#include "tiles/internal/placement.h"

#include "camel/runtime/program.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::tiles {
namespace {

using runtime::GCGraph;
using runtime::GCNode;
using runtime::gc_node_ref_t;
using runtime::GCRegion;
using runtime::GCRegionKind;
using runtime::Program;

/// Whether the class the query answered is the device's.
bool onDevice(Placement placement) { return placement != Placement::Host; }

const char *className(Placement placement) { return onDevice(placement) ? "device" : "host"; }

const char *regionClassName(GCRegionKind kind) {
    switch (kind) {
    case GCRegionKind::Root:
        return "body";
    case GCRegionKind::Arm:
        return "arm";
    case GCRegionKind::LoopCond:
        return "loop-condition";
    case GCRegionKind::LoopBody:
        return "loop-body";
    case GCRegionKind::Inline:
        return "inline";
    case GCRegionKind::Block:
        return "block";
    case GCRegionKind::Dead:
        return "dead";
    }
    return "region";
}

/// One value that changes class: the transfer the graph needs, once, wherever the value is read.
struct Crossing {
    gc_node_ref_t value = 0;
    bool upload         = false; // host value read on the device; otherwise a download
    uint32_t region     = 0;
    bool perRound       = false;
};

} // namespace

std::string residencyReport(const Program &program) {
    if (!hasPlacementFact(program)) {
        return {};
    }
    std::ostringstream out;
    for (const GCGraph *graph : program.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        std::unordered_map<gc_node_ref_t, Placement> blockOf;
        blockOf.reserve(graph->nodeCount());
        size_t deviceValues = 0, hostValues = 0;
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            const GCNode *node = graph->node(ref);
            if (node == nullptr || node->kind == runtime::GCNodeKind::Port) {
                continue; // A port is the caller's value: its block is the argument's (§3.2).
            }
            const Placement placement = placementOf(*graph, ref).placement;
            blockOf.emplace(ref, placement);
            onDevice(placement) ? ++deviceValues : ++hostValues;
        }

        std::vector<Crossing> crossings;
        std::unordered_set<uint64_t> seen;
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            const auto consumer = blockOf.find(ref);
            if (consumer == blockOf.end()) {
                continue;
            }
            const uint32_t region = graph->nodeRegion(ref);
            const GCRegion *around = graph->region(region);
            const bool perRound    = around != nullptr && around->kind == GCRegionKind::LoopBody;
            for (const gc_node_ref_t input : graph->normInputsOf(ref)) {
                const auto producer = blockOf.find(input);
                if (producer == blockOf.end() || producer->second == consumer->second) {
                    continue; // The same block: the value is already where it is read.
                }
                const bool upload = onDevice(consumer->second); // host value read on the device
                const uint64_t key =
                    (static_cast<uint64_t>(input) << 1) | static_cast<uint64_t>(upload);
                if (!seen.insert(key).second) {
                    continue; // One transfer per value at the boundary, not one per reader.
                }
                crossings.push_back(Crossing{input, upload, region, perRound});
            }
        }

        nlohmann::json calls = nlohmann::json::array();
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            const GCNode *node = graph->node(ref);
            if (node == nullptr) {
                continue;
            }
            if (node->kind == runtime::GCNodeKind::Func || node->kind == runtime::GCNodeKind::Call) {
                const GCGraph *callee = calleeGraphOf(program, *graph, ref);
                if (callee == nullptr) {
                    continue; // The run chooses the callee: no residency answer to derive.
                }
                nlohmann::json row;
                row["callee"]    = callee->name();
                row["caller"]    = className(placementOf(*graph, ref).placement);
                row["callee_at"] = className(bodyPlacementOf(*callee).placement);
                // A parameter follows its argument's block, so a call between two graphs of one
                // class moves no value; only a call that changes class needs the pair.
                row["transfers"] =
                    placementOf(*graph, ref).placement == bodyPlacementOf(*callee).placement ? 0
                                                                                            : 1;
                calls.push_back(std::move(row));
            }
        }

        size_t uploads = 0, downloads = 0, perRound = 0;
        for (const Crossing &crossing : crossings) {
            (crossing.upload ? uploads : downloads) += 1;
            perRound += crossing.perRound ? 1 : 0;
        }
        nlohmann::json entry;
        entry["graph"]     = graph->name();
        entry["body"]      = className(bodyPlacementOf(*graph).placement);
        entry["blocks"]    = {{"device", deviceValues}, {"host", hostValues}};
        entry["uploads"]   = uploads;
        entry["downloads"] = downloads;
        nlohmann::json values = nlohmann::json::array();
        for (const Crossing &crossing : crossings) {
            nlohmann::json row;
            row["value"]  = crossing.value;
            row["kind"]   = crossing.upload ? "upload" : "download";
            row["region"] = crossing.region;
            if (const GCRegion *around = graph->region(crossing.region)) {
                row["where"] = regionClassName(around->kind);
            }
            values.push_back(std::move(row));
        }
        entry["crossings"] = std::move(values);
        entry["per_round"] = perRound;
        entry["calls"]     = std::move(calls);
        out << "TILES_RESIDENCY " << entry.dump() << '\n';
    }
    return out.str();
}

} // namespace camel::tiles