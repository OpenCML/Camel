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
 * Updated: Oct. 31, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "nodevm.h"
#include "builtin/algo/topo.h"
#include "core/data/primary.h"

#ifndef NDEBUG
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"
#endif

using namespace std;
using namespace GraphIR;

std::shared_ptr<node_vec_t> NodeVMSchedPass::getTopoNodes(Graph *graph) {
    ASSERT(
        !graph->dirty(),
        std::format("Graph {} is dirty, please rearrange before precompiling.", graph->name()));

    if (graphTopoNodesCache_.find(graph) == graphTopoNodesCache_.end()) {
        node_ptr_t exitNode = graph->exitNode();
        auto sortedNodes = findReachable(
            exitNode,
            [](const node_ptr_t &n) {
                vector<node_ptr_t> ins;
                ins.reserve(n->dataInputs().size() + n->ctrlInputs().size());
                for (const auto &in : n->ctrlInputs()) {
                    if (&in->graph() == &n->graph()) // only consider nodes in the same graph
                        ins.push_back(in);
                }
                // Put value computation nodes at the back for correct tail-call optimization
                for (const auto &in : n->dataInputs()) {
                    if (&in->graph() == &n->graph()) // only consider nodes in the same graph
                        ins.push_back(in);
                }
                return ins;
            },
            true // skip the start node itself
        );

        EXEC_WHEN_DEBUG([&]() {
            l.in("Topo").debug("Topologically sorted nodes for graph {}:", graph->name());
            for (const auto &n : sortedNodes) {
                l.in("Topo").debug("  {}", n->toString());
            }
            size_t totalNodeCnt =
                graph->nodes().size() + graph->ports().size() + graph->closure().size();
            if (sortedNodes.size() != totalNodeCnt) {
                GraphIR::node_vec_t unreachableNodes;
                for (const auto &n : graph->nodes()) {
                    if (n != exitNode &&
                        std::find(sortedNodes.begin(), sortedNodes.end(), n) == sortedNodes.end()) {
                        unreachableNodes.push_back(n);
                    }
                }
                std::string nodeStrs;
                for (const auto &node : unreachableNodes) {
                    if (!nodeStrs.empty()) {
                        nodeStrs += ", ";
                    }
                    nodeStrs += node->toString();
                }
                l.in("Topo").warn(
                    "Unreachable nodes in graph {} detected: {}",
                    graph->name(),
                    nodeStrs);
            }
        }());

        const auto &sortedNodesPtr = std::make_shared<node_vec_t>(std::move(sortedNodes));
        graphTopoNodesCache_[graph] = sortedNodesPtr;
        return sortedNodesPtr;
    } else {
        return graphTopoNodesCache_[graph];
    }
}

