// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operator half of `remat`: `remat(f)` as a native operator of the `transforms` module.
 *
 * `remat` is a transform, not a library identity: the call answers with a function value whose
 * graph is *a new graph whose body is `f`'s body* plus the graph property
 * `kGraphFlagRecomputeBackward` (camel/runtime/graph.h). A call through it is the same call -- the
 * body is the same code, so no value changes -- and the property is what a step that cuts a graph
 * in two reads to decide what it has to keep across the cut (`split`, split.cpp).
 *
 * The copy is made when the call runs, by cloning `f`'s graph into a member of a `GraphDraftGroup`
 * (the shape `split` uses for the graphs it builds); the clone is the whole graph, ports and exits
 * included, so the two graphs are the same function. The author's ruling of 2026-11-01 puts the
 * mark on the *graph* and not on a call site and not in a tag, and says the transform is
 * instantiated with the compile step; until that step exists the mark is set here, where a call can
 * carry it.
 */

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

/// The operator group of the marking step (`transforms:remat`), beside the module's others.
const std::vector<oper_group_ptr_t> &rematOperatorGroups();
std::unordered_map<std::string, operator_t> rematOperatorKernels();
/// Publishes the purity and the graph-reading traits of `remat`.
void registerRematOperatorMetadata();

} // namespace camel::transforms