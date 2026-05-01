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
 * Updated: Apr. 12, 2026
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
#include "camel/utils/log.h"

#include <cstring>
#include <unordered_map>

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

camel::core::type::Type *resolveRuntimeNodeDataType(const GCGraph *graph, const GCNode *node) {
    ASSERT(graph != nullptr, "Runtime draft clone type resolution requires a non-null graph.");
    ASSERT(node != nullptr, "Runtime draft clone type resolution requires a non-null node.");
    if (node->dataType != nullptr) {
        return node->dataType;
    }
    if (node->dataIndex > 0) {
        const auto *runtimeTuple = graph->runtimeDataType();
        if (runtimeTuple != nullptr &&
            static_cast<size_t>(node->dataIndex) < runtimeTuple->size()) {
            return runtimeTuple->typeAt(static_cast<size_t>(node->dataIndex));
        }
    } else if (node->dataIndex < 0) {
        const auto *staticTuple  = graph->staticDataType();
        const size_t staticIndex = static_cast<size_t>(-node->dataIndex);
        if (staticTuple != nullptr && staticIndex < staticTuple->size()) {
            return staticTuple->typeAt(staticIndex);
        }
    }
    return nullptr;
}

std::vector<std::byte> clonePayloadBytes(
    const GraphDraft &sourceDraft, gc_node_ref_t sourceRef,
    const std::vector<gc_node_ref_t> &mapping) {
    const DraftNodeHeader *sourceNode = sourceDraft.header(sourceRef);
    ASSERT(sourceNode != nullptr, "Draft graph clone encountered a missing source node.");

    if (sourceNode->kind == GCNodeKind::Brch) {
        const auto sourceArms = sourceDraft.branchArmsOf(sourceRef);
        std::vector<std::byte> bytes(sizeof(DraftBrchPayload) + sourceArms.size_bytes());
        auto *payload = reinterpret_cast<DraftBrchPayload *>(bytes.data());
        const auto *sourceBrchPayload =
            reinterpret_cast<const DraftBrchPayload *>(sourceDraft.payloadOf(sourceRef).data());
        payload->join       = remapNodeRef(sourceBrchPayload->join, mapping);
        payload->armCount   = sourceBrchPayload->armCount;
        payload->defaultArm = remapNodeRef(sourceBrchPayload->defaultArm, mapping);
        payload->reserved   = 0;

        auto *arms = reinterpret_cast<GCBranchArm *>(bytes.data() + sizeof(DraftBrchPayload));
        for (size_t i = 0; i < sourceArms.size(); ++i) {
            arms[i].head = remapNodeRef(sourceArms[i].head, mapping);
            arms[i].tail = remapNodeRef(sourceArms[i].tail, mapping);
        }
        return bytes;
    }

    std::vector<std::byte> bytes(
        sourceDraft.payloadOf(sourceRef).begin(),
        sourceDraft.payloadOf(sourceRef).end());
    if (sourceNode->kind == GCNodeKind::Join && bytes.size() >= sizeof(GCJoinBody)) {
        auto *joinBody = reinterpret_cast<GCJoinBody *>(bytes.data());
        joinBody->brch = remapNodeRef(joinBody->brch, mapping);
    }
    return bytes;
}

camel::core::type::Type *resolveDraftNodeDataType(const GraphDraft &draft, gc_node_ref_t ref) {
    const DraftNodeHeader *node = draft.header(ref);
    ASSERT(node != nullptr, "Draft graph clone type resolution requires a live source node.");
    if (node->dataType != nullptr) {
        return node->dataType;
    }
    if (node->dataIndex > 0) {
        if (auto *runtimeType = draft.runtimeDataType();
            runtimeType != nullptr && static_cast<size_t>(node->dataIndex) < runtimeType->size()) {
            return runtimeType->typeAt(static_cast<size_t>(node->dataIndex));
        }
    } else if (node->dataIndex < 0) {
        const size_t staticIndex = static_cast<size_t>(-node->dataIndex);
        const auto staticTypes   = draft.staticSlotTypes();
        if (staticIndex < staticTypes.size()) {
            return staticTypes[staticIndex];
        }
    }
    return nullptr;
}

} // namespace

