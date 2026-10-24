// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

#include <algorithm>
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

std::vector<std::byte> loopPayloadBytes(GraphDraft &owner, const GCLoopBody &loop) {
    const GCLoopRef ref{
        .kind            = loop.kind,
        .iterableForm    = loop.iterableForm,
        .condRef         = owner.payloadGraphIndexOf(loop.condGraph),
        .bodyRef         = owner.payloadGraphIndexOf(loop.bodyGraph),
        .collectionCount = loop.collectionCount,
    };
    std::vector<std::byte> bytes(sizeof(GCLoopRef));
    std::memcpy(bytes.data(), &ref, sizeof(ref));
    return bytes;
}

std::vector<std::byte> clonePayloadBytes(
    GraphDraft &owner, const GCGraph *sourceGraph, gc_node_ref_t sourceRef,
    const std::vector<gc_node_ref_t> &mapping) {
    const GCNode *sourceNode = sourceGraph->node(sourceRef);
    ASSERT(sourceNode != nullptr, "Runtime draft clone encountered a null source node.");

    if (sourceNode->kind == GCNodeKind::Brch) {
        std::vector<std::byte> bytes(sizeof(DraftBrchPayload));
        auto *payload                 = reinterpret_cast<DraftBrchPayload *>(bytes.data());
        const auto *sourceBrchPayload = sourceGraph->nodeBodyAs<GCBrchBody>(sourceRef);
        payload->join                 = remapNodeRef(sourceBrchPayload->join, mapping);
        payload->armCount             = sourceBrchPayload->armCount;
        return bytes;
    }

    if (sourceNode->kind == GCNodeKind::Func) {
        // The index is into the source graph's reference table: the owner's has its own.
        const GCFuncRef callee{
            .ref = owner.payloadGraphIndexOf(sourceGraph->calleeIdOf(sourceRef))};
        std::vector<std::byte> bytes(sizeof(GCFuncRef));
        std::memcpy(bytes.data(), &callee, sizeof(callee));
        return bytes;
    }
    if (sourceNode->kind == GCNodeKind::Loop) {
        return loopPayloadBytes(owner, loopBodyOf(*sourceGraph, sourceRef));
    }
    const auto body = sourceGraph->nodePayloadBytes(sourceRef);
    std::vector<std::byte> bytes(body.begin(), body.end());
    if (sourceNode->kind == GCNodeKind::Join && bytes.size() >= sizeof(GCJoinBody)) {
        auto *joinBody = reinterpret_cast<GCJoinBody *>(bytes.data());
        joinBody->brch = remapNodeRef(joinBody->brch, mapping);
    }
    return bytes;
}

