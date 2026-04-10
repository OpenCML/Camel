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
 * The clone is intentionally graph-local:
 * - all runtime graph references remain untouched
 * - all internal node references are remapped to cloned node ids
 * - user lists are rebuilt from inputs during insertion rather than copied
 *
 * This keeps the primitive suitable for future runtime inline splice logic
 * without coupling it to one particular optimization pass.
 */

#include "camel/runtime/draft_clone.h"

#include <cstring>

namespace camel::runtime {

namespace {

gc_node_ref_t remapNodeRef(gc_node_ref_t sourceRef, const std::vector<gc_node_ref_t> &mapping) {
    if (sourceRef == kInvalidNodeRef) {
        return kInvalidNodeRef;
    }
    ASSERT(sourceRef < mapping.size(), "Runtime draft clone encountered an out-of-range node ref.");
    const gc_node_ref_t mapped = mapping[sourceRef];
    ASSERT(mapped != kInvalidNodeRef, "Runtime draft clone encountered an unmapped live node ref.");
    return mapped;
}

std::vector<gc_node_ref_t>
remapNodeSpan(std::span<const gc_node_ref_t> refs, const std::vector<gc_node_ref_t> &mapping) {
    std::vector<gc_node_ref_t> remapped;
    remapped.reserve(refs.size());
    for (gc_node_ref_t ref : refs) {
        remapped.push_back(remapNodeRef(ref, mapping));
    }
    return remapped;
}

std::vector<std::byte> clonePayloadBytes(
    const GCGraph *sourceGraph, gc_node_ref_t sourceRef,
    const std::vector<gc_node_ref_t> &mapping) {
    const GCNode *sourceNode = sourceGraph->node(sourceRef);
    ASSERT(sourceNode != nullptr, "Runtime draft clone encountered a null source node.");

    if (sourceNode->kind == GCNodeKind::Brch) {
        const auto sourceArms = sourceGraph->branchArmsOf(sourceRef);
        std::vector<std::byte> bytes(sizeof(DraftBrchPayload) + sourceArms.size_bytes());
        auto *payload                 = reinterpret_cast<DraftBrchPayload *>(bytes.data());
        const auto *sourceBrchPayload = sourceNode->bodyAs<GCBrchBody>();
        payload->join                 = remapNodeRef(sourceBrchPayload->join, mapping);
        payload->armCount             = sourceBrchPayload->armCount;
        payload->defaultArm           = remapNodeRef(sourceBrchPayload->defaultArm, mapping);
        payload->reserved             = 0;

        auto *arms = reinterpret_cast<GCBranchArm *>(bytes.data() + sizeof(DraftBrchPayload));
        for (size_t i = 0; i < sourceArms.size(); ++i) {
            arms[i].head = remapNodeRef(sourceArms[i].head, mapping);
            arms[i].tail = remapNodeRef(sourceArms[i].tail, mapping);
        }
        return bytes;
    }

    std::vector<std::byte> bytes(sourceNode->bodyBytes());
    if (!bytes.empty()) {
        std::memcpy(
            bytes.data(),
            reinterpret_cast<const std::byte *>(sourceNode) + sizeof(GCNode),
            bytes.size());
    }
    if (sourceNode->kind == GCNodeKind::Join && bytes.size() >= sizeof(GCJoinBody)) {
        auto *joinBody = reinterpret_cast<GCJoinBody *>(bytes.data());
        joinBody->brch = remapNodeRef(joinBody->brch, mapping);
    }
    return bytes;
}

} // namespace

DraftGraphCloneResult cloneRuntimeGraphIntoDraft(GraphDraft &owner, const GCGraph *sourceGraph) {
    ASSERT(sourceGraph != nullptr, "Runtime draft clone requires a non-null source graph.");

    // Inline-style clone should preserve the source graph's cross-graph registry.
    // The owner draft may already reference some of them, so GraphDraft keeps the
    // vectors de-duplicated.
    for (GCGraph *graph : sourceGraph->dependencies()) {
        owner.addDependency(graph);
    }
    for (GCGraph *graph : sourceGraph->subGraphs()) {
        owner.addSubGraph(graph);
    }
    for (GCGraph *graph : sourceGraph->staticGraphRefs()) {
        owner.addStaticGraphRef(graph);
    }

    DraftGraphCloneResult result;
    result.sourceToCloned.assign(sourceGraph->nodeBlockCount(), kInvalidNodeRef);
    const gc_node_ref_t baseId = static_cast<gc_node_ref_t>(owner.nodeSlotCount());

    gc_node_ref_t ordinal = 0;
    for (auto it = sourceGraph->nodes().begin(); it != sourceGraph->nodes().end(); ++it) {
        result.sourceToCloned[it.ref()] = static_cast<gc_node_ref_t>(baseId + ordinal);
        ++ordinal;
    }

    ordinal = 0;
    for (auto it = sourceGraph->nodes().begin(); it != sourceGraph->nodes().end(); ++it) {
        const gc_node_ref_t sourceRef = it.ref();
        const GCNode *sourceNode      = *it;
        ASSERT(sourceNode != nullptr, "Runtime draft clone encountered a null source node.");

        const std::vector<std::byte> payload =
            clonePayloadBytes(sourceGraph, sourceRef, result.sourceToCloned);
        const std::vector<gc_node_ref_t> normInputs =
            remapNodeSpan(sourceGraph->normInputsOf(sourceRef), result.sourceToCloned);
        const std::vector<gc_node_ref_t> withInputs =
            remapNodeSpan(sourceGraph->withInputsOf(sourceRef), result.sourceToCloned);
        const std::vector<gc_node_ref_t> ctrlInputs =
            remapNodeSpan(sourceGraph->ctrlInputsOf(sourceRef), result.sourceToCloned);

        DraftNodeInit init{
            .dataIndex    = sourceNode->dataIndex,
            .dataType     = sourceNode->dataType,
            .kind         = sourceNode->kind,
            .runtimeFlags = sourceNode->flags,
            .payload      = payload,
            .normInputs   = normInputs,
            .withInputs   = withInputs,
            .ctrlInputs   = ctrlInputs,
        };
        const gc_node_ref_t clonedId = owner.addNode(init);
        ASSERT(
            clonedId == static_cast<gc_node_ref_t>(baseId + ordinal),
            "Runtime draft clone node allocation order diverged from the planned id mapping.");
        result.clonedNodes.push_back(clonedId);
        ++ordinal;
    }

    auto remapGraphRefs = [&](std::span<const gc_node_ref_t> refs,
                              std::vector<gc_node_ref_t> &out) {
        out.clear();
        out.reserve(refs.size());
        for (gc_node_ref_t ref : refs) {
            out.push_back(remapNodeRef(ref, result.sourceToCloned));
        }
    };

    remapGraphRefs(sourceGraph->normPorts(), result.normPorts);
    remapGraphRefs(sourceGraph->withPorts(), result.withPorts);
    remapGraphRefs(sourceGraph->closureNodes(), result.closureNodes);
    result.entryNode  = remapNodeRef(sourceGraph->entryNodeRef(), result.sourceToCloned);
    result.exitNode   = remapNodeRef(sourceGraph->exitNodeRef(), result.sourceToCloned);
    result.outputNode = remapNodeRef(sourceGraph->outputNodeRef(), result.sourceToCloned);
    result.returnNode = remapNodeRef(sourceGraph->returnNodeRef(), result.sourceToCloned);
    result.returnKind = sourceGraph->returnKind();
    return result;
}

} // namespace camel::runtime
