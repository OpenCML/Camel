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
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The operator semantics of one OPER node of a frozen graph (docs/design/op-semantics,
 * sections 3 and 5): what the node's operator declares, instantiated with what the graph knows
 * at the node.
 *
 * The facts are the types of the node's inputs (a tensor type with its shape) and the values
 * of the inputs that are constants: a static DATA node, also reached through GATEs (an
 * inlined call's argument). A constant whose object is not a plain scalar counts only when
 * every user of it is a pure operator (nothing can have written it in place). A back end that
 * specializes a graph to the shapes of a call asks again on the specialized graph and gets the
 * instance for those shapes.
 */

#pragma once

#include "camel/core/operator_semantics.h"
#include "camel/runtime/graph.h"

#include <optional>

namespace camel::runtime {

/// The index semantics of the OPER node `node` of `graph`, or nullopt when the node is not an
/// OPER, its operator declares none, or the facts at the node do not fix it (sem::instantiate).
std::optional<camel::core::sem::IndexedInstance>
instanceOf(const GCGraph &graph, gc_node_ref_t node);

} // namespace camel::runtime
