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
 * Updated: May. 05, 2026
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
#include "camel/runtime/draft_types.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <tuple>
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
    // Without a static value: the formal keeps its port, with this more precise type (the actual
    // argument's), and the specialization re-infers its types from it.
    camel::core::type::Type *refinedType = nullptr;

    bool bindsValue() const { return staticValue.valid(); }
};

/// Marks a binding key as a type refinement (its value is unused).
constexpr uint8_t kTypeBindingFlag = 0xFF;

/// True when `actual` says strictly more than `formal` about the same values (e.g. a tensor type
/// with a static shape against the plain tensor type).
bool refinesType(camel::core::type::Type *actual, camel::core::type::Type *formal) {
    return actual && formal && actual != formal && formal->assignableFrom(actual) &&
           !actual->assignableFrom(formal);
}

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

RuntimeSpecializationBindingKind toRuntimeSpecializationBindingKind(FormalKind kind) {
    switch (kind) {
    case FormalKind::Norm:
        return RuntimeSpecializationBindingKind::Norm;
    case FormalKind::With:
        return RuntimeSpecializationBindingKind::With;
    case FormalKind::Closure:
        return RuntimeSpecializationBindingKind::Closure;
    }
    ASSERT(false, "Unsupported runtime specialization binding kind.");
    return RuntimeSpecializationBindingKind::Norm;
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

/// True when `graph` can call itself again through direct calls (it lies on a call cycle).
bool onCallCycle(const GCGraph *graph) {
    std::unordered_set<const GCGraph *> seen;
    std::vector<const GCGraph *> work{graph};
    while (!work.empty()) {
        const GCGraph *g = work.back();
        work.pop_back();
        for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
            const GCGraph *callee = g->directCalleeGraphOf(it.ref());
            if (!callee) {
                continue;
            }
            if (callee == graph) {
                return true;
            }
            if (seen.insert(callee).second) {
                work.push_back(callee);
            }
        }
    }
    return false;
}

