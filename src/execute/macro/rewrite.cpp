/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Macro draft rewriting.
 *
 * Evaluated macro values are materialized into the owner GraphDraft. Source
 * runtime graphs remain read-only; RuntimeGraphDraftSession commit later
 * rebuilds the reachable runtime closure and remaps static function carriers.
 */

#include "runtime.h"

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
    const bool needControlBridge = !ctrlPreds.empty() || draft.isBranchArmAnchor(replacedNodeId) ||
                                   draft.entryNode() == replacedNodeId ||
                                   draft.exitNode() == replacedNodeId;
    if (!needControlBridge) {
        return valueNodeId;
    }

    const auto *valueHeader = draft.header(valueNodeId);
    ASSERT(valueHeader != nullptr, "Macro rewrite bridge requires a materialized value node.");
    camel::runtime::DraftNodeInit gateInit{
        .dataIndex    = valueHeader->dataIndex,
        .dataType     = valueHeader->dataType,
        .kind         = GCNodeKind::Gate,
        .runtimeFlags = 0,
        .normInputs   = std::span<const gc_node_ref_t>(&valueNodeId, 1),
        .ctrlInputs   = ctrlPreds,
    };
    return draft.addNode(gateInit);
}

void dropMacroRewriteControlUses(camel::runtime::GraphDraft &draft, gc_node_ref_t replacedNodeId) {
    std::vector<gc_node_ref_t> ctrlUsers(
        draft.ctrlUsersOf(replacedNodeId).begin(),
        draft.ctrlUsersOf(replacedNodeId).end());
    for (gc_node_ref_t userId : ctrlUsers) {
        draft.unlinkInput(camel::runtime::DraftEdgeKind::Ctrl, userId, replacedNodeId);
    }
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
    if (replacementNodeId != valueNodeId) {
        draft.replaceAllCtrlUses(draftNodeId, replacementNodeId);
    } else {
        dropMacroRewriteControlUses(draft, draftNodeId);
    }
    draft.retargetBranchArmAnchors(draftNodeId, replacementNodeId, replacementNodeId);
    if (draft.outputNode() == draftNodeId) {
        draft.setOutputNode(replacementNodeId);
    }
    if (draft.returnNode() == draftNodeId) {
        draft.setReturnNode(replacementNodeId, camel::runtime::GCReturnKind::Self);
    }
    draft.eraseNode(draftNodeId);

    os << "[macro] rewrote " << candidate.runtimeGraph->name() << "::ref#" << candidate.nodeRef
       << " -> draft#" << replacementNodeId << "\n";
    return true;
}

} // namespace camel::execute::macro_runtime
