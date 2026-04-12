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
 * Created: Apr. 06, 2026
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Shared GIR runtime execution helpers.
 *
 * The goal of this file is to consolidate execution semantics that should not
 * diverge between execution-oriented passes. It intentionally stays below pass
 * policy level and only provides graph-local mechanics.
 */

#include "camel/execute/graph_runtime_support.h"

#include "camel/core/rtdata/func.h"

#include <format>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>

using namespace camel::core::context;

namespace camel::execute {

namespace {

using camel::runtime::gc_node_ref_t;

gc_node_ref_t findRuntimeMatchedJoinIndex(camel::runtime::GCGraph *graph, gc_node_ref_t headIndex) {
    ASSERT(graph != nullptr, "Runtime matched-JOIN lookup requires a graph.");
    ASSERT(graph->containsNodeRef(headIndex), "Runtime branch arm head ref is out of range.");

    std::queue<gc_node_ref_t> worklist;
    std::unordered_set<gc_node_ref_t> visited;
    worklist.push(headIndex);
    visited.insert(headIndex);

    while (!worklist.empty()) {
        gc_node_ref_t currentIndex = worklist.front();
        worklist.pop();

        const auto *current = graph->node(currentIndex);
        ASSERT(current != nullptr, "Runtime branch traversal resolved to a null node record.");
        if (current->kind == camel::runtime::GCNodeKind::Join) {
            return currentIndex;
        }

        auto push_outputs = [&](std::span<const gc_node_ref_t> outputs) {
            for (gc_node_ref_t outputIndex : outputs) {
                if (graph->containsNodeRef(outputIndex) && visited.insert(outputIndex).second) {
                    worklist.push(outputIndex);
                }
            }
        };

        push_outputs(graph->ctrlOutputsOf(currentIndex));
        push_outputs(graph->normOutputsOf(currentIndex));
        push_outputs(graph->withOutputsOf(currentIndex));
    }

    throw std::runtime_error("Runtime branch arm does not reach a JOIN node.");
}

} // namespace

std::vector<gc_node_ref_t> buildReachableExecutionTopoIndices(camel::runtime::GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime graph is null in execution topo construction.");
    ASSERT(
        graph->hasNodePayload(),
        std::format("Runtime graph '{}' has no node payload.", graph->name()));

    std::vector<gc_node_ref_t> order;
    order.reserve(graph->nodeCount());
    std::unordered_map<gc_node_ref_t, uint8_t> state;
    state.reserve(graph->nodeCount());

    std::function<void(gc_node_ref_t)> visit = [&](gc_node_ref_t index) {
        if (!graph->containsNodeRef(index)) {
            return;
        }
        if (state[index] == 2) {
            return;
        }
        if (state[index] == 1) {
            throw std::runtime_error(
                std::format(
                    "Cycle detected while building runtime execution topo in graph '{}' at node "
                    "ref {}.",
                    graph->name(),
                    index));
        }
        state[index] = 1;

        for (gc_node_ref_t input : graph->ctrlInputsOf(index)) {
            visit(input);
        }
        for (gc_node_ref_t input : graph->normInputsOf(index)) {
            visit(input);
        }
        for (gc_node_ref_t input : graph->withInputsOf(index)) {
            visit(input);
        }

        state[index] = 2;
        order.push_back(index);
    };

    // Execution reachability must start from the control-complete exit when it
    // exists. For sync/effectful graphs the returned value is often forwarded
    // from a trailing GATE whose norm input is the value and whose ctrl input
    // encodes the side-effect chain. Starting from returnNodeRef alone drops
    // that control suffix and causes schedulers to compile only the returned
    // constant while silently skipping the observable work.
    if (graph->exitNodeRef() != camel::runtime::kInvalidNodeRef) {
        visit(graph->exitNodeRef());
    } else if (graph->returnNodeRef() != camel::runtime::kInvalidNodeRef) {
        visit(graph->returnNodeRef());
    }
    return order;
}

gc_node_ref_t resolveRuntimeForwardedValueRef(camel::runtime::GCGraph *graph, gc_node_ref_t ref) {
    ASSERT(graph != nullptr, "Runtime forwarded-value resolution requires a graph.");

    gc_node_ref_t currentIndex = ref;
    while (currentIndex != camel::runtime::kInvalidNodeRef) {
        const auto *current = graph->node(currentIndex);
        ASSERT(current != nullptr, "Runtime forwarded-value chain resolved to null.");
        if (current->kind != camel::runtime::GCNodeKind::Gate) {
            return currentIndex;
        }
        const auto normInputs = graph->normInputsOf(currentIndex);
        if (!normInputs.empty()) {
            currentIndex = normInputs.back();
            continue;
        }
        const auto withInputs = graph->withInputsOf(currentIndex);
        if (!withInputs.empty()) {
            currentIndex = withInputs.back();
            continue;
        }
        return camel::runtime::kInvalidNodeRef;
    }
    return camel::runtime::kInvalidNodeRef;
}

gc_node_ref_t resolveRuntimeTailValueRef(camel::runtime::GCGraph *graph) {
    if (!graph) {
        return camel::runtime::kInvalidNodeRef;
    }
    auto current = graph->returnNodeRef();
    if (current == camel::runtime::kInvalidNodeRef) {
        current = graph->exitNodeRef();
    }
    while (current != camel::runtime::kInvalidNodeRef) {
        const auto *node = graph->node(current);
        ASSERT(node != nullptr, "Runtime tail-value lookup resolved to null.");
        if (node->kind != camel::runtime::GCNodeKind::Gate) {
            break;
        }
        const auto normInputs = graph->normInputsOf(current);
        if (!normInputs.empty()) {
            current = normInputs.back();
            continue;
        }
        const auto ctrlInputs = graph->ctrlInputsOf(current);
        if (!ctrlInputs.empty()) {
            current = ctrlInputs.back();
            continue;
        }
        return camel::runtime::kInvalidNodeRef;
    }
    return current;
}

bool runtimeNodeOutputsContain(
    const camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, gc_node_ref_t targetRef) {
    if (!graph || targetRef == camel::runtime::kInvalidNodeRef) {
        return false;
    }
    auto contains = [targetRef](std::span<const gc_node_ref_t> refs) {
        return std::find(refs.begin(), refs.end(), targetRef) != refs.end();
    };
    return contains(graph->normOutputsOf(nodeRef)) || contains(graph->withOutputsOf(nodeRef)) ||
           contains(graph->ctrlOutputsOf(nodeRef));
}

bool hasOnlyTrivialRuntimeTailSuffixAfter(
    const camel::runtime::GCGraph *graph, std::span<const gc_node_ref_t> topoOrder,
    size_t anchorIndex) {
    ASSERT(graph != nullptr, "Runtime tail-suffix check requires a graph.");
    for (size_t i = anchorIndex + 1; i < topoOrder.size(); ++i) {
        const auto *node = graph->node(topoOrder[i]);
        ASSERT(node != nullptr, "Runtime tail-suffix lookup resolved to null.");
        if (node->kind != camel::runtime::GCNodeKind::Gate) {
            return false;
        }
    }
    return true;
}

RuntimeBranchArmRegion collectRuntimeBranchArmRegion(
    camel::runtime::GCGraph *graph, gc_node_ref_t brchIndex, size_t armIndex) {
    ASSERT(graph != nullptr, "Runtime branch region collection requires a graph.");
    ASSERT(graph->node(brchIndex) != nullptr, "Runtime BRCH record is missing.");
    ASSERT(
        graph->node(brchIndex)->kind == camel::runtime::GCNodeKind::Brch,
        "Runtime branch region collection requires a BRCH node.");

    const auto armHeads = graph->ctrlOutputsOf(brchIndex);
    ASSERT(armIndex < armHeads.size(), "Runtime BRCH arm index is out of range.");
    const gc_node_ref_t headIndex = armHeads[armIndex];
    const gc_node_ref_t joinIndex = findRuntimeMatchedJoinIndex(graph, headIndex);

    const auto joinInputs = graph->withInputsOf(joinIndex);
    ASSERT(armIndex < joinInputs.size(), "Runtime JOIN arm-tail index is out of range.");
    const gc_node_ref_t tailIndex = joinInputs[armIndex];

    std::unordered_set<gc_node_ref_t> forward;
    std::vector<gc_node_ref_t> worklist{headIndex};
    while (!worklist.empty()) {
        const gc_node_ref_t currentIndex = worklist.back();
        worklist.pop_back();
        if (!graph->containsNodeRef(currentIndex) || currentIndex == joinIndex ||
            forward.contains(currentIndex)) {
            continue;
        }
        forward.insert(currentIndex);

        auto push_outputs = [&](std::span<const gc_node_ref_t> outputs) {
            for (gc_node_ref_t outputIndex : outputs) {
                if (graph->containsNodeRef(outputIndex) && outputIndex != joinIndex) {
                    worklist.push_back(outputIndex);
                }
            }
        };

        push_outputs(graph->ctrlOutputsOf(currentIndex));
        push_outputs(graph->normOutputsOf(currentIndex));
        push_outputs(graph->withOutputsOf(currentIndex));
    }

    RuntimeBranchArmRegion arm{
        .headIndex   = headIndex,
        .tailIndex   = tailIndex,
        .joinIndex   = joinIndex,
        .topoIndices = {},
    };
    std::unordered_set<gc_node_ref_t> visited;
    std::function<void(gc_node_ref_t)> visit = [&](gc_node_ref_t currentIndex) {
        if (!graph->containsNodeRef(currentIndex) || !forward.contains(currentIndex) ||
            visited.contains(currentIndex)) {
            return;
        }
        visited.insert(currentIndex);

        auto visit_inputs = [&](std::span<const gc_node_ref_t> inputs) {
            for (gc_node_ref_t inputIndex : inputs) {
                if (forward.contains(inputIndex)) {
                    visit(inputIndex);
                }
            }
        };

        visit_inputs(graph->ctrlInputsOf(currentIndex));
        visit_inputs(graph->normInputsOf(currentIndex));
        visit_inputs(graph->withInputsOf(currentIndex));
        arm.topoIndices.push_back(currentIndex);
    };
    visit(tailIndex);
    ASSERT(
        !arm.topoIndices.empty(),
        "Runtime branch arm collection produced an empty execution region.");
    return arm;
}

gc_node_ref_t resolveRuntimeBranchArmEntry(
    camel::runtime::GCGraph *graph, gc_node_ref_t brchRef, size_t armIndex,
    std::span<const gc_node_ref_t> topoOrder) {
    ASSERT(graph != nullptr, "Runtime branch-arm entry resolution requires a graph.");

    const auto branchArms = graph->branchArmsOf(brchRef);
    ASSERT(armIndex < branchArms.size(), "Runtime BRCH arm index is out of range.");
    const auto *brchBody = graph->nodeBodyAs<camel::runtime::GCBrchBody>(brchRef);
    ASSERT(brchBody != nullptr, "Runtime BRCH body is missing.");

    std::unordered_set<gc_node_ref_t> armRegion;
    std::vector<gc_node_ref_t> worklist{branchArms[armIndex].head};
    while (!worklist.empty()) {
        const auto nodeRef = worklist.back();
        worklist.pop_back();
        if (nodeRef == camel::runtime::kInvalidNodeRef || nodeRef == brchRef ||
            nodeRef == brchBody->join || !graph->containsNodeRef(nodeRef) ||
            !armRegion.insert(nodeRef).second) {
            continue;
        }

        auto pushOutputs = [&](std::span<const gc_node_ref_t> outputs) {
            for (auto outputRef : outputs) {
                if (outputRef != brchBody->join) {
                    worklist.push_back(outputRef);
                }
            }
        };
        pushOutputs(graph->ctrlOutputsOf(nodeRef));
        pushOutputs(graph->normOutputsOf(nodeRef));
        pushOutputs(graph->withOutputsOf(nodeRef));
    }

    std::unordered_set<gc_node_ref_t> dependencyVisited;
    std::function<void(gc_node_ref_t)> collectInputs = [&](gc_node_ref_t nodeRef) {
        if (nodeRef == camel::runtime::kInvalidNodeRef || nodeRef == brchRef ||
            nodeRef == brchBody->join || !graph->containsNodeRef(nodeRef) ||
            !dependencyVisited.insert(nodeRef).second) {
            return;
        }
        armRegion.insert(nodeRef);

        for (auto inputRef : graph->ctrlInputsOf(nodeRef)) {
            collectInputs(inputRef);
        }
        for (auto inputRef : graph->normInputsOf(nodeRef)) {
            collectInputs(inputRef);
        }
        for (auto inputRef : graph->withInputsOf(nodeRef)) {
            collectInputs(inputRef);
        }
    };

    std::vector<gc_node_ref_t> regionNodes(armRegion.begin(), armRegion.end());
    for (auto nodeRef : regionNodes) {
        collectInputs(nodeRef);
    }
    for (auto nodeRef : topoOrder) {
        if (!armRegion.contains(nodeRef)) {
            continue;
        }
        const auto *node = graph->node(nodeRef);
        if (node && node->kind != camel::runtime::GCNodeKind::Data &&
            node->kind != camel::runtime::GCNodeKind::Port &&
            node->kind != camel::runtime::GCNodeKind::Sync &&
            node->kind != camel::runtime::GCNodeKind::Gate &&
            node->kind != camel::runtime::GCNodeKind::Dref) {
            return nodeRef;
        }
    }
    return brchBody->join;
}

size_t selectRuntimeBranchArm(camel::runtime::GCGraph *graph, gc_node_ref_t brchRef, Frame *frame) {
    ASSERT(graph != nullptr, "Runtime branch selection requires a graph.");
    ASSERT(frame != nullptr, "Runtime branch selection requires a frame.");

    const auto normInputs = graph->normInputsOf(brchRef);
    const auto withInputs = graph->withInputsOf(brchRef);
    ASSERT(normInputs.size() == 1, "Runtime BRCH must have exactly one norm input.");

    const auto *condNode = graph->node(normInputs.front());
    ASSERT(condNode != nullptr, "Runtime BRCH condition node is missing.");
    const auto condIndex = condNode->dataIndex;

    if (withInputs.empty()) {
        return frame->get<bool>(condIndex) ? 0 : 1;
    }

    const camel::core::type::TypeCode condType = frame->codeAt(condIndex);
    if (camel::core::type::isGCTraced(condType)) {
        camel::core::type::Type *condTypePtr = frame->typeAt<camel::core::type::Type>(condIndex);
        camel::core::rtdata::Object *condData =
            frame->get<camel::core::rtdata::Object *>(condIndex);
        for (size_t i = 0; i < withInputs.size(); ++i) {
            const auto *caseNode = graph->node(withInputs[i]);
            ASSERT(caseNode != nullptr, "Runtime BRCH case node is missing.");
            camel::core::rtdata::Object *caseData =
                frame->get<camel::core::rtdata::Object *>(caseNode->dataIndex);
            if (condData->equals(caseData, condTypePtr, false)) {
                return i;
            }
        }
        return withInputs.size();
    }

    const slot_t condData = frame->get<slot_t>(condIndex);
    for (size_t i = 0; i < withInputs.size(); ++i) {
        const auto *caseNode = graph->node(withInputs[i]);
        ASSERT(caseNode != nullptr, "Runtime BRCH case node is missing.");
        if (condData == frame->get<slot_t>(caseNode->dataIndex)) {
            return i;
        }
    }
    return withInputs.size();
}

slot_t readRuntimeGraphReturn(camel::runtime::GCGraph *graph, Frame *frame) {
    ASSERT(graph != nullptr, "Runtime graph return read requires a graph.");
    ASSERT(frame != nullptr, "Runtime graph return read requires a frame.");

    const auto returnKind    = graph->returnKind();
    const auto returnNodeRef = graph->returnNodeRef();
    const auto *returnNode   = graph->returnNode();
    if (returnKind == camel::runtime::GCReturnKind::None ||
        returnNodeRef == camel::runtime::kInvalidNodeRef || returnNode == nullptr) {
        return NullSlot;
    }

    if (returnKind == camel::runtime::GCReturnKind::Self) {
        ASSERT(returnNode->dataIndex != 0, "Runtime graph return slot resolved to slot 0.");
        return frame->get<slot_t>(returnNode->dataIndex);
    }

    gc_node_ref_t currentIndex = resolveRuntimeForwardedValueRef(graph, returnNodeRef);
    if (currentIndex == camel::runtime::kInvalidNodeRef) {
        return NullSlot;
    }

    const auto *current = graph->node(currentIndex);
    ASSERT(current != nullptr, "Runtime graph return forwarding chain resolved to null.");
    ASSERT(current->dataIndex != 0, "Runtime graph return forwarding chain resolved to slot 0.");
    return frame->get<slot_t>(current->dataIndex);
}

void fillFrameForDirectInvoke(
    Frame *from, Frame *dest, camel::runtime::GCGraph *callerGraph, gc_node_ref_t callNodeIndex) {
    if (from == nullptr || dest == nullptr) {
        throw std::runtime_error("Frame pointer is null in runtime direct invoke binding.");
    }
    if (callerGraph == nullptr) {
        throw std::runtime_error("Caller runtime graph is null in runtime direct invoke binding.");
    }
    auto *targetGraph = dest->runtimeGraph();
    if (targetGraph == nullptr) {
        throw std::runtime_error(
            "Destination frame is missing runtime graph in runtime direct invoke binding.");
    }
    if (callerGraph->node(callNodeIndex) == nullptr) {
        throw std::runtime_error("Direct invoke call node record is null.");
    }

    const auto callerNormInputs = callerGraph->normInputsOf(callNodeIndex);
    const auto targetNormPorts  = targetGraph->normPorts();
    if (callerNormInputs.size() != targetNormPorts.size()) {
        throw std::runtime_error(
            std::format(
                "Runtime direct invoke norm-arity mismatch: caller graph '{}' node {} has {} args, "
                "target graph '{}' has {} norm ports.",
                callerGraph->name(),
                callNodeIndex,
                callerNormInputs.size(),
                targetGraph->name(),
                targetNormPorts.size()));
    }
    for (size_t i = 0; i < callerNormInputs.size(); ++i) {
        const auto *argRecord  = callerGraph->node(callerNormInputs[i]);
        const auto *portRecord = targetGraph->node(targetNormPorts[i]);
        if (argRecord == nullptr || portRecord == nullptr) {
            throw std::runtime_error("Runtime direct invoke norm argument/port record is null.");
        }
        dest->set(portRecord->dataIndex, from->get<slot_t>(argRecord->dataIndex));
    }

    const auto callerWithInputs = callerGraph->withInputsOf(callNodeIndex);
    const auto targetWithPorts  = targetGraph->withPorts();
    if (callerWithInputs.size() != targetWithPorts.size()) {
        throw std::runtime_error(
            std::format(
                "Runtime direct invoke with-arity mismatch: caller graph '{}' node {} has {} args, "
                "target graph '{}' has {} with ports.",
                callerGraph->name(),
                callNodeIndex,
                callerWithInputs.size(),
                targetGraph->name(),
                targetWithPorts.size()));
    }
    for (size_t i = 0; i < callerWithInputs.size(); ++i) {
        const auto *argRecord  = callerGraph->node(callerWithInputs[i]);
        const auto *portRecord = targetGraph->node(targetWithPorts[i]);
        if (argRecord == nullptr || portRecord == nullptr) {
            throw std::runtime_error("Runtime direct invoke with argument/port record is null.");
        }
        dest->set(portRecord->dataIndex, from->get<slot_t>(argRecord->dataIndex));
    }
}

void fillFrameForIndirectCall(
    Frame *from, Frame *dest, camel::runtime::GCGraph *callerGraph, gc_node_ref_t callNodeIndex) {
    if (from == nullptr || dest == nullptr) {
        throw std::runtime_error("Frame pointer is null in runtime indirect call binding.");
    }
    if (callerGraph == nullptr) {
        throw std::runtime_error("Caller runtime graph is null in runtime indirect call binding.");
    }
    auto *targetGraph = dest->runtimeGraph();
    if (targetGraph == nullptr) {
        throw std::runtime_error(
            "Destination frame is missing runtime graph in runtime indirect call binding.");
    }
    if (callerGraph->node(callNodeIndex) == nullptr) {
        throw std::runtime_error("Indirect call node record is null.");
    }

    const auto callerNormInputs = callerGraph->normInputsOf(callNodeIndex);
    const auto targetNormPorts  = targetGraph->normPorts();
    if (callerNormInputs.size() != targetNormPorts.size()) {
        throw std::runtime_error(
            std::format(
                "Runtime indirect call norm-arity mismatch: caller graph '{}' node {} has {} args, "
                "target graph '{}' has {} norm ports.",
                callerGraph->name(),
                callNodeIndex,
                callerNormInputs.size(),
                targetGraph->name(),
                targetNormPorts.size()));
    }
    for (size_t i = 0; i < callerNormInputs.size(); ++i) {
        const auto *argRecord  = callerGraph->node(callerNormInputs[i]);
        const auto *portRecord = targetGraph->node(targetNormPorts[i]);
        if (argRecord == nullptr || portRecord == nullptr) {
            throw std::runtime_error("Runtime indirect call norm argument/port record is null.");
        }
        dest->set(portRecord->dataIndex, from->get<slot_t>(argRecord->dataIndex));
    }

    const auto callerWithInputs = callerGraph->withInputsOf(callNodeIndex);
    if (callerWithInputs.empty()) {
        throw std::runtime_error(
            "CALL node must have function input in runtime indirect call binding.");
    }
    const auto *calleeRecord = callerGraph->node(callerWithInputs.front());
    if (calleeRecord == nullptr) {
        throw std::runtime_error("Indirect call callee record is null.");
    }
    Function *func = from->get<Function *>(calleeRecord->dataIndex);
    if (func == nullptr) {
        throw std::runtime_error("Indirect call callee slot resolved to null Function.");
    }
    Tuple *closure                = func ? func->tuple() : nullptr;
    const auto targetClosureNodes = targetGraph->closureNodes();
    if (targetClosureNodes.empty()) {
        return;
    }
    if (closure == nullptr) {
        throw std::runtime_error("Indirect call closure tuple is null.");
    }
    if (closure->size() != targetClosureNodes.size()) {
        throw std::runtime_error(
            std::format(
                "Runtime indirect call closure-arity mismatch: target graph '{}' expects {}, "
                "callee closure provides {}.",
                targetGraph->name(),
                targetClosureNodes.size(),
                closure->size()));
    }
    for (size_t i = 0; i < targetClosureNodes.size(); ++i) {
        const auto *closureRecord = targetGraph->node(targetClosureNodes[i]);
        if (closureRecord == nullptr) {
            throw std::runtime_error("Runtime indirect call closure node record is null.");
        }
        dest->set(closureRecord->dataIndex, closure->get<slot_t>(i));
    }
}

} // namespace camel::execute