data_ptr_t NodeVMSchedPass::evalGraph(Graph *graph, Frame &frame) {
    EXEC_WHEN_DEBUG(l.in("Eval").debug("Evaluating graph: {}", graph->name()));

    EXEC_WHEN_DEBUG([&]() {
        if (profiler::AdvancedTracer::getInstance().isTracing()) {
            profiler::AdvancedTracer::getInstance().traceFunctionCall("evalGraph:" + graph->name());
        }
    }());

    if (currRecursionDepth_++ > maxRecursionDepth_) {
        context_->rtmDiags()->of(RuntimeDiag::MaxRecursionDepthExceeded).commit(graph->name());
    }

    bool loop = false;
    auto nodesPtr = getTopoNodes(graph);

    Frame targetFrame(graph);
    frame_rptr_t currFrame = &frame, tailFrame = nullptr;

    auto evalFuncNode = [&](const node_ptr_t &n, bool isTailCall) {
        auto func = tt::as_shared<FuncNode>(n)->func();
        auto &targetGraph = func->graph();
        EXEC_WHEN_DEBUG(l.in("Eval").debug(
            "Calling function: {} (tail-call: {})",
            func->name().empty() ? targetGraph.name() : func->name(),
            isTailCall ? "yes" : "no"));
        frame_rptr_t nextFrame = nullptr;

        data_vec_t args;
        const auto &inNodes = n->dataInputs();
        args.reserve(inNodes.size());
        for (const auto &inNode : inNodes) {
            args.push_back(currFrame->get(inNode->index()));
        }

        auto portNodes = targetGraph.ports();
        ASSERT(
            inNodes.size() == portNodes.size(),
            std::format(
                "Function {} expects {} arguments, but got {}.",
                func->name().empty() ? targetGraph.name() : func->name(),
                portNodes.size(),
                inNodes.size()));

        if (isTailCall) {
            // Tail-call optimization
            loop = true;
            frame_rptr_t lastFrame = currFrame;

            if (&targetGraph == currFrame->graph()) {
                // Self-recursion optimization
                currFrame = lastFrame;
                EXEC_WHEN_DEBUG(l.in("Eval").debug(
                    "Optimizing self-recursion for graph: {}",
                    currFrame->graph()->name()));
            } else {
                nodesPtr = getTopoNodes(&targetGraph);

                if (tailFrame && tailFrame->graph() == &targetGraph) {
                    // Mutual-tail-recursion optimization
                    EXEC_WHEN_DEBUG(l.in("Eval").debug(
                        "Optimizing mutual-tail-recursion for graph: {}",
                        currFrame->graph()->name()));
                    currFrame = tailFrame;
                } else {
                    targetFrame = Frame(&targetGraph);
                    currFrame = &targetFrame;
                }
            }

            tailFrame = lastFrame;
            nextFrame = currFrame;

            // clear the frame for re-use
            // note: here the nextFrame may be the currFrame
            // so we need to reset it after retrieving the args
            EXEC_WHEN_DEBUG(nextFrame->reset());

            for (size_t i = 0; i < portNodes.size(); ++i) {
                nextFrame->set(portNodes[i]->index(), args[i]);
            }
        } else {
            Frame tmpFrame(&targetGraph);
            nextFrame = &tmpFrame;

            for (size_t i = 0; i < portNodes.size(); ++i) {
                nextFrame->set(portNodes[i]->index(), args[i]);
            }

            // evaluate the target graph
            data_ptr_t res = evalGraph(&targetGraph, *nextFrame);
            currFrame->set(n->index(), res);
        }
    };

    // for tail-call optimization
    // reuse the current frame for tail-recursive calls
    do {
        loop = false;
        auto &nodes = *nodesPtr;

        EXEC_WHEN_DEBUG([&]() {
            if (profiler::AdvancedTracer::getInstance().isTracing()) {
                profiler::AdvancedTracer::getInstance().traceFunctionCall(
                    "topoExecution:" + graph->name());
            }
        }());

        for (size_t i = 0; i < nodes.size(); ++i) {
            auto &n = nodes[i];

            EXEC_WHEN_DEBUG([&]() {
                if (profiler::AdvancedTracer::getInstance().isTracing()) {
                    std::string nodeName = "node:" + n->toString();
                    profiler::AdvancedTracer::getInstance().traceFunctionCall(nodeName);
                }
            }());

            switch (n->type()) {

            case NodeType::CAST: {
                ASSERT(false, "CAST node not implemented yet.");
                break;
            }

            case NodeType::COPY: {
                auto inputNode = n->normInputs().front();
                auto inputData = currFrame->get(inputNode->index());
                if (inputData == nullptr) {
                    // If input data is not initialized, create a default integer value 0
                    // For counters in recursive functions, this is typically an integer type
                    inputData = std::make_shared<IntData>(0);
                    currFrame->set(inputNode->index(), inputData);
                }
                currFrame->set(n->index(), inputData->clone());
                break;
            }

            case NodeType::FILL: {
                auto structNode = tt::as_shared<FillNode>(n);
                const auto &srcNode = n->normInputs().front();
                const auto &dataInputs = n->withInputs();
                data_ptr_t data = currFrame->get(srcNode->index())->clone();
                ASSERT(data != nullptr, "FILL data is null.");
                data_vec_t inputs;
                inputs.reserve(dataInputs.size());
                for (const auto &input : dataInputs) {
                    inputs.push_back(currFrame->get(input->index()));
                }
                data->resolve(inputs);
                currFrame->set(n->index(), data);
                break;
            }

            case NodeType::ACCS: {
                data_ptr_t source = currFrame->get(n->dataInputs().front()->index());
                auto accessNode = tt::as_shared<AccsNode>(n);
                if (accessNode->isNum()) {
                    size_t idx = accessNode->index<size_t>();
                    auto tupleData = tt::as_shared<TupleData>(source);
                    ASSERT(idx < tupleData->raw().size(), "Tuple index out of bounds.");
                    currFrame->set(n->index(), tupleData->raw()[idx]);
                } else {
                    std::string key = accessNode->index<std::string>();
                    auto structData = tt::as_shared<StructData>(source);
                    ASSERT(
                        structData->raw().find(key) != structData->raw().end(),
                        "Struct key not found: " + key);
                    currFrame->set(n->index(), structData->raw().at(key));
                }
                break;
            }

            case NodeType::BRCH: {
                ASSERT(
                    n->normInputs().size() == 1,
                    "Branch node must have exactly one norm input.");
                auto condData = currFrame->get(n->normInputs().front()->index());
                const auto &withIns = n->withInputs();
                const auto &ctrlOuts = n->ctrlOutputs();
                if (withIns.size() == 0) {
                    // if-else branch
                    ASSERT(
                        ctrlOuts.size() == 2,
                        "If-else branch node must have exactly two outputs.");
                    auto boolCondData = condData->as<BoolData>(Type::Bool());
                    bool cond = boolCondData->data();
                    if (cond) {
                        brInfoStack_.push(ctrlOuts.front());
                    } else {
                        brInfoStack_.push(ctrlOuts.back());
                    }
                } else {
                    // match-case branch
                    ASSERT(
                        withIns.size() == ctrlOuts.size() - 1,
                        "Match-case branch node must have exactly one more ctrl output than "
                        "norm inputs.");
                    size_t j = 0;
                    for (; j < withIns.size(); ++j) {
                        auto caseData = currFrame->get(withIns[j]->index());
                        EXEC_WHEN_DEBUG(l.in("Eval").debug(
                            "Matching case: {} with condition: {}",
                            caseData->toString(),
                            condData->toString()));
                        if (condData->equals(caseData)) {
                            brInfoStack_.push(ctrlOuts[j]);
                            break;
                        }
                    }
                    if (j == withIns.size()) {
                        // fallthrough to else case if no match
                        brInfoStack_.push(ctrlOuts.back());
                    }
                }
                i += ctrlOuts.size(); // skip all cases
                break;
            }

            case NodeType::JOIN: {
                auto execNode = brInfoStack_.top();
                brInfoStack_.pop();
                // Here, the branch function node is delayed to be executed at the JOIN
                // node, which facilitates tail-call optimization. This is because when the
                // JOIN node is the last node of the current execution sequence, the
                // function call can be optimized as a tail-call. If the branch function
                // call is executed earlier, it is difficult to determine whether it is a
                // tail-call.
                // 注意，这里除了要求 JOIN 节点是执行序列的最后一个节点之外
                // 还必须保证 JOIN 节点的返回值就是 Graph 的返回值
                // 如果 Graph 选择返回的值不是 JOIN 节点的产生值，不能做尾调用优化
                // 因为尾调用优化之后，原 Frame 就会被释放，这样 Graph 无法返回正确的值
                if (i == nodes.size() - 1 && graph->outputNode() == n) {
                    evalFuncNode(execNode, true);
                    // no need to set currFrame->set(n, ...)
                    // as the tail-call function will reset the frame
                } else {
                    evalFuncNode(execNode, false);
                    currFrame->set(n->index(), currFrame->get(execNode->index()));
                }
                break;
            }

            case NodeType::CALL: {
                const auto &funcNode = n->withInputs().front();
                const auto &funcData = currFrame->get(funcNode->index());
                auto &targetGraph = tt::as_shared<FunctionData>(funcData)->graph();
                Frame funcFrame(&targetGraph);

                data_vec_t args;
                const auto &inNodes = n->normInputs();
                args.reserve(inNodes.size());
                for (const auto &inNode : inNodes) {
                    args.push_back(currFrame->get(inNode->index()));
                }

                auto portNodes = targetGraph.ports();

                // 处理闭包参数
                if (targetGraph.hasClosure()) {
                    const auto &functionData = tt::as_shared<FunctionData>(funcData);
                    const auto &closureNodes = targetGraph.closure();
                    const auto &closureData = functionData->closure();
                    ASSERT(
                        functionData->closure().size() == closureNodes.size(),
                        "Function closure size mismatch.");
                    portNodes.reserve(portNodes.size() + closureNodes.size());
                    for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                        auto closureNode = closureNodes[ci];
                        portNodes.push_back(closureNode);
                        args.push_back(closureData[ci]);
                    }
                }

                for (size_t i = 0; i < portNodes.size(); ++i) {
                    funcFrame.set(portNodes[i]->index(), args[i]);
                }

                const auto &res = evalGraph(&targetGraph, funcFrame);
                currFrame->set(n->index(), res);
                break;
            }

            case NodeType::BIND: {
                ASSERT(false, "BIND node not implemented yet.");
                break;
            }

            case NodeType::FUNC: {
                evalFuncNode(n, i == nodes.size() - 1 && graph->outputNode() == n);
                break;
            }

            case NodeType::OPER: {
                auto opNode = tt::as_shared<OperNode>(n);
                const auto &uri = opNode->oper()->uri();

                if (uri.starts_with(":mark/")) {
                    evalMarkedOperator(uri.substr(6), n, *currFrame);
                    break;
                }

                context_->eval(uri, n, *currFrame);

                EXEC_WHEN_DEBUG([&]() {
                    const auto &result = currFrame->get(n->index());
                    const auto &expectedType = n->dataType();
                    ASSERT(result != nullptr, "Return data is null.");
                    ASSERT(
                        result->type()->assignable(expectedType),
                        std::format(
                            "Operator '<{}>' expected to return data of type '{}', "
                            "but got type '<{}>'.",
                            uri,
                            expectedType->toString(),
                            result->type()->toString()));
                }());

                break;
            }

            // 无需处理
            case NodeType::DATA:
                [[fallthrough]];
            case NodeType::PORT:
                [[fallthrough]];
            // 下面两个是无数据节点，无需处理，直接跳过
            case NodeType::SYNC:
                [[fallthrough]];
            case NodeType::NREF:
                break;

            default: {
                ASSERT(
                    false,
                    std::format("Node {} should not appear in execution sequence.", n->toString()));
                break;
            }
            }

            EXEC_WHEN_DEBUG([&]() {
                if (profiler::AdvancedTracer::getInstance().isTracing()) {
                    std::string nodeName = "node:" + n->toString();
                    profiler::AdvancedTracer::getInstance().traceFunctionReturn(nodeName);
                }
            }());
        }

        EXEC_WHEN_DEBUG([&]() {
            if (profiler::AdvancedTracer::getInstance().isTracing()) {
                profiler::AdvancedTracer::getInstance().traceFunctionReturn(
                    "topoExecution:" + graph->name());
            }
        }());

    } while (loop);

    currRecursionDepth_--;

    // 前面可能触发了尾调用优化
    // 此时的 Frame 的 Graph 不一定是参数传入的 Graph
    // 拿数据也要从最终的 Frame 中拿
    const auto &finalGraph = currFrame->graph();

    const auto &retNode = finalGraph->exitNode();
    ASSERT(retNode->withInputs().size() == 0, "Return node cannot have with inputs.");
    ASSERT(retNode->normInputs().size() <= 1, "Return node cannot have multiple norm inputs.");
    const auto &input = retNode->normInputs();

    data_ptr_t result;
    if (input.empty()) {
        result = Data::null();
    } else {
        // Check if input data is initialized, if not create a default value
        auto inputData = currFrame->get(input.front()->index());
        if (inputData == nullptr) {
            // If input data is not initialized, create a default integer value 0
            // For recursive function counters, this is typically an integer type
            inputData = std::make_shared<IntData>(0);
            currFrame->set(input.front()->index(), inputData);
        }
        result = inputData;
    }

    ASSERT(result != nullptr, "Return data is null.");
    ASSERT(
        result->type()->assignable(input.front()->dataType()),
        std::format(
            "Function '{}' expected to return data of type '{}', but got type '{}'.",
            graph->name(),
            graph->funcType()->exitType()->toString(),
            result->type()->toString()));

    EXEC_WHEN_DEBUG([&]() {
        if (profiler::AdvancedTracer::getInstance().isTracing()) {
            profiler::AdvancedTracer::getInstance().traceFunctionReturn(
                "evalGraph:" + graph->name());
        }
    }());

    return result;
}

