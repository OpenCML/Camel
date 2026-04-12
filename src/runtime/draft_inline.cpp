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
 * Runtime draft inline helpers.
 *
 * The current implementation intentionally mirrors only the runtime-native core
 * splice:
 * - clone callee nodes into the owner draft
 * - bind callee ports to the FUNC node's actual arguments
 * - redirect value and control users of the FUNC node
 * - erase the original FUNC node
 *
 * This
 * is the minimum reusable primitive needed before the large inline pass
 * fully consolidates
 * around the runtime-native draft/session pipeline.
 */

#include "camel/runtime/draft_inline.h"

#include "camel/core/rtdata/func.h"
#include "camel/runtime/draft_clone.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <unordered_set>

namespace camel::runtime {

namespace {

enum class FormalKind : uint8_t {
    Norm,
    With,
    Closure,
};

struct DraftStaticValue {
    slot_t value                  = NullSlot;
    camel::core::type::Type *type = nullptr;
    uint8_t runtimeFlags          = 0;
    gc_node_ref_t sourceNodeId    = kInvalidNodeRef;
    bool valid() const { return type != nullptr; }
};

struct DraftFormalBinding {
    FormalKind kind            = FormalKind::Norm;
    size_t index               = 0;
    gc_node_ref_t formalNodeId = kInvalidNodeRef;
    DraftStaticValue staticValue{};
};

gc_node_ref_t remapNodeRef(gc_node_ref_t sourceRef, const std::vector<gc_node_ref_t> &mapping) {
    if (sourceRef == kInvalidNodeRef) {
        return kInvalidNodeRef;
    }
    ASSERT(
        sourceRef < mapping.size(),
        "Runtime inline remap encountered an out-of-range node ref.");
    const gc_node_ref_t mapped = mapping[sourceRef];
    ASSERT(mapped != kInvalidNodeRef, "Runtime inline remap encountered an unmapped node ref.");
    return mapped;
}

std::vector<gc_node_ref_t>
appendUniqueRefs(std::span<const gc_node_ref_t> lhs, std::span<const gc_node_ref_t> rhs) {
    std::vector<gc_node_ref_t> merged(lhs.begin(), lhs.end());
    for (gc_node_ref_t ref : rhs) {
        if (std::find(merged.begin(), merged.end(), ref) == merged.end()) {
            merged.push_back(ref);
        }
    }
    return merged;
}

bool isExecutableEntryKind(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Data:
    case GCNodeKind::Port:
    case GCNodeKind::Sync:
    case GCNodeKind::Gate:
    case GCNodeKind::Dref:
        return false;
    default:
        return true;
    }
}

std::vector<gc_node_ref_t> collectExecutableEntryRoots(
    const GCGraph *sourceGraph, const std::vector<gc_node_ref_t> &sourceToCloned) {
    ASSERT(sourceGraph != nullptr, "Runtime inline entry-root discovery requires a source graph.");

    std::unordered_set<gc_node_ref_t> reachable;
    std::vector<gc_node_ref_t> stack{sourceGraph->exitNodeRef()};
    while (!stack.empty()) {
        const gc_node_ref_t curr = stack.back();
        stack.pop_back();
        if (curr == kInvalidNodeRef || !reachable.insert(curr).second) {
            continue;
        }
        for (gc_node_ref_t in : sourceGraph->normInputsOf(curr)) {
            stack.push_back(in);
        }
        for (gc_node_ref_t in : sourceGraph->withInputsOf(curr)) {
            stack.push_back(in);
        }
        for (gc_node_ref_t in : sourceGraph->ctrlInputsOf(curr)) {
            stack.push_back(in);
        }
    }

    std::vector<gc_node_ref_t> roots;
    for (auto it = sourceGraph->nodes().begin(); it != sourceGraph->nodes().end(); ++it) {
        const gc_node_ref_t sourceRef = it.ref();
        const GCNode *node            = *it;
        if (!node || !reachable.contains(sourceRef) || !isExecutableEntryKind(node->kind)) {
            continue;
        }

        bool hasNonDataPortInput = false;
        auto scanInputs          = [&](std::span<const gc_node_ref_t> inputs) {
            for (gc_node_ref_t in : inputs) {
                const GCNode *inputNode = sourceGraph->node(in);
                if (!inputNode) {
                    continue;
                }
                if (inputNode->kind != GCNodeKind::Data && inputNode->kind != GCNodeKind::Port) {
                    hasNonDataPortInput = true;
                    break;
                }
            }
        };
        scanInputs(sourceGraph->normInputsOf(sourceRef));
        if (!hasNonDataPortInput) {
            scanInputs(sourceGraph->withInputsOf(sourceRef));
        }
        if (!hasNonDataPortInput) {
            scanInputs(sourceGraph->ctrlInputsOf(sourceRef));
        }
        if (hasNonDataPortInput) {
            continue;
        }

        roots.push_back(remapNodeRef(sourceRef, sourceToCloned));
    }
    return roots;
}

std::vector<gc_node_ref_t> collectExecutableEntryRoots(
    const GraphDraft &sourceDraft, const std::vector<gc_node_ref_t> &sourceToCloned) {
    std::unordered_set<gc_node_ref_t> reachable;
    std::vector<gc_node_ref_t> stack{sourceDraft.exitNode()};
    while (!stack.empty()) {
        const gc_node_ref_t curr = stack.back();
        stack.pop_back();
        if (curr == kInvalidNodeRef || !reachable.insert(curr).second) {
            continue;
        }
        for (gc_node_ref_t in : sourceDraft.normInputsOf(curr)) {
            stack.push_back(in);
        }
        for (gc_node_ref_t in : sourceDraft.withInputsOf(curr)) {
            stack.push_back(in);
        }
        for (gc_node_ref_t in : sourceDraft.ctrlInputsOf(curr)) {
            stack.push_back(in);
        }
    }

    std::vector<gc_node_ref_t> roots;
    for (gc_node_ref_t sourceRef = 0; sourceRef < sourceDraft.nodeSlotCount(); ++sourceRef) {
        const DraftNodeHeader *node = sourceDraft.header(sourceRef);
        if (!node || !reachable.contains(sourceRef) || !isExecutableEntryKind(node->kind)) {
            continue;
        }

        bool hasNonDataPortInput = false;
        auto scanInputs          = [&](std::span<const gc_node_ref_t> inputs) {
            for (gc_node_ref_t in : inputs) {
                const DraftNodeHeader *inputNode = sourceDraft.header(in);
                if (!inputNode) {
                    continue;
                }
                if (inputNode->kind != GCNodeKind::Data && inputNode->kind != GCNodeKind::Port) {
                    hasNonDataPortInput = true;
                    break;
                }
            }
        };
        scanInputs(sourceDraft.normInputsOf(sourceRef));
        if (!hasNonDataPortInput) {
            scanInputs(sourceDraft.withInputsOf(sourceRef));
        }
        if (!hasNonDataPortInput) {
            scanInputs(sourceDraft.ctrlInputsOf(sourceRef));
        }
        if (hasNonDataPortInput) {
            continue;
        }

        roots.push_back(remapNodeRef(sourceRef, sourceToCloned));
    }
    return roots;
}

std::vector<gc_node_ref_t>
orderedFuncActualInputs(const GraphDraft &draft, gc_node_ref_t funcNodeId) {
    std::vector<gc_node_ref_t> inputs;
    const auto normInputs = draft.normInputsOf(funcNodeId);
    const auto withInputs = draft.withInputsOf(funcNodeId);
    inputs.reserve(normInputs.size() + withInputs.size());
    inputs.insert(inputs.end(), normInputs.begin(), normInputs.end());
    inputs.insert(inputs.end(), withInputs.begin(), withInputs.end());
    return inputs;
}

DraftNodeInit snapshotNodeInit(const GraphDraft &draft, gc_node_ref_t nodeId) {
    const DraftNodeHeader *header = draft.header(nodeId);
    ASSERT(header != nullptr, "Draft node snapshot requires a live node.");
    return DraftNodeInit{
        .dataIndex    = header->dataIndex,
        .dataType     = header->dataType,
        .kind         = header->kind,
        .runtimeFlags = header->runtimeFlags,
        .payload      = draft.payloadOf(nodeId),
        .normInputs   = draft.normInputsOf(nodeId),
        .withInputs   = draft.withInputsOf(nodeId),
        .ctrlInputs   = draft.ctrlInputsOf(nodeId),
        .normUsers    = draft.normUsersOf(nodeId),
        .withUsers    = draft.withUsersOf(nodeId),
        .ctrlUsers    = draft.ctrlUsersOf(nodeId),
    };
}

std::vector<std::pair<FormalKind, gc_node_ref_t>>
orderedDirectCallFormalNodes(const GCGraph *graph) {
    ASSERT(graph != nullptr, "Ordered direct-call formal collection requires a non-null graph.");
    std::vector<std::pair<FormalKind, gc_node_ref_t>> formals;
    formals.reserve(graph->normPorts().size() + graph->withPorts().size());
    for (gc_node_ref_t ref : graph->normPorts()) {
        formals.emplace_back(FormalKind::Norm, ref);
    }
    for (gc_node_ref_t ref : graph->withPorts()) {
        formals.emplace_back(FormalKind::With, ref);
    }
    return formals;
}

std::vector<std::pair<FormalKind, gc_node_ref_t>>
orderedDirectCallFormalNodes(const GraphDraft &draft) {
    std::vector<std::pair<FormalKind, gc_node_ref_t>> formals;
    formals.reserve(draft.normPorts().size() + draft.withPorts().size());
    for (gc_node_ref_t ref : draft.normPorts()) {
        formals.emplace_back(FormalKind::Norm, ref);
    }
    for (gc_node_ref_t ref : draft.withPorts()) {
        formals.emplace_back(FormalKind::With, ref);
    }
    return formals;
}

std::vector<std::pair<FormalKind, gc_node_ref_t>>
orderedClonedDirectCallFormalNodes(const DraftGraphCloneResult &cloned, const GCGraph *graph) {
    ASSERT(
        graph != nullptr,
        "Ordered cloned direct-call formal collection requires a non-null graph.");
    std::vector<std::pair<FormalKind, gc_node_ref_t>> formals;
    formals.reserve(cloned.normPorts.size() + cloned.withPorts.size());

    auto appendFormal = [&](FormalKind kind, gc_node_ref_t sourceRef) {
        const gc_node_ref_t clonedRef = remapNodeRef(sourceRef, cloned.sourceToCloned);
        formals.emplace_back(kind, clonedRef);
    };

    for (gc_node_ref_t ref : graph->normPorts()) {
        appendFormal(FormalKind::Norm, ref);
    }
    for (gc_node_ref_t ref : graph->withPorts()) {
        appendFormal(FormalKind::With, ref);
    }
    return formals;
}

std::vector<std::pair<FormalKind, gc_node_ref_t>>
orderedClonedDirectCallFormalNodes(const DraftGraphCloneResult &cloned, const GraphDraft &draft) {
    std::vector<std::pair<FormalKind, gc_node_ref_t>> formals;
    formals.reserve(cloned.normPorts.size() + cloned.withPorts.size());

    auto appendFormal = [&](FormalKind kind, gc_node_ref_t sourceRef) {
        const gc_node_ref_t clonedRef = remapNodeRef(sourceRef, cloned.sourceToCloned);
        formals.emplace_back(kind, clonedRef);
    };

    for (gc_node_ref_t ref : draft.normPorts()) {
        appendFormal(FormalKind::Norm, ref);
    }
    for (gc_node_ref_t ref : draft.withPorts()) {
        appendFormal(FormalKind::With, ref);
    }
    return formals;
}

DraftStaticValue tryResolveStaticValue(const GraphDraft &draft, gc_node_ref_t nodeId) {
    DraftStaticValue resolved{};
    gc_node_ref_t canonicalId = nodeId;
    while (canonicalId != kInvalidNodeRef) {
        const DraftNodeHeader *header = draft.header(canonicalId);
        if (!header) {
            return resolved;
        }
        if (header->kind == GCNodeKind::Gate) {
            const auto normInputs = draft.normInputsOf(canonicalId);
            if (!normInputs.empty()) {
                canonicalId = normInputs.back();
                continue;
            }
            const auto withInputs = draft.withInputsOf(canonicalId);
            canonicalId           = withInputs.empty() ? kInvalidNodeRef : withInputs.back();
            continue;
        }
        if (header->kind == GCNodeKind::Copy || header->kind == GCNodeKind::Cast ||
            header->kind == GCNodeKind::Dref) {
            const auto normInputs = draft.normInputsOf(canonicalId);
            canonicalId           = normInputs.empty() ? kInvalidNodeRef : normInputs.front();
            continue;
        }
        break;
    }

    const DraftNodeHeader *header = draft.header(canonicalId);
    if (!header || header->kind != GCNodeKind::Data || header->dataIndex >= 0) {
        return resolved;
    }

    const size_t staticIndex = static_cast<size_t>(-header->dataIndex);
    const auto staticSlots   = draft.staticSlots();
    const auto staticTypes   = draft.staticSlotTypes();
    if (staticIndex >= staticSlots.size() || staticIndex >= staticTypes.size()) {
        return resolved;
    }

    resolved.value        = staticSlots[staticIndex];
    resolved.type         = staticTypes[staticIndex];
    resolved.runtimeFlags = header->runtimeFlags;
    resolved.sourceNodeId = canonicalId;
    return resolved;
}

std::vector<DraftFormalBinding> collectDirectFuncSpecializations(
    const GraphDraft &draft, gc_node_ref_t funcNodeId, const GCGraph *calleeGraph) {
    std::vector<DraftFormalBinding> bindings;
    if (!calleeGraph) {
        return bindings;
    }

    const std::vector<gc_node_ref_t> actualInputs = orderedFuncActualInputs(draft, funcNodeId);
    const auto formals                            = orderedDirectCallFormalNodes(calleeGraph);
    CAMEL_LOG_INFO_S(
        "DraftOpt",
        "Specialize probe FUNC node {} callee='{}' actuals={} formals={}.",
        funcNodeId,
        calleeGraph->name(),
        actualInputs.size(),
        formals.size());
    if (actualInputs.size() != formals.size()) {
        return bindings;
    }

    for (size_t i = 0; i < formals.size(); ++i) {
        DraftStaticValue staticValue = tryResolveStaticValue(draft, actualInputs[i]);
        if (!staticValue.valid()) {
            continue;
        }
        bindings.push_back(
            DraftFormalBinding{
                .kind         = formals[i].first,
                .index        = i,
                .formalNodeId = formals[i].second,
                .staticValue  = staticValue,
            });
    }
    return bindings;
}

void eraseFormalFromDraft(GraphDraft &draft, FormalKind kind, gc_node_ref_t formalNodeId) {
    switch (kind) {
    case FormalKind::Norm:
        draft.removeNormPort(formalNodeId);
        break;
    case FormalKind::With:
        draft.removeWithPort(formalNodeId);
        break;
    case FormalKind::Closure:
        draft.removeClosureNode(formalNodeId);
        break;
    }
    draft.eraseNode(formalNodeId);
}

GCGraph *encodeSpecializedGraph(
    GraphDraft &draft, const GCGraph *baseGraph, std::string_view tag, size_t nonce) {
    ASSERT(baseGraph != nullptr, "Specialized graph encoding requires a base graph.");
    const std::string suffix      = std::format("${}{}", tag, nonce);
    const std::string stableId    = std::string(baseGraph->stableId()) + suffix;
    const std::string mangledName = std::string(baseGraph->mangledName()) + suffix;
    const std::string name        = std::string(baseGraph->name()) + suffix;
    return draft.encode(stableId, mangledName, name);
}

GCGraph *specializeGraphWithBindings(
    const GCGraph *baseGraph, std::span<const DraftFormalBinding> bindings, std::string_view tag,
    size_t nonce) {
    if (!baseGraph || bindings.empty()) {
        return const_cast<GCGraph *>(baseGraph);
    }

    auto draft = GraphDraft::decode(baseGraph);
    for (const DraftFormalBinding &binding : bindings) {
        const gc_node_ref_t formalDraftId = draft->draftIdOfSourceRef(binding.formalNodeId);
        ASSERT(
            formalDraftId != kInvalidNodeRef,
            "Specialization lost the formal-node mapping while decoding the callee graph.");
        const gc_node_ref_t staticNodeId = draft->materializeStaticValue(
            binding.staticValue.value,
            binding.staticValue.type,
            binding.staticValue.runtimeFlags);
        draft->replaceAllValueUses(formalDraftId, staticNodeId);
        eraseFormalFromDraft(*draft, binding.kind, formalDraftId);
    }
    return encodeSpecializedGraph(*draft, baseGraph, tag, nonce);
}

GCGraph *specializeClosureBoundGraph(const Function *function, size_t nonce) {
    if (!function || !function->runtimeGraph()) {
        return nullptr;
    }
    GCGraph *targetGraph = function->runtimeGraph();
    const Tuple *closure = function->tuple();
    if (!closure) {
        return targetGraph;
    }

    const auto *tupleType = function->tupleType();
    ASSERT(tupleType != nullptr, "Runtime Function closure specialization requires a tuple type.");
    ASSERT(
        tupleType->size() == targetGraph->closureNodes().size(),
        "Runtime Function closure size does not match the callee graph closure layout.");
    if (tupleType->size() == 0) {
        return targetGraph;
    }

    std::vector<DraftFormalBinding> bindings;
    bindings.reserve(tupleType->size());
    for (size_t i = 0; i < tupleType->size(); ++i) {
        bindings.push_back(
            DraftFormalBinding{
                .kind         = FormalKind::Closure,
                .index        = i,
                .formalNodeId = targetGraph->closureNodes()[i],
                .staticValue =
                    DraftStaticValue{
                        .value        = closure->get<slot_t>(i),
                        .type         = tupleType->typeAt(i),
                        .runtimeFlags = kGCNodeFlagConstant,
                    },
            });
    }
    return specializeGraphWithBindings(targetGraph, bindings, "closure", nonce);
}

void bindPortValueUses(GraphDraft &draft, gc_node_ref_t portNodeId, gc_node_ref_t actualInputId) {
    if (portNodeId == kInvalidNodeRef || actualInputId == kInvalidNodeRef) {
        return;
    }
    // Ports are parameter placeholders. After cloning into the owner draft,
    // all observable value uses must flow from the real caller operand instead.
    draft.replaceAllValueUses(portNodeId, actualInputId);
}

gc_node_ref_t remapFormalNodeToActual(
    gc_node_ref_t nodeId, const DraftGraphCloneResult &cloned, const GCGraph *calleeGraph,
    const GraphDraft *calleeDraftView, std::span<const gc_node_ref_t> boundActualInputs) {
    const auto formalNodes = calleeDraftView
                                 ? orderedClonedDirectCallFormalNodes(cloned, *calleeDraftView)
                                 : orderedClonedDirectCallFormalNodes(cloned, calleeGraph);
    for (size_t actualIndex = 0; actualIndex < formalNodes.size(); ++actualIndex) {
        if (formalNodes[actualIndex].second == nodeId) {
            ASSERT(
                actualIndex < boundActualInputs.size(),
                "Runtime inline formal-node remap overflow.");
            return boundActualInputs[actualIndex];
        }
    }
    return nodeId;
}

gc_node_ref_t resolveInlineValueExit(
    const GraphDraft &draft, const DraftGraphCloneResult &cloned, const GCGraph *calleeGraph,
    const GraphDraft *calleeDraftView, std::span<const gc_node_ref_t> boundActualInputs) {
    gc_node_ref_t current = cloned.returnNode != kInvalidNodeRef   ? cloned.returnNode
                            : cloned.outputNode != kInvalidNodeRef ? cloned.outputNode
                                                                   : cloned.exitNode;
    while (current != kInvalidNodeRef) {
        current = remapFormalNodeToActual(
            current,
            cloned,
            calleeGraph,
            calleeDraftView,
            boundActualInputs);
        const auto *header = draft.header(current);
        if (!header) {
            return kInvalidNodeRef;
        }
        if (header->kind == GCNodeKind::Gate &&
            std::find(boundActualInputs.begin(), boundActualInputs.end(), current) !=
                boundActualInputs.end()) {
            return current;
        }
        return draft.resolveForwardedValueRef(current);
    }
    return kInvalidNodeRef;
}

gc_node_ref_t resolveInlineCtrlExit(
    const GraphDraft &draft, const DraftGraphCloneResult &cloned, const GCGraph *calleeGraph,
    const GraphDraft *calleeDraftView, std::span<const gc_node_ref_t> boundActualInputs) {
    gc_node_ref_t current = cloned.exitNode != kInvalidNodeRef     ? cloned.exitNode
                            : cloned.returnNode != kInvalidNodeRef ? cloned.returnNode
                                                                   : cloned.outputNode;
    while (current != kInvalidNodeRef) {
        current = remapFormalNodeToActual(
            current,
            cloned,
            calleeGraph,
            calleeDraftView,
            boundActualInputs);
        return draft.resolveForwardedCtrlRef(current);
    }
    return kInvalidNodeRef;
}

} // namespace

