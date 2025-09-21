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
 * Updated: Sep. 21, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "fallback.h"
#include "builtin/algo/topo.h"
#include "utils/debug.h"

using namespace std;
using namespace GraphIR;

std::shared_ptr<node_vec_t> FallbackExecSchedPass::getTopoNodes(const graph_ptr_t &graph) {
    if (graphTopoNodesCache_.find(graph.get()) == graphTopoNodesCache_.end()) {
        node_ptr_t retNode = graph->returnNode();
        auto sortedNodes = findReachable(
            retNode,
            [](const node_ptr_t &n) {
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
            },
            true // skip the start node itself
        );
        EXEC_WHEN_DEBUG([&]() {
            if (sortedNodes.size() != graph->nodes().size() - 1) {
                GraphIR::node_vec_t unreachableNodes;
                for (const auto &n : graph->nodes()) {
                    if (std::find(sortedNodes.begin(), sortedNodes.end(), n) == sortedNodes.end()) {
                        unreachableNodes.push_back(n);
                    }
                }
                std::string nodeStrs;
                for (const auto &node : unreachableNodes) {
                    nodeStrs += node->toString() + ", ";
                }
                l.in("Topo").warn(
                    "Unreachable nodes in graph {} detected: {}",
                    graph->name(),
                    nodeStrs);
            }
        }());
        const auto &sortedNodesPtr = std::make_shared<node_vec_t>(std::move(sortedNodes));
        graphTopoNodesCache_[graph.get()] = sortedNodesPtr;
        return sortedNodesPtr;
    } else {
        return graphTopoNodesCache_[graph.get()];
    }
}

data_ptr_t FallbackExecSchedPass::evalGraph(const graph_ptr_t &graph, frame_ptr_t &frame) {
    l.in("Eval").debug("Evaluating graph: {}", graph->name());
    if (currRecursionDepth_++ > maxRecursionDepth_) {
        context_->rtmDiags()->of(RuntimeDiag::MaxRecursionDepthExceeded).commit(graph->name());
    }

    bool loop = false;
    auto nodesPtr = getTopoNodes(graph);
    frame_ptr_t currFrame = frame, tailFrame = nullptr;

    auto evalFuncNode = [&](const node_ptr_t &n, bool isTailCall) {
        auto func = tt::as_shared<FunctionNode>(n)->func();
        auto tgtGraph = func->graph();
        frame_ptr_t nextFrame;

        data_vec_t args;
        const auto &inNodes = n->dataInputs();
        args.reserve(inNodes.size());
        for (const auto &inNode : inNodes) {
            args.push_back(currFrame->get(inNode));
        }

        const auto &portNodes = tgtGraph->portNodes();
        ASSERT(
            inNodes.size() == portNodes.size(),
            "Function node input size does not match function graph port size.");

        if (isTailCall) {
            // Tail-call optimization
            loop = true;
            frame_ptr_t lastFrame = currFrame;

            if (tgtGraph == currFrame->graph()) {
                // Self-recursion optimization
                currFrame = lastFrame;
                l.in("Eval").debug(
                    "Optimizing self-recursion for graph: {}",
                    currFrame->graph()->name());
            } else {
                nodesPtr = getTopoNodes(tgtGraph);

                if (tailFrame && tailFrame->graph() == tgtGraph) {
                    // Mutual-tail-recursion optimization
                    l.in("Eval").debug(
                        "Optimizing mutual-tail-recursion for graph: {}",
                        currFrame->graph()->name());
                    currFrame = tailFrame;
                } else {
                    currFrame = Frame::create(currFrame, tgtGraph);
                }
            }

            tailFrame = lastFrame;
            nextFrame = currFrame;

            // clear the frame for re-use
            // note: here the nextFrame may be the currFrame
            // so we need to reset it after retrieving the args
            nextFrame->reset();

            for (size_t i = 0; i < portNodes.size(); ++i) {
                nextFrame->set(portNodes[i].first, args[i]);
            }
        } else {
            nextFrame = Frame::create(currFrame, tgtGraph);

            for (size_t i = 0; i < portNodes.size(); ++i) {
                nextFrame->set(portNodes[i].first, args[i]);
            }

            // evaluate the target graph
            data_ptr_t res = evalGraph(tgtGraph, nextFrame);
            currFrame->set(n, res);
        }
    };

    // for tail-call optimization
    // reuse the current frame for tail-recursive calls
    do {
        loop = false;
        auto &nodes = *nodesPtr;
        // 按拓扑序执行
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto &n = nodes[i];
            switch (n->type()) {
            case NodeType::Function: {
                evalFuncNode(n, i == nodes.size() - 1);
                break;
            }
            case NodeType::Operator: {
                auto opNode = tt::as_shared<OperatorNode>(n);
                const auto &uri = opNode->oper()->uri();

                if (uri.starts_with(":mark/")) {
                    if (uri == ":mark/map") {
                    } else if (uri == ":mark/filter") {
                        // ASSERT(normArgs.size() == 2, "filter operator requires 2 norm args.");
                        // auto listData = normArgs[0]->as<ListData>(Type::List());
                        // auto funcData = normArgs[1]->as<FunctionData>(Type::Func());
                        // data_ptr_t res = listData->filter(funcData, context_, currFrame);
                        // currFrame->set(n, res);
                    } else if (uri == ":mark/foreach") {
                        // ASSERT(normArgs.size() == 2, "foreach operator requires 2 norm args.");
                        // auto listData = normArgs[0]->as<ListData>(Type::List());
                        // auto funcData = normArgs[1]->as<FunctionData>(Type::Func());
                        // listData->foreach(funcData, context_, currFrame);
                        // currFrame->set(n, Data::null());
                    } else {
                        ASSERT(false, std::format("Mark Operator {} not implemented.", uri));
                    }
                    break;
                }

                context_->eval(uri, n, *currFrame);
                break;
            }
            case NodeType::Select: {
                // BRCH node
                auto selectNode = tt::as_shared<SelectNode>(n);
                auto selectType = selectNode->selectType();
                if (selectType == SelectNode::SelectType::Branch) {
                    // 判断输入条件，并将执行的节点放入Info栈
                    ASSERT(
                        n->dataInputs().size() == 1,
                        "Branch node must have exactly one data input.");
                    ASSERT(
                        n->ctrlOutputs().size() == 2,
                        "Branch node must have exactly two outputs.");
                    auto condDataRaw = currFrame->get(n->dataInputs().front());
                    auto condData = condDataRaw->as<BoolData>(Type::Bool());
                    bool cond = condData->data();
                    if (cond) {
                        brInfoStack_.push(n->ctrlOutputs().front());
                    } else {
                        brInfoStack_.push(n->ctrlOutputs().back());
                    }
                    i += 2; // skip the two branches
                } else {
                    // JOIN node
                    ASSERT(
                        n->ctrlInputs().size() == 2,
                        "Join node must have exactly two ctrl inputs.");
                    auto execNode = brInfoStack_.top();
                    brInfoStack_.pop();
                    if (i == nodes.size() - 1) {
                        // Here, the branch function node is delayed to be executed at the JOIN
                        // node, which facilitates tail-call optimization. This is because when the
                        // JOIN node is the last node of the current execution sequence, the
                        // function call can be optimized as a tail-call. If the branch function
                        // call is executed earlier, it is difficult to determine whether it is a
                        // tail-call.
                        evalFuncNode(execNode, true);
                    } else {
                        currFrame->set(n, currFrame->get(execNode));
                    }
                }
                break;
            }
            case NodeType::Struct: {
                auto structNode = tt::as_shared<StructNode>(n);
                const auto &dataInputs = n->dataInputs();
                data_ptr_t data = currFrame->get(n);
                ASSERT(data != nullptr, "Struct data is null.");
                data_vec_t inputs;
                inputs.reserve(dataInputs.size());
                for (const auto &input : dataInputs) {
                    inputs.push_back(currFrame->get(input));
                }
                data->resolve(inputs);
                currFrame->set(n, data);
                break;
            }
            case NodeType::Access: {
                ASSERT(false, "Access node evaluation not implemented yet.");
                auto accessNode = tt::as_shared<AccessNode>(n);
                data_ptr_t source = currFrame->get(n->dataInputs().front());
                data_ptr_t res;
                break;
            }
            case NodeType::Source: {
                data_ptr_t data = currFrame->get(n);
                ASSERT(data != nullptr, "Source data is null.");
                break;
            }
            default:
                ASSERT(false, "Unknown node type.");
            }
        }
    } while (loop);

    currRecursionDepth_--;

    const auto &retNode = currFrame->graph()->returnNode();
    ASSERT(retNode->withInputs().size() == 0, "Return node cannot have with inputs.");
    ASSERT(retNode->normInputs().size() <= 1, "Return node cannot have multiple norm inputs.");
    const auto &input = retNode->normInputs();
    if (input.empty()) {
        return Data::null();
    } else {
        return currFrame->get(input.front());
    }
}

