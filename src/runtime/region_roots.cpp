// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Which region decides whether a node runs (camel/runtime/region_roots.h). The frozen-graph half;
 * the draft half is the templates in the header, so neither file names a draft's type.
 *
 * What a region owes its writes is not answered here: it is an edge laid when the graph is
 * built (`Builder::layRegionWriteEdges`).
 */

#include "camel/runtime/region_roots.h"

#include "camel/core/operator_table.h"

namespace camel::runtime {

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

} // namespace camel::runtime
