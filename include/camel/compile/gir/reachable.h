// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "types.h"

#include <functional>

namespace camel::compile::gir {

/*
 * Reachability here follows the semantic graph closure used by execution and
 * bytecode linking:
 * - named subgraphs
 * - dependency edges
 * - direct FUNC body graphs
 * - function-valued DATA constants
 *
 * The traversal is pre-order DFS and visits each graph at most once.
 */
std::vector<graph_ptr_t> collectReachableGraphs(const graph_ptr_t &root);

void forEachReachableGraph(
    const graph_ptr_t &root, const std::function<void(const graph_ptr_t &)> &visitor);

} // namespace camel::compile::gir
