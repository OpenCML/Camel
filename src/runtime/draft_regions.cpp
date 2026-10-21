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

void GraphDraft::setRegionOrigin(uint32_t id, uint32_t origin) {
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    regions_[id].origin = origin;
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

} // namespace camel::runtime