graph_ptr_t NodeVMSchedPass::apply(graph_ptr_t &graph, std::ostream &os) {
    if (!graph->hasOutput()) {
        context_->rtmDiags()
            ->of(RuntimeDiag::MissingMainFunction)
            .commit(context_->mainModule()->name());
    }
    Frame rootFrame(graph.get());
    evalGraph(graph.get(), rootFrame);
    return Graph::null();
}

void NodeVMSchedPass::evalMarkedOperator(
    const std::string uri, const GraphIR::node_ptr_t &node, Frame &currFrame) {
    if (uri == "map_arr") {
        evalMarkedOperator_map_arr(node, currFrame);
    } else if (uri == "apply_arr") {
        evalMarkedOperator_apply_arr(node, currFrame);
    } else if (uri == "filter_arr") {
        evalMarkedOperator_filter_arr(node, currFrame);
    } else if (uri == "reduce_arr" || uri == "unordered_reduce_arr") {
        evalMarkedOperator_reduce_arr(node, currFrame);
    } else if (uri == "foreach_arr" || uri == "unordered_foreach_arr") {
        evalMarkedOperator_foreach_arr(node, currFrame);
    } else {
        ASSERT(false, std::format("Mark Operator {} not implemented.", uri));
    }
}

void NodeVMSchedPass::evalMarkedOperator_map_arr(const node_ptr_t &node, Frame &currFrame) {
    auto targetData = currFrame.get(node->normInputs().front()->index());
    auto funcData = currFrame.get(node->withInputs().front()->index());

    auto func = funcData->as<FunctionData>(Type::Func());
    type_ptr_t funcRetType = func->funcType()->exitType();

    auto applyMap = [&](const data_vec_t &inputVec) -> data_vec_t {
        data_vec_t res;
        res.reserve(inputVec.size());
        for (const auto &item : inputVec) {
            auto &targetGraph = func->graph();
            Frame frame(&targetGraph);
            if (targetGraph.hasClosure()) {
                const auto &functionData = tt::as_shared<FunctionData>(funcData);
                const auto &closureNodes = targetGraph.closure();
                const auto &closureData = functionData->closure();
                ASSERT(
                    functionData->closure().size() == closureNodes.size(),
                    "Function closure size mismatch.");
                for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                    auto closureNode = closureNodes[ci];
                    frame.set(closureNode->index(), closureData[ci]);
                }
            }
            frame.set(targetGraph.ports().front()->index(), item);
            res.push_back(evalGraph(&targetGraph, frame));
        }
        return res;
    };

    auto arrayData = tt::as_shared<ArrayData>(targetData);
    currFrame.set(
        node->index(),
        ArrayData::from(Type::Array(funcRetType), applyMap(arrayData->raw())));
}

