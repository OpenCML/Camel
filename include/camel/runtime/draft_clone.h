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
 * Created: Apr. 10, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Runtime draft graph clone helpers.
 *
 * Inline-style runtime rewrites need a graph-native way to copy one immutable
 * runtime graph into a mutable owner draft before rewiring ports and exits.
 * This helper provides the first low-level clone primitive: append all nodes of
 * a source runtime graph into an existing GraphDraft and return the source ->
 * cloned node-id mapping.
 */

#pragma once

#include "camel/runtime/draft.h"

#include <vector>

namespace camel::runtime {

struct DraftGraphCloneResult {
    std::vector<gc_node_ref_t> sourceToCloned;
    std::vector<gc_node_ref_t> clonedNodes;
    std::vector<gc_node_ref_t> normPorts;
    std::vector<gc_node_ref_t> withPorts;
    std::vector<gc_node_ref_t> closureNodes;
    gc_node_ref_t entryNode  = kInvalidNodeRef;
    gc_node_ref_t exitNode   = kInvalidNodeRef;
    gc_node_ref_t outputNode = kInvalidNodeRef;
    gc_node_ref_t returnNode = kInvalidNodeRef;
    GCReturnKind returnKind  = GCReturnKind::None;
};

DraftGraphCloneResult cloneRuntimeGraphIntoDraft(GraphDraft &owner, const GCGraph *sourceGraph);

} // namespace camel::runtime
