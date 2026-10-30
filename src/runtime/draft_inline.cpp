// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

#include "camel/core/operator_table.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/text.h"
#include "camel/core/type/typevar.h"
#include "camel/runtime/draft_clone.h"
#include "camel/runtime/draft_types.h"
#include "camel/runtime/draft_writes.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <map>
#include <stdexcept>
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

/// The parameter type an instance of a declaration gets from one argument, as design principles
/// 5.5 states it: the declaration with its type variables substituted, and with every extent the
/// declaration leaves undetermined kept as declared. An extent that is not a constant asks
/// nothing at a call (5.4), so an argument's exact length is a property of the value and never
/// part of the instance — which is what lets a recursion that writes `int[]`, `T[?]` or a
/// `Tensor` reach a fixed point instead of taking a new copy at every level.
///
/// A dimension name the declaration writes (`T[n]`) is bound from the argument. `relaxNames`
/// widens it to an unknown extent instead of binding it, which is how a recursion that carries
/// one name to a different value at every level (polymorphic recursion) stops making new
/// instances.
///
/// A non-array type is returned as `actual`: what a declaration says about a tuple, a struct or
/// a scalar is not a dimension, and refining those is what this helper must not change.
camel::core::type::Type *instanceParameterType(
    camel::core::type::Type *formal, camel::core::type::Type *actual, bool relaxNames) {
    using namespace camel::core::type;
    const auto *formalArray = asArrayType(formal);
    if (formalArray == nullptr || actual == nullptr) {
        return actual;
    }
    const auto *actualArray = asArrayType(actual);

    // The element position is where a declaration's type variable sits, and where the element
    // nobody wrote sits too (`[]`, whose element is `Void`).
    // The shape is not a type variable, whatever the argument's own shape is.
    Type *element = formalArray->elemType();
    if (!relaxNames && (element == nullptr || containsTypeVar(element) ||
                        element->code() == TypeCode::Void)) {
        if (actualArray != nullptr && actualArray->elemType() != nullptr &&
            actualArray->elemType()->code() != TypeCode::Void) {
            element = actualArray->elemType();
        }
    }

    std::vector<ExtentForm> extents(formalArray->extents().begin(), formalArray->extents().end());
    for (size_t i = 0; i < extents.size(); ++i) {
        if (!extents[i].hasNames()) {
            continue; // a constant, or an extent the declaration leaves open: not the instance's
        }
        if (relaxNames || actualArray == nullptr || i >= actualArray->extents().size() ||
            !actualArray->extents()[i].isConstant()) {
            // Nothing binds the name: the argument leaves that dimension open too, or this is a
            // lap past the first one and the name is widened instead of being bound.
            if (relaxNames) {
                extents[i] = ExtentForm::unknown();
            }
            continue;
        }
        extents[i] = actualArray->extents()[i];
    }

    bool anyRank   = formalArray->anyRank();
    bool packBound = false;
    if (anyRank && !relaxNames && !formalArray->packName().empty() && actualArray != nullptr &&
        !actualArray->anyRank() &&
        actualArray->extents().size() >= formalArray->extents().size()) {
        // `T[n, ..rest]`: the pack is a dimension name the declaration writes, and the argument
        // fixes it to the dimensions it had after the prefix. An open rank with no name (`T[..]`,
        // `Tensor`) states no dimension to bind: the argument's rank is a property of its value.
        extents.insert(
            extents.end(),
            actualArray->extents().begin() +
                static_cast<std::ptrdiff_t>(formalArray->extents().size()),
            actualArray->extents().end());
        anyRank   = false;
        packBound = true;
    }

    if (!packBound && element == formalArray->elemType() &&
        extents.size() == formalArray->extents().size() &&
        std::equal(extents.begin(), extents.end(), formalArray->extents().begin())) {
        return formal;
    }
    if (anyRank) {
        return ArrayType::openRank(element, extents);
    }
    if (extents.empty()) {
        return element;
    }
    return ArrayType::create(element, extents);
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
/// A GATE counts. A callee that was itself inlined into (earlier in the same session, or by an
/// earlier pass) starts with the entry its splice wrote, which is the first statement of the
/// body's code: its first operators wait on the predecessors the splice handed them, so they have
/// a non-constant input and are not roots themselves. Leaving them out made such a body have no
/// root at all. The control predecessors of the call (the BRCH whose arm the call is, the
/// statement before it) were then attached to the callee's recorded entry node only, and the code
/// behind that entry ran whether the arm was taken or not, and before the statements it was
/// written after.
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
        for (const camel::runtime::GCGraphSyncEdge &edge : sourceGraph->syncPredsOf(curr)) {
            stack.push_back(edge.pred);
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
        if (!hasNonDataPortInput) {
            for (const camel::runtime::GCGraphSyncEdge &edge : sourceGraph->syncPredsOf(sourceRef)) {
                const GCNode *inputNode = sourceGraph->node(edge.pred);
                if (inputNode && inputNode->kind != GCNodeKind::Data &&
                    inputNode->kind != GCNodeKind::Port) {
                    hasNonDataPortInput = true;
                    break;
                }
            }
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
        for (gc_node_ref_t in : sourceDraft.syncInputsOf(curr)) {
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
        if (!hasNonDataPortInput) {
            scanInputs(sourceDraft.syncInputsOf(sourceRef));
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
        .attrs        = header->attrs,
        .region       = header->region,
        .name         = header->name,
        .payload      = draft.payloadOf(nodeId),
        .normInputs   = draft.normInputsOf(nodeId),
        .withInputs   = draft.withInputsOf(nodeId),
        .ctrlInputs   = draft.ctrlInputsOf(nodeId),
        // The order and sync runs are part of what the node is (docs/design/effects/edges.cn.md 4):
        // a rewrite that replaces it and drops them loses where it stood -- the statement written
        // before it in its block's chain, and what ordered it -- and the chain it was part of dies
        // with it (a devirtualized call used to take the whole `sync` block's effects down).
        .orderInputs  = draft.orderInputsOf(nodeId),
        .syncInputs   = draft.syncInputsOf(nodeId),
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
            if (!source || !source->dataType ||
                !camel::core::type::sameCanonicalType(source->dataType, header->dataType)) {
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

/// The graph of `function`, which the session reads from now on (the function may have been
/// made in another program).
GCGraph *functionGraph(RuntimeGraphDraftSession &session, const ::Function *function) {
    GCGraph *graph = function ? function->graph() : nullptr;
    if (graph != nullptr) {
        session.adoptProgram(*function->program());
    }
    return graph;
}

/// True when `graph` can call itself again through direct calls (it lies on a call cycle).
bool onCallCycle(const Program &program, const GCGraph *graph) {
    std::unordered_set<const GCGraph *> seen;
    std::vector<const GCGraph *> work{graph};
    while (!work.empty()) {
        const GCGraph *g = work.back();
        work.pop_back();
        for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
            const GCGraph *callee = program.calleeOf(*g, it.ref());
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
        for (auto users : {draft.normUsersOf(brch),
                           draft.withUsersOf(brch),
                           draft.ctrlUsersOf(brch),
                           draft.syncUsersOf(brch)}) {
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
            for (auto users : {draft.normUsersOf(n),
                               draft.withUsersOf(n),
                               draft.ctrlUsersOf(n),
                               draft.syncUsersOf(n)}) {
                work.insert(work.end(), users.begin(), users.end());
            }
        }
    }
    return false;
}

std::vector<DraftFormalBinding> collectDirectFuncSpecializations(
    const GraphDraft &draft, gc_node_ref_t funcNodeId, const GCGraph *calleeGraph,
    bool functionValuesOnly) {
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

    // A call of a function that is already one of this function's type instances is the second
    // lap of a recursion: a dimension name it states binds no further value from there on.
    // The lap that unrolls a recursion is the recursive call *inside* an instance: it names the
    // function itself again with arguments of the instance's own types, and a new instance made
    // from there is one level of that recursion per rewriting pass. Both laps count as
    // past the first.
    const GraphId calleeRoot = calleeGraph->typeSpecializationRoot();
    const GraphId callerRoot = draft.typeSpecializationRoot();
    // Either lap is past the first: the callee is itself an instance of this declaration, or the
    // call is written inside one (the caller's graph is an instance). Both bind no further
    // dimension value, which is what keeps the total number of instances bounded.
    const bool relaxNames = isValid(calleeRoot) || isValid(callerRoot);

    for (size_t i = 0; i < formals.size(); ++i) {
        DraftStaticValue staticValue = tryResolveStaticValue(draft, actualInputs[i]);
        if (functionValuesOnly) {
            // The copy is this same function with one parameter already known, so the one binding
            // that is made is the function value a function-typed parameter holds. Nothing else is
            // bound, not a constant (that unrolls a recursion, a rewrite with a budget of its own)
            // and not a refined parameter type (that re-infers the copy's types). A function value
            // is not an object in the language's sense: it has no identity (design principles
            // 5.2), so nothing observable can tell the copy from the call.
            if (staticValue.valid() && staticValue.type->isGCTraced() &&
                staticValue.type->code() == camel::core::type::TypeCode::Function) {
                bindings.push_back(
                    DraftFormalBinding{
                        .kind         = formals[i].first,
                        .index        = i,
                        .formalNodeId = formals[i].second,
                        .staticValue  = staticValue,
                    });
            }
            continue;
        }
        // An argument whose value is not bound (dynamic, or an object, see below) may still have a
        // type more precise than the parameter's (a tensor whose type carries its shape): then the
        // callee's types are specialized to it.
        const auto bindType = [&] {
            const auto *actualHeader = draft.header(actualInputs[i]);
            const auto *formalNode   = calleeGraph->node(formals[i].second);
            if (!actualHeader || !formalNode) {
                return;
            }
            // What the instance's parameter states, not what the argument's value happens to be:
            // the extents the declaration leaves open stay open (see instanceParameterType).
            camel::core::type::Type *instantiated =
                instanceParameterType(formalNode->dataType(), actualHeader->dataType, relaxNames);
            if (refinesType(instantiated, formalNode->dataType())) {
                bindings.push_back(
                    DraftFormalBinding{
                        .kind         = formals[i].first,
                        .index        = i,
                        .formalNodeId = formals[i].second,
                        .staticValue  = {},
                        .refinedType  = instantiated,
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
    //
    // A bound function value is not a constant the recursion can count down to: the call the copy
    // makes is the same call, one function further along, and the specialization table answers the
    // second lap with the copy of the first. So a function value may be bound here, and a call in
    // a loop body or an arm keeps the one binding that turns its dispatch into a direct call.
    const bool bindsValues =
        std::ranges::any_of(bindings, [](const DraftFormalBinding &b) { return b.bindsValue(); });
    if (bindsValues && insideBranchArm(draft, funcNodeId) &&
        onCallCycle(*draft.program(), calleeGraph)) {
        // Only a constant (or an object) bound here unrolls, so only a call that binds one loses
        // its value bindings.
        const bool bindsOtherValues = std::ranges::any_of(bindings, [](const DraftFormalBinding &b) {
            return b.bindsValue() && b.staticValue.type != nullptr &&
                   b.staticValue.type->code() != camel::core::type::TypeCode::Function;
        });
        if (bindsOtherValues) {
            std::erase_if(bindings, [](const DraftFormalBinding &b) { return b.bindsValue(); });
        }
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
    GCGraph *encoded = draft.encode(mangledName, name);
    session.adoptGraph(encoded);
    return encoded;
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
    // The order is part of the key, so both sides must place two bindings of one type the same
    // way: a type is ordered by its canonical id (specializations.h) and not by the address of
    // whichever object states it, or one instance would be two keys.
    const auto orderOf = [](const RuntimeSpecializationBindingKey &binding) {
        return std::tuple{
            binding.kind,
            binding.index,
            camel::core::type::TypeTable::instance().canonicalId(binding.type),
            binding.value,
            binding.runtimeFlags};
    };
    std::sort(
        key.bindings.begin(), key.bindings.end(),
        [&orderOf](const auto &lhs, const auto &rhs) { return orderOf(lhs) < orderOf(rhs); });
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

    auto draft        = GraphDraft::decode(session.work(), baseGraph);
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
    GraphTransaction &transaction, const Function *function,
    std::span<camel::core::type::Type *const> normTypes,
    std::span<camel::core::type::Type *const> closureTypes) {
    // The graph is the transaction's: this helper edits it through the one handle.
    auto &draft = transaction.edit();
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
    GCGraph *targetGraph = functionGraph(session, function);
    if (targetGraph == nullptr) {
        return nullptr;
    }
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

void bindPortValueUses(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t portNodeId,
    gc_node_ref_t actualInputId) {
    auto &draft = transaction.edit();
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
    const std::vector<gc_node_ref_t> waiting = draft.waitUsersOf(portNodeId);
    for (gc_node_ref_t user : waiting) {
        draft.unlinkInput(DraftEdgeKind::Ctrl, user, portNodeId);
        appendControlDependency(transaction, actualInputId, user);
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
    RuntimeGraphDraftSession &session, GraphTransaction &transaction, gc_node_ref_t funcNodeId, bool functionValuesOnly) {
    auto &draft = transaction.edit();
    const DraftNodeHeader *funcHeader = draft.header(funcNodeId);
    if (!funcHeader || funcHeader->kind != GCNodeKind::Func) {
        return false;
    }
    GCGraph *const calleeGraph = draft.calleeGraphOf(funcNodeId);
    if (!calleeGraph) {
        return false;
    }

    const std::vector<DraftFormalBinding> bindings =
        collectDirectFuncSpecializations(draft, funcNodeId, calleeGraph, functionValuesOnly);
    CAMEL_LOG_INFO_S(
        "DraftOpt",
        "Specialize FUNC node {} callee='{}' bindings={}.",
        funcNodeId,
        calleeGraph->name(),
        bindings.size());
    if (bindings.empty()) {
        return false;
    }

    // The recursive call inside a type instance of the function it names asks for an instance
    // whose parameters are the ones that instance already states: the one it is in. Nothing is
    // bound by the copy, so the types the instance states have to accept the arguments;
    // when they do not, the call still specializes.
    const GraphId instanceRoot = draft.typeSpecializationRoot();
    if (std::ranges::none_of(
            bindings, [](const DraftFormalBinding &b) { return b.bindsValue(); }) &&
        isValid(instanceRoot) && instanceRoot == calleeGraph->id()) {
        std::vector<gc_node_ref_t> ports(draft.normPorts().begin(), draft.normPorts().end());
        const auto withPorts = draft.withPorts();
        ports.insert(ports.end(), withPorts.begin(), withPorts.end());
        const std::vector<gc_node_ref_t> actuals = orderedFuncActualInputs(draft, funcNodeId);
        bool accepts = ports.size() == actuals.size();
        for (size_t i = 0; accepts && i < ports.size(); ++i) {
            const DraftNodeHeader *port   = draft.header(ports[i]);
            const DraftNodeHeader *actual = draft.header(actuals[i]);
            accepts = port && actual && port->dataType && actual->dataType &&
                      port->dataType->assignableFrom(actual->dataType);
        }
        if (accepts) {
            draft.setCalleeGraph(funcNodeId, draft.id());
            return true;
        }
    }

    // Type-only specialization on a call cycle has no shape bound any more. A call inside an
    // instance of the function it names reuses that instance (above), and a call whose key
    // differs is the polymorphic case, which widens to `?`; what is left here is a runaway
    // guard, and passing it means the specialization did not reach a fixed point. That is a
    // compiler error naming the function: the eight-copy count this replaces used to keep the
    // current callee instead, which silently truncated the shape.
    const bool boundedTypeSpecialization =
        std::ranges::none_of(
            bindings,
            [](const DraftFormalBinding &b) { return b.bindsValue(); }) &&
        onCallCycle(*draft.program(), calleeGraph);
    if (boundedTypeSpecialization &&
        !session.findSpecialization(
            makeRuntimeSpecializationKey(calleeGraph, bindings))) {
        const GraphId root = isValid(calleeGraph->typeSpecializationRoot())
                                 ? calleeGraph->typeSpecializationRoot()
                                 : calleeGraph->id();
        if (session.typeSpecializationCount(root) >= kTypeSpecializationFixedPointGuard) {
            CAMEL_LOG_INFO_S(
                "DraftOpt",
                "FUNC node {} cannot specialize '{}': '{}' has {} type specializations.",
                funcNodeId,
                calleeGraph->name(),
                toText(root),
                kTypeSpecializationFixedPointGuard);
            throw std::runtime_error(std::format(
                "the type specialization of '{}' did not reach a fixed point: {} instances, each "
                "with a key the one before it did not have",
                calleeGraph->name(),
                kTypeSpecializationFixedPointGuard));
        }
        session.noteTypeSpecialization(root);
    }

    GCGraph *specializedGraph = specializeGraphWithBindings(
        session,
        calleeGraph,
        bindings,
        boundedTypeSpecialization ? kTypeSpecializationTag : std::string_view("spec"),
        session.nextSpecializationNonce());
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

    // The call keeps the mask of the captured variables it writes: only its declared `with`
    // arguments can be bound away here, so the captures keep their places after them, and the
    // value it hands on is the same tuple of their new versions, typed for this callee.
    const uint32_t writtenCaptures = draft.funcWriteMaskOf(funcNodeId);
    GCFuncRef newBody{
        .ref = draft.payloadGraphIndexOf(specializedGraph->id()), .reserved = writtenCaptures};
    std::array<std::byte, sizeof(GCFuncRef)> newPayload{};
    std::memcpy(newPayload.data(), &newBody, sizeof(newBody));
    init.payload    = std::span<const std::byte>(newPayload.data(), newPayload.size());
    init.normInputs = normInputs;
    init.withInputs = withInputs;
    const auto *type = specializedGraph->funcType();
    if (type != nullptr) {
        camel::core::type::Type *value =
            writtenCaptures != 0 ? callValueTypeOf(*specializedGraph, writtenCaptures)
                                 : (type->hasExitType() ? type->exitType() : nullptr);
        if (value != nullptr) {
            init.dataType = value;
        }
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
            if (!draft.waitInputsOf(current).empty()) {
                appendControlDependency(transaction, current, funcNodeId);
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
void keepCalleeOrdering(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t funcNodeId, gc_node_ref_t callee) {
    auto &draft                   = transaction.edit();
    const DraftNodeHeader *header = draft.header(callee);
    if (!header || draft.waitInputsOf(callee).empty()) {
        return;
    }
    appendControlDependency(transaction, callee, funcNodeId);
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
    auto draft                            = GraphDraft::decode(session.work(), base);
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
            type->exitType()));
    draft->setClosureType(nullptr);
    GCGraph *lifted = encodeSpecializedGraph(session, *draft, base, "lift", nonce);
    session.rememberSpecialization(std::move(key), lifted);
    return lifted;
}

/// A CALL of a closure built by FILL in this graph becomes a direct FUNC of the lifted graph.
bool devirtualizeClosureCallInDraft(
    RuntimeGraphDraftSession &session, camel::runtime::GraphTransaction &transaction,
    gc_node_ref_t callNodeId, gc_node_ref_t fillNodeId) {
    auto &draft = transaction.edit();
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
    GCGraph *base        = functionGraph(session, function);
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
    GCGraph *lifted = liftClosureGraph(session, base, session.nextLiftNonce());
    if (!lifted) {
        return false;
    }

    DraftNodeInit init = snapshotNodeInit(draft, callNodeId);
    std::vector<gc_node_ref_t> normInputs(
        draft.normInputsOf(callNodeId).begin(),
        draft.normInputsOf(callNodeId).end());
    normInputs.insert(normInputs.end(), captures.begin(), captures.end());
    const std::vector<gc_node_ref_t> directWith(withInputs.begin() + 1, withInputs.end());
    GCFuncRef funcBody{.ref = draft.payloadGraphIndexOf(lifted->id())};
    std::array<std::byte, sizeof(GCFuncRef)> payload{};
    std::memcpy(payload.data(), &funcBody, sizeof(funcBody));
    init.kind                       = GCNodeKind::Func;
    init.payload                    = std::span<const std::byte>(payload.data(), payload.size());
    init.normInputs                 = normInputs;
    init.withInputs                 = directWith;
    const gc_node_ref_t calleeInput = withInputs.front();
    draft.rewriteNode(callNodeId, init);
    keepCalleeOrdering(transaction, callNodeId, calleeInput);
    CAMEL_LOG_INFO_S(
        "DraftOpt",
        "Devirtualized closure CALL node {} to direct FUNC '{}'.",
        callNodeId,
        lifted->name());
    return true;
}

/// A node whose work is inside the regions it owns (the arms of a branch, the parts of a loop):
/// it carries no computation of its own to order, and what its regions hold is in them before it
/// is. Ordering such a node would order the region as a whole, which is not what 5.7 asks for and
/// what a back end cannot lay out -- the region's own nodes are ordered one by one instead.
bool nodeOwnsRegions(GCNodeKind kind) {
    return kind == GCNodeKind::Brch || kind == GCNodeKind::Join || kind == GCNodeKind::Loop;
}

/// The nodes a spliced body's order edges have to cover (docs/design/effects/edges.cn.md 5.7): the
/// effect and may-fault nodes of the body, i.e. exactly the ones the model does not let a pass
/// arrange freely (design principles 3.4). The same predicate is what an order edge may sit on at
/// all, so the seal check cannot refuse an edge this writes (4.6).
bool splicedBodyNeedsOrder(const GraphDraft &draft, gc_node_ref_t id) {
    const DraftNodeHeader *header = draft.header(id);
    if (header == nullptr || header->kind == GCNodeKind::Dead ||
        nodeOwnsRegions(header->kind)) {
        return false;
    }
    return !nodeIsFreelySchedulable(draft, id);
}

/// The node that takes the call's place in the chain of its block (docs/design/effects/edges.cn.md
/// 5.7): the body's result, when the splice made it and nothing has written it into a chain yet. A
/// node the caller brought in (a forwarded argument, a constant) is not the splice's to make one
/// statement of the caller's block, and a node the body's own chain already ends in keeps that
/// place -- an order edge or a sync edge moved onto it would fork the chain. Either way the GATE
/// over the value carries the place instead, and it is the splice's own node.
gc_node_ref_t spliceChainAnchor(
    GraphDraft &draft, size_t slotsBefore, gc_node_ref_t value, uint32_t region) {
    if (value >= static_cast<gc_node_ref_t>(slotsBefore) && draft.alive(value) &&
        draft.syncInputsOf(value).empty() && draft.syncUsersOf(value).empty()) {
        return value;
    }
    const DraftNodeHeader *valueHeader = draft.header(value);
    const DraftNodeInit gateInit{
        .dataIndex = valueHeader ? valueHeader->dataIndex : static_cast<gc_slot_idx_t>(0),
        .dataType  = valueHeader ? valueHeader->dataType : nullptr,
        .kind      = GCNodeKind::Gate,
        .region    = region,
        .normInputs = std::span<const gc_node_ref_t>(&value, 1),
    };
    return draft.addNode(gateInit);
}

/// 5.7's extra constraint, written beside the splice: the chain the call stood in runs
/// `before -> body -> after`, and *beside* that the body's effect and may-fault nodes are ordered
/// after the statement written before the call and before the one written after it. An order edge
/// only orders -- it makes nothing run, and it crosses a function boundary without claiming the
/// body's statements are statements of the caller's block -- which is why the splice needs it for
/// a body that carries no chain of its own, and keeps it for one that does.
///
/// Only the body's nodes in the block the call itself is a statement of are ordered: what the body
/// does inside a region of its own (the parts of a loop, the arms of a branch) is laid out by that
/// region's rules (5.2 and the Join/Loop layout), and an ordering never leaves the region that
/// holds it (4.6) -- ordering an inner-region node after a statement outside it would order the
/// region's work against the region, which the layout cannot honour.
void addSpliceOrderEdges(
    GraphDraft &draft, size_t slotsBefore, uint32_t blockRegion,
    const std::vector<gc_node_ref_t> &before, const std::vector<gc_node_ref_t> &after) {
    if (before.empty() && after.empty()) {
        return;
    }
    for (gc_node_ref_t id = static_cast<gc_node_ref_t>(slotsBefore); id < draft.nodeSlotCount();
         ++id) {
        if (!draft.alive(id) || !splicedBodyNeedsOrder(draft, id) ||
            controlRegionOf(draft, id) != blockRegion) {
            continue;
        }
        for (gc_node_ref_t pred : before) {
            if (draft.alive(pred) && controlRegionOf(draft, pred) == blockRegion) {
                draft.appendOrderDependency(id, pred);
            }
        }
        for (gc_node_ref_t user : after) {
            if (draft.alive(user) && splicedBodyNeedsOrder(draft, user) &&
                controlRegionOf(draft, user) == blockRegion) {
                draft.appendOrderDependency(user, id);
            }
        }
    }
}

} // namespace

bool devirtualizeStaticCallInDraft(
    RuntimeGraphDraftSession &session, GraphTransaction &transaction, gc_node_ref_t callNodeId,
    bool unwrapGatedCallee) {
    auto &draft                       = transaction.edit();
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
        return devirtualizeClosureCallInDraft(session, transaction, callNodeId, callee);
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

    GCGraph *targetGraph =
        specializeClosureBoundGraph(session, function, session.nextSpecializationNonce());
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
    GCFuncRef funcBody{.ref = draft.payloadGraphIndexOf(targetGraph->id())};
    std::array<std::byte, sizeof(GCFuncRef)> newPayload{};
    std::memcpy(newPayload.data(), &funcBody, sizeof(funcBody));
    init.kind    = GCNodeKind::Func;
    init.payload = std::span<const std::byte>(newPayload.data(), newPayload.size());
    const std::vector<gc_node_ref_t> directWith(withInputs.begin() + 1, withInputs.end());
    const gc_node_ref_t calleeInput = withInputs.front();
    init.withInputs                 = directWith;
    draft.rewriteNode(callNodeId, init);
    keepCalleeOrdering(transaction, callNodeId, calleeInput);
    CAMEL_LOG_INFO_S(
        "DraftOpt",
        "Devirtualized CALL node {} to direct FUNC '{}'.",
        callNodeId,
        targetGraph->name());
    return true;
}

/// A statement of a block follows one other statement of it at most, and is followed by one: a
/// block's statements are one chain (docs/design/effects/edges.cn.md 5.3). A chain that forks is
/// therefore not a chain, yet a splice can leave one: it reads the end of the body's chain while the
/// body's own calls are still to be spliced, so the statement written after the call is written after
/// a statement that gains a follower later, when the code the call stands for is spliced in turn.
/// The place is resolved again here, after the code is in -- reading which statement ends the chain
/// that grows from the one it was written after, rather than the snapshot the splice took.
void resolvePlaceForks(GraphDraft &draft) {
    const auto isStatement = [&](gc_node_ref_t n) {
        const DraftNodeHeader *header = draft.header(n);
        return draft.alive(n) && header != nullptr &&
               (header->runtimeFlags & kGCNodeFlagSyncStatement) != 0;
    };
    const auto inBlock = [&](gc_node_ref_t a, gc_node_ref_t b) {
        return sameSyncBlock(draft, a, b);
    };
    // Does `from` wait on `target`, directly or through any edge kind? The place is moved only where
    // the answer is no: a statement's place may not be one that waits on the statement itself (the
    // graph would lie on a cycle of inputs). An exhausted walk answers yes, because not proving it
    // free does not make it safe to move.
    const auto waitsOn = [&](gc_node_ref_t from, gc_node_ref_t target) {
        std::vector<gc_node_ref_t> work{from};
        std::unordered_set<gc_node_ref_t> seen;
        size_t budget = size_t{1} << 14;
        while (!work.empty()) {
            const gc_node_ref_t node = work.back();
            work.pop_back();
            if (node == target) {
                return true;
            }
            if (!seen.insert(node).second) {
                continue;
            }
            for (const auto &group :
                 {draft.normInputsOf(node),
                  draft.withInputsOf(node),
                  draft.ctrlInputsOf(node),
                  draft.syncInputsOf(node)}) {
                for (gc_node_ref_t input : group) {
                    if (budget == 0) {
                        return true;
                    }
                    --budget;
                    work.push_back(input);
                }
            }
        }
        return false;
    };
    // Every statement of a block that follows a statement a second statement of the block follows.
    std::vector<gc_node_ref_t> forked;
    for (gc_node_ref_t n = 0; n < draft.nodeSlotCount(); ++n) {
        if (!isStatement(n)) {
            continue;
        }
        const auto inputs = draft.syncInputsOf(n);
        if (inputs.size() != 1) {
            continue;
        }
        const gc_node_ref_t pred = inputs.front();
        if (pred == n || !isStatement(pred) || !inBlock(n, pred)) {
            continue;
        }
        size_t followers = 0;
        for (gc_node_ref_t user : draft.syncUsersOf(pred)) {
            if (user != n && isStatement(user) && inBlock(user, pred)) {
                ++followers;
            }
        }
        if (followers != 0) {
            forked.push_back(n);
        }
    }
    for (gc_node_ref_t n : forked) {
        const auto inputs = draft.syncInputsOf(n);
        if (inputs.size() != 1 || !draft.alive(n)) {
            continue;
        }
        const gc_node_ref_t pred = inputs.front();
        // The chain end that grows from `pred`, walked among the statements of its block and never
        // through `n` itself: the place moves to where the chain that the statement belonged to has
        // grown, which is where this statement's place is once the code is in.
        gc_node_ref_t tail = pred;
        std::unordered_set<gc_node_ref_t> seen{pred};
        bool moved = true;
        while (moved) {
            moved = false;
            gc_node_ref_t next = kInvalidNodeRef;
            size_t count      = 0;
            for (gc_node_ref_t user : draft.syncUsersOf(tail)) {
                if (user == n || !isStatement(user) || !inBlock(user, pred)) {
                    continue;
                }
                ++count;
                next = user;
            }
            if (count != 1 || !seen.insert(next).second) {
                break;
            }
            tail  = next;
            moved = true;
        }
        if (tail == pred || tail == n || waitsOn(tail, n)) {
            continue;
        }
        const gc_node_ref_t only[1] = {tail};
        draft.setSyncInputs(n, only);
        const auto ctrl = draft.ctrlInputsOf(n);
        if (std::ranges::find(ctrl, tail) == ctrl.end()) {
            draft.appendInput(DraftEdgeKind::Ctrl, n, tail);
        }
    }
}

DraftInlineResult inlineCallableInDraft(
    RuntimeGraphDraftSession &session, GraphTransaction &transaction, gc_node_ref_t funcNodeId,
    bool asArm) {
    auto &draft              = transaction.edit();
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
    if (draft.funcWriteMaskOf(funcNodeId) != 0) {
        // The call hands on the tuple of the callee's result and the new versions of the captured
        // variables it writes, and its ACCS nodes read its parts; the body spliced in its place
        // has the result alone, so the call is left as it is.
        CAMEL_LOG_INFO_S(
            "DraftInline",
            "Inline splice node {} rejected: the call writes captured variables, and its value is "
            "their tuple.",
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

    // What the body has to wait on: the control predecessors of the call, and the statement the
    // call was written after in its `sync` block (the chain is an ordering of its own,
    // docs/design/effects/edges.cn.md 1.5). The bodies of the statements in it are a chain of
    // another block, so the order crosses to them as control, exactly as it does for the other
    // ordering the call carried.
    std::vector<gc_node_ref_t> ctrlPreds(
        draft.ctrlInputsOf(funcNodeId).begin(),
        draft.ctrlInputsOf(funcNodeId).end());
    for (gc_node_ref_t syncPred : draft.syncInputsOf(funcNodeId)) {
        if (std::ranges::find(ctrlPreds, syncPred) == ctrlPreds.end()) {
            ctrlPreds.push_back(syncPred);
        }
    }
    ctrlPreds                  = controlFrontier(transaction, ctrlPreds);
    const bool hasExternalCtrl = !ctrlPreds.empty();
    // The statement(s) written after the call in its block: the call is replaced by the body's
    // result and the chain goes on from there, so 5.7's order edges have to reach exactly the nodes
    // that followed the call. Read now, because replacing the call's uses below is what makes them
    // the body's successors.
    std::vector<gc_node_ref_t> callFollowers(
        draft.ctrlUsersOf(funcNodeId).begin(), draft.ctrlUsersOf(funcNodeId).end());
    for (gc_node_ref_t syncUser : draft.syncUsersOf(funcNodeId)) {
        if (std::ranges::find(callFollowers, syncUser) == callFollowers.end()) {
            callFollowers.push_back(syncUser);
        }
    }

    // The order edges the call stood in: a read written before it is ordered before the body's
    // writes, and the reads written after it after them, as they were before the call (5.7).
    const std::vector<gc_node_ref_t> callOrderInputs(
        draft.orderInputsOf(funcNodeId).begin(), draft.orderInputsOf(funcNodeId).end());
    const std::vector<gc_node_ref_t> callOrderUsers(
        draft.orderUsersOf(funcNodeId).begin(), draft.orderUsersOf(funcNodeId).end());

    // The body goes in a region of its own, inside the region of the call: where the code
    // came from stays recorded after the call is gone. The body of an arm of a branch is the
    // Arm region of that arm.
    const bool intoArm = asArm;
    ASSERT(
        !intoArm || (draft.nodeRegion(funcNodeId) != 0 &&
                     draft.region(draft.nodeRegion(funcNodeId)).kind == GCRegionKind::Arm),
        "The call of an arm is the node of the region of its arm.");
    const size_t slotsBefore = draft.nodeSlotCount();
    // The block the call is a statement of: 5.7's order edges reach only what the body does in it.
    // Read before the splice, because the call itself is the statement being replaced.
    const uint32_t callBlockRegion = controlRegionOf(draft, funcNodeId);
    // The call is the whole of its arm, which already has its region: the body goes into it.
    // The region records the function this code is a copy of, which is what the inline history
    // asks it (GraphDraft::inlinedCopyOf). A type specialization is that function with a
    // more precise type, and the program the next round sees makes it again, under a new id, when
    // its copy of the specialization is gone: recording the specialization's id would leave a
    // record nothing resolves, the history would not recognize the call, and a recursion would
    // be unrolled a level per round without bound. Recording the function it derives from keeps
    // the record resolvable for as long as the function is.
    const GraphId inlineSource = isValid(calleeGraph->typeSpecializationRoot())
                                     ? calleeGraph->typeSpecializationRoot()
                                     : calleeGraph->id();
    const uint32_t inlineRegion =
        intoArm ? draft.nodeRegion(funcNodeId)
                : draft.addInlineRegion(
                      draft.nodeRegion(funcNodeId),
                      inlineSource,
                      kInvalidNodeRef);
    draft.setRegionOrigin(inlineRegion, static_cast<uint32_t>(draft.nodeOrigin(funcNodeId)));
    if (intoArm) {
        draft.setRegionInline(inlineRegion, kInvalidGraphId, kInvalidNodeRef);
    }
    DraftGraphCloneResult cloned =
        calleeDraftView ? cloneDraftGraphIntoDraft(draft, *calleeDraftView, inlineRegion)
                        : cloneRuntimeGraphIntoDraft(draft, calleeGraph, inlineRegion);
    if (cloned.returnNode != kInvalidNodeRef) {
        draft.setRegionResult(inlineRegion, cloned.returnNode);
    }
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

    // An argument the body never reads still runs. The call is where the argument is evaluated, and it
    // takes its effects with it (design principles 3.4): `h(5, println(9))` prints `9` although `h`
    // ignores its second parameter. Binding no use to the port left the actual reached by nothing
    // once the call was gone, and the effect stopped running with it (9.7). The body waits on the
    // argument instead -- on the same nodes the call's own predecessors are waited for on -- so the
    // effect runs where the call stood and before the body does.
    std::vector<gc_node_ref_t> unreadArgumentEffects;
    for (size_t i = 0; i < clonedFormalNodes.size() && i < actualInputs.size(); ++i) {
        const gc_node_ref_t port = clonedFormalNodes[i].second;
        if (port == kInvalidNodeRef || !draft.normUsersOf(port).empty() ||
            !draft.withUsersOf(port).empty() || !draft.waitUsersOf(port).empty()) {
            continue;
        }
        const gc_node_ref_t actual = actualInputs[i];
        if (!draft.alive(actual) || actual == funcNodeId ||
            !carriesOrdering(draft, actual, true)) {
            continue;
        }
        // Only the argument whose call was its last reader is lost: one another node reads keeps
        // running where that node is, and does not have to be pulled here as well.
        bool readOnlyHere = true;
        for (auto group : {draft.normUsersOf(actual),
                           draft.withUsersOf(actual),
                           draft.ctrlUsersOf(actual),
                           draft.syncUsersOf(actual)}) {
            for (gc_node_ref_t user : group) {
                readOnlyHere = readOnlyHere && user == funcNodeId;
            }
        }
        if (readOnlyHere &&
            std::ranges::find(unreadArgumentEffects, actual) == unreadArgumentEffects.end()) {
            unreadArgumentEffects.push_back(actual);
        }
    }
    // What the body waits for: the call's predecessors, and the effects of arguments it never
    // read.
    std::vector<gc_node_ref_t> waitPreds = ctrlPreds;
    for (gc_node_ref_t effect : unreadArgumentEffects) {
        if (std::ranges::find(waitPreds, effect) == waitPreds.end()) {
            waitPreds.push_back(effect);
        }
    }
    const bool bodyWaits = hasExternalCtrl || !unreadArgumentEffects.empty();

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
        bindPortValueUses(transaction, portId, boundActualInputs[actualIndex++]);
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

    // The entry roots take over the call's predecessors: each of them waits on the predecessors
    // itself. The roots are all there is to order: the parameter gates and the callee's recorded
    // entry node are not entry points. (They used to stand in when a body had no root; nothing of
    // such a body reads them, so the predecessors ended at nodes nothing followed.) A body with
    // several roots used to get one SYNC node standing for the predecessors, which the model
    // deletes (docs/design/effects/edges.cn.md 5.3): the wait is written on the roots themselves.
    std::vector<gc_node_ref_t> gateCtrl = waitPreds;
    if (!entryRoots.empty()) {
        result.ctrlEntry = entryRoots.front();
        for (gc_node_ref_t targetId : entryRoots) {
            const std::vector<gc_node_ref_t> mergedCtrlInputs =
                appendUniqueRefs(draft.ctrlInputsOf(targetId), waitPreds);
            draft.setCtrlInputs(targetId, mergedCtrlInputs);
        }
    }
    // A gate waits on the predecessors themselves; never on an entry root, which may read the gate.
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
    if (bodyWaits && entryRoots.empty() && resultIsGatedArgument) {
        // The body forwards an argument: its gate already waits on the predecessors.
        result.ctrlEntry = result.valueExit;
    } else if (bodyWaits && entryRoots.empty()) {
        // The body executes nothing: its result is a constant or one of the arguments. The
        // call still ran after its predecessors, and what waited on the call, or read its
        // value, ran after them too. A GATE on the result carries that. Without it the result
        // waits on nothing: where the call was the tail of a branch arm (placed in the arm by
        // the BRCH, or by an argument computed there), the tail became a value outside the
        // branch, the arm was taken to have no code, and what the call's arguments ran in the
        // arm belonged to no arm any more. The same gate carries an argument's effect when the
        // body reads none of it: the call ran it, so the value the call became waits for it.
        const DraftNodeHeader *valueHeader        = draft.header(result.valueExit);
        std::vector<gc_node_ref_t> gateCtrlInputs = waitPreds;
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
    // What the splice added around the body (the gates of the arguments, the bridge gate of the
    // result) is where the body is: in the arm, or where the call was.
    const uint32_t spliceRegion = intoArm ? inlineRegion : draft.nodeRegion(funcNodeId);
    for (gc_node_ref_t id = static_cast<gc_node_ref_t>(slotsBefore); id < draft.nodeSlotCount();
         ++id) {
        if (spliceRegion != 0 && draft.alive(id) &&
            !draft.regionWithin(draft.nodeRegion(id), spliceRegion)) {
            draft.setNodeRegion(id, spliceRegion);
        }
    }
    if (intoArm) {
        draft.setRegionResult(inlineRegion, result.valueExit);
    }

    // Value users and control users observing the call result now observe the
    // inlined output/completion anchor instead.
    draft.replaceAllValueUses(funcNodeId, result.valueExit);
    draft.replaceAllCtrlUses(funcNodeId, result.ctrlExit);
    // 5.7: the body's result replaces the call and keeps the call's own place in the chain of the
    // block -- its incoming and outgoing sync edges, and the order edges it stood in.
    // `takeStatementPlace` writes that place (and the control edge naming the same pairs beside it,
    // for as long as the chain of a block is written as control edges) when the replacer is a
    // statement of the block the call was written in. It runs here, before `eraseNode` closes the
    // chain over the call: the call has to be alive for its place to be read. A call with no such
    // place -- every call of today's corpus, whose block writes its chain as control edges --
    // has nothing to hand over, and nothing is added to the graph for it.
    //
    // A body that carries a chain of its own stands where the call stood as the statement the call
    // was: its head is written after the statement the call was written after, and the statements
    // written after the call are written after its tail. That is handed over below, once the body's
    // statements are laid out and the data edges of the reads it holds are final -- the chain then
    // finds a read where its nearest write left it instead of moving the read
    // (docs/design/effects/edges.cn.md 2, 5.6). A body that carries no chain of its own keeps the
    // value gate `spliceChainAnchor` builds: the gate is what the block's value reads and what the
    // chain runs through.
    const bool callHasChainPlace = !draft.syncInputsOf(funcNodeId).empty() ||
                                   !draft.syncUsersOf(funcNodeId).empty() ||
                                   !draft.orderInputsOf(funcNodeId).empty();
    const auto inCallBlock = [&](gc_node_ref_t id) {
        return id >= static_cast<gc_node_ref_t>(slotsBefore) && draft.alive(id) &&
               controlRegionOf(draft, id) == callBlockRegion;
    };
    // A statement of the block as the chain reads it: the flag the builder writes when it chains a
    // statement (`markSyncStatement`), read back over the code this splice added.
    const auto isBodyStatement = [&](gc_node_ref_t id) {
        if (id == funcNodeId || !inCallBlock(id)) {
            return false;
        }
        const DraftNodeHeader *h = draft.header(id);
        return h != nullptr && (h->runtimeFlags & kGCNodeFlagSyncStatement) != 0;
    };
    struct BodyChain {
        gc_node_ref_t head = kInvalidNodeRef;
        gc_node_ref_t tail = kInvalidNodeRef;
        size_t heads       = 0;
        size_t tails       = 0;
    };
    // The ends of the body's chain as the block's chain sees it: a statement nothing in the block
    // follows heads a chain, and one nothing in the block is written after ends it.
    const auto scanBodyChain = [&]() {
        BodyChain chain;
        for (gc_node_ref_t id = static_cast<gc_node_ref_t>(slotsBefore);
             id < draft.nodeSlotCount();
             ++id) {
            if (!isBodyStatement(id)) {
                continue;
            }
            bool bodyFollows = false;
            for (gc_node_ref_t user : draft.syncUsersOf(id)) {
                if (inCallBlock(user)) {
                    bodyFollows = true;
                    break;
                }
            }
            if (draft.syncInputsOf(id).empty()) {
                if (bodyFollows) {
                    chain.head = id;
                    ++chain.heads;
                }
            } else if (!bodyFollows) {
                chain.tail = id;
                ++chain.tails;
            }
        }
        return chain;
    };
    BodyChain bodyChain = scanBodyChain();
    // The chain the call stood in has to run through the body once the body is spliced in: its
    // statements are statements of the caller's block (docs/design/effects/edges.cn.md 5.7), and a
    // block's statements are one chain (5.3). The builder writes that chain statement by statement,
    // and it leaves one link of it out where the statement reads, as a value, the node the link
    // would run from: the data edge already orders the two, so `chainLinkSaysSomething`
    // (src/compile/gir/builder_helpers.h) writes no chain link there. A body compiled on its own is
    // entered from the call's predecessors, so the missing link costs it nothing; spliced in, the
    // pieces of its chain are on nothing at all, and a statement on no chain is a statement that
    // never runs (3.1). So the pieces are joined here, where both ends are known: a statement that
    // follows nothing in the block is written after the nearest statement it reads through a chain
    // of values -- the order the body's data edges already give it -- and the body's chain becomes
    // the one chain a block has.
    if (callHasChainPlace && bodyChain.heads > 1) {
        for (gc_node_ref_t head = static_cast<gc_node_ref_t>(slotsBefore);
             head < draft.nodeSlotCount();
             ++head) {
            if (!isBodyStatement(head) || !draft.syncInputsOf(head).empty()) {
                continue;
            }
            // The nearest statement this one reads: walking back over the value ports only, so the
            // link written below is one the body's data edges already order.
            std::vector<gc_node_ref_t> todo(
                draft.normInputsOf(head).begin(), draft.normInputsOf(head).end());
            todo.insert(
                todo.end(), draft.withInputsOf(head).begin(), draft.withInputsOf(head).end());
            std::unordered_set<gc_node_ref_t> seen{head};
            gc_node_ref_t pred = kInvalidNodeRef;
            while (!todo.empty() && pred == kInvalidNodeRef) {
                const gc_node_ref_t current = todo.back();
                todo.pop_back();
                if (current < static_cast<gc_node_ref_t>(slotsBefore) || !draft.alive(current) ||
                    !seen.insert(current).second) {
                    continue;
                }
                if (isBodyStatement(current) && draft.syncUsersOf(current).empty()) {
                    pred = current;
                    break;
                }
                todo.insert(
                    todo.end(),
                    draft.normInputsOf(current).begin(),
                    draft.normInputsOf(current).end());
                todo.insert(
                    todo.end(),
                    draft.withInputsOf(current).begin(),
                    draft.withInputsOf(current).end());
            }
            if (pred == kInvalidNodeRef || pred == head) {
                continue;
            }
            // The pair the block's chain is written with: both ends a statement of it, the sync edge
            // naming the order and the control edge the same pair beside it, as the builder writes
            // them (writeSyncStatement).
            const auto markStatement = [&](gc_node_ref_t node) {
                const DraftNodeHeader *h = draft.header(node);
                if (h != nullptr) {
                    draft.setNodeRuntimeFlags(
                        node, static_cast<uint8_t>(h->runtimeFlags | kGCNodeFlagSyncStatement));
                }
            };
            markStatement(pred);
            markStatement(head);
            const gc_node_ref_t only[1] = {pred};
            draft.setSyncInputs(head, only);
            const auto ctrl = draft.ctrlInputsOf(head);
            if (std::ranges::find(ctrl, pred) == ctrl.end()) {
                draft.appendInput(DraftEdgeKind::Ctrl, head, pred);
            }
        }
        bodyChain = scanBodyChain();
    }
    gc_node_ref_t bodyHead            = bodyChain.head;
    gc_node_ref_t bodyTail            = bodyChain.tail;
    const size_t bodyHeads            = bodyChain.heads;
    const size_t bodyTails            = bodyChain.tails;
    // One head and one tail, or the body's chain is not one statement of the block -- a chain that
    // forks or breaks keeps the gate carrying the place rather than half of it moving.
    const bool bodyHasChain = bodyHeads == 1 && bodyTails == 1;
    if (!bodyHasChain) {
        bodyHead = kInvalidNodeRef;
        bodyTail = kInvalidNodeRef;
    }
    const std::vector<gc_node_ref_t> callSyncInputs(
        draft.syncInputsOf(funcNodeId).begin(), draft.syncInputsOf(funcNodeId).end());
    const std::vector<gc_node_ref_t> callSyncUsers(
        draft.syncUsersOf(funcNodeId).begin(), draft.syncUsersOf(funcNodeId).end());
    if (callHasChainPlace && !bodyHasChain) {
        const gc_node_ref_t chainAnchor =
            spliceChainAnchor(draft, slotsBefore, result.valueExit, draft.nodeRegion(funcNodeId));
        draft.takeStatementPlace(funcNodeId, chainAnchor);
        // 5.7: the caller's chain reaches the body's result by a sync edge. The body's statements are
        // reached from the graph's result through that chain (2), so the place the call had is handed
        // to the tail of the body's chain -- the statement the body's value reads, or the last
        // statement that stands in the body's chain -- rather than closed over the call. Closing it
        // over the call steps over the body, and a body whose value nothing reads (a call whose result
        // is unused) then keeps its statements reached by nothing.
        const auto inBody  = [&](gc_node_ref_t n) { return n >= slotsBefore; };
        const auto related = [&](gc_node_ref_t n) {
            return draft.regionWithin(draft.nodeRegion(n), draft.nodeRegion(funcNodeId)) ||
                   draft.regionWithin(draft.nodeRegion(funcNodeId), draft.nodeRegion(n));
        };
        // The body's result reads the tail of the body's chain: with the value standing in a chain of
        // its own, the caller's chain reaches the value itself and the body's tail stays on it
        // (edges.cn.md 5.3). A value that carries no chain (a constant) leaves the chain ending at the
        // body's last statement, so that statement takes the place instead.
        gc_node_ref_t bodyTail = kInvalidNodeRef;
        bool valueInChain      = false;
        for (gc_node_ref_t pred : draft.syncInputsOf(result.valueExit)) {
            if (inBody(pred) && related(pred)) {
                valueInChain = true;
                break;
            }
        }
        if (valueInChain) {
            bodyTail = result.valueExit;
        }
        for (gc_node_ref_t n = static_cast<gc_node_ref_t>(slotsBefore);
             bodyTail == kInvalidNodeRef && n < draft.nodeSlotCount();
             ++n) {
            const auto *h = draft.header(n);
            if (!draft.alive(n) || !related(n) || h == nullptr ||
                (h->runtimeFlags & camel::runtime::kGCNodeFlagSyncStatement) == 0) {
                continue;
            }
            bool followed = false;
            for (gc_node_ref_t user : draft.syncUsersOf(n)) {
                if (inBody(user)) {
                    followed = true;
                    break;
                }
            }
            if (!followed) {
                bodyTail = n;
            }
        }
        if (bodyTail != kInvalidNodeRef) {
            draft.handChainPlaceTo(funcNodeId, bodyTail);
        }
    }
    // The statements written directly after the call keep their place in the chain of the block
    // they are written in: the call is gone, so they follow what the call followed. They do not
    // follow the body's anchor instead -- that anchor is a node of the region the body was given,
    // and a sync edge never leaves the region of its block. `closeChainOver` writes that place, and
    // the control edge that names the same pair beside it; it runs here rather than inside eraseNode
    // below because the users have to be known before the call's own edges go.
    //
    // What the body ordered before those statements used to be carried as one order edge into the
    // body's anchor as well (7ca495665). It is gone: an order edge only orders something that has an
    // order, and the following statement is a plain statement of the block, not an effect
    // (docs/design/effects/edges.cn.md 1.5, 4.6). The ordering it named is what the body's region
    // carries once it stands on the chain as one statement of the block.
    draft.closeChainOver(funcNodeId);
    // 5.7's extra constraint: the body's effect and may-fault nodes are ordered after the statement
    // the call was written after and before the one written after it. The chain above carries the
    // same order for a body whose statements it reaches; this edge is what orders a body that
    // carries no chain of its own, and it is what survives when the chain's edges change kind.
    std::vector<gc_node_ref_t> orderPreds = ctrlPreds;
    for (gc_node_ref_t pred : callOrderInputs) {
        if (std::ranges::find(orderPreds, pred) == orderPreds.end()) {
            orderPreds.push_back(pred);
        }
    }
    std::vector<gc_node_ref_t> orderFollowers = callFollowers;
    for (gc_node_ref_t user : callOrderUsers) {
        if (std::ranges::find(orderFollowers, user) == orderFollowers.end()) {
            orderFollowers.push_back(user);
        }
    }
    addSpliceOrderEdges(draft, slotsBefore, callBlockRegion, orderPreds, orderFollowers);
    // 5.7: the chain the body brought with it stands where the call stood. Written here, after the
    // body's statements are laid out and the data edges of the reads it holds are final: a read of a
    // captured `var` keeps the place its nearest write gave it, and the chain is joined around it
    // rather than taking the read with it (docs/design/effects/edges.cn.md 2, 5.6).
    //
    // The place the call stood in is its `sync` predecessor, or -- while a block's chain is still
    // carried as control edges beside the sync ones -- the control predecessor that names the same
    // pair. A call written inside an arm has the entry the builder writes from the BRCH on its own
    // side of the region, and the chain it stands in is the one the branch's place entered: without
    // reading that place the body would be handed nothing to enter its chain from.
    std::vector<gc_node_ref_t> placePreds = callSyncInputs;
    if (placePreds.empty()) {
        placePreds = ctrlPreds;
    }
    // The statement written after the call is the one its sync edge names, and only that one: a sync
    // edge is what says one statement is written directly after another of its block
    // (docs/design/effects/edges.cn.md 1.5, 4.6), and a block's statements are one chain (5.3). The
    // call can have more nodes waiting on it than that -- a read of a `var` it writes, a gate
    // carrying the block's value, the tuple a `let` takes apart -- and those wait on it as an order,
    // not as the block's next statement: `replaceAllCtrlUses` above has already given each of them
    // the body's completion to wait on, so they keep their place and are not written into the
    // chain. Taking them from the control side as well hands the body's tail several sync
    // followers, and a statement with two followers is a chain that forks, which is no chain. The
    // control side is what names the successors only for a call that has no sync edge either way,
    // where there is nothing else to read them from.
    std::vector<gc_node_ref_t> placeUsers = callSyncUsers;
    if (placePreds.empty() && placeUsers.empty()) {
        placeUsers = callFollowers;
    }
    if (bodyHasChain && !placePreds.empty() && draft.alive(bodyHead) && draft.alive(bodyTail)) {
        const auto markStatement = [&](gc_node_ref_t node) {
            const DraftNodeHeader *header = draft.header(node);
            if (header != nullptr) {
                draft.setNodeRuntimeFlags(
                    node, static_cast<uint8_t>(header->runtimeFlags | kGCNodeFlagSyncStatement));
            }
        };
        const auto nameControlPair = [&](gc_node_ref_t user, gc_node_ref_t pred) {
            const auto ctrl = draft.ctrlInputsOf(user);
            if (std::ranges::find(ctrl, pred) == ctrl.end()) {
                draft.appendInput(DraftEdgeKind::Ctrl, user, pred);
            }
        };
        // The statements written after the call follow the body's tail. `closeChainOver` closed the
        // chain over the call above, so they follow what the call followed until this takes that
        // place back from it.
        for (gc_node_ref_t user : placeUsers) {
            if (user == funcNodeId || !draft.alive(user) || user == bodyHead || user == bodyTail) {
                continue;
            }
            markStatement(user);
            markStatement(bodyTail);
            const gc_node_ref_t only[1] = {bodyTail};
            draft.setSyncInputs(user, only);
            nameControlPair(user, bodyTail);
        }
        // The body's head is written after the statement the call was written after.
        const gc_node_ref_t callPred = placePreds.front();
        if (callPred != bodyHead && callPred != bodyTail && draft.alive(callPred)) {
            markStatement(bodyHead);
            markStatement(callPred);
            const gc_node_ref_t only[1] = {callPred};
            draft.setSyncInputs(bodyHead, only);
            nameControlPair(bodyHead, callPred);
        }
    }

    draft.eraseNode(funcNodeId);
    resolvePlaceForks(draft);
    CAMEL_LOG_INFO_S("DraftInline", "Inline splice finished for draft node {}.", funcNodeId);
    return result;
}

} // namespace camel::runtime
