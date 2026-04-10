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
 * Runtime draft inline helpers.
 *
 * The current implementation intentionally mirrors only the runtime-native core
 * splice:
 * - clone callee nodes into the owner draft
 * - bind callee ports to the FUNC node's actual arguments
 * - redirect value and control users of the FUNC node
 * - erase the original FUNC node
 *
 * This is the minimum reusable primitive needed before the large inline pass
 * itself can be migrated away from GIR-side GraphBuilder::inlineCallable.
 */

#include "camel/runtime/draft_inline.h"

#include "camel/runtime/draft_clone.h"

#include <algorithm>
#include <unordered_set>

namespace camel::runtime {

namespace {

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

void bindPortValueUses(GraphDraft &draft, gc_node_ref_t portNodeId, gc_node_ref_t actualInputId) {
    if (portNodeId == kInvalidNodeRef || actualInputId == kInvalidNodeRef) {
        return;
    }
    // Ports are parameter placeholders. After cloning into the owner draft,
    // all observable value uses must flow from the real caller operand instead.
    draft.replaceAllValueUses(portNodeId, actualInputId);
}

} // namespace

DraftInlineResult inlineCallableInDraft(GraphDraft &draft, gc_node_ref_t funcNodeId) {
    DraftInlineResult result{.callNode = funcNodeId};
    const DraftNodeHeader *funcHeader = draft.header(funcNodeId);
    if (!funcHeader || funcHeader->kind != GCNodeKind::Func) {
        return result;
    }

    const auto payloadBytes = draft.payloadOf(funcNodeId);
    if (payloadBytes.size_bytes() < sizeof(GCFuncBody)) {
        return result;
    }
    const auto *funcBody = reinterpret_cast<const GCFuncBody *>(payloadBytes.data());
    if (!funcBody->calleeGraph) {
        return result;
    }

    const std::vector<gc_node_ref_t> actualInputs = orderedFuncActualInputs(draft, funcNodeId);
    const size_t formalCount                      = funcBody->calleeGraph->normPorts().size() +
                                                    funcBody->calleeGraph->withPorts().size() +
                                                    funcBody->calleeGraph->closureNodes().size();
    if (actualInputs.size() != formalCount) {
        return DraftInlineResult{};
    }
    if (funcBody->calleeGraph->entryNodeRef() == kInvalidNodeRef ||
        funcBody->calleeGraph->outputNodeRef() == kInvalidNodeRef) {
        return DraftInlineResult{};
    }

    const std::vector<gc_node_ref_t> ctrlPreds(
        draft.ctrlInputsOf(funcNodeId).begin(),
        draft.ctrlInputsOf(funcNodeId).end());
    const bool needParameterGates = !ctrlPreds.empty() || !draft.ctrlUsersOf(funcNodeId).empty();

    DraftGraphCloneResult cloned = cloneRuntimeGraphIntoDraft(draft, funcBody->calleeGraph);
    const std::vector<gc_node_ref_t> entryRoots =
        collectExecutableEntryRoots(funcBody->calleeGraph, cloned.sourceToCloned);

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

    size_t actualIndex = 0;
    for (gc_node_ref_t portId : cloned.normPorts) {
        bindPortValueUses(draft, portId, boundActualInputs[actualIndex++]);
    }
    for (gc_node_ref_t portId : cloned.withPorts) {
        bindPortValueUses(draft, portId, boundActualInputs[actualIndex++]);
    }
    for (gc_node_ref_t closureId : cloned.closureNodes) {
        bindPortValueUses(draft, closureId, boundActualInputs[actualIndex++]);
    }

    // Callee ports are now pure placeholders. They should disappear before the
    // owner graph is re-encoded so later passes never observe cloned parameter
    // carriers as real executable nodes.
    for (gc_node_ref_t portId : cloned.normPorts) {
        draft.eraseNode(portId);
    }
    for (gc_node_ref_t portId : cloned.withPorts) {
        draft.eraseNode(portId);
    }
    for (gc_node_ref_t closureId : cloned.closureNodes) {
        draft.eraseNode(closureId);
    }

    result.valueExit = cloned.exitNode;
    if (result.valueExit == kInvalidNodeRef) {
        return DraftInlineResult{};
    }

    std::vector<gc_node_ref_t> entryTargets =
        !parameterGateTargets.empty() ? parameterGateTargets : entryRoots;
    if (entryTargets.empty()) {
        entryTargets.push_back(result.valueExit);
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

    // Value users and control users observing the call result now observe the
    // inlined output/completion anchor instead.
    draft.replaceAllValueUses(funcNodeId, result.valueExit);
    draft.replaceAllCtrlUses(funcNodeId, result.valueExit);

    draft.eraseNode(funcNodeId);
    return result;
}

} // namespace camel::runtime