any FallbackExecSchedPass::apply(const graph_ptr_t &graph) {
    auto rootFrame = Frame::create(nullptr, graph);
    auto optMainGraph = graph->getSubGraph("main");
    ASSERT(optMainGraph.has_value(), "Main graph not found.");
    auto mainGraph = optMainGraph.value();
    auto mainFrame = Frame::create(rootFrame, mainGraph);
    return evalGraph(mainGraph, mainFrame);
}

void FallbackExecSchedPass::evalMarkedOperatorMap(const node_ptr_t &n, frame_ptr_t &currFrame) {
    if (n->withInputs().size() != 1) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<map>", 1, n->withInputs().size());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }
    if (n->normInputs().size() != 1) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<map>", 1, n->normInputs().size());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidNormParameter, "Incorrect args.");
    }
    auto targetData = currFrame->get(n->withInputs().front());
    auto funcData = currFrame->get(n->normInputs().front());
    if (funcData->type()->code() != TypeCode::Func) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<map>", "Function", funcData->type()->toString());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidNormParameter, "Incorrect args.");
    }
    switch (targetData->type()->code()) {
    case TypeCode::List: {
        auto listData = targetData->as<ListData>(Type::List());
        data_vec_t &target = listData->raw();
        data_vec_t res;
        res.reserve(target.size());
        for (const auto &item : target) {
            frame_ptr_t mapFrame =
                Frame::create(currFrame, funcData->as<FunctionData>(Type::Func())->graph());
            mapFrame->set(
                funcData->as<FunctionData>(Type::Func())->graph()->portNodes().front().first,
                item);
            data_ptr_t mapRes =
                evalGraph(funcData->as<FunctionData>(Type::Func())->graph(), mapFrame);
            res.push_back(mapRes);
        }
        // currFrame->set(n, ListData::create(listData->elementType(), res));
        break;
    }

    default:
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<map>", "List", funcData->type()->toString());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }
}
