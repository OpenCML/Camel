// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The roots a region has (camel/runtime/region_roots.h).
 */

#include "camel/runtime/region_roots.h"

#include "camel/core/operator_table.h"

namespace camel::runtime {

namespace {

/// The kinds of region the program chooses to run or not. Everything else -- the body, an inlined
/// body, a tagged block, a region a rewrite removed -- is transparent: a node in it runs with the
/// region around it, which is why `decidingRegionOf` walks past it instead of stopping there.
bool isDeciding(GCRegionKind kind) {
    switch (kind) {
    case GCRegionKind::Arm:
    case GCRegionKind::LoopCond:
    case GCRegionKind::LoopBody:
        return true;
    case GCRegionKind::Root:
    case GCRegionKind::Inline:
    case GCRegionKind::Block:
    case GCRegionKind::Dead:
        break;
    }
    return false;
}

template <typename RegionAt>
uint32_t decideOf(uint32_t regionCount, const RegionAt &regionAt, uint32_t at) {
    // The ids of a graph are not reused and a region's parent is always a region the graph holds
    // (0 is every region's ancestor), so the walk ends: at the body, or at a region of a kind the
    // program does not choose.
    for (uint32_t depth = 0; depth <= regionCount; ++depth) {
        if (at == 0) {
            return 0;
        }
        const GCRegion *one = regionAt(at);
        if (one == nullptr) {
            return 0; // a region the table does not hold is not a place the program chose
        }
        if (isDeciding(one->kind)) {
            return at;
        }
        if (one->parent == at) {
            return 0; // a region that is its own parent: the walk would not end
        }
        at = one->parent;
    }
    return 0;
}

template <typename KindOf, typename RegionOfNode, typename OperOf>
std::vector<RegionRootCandidate> candidatesWith(
    gc_cnt_t nodeCount, const KindOf &kindOf, const RegionOfNode &regionOfNode,
    const OperOf &operOf) {
    std::vector<RegionRootCandidate> candidates;
    for (gc_node_ref_t ref = 0; ref < nodeCount; ++ref) {
        const GCNodeKind kind = kindOf(ref);
        if (kind == GCNodeKind::Dead) {
            continue; // no node in this slot (a draft slot a rewrite emptied)
        }
        // Only an operator writes by itself (`writesOutsideResource` answers the same), and the test
        // is here as well so that the operator of a node that has none is never read.
        if (kind != GCNodeKind::Oper) {
            continue;
        }
        if (!writesOutsideResource(kind, operOf(ref))) {
            continue;
        }
        candidates.push_back(RegionRootCandidate{ref, regionOfNode(ref)});
    }
    return candidates;
}

} // namespace

bool writesOutsideResource(GCNodeKind kind, camel::core::OperId oper) {
    if (kind != GCNodeKind::Oper) {
        // A FUNC or a CALL runs a graph, and what that graph writes is written in the graph's own
        // body, where it is a root already (design principles 3.2: a function's behaviour is its
        // own). A BRCH or a LOOP is its regions, and the nodes of a region are roots of the
        // region, not of the region's owner (3.1: a branch is entered by the arm that is taken).
        return false;
    }
    if (oper == camel::core::kNoOper) {
        return false;
    }
    return camel::core::OperatorTable::instance().writesOutside(oper);
}

uint32_t decidingRegionOf(const GCGraph &graph, gc_node_ref_t ref) {
    return decideOf(
        static_cast<uint32_t>(graph.regionCount()),
        [&graph](uint32_t id) { return graph.region(id); },
        graph.nodeRegion(ref));
}

uint32_t decidingRegionOf(const GraphDraft &draft, gc_node_ref_t ref) {
    return decideOf(
        static_cast<uint32_t>(draft.regionCount()),
        [&draft](uint32_t id) -> const GCRegion * {
            // `GraphDraft::region` GUARDs an id the draft does not hold, and a parent id read out of
            // a stale record is not a reason to abort a reading of the tree: an id nobody holds is a
            // region the program did not choose (the walk's own null answer).
            return id < draft.regionCount() ? &draft.region(id) : nullptr;
        },
        draft.nodeRegion(ref));
}

gc_node_ref_t decidingOwnerOf(const GCGraph &graph, uint32_t region) {
    if (region == 0) {
        return kInvalidNodeRef;
    }
    const GCRegion *one = graph.region(region);
    return one != nullptr ? one->owner : kInvalidNodeRef;
}

gc_node_ref_t decidingOwnerOf(const GraphDraft &draft, uint32_t region) {
    if (region == 0 || region >= draft.regionCount()) {
        return kInvalidNodeRef;
    }
    return draft.region(region).owner;
}

std::vector<RegionRootCandidate> regionRootCandidatesOf(const GCGraph &graph) {
    const auto regionOfNode = [&graph](gc_node_ref_t ref) {
        return decidingRegionOf(graph, ref);
    };
    const auto operOf = [&graph](gc_node_ref_t ref) {
        const auto *body = graph.nodeBodyAs<GCOperBody>(ref);
        return body != nullptr ? body->oper : camel::core::kNoOper;
    };
    return candidatesWith(
        graph.nodeCount(),
        [&graph](gc_node_ref_t ref) -> GCNodeKind {
            const GCNode *node = graph.node(ref);
            return node != nullptr ? node->kind : GCNodeKind::Dead;
        },
        regionOfNode,
        operOf);
}

std::vector<RegionRootCandidate> regionRootCandidatesOf(const GraphDraft &draft) {
    const auto regionOfNode = [&draft](gc_node_ref_t ref) {
        return decidingRegionOf(draft, ref);
    };
    const auto operOf = [&draft](gc_node_ref_t ref) {
        if (!draft.alive(ref)) {
            return camel::core::kNoOper;
        }
        const std::span<const std::byte> payload = draft.payloadOf(ref);
        if (payload.size() < sizeof(GCOperBody)) {
            return camel::core::kNoOper;
        }
        return reinterpret_cast<const GCOperBody *>(payload.data())->oper;
    };
    return candidatesWith(
        static_cast<gc_cnt_t>(draft.nodeSlotCount()),
        [&draft](gc_node_ref_t ref) -> GCNodeKind {
            if (!draft.alive(ref)) {
                return GCNodeKind::Dead; // a slot a rewrite emptied
            }
            const DraftNodeHeader *header = draft.header(ref);
            return header != nullptr ? header->kind : GCNodeKind::Dead;
        },
        regionOfNode,
        operOf);
}

} // namespace camel::runtime