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

std::shared_ptr<GIR::node_vec_t>
FallbackExecSchedPass::getTopoNodes(const GIR::graph_ptr_t &graph) {
    if (graphTopoNodesCache_.find(graph.get()) == graphTopoNodesCache_.end()) {
        node_ptr_t retNode = graph->outputNode();
        auto sortedNodes = findReachable(retNode, [](const node_ptr_t &n) {
            vector<node_ptr_t> ins;
            ins.reserve(n->dataInputs().size() + n->ctrlInputs().size());
            for (const auto &in : n->dataInputs()) {
                if (in->graph() == n->graph()) // only consider nodes in the same graph
                    ins.push_back(in);
            }
            for (const auto &in : n->ctrlInputs()) {
                if (in->graph() == n->graph()) // only consider nodes in the same graph
                    ins.push_back(in);
            }
            return ins;
        });
        if (sortedNodes.size() != graph->nodes().size()) {
            l.in("Topo").warn(
                "Topological sort found {} reachable nodes, but graph {} has {} nodes.",
                sortedNodes.size(),
                graph->name(),
                graph->nodes().size());
        }
        l.in("Topo")
            .debug("Topological order for graph {}, {} nodes:", graph->name(), sortedNodes.size());
        for (auto &n : sortedNodes) {
            l.in("Topo").debug("{}", n->toString());
        }
        const auto &sortedNodesPtr = std::make_shared<node_vec_t>(std::move(sortedNodes));
        graphTopoNodesCache_[graph.get()] = sortedNodesPtr;
        return sortedNodesPtr;
    } else {
        return graphTopoNodesCache_[graph.get()];
    }
}

data_ptr_t FallbackExecSchedPass::evalGraph(const graph_ptr_t &graph, frame_ptr_t &frame) {
    l.in("Eval").debug("Evaluating graph: {}", graph->name());
    data_ptr_t result;
    // 按拓扑序执行
    for (const auto &n : *getTopoNodes(graph)) {
        // 用于跳过被标记为不执行的节点（分支功能）
        if (!brInfoStack_.empty() && n == brInfoStack_.top().second) {
            continue;
        }
        switch (n->type()) {
        case NodeType::Function: {
            auto func = tt::as_shared<FunctionNode>(n)->func();
            auto subGraph = func->graph();
            auto newFrame = Frame::create(frame, subGraph);
            const auto &inNodes = n->dataInputs();
            const auto &ports = subGraph->portNodes();
            ASSERT(
                inNodes.size() == ports.size(),
                "Function node input size does not match function graph port size.");
            for (size_t i = 0; i < ports.size(); ++i) {
                newFrame->set(ports[i].first, frame->get(inNodes[i]));
            }
            data_ptr_t res = evalGraph(subGraph, newFrame);
            frame->set(n, res);
            break;
        }
        case NodeType::Operator: {
            auto opNode = tt::as_shared<OperatorNode>(n);
            const auto &uri = opNode->oper()->uri();

            data_vec_t withArgs, normArgs;
            withArgs.reserve(n->withInputs().size());
            normArgs.reserve(n->normInputs().size());

            for (const auto &inNode : n->withInputs()) {
                withArgs.push_back(frame->get(inNode));
            }
            for (const auto &inNode : n->normInputs()) {
                normArgs.push_back(frame->get(inNode));
            }

            data_ptr_t res = context_->eval(uri, withArgs, normArgs);
            frame->set(n, res);
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
                auto condDataRaw = frame->get(n->dataInputs().front());
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
                ASSERT(n->ctrlInputs().size() == 2, "Join node must have exactly two ctrl inputs.");
                size_t idx = brInfoStack_.top().first;
                brInfoStack_.pop();
                auto data = frame->get(n->ctrlInputs()[idx]);
                frame->set(n, data);
            }
            break;
        }
        case NodeType::Struct: {
            auto structNode = tt::as_shared<StructNode>(n);
            const auto &dataInputs = n->dataInputs();
            data_ptr_t data = frame->get(n);
            ASSERT(data != nullptr, "Struct data is null.");
            data_vec_t inputs;
            inputs.reserve(dataInputs.size());
            for (const auto &input : dataInputs) {
                inputs.push_back(frame->get(input));
            }
            data->resolve(inputs);
            frame->set(n, data);
            break;
        }
        case NodeType::Access: {
            ASSERT(false, "Access node evaluation not implemented yet.");
            auto accessNode = tt::as_shared<AccessNode>(n);
            data_ptr_t source = frame->get(n->dataInputs().front());
            data_ptr_t res;
            break;
        }
        case NodeType::Source: {
            data_ptr_t data = frame->get(n);
            ASSERT(data != nullptr, "Source data is null.");
            break;
        }
        case NodeType::Return: {
            ASSERT(n->withInputs().size() == 0, "Return node cannot have with inputs.");
            const auto &input = n->dataInputs();
            if (input.empty()) {
                result = Data::null();
            } else {
                result = frame->get(input.front());
            }
            break;
        }
        }
    }

    ASSERT(result != nullptr, "Graph evaluation did not produce a result.");
    l.in("Eval").debug(
        "Graph {} evaluation completed with returned value {}.",
        graph->name(),
        result->toString());
    return result;
}

any FallbackExecSchedPass::apply(const graph_ptr_t &graph) {
    auto rootFrame = Frame::create(nullptr, graph);
    auto optMainGraph = graph->getSubGraph("main");
    ASSERT(optMainGraph.has_value(), "Main graph not found.");
    auto mainGraph = optMainGraph.value();
    auto mainFrame = Frame::create(rootFrame, mainGraph);
    return evalGraph(mainGraph, mainFrame);
}