camel::core::type::Type *resolveRuntimeNodeDataType(const GCGraph *graph, const GCNode *node) {
    ASSERT(graph != nullptr, "Runtime draft clone type resolution requires a non-null graph.");
    ASSERT(node != nullptr, "Runtime draft clone type resolution requires a non-null node.");
    if (node->dataType() != nullptr) {
        return node->dataType();
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
    GraphDraft &owner, const GraphDraft &sourceDraft, gc_node_ref_t sourceRef,
    const std::vector<gc_node_ref_t> &mapping) {
    const DraftNodeHeader *sourceNode = sourceDraft.header(sourceRef);
    ASSERT(sourceNode != nullptr, "Draft graph clone encountered a missing source node.");

    if (sourceNode->kind == GCNodeKind::Brch) {
        std::vector<std::byte> bytes(sizeof(DraftBrchPayload));
        auto *payload = reinterpret_cast<DraftBrchPayload *>(bytes.data());
        const auto *sourceBrchPayload =
            reinterpret_cast<const DraftBrchPayload *>(sourceDraft.payloadOf(sourceRef).data());
        payload->join     = remapNodeRef(sourceBrchPayload->join, mapping);
        payload->armCount = sourceBrchPayload->armCount;
        return bytes;
    }

    if (sourceNode->kind == GCNodeKind::Func) {
        const GCFuncRef callee{.ref = owner.payloadGraphIndexOf(sourceDraft.calleeIdOf(sourceRef))};
        std::vector<std::byte> bytes(sizeof(GCFuncRef));
        std::memcpy(bytes.data(), &callee, sizeof(callee));
        return bytes;
    }
    if (sourceNode->kind == GCNodeKind::Loop) {
        return loopPayloadBytes(owner, sourceDraft.loopGraphsOf(sourceRef));
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
        if (const auto slots = draft.runtimeSlotTypes();
            static_cast<size_t>(node->dataIndex) < slots.size()) {
            return slots[static_cast<size_t>(node->dataIndex)];
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

const GCRegion &regionOf(const GraphDraft &draft, uint32_t id) { return draft.region(id); }
const GCRegion &regionOf(const GCGraph &graph, uint32_t id) { return *graph.region(id); }
std::span<const gc_node_ref_t> portsOf(const GraphDraft &draft, uint32_t id) {
    return draft.regionPortsOf(id);
}
std::span<const gc_node_ref_t> portsOf(const GCGraph &graph, uint32_t id) {
    return graph.regionPorts(id);
}

/// Puts the cloned nodes in regions of `owner`: those of the source in `intoRegion`, those of its
/// regions in regions made again as children of it. `Source` is a GCGraph or a GraphDraft.
template <typename Source>
void adoptRegions(
    GraphDraft &owner, const Source &source, const std::vector<gc_node_ref_t> &sourceToCloned,
    uint32_t intoRegion) {
    const size_t count = source.regionCount();
    if (count == 0 && intoRegion == 0) {
        return;
    }
    const auto mapNode = [&](gc_node_ref_t ref) {
        return ref < sourceToCloned.size() ? sourceToCloned[ref] : kInvalidNodeRef;
    };
    std::vector<uint32_t> regionMap(std::max<size_t>(count, 1), intoRegion);
    for (uint32_t r = 1; r < count; ++r) {
        const GCRegion &from = regionOf(source, r);
        if (from.kind == GCRegionKind::Dead) {
            continue;
        }
        const uint32_t made = owner.addRegion(from.kind, regionMap[from.parent], mapNode(from.owner), from.index);
        regionMap[r]        = made;
        owner.setRegionResult(made, mapNode(from.result));
        owner.setRegionOrigin(made, from.origin);
        owner.setRegionAttrs(made, from.attrs);
        owner.setRegionParams(made, from.params);
        owner.setRegionInline(made, from.inlineOf, mapNode(from.callSite));
        std::vector<gc_node_ref_t> ports;
        for (gc_node_ref_t port : portsOf(source, r)) {
            ports.push_back(mapNode(port));
        }
        owner.setRegionPorts(made, ports);
    }
    for (gc_node_ref_t ref = 0; ref < sourceToCloned.size(); ++ref) {
        const gc_node_ref_t cloned = sourceToCloned[ref];
        if (cloned == kInvalidNodeRef) {
            continue;
        }
        const uint32_t target = regionMap[source.nodeRegion(ref)];
        if (target != 0) {
            owner.setNodeRegion(cloned, target);
        }
    }
}

} // namespace

DraftGraphCloneResult cloneRuntimeGraphIntoDraft(
    GraphDraft &owner, const GCGraph *sourceGraph, uint32_t intoRegion) {
    ASSERT(sourceGraph != nullptr, "Runtime draft clone requires a non-null source graph.");
    CAMEL_LOG_INFO_S(
        "DraftClone",
        "Clone begin from graph '{}' with {} nodes.",
        sourceGraph->name(),
        sourceGraph->nodeCount());

    // Inline-style clone should preserve the source graph's cross-graph registry.
    // The owner draft may already reference some of them, so GraphDraft keeps the
    // vectors de-duplicated.
    for (GraphId graph : sourceGraph->dependencies()) {
        owner.addDependency(graph);
    }
    for (GraphId graph : sourceGraph->subGraphs()) {
        owner.addSubGraph(graph);
    }
    for (GraphId graph : sourceGraph->staticGraphRefs()) {
        owner.addStaticGraphRef(graph);
    }
    for (GraphId graph : sourceGraph->ruleGraphs()) {
        owner.addRuleGraphRef(graph);
    }
    CAMEL_LOG_INFO_S("DraftClone", "Clone copied cross-graph registries.");

    DraftGraphCloneResult result;
    result.sourceToCloned.assign(sourceGraph->nodeCount(), kInvalidNodeRef);
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
    std::vector<std::vector<gc_node_ref_t>> pendingOrderInputs;
    std::vector<std::vector<gc_node_ref_t>> pendingSyncInputs;
    pendingNormInputs.reserve(sourceGraph->nodeCount());
    pendingWithInputs.reserve(sourceGraph->nodeCount());
    pendingCtrlInputs.reserve(sourceGraph->nodeCount());
    pendingOrderInputs.reserve(sourceGraph->nodeCount());
    pendingSyncInputs.reserve(sourceGraph->nodeCount());
    // The two orderings that are side arrays of a frozen graph (GCGraph::orderPredsOf,
    // GCGraph::syncPredsOf) travel with the nodes they join: a clone is the same code, so the
    // statements of a copied block keep the chain they were written in
    // (docs/design/effects/edges.cn.md 1.5).
    const auto remappedPreds = [&](auto edges) {
        std::vector<gc_node_ref_t> preds;
        preds.reserve(edges.size());
        for (const auto &edge : edges) {
            preds.push_back(remapNodeRef(edge.pred, result.sourceToCloned));
        }
        return preds;
    };

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
        pendingOrderInputs.push_back(remappedPreds(sourceGraph->orderPredsOf(sourceRef)));
        pendingSyncInputs.push_back(remappedPreds(sourceGraph->syncPredsOf(sourceRef)));
        CAMEL_LOG_INFO_S("DraftClone", "Clone remapped inputs for sourceRef={}.", sourceRef);

        CAMEL_LOG_INFO_S("DraftClone", "Clone payload for sourceRef={}.", sourceRef);
        const std::vector<std::byte> payload =
            clonePayloadBytes(owner, sourceGraph, sourceRef, result.sourceToCloned);
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
                    "Runtime draft clone static-slot remap exceeds the data-index range.");
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
            .origin       = sourceGraph->nodeOrigin(sourceRef),
            .permissions  = sourceGraph->nodePermissions(sourceRef),
            .attrs        = sourceGraph->nodeAttrs(sourceRef),
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
        owner.setOrderInputs(clonedId, pendingOrderInputs[i]);
        owner.setSyncInputs(clonedId, pendingSyncInputs[i]);
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
    adoptRegions(owner, *sourceGraph, result.sourceToCloned, intoRegion);
    return result;
}

DraftGraphCloneResult cloneDraftGraphIntoDraft(
    GraphDraft &owner, const GraphDraft &sourceDraft, uint32_t intoRegion) {
    for (GraphId graph : sourceDraft.dependencies()) {
        owner.addDependency(graph);
    }
    for (GraphId graph : sourceDraft.subGraphs()) {
        owner.addSubGraph(graph);
    }
    for (GraphId graph : sourceDraft.staticGraphRefs()) {
        owner.addStaticGraphRef(graph);
    }
    for (GraphId graph : sourceDraft.ruleGraphs()) {
        owner.addRuleGraphRef(graph);
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
    std::vector<std::vector<gc_node_ref_t>> pendingOrderInputs;
    std::vector<std::vector<gc_node_ref_t>> pendingSyncInputs;
    pendingNormInputs.reserve(ordinal);
    pendingWithInputs.reserve(ordinal);
    pendingCtrlInputs.reserve(ordinal);
    pendingOrderInputs.reserve(ordinal);
    pendingSyncInputs.reserve(ordinal);

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
        // The two orderings travel with the nodes they join: the copied statements keep the chain
        // the source block wrote them in (docs/design/effects/edges.cn.md 1.5).
        pendingOrderInputs.push_back(
            remapNodeSpan(sourceDraft.orderInputsOf(sourceRef), result.sourceToCloned));
        pendingSyncInputs.push_back(
            remapNodeSpan(sourceDraft.syncInputsOf(sourceRef), result.sourceToCloned));

        const std::vector<std::byte> payload =
            clonePayloadBytes(owner, sourceDraft, sourceRef, result.sourceToCloned);

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
                    "Draft graph clone static-slot remap exceeds the data-index range.");
                it->second = static_cast<gc_slot_idx_t>(-static_cast<int32_t>(ownerStaticIndex));
            }
            mappedDataIndex = it->second;
        }

        DraftNodeInit init{
            .dataIndex    = mappedDataIndex,
            .dataType     = sourceDataType,
            .kind         = sourceNode->kind,
            .runtimeFlags = sourceNode->runtimeFlags,
            .origin       = sourceNode->origin,
            .permissions  = sourceNode->permissions,
            .attrs        = sourceNode->attrs,
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
        owner.setOrderInputs(clonedId, pendingOrderInputs[i]);
        owner.setSyncInputs(clonedId, pendingSyncInputs[i]);
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
    adoptRegions(owner, sourceDraft, result.sourceToCloned, intoRegion);
    return result;
}

} // namespace camel::runtime
