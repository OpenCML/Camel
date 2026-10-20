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
 * Updated: Oct. 16, 2026
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
#include "camel/runtime/control.h"

#include "camel/core/rtdata/func.h"
#include "camel/runtime/draft_clone.h"
#include "camel/runtime/draft_types.h"
#include "camel/runtime/draft_writes.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <map>
#include <tuple>
#include <unordered_set>
#include <vector>

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

/// Kinds an entry root of an inlined body may have: every node that is not a value carrier.
/// SYNC and GATE count. A callee that was itself inlined into (earlier in the same session, or
/// by an earlier pass) starts with the entry SYNC of that splice, and its first operators wait
/// on that SYNC: they have a non-constant input, so they are not roots themselves. Leaving the
/// SYNC out made such a body have no root at all. The control predecessors of the call (the
/// BRCH whose arm the call is, the statement before it) were then attached to the callee's
/// recorded entry node only, and everything behind the SYNC ran whether the arm was taken or
/// not, and before the statements it was written after.
bool isExecutableEntryKind(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Data:
    case GCNodeKind::Port:
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
        // An argument passed to a `var` parameter is written by the callee (a variable's cell,
        // or an object an operator changes in place): it does not have the static value it
        // was initialized from.
        if (isWrittenInPlace(draft, canonicalId)) {
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
        if (header->kind == GCNodeKind::Cast) {
            // A CAST computes a value: `7 as float` is 7.0, not the bits of the integer 7.
            // Looking through it bound the unconverted constant to the parameter (f(i as
            // float) computed with 0.0...035 instead of 7.0). Only a CAST between equal
            // types forwards its input; a constant under any other CAST becomes a static
            // value when std::opt::fold has evaluated the conversion.
            const auto normInputs = draft.normInputsOf(canonicalId);
            const DraftNodeHeader *source =
                normInputs.empty() ? nullptr : draft.header(normInputs.front());
            if (!source || !source->dataType || source->dataType != header->dataType) {
                return resolved;
            }
            canonicalId = normInputs.front();
            continue;
        }
        if (header->kind == GCNodeKind::Copy || header->kind == GCNodeKind::Dref) {
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
        for (auto users :
             {draft.normUsersOf(brch), draft.withUsersOf(brch), draft.ctrlUsersOf(brch)}) {
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
                refinesType(actualHeader->dataType, formalNode->dataType())) {
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
    const bool bindsValues =
        std::ranges::any_of(bindings, [](const DraftFormalBinding &b) { return b.bindsValue(); });
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
    RuntimeGraphDraftSession &session, GraphDraft &draft, const GCGraph *baseGraph,
    std::string_view tag, size_t nonce) {
    ASSERT(baseGraph != nullptr, "Specialized graph encoding requires a base graph.");
    const std::string suffix      = std::format("${}{}", tag, nonce);
    const std::string mangledName = std::string(baseGraph->mangledName()) + suffix;
    const std::string name        = std::string(baseGraph->name()) + suffix;
    draft.setId(session.context()->graphIds().allocate());
    // A type-only specialization derives from the first function of the chain that is not one;
    // what derives from a specialization keeps what the specialization has.
    if (tag == kTypeSpecializationTag && !isValid(draft.typeSpecializationRoot())) {
        draft.setTypeSpecializationRoot(baseGraph->id());
    }
    return draft.encode(mangledName, name);
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

    auto draft        = GraphDraft::decode(baseGraph);
    bool refinesTypes = false;
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
    GCGraph *specialized = encodeSpecializedGraph(session, *draft, baseGraph, tag, nonce);
    session.rememberSpecialization(std::move(cacheKey), specialized);
    return specialized;
}

} // namespace

void bindFunctionCall(
    GraphDraft &draft, const Function *function,
    std::span<camel::core::type::Type *const> normTypes,
    std::span<camel::core::type::Type *const> closureTypes) {
    ASSERT(function != nullptr, "bindFunctionCall requires a function.");
    if (const Tuple *closure = function->tuple()) {
        const auto *tupleType = function->tupleType();
        const std::vector<gc_node_ref_t> nodes(
            draft.closureNodes().begin(),
            draft.closureNodes().end());
        ASSERT(
            tupleType != nullptr && tupleType->size() == nodes.size(),
            "Function closure size does not match its graph's closure layout.");
        for (size_t i = 0; i < nodes.size(); ++i) {
            const gc_node_ref_t value = draft.materializeStaticValue(
                closure->get<slot_t>(i),
                i < closureTypes.size() && closureTypes[i] ? closureTypes[i] : tupleType->typeAt(i),
                kGCNodeFlagConstant);
            draft.replaceAllValueUses(nodes[i], value);
            eraseFormalFromDraft(draft, FormalKind::Closure, nodes[i]);
        }
    }
    const std::vector<gc_node_ref_t> ports(draft.normPorts().begin(), draft.normPorts().end());
    ASSERT(ports.size() == normTypes.size(), "bindFunctionCall received the wrong argument count.");
    for (size_t i = 0; i < ports.size(); ++i) {
        if (normTypes[i]) {
            draft.setNodeDataType(ports[i], normTypes[i]);
        }
    }
    (void)reinferDraftTypes(draft);
}

namespace {

GCGraph *specializeClosureBoundGraph(
    RuntimeGraphDraftSession &session, const Function *function, size_t nonce) {
    if (!function || !function->graph()) {
        return nullptr;
    }
    GCGraph *targetGraph = function->graph();
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
    // So must the control uses. A node of the body may wait on a parameter without reading it:
    // the front end makes a BRCH wait on every value its arms capture, and a rewrite that
    // removes a node hands its control inputs to its users. Erasing the port dropped those
    // edges, and with them the only thing that ordered such a node after the argument.
    const std::vector<gc_node_ref_t> waiting(
        draft.ctrlUsersOf(portNodeId).begin(),
        draft.ctrlUsersOf(portNodeId).end());
    for (gc_node_ref_t user : waiting) {
        draft.unlinkInput(DraftEdgeKind::Ctrl, user, portNodeId);
        appendControlDependency(draft, actualInputId, user);
    }
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
    GCGraph *const calleeGraph = draft.calleeGraphOf(funcNodeId);
    if (!calleeGraph) {
        return false;
    }

    const std::vector<DraftFormalBinding> bindings =
        collectDirectFuncSpecializations(draft, funcNodeId, calleeGraph);
    CAMEL_LOG_INFO_S(
        "DraftOpt",
        "Specialize FUNC node {} callee='{}' bindings={}.",
        funcNodeId,
        calleeGraph->name(),
        bindings.size());
    if (bindings.empty()) {
        return false;
    }

    // Type-only specialization of a recursive function is bounded (see
    // kMaxTypeSpecializationsPerRecursiveFunction); a copy that already exists is reused freely.
    const bool boundedTypeSpecialization =
        std::ranges::none_of(
            bindings,
            [](const DraftFormalBinding &b) { return b.bindsValue(); }) &&
        onCallCycle(calleeGraph);
    if (boundedTypeSpecialization &&
        !session.findSpecialization(
            makeRuntimeSpecializationKey(calleeGraph, bindings))) {
        const GraphId root = isValid(calleeGraph->typeSpecializationRoot())
                                 ? calleeGraph->typeSpecializationRoot()
                                 : calleeGraph->id();
        if (session.typeSpecializationCount(root) >= kMaxTypeSpecializationsPerRecursiveFunction) {
            CAMEL_LOG_INFO_S(
                "DraftOpt",
                "FUNC node {} keeps callee '{}': '{}' has {} type specializations.",
                funcNodeId,
                calleeGraph->name(),
                toText(root),
                kMaxTypeSpecializationsPerRecursiveFunction);
            return false;
        }
        session.noteTypeSpecialization(root);
    }

    GCGraph *specializedGraph = specializeGraphWithBindings(
        session,
        calleeGraph,
        bindings,
        boundedTypeSpecialization ? kTypeSpecializationTag : std::string_view("spec"),
        specializationNonce++);
    ASSERT(specializedGraph != nullptr, "Direct FUNC specialization produced a null graph.");

    DraftNodeInit init = snapshotNodeInit(draft, funcNodeId);
    std::vector<gc_node_ref_t> normInputs(
        draft.normInputsOf(funcNodeId).begin(),
        draft.normInputsOf(funcNodeId).end());
    std::vector<gc_node_ref_t> withInputs(
        draft.withInputsOf(funcNodeId).begin(),
        draft.withInputsOf(funcNodeId).end());

    const size_t normCount = calleeGraph->normPorts().size();
    std::vector<gc_node_ref_t> boundActuals; // arguments the call no longer passes
    for (auto it = bindings.rbegin(); it != bindings.rend(); ++it) {
        if (!it->bindsValue()) {
            continue; // the argument is still passed, to a parameter of a more precise type
        }
        if (it->kind == FormalKind::Norm) {
            boundActuals.push_back(normInputs[it->index]);
            normInputs.erase(normInputs.begin() + static_cast<std::ptrdiff_t>(it->index));
            continue;
        }
        const size_t withIndex = it->index - normCount;
        if (withIndex < withInputs.size()) {
            boundActuals.push_back(withInputs[withIndex]);
            withInputs.erase(withInputs.begin() + static_cast<std::ptrdiff_t>(withIndex));
        }
    }

    GCFuncRef newBody{.ref = draft.payloadGraphIndexOf(specializedGraph)};
    std::array<std::byte, sizeof(GCFuncRef)> newPayload{};
    std::memcpy(newPayload.data(), &newBody, sizeof(newBody));
    init.payload    = std::span<const std::byte>(newPayload.data(), newPayload.size());
    init.normInputs = normInputs;
    init.withInputs = withInputs;
    if (const auto *type = specializedGraph->funcType(); type && type->hasExitType()) {
        init.dataType = type->exitType();
    }
    draft.rewriteNode(funcNodeId, init);

    // A bound argument may have been a constant seen through a GATE that waits (the result of an
    // inlined sync call: its value is static, but reading it ordered this call after the
    // callee's effects). The call no longer reads it, so it waits on that GATE instead;
    // otherwise everything the GATE waited on would no longer reach the exit and never run.
    for (gc_node_ref_t actual : boundActuals) {
        for (gc_node_ref_t current = actual; current != kInvalidNodeRef;) {
            const DraftNodeHeader *header = draft.header(current);
            if (!header) {
                break;
            }
            if (!draft.ctrlInputsOf(current).empty()) {
                appendControlDependency(draft, current, funcNodeId);
                break;
            }
            if (header->kind != GCNodeKind::Gate && header->kind != GCNodeKind::Copy &&
                header->kind != GCNodeKind::Cast && header->kind != GCNodeKind::Dref) {
                break;
            }
            const auto inputs = draft.normInputsOf(current);
            current           = inputs.empty() ? kInvalidNodeRef : inputs.back();
        }
    }
    return true;
}

gc_node_ref_t resolveTupleProjection(const GraphDraft &draft, gc_node_ref_t id) {
    while (true) {
        const DraftNodeHeader *header = draft.header(id);
        if (!header || header->kind != GCNodeKind::Accs) {
            return id;
        }
        const auto *accs   = reinterpret_cast<const GCAccsBody *>(draft.payloadOf(id).data());
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
        if (isWrittenInPlace(draft, sources[0])) {
            // The tuple is passed to a `var` parameter: what the projection reads is up to
            // the callee, not what was filled here.
            return id;
        }
        const auto slots  = fill->slots();
        const auto values = draft.withInputsOf(sources[0]);
        const auto it =
            std::find(slots.begin(), slots.end(), static_cast<gc_slot_idx_t>(accs->value));
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
    appendControlDependency(draft, callee, funcNodeId);
}

namespace {

/// `base` with its closure nodes turned into trailing norm ports, in closure order (lambda
/// lifting): calling it with the captures as extra arguments equals calling the closure.
GCGraph *liftClosureGraph(RuntimeGraphDraftSession &session, GCGraph *base, size_t nonce) {
    RuntimeSpecializationKey key{
        .baseGraph = base,
        .bindings  = {RuntimeSpecializationBindingKey{
            .kind = RuntimeSpecializationBindingKind::Lift}},
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
    draft->setFuncType(
        camel::core::type::FunctionType::create(
            withTypes,
            normTypes,
            type->exitType(),
            type->modifiers()));
    draft->setClosureType(nullptr);
    GCGraph *lifted = encodeSpecializedGraph(session, *draft, base, "lift", nonce);
    session.rememberSpecialization(std::move(key), lifted);
    return lifted;
}

/// A CALL of a closure built by FILL in this graph becomes a direct FUNC of the lifted graph.
bool devirtualizeClosureCallInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t callNodeId,
    gc_node_ref_t fillNodeId) {
    static size_t liftNonce = 0;
    const auto *fill = reinterpret_cast<const GCFillBody *>(draft.payloadOf(fillNodeId).data());
    if (fill->fillKind != GCFillKind::FunctionClosure ||
        draft.normInputsOf(fillNodeId).size() != 1) {
        return false;
    }
    DraftStaticValue source = tryResolveStaticValue(draft, draft.normInputsOf(fillNodeId)[0]);
    if (!source.valid() || !source.type ||
        source.type->code() != camel::core::type::TypeCode::Function) {
        return false;
    }
    ::Function *function = camel::core::rtdata::fromSlot<::Function *>(source.value);
    GCGraph *base        = function ? function->graph() : nullptr;
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
    GCFuncRef funcBody{.ref = draft.payloadGraphIndexOf(lifted)};
    std::array<std::byte, sizeof(GCFuncRef)> payload{};
    std::memcpy(payload.data(), &funcBody, sizeof(funcBody));
    init.kind                       = GCNodeKind::Func;
    init.payload                    = std::span<const std::byte>(payload.data(), payload.size());
    init.normInputs                 = normInputs;
    init.withInputs                 = directWith;
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
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t callNodeId,
    bool unwrapGatedCallee) {
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

    gc_node_ref_t calleeRef = withInputs.front();
    if (unwrapGatedCallee) {
        // A gate orders the callee value without changing it (sync code gates a callee after
        // the previous statement); look through it. keepCalleeOrdering keeps the gate as a
        // control input of the rewritten FUNC, so statement ordering is preserved.
        while (true) {
            const DraftNodeHeader *gateHeader = draft.header(calleeRef);
            if (!gateHeader || gateHeader->kind != GCNodeKind::Gate) {
                break;
            }
            const auto gateNorm = draft.normInputsOf(calleeRef);
            if (gateNorm.empty()) {
                break;
            }
            calleeRef = gateNorm.back();
        }
    }

    const gc_node_ref_t callee = resolveTupleProjection(draft, calleeRef);
    if (const auto *calleeHeader = draft.header(callee);
        calleeHeader && calleeHeader->kind == GCNodeKind::Fill) {
        return devirtualizeClosureCallInDraft(session, draft, callNodeId, callee);
    }

    DraftStaticValue calleeStatic = tryResolveStaticValue(draft, calleeRef);
    if (!calleeStatic.valid() || !calleeStatic.type ||
        calleeStatic.type->code() != camel::core::type::TypeCode::Function) {
        CAMEL_LOG_INFO_S(
            "DraftOpt",
            "Devirtualize CALL node {} skipped: callee is not a static Function.",
            callNodeId);
        return false;
    }

    ::Function *function = camel::core::rtdata::fromSlot<::Function *>(calleeStatic.value);
    if (!function || !function->graph()) {
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
    GCFuncRef funcBody{.ref = draft.payloadGraphIndexOf(targetGraph)};
    std::array<std::byte, sizeof(GCFuncRef)> newPayload{};
    std::memcpy(newPayload.data(), &funcBody, sizeof(funcBody));
    init.kind    = GCNodeKind::Func;
    init.payload = std::span<const std::byte>(newPayload.data(), newPayload.size());
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

namespace {

/// True when `to` is reachable from `from` along value or control edges (to runs no earlier).
/// Local mirror of the generic passes' reaches(); the runtime layer cannot pull in passes helpers.
bool draftReaches(const GraphDraft &draft, gc_node_ref_t from, gc_node_ref_t to) {
    std::vector<bool> seen(draft.nodeSlotCount(), false);
    std::vector<gc_node_ref_t> work{from};
    while (!work.empty()) {
        const gc_node_ref_t n = work.back();
        work.pop_back();
        if (n == to) {
            return true;
        }
        if (n >= seen.size() || seen[n] || !draft.header(n)) {
            continue;
        }
        seen[n] = true;
        for (auto users : {draft.normUsersOf(n), draft.withUsersOf(n), draft.ctrlUsersOf(n)}) {
            work.insert(work.end(), users.begin(), users.end());
        }
    }
    return false;
}

/// The graph a per-arm call of `elem` runs when the arm tuple's callee element is statically
/// known: a closure FILL in this graph (every capture filled here), or a static Function
/// carrier. nullptr otherwise.
GCGraph *resolveBranchedArmCalleeGraph(const GraphDraft &draft, gc_node_ref_t elem) {
    const DraftNodeHeader *header = draft.header(elem);
    if (!header) {
        return nullptr;
    }
    gc_node_ref_t carrier = elem;
    if (header->kind == GCNodeKind::Fill) {
        const auto *fill = reinterpret_cast<const GCFillBody *>(draft.payloadOf(elem).data());
        const auto norm  = draft.normInputsOf(elem);
        if (fill->fillKind != GCFillKind::FunctionClosure || norm.size() != 1) {
            return nullptr;
        }
        carrier = norm.front();
    }
    DraftStaticValue fn = tryResolveStaticValue(draft, carrier);
    if (!fn.valid() || !fn.type || fn.type->code() != camel::core::type::TypeCode::Function) {
        return nullptr;
    }
    ::Function *function = camel::core::rtdata::fromSlot<::Function *>(fn.value);
    GCGraph *base        = function ? function->graph() : nullptr;
    if (!base) {
        return nullptr;
    }
    if (carrier != elem) {
        // devirtualizeClosureCallInDraft requires all captures filled here (none in the template).
        const auto *fill = reinterpret_cast<const GCFillBody *>(draft.payloadOf(elem).data());
        if (base->closureNodes().size() != draft.withInputsOf(elem).size() ||
            fill->slots().size() != draft.withInputsOf(elem).size()) {
            return nullptr;
        }
    }
    return base;
}

/// The element of the arm tuple FILL `fillId` written to tuple slot `slot`, or kInvalidNodeRef
/// when the fill leaves that slot to the template.
gc_node_ref_t armFillElement(const GraphDraft &draft, gc_node_ref_t fillId, size_t slot) {
    const auto *fill  = reinterpret_cast<const GCFillBody *>(draft.payloadOf(fillId).data());
    const auto slots  = fill->slots();
    const auto values = draft.withInputsOf(fillId);
    for (size_t k = 0; k < slots.size() && k < values.size(); ++k) {
        if (static_cast<size_t>(slots[k]) == slot) {
            return values[k];
        }
    }
    return kInvalidNodeRef;
}

} // namespace

/// A CALL whose callee is projected out of a branch JOIN (the with_grad pullback shape:
/// each arm packs (value, pullback closure) into a tuple, the JOIN selects, and the pullback is
/// called indirectly) is split across the branch. For every arm a new CALL invokes that arm's
/// closure with the original arguments; the call's result projections and the JOIN's other
/// projections each get a JOIN of their own over the per-arm values. The arm fills, the old
/// tuple JOIN, and the original CALL die (dce reclaims the fills). The new per-arm calls are
/// ordinary indirect calls: the rewrite loop's own devirtualization and inlining absorb them.
///
/// Placement discipline (the VMs execute an arm as the topo range head..tail): each per-arm
/// chain ends at the projection feeding the main new JOIN, which becomes the arm's tail anchor
/// and ctrl-waits on the arm's other new nodes and the arm's head, so the whole chain stays
/// inside the arm. The extra JOINs ctrl-wait on the main one so the topo builder places them
/// after it (they read values already computed inside the arms).
///
/// The CALL's arguments may themselves be computed after the JOIN (a cotangent derived from the
/// branch's value projection is the common case): the per-arm calls then read a value that runs
/// later than the branch their chain belongs to. Arguments produced strictly inside an arm are
/// rejected instead (see the inArm walk below).
///
/// EXPORT-ONLY: the result has several sibling JOINs reading the same BRCH whose users merge
/// downstream, and per-arm chains may read post-JOIN values. That is a valid graph for a
/// value-DAG consumer (the ONNX exporter, which emits an If whose arm subgraphs capture
/// outer-scope names) but is not executable: a JOIN no longer cuts its BRCH off from what runs
/// after it, and the execution-topo builder reports a cycle. Gate on
/// OptimizeRewriteConfig::enableBranchedCallSplit.
bool devirtualizeBranchedCallInDraft(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t callNodeId) {
    (void)session;

    const DraftNodeHeader *callHeader = draft.header(callNodeId);
    if (!callHeader || callHeader->kind != GCNodeKind::Call) {
        return false;
    }
    if (callNodeId == draft.exitNode() || callNodeId == draft.outputNode() ||
        callNodeId == draft.returnNode()) {
        return false;
    }
    const auto callWith = draft.withInputsOf(callNodeId);
    if (callWith.empty()) {
        return false;
    }
    const gc_node_ref_t calleeInput = callWith.front();
    gc_node_ref_t calleeAccs        = calleeInput;
    // Sync code gates the projected callee after the previous statement; the gate orders the
    // value without changing it, so the split sees through it. The gate dies with the call.
    if (const DraftNodeHeader *gateHeader = draft.header(calleeAccs);
        gateHeader && gateHeader->kind == GCNodeKind::Gate &&
        !draft.normInputsOf(calleeAccs).empty()) {
        calleeAccs = draft.normInputsOf(calleeAccs).back();
    }
    const DraftNodeHeader *accsHeader = draft.header(calleeAccs);
    if (!accsHeader || accsHeader->kind != GCNodeKind::Accs ||
        draft.normInputsOf(calleeAccs).size() != 1 || !draft.ctrlInputsOf(calleeAccs).empty() ||
        !draft.ctrlUsersOf(calleeAccs).empty()) {
        return false;
    }
    const auto *accsBody = reinterpret_cast<const GCAccsBody *>(draft.payloadOf(calleeAccs).data());
    if (accsBody->accsKind != GCAccsKind::TupleIndex) {
        return false;
    }
    const size_t calleeSlot           = accsBody->value;
    const gc_node_ref_t join          = draft.normInputsOf(calleeAccs).front();
    const DraftNodeHeader *joinHeader = draft.header(join);
    if (!joinHeader || joinHeader->kind != GCNodeKind::Join ||
        draft.normInputsOf(join).size() != 1 || join == draft.exitNode() ||
        join == draft.outputNode() || join == draft.returnNode() ||
        !draft.ctrlInputsOf(join).empty()) {
        return false;
    }
    // The callee projection feeds this call alone (through the gate when one wraps it). A
    // shared projection (e.g. also captured into a continuation tuple) is left alone: keeping
    // the JOIN alive for it while splitting the call duplicates the continuation structure and
    // grows the graph past the encoding limit.
    for (auto users : {draft.normUsersOf(calleeAccs), draft.withUsersOf(calleeAccs)}) {
        for (gc_node_ref_t user : users) {
            if (user != calleeInput && user != callNodeId) {
                return false;
            }
        }
    }
    if (calleeInput != calleeAccs) {
        for (auto users : {draft.normUsersOf(calleeInput), draft.withUsersOf(calleeInput)}) {
            for (gc_node_ref_t user : users) {
                if (user != callNodeId) {
                    return false;
                }
            }
        }
    }
    const gc_node_ref_t brch          = draft.normInputsOf(join).front();
    const DraftNodeHeader *brchHeader = draft.header(brch);
    if (!brchHeader || brchHeader->kind != GCNodeKind::Brch) {
        return false;
    }
    const auto *brchPayload =
        reinterpret_cast<const DraftBrchPayload *>(draft.payloadOf(brch).data());
    if (brchPayload->join != join) {
        return false;
    }
    const auto arms       = draft.withInputsOf(join);
    const auto armAnchors = draft.branchArmsOf(brch);
    if (arms.empty() || armAnchors.size() != arms.size()) {
        return false;
    }

    // Every value user of the JOIN projects one of its slots; collect them by slot. A
    // projection that another call uses as its callee would be turned into a value JOIN here,
    // stranding that call, so that shape is left alone. Nodes an outer branch anchors on are
    // left alone too: erasing them would dangle that branch's arms.
    std::map<size_t, std::vector<gc_node_ref_t>> joinProjections;
    for (auto users : {draft.normUsersOf(join), draft.withUsersOf(join)}) {
        for (gc_node_ref_t user : users) {
            // The projections may carry control edges (sync code orders reads after the JOIN
            // or the call); the split retargets those waits onto the replacement JOINs. Nodes
            // an outer branch anchors on are still left alone: erasing them would dangle that
            // branch's arms.
            const DraftNodeHeader *userHeader = draft.header(user);
            if (!userHeader || userHeader->kind != GCNodeKind::Accs ||
                draft.normInputsOf(user).size() != 1 || draft.normInputsOf(user).front() != join ||
                draft.isBranchArmAnchor(user) || user == draft.exitNode() ||
                user == draft.outputNode() || user == draft.returnNode()) {
                return false;
            }
            const auto *body = reinterpret_cast<const GCAccsBody *>(draft.payloadOf(user).data());
            if (body->accsKind != GCAccsKind::TupleIndex) {
                return false;
            }
            if (user != calleeAccs) {
                for (gc_node_ref_t withUser : draft.withUsersOf(user)) {
                    const DraftNodeHeader *withUserHeader = draft.header(withUser);
                    if (withUserHeader && withUserHeader->kind == GCNodeKind::Call &&
                        !draft.withInputsOf(withUser).empty() &&
                        draft.withInputsOf(withUser).front() == user) {
                        return false;
                    }
                }
            }
            joinProjections[body->value].push_back(user);
        }
    }
    const auto calleeProjIt = joinProjections.find(calleeSlot);
    if (calleeProjIt == joinProjections.end() || calleeProjIt->second.size() != 1) {
        return false;
    }
    // Every value user of the CALL projects one of its result slots (control edges allowed, as
    // above).
    std::map<size_t, std::vector<gc_node_ref_t>> callProjections;
    for (auto users : {draft.normUsersOf(callNodeId), draft.withUsersOf(callNodeId)}) {
        for (gc_node_ref_t user : users) {
            const DraftNodeHeader *userHeader = draft.header(user);
            if (!userHeader || userHeader->kind != GCNodeKind::Accs ||
                draft.normInputsOf(user).size() != 1 ||
                draft.normInputsOf(user).front() != callNodeId) {
                return false;
            }
            const auto *body = reinterpret_cast<const GCAccsBody *>(draft.payloadOf(user).data());
            if (body->accsKind != GCAccsKind::TupleIndex) {
                return false;
            }
            callProjections[body->value].push_back(user);
        }
    }
    if (callProjections.empty()) {
        return false;
    }

    // Arms are tuple FILLs read only by the JOIN, anchored as the arm tails; the callee slot
    // and every projected slot must be filled explicitly in every arm.
    const size_t armCount = arms.size();
    std::vector<gc_node_ref_t> armCallees(armCount, kInvalidNodeRef);
    std::map<size_t, std::vector<gc_node_ref_t>> armValues; // slot -> per-arm element
    for (size_t i = 0; i < armCount; ++i) {
        const gc_node_ref_t armFill       = arms[i];
        const DraftNodeHeader *fillHeader = draft.header(armFill);
        if (!fillHeader || fillHeader->kind != GCNodeKind::Fill || armAnchors[i].tail != armFill ||
            !draft.ctrlUsersOf(armFill).empty()) {
            return false;
        }
        const auto *fill = reinterpret_cast<const GCFillBody *>(draft.payloadOf(armFill).data());
        if (fill->fillKind != GCFillKind::Tuple) {
            return false;
        }
        for (auto users : {draft.normUsersOf(armFill), draft.withUsersOf(armFill)}) {
            for (gc_node_ref_t user : users) {
                if (user != join) {
                    return false;
                }
            }
        }
        armCallees[i] = armFillElement(draft, armFill, calleeSlot);
        if (armCallees[i] == kInvalidNodeRef) {
            return false;
        }
        for (const auto &[slot, users] : joinProjections) {
            if (slot == calleeSlot) {
                continue;
            }
            const gc_node_ref_t elem = armFillElement(draft, armFill, slot);
            if (elem == kInvalidNodeRef) {
                return false;
            }
            armValues[slot].push_back(elem);
        }
    }
    // Each arm's callee must devirtualize: a static Function, or a closure FILL of one, with a
    // matching call arity (the same checks devirtualizeStaticCallInDraft performs).
    const auto callNorm = draft.normInputsOf(callNodeId);
    for (gc_node_ref_t callee : armCallees) {
        GCGraph *target = resolveBranchedArmCalleeGraph(draft, callee);
        if (!target || target->withPorts().size() != callWith.size() - 1 ||
            target->normPorts().size() != callNorm.size()) {
            return false;
        }
    }

    // Nodes running inside the branch (arms and everything after). Control predecessors of the
    // CALL must live outside it: a per-arm call ctrl-waiting on an in-branch node would order it
    // against work another arm owns.
    std::vector<bool> inBranch(draft.nodeSlotCount(), false);
    {
        std::vector<gc_node_ref_t> work;
        for (auto users :
             {draft.normUsersOf(brch), draft.withUsersOf(brch), draft.ctrlUsersOf(brch)}) {
            work.insert(work.end(), users.begin(), users.end());
        }
        while (!work.empty()) {
            const gc_node_ref_t n = work.back();
            work.pop_back();
            if (n >= inBranch.size() || inBranch[n] || !draft.header(n)) {
                continue;
            }
            inBranch[n] = true;
            for (auto users : {draft.normUsersOf(n), draft.withUsersOf(n), draft.ctrlUsersOf(n)}) {
                work.insert(work.end(), users.begin(), users.end());
            }
        }
    }
    const auto outsideBranch = [&](gc_node_ref_t id) {
        return id < inBranch.size() && !inBranch[id] && draft.header(id);
    };
    // Arguments need a weaker rule than control predecessors. The split graph is only ever
    // walked as a value DAG (export-only, see enableBranchedCallSplit), so a per-arm call may
    // read anything the original CALL read that is not owned by an arm — including a value
    // computed after the JOIN, such as a cotangent derived from the branch's value projection:
    // the exporter emits it ahead of the If and the arm subgraphs reference it as an
    // outer-scope name. An argument produced strictly inside an arm (reachable from the BRCH
    // without passing through the JOIN) is still rejected: its producer exists only under that
    // arm's condition, and the split would reference it from the sibling arm as well.
    std::vector<bool> inArm(draft.nodeSlotCount(), false);
    {
        std::vector<gc_node_ref_t> work;
        for (auto users :
             {draft.normUsersOf(brch), draft.withUsersOf(brch), draft.ctrlUsersOf(brch)}) {
            for (gc_node_ref_t user : users) {
                if (user != join) {
                    work.push_back(user);
                }
            }
        }
        while (!work.empty()) {
            const gc_node_ref_t n = work.back();
            work.pop_back();
            if (n >= inArm.size() || inArm[n] || !draft.header(n) || n == join) {
                continue;
            }
            inArm[n] = true;
            for (auto users : {draft.normUsersOf(n), draft.withUsersOf(n), draft.ctrlUsersOf(n)}) {
                work.insert(work.end(), users.begin(), users.end());
            }
        }
    }
    const auto insideArm = [&](gc_node_ref_t id) { return id < inArm.size() && inArm[id]; };
    for (gc_node_ref_t arg : callNorm) {
        if (insideArm(arg)) {
            return false;
        }
    }
    for (size_t i = 1; i < callWith.size(); ++i) {
        if (insideArm(callWith[i])) {
            return false;
        }
    }
    std::vector<gc_node_ref_t> keptCtrl;
    for (gc_node_ref_t pred : draft.ctrlInputsOf(callNodeId)) {
        if (outsideBranch(pred) &&
            std::find(keptCtrl.begin(), keptCtrl.end(), pred) == keptCtrl.end()) {
            keptCtrl.push_back(pred);
        }
    }

    // ---- rewrite ----

    const uint64_t origin  = draft.nodeOrigin(callNodeId);
    const auto callPayload = draft.payloadOf(callNodeId);
    GCCallBody callBody{};
    if (callPayload.size_bytes() >= sizeof(GCCallBody)) {
        std::memcpy(&callBody, callPayload.data(), sizeof(GCCallBody));
    }
    const size_t mainSlot = callProjections.begin()->first;

    // Per arm: a call of the arm's callee, then one projection per used result slot. The
    // projection feeding the main JOIN closes the arm (tail anchor, ctrl-waiting on the arm's
    // other new nodes, the arm's other projected values, and the arm's head).
    std::vector<gc_node_ref_t> mainArms(armCount, kInvalidNodeRef);
    std::map<size_t, std::vector<gc_node_ref_t>> callProjArms; // result slot -> per-arm ACCS
    for (size_t i = 0; i < armCount; ++i) {
        const gc_node_ref_t armCall = draft.addCallNode(callHeader->dataType, callBody);
        std::vector<gc_node_ref_t> withInputs{armCallees[i]};
        withInputs.insert(withInputs.end(), callWith.begin() + 1, callWith.end());
        draft.setNormInputs(armCall, callNorm);
        draft.setWithInputs(armCall, withInputs);
        draft.setCtrlInputs(armCall, keptCtrl);
        draft.setNodeOrigin(armCall, origin);
        for (const auto &[slot, users] : callProjections) {
            camel::core::type::Type *projType = draft.header(users.front())->dataType;
            const gc_node_ref_t proj = draft.addAccsNode(projType, static_cast<uint32_t>(slot));
            draft.setNormInputs(proj, std::span<const gc_node_ref_t>(&armCall, 1));
            draft.setNodeOrigin(proj, origin);
            callProjArms[slot].push_back(proj);
        }
        std::vector<gc_node_ref_t> armCtrl;
        const gc_node_ref_t armTail = callProjArms[mainSlot].back();
        for (const auto &[slot, projs] : callProjArms) {
            if (slot != mainSlot) {
                armCtrl.push_back(projs.back());
            }
        }
        for (const auto &[slot, elems] : armValues) {
            if (elems[i] != armTail &&
                std::find(armCtrl.begin(), armCtrl.end(), elems[i]) == armCtrl.end()) {
                armCtrl.push_back(elems[i]);
            }
        }
        const gc_node_ref_t head = armAnchors[i].head;
        if (head != kInvalidNodeRef && head != armTail &&
            std::find(armCtrl.begin(), armCtrl.end(), head) == armCtrl.end() &&
            !draftReaches(draft, armTail, head)) {
            armCtrl.push_back(head);
        }
        draft.setCtrlInputs(armTail, armCtrl);
        mainArms[i] = armTail;
    }

    camel::core::type::Type *mainType = draft.header(callProjections[mainSlot].front())->dataType;
    const gc_node_ref_t mainJoin =
        draft.addJoinNode(mainType, brch, static_cast<gc_cnt_t>(armCount));
    draft.setNormInputs(mainJoin, std::span<const gc_node_ref_t>(&brch, 1));
    draft.setWithInputs(mainJoin, mainArms);
    draft.setNodeOrigin(mainJoin, origin);

    // The remaining projections each get a JOIN over their per-arm values. They ctrl-wait on
    // the main JOIN so the topo builder places them after it: their arm values are computed
    // inside the arms, and only the main JOIN closes the branch for the schedulers.
    const auto replaceProjectionUsers = [&](std::span<const gc_node_ref_t> users,
                                            gc_node_ref_t replacement) {
        for (gc_node_ref_t user : users) {
            draft.replaceAllValueUses(user, replacement);
            draft.replaceAllCtrlUses(user, replacement);
            draft.eraseNode(user);
        }
    };
    for (const auto &[slot, users] : callProjections) {
        if (slot == mainSlot) {
            continue;
        }
        camel::core::type::Type *projType = draft.header(users.front())->dataType;
        const gc_node_ref_t projJoin =
            draft.addJoinNode(projType, brch, static_cast<gc_cnt_t>(armCount));
        draft.setNormInputs(projJoin, std::span<const gc_node_ref_t>(&brch, 1));
        draft.setWithInputs(projJoin, callProjArms[slot]);
        draft.setCtrlInputs(projJoin, std::span<const gc_node_ref_t>(&mainJoin, 1));
        draft.setNodeOrigin(projJoin, origin);
        replaceProjectionUsers(users, projJoin);
    }
    for (const auto &[slot, users] : joinProjections) {
        if (slot == calleeSlot) {
            continue;
        }
        camel::core::type::Type *projType = draft.header(users.front())->dataType;
        const gc_node_ref_t projJoin =
            draft.addJoinNode(projType, brch, static_cast<gc_cnt_t>(armCount));
        draft.setNormInputs(projJoin, std::span<const gc_node_ref_t>(&brch, 1));
        draft.setWithInputs(projJoin, armValues[slot]);
        draft.setCtrlInputs(projJoin, std::span<const gc_node_ref_t>(&mainJoin, 1));
        draft.setNodeOrigin(projJoin, origin);
        replaceProjectionUsers(users, projJoin);
    }
    replaceProjectionUsers(callProjections[mainSlot], mainJoin);

    // The old call is gone; its control users (and the old JOIN's) now wait on the main JOIN,
    // which completes after the taken arm's call. This also retargets the BRCH's join payload.
    draft.replaceAllCtrlUses(callNodeId, mainJoin);
    draft.eraseNode(callNodeId);
    if (calleeInput != calleeAccs) {
        draft.eraseNode(calleeInput);
    }
    draft.eraseNode(calleeAccs);
    draft.replaceAllCtrlUses(join, mainJoin);
    draft.eraseNode(join);
    for (size_t i = 0; i < armCount; ++i) {
        draft.retargetBranchArmAnchors(arms[i], mainArms[i], mainArms[i]);
    }
    CAMEL_LOG_INFO_S(
        "DraftOpt",
        "Devirtualized branched CALL node {}: {} arm call(s) under BRCH {}, main JOIN {}.",
        callNodeId,
        armCount,
        brch,
        mainJoin);
    return true;
}

namespace {

/// `id` and everything it runs after (its transitive inputs of every kind).
std::vector<bool> draftUpstream(const GraphDraft &draft, gc_node_ref_t id) {
    std::vector<bool> seen(draft.nodeSlotCount(), false);
    std::vector<gc_node_ref_t> work{id};
    while (!work.empty()) {
        const gc_node_ref_t n = work.back();
        work.pop_back();
        if (n >= seen.size() || seen[n] || !draft.header(n)) {
            continue;
        }
        seen[n] = true;
        for (auto inputs : {draft.normInputsOf(n), draft.withInputsOf(n), draft.ctrlInputsOf(n)}) {
            work.insert(work.end(), inputs.begin(), inputs.end());
        }
    }
    return seen;
}

/**
 * The arguments of call `callId` that alone keep it inside a branch arm.
 *
 * The schedulers run, when an arm is taken, the nodes that run after the arm's head and not
 * after the JOIN, and everything else of the graph unconditionally, before the BRCH. A call is
 * placed by its inputs: it is inside an arm when one of its control predecessors or one of its
 * arguments is. The nodes of the callee's body are placed by their own inputs, and a node of
 * the body that reads no parameter (a branch on a constant, a call without arguments, an
 * effect) has none in the caller. The control predecessors of the call are handed to the
 * body's entry roots; an argument is not. So where the call was inside an arm through an
 * argument only, such a node left the arm. It then ran when the arm was not taken (a fault
 * there ended a program that never reached that code), or did not run when it was (an effect
 * that waits on the BRCH but not on the arm's head is not part of what the arm runs), and a
 * BRCH could leave while its JOIN stayed inside, which no scheduler can lay out.
 *
 * Returned are the arguments needed to cover every arm the call is in and that its control
 * predecessors do not already place it in. The splice makes the body wait on them, as the call
 * itself did.
 */
std::vector<gc_node_ref_t> armPlacingArguments(const GraphDraft &draft, gc_node_ref_t callId) {
    const std::vector<bool> before = draftUpstream(draft, callId);
    std::vector<gc_node_ref_t> open; // the heads of the arms the call is in
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        const DraftNodeHeader *h = draft.header(id);
        if (!before[id] || !h || h->kind != GCNodeKind::Brch) {
            continue;
        }
        const gc_node_ref_t join =
            reinterpret_cast<const DraftBrchPayload *>(draft.payloadOf(id).data())->join;
        if (join != kInvalidNodeRef && join < before.size() && before[join]) {
            continue; // the call runs after the whole branch
        }
        for (const GCBranchArm &arm : draft.branchArmsOf(id)) {
            // The call that is itself the head stays the head: the splice moves the anchor.
            if (arm.head != kInvalidNodeRef && arm.head != callId && arm.head < before.size() &&
                before[arm.head]) {
                open.push_back(arm.head);
            }
        }
    }
    std::vector<gc_node_ref_t> placing;
    if (open.empty()) {
        return placing;
    }
    const auto cover = [&](gc_node_ref_t input) {
        const std::vector<bool> reached = draftUpstream(draft, input);
        return std::erase_if(open, [&](gc_node_ref_t head) { return reached[head]; }) > 0;
    };
    for (gc_node_ref_t pred : draft.ctrlInputsOf(callId)) {
        (void)cover(pred);
    }
    for (auto inputs : {draft.normInputsOf(callId), draft.withInputsOf(callId)}) {
        for (gc_node_ref_t input : inputs) {
            if (open.empty()) {
                return placing;
            }
            if (cover(input) && std::ranges::find(placing, input) == placing.end()) {
                placing.push_back(input);
            }
        }
    }
    return placing;
}

} // namespace

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

    GCGraph *const calleeGraph = draft.calleeGraphOf(funcNodeId);
    if (!calleeGraph) {
        CAMEL_LOG_INFO_S(
            "DraftInline",
            "Inline splice node {} rejected: null callee graph.",
            funcNodeId);
        return result;
    }

    const GraphDraft *calleeDraftView = std::as_const(session).tryDraft(calleeGraph);
    const std::vector<gc_node_ref_t> actualInputs = orderedFuncActualInputs(draft, funcNodeId);
    const auto formalNodes = calleeDraftView ? orderedDirectCallFormalNodes(*calleeDraftView)
                                             : orderedDirectCallFormalNodes(calleeGraph);
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
        calleeDraftView ? calleeDraftView->entryNode() : calleeGraph->entryNodeRef();
    const gc_node_ref_t calleeOutput =
        calleeDraftView ? calleeDraftView->outputNode() : calleeGraph->outputNodeRef();
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
        static_cast<void *>(calleeGraph));

    // What the body has to wait on: the control predecessors of the call, and the arguments
    // that place the call inside a branch arm (see armPlacingArguments).
    std::vector<gc_node_ref_t> ctrlPreds(
        draft.ctrlInputsOf(funcNodeId).begin(),
        draft.ctrlInputsOf(funcNodeId).end());
    for (gc_node_ref_t argument : armPlacingArguments(draft, funcNodeId)) {
        if (std::find(ctrlPreds.begin(), ctrlPreds.end(), argument) == ctrlPreds.end()) {
            ctrlPreds.push_back(argument);
        }
    }
    ctrlPreds                  = controlFrontier(draft, ctrlPreds);
    const bool hasExternalCtrl = !ctrlPreds.empty();

    DraftGraphCloneResult cloned = calleeDraftView
                                       ? cloneDraftGraphIntoDraft(draft, *calleeDraftView)
                                       : cloneRuntimeGraphIntoDraft(draft, calleeGraph);
    CAMEL_LOG_INFO_S(
        "DraftInline",
        "Inline splice node {} cloned {} nodes from callee.",
        funcNodeId,
        cloned.clonedNodes.size());
    const std::vector<gc_node_ref_t> entryRoots =
        calleeDraftView ? collectExecutableEntryRoots(*calleeDraftView, cloned.sourceToCloned)
                        : collectExecutableEntryRoots(calleeGraph, cloned.sourceToCloned);
    CAMEL_LOG_INFO_S(
        "DraftInline",
        "Inline splice node {} collected {} executable entry roots.",
        funcNodeId,
        entryRoots.size());

    const auto clonedFormalNodes =
        calleeDraftView ? orderedClonedDirectCallFormalNodes(cloned, *calleeDraftView)
                        : orderedClonedDirectCallFormalNodes(cloned, calleeGraph);
    ASSERT(
        clonedFormalNodes.size() == formalNodes.size(),
        "Runtime inline formal-node planning diverged between source and cloned graphs.");

    // With control predecessors, the body reads each argument through a GATE that waits on
    // them. The gates order nothing the entry roots below do not order already (every
    // executable node of the body runs after a root); they are kept because consumers that
    // read the graph as a value DAG (the ONNX exporter) take a gated constant for "this value
    // exists only once the branch or the statement before it has run".
    std::vector<gc_node_ref_t> boundActualInputs = actualInputs;
    std::vector<gc_node_ref_t> parameterGates;
    if (hasExternalCtrl) {
        parameterGates.reserve(actualInputs.size());
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
            parameterGates.push_back(gateId);
            boundActualInputs[i] = gateId;
        }
    }

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

    // The entry roots take over the call's predecessors: directly when there is one root,
    // through a SYNC when there are several. The roots are all there is to order: the
    // parameter gates and the callee's recorded entry node are not entry points. (They used to
    // stand in when a body had no root; nothing of such a body reads them, so the
    // predecessors ended at nodes nothing followed.)
    std::vector<gc_node_ref_t> gateCtrl = ctrlPreds;
    if (entryRoots.size() == 1) {
        result.ctrlEntry = entryRoots.front();
        const std::vector<gc_node_ref_t> mergedCtrlInputs =
            appendUniqueRefs(draft.ctrlInputsOf(result.ctrlEntry), ctrlPreds);
        draft.setCtrlInputs(result.ctrlEntry, mergedCtrlInputs);
    } else if (entryRoots.size() > 1) {
        DraftNodeInit syncInit{
            .dataIndex    = 0,
            .dataType     = nullptr,
            .kind         = GCNodeKind::Sync,
            .runtimeFlags = 0,
            .ctrlInputs   = ctrlPreds,
        };
        result.ctrlEntry = draft.addNode(syncInit);
        gateCtrl         = {result.ctrlEntry};
        for (gc_node_ref_t targetId : entryRoots) {
            const std::vector<gc_node_ref_t> mergedCtrlInputs = appendUniqueRefs(
                draft.ctrlInputsOf(targetId),
                std::span<const gc_node_ref_t>(&result.ctrlEntry, 1));
            draft.setCtrlInputs(targetId, mergedCtrlInputs);
        }
    }
    // A gate waits on the predecessors themselves, or on the SYNC that stands for them; never
    // on an entry root, which may read the gate.
    for (gc_node_ref_t gateId : parameterGates) {
        draft.setCtrlInputs(gateId, gateCtrl);
    }

    result.valueExit = resolveInlineValueExit(
        draft,
        cloned,
        calleeGraph,
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
                calleeGraph ? calleeGraph->name() : "<null>"));
    }
    result.ctrlExit = resolveInlineCtrlExit(
        draft,
        cloned,
        calleeGraph,
        calleeDraftView,
        boundActualInputs);
    if (result.ctrlExit == kInvalidNodeRef) {
        result.ctrlExit = result.valueExit;
    }
    const bool resultIsGatedArgument =
        std::find(parameterGates.begin(), parameterGates.end(), result.valueExit) !=
            parameterGates.end() &&
        result.ctrlExit == result.valueExit;
    if (hasExternalCtrl && entryRoots.empty() && resultIsGatedArgument) {
        // The body forwards an argument: its gate already waits on the predecessors.
        result.ctrlEntry = result.valueExit;
    } else if (hasExternalCtrl && entryRoots.empty()) {
        // The body executes nothing: its result is a constant or one of the arguments. The
        // call still ran after its predecessors, and what waited on the call, or read its
        // value, ran after them too. A GATE on the result carries that. Without it the result
        // waits on nothing: where the call was the tail of a branch arm (placed in the arm by
        // the BRCH, or by an argument computed there), the tail became a value outside the
        // branch, the arm was taken to have no code, and what the call's arguments ran in the
        // arm belonged to no arm any more.
        const DraftNodeHeader *valueHeader        = draft.header(result.valueExit);
        std::vector<gc_node_ref_t> gateCtrlInputs = ctrlPreds;
        if (result.ctrlExit != result.valueExit) {
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
    } else if (result.ctrlExit != result.valueExit) {
        const auto *ctrlHeader = draft.header(result.ctrlExit);
        const auto ctrlValue   = draft.normInputsOf(result.ctrlExit);
        if (ctrlHeader && ctrlHeader->kind == GCNodeKind::Gate && ctrlValue.size() == 1 &&
            ctrlValue.front() == result.valueExit) {
            // The callee's completion gate already forwards the return value after all of
            // its effects. Reuse it instead of building GATE(value, ctrl=GATE(value, ...)).
            // Do not fold gates forwarding a different value, or the empty-body case above:
            // that case still has to attach the caller's control predecessors.
            result.valueExit = result.ctrlExit;
        } else {
            const DraftNodeHeader *valueHeader = draft.header(result.valueExit);
            DraftNodeInit gateInit{
                .dataIndex = valueHeader ? valueHeader->dataIndex : static_cast<gc_slot_idx_t>(0),
                .dataType  = valueHeader ? valueHeader->dataType : nullptr,
                .kind      = GCNodeKind::Gate,
                .runtimeFlags = 0,
                .normInputs   = std::span<const gc_node_ref_t>(&result.valueExit, 1),
                .ctrlInputs   = std::span<const gc_node_ref_t>(&result.ctrlExit, 1),
            };
            const gc_node_ref_t bridgeGate = draft.addNode(gateInit);
            result.valueExit               = bridgeGate;
            result.ctrlExit                = bridgeGate;
        }
    }
    if (result.ctrlEntry == kInvalidNodeRef) {
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
