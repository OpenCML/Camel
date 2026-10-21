/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/* The regions of a graph under construction (camel/runtime/graph.h, GCRegion). */
#include "camel/runtime/draft.h"


#include <stdexcept>

namespace camel::runtime {

uint32_t GraphDraft::nodeRegion(gc_node_ref_t id) const {
    const DraftNodeHeader *h = header(id);
    return h ? h->region : 0;
}

void GraphDraft::setNodeRegion(gc_node_ref_t id, uint32_t region) {
    requireOneWriting("setNodeRegion");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot put a missing draft node in a region.");
    ASSERT(region == 0 || region < regions_.size(), "A node is put in a region that is not there.");
    draftNode->header.region = region;
}

uint32_t GraphDraft::addRegion(
    GCRegionKind kind, uint32_t parent, gc_node_ref_t owner, uint32_t index) {
    ++mutations_;
    if (regions_.empty()) {
        regions_.push_back(GCRegion{});
    }
    ASSERT(parent < regions_.size(), "A region is made in a region that is not there.");
    GCRegion region;
    region.kind   = kind;
    region.parent = parent;
    region.owner  = owner;
    region.index  = index;
    regions_.push_back(region);
    return static_cast<uint32_t>(regions_.size() - 1);
}

uint32_t GraphDraft::addInlineRegion(uint32_t parent, GraphId callee, gc_node_ref_t callSite) {
    const uint32_t id = addRegion(GCRegionKind::Inline, parent);
    regions_[id].inlineOf = callee;
    regions_[id].callSite = callSite;
    return id;
}

void GraphDraft::setRegionResult(uint32_t id, gc_node_ref_t result) {
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    regions_[id].result = result;
}

void GraphDraft::setRegionPorts(uint32_t id, std::span<const gc_node_ref_t> ports) {
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    // The ports of a region go at the end of the table; the room of the old ones stays unused
    // until the graph is encoded.
    regions_[id].portBegin = static_cast<uint32_t>(regionPorts_.size());
    regions_[id].portCount = static_cast<uint32_t>(ports.size());
    regionPorts_.insert(regionPorts_.end(), ports.begin(), ports.end());
}

void GraphDraft::setRegionInline(uint32_t id, GraphId callee, gc_node_ref_t callSite) {
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    regions_[id].inlineOf = callee;
    regions_[id].callSite = callSite;
}

void GraphDraft::setRegionOwner(uint32_t id, gc_node_ref_t owner, uint32_t index) {
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    regions_[id].owner = owner;
    regions_[id].index = index;
}

void GraphDraft::inheritRegion(gc_node_ref_t input, gc_node_ref_t user) {
    if (regions_.size() <= 1 || !inheritsRegions_) {
        return;
    }
    DraftNode *reader = node(user);
    const DraftNode *read = node(input);
    // A JOIN reads the results of its arms, and a LOOP those of its parts.
    if (reader == nullptr || read == nullptr || reader->header.kind == GCNodeKind::Join ||
        reader->header.kind == GCNodeKind::Loop) {
        return;
    }
    // Inline and Block regions only say where code came from: their values are read from outside.
    const auto controlOf = [&](uint32_t region) {
        while (region != 0 && region < regions_.size() &&
               (regions_[region].kind == GCRegionKind::Inline ||
                regions_[region].kind == GCRegionKind::Block ||
                regions_[region].kind == GCRegionKind::Dead)) {
            region = regions_[region].parent;
        }
        return region;
    };
    const uint32_t readControl   = controlOf(read->header.region);
    const uint32_t readerControl = controlOf(reader->header.region);
    if (readControl == 0 || readControl == readerControl || regionWithin(readerControl, readControl)) {
        return;
    }
    if (regionWithin(readControl, readerControl)) {
        reader->header.region = readControl;
    }
}

void GraphDraft::dissolveRegionsOf(gc_node_ref_t id) {
    for (uint32_t r = 1; r < regions_.size(); ++r) {
        GCRegion &region = regions_[r];
        const bool control = region.kind == GCRegionKind::Arm ||
                             region.kind == GCRegionKind::LoopCond ||
                             region.kind == GCRegionKind::LoopBody;
        if (!control || region.owner != id) {
            continue;
        }
        const uint32_t around = region.parent;
        for (DraftNode *held : nodesById_) {
            if (held && held->header.region == r) {
                held->header.region = around;
            }
        }
        for (uint32_t other = 1; other < regions_.size(); ++other) {
            if (regions_[other].kind != GCRegionKind::Dead && regions_[other].parent == r) {
                regions_[other].parent = around;
            }
        }
        regions_[r].kind   = GCRegionKind::Dead;
        regions_[r].owner  = kInvalidNodeRef;
        regions_[r].result = kInvalidNodeRef;
        ++mutations_;
    }
}

void GraphDraft::retireRegion(uint32_t id, bool keepRegion) {
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    const uint32_t around = regions_[id].parent;
    for (uint32_t r = 1; r < regions_.size(); ++r) {
        if (regions_[r].kind == GCRegionKind::Dead || !regionWithin(r, id) ||
            (keepRegion && r == id)) {
            continue;
        }
        for (DraftNode *held : nodesById_) {
            if (held && held->header.region == r) {
                held->header.region = around;
            }
        }
        regions_[r].kind   = GCRegionKind::Dead;
        regions_[r].owner  = kInvalidNodeRef;
        regions_[r].result = kInvalidNodeRef;
    }
}

bool GraphDraft::isArmCall(gc_node_ref_t id) const {
    const uint32_t r = nodeRegion(id);
    return r != 0 && r < regions_.size() && regions_[r].kind == GCRegionKind::Arm &&
           regions_[r].callSite == id;
}

uint32_t GraphDraft::loopRegionOf(gc_node_ref_t loop, GCRegionKind part) const {
    for (uint32_t r = 1; r < regions_.size(); ++r) {
        if (regions_[r].kind == part && regions_[r].owner == loop) {
            return r;
        }
    }
    return 0;
}

uint32_t GraphDraft::armRegionOf(gc_node_ref_t brch, size_t index) const {
    for (uint32_t r = 1; r < regions_.size(); ++r) {
        if (regions_[r].kind == GCRegionKind::Arm && regions_[r].owner == brch &&
            regions_[r].index == index) {
            return r;
        }
    }
    return 0;
}

void GraphDraft::flattenRegion(uint32_t id) {
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    // The regions in `id` stay, as regions of the body; every other region ends, its nodes go to
    // the body.
    std::vector<uint8_t> kept(regions_.size(), 0);
    for (uint32_t r = 1; r < regions_.size(); ++r) {
        kept[r] = r != id && regions_[r].kind != GCRegionKind::Dead && regionWithin(r, id);
    }
    for (DraftNode *held : nodesById_) {
        if (held && !kept[held->header.region]) {
            held->header.region = 0;
        }
    }
    for (uint32_t r = 1; r < regions_.size(); ++r) {
        if (kept[r]) {
            if (regions_[r].parent == id) {
                regions_[r].parent = 0;
            }
            continue;
        }
        regions_[r].kind   = GCRegionKind::Dead;
        regions_[r].owner  = kInvalidNodeRef;
        regions_[r].result = kInvalidNodeRef;
    }
}

void GraphDraft::clearRegions() {
    ++mutations_;
    for (DraftNode *held : nodesById_) {
        if (held) {
            held->header.region = 0;
        }
    }
    regions_.clear();
    regionPorts_.clear();
}

void GraphDraft::setRegionOrigin(uint32_t id, uint32_t origin) {
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    regions_[id].origin = origin;
}

void GraphDraft::setRegionParams(uint32_t id, const camel::core::type::FunctionType *params) {
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    regions_[id].params = params;
}

void GraphDraft::setRegionAttrs(uint32_t id, uint32_t attrs) {
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    regions_[id].attrs = attrs;
}

bool GraphDraft::regionWithin(uint32_t inner, uint32_t outer) const {
    while (inner != outer) {
        if (inner == 0 || inner >= regions_.size()) {
            return false;
        }
        inner = regions_[inner].parent;
    }
    return true;
}

void GraphDraft::regionizeArmCalls() {
    for (gc_node_ref_t brch : brchIds_) {
        const DraftNodeHeader *h = header(brch);
        if (!h || h->kind != GCNodeKind::Brch) {
            continue;
        }
        // The call of an arm is what the JOIN reads of it, ordered after the BRCH.
        const gc_node_ref_t join =
            reinterpret_cast<const DraftBrchPayload *>(payloadOf(brch).data())->join;
        const std::vector<gc_node_ref_t> results(
            withInputsOf(join).begin(),
            withInputsOf(join).end());
        for (uint32_t k = 0; k < results.size(); ++k) {
            const gc_node_ref_t call = results[k];
            if (!alive(call) || header(call)->kind != GCNodeKind::Func || nodeRegion(call) != 0 ||
                std::ranges::find(ctrlInputsOf(call), brch) == ctrlInputsOf(call).end()) {
                continue;
            }
            const uint32_t region = addRegion(GCRegionKind::Arm, nodeRegion(brch), brch, k);
            setNodeRegion(call, region);
            setRegionResult(region, call);
            setRegionInline(region, kInvalidGraphId, call);
        }
    }
}

} // namespace camel::runtime