void NodeVMSchedPass::evalMarkedOperator_apply_arr(const node_ptr_t &node, Frame &currFrame) {
    auto targetData = currFrame.get(node->normInputs().front()->index());
    auto funcData = currFrame.get(node->withInputs().front()->index());
    auto func = funcData->as<FunctionData>(Type::Func());

    auto applyFunc = [&](const data_ptr_t &item) -> data_ptr_t {
        auto &targetGraph = func->graph();
        Frame frame(&targetGraph);
        if (targetGraph.hasClosure()) {
            const auto &functionData = tt::as_shared<FunctionData>(funcData);
            const auto &closureNodes = targetGraph.closure();
            const auto &closureData = functionData->closure();
            ASSERT(
                functionData->closure().size() == closureNodes.size(),
                "Function closure size mismatch.");
            for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                auto closureNode = closureNodes[ci];
                frame.set(closureNode->index(), closureData[ci]);
            }
        }
        frame.set(targetGraph.ports().front()->index(), item);
        return evalGraph(&targetGraph, frame);
    };

    for (auto &item : tt::as_shared<ArrayData>(targetData)->raw()) {
        item = applyFunc(item);
    }

    currFrame.set(node->index(), targetData);
}

void NodeVMSchedPass::evalMarkedOperator_filter_arr(const node_ptr_t &node, Frame &currFrame) {
    auto targetData = currFrame.get(node->normInputs().front()->index());
    auto funcData = currFrame.get(node->withInputs().front()->index());
    auto func = funcData->as<FunctionData>(Type::Func());

    auto shouldKeep = [&](const data_ptr_t &item) -> bool {
        auto &targetGraph = func->graph();
        Frame frame(&targetGraph);
        if (targetGraph.hasClosure()) {
            const auto &functionData = tt::as_shared<FunctionData>(funcData);
            const auto &closureNodes = targetGraph.closure();
            const auto &closureData = functionData->closure();
            ASSERT(
                functionData->closure().size() == closureNodes.size(),
                "Function closure size mismatch.");
            for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                auto closureNode = closureNodes[ci];
                frame.set(closureNode->index(), closureData[ci]);
            }
        }
        frame.set(targetGraph.ports().front()->index(), item);
        auto result = evalGraph(&targetGraph, frame);
        return result->as<BoolData>(Type::Bool())->data();
    };

    auto filterSequence = [&](auto containerData, auto createFunc) {
        const auto &raw = containerData->raw();
        data_vec_t res;
        for (const auto &item : raw) {
            if (shouldKeep(item))
                res.push_back(item);
        }
        currFrame.set(node->index(), createFunc(containerData->type(), std::move(res)));
    };

    filterSequence(tt::as_shared<ArrayData>(targetData), [](auto t, data_vec_t v) {
        return ArrayData::from(t, std::move(v));
    });
}

