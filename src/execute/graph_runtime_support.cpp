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
 * Updated: Apr. 10, 2026
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

#include "camel/common/algo/topo.h"
#include "camel/core/rtdata/func.h"

#include <format>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>

using namespace GIR;
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

std::vector<Node *> buildReachableExecutionTopo(Graph *graph) {
    ASSERT(
        graph != nullptr && graph->finalized(),
        std::format(
            "Graph {} must be finalized before execution topo construction.",
            graph ? graph->name() : "<null>"));

    return findReachable(
        graph->exitNode(),
        [](Node *n) {
            node_vec_t ins;
            ins.reserve(n->dataInputs().size() + n->ctrlInputs().size());
            for (const auto &in : n->ctrlInputs()) {
                if (&in->graph() == &n->graph()) {
                    ins.emplace_back(in);
                }
            }
            for (const auto &in : n->dataInputs()) {
                if (&in->graph() == &n->graph()) {
                    ins.emplace_back(in);
                }
            }
            return ins;
        },
        false);
}

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
        ASSERT(state[index] != 1, "Cycle detected while building runtime execution topo.");
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

    if (graph->returnNodeRef() != camel::runtime::kInvalidNodeRef) {
        visit(graph->returnNodeRef());
    } else if (graph->exitNodeRef() != camel::runtime::kInvalidNodeRef) {
        visit(graph->exitNodeRef());
    }
    return order;
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

    gc_node_ref_t currentIndex = returnNodeRef;
    while (true) {
        const auto *current = graph->node(currentIndex);
        ASSERT(current != nullptr, "Runtime graph return forwarding chain resolved to null.");
        if (current->kind != camel::runtime::GCNodeKind::Gate) {
            break;
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
        return NullSlot;
    }

    const auto *current = graph->node(currentIndex);
    ASSERT(current != nullptr, "Runtime graph return forwarding chain resolved to null.");
    ASSERT(current->dataIndex != 0, "Runtime graph return forwarding chain resolved to slot 0.");
    return frame->get<slot_t>(current->dataIndex);
}

void fillFrameForDirectInvoke(
    Frame *from, Frame *dest, camel::runtime::GCGraph *callerGraph, gc_node_ref_t callNodeIndex) {
    ASSERT(
        from != nullptr && dest != nullptr,
        "Frame pointer is null in runtime direct invoke binding.");
    ASSERT(
        callerGraph != nullptr,
        "Caller runtime graph is null in runtime direct invoke binding.");
    auto *targetGraph = dest->runtimeGraph();
    ASSERT(
        targetGraph != nullptr,
        "Destination frame is missing runtime graph in runtime direct invoke binding.");
    ASSERT(callerGraph->node(callNodeIndex) != nullptr, "Direct invoke call node record is null.");

    const auto callerNormInputs = callerGraph->normInputsOf(callNodeIndex);
    const auto targetNormPorts  = targetGraph->normPorts();
    ASSERT(
        callerNormInputs.size() == targetNormPorts.size(),
        "Norm nodes and ports count mismatch in runtime direct invoke binding.");
    for (size_t i = 0; i < callerNormInputs.size(); ++i) {
        const auto *argRecord = callerGraph->node(callerNormInputs[i]);
        ASSERT(argRecord != nullptr, "Direct invoke norm argument record is null.");
        dest->set(
            targetGraph->node(targetNormPorts[i])->dataIndex,
            from->get<slot_t>(argRecord->dataIndex));
    }

    const auto callerWithInputs = callerGraph->withInputsOf(callNodeIndex);
    const auto targetWithPorts  = targetGraph->withPorts();
    ASSERT(
        callerWithInputs.size() == targetWithPorts.size(),
        "With nodes and ports count mismatch in runtime direct invoke binding.");
    for (size_t i = 0; i < callerWithInputs.size(); ++i) {
        const auto *argRecord = callerGraph->node(callerWithInputs[i]);
        ASSERT(argRecord != nullptr, "Direct invoke with argument record is null.");
        dest->set(
            targetGraph->node(targetWithPorts[i])->dataIndex,
            from->get<slot_t>(argRecord->dataIndex));
    }
}