bool specializeDirectFuncInDraft(GraphDraft &draft, gc_node_ref_t funcNodeId) {
    static size_t specializationNonce = 0;

    const DraftNodeHeader *funcHeader = draft.header(funcNodeId);
    if (!funcHeader || funcHeader->kind != GCNodeKind::Func) {
        return false;
    }
    const auto payloadBytes = draft.payloadOf(funcNodeId);
    if (payloadBytes.size_bytes() < sizeof(GCFuncBody)) {
        return false;
    }
    const auto *funcBody = reinterpret_cast<const GCFuncBody *>(payloadBytes.data());
    if (!funcBody->calleeGraph) {
        return false;
    }

    const std::vector<DraftFormalBinding> bindings =
        collectDirectFuncSpecializations(draft, funcNodeId, funcBody->calleeGraph);
    CAMEL_LOG_INFO_S(
        "DraftOpt",
        "Specialize FUNC node {} callee='{}' bindings={}.",
        funcNodeId,
        funcBody->calleeGraph->name(),
        bindings.size());
    if (bindings.empty()) {
        return false;
    }

    GCGraph *specializedGraph =
        specializeGraphWithBindings(funcBody->calleeGraph, bindings, "spec", specializationNonce++);
    ASSERT(specializedGraph != nullptr, "Direct FUNC specialization produced a null graph.");

    DraftNodeInit init = snapshotNodeInit(draft, funcNodeId);
    std::vector<gc_node_ref_t> normInputs(
        draft.normInputsOf(funcNodeId).begin(),
        draft.normInputsOf(funcNodeId).end());
    std::vector<gc_node_ref_t> withInputs(
        draft.withInputsOf(funcNodeId).begin(),
        draft.withInputsOf(funcNodeId).end());

    const size_t normCount = funcBody->calleeGraph->normPorts().size();
    for (auto it = bindings.rbegin(); it != bindings.rend(); ++it) {
        if (it->kind == FormalKind::Norm) {
            normInputs.erase(normInputs.begin() + static_cast<std::ptrdiff_t>(it->index));
            continue;
        }
        const size_t withIndex = it->index - normCount;
        if (withIndex < withInputs.size()) {
            withInputs.erase(withInputs.begin() + static_cast<std::ptrdiff_t>(withIndex));
        }
    }

    GCFuncBody newBody{.calleeGraph = specializedGraph};
    std::array<std::byte, sizeof(GCFuncBody)> newPayload{};
    std::memcpy(newPayload.data(), &newBody, sizeof(newBody));
    init.payload    = std::span<const std::byte>(newPayload.data(), newPayload.size());
    init.normInputs = normInputs;
    init.withInputs = withInputs;
    draft.rewriteNode(funcNodeId, init);
    return true;
}

