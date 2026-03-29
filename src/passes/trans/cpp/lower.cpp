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
 * Created: Mar. 11, 2026
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "lower.h"

#include "bridge.h"

#include "camel/common/algo/topo.h"
#include "camel/utils/type.h"

#include <array>

using namespace GIR;
using namespace camel::core::type;

namespace {

std::vector<Node *> buildTopoNodes(Graph *graph) {
    Node *exitNode = graph->exitNode();
    return findReachable(
        exitNode,
        [](Node *node) {
            std::vector<Node *> inputs;
            inputs.reserve(node->dataInputs().size() + node->ctrlInputs().size());
            for (const auto &in : node->ctrlInputs()) {
                if (&in->graph() == &node->graph()) {
                    inputs.push_back(in);
                }
            }
            for (const auto &in : node->dataInputs()) {
                if (&in->graph() == &node->graph()) {
                    inputs.push_back(in);
                }
            }
            return inputs;
        },
        false);
}

void addIssue(GraphLoweringPlan &plan, const Node *node, std::string reason) {
    plan.issues.push_back({node, std::move(reason)});
}

bool hasBridgeableInputs(const Node *node) {
    for (const auto *input : node->dataInputs()) {
        if (!isBridgeableTypeForCpp(input->dataType())) {
            return false;
        }
    }
    return true;
}

bool hasBridgeableOutput(const Node *node) { return isBridgeableTypeForCpp(node->dataType()); }

using Spec = CppOperatorSpec;

constexpr std::array kOperatorSpecs = {
    Spec{":op/add_i", CppOperatorEmitKind::InlineBinary, 2, 0, "+"},
    Spec{":op/add_l", CppOperatorEmitKind::InlineBinary, 2, 0, "+"},
    Spec{":op/add_f", CppOperatorEmitKind::InlineBinary, 2, 0, "+"},
    Spec{":op/add_d", CppOperatorEmitKind::InlineBinary, 2, 0, "+"},
    Spec{":op/sub_i", CppOperatorEmitKind::InlineBinary, 2, 0, "-"},
    Spec{":op/sub_l", CppOperatorEmitKind::InlineBinary, 2, 0, "-"},
    Spec{":op/sub_f", CppOperatorEmitKind::InlineBinary, 2, 0, "-"},
    Spec{":op/sub_d", CppOperatorEmitKind::InlineBinary, 2, 0, "-"},
    Spec{":op/mul_i", CppOperatorEmitKind::InlineBinary, 2, 0, "*"},
    Spec{":op/mul_l", CppOperatorEmitKind::InlineBinary, 2, 0, "*"},
    Spec{":op/mul_f", CppOperatorEmitKind::InlineBinary, 2, 0, "*"},
    Spec{":op/mul_d", CppOperatorEmitKind::InlineBinary, 2, 0, "*"},
    Spec{":op/div_i", CppOperatorEmitKind::InlineBinary, 2, 0, "/"},
    Spec{":op/div_l", CppOperatorEmitKind::InlineBinary, 2, 0, "/"},
    Spec{":op/div_f", CppOperatorEmitKind::InlineBinary, 2, 0, "/"},
    Spec{":op/div_d", CppOperatorEmitKind::InlineBinary, 2, 0, "/"},
    Spec{":op/lt_i", CppOperatorEmitKind::InlineBinary, 2, 0, "<"},
    Spec{":op/lt_l", CppOperatorEmitKind::InlineBinary, 2, 0, "<"},
    Spec{":op/lt_f", CppOperatorEmitKind::InlineBinary, 2, 0, "<"},
    Spec{":op/lt_d", CppOperatorEmitKind::InlineBinary, 2, 0, "<"},
    Spec{":op/gt_i", CppOperatorEmitKind::InlineBinary, 2, 0, ">"},
    Spec{":op/gt_l", CppOperatorEmitKind::InlineBinary, 2, 0, ">"},
    Spec{":op/gt_f", CppOperatorEmitKind::InlineBinary, 2, 0, ">"},
    Spec{":op/gt_d", CppOperatorEmitKind::InlineBinary, 2, 0, ">"},
    Spec{":op/le_i", CppOperatorEmitKind::InlineBinary, 2, 0, "<="},
    Spec{":op/le_l", CppOperatorEmitKind::InlineBinary, 2, 0, "<="},
    Spec{":op/le_f", CppOperatorEmitKind::InlineBinary, 2, 0, "<="},
    Spec{":op/le_d", CppOperatorEmitKind::InlineBinary, 2, 0, "<="},
    Spec{":op/ge_i", CppOperatorEmitKind::InlineBinary, 2, 0, ">="},
    Spec{":op/ge_l", CppOperatorEmitKind::InlineBinary, 2, 0, ">="},
    Spec{":op/ge_f", CppOperatorEmitKind::InlineBinary, 2, 0, ">="},
    Spec{":op/ge_d", CppOperatorEmitKind::InlineBinary, 2, 0, ">="},
    Spec{":op/eq_i", CppOperatorEmitKind::InlineBinary, 2, 0, "=="},
    Spec{":op/eq_l", CppOperatorEmitKind::InlineBinary, 2, 0, "=="},
    Spec{":op/eq_f", CppOperatorEmitKind::InlineBinary, 2, 0, "=="},
    Spec{":op/eq_d", CppOperatorEmitKind::InlineBinary, 2, 0, "=="},
    Spec{":op/eq_b", CppOperatorEmitKind::InlineBinary, 2, 0, "=="},
    Spec{":op/ne_i", CppOperatorEmitKind::InlineBinary, 2, 0, "!="},
    Spec{":op/ne_l", CppOperatorEmitKind::InlineBinary, 2, 0, "!="},
    Spec{":op/ne_f", CppOperatorEmitKind::InlineBinary, 2, 0, "!="},
    Spec{":op/ne_d", CppOperatorEmitKind::InlineBinary, 2, 0, "!="},
    Spec{":op/ne_b", CppOperatorEmitKind::InlineBinary, 2, 0, "!="},
    Spec{":op/not", CppOperatorEmitKind::InlineUnary, 1, 0, "!"},
    Spec{":op/neg_i", CppOperatorEmitKind::InlineUnary, 1, 0, "-"},
    Spec{":op/neg_l", CppOperatorEmitKind::InlineUnary, 1, 0, "-"},
    Spec{":op/neg_f", CppOperatorEmitKind::InlineUnary, 1, 0, "-"},
    Spec{":op/neg_d", CppOperatorEmitKind::InlineUnary, 1, 0, "-"},
    Spec{":op/inv_i", CppOperatorEmitKind::InlineUnary, 1, 0, "~"},
    Spec{":op/inv_l", CppOperatorEmitKind::InlineUnary, 1, 0, "~"},
    Spec{":op/itoi", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ltoi", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ftoi", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/dtoi", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/itol", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ltol", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ftol", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/dtol", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/itof", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ltof", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ftof", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/dtof", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/itod", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ltod", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ftod", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/dtod", CppOperatorEmitKind::NumericCast, 1, 0, ""},
};

} // namespace

const CppOperatorSpec *findCppOperatorSpec(const OperNode *node) {
    const std::string &uri = node->oper()->uri();
    for (const auto &spec : kOperatorSpecs) {
        if (spec.uri == uri) {
            return &spec;
        }
    }
    return nullptr;
}

bool canRuntimeFallbackOperatorForCpp(const OperNode *node) {
    if (findCppOperatorSpec(node) != nullptr) {
        return true;
    }
    if (!hasBridgeableOutput(node) || !hasBridgeableInputs(node)) {
        return false;
    }
    return true;
}

GraphLoweringPlan analyzeGraphForCpp(Graph *graph) {
    GraphLoweringPlan plan;
    plan.graph     = graph;
    plan.topoNodes = buildTopoNodes(graph);

    for (const auto &port : graph->normPorts()) {
        if (!isPureScalarTypeForCpp(port->dataType())) {
            addIssue(plan, port, "non-scalar norm port requires runtime frame");
        }
    }
    for (const auto &port : graph->withPorts()) {
        if (!isPureScalarTypeForCpp(port->dataType())) {
            addIssue(plan, port, "non-scalar with port requires runtime frame");
        }
    }
    for (const auto &port : graph->closure()) {
        addIssue(plan, port, "closure capture is not supported by direct C++ lowering");
    }
    if (!isPureScalarTypeForCpp(graph->funcType()->exitType())) {
        addIssue(plan, graph->exitNode(), "non-scalar return type requires runtime frame");
    }

    for (Node *node : plan.topoNodes) {
        switch (node->type()) {
        case NodeType::DATA: {
            auto *dataNode = tt::as_ptr<DataNode>(node);
            if (!cppLiteralFor(dataNode->dataSlot(), dataNode->dataType()).has_value()) {
                addIssue(plan, node, "data literal cannot be emitted as a C++ literal");
            }
        } break;

        case NodeType::PORT:
            break;

        case NodeType::OPER: {
            auto *opNode = tt::as_ptr<OperNode>(node);
            if (!hasBridgeableOutput(opNode) || !hasBridgeableInputs(opNode)) {
                addIssue(
                    plan,
                    node,
                    "operator uses types that cannot be bridged through C++ lowering");
                break;
            }
            if (!canRuntimeFallbackOperatorForCpp(opNode)) {
                addIssue(
                    plan,
                    node,
                    "operator cannot be lowered or bridged through helper fallback");
            }
        } break;

        case NodeType::BRCH:
            if (!tt::as_ptr<BrchNode>(node)->hasSelectorInput() ||
                !tt::as_ptr<BrchNode>(node)->caseInputs().empty() ||
                tt::as_ptr<BrchNode>(node)->armCount() != 2) {
                addIssue(plan, node, "only two-way scalar if-else branches are supported");
            }
            break;

        case NodeType::JOIN:
            if (!tt::as_ptr<JoinNode>(node)->hasBranchIndexInput() ||
                !tt::as_ptr<JoinNode>(node)->hasMatchedBranch() ||
                tt::as_ptr<JoinNode>(node)->armCount() != 2) {
                addIssue(plan, node, "join must be driven by a matching two-way BRCH");
            }
            break;

        case NodeType::FUNC: {
            auto *funcNode = tt::as_ptr<FuncNode>(node);
            Graph *callee  = funcNode->bodyGraph();
            if (callee->hasClosure()) {
                addIssue(plan, node, "direct function lowering does not support closures");
                break;
            }
            for (const auto &port : callee->normPorts()) {
                if (!isPureScalarTypeForCpp(port->dataType())) {
                    addIssue(plan, node, "callee norm port is not scalar");
                }
            }
            for (const auto &port : callee->withPorts()) {
                if (!isPureScalarTypeForCpp(port->dataType())) {
                    addIssue(plan, node, "callee with port is not scalar");
                }
            }
            if (!isPureScalarTypeForCpp(callee->funcType()->exitType())) {
                addIssue(plan, node, "callee return type is not scalar");
            }
        } break;

        case NodeType::CAST:
        case NodeType::COPY:
            if (!hasBridgeableOutput(node) || !hasBridgeableInputs(node)) {
                addIssue(plan, node, "COPY/CAST requires C++-bridgeable inputs and outputs");
            }
            break;

        case NodeType::GATE:
            if (!hasBridgeableOutput(node) || !hasBridgeableInputs(node)) {
                addIssue(plan, node, "GATE requires C++-bridgeable inputs and outputs");
                break;
            }
            if (node->normInputs().empty()) {
                addIssue(plan, node, "GATE must have at least one Norm input");
                break;
            }
            if (node->ctrlInputs().empty()) {
                addIssue(plan, node, "GATE must have at least one Ctrl input");
                break;
            }
            break;

        case NodeType::FILL:
        case NodeType::ACCS:
        case NodeType::CALL:
        case NodeType::BIND:
        case NodeType::SYNC:
        case NodeType::DREF:
            addIssue(plan, node, "node type is not supported by the direct C++ path yet");
            break;

        default:
            addIssue(plan, node, "unknown node type");
            break;
        }
    }

    plan.frameElidable  = plan.issues.empty();
    plan.directCallable = plan.frameElidable;
    return plan;
}