/// True when node `id` runs inside an arm of a branch of the draft (between a BRCH and its JOIN).
bool insideBranchArm(const GraphDraft &draft, gc_node_ref_t id) {
    for (gc_node_ref_t brch = 0; brch < draft.nodeSlotCount(); ++brch) {
        const auto *h = draft.header(brch);
        if (!h || h->kind != GCNodeKind::Brch) {
            continue;
        }
        const gc_node_ref_t join =
            reinterpret_cast<const DraftBrchPayload *>(draft.payloadOf(brch).data())->join;
        std::vector<bool> seen(draft.nodeSlotCount(), false);
        std::vector<gc_node_ref_t> work;
        for (auto users : {draft.normUsersOf(brch), draft.withUsersOf(brch), draft.ctrlUsersOf(brch)}) {
            work.insert(work.end(), users.begin(), users.end());
        }
        while (!work.empty()) {
            const gc_node_ref_t n = work.back();
            work.pop_back();
            if (n == join || n >= seen.size() || seen[n] || !draft.header(n)) {
                continue;
            }
            if (n == id) {
                return true;
            }
            seen[n] = true;
            for (auto users : {draft.normUsersOf(n), draft.withUsersOf(n), draft.ctrlUsersOf(n)}) {
                work.insert(work.end(), users.begin(), users.end());
            }
        }
    }
    return false;
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
        // An argument whose value is not bound (dynamic, or an object, see below) may still have a
        // type more precise than the parameter's (a tensor whose type carries its shape): then the
        // callee's types are specialized to it.
        const auto bindType = [&] {
            const auto *actualHeader = draft.header(actualInputs[i]);
            const auto *formalNode   = calleeGraph->node(formals[i].second);
            if (actualHeader && formalNode &&
                refinesType(actualHeader->dataType, formalNode->dataType)) {
                bindings.push_back(
                    DraftFormalBinding{
                        .kind         = formals[i].first,
                        .index        = i,
                        .formalNodeId = formals[i].second,
                        .staticValue  = {},
                        .refinedType  = actualHeader->dataType,
                    });
            }
        };
        if (!staticValue.valid()) {
            bindType();
            continue;
        }
        if (staticValue.type->isGCTraced()) {
            bindType();
            // Direct-call specialization bakes the bound value into the callee
            // as a new static carrier. That is correct for immutable primitive
            // values, but it breaks runtime object identity for GC values. A
            // common failure mode is `COPY(static [])`: the caller intentionally
            // materializes a fresh mutable array, while specialization would
            // incorrectly capture the original static template object and
            // disconnect later mutations from the caller-visible runtime copy.
            //
            // Closure-bound Function specialization remains handled by the
            // dedicated closure path below. Here we conservatively keep all
            // GC-traced actuals dynamic.
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

    // Binding constants into a call of a recursive function unrolls one level. That only ends
    // when the recursion's stop condition becomes constant, and then folding has already removed
    // the branch guarding the call. A call still guarded by a branch whose condition is not
    // constant would unroll forever: it gets type refinements only.
    const bool bindsValues = std::ranges::any_of(bindings, [](const DraftFormalBinding &b) {
        return b.bindsValue();
    });
    if (bindsValues && insideBranchArm(draft, funcNodeId) && onCallCycle(calleeGraph)) {
        std::erase_if(bindings, [](const DraftFormalBinding &b) { return b.bindsValue(); });
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

RuntimeSpecializationKey makeRuntimeSpecializationKey(
    const GCGraph *baseGraph, std::span<const DraftFormalBinding> bindings) {
    RuntimeSpecializationKey key{
        .baseGraph = const_cast<GCGraph *>(baseGraph),
        .bindings  = {},
    };
    key.bindings.reserve(bindings.size());
    for (const DraftFormalBinding &binding : bindings) {
        key.bindings.push_back(
            binding.bindsValue()
                ? RuntimeSpecializationBindingKey{
                      .kind         = toRuntimeSpecializationBindingKind(binding.kind),
                      .index        = binding.index,
                      .value        = binding.staticValue.value,
                      .type         = binding.staticValue.type,
                      .runtimeFlags = binding.staticValue.runtimeFlags,
                  }
                : RuntimeSpecializationBindingKey{
                      .kind         = toRuntimeSpecializationBindingKind(binding.kind),
                      .index        = binding.index,
                      .value        = NullSlot,
                      .type         = binding.refinedType,
                      .runtimeFlags = kTypeBindingFlag,
                  });
    }
    std::sort(
        key.bindings.begin(),
        key.bindings.end(),
        [](const RuntimeSpecializationBindingKey &lhs, const RuntimeSpecializationBindingKey &rhs) {
            return std::tie(lhs.kind, lhs.index, lhs.type, lhs.value, lhs.runtimeFlags) <
                   std::tie(rhs.kind, rhs.index, rhs.type, rhs.value, rhs.runtimeFlags);
        });
    return key;
}

GCGraph *specializeGraphWithBindings(
    RuntimeGraphDraftSession &session, const GCGraph *baseGraph,
    std::span<const DraftFormalBinding> bindings, std::string_view tag, size_t nonce) {
    if (!baseGraph || bindings.empty()) {
        return const_cast<GCGraph *>(baseGraph);
    }

    RuntimeSpecializationKey cacheKey = makeRuntimeSpecializationKey(baseGraph, bindings);
    if (GCGraph *cached = session.findSpecialization(cacheKey)) {
        return cached;
    }

    auto draft          = GraphDraft::decode(baseGraph);
    bool refinesTypes   = false;
    for (const DraftFormalBinding &binding : bindings) {
        const gc_node_ref_t formalDraftId = draft->draftIdOfSourceRef(binding.formalNodeId);
        ASSERT(
            formalDraftId != kInvalidNodeRef,
            "Specialization lost the formal-node mapping while decoding the callee graph.");
        if (!binding.bindsValue()) {
            draft->setNodeDataType(formalDraftId, binding.refinedType);
            refinesTypes = true;
            continue;
        }
        const gc_node_ref_t staticNodeId = draft->materializeStaticValue(
            binding.staticValue.value,
            binding.staticValue.type,
            binding.staticValue.runtimeFlags);
        draft->replaceAllValueUses(formalDraftId, staticNodeId);
        eraseFormalFromDraft(*draft, binding.kind, formalDraftId);
    }
    if (refinesTypes) {
        (void)reinferDraftTypes(*draft);
    }
    GCGraph *specialized = encodeSpecializedGraph(*draft, baseGraph, tag, nonce);
    session.rememberSpecialization(std::move(cacheKey), specialized);
    return specialized;
}

GCGraph *specializeClosureBoundGraph(
    RuntimeGraphDraftSession &session, const Function *function, size_t nonce) {
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
    return specializeGraphWithBindings(session, targetGraph, bindings, "closure", nonce);
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

gc_node_ref_t resolveValueCandidate(
    const GraphDraft &draft, gc_node_ref_t candidate,
    std::span<const gc_node_ref_t> boundActualInputs) {
    if (candidate == kInvalidNodeRef) {
        return kInvalidNodeRef;
    }

    const auto *header = draft.header(candidate);
    if (!header) {
        return kInvalidNodeRef;
    }

    if (header->kind == GCNodeKind::Gate &&
        std::find(boundActualInputs.begin(), boundActualInputs.end(), candidate) !=
            boundActualInputs.end()) {
        return header->dataIndex != 0 ? candidate : kInvalidNodeRef;
    }

    const gc_node_ref_t resolved = draft.resolveForwardedValueRef(candidate);
    if (resolved == kInvalidNodeRef) {
        return kInvalidNodeRef;
    }

    const auto *resolvedHeader = draft.header(resolved);
    if (!resolvedHeader || resolvedHeader->dataIndex == 0) {
        return kInvalidNodeRef;
    }
    return resolved;
}

gc_node_ref_t resolveInlineValueExit(
    const GraphDraft &draft, const DraftGraphCloneResult &cloned, const GCGraph *calleeGraph,
    const GraphDraft *calleeDraftView, std::span<const gc_node_ref_t> boundActualInputs) {
    std::array<gc_node_ref_t, 3> candidates{
        cloned.returnNode,
        cloned.outputNode,
        cloned.exitNode,
    };
    for (gc_node_ref_t current : candidates) {
        if (current == kInvalidNodeRef) {
            continue;
        }
        current = remapFormalNodeToActual(
            current,
            cloned,
            calleeGraph,
            calleeDraftView,
            boundActualInputs);
        if (const gc_node_ref_t resolved = resolveValueCandidate(draft, current, boundActualInputs);
            resolved != kInvalidNodeRef) {
            return resolved;
        }
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

bool specializeDirectFuncInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t funcNodeId) {
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

    GCGraph *specializedGraph = specializeGraphWithBindings(
        session,
        funcBody->calleeGraph,
        bindings,
        "spec",
        specializationNonce++);
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
        if (!it->bindsValue()) {
            continue; // the argument is still passed, to a parameter of a more precise type
        }
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
    if (const auto *type = specializedGraph->funcType(); type && type->hasExitType()) {
        init.dataType = type->exitType();
    }
    draft.rewriteNode(funcNodeId, init);
    return true;
}

gc_node_ref_t resolveTupleProjection(const GraphDraft &draft, gc_node_ref_t id) {
    while (true) {
        const DraftNodeHeader *header = draft.header(id);
        if (!header || header->kind != GCNodeKind::Accs) {
            return id;
        }
        const auto *accs = reinterpret_cast<const GCAccsBody *>(draft.payloadOf(id).data());
        const auto sources = draft.normInputsOf(id);
        if (accs->accsKind != GCAccsKind::TupleIndex || sources.size() != 1) {
            return id;
        }
        const DraftNodeHeader *sourceHeader = draft.header(sources[0]);
        if (!sourceHeader || sourceHeader->kind != GCNodeKind::Fill) {
            return id;
        }
        const auto *fill = reinterpret_cast<const GCFillBody *>(draft.payloadOf(sources[0]).data());
        if (fill->fillKind != GCFillKind::Tuple) {
            return id;
        }
        const auto slots  = fill->slots();
        const auto values = draft.withInputsOf(sources[0]);
        const auto it     = std::find(slots.begin(), slots.end(), static_cast<gc_slot_idx_t>(accs->value));
        if (it == slots.end() || static_cast<size_t>(it - slots.begin()) >= values.size()) {
            return id; // the element comes from the template
        }
        id = values[static_cast<size_t>(it - slots.begin())];
    }
}

/// A CALL rewritten into a direct FUNC no longer reads its callee value. When that value carried
/// ordering (in sync code the callee is gated after the previous statement), the FUNC keeps it
/// as a control input, so the statements before still run first. A callee that waits on nothing
/// (a constant, a closure built from values) carries no ordering.
void keepCalleeOrdering(GraphDraft &draft, gc_node_ref_t funcNodeId, gc_node_ref_t callee) {
    const DraftNodeHeader *header = draft.header(callee);
    if (!header || draft.ctrlInputsOf(callee).empty()) {
        return;
    }
    const auto existing = draft.ctrlInputsOf(funcNodeId);
    if (callee != funcNodeId && std::find(existing.begin(), existing.end(), callee) == existing.end()) {
        draft.appendInput(DraftEdgeKind::Ctrl, funcNodeId, callee);
    }
}

namespace {

/// `base` with its closure nodes turned into trailing norm ports, in closure order (lambda
/// lifting): calling it with the captures as extra arguments equals calling the closure.
GCGraph *liftClosureGraph(RuntimeGraphDraftSession &session, GCGraph *base, size_t nonce) {
    RuntimeSpecializationKey key{
        .baseGraph = base,
        .bindings  = {RuntimeSpecializationBindingKey{.kind = RuntimeSpecializationBindingKind::Lift}},
    };
    if (GCGraph *cached = session.findSpecialization(key)) {
        return cached;
    }
    auto draft                            = GraphDraft::decode(base);
    camel::core::type::FunctionType *type = draft->funcType();
    if (!type) {
        return nullptr;
    }
    camel::core::type::param_vec_t withTypes, normTypes;
    for (size_t i = 0; i < type->withTypesCount(); ++i) {
        withTypes.emplace_back(type->withTypeAt(i), type->withIsVarAt(i));
    }
    for (size_t i = 0; i < type->normTypesCount(); ++i) {
        normTypes.emplace_back(type->normTypeAt(i), type->normIsVarAt(i));
    }
    const std::vector<gc_node_ref_t> captures(
        draft->closureNodes().begin(),
        draft->closureNodes().end());
    for (gc_node_ref_t capture : captures) {
        draft->removeClosureNode(capture);
        draft->appendNormPort(capture);
        normTypes.emplace_back(draft->header(capture)->dataType, false);
    }
    draft->setFuncType(camel::core::type::FunctionType::create(
        withTypes,
        normTypes,
        type->exitType(),
        type->modifiers()));
    draft->setClosureType(nullptr);
    GCGraph *lifted = encodeSpecializedGraph(*draft, base, "lift", nonce);
    session.rememberSpecialization(std::move(key), lifted);
    return lifted;
}

/// A CALL of a closure built by FILL in this graph becomes a direct FUNC of the lifted graph.
bool devirtualizeClosureCallInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t callNodeId,
    gc_node_ref_t fillNodeId) {
    static size_t liftNonce = 0;
    const auto *fill = reinterpret_cast<const GCFillBody *>(draft.payloadOf(fillNodeId).data());
    if (fill->fillKind != GCFillKind::FunctionClosure || draft.normInputsOf(fillNodeId).size() != 1) {
        return false;
    }
    DraftStaticValue source = tryResolveStaticValue(draft, draft.normInputsOf(fillNodeId)[0]);
    if (!source.valid() || !source.type ||
        source.type->code() != camel::core::type::TypeCode::Function) {
        return false;
    }
    ::Function *function = camel::core::rtdata::fromSlot<::Function *>(source.value);
    GCGraph *base        = function ? function->runtimeGraph() : nullptr;
    if (!base) {
        return false;
    }
    // Every capture must be filled here; captures left in the template are not handled.
    const auto slots  = fill->slots();
    const auto values = draft.withInputsOf(fillNodeId);
    const size_t n    = base->closureNodes().size();
    if (slots.size() != n || values.size() != n) {
        return false;
    }
    std::vector<gc_node_ref_t> captures(n, kInvalidNodeRef);
    for (size_t k = 0; k < n; ++k) {
        const auto slot = static_cast<size_t>(slots[k]);
        if (slot >= n || captures[slot] != kInvalidNodeRef) {
            return false;
        }
        captures[slot] = values[k];
    }
    const auto withInputs = draft.withInputsOf(callNodeId);
    if (base->withPorts().size() != withInputs.size() - 1) {
        return false;
    }
    GCGraph *lifted = liftClosureGraph(session, base, liftNonce++);
    if (!lifted) {
        return false;
    }

    DraftNodeInit init = snapshotNodeInit(draft, callNodeId);
    std::vector<gc_node_ref_t> normInputs(
        draft.normInputsOf(callNodeId).begin(),
        draft.normInputsOf(callNodeId).end());
    normInputs.insert(normInputs.end(), captures.begin(), captures.end());
    const std::vector<gc_node_ref_t> directWith(withInputs.begin() + 1, withInputs.end());
    GCFuncBody funcBody{.calleeGraph = lifted};
    std::array<std::byte, sizeof(GCFuncBody)> payload{};
    std::memcpy(payload.data(), &funcBody, sizeof(funcBody));
    init.kind       = GCNodeKind::Func;
    init.payload    = std::span<const std::byte>(payload.data(), payload.size());
    init.normInputs = normInputs;
    init.withInputs = directWith;
    const gc_node_ref_t calleeInput = withInputs.front();
    draft.rewriteNode(callNodeId, init);
    keepCalleeOrdering(draft, callNodeId, calleeInput);
    CAMEL_LOG_INFO_S(
        "DraftOpt",
        "Devirtualized closure CALL node {} to direct FUNC '{}'.",
        callNodeId,
        lifted->name());
    return true;
}

} // namespace

bool devirtualizeStaticCallInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t callNodeId) {
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

    const gc_node_ref_t callee = resolveTupleProjection(draft, withInputs.front());
    if (const auto *calleeHeader = draft.header(callee);
        calleeHeader && calleeHeader->kind == GCNodeKind::Fill) {
        return devirtualizeClosureCallInDraft(session, draft, callNodeId, callee);
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

    GCGraph *targetGraph = specializeClosureBoundGraph(session, function, specializationNonce++);
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
    const std::vector<gc_node_ref_t> directWith(withInputs.begin() + 1, withInputs.end());
    const gc_node_ref_t calleeInput = withInputs.front();
    init.withInputs                 = directWith;
    draft.rewriteNode(callNodeId, init);
    keepCalleeOrdering(draft, callNodeId, calleeInput);
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
    std::vector<gc_node_ref_t> parameterCtrlInputs = ctrlPreds;
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
        result.ctrlEntry    = draft.addNode(syncInit);
        parameterCtrlInputs = {result.ctrlEntry};
        for (gc_node_ref_t targetId : entryTargets) {
            const std::vector<gc_node_ref_t> mergedCtrlInputs = appendUniqueRefs(
                draft.ctrlInputsOf(targetId),
                std::span<const gc_node_ref_t>(&result.ctrlEntry, 1));
            draft.setCtrlInputs(targetId, mergedCtrlInputs);
        }
    }

    if (!parameterGateTargets.empty() && !parameterCtrlInputs.empty()) {
        for (gc_node_ref_t gateId : parameterGateTargets) {
            if (gateId == result.ctrlEntry) {
                continue;
            }
            // Parameter gates must become ready before callee entry nodes consume
            // them. Wiring a gate to the resolved entry node itself can form a
            // data/control cycle when that entry node reads the same gate (for
            // example recursive `timeit` specializations where `repeat - 1`
            // feeds the next branch condition). Multi-entry splices still use
            // the synthetic SYNC anchor because it precedes every entry root.
            const std::vector<gc_node_ref_t> gateCtrlInputs =
                appendUniqueRefs(draft.ctrlInputsOf(gateId), parameterCtrlInputs);
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
    {
        ASSERT(
            draft.header(result.valueExit) != nullptr &&
                draft.header(result.valueExit)->dataIndex != 0,
            std::format(
                "Runtime inline resolved value exit {} with slot {} in caller '{}' while "
                "inlining callee '{}'.",
                result.valueExit,
                draft.header(result.valueExit) ? draft.header(result.valueExit)->dataIndex : 0,
                "<draft>",
                funcBody->calleeGraph ? funcBody->calleeGraph->name() : "<null>"));
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
    const bool needValueCtrlBridge =
        result.ctrlExit != kInvalidNodeRef && result.ctrlExit != result.valueExit;
    if (hasExternalCtrl && needControlBridge &&
        (result.ctrlEntry == kInvalidNodeRef || !draft.isControlAnchor(result.ctrlEntry))) {
        const DraftNodeHeader *valueHeader        = draft.header(result.valueExit);
        std::vector<gc_node_ref_t> gateCtrlInputs = ctrlPreds;
        if (result.ctrlExit != kInvalidNodeRef) {
            gateCtrlInputs = appendUniqueRefs(
                std::span<const gc_node_ref_t>(gateCtrlInputs.data(), gateCtrlInputs.size()),
                std::span<const gc_node_ref_t>(&result.ctrlExit, 1));
        }
        DraftNodeInit gateInit{
            .dataIndex    = valueHeader ? valueHeader->dataIndex : static_cast<gc_slot_idx_t>(0),
            .dataType     = valueHeader ? valueHeader->dataType : nullptr,
            .kind         = GCNodeKind::Gate,
            .runtimeFlags = 0,
            .normInputs   = std::span<const gc_node_ref_t>(&result.valueExit, 1),
            .ctrlInputs   = gateCtrlInputs,
        };
        const gc_node_ref_t bridgeGate = draft.addNode(gateInit);
        result.ctrlEntry               = bridgeGate;
        result.ctrlExit                = bridgeGate;
        result.valueExit               = bridgeGate;
    }
    if (needValueCtrlBridge) {
        const DraftNodeHeader *valueHeader = draft.header(result.valueExit);
        DraftNodeInit gateInit{
            .dataIndex    = valueHeader ? valueHeader->dataIndex : static_cast<gc_slot_idx_t>(0),
            .dataType     = valueHeader ? valueHeader->dataType : nullptr,
            .kind         = GCNodeKind::Gate,
            .runtimeFlags = 0,
            .normInputs   = std::span<const gc_node_ref_t>(&result.valueExit, 1),
            .ctrlInputs   = std::span<const gc_node_ref_t>(&result.ctrlExit, 1),
        };
        const gc_node_ref_t bridgeGate = draft.addNode(gateInit);
        result.valueExit               = bridgeGate;
        result.ctrlExit                = bridgeGate;
        if (result.ctrlEntry == kInvalidNodeRef) {
            result.ctrlEntry = bridgeGate;
        }
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