DraftGraphCloneResult cloneRuntimeGraphIntoDraft(GraphDraft &owner, const GCGraph *sourceGraph) {
    ASSERT(sourceGraph != nullptr, "Runtime draft clone requires a non-null source graph.");
    CAMEL_LOG_INFO_S(
        "DraftClone",
        "Clone begin from graph '{}' with {} nodes.",
        sourceGraph->name(),
        sourceGraph->nodeCount());

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
    CAMEL_LOG_INFO_S("DraftClone", "Clone copied cross-graph registries.");

    DraftGraphCloneResult result;
    result.sourceToCloned.assign(sourceGraph->nodeBlockCount(), kInvalidNodeRef);
    const gc_node_ref_t baseId = static_cast<gc_node_ref_t>(owner.nodeSlotCount());
    std::unordered_map<gc_slot_idx_t, gc_slot_idx_t> slotMap;
    std::unordered_map<gc_slot_idx_t, gc_slot_idx_t> staticSlotMap;

    gc_node_ref_t ordinal = 0;
    for (auto it = sourceGraph->nodes().begin(); it != sourceGraph->nodes().end(); ++it) {
        result.sourceToCloned[it.ref()] = static_cast<gc_node_ref_t>(baseId + ordinal);
        ++ordinal;
    }
    CAMEL_LOG_INFO_S("DraftClone", "Clone planned {} node mappings.", ordinal);

    std::vector<std::vector<gc_node_ref_t>> pendingNormInputs;
    std::vector<std::vector<gc_node_ref_t>> pendingWithInputs;
    std::vector<std::vector<gc_node_ref_t>> pendingCtrlInputs;
    pendingNormInputs.reserve(sourceGraph->nodeCount());
    pendingWithInputs.reserve(sourceGraph->nodeCount());
    pendingCtrlInputs.reserve(sourceGraph->nodeCount());

    ordinal = 0;
    for (auto it = sourceGraph->nodes().begin(); it != sourceGraph->nodes().end(); ++it) {
        const gc_node_ref_t sourceRef = it.ref();
        const GCNode *sourceNode      = *it;
        ASSERT(sourceNode != nullptr, "Runtime draft clone encountered a null source node.");
        camel::core::type::Type *sourceDataType =
            resolveRuntimeNodeDataType(sourceGraph, sourceNode);
        CAMEL_LOG_INFO_S(
            "DraftClone",
            "Clone create node sourceRef={} kind={}.",
            sourceRef,
            static_cast<int>(sourceNode->kind));

        CAMEL_LOG_INFO_S("DraftClone", "Clone remap inputs for sourceRef={}.", sourceRef);
        pendingNormInputs.push_back(
            remapNodeSpan(sourceGraph->normInputsOf(sourceRef), result.sourceToCloned));
        pendingWithInputs.push_back(
            remapNodeSpan(sourceGraph->withInputsOf(sourceRef), result.sourceToCloned));
        pendingCtrlInputs.push_back(
            remapNodeSpan(sourceGraph->ctrlInputsOf(sourceRef), result.sourceToCloned));
        CAMEL_LOG_INFO_S("DraftClone", "Clone remapped inputs for sourceRef={}.", sourceRef);

        CAMEL_LOG_INFO_S("DraftClone", "Clone payload for sourceRef={}.", sourceRef);
        const std::vector<std::byte> payload =
            clonePayloadBytes(sourceGraph, sourceRef, result.sourceToCloned);
        CAMEL_LOG_INFO_S(
            "DraftClone",
            "Clone payload ready for sourceRef={} bytes={}.",
            sourceRef,
            payload.size());

        gc_slot_idx_t mappedDataIndex = sourceNode->dataIndex;
        CAMEL_LOG_INFO_S(
            "DraftClone",
            "Clone remap data index for sourceRef={} originalIndex={}.",
            sourceRef,
            mappedDataIndex);
        if (mappedDataIndex > 0) {
            CAMEL_LOG_INFO_S(
                "DraftClone",
                "Clone positive slot remap sourceRef={} dataType={}.",
                sourceRef,
                static_cast<const void *>(sourceDataType));
            auto [it, inserted] = slotMap.try_emplace(mappedDataIndex, 0);
            if (inserted) {
                ASSERT(
                    sourceDataType != nullptr,
                    "Runtime draft clone cannot remap a positive slot without a data type.");
                CAMEL_LOG_INFO_S(
                    "DraftClone",
                    "Clone allocate runtime slot sourceRef={} originalIndex={}.",
                    sourceRef,
                    mappedDataIndex);
                it->second = owner.allocateRuntimeSlot(sourceDataType);
                CAMEL_LOG_INFO_S(
                    "DraftClone",
                    "Clone allocated runtime slot sourceRef={} mappedIndex={}.",
                    sourceRef,
                    it->second);
            }
            mappedDataIndex = it->second;
        } else if (mappedDataIndex < 0) {
            auto [it, inserted] = staticSlotMap.try_emplace(mappedDataIndex, 0);
            if (inserted) {
                const size_t staticIndex = static_cast<size_t>(-mappedDataIndex);
                const auto staticSlots   = sourceGraph->staticSlots();
                const auto *staticTypes  = sourceGraph->staticDataType();
                ASSERT(
                    staticTypes != nullptr && staticIndex < staticSlots.size() &&
                        staticIndex < staticTypes->size(),
                    "Runtime draft clone encountered an out-of-range static slot index.");
                const size_t ownerStaticIndex = owner.appendStaticSlot(
                    staticSlots[staticIndex],
                    staticTypes->typeAt(staticIndex));
                ASSERT(
                    ownerStaticIndex <=
                        static_cast<size_t>(std::numeric_limits<gc_slot_idx_t>::max()),
                    "Runtime draft clone static-slot remap exceeds 16-bit data-index capacity.");
                it->second = static_cast<gc_slot_idx_t>(-static_cast<int32_t>(ownerStaticIndex));
            }
            mappedDataIndex = it->second;
        }
        CAMEL_LOG_INFO_S(
            "DraftClone",
            "Clone remapped data index for sourceRef={} mappedIndex={}.",
            sourceRef,
            mappedDataIndex);

        DraftNodeInit init{
            .dataIndex    = mappedDataIndex,
            .dataType     = sourceDataType,
            .kind         = sourceNode->kind,
            .runtimeFlags = sourceNode->flags,
            .payload      = payload,
        };
        CAMEL_LOG_INFO_S("DraftClone", "Clone addNode for sourceRef={}.", sourceRef);
        const gc_node_ref_t clonedId = owner.addNode(init);
        CAMEL_LOG_INFO_S(
            "DraftClone",
            "Clone created node sourceRef={} -> clonedId={}.",
            sourceRef,
            clonedId);
        ASSERT(
            clonedId == static_cast<gc_node_ref_t>(baseId + ordinal),
            "Runtime draft clone node allocation order diverged from the planned id mapping.");
        result.clonedNodes.push_back(clonedId);
        ++ordinal;
    }
    CAMEL_LOG_INFO_S(
        "DraftClone",
        "Clone created {} nodes, now patching inputs.",
        result.clonedNodes.size());

    for (size_t i = 0; i < result.clonedNodes.size(); ++i) {
        const gc_node_ref_t clonedId = result.clonedNodes[i];
        owner.setNormInputs(clonedId, pendingNormInputs[i]);
        owner.setWithInputs(clonedId, pendingWithInputs[i]);
        owner.setCtrlInputs(clonedId, pendingCtrlInputs[i]);
    }
    CAMEL_LOG_INFO_S("DraftClone", "Clone patched cloned node inputs.");

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
    CAMEL_LOG_INFO_S("DraftClone", "Clone finished for graph '{}'.", sourceGraph->name());
    return result;
}

