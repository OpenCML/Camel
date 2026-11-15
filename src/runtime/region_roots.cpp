// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The roots a region has (camel/runtime/region_roots.h). The frozen-graph half; the draft half is
 * the templates in the header, so neither file names a draft's type.
 */

#include "camel/runtime/region_roots.h"

#include "camel/core/operator_table.h"

namespace camel::runtime {

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
    return detail::decideOf(
        static_cast<uint32_t>(graph.regionCount()),
        [&graph](uint32_t id) { return graph.region(id); },
        graph.nodeRegion(ref));
}

gc_node_ref_t decidingOwnerOf(const GCGraph &graph, uint32_t region) {
    if (region == 0) {
        return kInvalidNodeRef;
    }
    const GCRegion *one = graph.region(region);
    return one != nullptr ? one->owner : kInvalidNodeRef;
}

std::vector<RegionRootCandidate> regionRootCandidatesOf(const GCGraph &graph) {
    return detail::candidatesWith(
        graph.nodeCount(),
        [&graph](gc_node_ref_t ref) -> GCNodeKind {
            const GCNode *node = graph.node(ref);
            return node != nullptr ? node->kind : GCNodeKind::Dead;
        },
        [&graph](gc_node_ref_t ref) { return decidingRegionOf(graph, ref); },
        [&graph](gc_node_ref_t ref) {
            const auto *body = graph.nodeBodyAs<GCOperBody>(ref);
            return body != nullptr ? body->oper : camel::core::kNoOper;
        });
}

} // namespace camel::runtime