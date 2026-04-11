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
 * Author: Zhenjie Wei
 * Created: Mar. 29, 2026
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "graph.h"

namespace camel::compile::gir::validate {

// Structural invariants for an encodable compile-time graph.
// This remains a standalone debug utility rather than part of the Graph type.
void assertGraphSealingPreconditions(const Graph &graph);
// Depth-first over root and its subGraphs()/dependencies(), applying the same assertions graph
// by graph.
void assertGraphTreeSealingPreconditions(const graph_ptr_t &graph);
void assertGraphTreeStaticReferences(const graph_ptr_t &graph);

} // namespace camel::compile::gir::validate