void NodeVMSchedPass::evalMarkedOperator_reduce_arr(const node_ptr_t &node, Frame &currFrame) {
    auto targetData = currFrame.get(node->normInputs().front()->index());
    auto funcData = currFrame.get(node->withInputs()[0]->index());
    auto initData = currFrame.get(node->withInputs()[1]->index());

    auto func = funcData->as<FunctionData>(Type::Func());

    data_vec_t elements = tt::as_shared<ArrayData>(targetData)->raw();

    if (elements.empty()) {
        currFrame.set(node->index(), initData);
        return;
    }

    data_ptr_t result = initData;
    for (const auto &item : elements) {
        auto &targetGraph = func->graph();
        Frame frame(&targetGraph);

        if (targetGraph.hasClosure()) {
            const auto &functionData = tt::as_shared<FunctionData>(funcData);
            const auto &closureNodes = targetGraph.closure();
            const auto &closureData = functionData->closure();
            ASSERT(
                functionData->closure().size() == closureNodes.size(),
                "Function closure size mismatch.");
            for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                auto closureNode = closureNodes[ci];
                frame.set(closureNode->index(), closureData[ci]);
            }
        }

        const auto &ports = targetGraph.ports();
        frame.set(ports[0]->index(), result); // acc
        frame.set(ports[1]->index(), item);   // cur

        result = evalGraph(&targetGraph, frame); // 更新 result
    }

    currFrame.set(node->index(), result);
}

void NodeVMSchedPass::evalMarkedOperator_foreach_arr(const node_ptr_t &node, Frame &currFrame) {
    auto targetData = currFrame.get(node->normInputs().front()->index());
    auto funcData = currFrame.get(node->withInputs().front()->index());
    auto func = funcData->as<FunctionData>(Type::Func());

    auto applyFunc = [&](const data_ptr_t &item) {
        auto &targetGraph = func->graph();
        Frame frame(&targetGraph);
        if (targetGraph.hasClosure()) {
            const auto &functionData = tt::as_shared<FunctionData>(funcData);
            const auto &closureNodes = targetGraph.closure();
            const auto &closureData = functionData->closure();
            ASSERT(
                functionData->closure().size() == closureNodes.size(),
                "Function closure size mismatch.");
            for (size_t ci = 0; ci < closureNodes.size(); ++ci) {
                auto closureNode = closureNodes[ci];
                frame.set(closureNode->index(), closureData[ci]);
            }
        }
        frame.set(targetGraph.ports().front()->index(), item);
        evalGraph(&targetGraph, frame); // 忽略返回值
    };

    for (const auto &item : tt::as_shared<ArrayData>(targetData)->raw()) {
        applyFunc(item);
    }
}