bool devirtualizeStaticCallInDraft(GraphDraft &draft, gc_node_ref_t callNodeId) {
    static size_t specializationNonce = 0;

    const DraftNodeHeader *callHeader = draft.header(callNodeId);
    if (!callHeader || callHeader->kind != GCNodeKind::Call) {
        return false;
    }

    const auto withInputs = draft.withInputsOf(callNodeId);
    if (withInputs.empty()) {
        CAMEL_LOG_INFO_S(
            "DraftOpt",
            "Devirtualize CALL node {} skipped: no callee input.",
            callNodeId);
        return false;
    }

    DraftStaticValue calleeStatic = tryResolveStaticValue(draft, withInputs.front());
    if (!calleeStatic.valid() || !calleeStatic.type ||
        calleeStatic.type->code() != camel::core::type::TypeCode::Function) {
        CAMEL_LOG_INFO_S(
            "DraftOpt",
            "Devirtualize CALL node {} skipped: callee is not a static Function.",
            callNodeId);
        return false;
    }

    ::Function *function = camel::core::rtdata::fromSlot<::Function *>(calleeStatic.value);
    if (!function || !function->runtimeGraph()) {
        CAMEL_LOG_INFO_S(
            "DraftOpt",
            "Devirtualize CALL node {} skipped: Function carrier has no runtime graph.",
            callNodeId);
        return false;
    }

    GCGraph *targetGraph = specializeClosureBoundGraph(function, specializationNonce++);
    if (!targetGraph) {
        return false;
    }

    const size_t directWithCount = withInputs.size() - 1;
    if (targetGraph->withPorts().size() != directWithCount) {
        CAMEL_LOG_INFO_S(
            "DraftOpt",
            "Devirtualize CALL node {} skipped: target with-arity {} != call with-arity {}.",
            callNodeId,
            targetGraph->withPorts().size(),
            directWithCount);
        return false;
    }
    if (!targetGraph->closureNodes().empty()) {
        CAMEL_LOG_INFO_S(
            "DraftOpt",
            "Devirtualize CALL node {} skipped: specialized target still has {} closure nodes.",
            callNodeId,
            targetGraph->closureNodes().size());
        return false;
    }

    DraftNodeInit init = snapshotNodeInit(draft, callNodeId);
    GCFuncBody funcBody{.calleeGraph = targetGraph};
    std::array<std::byte, sizeof(GCFuncBody)> newPayload{};
    std::memcpy(newPayload.data(), &funcBody, sizeof(funcBody));
    init.kind       = GCNodeKind::Func;
    init.payload    = std::span<const std::byte>(newPayload.data(), newPayload.size());
    init.withInputs = std::span<const gc_node_ref_t>(withInputs.begin() + 1, directWithCount);
    draft.rewriteNode(callNodeId, init);
    CAMEL_LOG_INFO_S(
        "DraftOpt",
        "Devirtualized CALL node {} to direct FUNC '{}'.",
        callNodeId,
        targetGraph->name());
    return true;
}

