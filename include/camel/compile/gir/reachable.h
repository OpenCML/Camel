/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Apr. 08, 2026
 * Updated: Apr. 08, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
