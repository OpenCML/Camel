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
 * Updated: Sep. 30, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "fallback.h"
#include "builtin/algo/topo.h"

using namespace std;
using namespace GraphIR;

std::shared_ptr<node_vec_t> FallbackExecSchedPass::getTopoNodes(Graph *graph) {
    if (graphTopoNodesCache_.find(graph) == graphTopoNodesCache_.end()) {
        node_ptr_t retNode = graph->exitNode();
        auto sortedNodes = findReachable(
            retNode,
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
            if (sortedNodes.size() != graph->nodes().size() - 1) {
                GraphIR::node_vec_t unreachableNodes;
                for (const auto &n : graph->nodes()) {
                    if (n != retNode &&
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

data_ptr_t FallbackExecSchedPass::evalGraph(Graph *graph, const frame_ptr_t &frame) {
    EXEC_WHEN_DEBUG(l.in("Eval").debug("Evaluating graph: {}", graph->name()));
    if (currRecursionDepth_++ > maxRecursionDepth_) {
        context_->rtmDiags()->of(RuntimeDiag::MaxRecursionDepthExceeded).commit(graph->name());
    }

    bool loop = false;
    auto nodesPtr = getTopoNodes(graph);
    frame_ptr_t currFrame = frame, tailFrame = nullptr;

    auto evalFuncNode = [&](const node_ptr_t &n, bool isTailCall) {
        auto func = tt::as_shared<FuncNode>(n)->func();
        auto &tgtGraph = func->graph();
        EXEC_WHEN_DEBUG(l.in("Eval").debug(
            "Calling function: {} (tail-call: {})",
            func->name().empty() ? tgtGraph.name() : func->name(),
            isTailCall ? "yes" : "no"));
        frame_ptr_t nextFrame;

        data_vec_t args;
        const auto &inNodes = n->dataInputs();
        args.reserve(inNodes.size());
        for (const auto &inNode : inNodes) {
            args.push_back(currFrame->get(inNode));
        }

        const auto &portNodes = tgtGraph.ports();
        ASSERT(
            inNodes.size() == portNodes.size(),
            "Function node input size does not match function graph port size.");

        if (isTailCall) {
            // Tail-call optimization
            loop = true;
            frame_ptr_t lastFrame = currFrame;

            if (&tgtGraph == &currFrame->graph()) {
                // Self-recursion optimization
                currFrame = lastFrame;
                EXEC_WHEN_DEBUG(l.in("Eval").debug(
                    "Optimizing self-recursion for graph: {}",
                    currFrame->graph().name()));
            } else {
                nodesPtr = getTopoNodes(&tgtGraph);

                if (tailFrame && &tailFrame->graph() == &tgtGraph) {
                    // Mutual-tail-recursion optimization
                    EXEC_WHEN_DEBUG(l.in("Eval").debug(
                        "Optimizing mutual-tail-recursion for graph: {}",
                        currFrame->graph().name()));
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
            EXEC_WHEN_DEBUG(nextFrame->reset());

            for (size_t i = 0; i < portNodes.size(); ++i) {
                nextFrame->set(portNodes[i], args[i]);
            }
        } else {
            nextFrame = Frame::create(currFrame, tgtGraph);

            for (size_t i = 0; i < portNodes.size(); ++i) {
                nextFrame->set(portNodes[i], args[i]);
            }

            // evaluate the target graph
            data_ptr_t res = evalGraph(&tgtGraph, nextFrame);
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
            case NodeType::DATA: {
                ASSERT(currFrame->get(n) != nullptr, "DATA data is null.");
                break;
            }
            case NodeType::PORT: {
                ASSERT(currFrame->get(n) != nullptr, "PORT data is null.");
                break;
            }
            case NodeType::COPY: {
                currFrame->set(n, currFrame->get(n->withInputs().front())->clone());
                break;
            }
            case NodeType::FILL: {
                auto structNode = tt::as_shared<FillNode>(n);
                const auto &srcNode = n->withInputs().front();
                const auto &dataInputs = n->normInputs();
                data_ptr_t data = currFrame->get(srcNode)->clone();
                ASSERT(data != nullptr, "FILL data is null.");
                data_vec_t inputs;
                inputs.reserve(dataInputs.size());
                for (const auto &input : dataInputs) {
                    inputs.push_back(currFrame->get(input));
                }
                data->resolve(inputs);
                currFrame->set(n, data);
                break;
            }
            case NodeType::ACCS: {
                data_ptr_t source = currFrame->get(n->dataInputs().front());
                auto accessNode = tt::as_shared<AccsNode>(n);
                if (accessNode->isNum()) {
                    size_t idx = accessNode->index<size_t>();
                    if (source->type()->code() == TypeCode::Array) {
                        auto arrayData = tt::as_shared<ArrayData>(source);
                        ASSERT(idx < arrayData->size(), "Array index out of bounds.");
                        currFrame->set(n, arrayData->raw()[idx]);
                    } else if (source->type()->code() == TypeCode::Tuple) {
                        auto tupleData = tt::as_shared<TupleData>(source);
                        ASSERT(idx < tupleData->size(), "Tuple index out of bounds.");
                        currFrame->set(n, tupleData->raw()[idx]);
                    } else if (source->type()->code() == TypeCode::Vector) {
                        auto vectorData = tt::as_shared<VectorData>(source);
                        ASSERT(idx < vectorData->size(), "Vector index out of bounds.");
                        currFrame->set(n, vectorData->raw()[idx]);
                    } else {
                        context_->rtmDiags()
                            ->of(RuntimeDiag::IncompatibleArgType)
                            .commit(0, "ACCS", "Array/Tuple/Vector", source->type()->toString());
                        throw CamelRuntimeException(
                            RuntimeExceptionCode::InvalidWithParameter,
                            "Incorrect args.");
                    }
                } else {
                    std::string key = accessNode->index<std::string>();
                    if (source->type()->code() == TypeCode::Dict) {
                        auto dictData = tt::as_shared<DictData>(source);
                        ASSERT(
                            dictData->raw().find(key) != dictData->raw().end(),
                            "Dict key not found: " + key);
                        currFrame->set(n, dictData->raw().at(key));
                    } else {
                        context_->rtmDiags()
                            ->of(RuntimeDiag::IncompatibleArgType)
                            .commit(0, "ACCS", "Dict", source->type()->toString());
                        throw CamelRuntimeException(
                            RuntimeExceptionCode::InvalidWithParameter,
                            "Incorrect args.");
                    }
                }
                break;
            }
            case NodeType::BRCH: {
                ASSERT(
                    n->withInputs().size() == 1,
                    "Branch node must have exactly one with input.");
                auto condData = currFrame->get(n->withInputs().front());
                const auto &normIns = n->normInputs();
                const auto &ctrlOuts = n->ctrlOutputs();
                if (normIns.size() == 0) {
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
                        normIns.size() == ctrlOuts.size() - 1,
                        "Match-case branch node must have exactly one more ctrl output than "
                        "norm inputs.");
                    size_t j = 0;
                    for (; j < normIns.size(); ++j) {
                        auto caseData = currFrame->get(normIns[j]);
                        EXEC_WHEN_DEBUG(l.in("Eval").debug(
                            "Matching case: {} with condition: {}",
                            caseData->toString(),
                            condData->toString()));
                        if (condData->equals(caseData)) {
                            brInfoStack_.push(ctrlOuts[j]);
                            break;
                        }
                    }
                    if (j == normIns.size()) {
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
                if (i == nodes.size() - 1) {
                    evalFuncNode(execNode, true);
                    // no need to set currFrame->set(n, ...)
                    // as the tail-call function will reset the frame
                } else {
                    evalFuncNode(execNode, false);
                    currFrame->set(n, currFrame->get(execNode));
                }
                break;
            }
            case NodeType::CALL: {
                const auto &funcNode = n->withInputs().front();
                const auto &funcData = currFrame->get(funcNode);
                auto &tgtGraph = tt::as_shared<FunctionData>(funcData)->graph();
                frame_ptr_t funcFrame = Frame::create(currFrame, tgtGraph);

                data_vec_t args;
                const auto &inNodes = n->normInputs();
                args.reserve(inNodes.size());
                for (const auto &inNode : inNodes) {
                    args.push_back(currFrame->get(inNode));
                }

                const auto &portNodes = tgtGraph.ports();
                for (size_t i = 0; i < portNodes.size(); ++i) {
                    funcFrame->set(portNodes[i], args[i]);
                }

                const auto &res = evalGraph(&tgtGraph, funcFrame);
                currFrame->set(n, res);
                break;
            }
            case NodeType::BIND: {
                ASSERT(false, "BIND node not implemented yet.");
                break;
            }
            case NodeType::FUNC: {
                evalFuncNode(n, i == nodes.size() - 1);
                break;
            }
            case NodeType::OPER: {
                auto opNode = tt::as_shared<OperNode>(n);
                const auto &uri = opNode->oper()->uri();

                if (uri.starts_with(":mark/")) {
                    evalMarkedOperator(uri.substr(6), n, currFrame);
                    break;
                }

                context_->eval(uri, n, *currFrame);
                break;
            }
            case NodeType::EXIT: {
                ASSERT(false, "EXIT node should not appear in the execution sequence.");
                break;
            }
            }
        }
    } while (loop);

    currRecursionDepth_--;

    const auto &retNode = currFrame->graph().exitNode();
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
    auto rootFrame = Frame::create(nullptr, *graph);
    auto optMainGraph = graph->getSubGraph("main");
    ASSERT(optMainGraph.has_value(), "Main graph not found.");
    auto mainGraph = optMainGraph.value();
    auto mainFrame = Frame::create(rootFrame, *mainGraph);
    return evalGraph(mainGraph.get(), mainFrame);
}

void FallbackExecSchedPass::evalMarkedOperator(
    const std::string uri, const GraphIR::node_ptr_t &node, frame_ptr_t &currFrame) {
    if (uri == "map") {
        evalMarkedOperator_map(node, currFrame);
    } else if (uri == "apply") {
        evalMarkedOperator_apply(node, currFrame);
    } else if (uri == "filter") {
        evalMarkedOperator_filter(node, currFrame);
    } else if (uri == "reduce") {
        evalMarkedOperator_reduce(node, currFrame);
    } else if (uri == "foreach") {
        evalMarkedOperator_foreach(node, currFrame);
    } else {
        ASSERT(false, std::format("Mark Operator {} not implemented.", uri));
    }
}

void FallbackExecSchedPass::evalMarkedOperator_map(const node_ptr_t &node, frame_ptr_t &currFrame) {
    if (node->withInputs().size() != 1 || node->normInputs().size() != 1) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<map>", 1, node->withInputs().size() + node->normInputs().size());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }

    auto targetData = currFrame->get(node->withInputs().front());
    auto funcData = currFrame->get(node->normInputs().front());

    if (funcData->type()->code() != TypeCode::Func) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<map>", "Function", funcData->type()->toString());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidNormParameter, "Incorrect args.");
    }
    auto func = funcData->as<FunctionData>(Type::Func());
    type_ptr_t funcRetType = func->funcType()->returnType();

    auto applyMap = [&](const data_vec_t &inputVec) -> data_vec_t {
        data_vec_t res;
        res.reserve(inputVec.size());
        for (const auto &item : inputVec) {
            auto mapFrame = Frame::create(currFrame, func->graph());
            mapFrame->set(func->graph().ports().front(), item);
            res.push_back(evalGraph(&func->graph(), mapFrame));
        }
        return res;
    };

    switch (targetData->type()->code()) {
    case TypeCode::List:
        currFrame->set(
            node,
            ListData::create(applyMap(tt::as_shared<ListData>(targetData)->raw())));
        break;
    case TypeCode::Array: {
        auto arrayData = tt::as_shared<ArrayData>(targetData);
        currFrame->set(
            node,
            ArrayData::create(
                Type::Array(funcRetType, arrayData->size()),
                applyMap(arrayData->raw())));
        break;
    }
    case TypeCode::Vector: {
        auto vectorData = tt::as_shared<VectorData>(targetData);
        currFrame->set(
            node,
            VectorData::create(Type::Vector(funcRetType), applyMap(vectorData->raw())));
        break;
    }
    case TypeCode::Dict: {
        auto dictData = tt::as_shared<DictData>(targetData);
        std::unordered_map<std::string, data_ptr_t> res;
        for (const auto &[k, v] : dictData->raw()) {
            auto mapFrame = Frame::create(currFrame, func->graph());
            mapFrame->set(func->graph().ports().front(), v);
            res[k] = evalGraph(&func->graph(), mapFrame);
        }
        currFrame->set(node, DictData::create(std::move(res)));
        break;
    }
    default:
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<map>", "List/Array/Tuple/Vector/Dict", targetData->type()->toString());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }
}

void FallbackExecSchedPass::evalMarkedOperator_apply(
    const node_ptr_t &node, frame_ptr_t &currFrame) {
    if (node->withInputs().size() != 1 || node->normInputs().size() != 1) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<apply>", 1, node->withInputs().size() + node->normInputs().size());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }

    auto targetData = currFrame->get(node->withInputs().front());
    auto funcData = currFrame->get(node->normInputs().front());
    auto func = funcData->as<FunctionData>(Type::Func());

    auto applyFunc = [&](const data_ptr_t &item) -> data_ptr_t {
        auto frame = Frame::create(currFrame, func->graph());
        frame->set(func->graph().ports().front(), item);
        return evalGraph(&func->graph(), frame);
    };

    auto applyToSequence = [&](auto containerData, auto createFunc) {
        const auto &raw = containerData->raw();
        data_vec_t res;
        res.reserve(raw.size());
        for (const auto &item : raw) {
            res.push_back(applyFunc(item));
        }
        currFrame->set(node, createFunc(containerData->type(), std::move(res)));
    };

    switch (targetData->type()->code()) {
    case TypeCode::List:
        applyToSequence(tt::as_shared<ListData>(targetData), [](auto, data_vec_t v) {
            return ListData::create(std::move(v));
        });
        break;
    case TypeCode::Array:
        applyToSequence(tt::as_shared<ArrayData>(targetData), [](auto t, data_vec_t v) {
            return ArrayData::create(t, std::move(v));
        });
        break;
    case TypeCode::Tuple:
        applyToSequence(tt::as_shared<TupleData>(targetData), [](auto t, data_vec_t v) {
            return TupleData::create(t, std::move(v));
        });
        break;
    case TypeCode::Vector:
        applyToSequence(tt::as_shared<VectorData>(targetData), [](auto t, data_vec_t v) {
            return VectorData::create(t, std::move(v));
        });
        break;
    default:
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<apply>", "List/Array/Tuple/Vector/Dict", targetData->toString());
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidWithParameter,
            "Unsupported type.");
    }
}

void FallbackExecSchedPass::evalMarkedOperator_filter(
    const node_ptr_t &node, frame_ptr_t &currFrame) {
    if (node->withInputs().size() != 1 || node->normInputs().size() != 1) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<filter>", 1, node->withInputs().size() + node->normInputs().size());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }

    auto targetData = currFrame->get(node->withInputs().front());
    auto funcData = currFrame->get(node->normInputs().front());
    auto func = funcData->as<FunctionData>(Type::Func());

    auto shouldKeep = [&](const data_ptr_t &item) -> bool {
        auto frame = Frame::create(currFrame, func->graph());
        frame->set(func->graph().ports().front(), item);
        auto result = evalGraph(&func->graph(), frame);
        ASSERT(result->type()->code() == TypeCode::Bool, "Filter function must return Bool.");
        return result->as<BoolData>(Type::Bool())->data();
    };

    auto filterSequence = [&](auto containerData, auto createFunc) {
        const auto &raw = containerData->raw();
        data_vec_t res;
        for (const auto &item : raw) {
            if (shouldKeep(item))
                res.push_back(item);
        }
        currFrame->set(node, createFunc(containerData->type(), std::move(res)));
    };

    switch (targetData->type()->code()) {
    case TypeCode::List:
        filterSequence(tt::as_shared<ListData>(targetData), [](auto, data_vec_t v) {
            return ListData::create(std::move(v));
        });
        break;
    case TypeCode::Array:
        filterSequence(tt::as_shared<ArrayData>(targetData), [](auto t, data_vec_t v) {
            return ArrayData::create(t, std::move(v));
        });
        break;
    case TypeCode::Tuple:
        filterSequence(tt::as_shared<TupleData>(targetData), [](auto t, data_vec_t v) {
            return TupleData::create(t, std::move(v));
        });
        break;
    case TypeCode::Vector:
        filterSequence(tt::as_shared<VectorData>(targetData), [](auto t, data_vec_t v) {
            return VectorData::create(t, std::move(v));
        });
        break;
    default:
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<filter>", "List/Array/Tuple/Vector/Dict", targetData->toString());
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidWithParameter,
            "Unsupported type.");
    }
}

void FallbackExecSchedPass::evalMarkedOperator_reduce(
    const node_ptr_t &node, frame_ptr_t &currFrame) {
    // 参数检查：必须有1个with输入，2个norm输入
    if (node->withInputs().size() != 1 || node->normInputs().size() != 2) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<reduce>", 2, node->withInputs().size() + node->normInputs().size());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }

    auto targetData = currFrame->get(node->withInputs().front());
    auto funcData = currFrame->get(node->normInputs()[0]);
    auto initData = currFrame->get(node->normInputs()[1]);

    if (funcData->type()->code() != TypeCode::Func) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<reduce>", "Function", funcData->type()->toString());
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            "Expected function.");
    }

    auto func = funcData->as<FunctionData>(Type::Func());

    // 检查函数参数个数（必须是2个参数）
    if (func->graph().ports().size() != 2) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<reduce>", 2, func->graph().ports().size());
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidNormParameter,
            "Function must take 2 parameters.");
    }

    data_vec_t elements;

    switch (targetData->type()->code()) {
    case TypeCode::List:
        elements = tt::as_shared<ListData>(targetData)->raw();
        break;
    case TypeCode::Array:
        elements = tt::as_shared<ArrayData>(targetData)->raw();
        break;
    case TypeCode::Vector:
        elements = tt::as_shared<VectorData>(targetData)->raw();
        break;
    case TypeCode::Tuple:
        elements = tt::as_shared<TupleData>(targetData)->raw();
        break;
    default:
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<reduce>", "List/Array/Vector/Tuple", targetData->type()->toString());
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidWithParameter,
            "Unsupported input type.");
    }

    if (elements.empty()) {
        // 空序列：返回初始值
        currFrame->set(node, initData);
        return;
    }

    // 执行 reduce 操作
    data_ptr_t result = initData;
    for (const auto &item : elements) {
        auto frame = Frame::create(currFrame, func->graph());

        // 设置参数：acc, cur
        const auto &ports = func->graph().ports();
        frame->set(ports[0], result); // acc
        frame->set(ports[1], item);   // cur

        result = evalGraph(&func->graph(), frame); // 更新 result
    }

    // 设置结果
    currFrame->set(node, result);
}

void FallbackExecSchedPass::evalMarkedOperator_foreach(
    const node_ptr_t &node, frame_ptr_t &currFrame) {
    if (node->withInputs().size() != 1 || node->normInputs().size() != 1) {
        context_->rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<foreach>", 1, node->withInputs().size() + node->normInputs().size());
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidWithParameter, "Incorrect args.");
    }

    auto targetData = currFrame->get(node->withInputs().front());
    auto funcData = currFrame->get(node->normInputs().front());
    auto func = funcData->as<FunctionData>(Type::Func());

    auto applyFunc = [&](const data_ptr_t &item) {
        auto frame = Frame::create(currFrame, func->graph());
        frame->set(func->graph().ports().front(), item);
        evalGraph(&func->graph(), frame); // 忽略返回值
    };

    switch (targetData->type()->code()) {
    case TypeCode::List:
        for (const auto &item : tt::as_shared<ListData>(targetData)->raw()) {
            applyFunc(item);
        }
        break;
    case TypeCode::Array:
        for (const auto &item : tt::as_shared<ArrayData>(targetData)->raw()) {
            applyFunc(item);
        }
        break;
    case TypeCode::Tuple:
        for (const auto &item : tt::as_shared<TupleData>(targetData)->raw()) {
            applyFunc(item);
        }
        break;
    case TypeCode::Vector:
        for (const auto &item : tt::as_shared<VectorData>(targetData)->raw()) {
            applyFunc(item);
        }
        break;
    case TypeCode::Dict:
        for (const auto &[k, v] : tt::as_shared<DictData>(targetData)->raw()) {
            applyFunc(v);
        }
        break;
    default:
        context_->rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<foreach>", "List/Array/Tuple/Vector/Dict", targetData->toString());
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidWithParameter,
            "Unsupported type.");
    }
}
