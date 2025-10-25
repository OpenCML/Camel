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
 * Updated: Oct. 25, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "fastvm.h"
#include "builtin/algo/topo.h"
#include "builtin/passes/sched/common/precompile.h"
#include "core/data/primary.h"

#ifndef NDEBUG
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"
#endif

using namespace std;
using namespace GraphIR;

std::shared_ptr<bytecode_vec_t> FastVMSchedPass::getCodeOfGraph(Graph *graph) {
    if (codes_.find(graph) == codes_.end()) {
        auto code = precompile(context_, graph);
        codes_[graph] = code;
        return code;
    } else {
        return codes_[graph];
    }
}

data_ptr_t FastVMSchedPass::evalGraph(Graph *graph, Frame &frame) {
    EXEC_WHEN_DEBUG(l.in("Eval").debug("Evaluating graph: {}", graph->name()));

    if (currRecursionDepth_++ > maxRecursionDepth_) {
        context_->rtmDiags()->of(RuntimeDiag::MaxRecursionDepthExceeded).commit(graph->name());
    }

    bool loop = false;
    auto bytecodes = getCodeOfGraph(graph);

    Frame targetFrame(graph);
    frame_rptr_t currFrame = &frame, tailFrame = nullptr;

    auto evalFuncNode =
        [&](GraphIR::Graph &targetGraph, const data_vec_t &args, bool isTailCall) -> data_ptr_t {
        EXEC_WHEN_DEBUG(l.in("Eval").debug(
            "Calling function: {} (tail-call: {})",
            targetGraph.name(),
            isTailCall ? "yes" : "no"));
        frame_rptr_t nextFrame = nullptr;

        node_vec_t portNodes;
        for (const auto &p : targetGraph.ports()) {
            portNodes.push_back(p);
        }
        ASSERT(
            args.size() == portNodes.size(),
            std::format(
                "Function {} expects {} arguments, but got {}.",
                targetGraph.name(),
                portNodes.size(),
                args.size()));

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
                bytecodes = getCodeOfGraph(&targetGraph);

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

            return nullptr; // indicate tail-call
        } else {
            Frame tmpFrame(&targetGraph);
            nextFrame = &tmpFrame;

            for (size_t i = 0; i < portNodes.size(); ++i) {
                nextFrame->set(portNodes[i]->index(), args[i]);
            }

            // evaluate the target graph
            return evalGraph(&targetGraph, *nextFrame);
        }
    };

    // for tail-call optimization
    // reuse the current frame for tail-recursive calls
    do {
        loop = false;
        const Bytecode *code = bytecodes->data();
        size_t codeSize = bytecodes->size();

        size_t i = 0;
        while (i < codeSize) {
            const Bytecode &bc = code[i];
            switch (bc.opcode) {

            case OpCode::NOOP: {
                // do nothing
                break;
            }

            case OpCode::CAST: {
                ASSERT(false, "CAST opcode not implemented in FastVM.");
                break;
            }

            case OpCode::COPY: {
                auto srcData = currFrame->get(bc.fastop[0]);
                currFrame->set(bc.result, srcData->clone());
                break;
            }

            case OpCode::ACCS: {
                auto srcData = currFrame->get(bc.fastop[0]);
                auto tuple = tt::as_shared<TupleData>(srcData);
                ASSERT(
                    static_cast<size_t>(bc.fastop[1]) < tuple->size(),
                    "Tuple access index out of range in FastVM.");
                currFrame->set(bc.result, tuple->get(static_cast<size_t>(bc.fastop[1])));
                break;
            }

            case OpCode::JUMP: {
                i = static_cast<arr_size_t>(bc.fastop[0]);
                continue; // skip i increment
            }

            case OpCode::BRCH: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();
                auto condData = currFrame->get(nargs[0]);

                size_t jumpIdx = 0;

                if (bc.withCnt() == 0) {
                    bool cond = tt::as_shared<BoolData>(condData)->data();
                    if (cond) {
                        jumpIdx = 0; // jump to true branch
                    } else {
                        jumpIdx = 1; // jump to false branch
                    }
                } else {
                    size_t j = 0;
                    for (; j < bc.withCnt(); ++j) {
                        auto caseData = currFrame->get(wargs[j]);
                        EXEC_WHEN_DEBUG(l.in("Eval").debug(
                            "Matching case: {} with condition: {}",
                            caseData->toString(),
                            condData->toString()));
                        if (condData->equals(caseData)) {
                            jumpIdx = j; // jump to matched case
                            break;
                        }
                    }
                    if (j == bc.withCnt()) {
                        // fallthrough to else case if no match
                        jumpIdx = bc.withCnt();
                    }
                }

                currFrame->set(
                    bc.result,
                    std::make_shared<Int32Data>(static_cast<int32_t>(jumpIdx)));
                i += bc.opsize + jumpIdx;

                continue; // skip i increment
            }

            case OpCode::JOIN: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();
                const auto &input = currFrame->get(nargs[0]);
                int32_t choosenIdx = tt::as_shared<Int32Data>(input)->data();
                ASSERT(
                    choosenIdx >= 0 && static_cast<size_t>(choosenIdx) < bc.withCnt(),
                    "JOIN opcode choosen index out of range in FastVM.");
                auto resultData = currFrame->get(wargs[static_cast<size_t>(choosenIdx)]);
                currFrame->set(bc.result, resultData);
                break;
            }

            case OpCode::FILL: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();
                auto target = currFrame->get(nargs[0])->clone();
                ASSERT(target != nullptr, "FILL target data is null.");
                data_vec_t inputs;
                inputs.reserve(bc.withCnt());
                for (size_t j = 0; j < bc.withCnt(); ++j) {
                    inputs.push_back(currFrame->get(wargs[j]));
                }
                target->resolve(inputs);
                currFrame->set(bc.result, target);
                break;
            }

            case OpCode::CALL: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();
                auto funcData = currFrame->get(wargs[0]);
                auto function = tt::as_shared<FunctionData>(funcData);
                auto &targetGraph = function->graph();
                Frame funcFrame(&targetGraph);

                const auto &normPorts = targetGraph.normPorts();
                ASSERT(
                    bc.normCnt() == normPorts.size(),
                    "CALL opcode norm argument count mismatch in FastVM.");
                for (size_t j = 0; j < bc.normCnt(); ++j) {
                    funcFrame.set(normPorts[j]->index(), currFrame->get(nargs[j]));
                }

                const auto &withPorts = targetGraph.withPorts();
                ASSERT(
                    withPorts.size() == bc.withCnt() - 1,
                    "CALL opcode with argument count mismatch in FastVM.");
                for (size_t j = 1; j < bc.withCnt(); ++j) {
                    funcFrame.set(withPorts[j - 1]->index(), currFrame->get(wargs[j]));
                }

                const auto &closurePorts = targetGraph.closure();
                const auto &closureData = function->closure();
                ASSERT(
                    closurePorts.size() == closureData.size(),
                    "Function closure size mismatch in FastVM.");
                for (size_t j = 0; j < closurePorts.size(); ++j) {
                    funcFrame.set(closurePorts[j]->index(), closureData[j]);
                }

                const auto &result = evalGraph(&targetGraph, funcFrame);
                currFrame->set(bc.result, result);
                break;
            }

            case OpCode::FUNC: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();

                GraphIR::Graph *funcGraph = bc.extra()->graph;
                const auto &normPorts = funcGraph->normPorts();
                const auto &withPorts = funcGraph->withPorts();

                data_vec_t args;
                args.reserve(normPorts.size() + withPorts.size());

                ASSERT(
                    bc.normCnt() == normPorts.size(),
                    "FUNC opcode norm argument count mismatch in FastVM.");
                for (size_t j = 0; j < bc.normCnt(); ++j) {
                    args.push_back(currFrame->get(nargs[j]));
                }

                ASSERT(
                    withPorts.size() == bc.withCnt(),
                    "FUNC opcode with argument count mismatch in FastVM.");
                for (size_t j = 0; j < bc.withCnt(); ++j) {
                    args.push_back(currFrame->get(wargs[j]));
                }

                auto res = evalFuncNode(*funcGraph, args, false);
                currFrame->set(bc.result, res);

                break;
            }

            case OpCode::TAIL: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();

                GraphIR::Graph *funcGraph = bc.extra()->graph;
                const auto &normPorts = funcGraph->normPorts();
                const auto &withPorts = funcGraph->withPorts();

                data_vec_t args;
                args.reserve(normPorts.size() + withPorts.size());

                ASSERT(
                    bc.normCnt() == normPorts.size(),
                    "FUNC opcode norm argument count mismatch in FastVM.");
                for (size_t j = 0; j < bc.normCnt(); ++j) {
                    args.push_back(currFrame->get(nargs[j]));
                }

                ASSERT(
                    withPorts.size() == bc.withCnt(),
                    "FUNC opcode with argument count mismatch in FastVM.");
                for (size_t j = 0; j < bc.withCnt(); ++j) {
                    args.push_back(currFrame->get(wargs[j]));
                }

                evalFuncNode(*funcGraph, args, true);

                // 跳过后续字节码，进行下一轮循环
                i = codeSize;
                continue;
            }

            case OpCode::OPER: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();
                auto func = bc.extra()->func;
                EXEC_WHEN_DEBUG(l.in("Eval").debug(
                    "Executing operator {}.",
                    context_->execMgr().getNameOfAnOperator(bc.extra()->func)));
                func(bc.result, nargs, wargs, *currFrame, *context_);
                break;
            }

            case OpCode::SCHD: {
                ASSERT(false, "SCHD opcode not implemented in FastVM.");
                break;
            }

            default:
                ASSERT(false, "Unsupported opcode in FastVM.");
            }

            // move to the next bytecode
            i += bc.opsize;
        }

    } while (loop);

    currRecursionDepth_--;

    const auto &retNode = currFrame->graph()->exitNode();
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
            inputData = std::make_shared<Int32Data>(0);
            currFrame->set(input.front()->index(), inputData);
        }
        result = inputData;
    }

    return result;
}

graph_ptr_t FastVMSchedPass::apply(graph_ptr_t &graph, std::ostream &os) {
    if (!graph->hasOutput()) {
        context_->rtmDiags()
            ->of(RuntimeDiag::MissingMainFunction)
            .commit(context_->mainModule()->name());
    }
    Frame rootFrame(graph.get());
    evalGraph(graph.get(), rootFrame);
    return Graph::null();
}

void FastVMSchedPass::evalMarkedOperator(
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

void FastVMSchedPass::evalMarkedOperator_map_arr(const node_ptr_t &node, Frame &currFrame) {
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

void FastVMSchedPass::evalMarkedOperator_apply_arr(const node_ptr_t &node, Frame &currFrame) {
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

void FastVMSchedPass::evalMarkedOperator_filter_arr(const node_ptr_t &node, Frame &currFrame) {
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

void FastVMSchedPass::evalMarkedOperator_reduce_arr(const node_ptr_t &node, Frame &currFrame) {
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

void FastVMSchedPass::evalMarkedOperator_foreach_arr(const node_ptr_t &node, Frame &currFrame) {
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
