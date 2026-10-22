// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

/// Clones the nodes of a graph into `owner`. The cloned nodes are in the region `intoRegion` of
/// `owner` (0: the body); the regions the source graph has are made again inside it.
DraftGraphCloneResult cloneRuntimeGraphIntoDraft(
    GraphDraft &owner, const GCGraph *sourceGraph, uint32_t intoRegion = 0);
DraftGraphCloneResult cloneDraftGraphIntoDraft(
    GraphDraft &owner, const GraphDraft &sourceDraft, uint32_t intoRegion = 0);

} // namespace camel::runtime