void fillFrameForIndirectCall(
    Frame *from, Frame *dest, camel::runtime::GCGraph *callerGraph, gc_node_ref_t callNodeIndex) {
    ASSERT(
        from != nullptr && dest != nullptr,
        "Frame pointer is null in runtime indirect call binding.");
    ASSERT(
        callerGraph != nullptr,
        "Caller runtime graph is null in runtime indirect call binding.");
    auto *targetGraph = dest->runtimeGraph();
    ASSERT(
        targetGraph != nullptr,
        "Destination frame is missing runtime graph in runtime indirect call binding.");
    ASSERT(callerGraph->node(callNodeIndex) != nullptr, "Indirect call node record is null.");

    const auto callerNormInputs = callerGraph->normInputsOf(callNodeIndex);
    const auto targetNormPorts  = targetGraph->normPorts();
    ASSERT(
        callerNormInputs.size() == targetNormPorts.size(),
        "Norm nodes and ports count mismatch in runtime indirect call binding.");
    for (size_t i = 0; i < callerNormInputs.size(); ++i) {
        const auto *argRecord = callerGraph->node(callerNormInputs[i]);
        ASSERT(argRecord != nullptr, "Indirect call norm argument record is null.");
        dest->set(
            targetGraph->node(targetNormPorts[i])->dataIndex,
            from->get<slot_t>(argRecord->dataIndex));
    }

    const auto callerWithInputs = callerGraph->withInputsOf(callNodeIndex);
    ASSERT(
        !callerWithInputs.empty(),
        "CALL node must have function input in runtime indirect call binding.");
    const auto *calleeRecord = callerGraph->node(callerWithInputs.front());
    ASSERT(calleeRecord != nullptr, "Indirect call callee record is null.");
    Function *func                = from->get<Function *>(calleeRecord->dataIndex);
    Tuple *closure                = func ? func->tuple() : nullptr;
    const auto targetClosureNodes = targetGraph->closureNodes();
    if (targetClosureNodes.empty()) {
        return;
    }
    ASSERT(closure != nullptr, "Indirect call closure tuple is null.");
    ASSERT(
        closure->size() == targetClosureNodes.size(),
        "Closure nodes and tuple size mismatch in runtime indirect call binding.");
    for (size_t i = 0; i < targetClosureNodes.size(); ++i) {
        dest->set(targetGraph->node(targetClosureNodes[i])->dataIndex, closure->get<slot_t>(i));
    }
}

void fillFrameForDirectInvoke(Frame *from, Frame *dest, Graph *graph, Node *node) {
    ASSERT(from != nullptr && dest != nullptr, "Frame pointer is null in direct invoke binding.");
    ASSERT(graph != nullptr && node != nullptr, "Graph or node is null in direct invoke binding.");

    const auto &normNodes = node->normInputs();
    const auto &normPorts = graph->normPorts();
    ASSERT(
        normNodes.size() == normPorts.size(),
        "Norm nodes and ports count mismatch in direct invoke binding.");
    for (size_t i = 0; i < normNodes.size(); ++i) {
        dest->set(normPorts[i]->index(), from->get<slot_t>(normNodes[i]->index()));
    }

    const auto &withNodes = node->withInputs();
    const auto &withPorts = graph->withPorts();
    ASSERT(
        withNodes.size() == withPorts.size(),
        "With nodes and ports count mismatch in direct invoke binding.");
    for (size_t i = 0; i < withNodes.size(); ++i) {
        dest->set(withPorts[i]->index(), from->get<slot_t>(withNodes[i]->index()));
    }
}

void fillFrameForIndirectCall(Frame *from, Frame *dest, Graph *graph, CallNode *node) {
    ASSERT(from != nullptr && dest != nullptr, "Frame pointer is null in indirect call binding.");
    ASSERT(graph != nullptr && node != nullptr, "Graph or node is null in indirect call binding.");

    const auto &normNodes = node->normInputs();
    const auto &normPorts = graph->normPorts();
    ASSERT(
        normNodes.size() == normPorts.size(),
        "Norm nodes and ports count mismatch in indirect call binding.");
    for (size_t i = 0; i < normNodes.size(); ++i) {
        dest->set(normPorts[i]->index(), from->get<slot_t>(normNodes[i]->index()));
    }

    const auto &withNodes = node->withInputs();
    const auto &withPorts = graph->withPorts();
    ASSERT(
        withNodes.size() == withPorts.size() + 1,
        "With nodes and ports count mismatch in indirect call binding.");
    for (size_t i = 0; i < withPorts.size(); ++i) {
        dest->set(withPorts[i]->index(), from->get<slot_t>(withNodes[i + 1]->index()));
    }
}

} // namespace camel::execute
