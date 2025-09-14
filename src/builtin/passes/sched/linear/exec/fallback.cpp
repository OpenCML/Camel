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
 * Created: Sep. 08, 2025
 * Updated: Sep. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "fallback.h"
#include "builtin/algo/topo.h"

using namespace std;
using namespace GIR;

data_ptr_t FallbackExecSchedPass::evalGraph(const graph_ptr_t &graph, arena_ptr_t &frame) {
    shared_ptr<node_vec_t> sortedNodesPtr;
    if (graphTNS_.find(graph.get()) == graphTNS_.end()) {
        auto sortedNodes = topoSort(
            graph->nodes().begin(),
            graph->nodes().end(),
            [](const auto &n) { return n->inDegree(); },
            [](const auto &n) {
                vector<node_ptr_t> outs;
                outs.insert(outs.end(), n->dataOutputs().begin(), n->dataOutputs().end());
                outs.insert(outs.end(), n->ctrlOutputs().begin(), n->ctrlOutputs().end());
                return outs;
            });
        sortedNodesPtr = std::make_shared<node_vec_t>(std::move(sortedNodes));
        graphTNS_[graph.get()] = sortedNodesPtr;
    } else {
        sortedNodesPtr = graphTNS_[graph.get()];
    }
    // 按拓扑序执行
    for (const auto &n : *sortedNodesPtr) {
        // 用于跳过被标记为不执行的节点（分支功能）
        if (!brInfoStack_.empty() && n == brInfoStack_.top().second) {
            continue;
        }
        switch (n->type()) {
        case NodeType::Function: {
            auto func = tt::as_shared<FunctionNode>(n)->func();
            auto newFrame = func->arena()->clone();
            auto subGraph = func->graph();
            const auto &dataInputs = n->dataInputs();
            const auto &portIndices = subGraph->portIndices();
            ASSERT(
                dataInputs.size() == portIndices.size(),
                "Function node input size does not match function graph port size.");
            for (size_t i = 0; i < portIndices.size(); ++i) {
                newFrame->set(portIndices[i], frame->get(dataInputs[i]->index()));
            }
            data_ptr_t res = evalGraph(subGraph, newFrame);
            frame->set(n->index(), res);
            break;
        }
        case NodeType::Operator: {
            auto opNode = tt::as_shared<OperatorNode>(n);
            const auto &uri = opNode->oper()->uri();

            data_vec_t withArgs, normArgs;
            withArgs.reserve(n->withInputs().size());
            normArgs.reserve(n->normInputs().size());

            for (const auto &input : n->withInputs()) {
                withArgs.push_back(frame->get(input->index()));
            }
            for (const auto &input : n->normInputs()) {
                normArgs.push_back(frame->get(input->index()));
            }

            data_ptr_t res = context_->eval(uri, withArgs, normArgs);
            frame->set(n->index(), res);
            break;
        }
        case NodeType::Select: {
            auto selectNode = tt::as_shared<SelectNode>(n);
            auto selectType = selectNode->selectType();
            if (selectType == SelectNode::SelectType::Branch) {
                // 判断输入条件，并将不执行的节点放入跳过栈
                ASSERT(
                    n->dataInputs().size() == 1,
                    "Branch node must have exactly one data input.");
                ASSERT(n->ctrlOutputs().size() == 2, "Branch node must have exactly two outputs.");
                auto condDataRaw = frame->get(n->dataInputs().front()->index());
                auto condData = condDataRaw->as<BoolData>(Type::Bool());
                bool cond = condData->data();
                if (cond) {
                    // True branch, skip the second output
                    brInfoStack_.push({0, n->ctrlOutputs().back()});
                } else {
                    // False branch, skip the first output
                    brInfoStack_.push({1, n->ctrlOutputs().front()});
                }
            } else {
                ASSERT(n->dataInputs().size() == 2, "Join node must have exactly two data inputs.");
                size_t idx = brInfoStack_.top().first;
                brInfoStack_.pop();
                auto data = frame->get(n->dataInputs()[idx]->index());
                frame->set(n->index(), data);
            }
            break;
        }
        case NodeType::Struct: {
            auto structNode = tt::as_shared<StructNode>(n);
            const auto &dataInputs = n->dataInputs();
            data_ptr_t data = frame->get(n->index());
            ASSERT(data != nullptr, "Struct data is null.");
            data_vec_t inputs;
            inputs.reserve(dataInputs.size());
            for (const auto &input : dataInputs) {
                inputs.push_back(frame->get(input->index()));
            }
            data->resolve(inputs);
            frame->set(n->index(), data);
            break;
        }
        case NodeType::Access: {
            auto accessNode = tt::as_shared<AccessNode>(n);
            data_ptr_t source = frame->get(n->dataInputs().front()->index());
            data_ptr_t res;
            ASSERT(false, "Access node evaluation not implemented yet.");
            break;
        }
        case NodeType::Source: {
            auto sourceNode = tt::as_shared<SourceNode>(n);
            data_ptr_t data = sourceNode->dataOf(frame);
            ASSERT(data != nullptr, "Source data is null.");
            break;
        }
        }
    }

    // 返回图的返回节点数据
    auto retNode = graph->output();
    ASSERT(retNode != nullptr, "Graph has no return node.");
    return frame->get(retNode->index());
}

any FallbackExecSchedPass::apply(const graph_ptr_t &graph) {
    auto arena = graph->arena()->clone();
    return evalGraph(graph, arena);
}