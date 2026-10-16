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
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

/* Bounded analysis and reduction of unconditional control dependencies. */
#pragma once

#include "camel/runtime/draft.h"

namespace camel::runtime {

/// Proves an existing CTRL-only path from predecessor to user, without crossing BRCH/JOIN.
/// Exhausting maxWork means "not proven", so callers must retain the edge.
bool hasControlDependency(
    const GraphDraft &draft, gc_node_ref_t predecessor, gc_node_ref_t user, size_t maxWork = 64);

/// Retains a sufficient frontier of predecessors before a rewrite transfers their ordering.
/// Uses one shared search budget for the entire list and preserves uncertain dependencies.
std::vector<gc_node_ref_t> controlFrontier(
    const GraphDraft &draft, std::span<const gc_node_ref_t> inputs, size_t maxWork = 128);

/// Adds an edge only when existing CTRL paths do not already prove it (bounded search).
bool appendControlDependency(
    GraphDraft &draft, gc_node_ref_t predecessor, gc_node_ref_t user, size_t maxWork = 32);

/// Removes CTRL edges implied by other CTRL paths. Branch boundaries and arm anchors are
/// retained. Work is limited per target, per graph size, and by maxWork; zero disables analysis.
size_t reduceControlDependencies(GraphDraft &draft, size_t maxWork = 250000);

} // namespace camel::runtime
