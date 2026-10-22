// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Macro draft rewriting.
 *
 * Evaluated macro values are materialized into the owner GraphDraft. Source
 * runtime graphs remain read-only; RuntimeGraphDraftSession commit later
 * rebuilds the reachable runtime closure and remaps static function carriers.
 */

#include "runtime.h"

#include "camel/utils/log.h"

#include <vector>

namespace camel::execute::macro_runtime {

namespace {

using camel::runtime::gc_node_ref_t;
using camel::runtime::GCNodeKind;

gc_node_ref_t bridgeMacroRewriteControl(
    camel::runtime::GraphDraft &draft, gc_node_ref_t replacedNodeId, gc_node_ref_t valueNodeId) {
    const std::vector<gc_node_ref_t> ctrlPreds(
        draft.ctrlInputsOf(replacedNodeId).begin(),
        draft.ctrlInputsOf(replacedNodeId).end());
    if (ctrlPreds.empty()) {
        return valueNodeId;
    }

    const auto *valueHeader = draft.header(valueNodeId);
    ASSERT(valueHeader != nullptr, "Macro rewrite bridge requires a materialized value node.");
    camel::runtime::DraftNodeInit gateInit{
        .dataIndex    = valueHeader->dataIndex,
        .dataType     = valueHeader->dataType,
        .kind         = GCNodeKind::Gate,
        .runtimeFlags = 0,
        .region       = draft.nodeRegion(replacedNodeId),
        .normInputs   = std::span<const gc_node_ref_t>(&valueNodeId, 1),
        .ctrlInputs   = ctrlPreds,
    };
    return draft.addNode(gateInit);
}

} // namespace

bool applyMacroRewrite(
    camel::runtime::RuntimeGraphDraftSession &session, const MacroCandidate &candidate,
    const MacroEvalResult &result, std::ostream &os) {
    if (!candidate.runtimeGraph || candidate.nodeRef == camel::runtime::kInvalidNodeRef ||
        !result.valid()) {
        return false;
    }

    const auto *runtimeNode = candidate.runtimeGraph->node(candidate.nodeRef);
    if (!runtimeNode) {
        return false;
    }

    camel::runtime::GraphDraft &draft = session.edit(candidate.runtimeGraph);
    const gc_node_ref_t draftNodeId   = draft.draftIdOfSourceRef(candidate.nodeRef);
    if (draftNodeId == camel::runtime::kInvalidNodeRef || !draft.alive(draftNodeId)) {
        return false;
    }

    const gc_node_ref_t valueNodeId =
        draft.materializeStaticValue(result.value(), result.type, result.runtimeFlags);
    const gc_node_ref_t replacementNodeId =
        bridgeMacroRewriteControl(draft, draftNodeId, valueNodeId);

    draft.replaceAllValueUses(draftNodeId, replacementNodeId);
    draft.replaceAllCtrlUses(draftNodeId, replacementNodeId);
    if (draft.outputNode() == draftNodeId) {
        draft.setOutputNode(replacementNodeId);
    }
    if (draft.returnNode() == draftNodeId) {
        draft.setReturnNode(replacementNodeId, camel::runtime::GCReturnKind::Self);
    }
    draft.eraseNode(draftNodeId);

    (void)os;
    CAMEL_LOG_INFO_S(
        "Macro",
        "Rewrote {}::ref#{} -> draft#{}.",
        candidate.runtimeGraph->name(),
        candidate.nodeRef,
        replacementNodeId);
    return true;
}

} // namespace camel::execute::macro_runtime