DraftInlineResult inlineCallableInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t funcNodeId) {
    DraftInlineResult result{.callNode = funcNodeId};
    CAMEL_LOG_INFO_S("DraftInline", "Inline splice begin for draft node {}.", funcNodeId);
    const DraftNodeHeader *funcHeader = draft.header(funcNodeId);
    if (!funcHeader || funcHeader->kind != GCNodeKind::Func) {
        CAMEL_LOG_INFO_S(
            "DraftInline",
            "Inline splice node {} rejected: node is missing or not FUNC.",
            funcNodeId);
        return result;
    }

    const auto payloadBytes = draft.payloadOf(funcNodeId);
    if (payloadBytes.size_bytes() < sizeof(GCFuncBody)) {
        CAMEL_LOG_INFO_S(
            "DraftInline",
            "Inline splice node {} rejected: payload too small ({} bytes).",
            funcNodeId,
            payloadBytes.size_bytes());
        return result;
    }
    const auto *funcBody = reinterpret_cast<const GCFuncBody *>(payloadBytes.data());
    if (!funcBody->calleeGraph) {
        CAMEL_LOG_INFO_S(
            "DraftInline",
            "Inline splice node {} rejected: null callee graph.",
            funcNodeId);
        return result;
    }

    const GraphDraft *calleeDraftView             = session.tryDraft(funcBody->calleeGraph);
    const std::vector<gc_node_ref_t> actualInputs = orderedFuncActualInputs(draft, funcNodeId);
    const auto formalNodes = calleeDraftView ? orderedDirectCallFormalNodes(*calleeDraftView)
                                             : orderedDirectCallFormalNodes(funcBody->calleeGraph);
    if (actualInputs.size() != formalNodes.size()) {
        CAMEL_LOG_INFO_S(
            "DraftInline",
            "Inline splice node {} rejected: actual/formal mismatch ({} vs {}).",
            funcNodeId,
            actualInputs.size(),
            formalNodes.size());
        return DraftInlineResult{};
    }
    const gc_node_ref_t calleeEntry =
        calleeDraftView ? calleeDraftView->entryNode() : funcBody->calleeGraph->entryNodeRef();
    const gc_node_ref_t calleeOutput =
        calleeDraftView ? calleeDraftView->outputNode() : funcBody->calleeGraph->outputNodeRef();
    if (calleeEntry == kInvalidNodeRef || calleeOutput == kInvalidNodeRef) {
        CAMEL_LOG_INFO_S(
            "DraftInline",
            "Inline splice node {} rejected: callee entry/output is incomplete (entry={}, "
            "output={}).",
            funcNodeId,
            calleeEntry,
            calleeOutput);
        return DraftInlineResult{};
    }
    CAMEL_LOG_INFO_S(
        "DraftInline",
        "Inline splice node {} validated callee graph {:p}.",
        funcNodeId,
        static_cast<void *>(funcBody->calleeGraph));

    const std::vector<gc_node_ref_t> ctrlPreds(
        draft.ctrlInputsOf(funcNodeId).begin(),
        draft.ctrlInputsOf(funcNodeId).end());
    const bool hasExternalCtrl    = !ctrlPreds.empty();
    const bool needControlBridge  = hasExternalCtrl || !draft.ctrlUsersOf(funcNodeId).empty() ||
                                    draft.isBranchArmAnchor(funcNodeId) ||
                                    draft.entryNode() == funcNodeId ||
                                    draft.exitNode() == funcNodeId;
    const bool needParameterGates = hasExternalCtrl;

    DraftGraphCloneResult cloned = calleeDraftView
                                       ? cloneDraftGraphIntoDraft(draft, *calleeDraftView)
                                       : cloneRuntimeGraphIntoDraft(draft, funcBody->calleeGraph);
    CAMEL_LOG_INFO_S(
        "DraftInline",
        "Inline splice node {} cloned {} nodes from callee.",
        funcNodeId,
        cloned.clonedNodes.size());
    const std::vector<gc_node_ref_t> entryRoots =
        calleeDraftView ? collectExecutableEntryRoots(*calleeDraftView, cloned.sourceToCloned)
                        : collectExecutableEntryRoots(funcBody->calleeGraph, cloned.sourceToCloned);
    CAMEL_LOG_INFO_S(
        "DraftInline",
        "Inline splice node {} collected {} executable entry roots.",
        funcNodeId,
        entryRoots.size());

    std::vector<gc_node_ref_t> boundActualInputs = actualInputs;
    std::vector<gc_node_ref_t> parameterGateTargets;
    if (needParameterGates) {
        parameterGateTargets.reserve(actualInputs.size());
        for (size_t i = 0; i < actualInputs.size(); ++i) {
            const DraftNodeHeader *actualHeader = draft.header(actualInputs[i]);
            DraftNodeInit gateInit{
                .dataIndex = actualHeader ? actualHeader->dataIndex : static_cast<gc_slot_idx_t>(0),
                .dataType  = actualHeader ? actualHeader->dataType : nullptr,
                .kind      = GCNodeKind::Gate,
                .runtimeFlags = 0,
                .normInputs   = std::span<const gc_node_ref_t>(&actualInputs[i], 1),
            };
            const gc_node_ref_t gateId = draft.addNode(gateInit);
            parameterGateTargets.push_back(gateId);
            boundActualInputs[i] = gateId;
        }
    }

    const auto clonedFormalNodes =
        calleeDraftView ? orderedClonedDirectCallFormalNodes(cloned, *calleeDraftView)
                        : orderedClonedDirectCallFormalNodes(cloned, funcBody->calleeGraph);
    ASSERT(
        clonedFormalNodes.size() == formalNodes.size(),
        "Runtime inline formal-node planning diverged between source and cloned graphs.");

    size_t actualIndex = 0;
    for (const auto &[kind, portId] : clonedFormalNodes) {
        (void)kind;
        bindPortValueUses(draft, portId, boundActualInputs[actualIndex++]);
    }
    CAMEL_LOG_INFO_S(
        "DraftInline",
        "Inline splice node {} rebound {} formal inputs.",
        funcNodeId,
        actualIndex);

    // Callee ports are now pure placeholders. They should disappear before the
    // owner graph is re-encoded so later passes never observe cloned parameter
    // carriers as real executable nodes.
    std::unordered_set<gc_node_ref_t> erasedFormals;
    for (const auto &[kind, formalId] : clonedFormalNodes) {
        (void)kind;
        if (erasedFormals.insert(formalId).second && draft.alive(formalId)) {
            draft.eraseNode(formalId);
        }
    }

    std::vector<gc_node_ref_t> entryTargets = entryRoots;
    if (entryTargets.empty()) {
        entryTargets = parameterGateTargets;
    }
    if (entryTargets.empty() && cloned.entryNode != kInvalidNodeRef) {
        entryTargets.push_back(remapFormalNodeToActual(
            cloned.entryNode,
            cloned,
            funcBody->calleeGraph,
            calleeDraftView,
            boundActualInputs));
    }
    if (entryTargets.size() == 1) {
        result.ctrlEntry = entryTargets.front();
        const std::vector<gc_node_ref_t> mergedCtrlInputs =
            appendUniqueRefs(draft.ctrlInputsOf(result.ctrlEntry), ctrlPreds);
        draft.setCtrlInputs(result.ctrlEntry, mergedCtrlInputs);
    } else {
        DraftNodeInit syncInit{
            .dataIndex    = 0,
            .dataType     = nullptr,
            .kind         = GCNodeKind::Sync,
            .runtimeFlags = 0,
            .ctrlInputs   = ctrlPreds,
        };
        result.ctrlEntry = draft.addNode(syncInit);
        for (gc_node_ref_t targetId : entryTargets) {
            const std::vector<gc_node_ref_t> mergedCtrlInputs = appendUniqueRefs(
                draft.ctrlInputsOf(targetId),
                std::span<const gc_node_ref_t>(&result.ctrlEntry, 1));
            draft.setCtrlInputs(targetId, mergedCtrlInputs);
        }
    }

    if (!parameterGateTargets.empty() && result.ctrlEntry != kInvalidNodeRef) {
        for (gc_node_ref_t gateId : parameterGateTargets) {
            if (gateId == result.ctrlEntry) {
                continue;
            }
            const std::vector<gc_node_ref_t> gateCtrlInputs = appendUniqueRefs(
                draft.ctrlInputsOf(gateId),
                std::span<const gc_node_ref_t>(&result.ctrlEntry, 1));
            draft.setCtrlInputs(gateId, gateCtrlInputs);
        }
    }

    result.valueExit = resolveInlineValueExit(
        draft,
        cloned,
        funcBody->calleeGraph,
        calleeDraftView,
        boundActualInputs);
    if (result.valueExit == kInvalidNodeRef) {
        return DraftInlineResult{};
    }
    result.ctrlExit = resolveInlineCtrlExit(
        draft,
        cloned,
        funcBody->calleeGraph,
        calleeDraftView,
        boundActualInputs);
    if (result.ctrlExit == kInvalidNodeRef) {
        result.ctrlExit = result.valueExit;
    }
    if (hasExternalCtrl && needControlBridge &&
        (result.ctrlEntry == kInvalidNodeRef || !draft.isControlAnchor(result.ctrlEntry))) {
        const DraftNodeHeader *valueHeader = draft.header(result.valueExit);
        DraftNodeInit gateInit{
            .dataIndex    = valueHeader ? valueHeader->dataIndex : static_cast<gc_slot_idx_t>(0),
            .dataType     = valueHeader ? valueHeader->dataType : nullptr,
            .kind         = GCNodeKind::Gate,
            .runtimeFlags = 0,
            .normInputs   = std::span<const gc_node_ref_t>(&result.valueExit, 1),
            .ctrlInputs   = ctrlPreds,
        };
        const gc_node_ref_t bridgeGate = draft.addNode(gateInit);
        result.ctrlEntry               = bridgeGate;
        result.ctrlExit                = bridgeGate;
        result.valueExit               = bridgeGate;
    } else if (result.ctrlEntry == kInvalidNodeRef) {
        result.ctrlEntry = result.ctrlExit;
    }
    draft.retargetBranchArmAnchors(funcNodeId, result.ctrlEntry, result.ctrlExit);

    // Value users and control users observing the call result now observe the
    // inlined output/completion anchor instead.
    draft.replaceAllValueUses(funcNodeId, result.valueExit);
    draft.replaceAllCtrlUses(funcNodeId, result.ctrlExit);

    draft.eraseNode(funcNodeId);
    CAMEL_LOG_INFO_S("DraftInline", "Inline splice finished for draft node {}.", funcNodeId);
    return result;
}

} // namespace camel::runtime