DraftGraphCloneResult cloneDraftGraphIntoDraft(GraphDraft &owner, const GraphDraft &sourceDraft) {
    for (GCGraph *graph : sourceDraft.dependencies()) {
        owner.addDependency(graph);
    }
    for (GCGraph *graph : sourceDraft.subGraphs()) {
        owner.addSubGraph(graph);
    }
    for (GCGraph *graph : sourceDraft.staticGraphRefs()) {
        owner.addStaticGraphRef(graph);
    }

    DraftGraphCloneResult result;
    result.sourceToCloned.assign(sourceDraft.nodeSlotCount(), kInvalidNodeRef);
    const gc_node_ref_t baseId = static_cast<gc_node_ref_t>(owner.nodeSlotCount());
    std::unordered_map<gc_slot_idx_t, gc_slot_idx_t> slotMap;
    std::unordered_map<gc_slot_idx_t, gc_slot_idx_t> staticSlotMap;

    gc_node_ref_t ordinal = 0;
    for (gc_node_ref_t sourceRef = 0; sourceRef < sourceDraft.nodeSlotCount(); ++sourceRef) {
        if (!sourceDraft.alive(sourceRef)) {
            continue;
        }
        result.sourceToCloned[sourceRef] = static_cast<gc_node_ref_t>(baseId + ordinal);
        ++ordinal;
    }

    std::vector<std::vector<gc_node_ref_t>> pendingNormInputs;
    std::vector<std::vector<gc_node_ref_t>> pendingWithInputs;
    std::vector<std::vector<gc_node_ref_t>> pendingCtrlInputs;
    pendingNormInputs.reserve(ordinal);
    pendingWithInputs.reserve(ordinal);
    pendingCtrlInputs.reserve(ordinal);

    ordinal = 0;
    for (gc_node_ref_t sourceRef = 0; sourceRef < sourceDraft.nodeSlotCount(); ++sourceRef) {
        const DraftNodeHeader *sourceNode = sourceDraft.header(sourceRef);
        if (!sourceNode) {
            continue;
        }

        camel::core::type::Type *sourceDataType = resolveDraftNodeDataType(sourceDraft, sourceRef);
        pendingNormInputs.push_back(
            remapNodeSpan(sourceDraft.normInputsOf(sourceRef), result.sourceToCloned));
        pendingWithInputs.push_back(
            remapNodeSpan(sourceDraft.withInputsOf(sourceRef), result.sourceToCloned));
        pendingCtrlInputs.push_back(
            remapNodeSpan(sourceDraft.ctrlInputsOf(sourceRef), result.sourceToCloned));

        const std::vector<std::byte> payload =
            clonePayloadBytes(sourceDraft, sourceRef, result.sourceToCloned);

        gc_slot_idx_t mappedDataIndex = sourceNode->dataIndex;
        if (mappedDataIndex > 0) {
            auto [it, inserted] = slotMap.try_emplace(mappedDataIndex, 0);
            if (inserted) {
                ASSERT(
                    sourceDataType != nullptr,
                    "Draft graph clone cannot remap a positive slot without a data type.");
                it->second = owner.allocateRuntimeSlot(sourceDataType);
            }
            mappedDataIndex = it->second;
        } else if (mappedDataIndex < 0) {
            auto [it, inserted] = staticSlotMap.try_emplace(mappedDataIndex, 0);
            if (inserted) {
                const size_t staticIndex = static_cast<size_t>(-mappedDataIndex);
                const auto staticSlots   = sourceDraft.staticSlots();
                const auto staticTypes   = sourceDraft.staticSlotTypes();
                ASSERT(
                    staticIndex < staticSlots.size() && staticIndex < staticTypes.size(),
                    "Draft graph clone encountered an out-of-range static slot index.");
                const size_t ownerStaticIndex =
                    owner.appendStaticSlot(staticSlots[staticIndex], staticTypes[staticIndex]);
                ASSERT(
                    ownerStaticIndex <=
                        static_cast<size_t>(std::numeric_limits<gc_slot_idx_t>::max()),
                    "Draft graph clone static-slot remap exceeds 16-bit data-index capacity.");
                it->second = static_cast<gc_slot_idx_t>(-static_cast<int32_t>(ownerStaticIndex));
            }
            mappedDataIndex = it->second;
        }

        DraftNodeInit init{
            .dataIndex    = mappedDataIndex,
            .dataType     = sourceDataType,
            .kind         = sourceNode->kind,
            .runtimeFlags = sourceNode->runtimeFlags,
            .payload      = payload,
        };
        const gc_node_ref_t clonedId = owner.addNode(init);
        ASSERT(
            clonedId == static_cast<gc_node_ref_t>(baseId + ordinal),
            "Draft graph clone node allocation order diverged from the planned id mapping.");
        result.clonedNodes.push_back(clonedId);
        ++ordinal;
    }

    for (size_t i = 0; i < result.clonedNodes.size(); ++i) {
        const gc_node_ref_t clonedId = result.clonedNodes[i];
        owner.setNormInputs(clonedId, pendingNormInputs[i]);
        owner.setWithInputs(clonedId, pendingWithInputs[i]);
        owner.setCtrlInputs(clonedId, pendingCtrlInputs[i]);
    }

    auto remapDraftRefs = [&](std::span<const gc_node_ref_t> refs,
                              std::vector<gc_node_ref_t> &out) {
        out.clear();
        out.reserve(refs.size());
        for (gc_node_ref_t ref : refs) {
            out.push_back(remapNodeRef(ref, result.sourceToCloned));
        }
    };

    remapDraftRefs(sourceDraft.normPorts(), result.normPorts);
    remapDraftRefs(sourceDraft.withPorts(), result.withPorts);
    remapDraftRefs(sourceDraft.closureNodes(), result.closureNodes);
    result.entryNode  = remapNodeRef(sourceDraft.entryNode(), result.sourceToCloned);
    result.exitNode   = remapNodeRef(sourceDraft.exitNode(), result.sourceToCloned);
    result.outputNode = remapNodeRef(sourceDraft.outputNode(), result.sourceToCloned);
    result.returnNode = remapNodeRef(sourceDraft.returnNode(), result.sourceToCloned);
    result.returnKind = sourceDraft.returnKind();
    return result;
}

} // namespace camel::runtime
