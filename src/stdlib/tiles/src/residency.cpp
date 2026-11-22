// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The residency conclusion (docs/design/gpu/plan.cn.md 3.2), as the boundary decision:
//
//   * a value crosses a boundary only when its block changes: one `upload` for a host value a
//     segment reads, one `download` for a device value the host observes;
//   * a segment's members are the device's -- its kernels write them, wherever the region was
//     written -- so a segment reads its inputs on the device and only what the host observes comes
//     back;
//   * a value crosses once, however many readers it has, and a value one segment hands to the next
//     stays where it is: the same block, no copy;
//   * a value nothing live reads is not a boundary at all.
//
// Where each value's block lives, and what the host observes, is the reading the caller feeds this
// decision with (`ResidencyFacts`, and `planResidency`'s graph overload with the graph reader): the
// decision asks nothing about the graph's own operators or types.
//
// The conclusion is a reading, not a rewrite: it says which transfers the program needs and where
// they belong. `TILES_RESIDENCY` prints it next to the placement report so a test can pin the
// numbers, and the pass that inserts the nodes takes its answer from here -- `planResidency` is the
// boundary decision, and nothing else decides a transfer.

#include "tiles/internal/residency.h"

#include "tiles/internal/placement.h"
#include "tiles/internal/regions.h"

#include "camel/runtime/program.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>
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
namespace rt = camel::runtime;

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

ResidencyPlan planResidency(
    const ResidencyFacts &facts, const std::vector<RegionNode> &nodes,
    const std::vector<Segment> &regions) {
    const auto &values = facts.values;
    const size_t none  = regions.size();
    std::vector<size_t> owner(values.size(), none);
    std::vector<bool> device(values.size());
    for (size_t id = 0; id < values.size(); ++id)
        device[id] = values[id].block.onDevice();
    for (size_t r = 0; r < regions.size(); ++r)
        for (auto id : regions[r].members) {
            if (id >= values.size() || owner[id] != none)
                throw std::runtime_error("invalid residency region membership");
            owner[id]  = r;
            device[id] = true; // A segment's kernels write its members, so they live on the device.
        }
    // Liveness, as the partition reads it: what the graph's own values depend on, values and order
    // edges alike.
    std::vector<bool> live(nodes.size());
    std::vector<size_t> pending;
    for (size_t id = 0; id < nodes.size(); ++id)
        if (nodes[id].output)
            pending.push_back(id);
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (id >= nodes.size())
            throw std::runtime_error("residency dependency outside graph");
        if (live[id])
            continue;
        live[id] = true;
        for (auto input : nodes[id].inputs)
            pending.push_back(input);
        for (auto input : nodes[id].order)
            pending.push_back(input);
    }
    // The observation policy: a value is observed when the host reads it. A node outside every
    // segment reads its operands in the types its own node declares -- ordinary ones -- the way the
    // runtime reads them, so a value a segment wrote has to be written back before such a reader
    // runs: a host operator, a branch's join, a loop's condition, a gate, a call. A segment's own
    // kernels read theirs in the segment's types, which the extraction writes, so a reader inside a
    // segment observes nothing; and a reader that takes the value where it lies -- a call whose
    // callee declares device parameters -- observes nothing either. A value nothing live reads is
    // nobody's observation, so the device keeps it.
    std::vector<bool> observed(values.size());
    for (size_t ref = 0; ref < nodes.size() && ref < values.size(); ++ref) {
        if (!live[ref] || values[ref].readsInPlace || owner[ref] != none)
            continue;
        for (auto input : nodes[ref].inputs)
            if (input < observed.size())
                observed[input] = true;
    }
    if (facts.returnsToHost)
        for (size_t id = 0; id < nodes.size() && id < observed.size(); ++id)
            if (nodes[id].output)
                observed[id] = true;
    std::set<size_t> uploads, downloads;
    ResidencyPlan result;
    for (size_t r = 0; r < regions.size(); ++r) {
        SegmentResidency segment;
        segment.region = r;
        for (auto id : regions[r].inputs) {
            if (id >= values.size() || owner[id] == r)
                throw std::runtime_error("invalid residency region input");
            if (nodes[id].embedded)
                continue; // The extraction puts the value in the segment's own graph.
            if (!device[id]) {
                uploads.insert(id);
                segment.uploads.push_back(id);
                continue;
            }
            if (owner[id] == none)
                continue; // Written on the device outside every segment: the segment reads it there.
            const auto &outputs = regions[owner[id]].outputs;
            if (owner[id] >= r || std::find(outputs.begin(), outputs.end(), id) == outputs.end())
                throw std::runtime_error("resident input lacks an earlier region output");
            segment.resident.push_back({id, owner[id], r});
            result.resident.push_back({id, owner[id], r});
        }
        for (auto id : regions[r].outputs) {
            if (id >= values.size() || owner[id] != r)
                throw std::runtime_error("invalid residency region output");
            if (!observed[id])
                continue;
            downloads.insert(id);
            segment.downloads.push_back(id);
        }
        result.segments.push_back(std::move(segment));
    }
    result.uploads.assign(uploads.begin(), uploads.end());
    result.downloads.assign(downloads.begin(), downloads.end());
    return result;
}

std::string residencyReport(const rt::Program &program) {
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
