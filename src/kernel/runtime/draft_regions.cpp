// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* The regions of a graph under construction (camel/runtime/graph.h, GCRegion). */

#include "camel/runtime/draft.h"

#include <algorithm>
#include <stdexcept>

namespace camel::runtime {

uint32_t GraphDraft::nodeRegion(gc_node_ref_t id) const {
    const DraftNodeHeader *h = header(id);
    // A node that is not in the draft has no region. Answering 0 would name the body, and 0 is a
    // region the draft really has -- `region(0)` answers the body even when the table holds nothing
    // else, and the record is written as soon as anything needs it -- so "region 0" is an actual
    // membership, not "I do not know": the asker would be told that a node it can no longer find
    // belongs to the graph's body, and would then order it, report it or read its ports as if it
    // were there. There is no region to give, so fail instead of naming one.
    GUARD(
        h != nullptr,
        "Region of a node that is not in the draft: the id was erased or never existed, and "
        "region 0 is the body, a real region, not the answer for a node that is not there.");
    return h->region;
}

void GraphDraft::setNodeRegion(gc_node_ref_t id, uint32_t region) {
    requireOneWriting("setNodeRegion");
    ++mutations_;
    DraftNode *draftNode = node(id);
    ASSERT(draftNode != nullptr, "Cannot put a missing draft node in a region.");
    ASSERT(region == 0 || region < regions_.size(), "A node is put in a region that is not there.");
    noteOriginalNode(id);
    draftNode->header.region = region;
}

uint32_t
GraphDraft::addRegion(GCRegionKind kind, uint32_t parent, gc_node_ref_t owner, uint32_t index) {
    noteOriginalRegions();
    ++mutations_;
    ASSERT(!regions_.empty(), "A draft is made with its body region, so the table is not empty.");
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
    const uint32_t id     = addRegion(GCRegionKind::Inline, parent);
    regions_[id].inlineOf = callee;
    regions_[id].callSite = callSite;
    return id;
}

void GraphDraft::setBrchJoin(gc_node_ref_t brch, gc_node_ref_t join) {
    noteOriginalNode(brch);
    ++mutations_;
    DraftNode *node = this->node(brch);
    ASSERT(node != nullptr && node->header.kind == GCNodeKind::Brch, "Not a branch node.");
    DraftNodeView::payloadAs<DraftBrchPayload>(node)->join = join;
}

void GraphDraft::setRegionResult(uint32_t id, gc_node_ref_t result) {
    noteOriginalRegions();
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    regions_[id].result = result;
}

void GraphDraft::setRegionPorts(uint32_t id, std::span<const gc_node_ref_t> ports) {
    noteOriginalRegions();
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    // The ports of a region go at the end of the table; the room of the old ones stays unused
    // until the graph is encoded.
    regions_[id].portBegin = static_cast<uint32_t>(regionPorts_.size());
    regions_[id].portCount = static_cast<uint32_t>(ports.size());
    regionPorts_.insert(regionPorts_.end(), ports.begin(), ports.end());
}

void GraphDraft::setRegionInline(uint32_t id, GraphId callee, gc_node_ref_t callSite) {
    noteOriginalRegions();
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    regions_[id].inlineOf = callee;
    regions_[id].callSite = callSite;
}

void GraphDraft::setRegionOwner(uint32_t id, gc_node_ref_t owner, uint32_t index) {
    noteOriginalRegions();
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    regions_[id].owner = owner;
    regions_[id].index = index;
}

void GraphDraft::inheritRegion(gc_node_ref_t input, gc_node_ref_t user, DraftEdgeSlot slot) {
    if (regions_.size() <= 1 || !inheritsRegions_) {
        return;
    }
    // Only a value read pulls a node into a region. An order or a control edge says when the user
    // runs, not what it reads: moving the user to where the producer runs would change when the
    // user runs -- an effect would run once per pass of a loop it was never written in -- and it
    // would leave the edges the user already carried pointing out of the region it was moved into.
    // A pair like that is ordered on the node that owns the region (docs/design/effects/edges.cn.md
    // 4.6), which is the writer's business, not this move's.
    if (slot != DraftEdgeSlot::NormalPorts && slot != DraftEdgeSlot::ContextPorts) {
        return;
    }
    DraftNode *reader     = node(user);
    const DraftNode *read = node(input);
    // A JOIN reads the results of its arms, and a LOOP those of its parts.
    if (reader == nullptr || read == nullptr || reader->header.kind == GCNodeKind::Join ||
        reader->header.kind == GCNodeKind::Loop) {
        return;
    }
    // A node already attached to a chain stays in that chain's region. An unattached node has no
    // chain place to preserve; a value an inner region computes is reached through its result.
    if (!syncInputsOf(user).empty() || !syncUsersOf(user).empty()) {
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
    if (readControl == 0 || readControl == readerControl ||
        regionWithin(readerControl, readControl)) {
        return;
    }
    if (regionWithin(readControl, readerControl)) {
        // The move is a write of the node's region, so it goes through the setter that records it
        // (`noteOriginalNode`): a node a rewrite pulled into a branch arm and then took back is a
        // node the rollback has to put back where it stood.
        setNodeRegion(user, readControl);
    }
}

void GraphDraft::dissolveRegionsOf(gc_node_ref_t id) {
    noteOriginalRegions();
    for (uint32_t r = 1; r < regions_.size(); ++r) {
        GCRegion &region   = regions_[r];
        const bool control = region.kind == GCRegionKind::Arm ||
                             region.kind == GCRegionKind::LoopCond ||
                             region.kind == GCRegionKind::LoopBody;
        if (!control || region.owner != id) {
            continue;
        }
        const uint32_t around = region.parent;
        for (DraftNode *held : nodesById_) {
            if (held && held->header.region == r) {
                setNodeRegion(held->header.selfId, around);
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
    noteOriginalRegions();
    for (uint32_t r = 1; r < regions_.size(); ++r) {
        if (regions_[r].kind == GCRegionKind::Dead || !regionWithin(r, id) ||
            (keepRegion && r == id)) {
            continue;
        }
        for (DraftNode *held : nodesById_) {
            if (held && held->header.region == r) {
                setNodeRegion(held->header.selfId, around);
            }
        }
        regions_[r].kind   = GCRegionKind::Dead;
        regions_[r].owner  = kInvalidNodeRef;
        regions_[r].result = kInvalidNodeRef;
    }
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
    noteOriginalRegions();
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
            setNodeRegion(held->header.selfId, 0);
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

/// The control regions a node runs with, outermost first: the arms and the parts of a loop. A
/// region that only records where code came from -- an inlined body, a tagged block -- is looked
/// through, because the code inside it is the code of the block it was moved into.
namespace {

std::vector<uint32_t> controlChainOf(const GraphDraft &draft, const std::vector<GCRegion> &regions,
                                     gc_node_ref_t id) {
    std::vector<uint32_t> chain;
    for (uint32_t region = draft.nodeRegion(id); region != 0 && region < regions.size();
         region = regions[region].parent) {
        const GCRegionKind kind = regions[region].kind;
        if (kind == GCRegionKind::Arm || kind == GCRegionKind::LoopCond ||
            kind == GCRegionKind::LoopBody) {
            chain.push_back(region);
        }
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

} // namespace

size_t GraphDraft::rehomeOrderEdges() {
    if (regions_.size() <= 1) {
        return 0;
    }
    const size_t count = nodeSlotCount();
    std::vector<std::vector<gc_node_ref_t>> wanted(count);
    std::vector<uint8_t> touched(count, 0);
    for (gc_node_ref_t user = 0; user < static_cast<gc_node_ref_t>(count); ++user) {
        if (!alive(user)) {
            continue;
        }
        for (gc_node_ref_t pred : orderInputsOf(user)) {
            gc_node_ref_t from  = pred;
            gc_node_ref_t to    = user;
            // The deeper end of an order edge that reaches into a region is the region itself, and
            // the node that stands for it is its owner (docs/design/effects/edges.cn.md 4.6). Its
            // own place may still be inside another region, so the walk repeats.
            for (;;) {
                const std::vector<uint32_t> fromChain = controlChainOf(*this, regions_, from);
                const std::vector<uint32_t> toChain   = controlChainOf(*this, regions_, to);
                size_t common                         = 0;
                while (common < fromChain.size() && common < toChain.size() &&
                       fromChain[common] == toChain[common]) {
                    ++common;
                }
                if (common == fromChain.size() && common == toChain.size()) {
                    break; // the two ends run in the control regions that hold them both
                }
                bool rehomed = false;
                if (common < fromChain.size()) {
                    const gc_node_ref_t owner = regions_[fromChain[common]].owner;
                    if (owner != kInvalidNodeRef && owner != from && alive(owner)) {
                        from   = owner;
                        rehomed = true;
                    }
                }
                if (!rehomed && common < toChain.size()) {
                    const gc_node_ref_t owner = regions_[toChain[common]].owner;
                    if (owner != kInvalidNodeRef && owner != to && alive(owner)) {
                        to      = owner;
                        rehomed = true;
                    }
                }
                if (!rehomed) {
                    break; // nothing to hang the edge on: the seal check says so, not this pass
                }
            }
            wanted[to].push_back(from);
            if (from != pred || to != user) {
                touched[user] = 1;
                touched[to]   = 1;
            }
        }
    }
    size_t changed = 0;
    for (gc_node_ref_t user = 0; user < static_cast<gc_node_ref_t>(count); ++user) {
        if (!touched[user]) {
            continue;
        }
        std::vector<gc_node_ref_t> inputs;
        for (gc_node_ref_t pred : wanted[user]) {
            // An edge whose two ends came out as one node is gone: the owner runs before the code
            // written inside its region, so the edge says nothing it has to keep.
            if (pred != user && std::ranges::find(inputs, pred) == inputs.end()) {
                inputs.push_back(pred);
            }
        }
        const std::span<const gc_node_ref_t> held = orderInputsOf(user);
        if (inputs == std::vector<gc_node_ref_t>(held.begin(), held.end())) {
            continue;
        }
        setOrderInputs(user, inputs);
        ++changed;
    }
    return changed;
}

void GraphDraft::clearRegions() {
    // The table goes and every node comes out of its region: both halves are recorded, the table
    // once (`noteOriginalRegions`) and each node through the setter that records it.
    noteOriginalRegions();
    ++mutations_;
    for (DraftNode *held : nodesById_) {
        if (held) {
            setNodeRegion(held->header.selfId, 0);
        }
    }
    regions_.clear();
    regions_.push_back(GCRegion{}); // the body, which every draft has
    regionPorts_.clear();
}

void GraphDraft::setRegionOrigin(uint32_t id, uint32_t origin) {
    noteOriginalRegions();
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    regions_[id].origin = origin;
}

void GraphDraft::setRegionParams(uint32_t id, const camel::core::type::FunctionType *params) {
    noteOriginalRegions();
    ++mutations_;
    ASSERT(id > 0 && id < regions_.size(), "No such region.");
    regions_[id].params = params;
}

void GraphDraft::setRegionAttrs(uint32_t id, uint32_t attrs) {
    noteOriginalRegions();
    ++mutations_;
    ASSERT(id < regions_.size(), "No such region.");
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

bool GraphDraft::inlinedCopyOf(gc_node_ref_t id, const GCGraph *callee) const {
    // `1` is the body and nothing else: every draft is made with its body region (GraphDraft), and
    // an inlined body is a region of its own -- `addInlineRegion` makes one through `addRegion`,
    // which hands out an id from 1 up even when the parent is the body. So a table of one holds no
    // Inline region, and no node of this draft can be inside a body inlined here. A draft whose own
    // graph was inlined somewhere holds that Inline region beside its body, and the table is two.
    if (callee == nullptr || regions_.size() <= 1) {
        return false;
    }
    // What the call is inside of, by the function it is a copy of: a type specialization is that
    // function with a more precise type, so it counts as the same callee (design 9.4). A value
    // specialization (a fixed-length unrolling: the same function with different constants) is a
    // callee of its own, which is what lets `export_gated_gru_step` unroll.
    const auto functionOf = [](const GCGraph *graph, GraphId fallback) {
        if (graph == nullptr) {
            return fallback;
        }
        return isValid(graph->typeSpecializationRoot()) ? graph->typeSpecializationRoot()
                                                        : graph->id();
    };
    const GraphId calleeFunction = functionOf(callee, callee->id());
    // The region a call is in says what inlined body it belongs to; a region that is the body of
    // a function inlined at its call carries the same source (an Arm region that became one).
    for (uint32_t region = nodeRegion(id); region != 0 && region < regions_.size();
         region          = regions_[region].parent) {
        const GraphId inlineOf = regions_[region].inlineOf;
        if (inlineOf == callee->id()) {
            return true;
        }
        const auto *source = program_ != nullptr ? program_->graph(inlineOf) : nullptr;
        if (functionOf(source, inlineOf) == calleeFunction) {
            return true;
        }
    }
    return false;
}

} // namespace camel::runtime
