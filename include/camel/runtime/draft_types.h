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
 * Created: Sep. 30, 2026
 * Updated: Sep. 30, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Type re-inference on a graph draft.
 *
 * Compilation types each node once. When a rewrite learns more about a graph's inputs (a call
 * site passes a tensor whose type carries its shape), the node types downstream can be refined
 * the same way compilation derived them: operator results through their overload's resolver,
 * forwarding nodes from their input, projections from the projected element. Shape facts then
 * live in node types, where folding and translation read them.
 */

#pragma once

#include "camel/runtime/draft.h"

namespace camel::runtime {

/**
 * Recomputes the types of `draft`'s nodes from its ports' types, in dependency order, and updates
 * the draft's function type to match its ports and result. A node whose type cannot be recomputed
 * keeps its type. Returns the number of nodes whose type changed.
 */
size_t reinferDraftTypes(GraphDraft &draft);

} // namespace camel::runtime
