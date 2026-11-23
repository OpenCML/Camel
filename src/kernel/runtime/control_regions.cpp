// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The control-region reader (camel/runtime/control_regions.h). The tree is the fact
 * `core.controlRegions` and is built there; this file only binds a graph's tree and the view the
 * tree was made from.
 */

#include "camel/runtime/control_regions.h"

#include "camel/runtime/graph.h"

#include <stdexcept>

namespace camel::runtime {

ControlRegions::ControlRegions(const GCGraph &graph)
    : view_(&graph.view()), tree_(camel::facts::controlRegionsOf(graph)) {
    if (!tree_) {
        // The fact layer has no tree for a graph it was handed: that is a defect in the kind, not
        // a state to guess an answer for (facts.h: a kind's compute answers, or says nothing).
        throw std::logic_error("core.controlRegions has no tree for the graph it was asked about");
    }
}

} // namespace camel::runtime