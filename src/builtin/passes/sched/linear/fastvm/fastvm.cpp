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
 * Updated: Dec. 13, 2025
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

bytecode_vec_t *FastVMSchedPass::getBytecodesOfGraph(GraphIR::Graph *graph) {
    bytecode_vec_t *codes = graph->getExtra<bytecode_vec_t, 1>();
    if (codes == nullptr) {
        codes = &bytecodes_.emplace_back(precompile(context_, graph));
        graph->setExtra<bytecode_vec_t, 1>(codes);
    }
    return codes;
}

slot_t FastVMSchedPass::call(Graph *graph, Frame *frame) {
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Evaluating graph: {}", graph->name()));

    if (currRecursionDepth_++ > maxRecursionDepth_) {
        context_->rtmDiags()
            ->of(RuntimeDiag::MaxRecursionDepthExceeded)
            .commit(graph->name(), maxRecursionDepth_);
    }

    bool loop = false;

    bytecode_vec_t *codes = getBytecodesOfGraph(graph);

    Frame *nextFrame = nullptr; // for mutual-tail-recursion optimization
    Frame *currFrame = frame, *twinFrame = nullptr;

    // for tail-call optimization
    // reuse the current frame for tail-recursive calls
    do {
        loop                 = false;
        const Bytecode *code = codes->data();
        size_t codeSize      = codes->size();

        size_t i = 0;
        while (i < codeSize) {
            const Bytecode &bc = code[i];
            EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode[{}]: {}", i, bc.toString()));

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
                TypeCode srcType = currFrame->typeAt(bc.fastop[0]);
                if (isGCTraced(srcType)) {
                    Object *srcData = currFrame->get<Object *>(bc.fastop[0]);
                    currFrame->set(bc.result, srcData->clone(mm::autoSpace(), false));
                } else {
                    slot_t srcData = currFrame->get<slot_t>(bc.fastop[0]);
                    currFrame->set(bc.result, srcData);
                }
                break;
            }

            case OpCode::ACCS: {
                TypeCode srcType = currFrame->typeAt(bc.fastop[0]);
                if (srcType == TypeCode::Tuple) {
                    Tuple *t = currFrame->get<Tuple *>(bc.fastop[0]);
                    ASSERT(
                        static_cast<size_t>(bc.fastop[1]) < t->size(),
                        "Tuple access index out of range in FastVM.");
                    currFrame->set(bc.result, t->get<slot_t>(static_cast<size_t>(bc.fastop[1])));
                } else if (srcType == TypeCode::Struct) {
                    Struct *s = currFrame->get<Struct *>(bc.fastop[0]);
                    ASSERT(
                        static_cast<size_t>(bc.fastop[1]) < s->size(),
                        "Struct access index out of range in FastVM.");
                    currFrame->set(bc.result, s->get<slot_t>(static_cast<size_t>(bc.fastop[1])));
                } else {
                    ASSERT(false, "ACCS opcode unsupported source type in FastVM.");
                }
                break;
            }

            case OpCode::JUMP: {
                i = static_cast<arr_size_t>(bc.fastop[0]);
                continue; // skip i increment
            }

            case OpCode::BRCH: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();

                size_t jumpIdx = 0;
                if (bc.withCnt() == 0) {
                    // 普通的 if-else 分支，cond 是 bool 类型
                    bool condData = currFrame->get<bool>(nargs[0]);
                    if (condData) {
                        jumpIdx = 0; // jump to true branch
                    } else {
                        jumpIdx = 1; // jump to false branch
                    }
                } else {
                    // match-case，依次判断各分支
                    size_t j          = 0;
                    TypeCode condType = currFrame->typeAt(nargs[0]);

                    if (isGCTraced(condType)) {
                        auto condData = currFrame->get<Object *>(nargs[0]);
                        for (; j < bc.withCnt(); ++j) {
                            auto caseData = currFrame->get<Object *>(wargs[j]);
                            if (condData->equals(caseData)) {
                                jumpIdx = j; // jump to matched case
                                break;
                            }
                        }
                    } else {
                        auto condData = currFrame->get<slot_t>(nargs[0]);
                        for (; j < bc.withCnt(); ++j) {
                            auto caseData = currFrame->get<slot_t>(wargs[j]);
                            if (condData == caseData) {
                                jumpIdx = j; // jump to matched case
                                break;
                            }
                        }
                    }

                    if (j == bc.withCnt()) {
                        // fallthrough to else case if no match
                        jumpIdx = bc.withCnt();
                    }
                }

                currFrame->set(bc.result, fromSlot<Int>(jumpIdx));
                i += bc.opsize + jumpIdx;

                continue; // skip i increment
            }

            case OpCode::JOIN: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();
                int32_t brIndex        = currFrame->get<int32_t>(nargs[0]);
                ASSERT(
                    brIndex >= 0 && static_cast<size_t>(brIndex) < bc.withCnt(),
                    "JOIN opcode choosen index out of range in FastVM.");
                slot_t result = currFrame->get<slot_t>(wargs[static_cast<size_t>(brIndex)]);
                currFrame->set(bc.result, result);
                break;
            }

            case OpCode::FILL: {
                ASSERT(false, "FILL opcode not fully implemented in FastVM.");
                // const data_arr_t nargs = bc.nargs();
                // const data_arr_t wargs = bc.wargs();

                // TypeCode targetType = currFrame->typeAt(nargs[0]);
                // ASSERT(isGCTraced(targetType), "FILL target type is not GC-traced in FastVM.");

                // Object *target = currFrame->get<Object *>(nargs[0])->clone(mm::autoSpace());
                // ASSERT(target != nullptr, "FILL target data is null.");

                // switch (targetType) {
                // case TypeCode::Tuple: {
                //     auto tuple = static_cast<Tuple *>(target);
                //     break;
                // }
                // default:
                //     ASSERT(false, "Unsupported FILL target type.");
                // }

                // data_vec_t inputs;
                // inputs.reserve(bc.withCnt());
                // for (size_t j = 0; j < bc.withCnt(); ++j) {
                //     inputs.push_back(currFrame->get(wargs[j]));
                // }
                // target->resolve(inputs);

                // currFrame->set(bc.result, target);
                break;
            }

            case OpCode::CALL: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();
                auto function          = currFrame->get<Function *>(wargs[0]);
                auto targetGraph       = function->graph();

                Frame *funcFrame = framePool_.acquire(targetGraph);

                size_t i = 0;
                for (; i < nargs.size; ++i) {
                    funcFrame->set(i + 1, currFrame->get<slot_t>(nargs[i]));
                }

                Tuple *closureData = function->tuple();
                for (size_t j = 0; j < closureData->size(); ++j) {
                    funcFrame->set(i + j + 1, closureData->get<slot_t>(j));
                }

                const auto &result = call(targetGraph, funcFrame);

                currFrame->set(bc.result, result);
                break;
            }

            case OpCode::FUNC: {
                Frame *funcFrame = framePool_.acquire(bc.extra()->graph);

                size_t argsCnt         = bc.argsCnt();
                const data_idx_t *args = bc.operands();
                for (size_t i = 0; i < argsCnt; ++i) {
                    funcFrame->set(i + 1, currFrame->get<slot_t>(args[i]));
                }

                slot_t result = call(bc.extra()->graph, funcFrame);

                currFrame->set(bc.result, result);
                break;
            }

            case OpCode::TAIL: {
                // 尾调用优化
                Graph *funcFrame = bc.extra()->graph;

                // enable tail-call optimization
                // 设置 loop flag 为 true
                // 当前字节码执行子循环结束后不会退出大循环
                // 而会根据新指定的 currFrame 和 bytecodes 执行新图
                // 这样可以复用当前 C++ 栈帧，避免 C++ 栈溢出
                loop = true;
                // 设置 lastFrame 为当前帧 currFrame 备用
                Frame *lastFrame = currFrame;

                // 下面准备将 currFrame 重新指向新栈帧
                if (funcFrame == currFrame->graph()) {
                    // 如果目标图就是当前帧的图，说明在进行自递归尾调用
                    // 此时可以复用当前栈帧和字节码，无需修改栈帧指向
                    EXEC_WHEN_DEBUG(l.in("FastVM").debug(
                        "Optimizing self-recursion for graph: {}",
                        currFrame->graph()->name()));
                } else {
                    // 否则需要切换到目标图的新字节码，并修改 currFrame 指向
                    codes = getBytecodesOfGraph(funcFrame);

                    // 即便目标图不是当前图，仍可能存在互调用尾递归现象
                    // 即 A 尾调用 B，B 又尾调用 A
                    // Camel 中分支默认被编译为子图，互调用非常常见，必须针对性优化
                    // 思路是在互调用时维护一个孪生栈帧 twinFrame
                    // 在执行 A 时将孪生栈帧指向 B，同理在执行 B 时将其指向 A
                    // 复用 C++ 的栈帧和循环，但在 A/B 两个栈帧中切换
                    if (twinFrame && twinFrame->graph() == funcFrame) {
                        // 如果缓存的孪生栈帧刚好是目标栈帧，则复用
                        EXEC_WHEN_DEBUG(l.in("FastVM").debug(
                            "Optimizing mutual-tail-recursion for graph: {}",
                            currFrame->graph()->name()));
                        // 交换孪生帧
                        currFrame = twinFrame;
                        twinFrame = lastFrame;
                    } else {
                        // 不可复用栈帧的尾调用
                        // 将当前栈帧设置为孪生帧
                        twinFrame = currFrame;

                        // 创建新的栈帧并设置参数
                        nextFrame              = framePool_.acquire(funcFrame);
                        size_t argsCnt         = bc.argsCnt();
                        const data_idx_t *args = bc.operands();
                        for (size_t i = 0; i < argsCnt; ++i) {
                            nextFrame->set(i + 1, currFrame->get<slot_t>(args[i]));
                        }

                        // 切换到新栈帧
                        currFrame = nextFrame;

                        // 跳过后续字节码，进行下一轮循环
                        i = codeSize;
                        continue;
                    }
                }

                // 自递归和互递归的情况在这里集中设置参数
                size_t argsCnt         = bc.argsCnt();
                const data_idx_t *args = bc.operands();
                for (size_t i = 0; i < argsCnt; ++i) {
                    currFrame->set(i + 1, lastFrame->get<slot_t>(args[i]));
                }

                // 跳过后续字节码，进行下一轮循环
                i = codeSize;
                continue;
            }

            case OpCode::OPER: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();
                auto func              = bc.extra()->func;
                EXEC_WHEN_DEBUG(l.in("FastVM").debug(
                    "Executing operator {}.",
                    context_->execMgr().getNameOfAnOperator(bc.extra()->func)));
                func(bc.result, nargs, wargs, *currFrame, *context_);
                break;
            }

            case OpCode::SCHD: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();
                auto mark              = bc.extra()->mark;
                evalMarkedOperator(mark, bc.result, nargs, wargs, *currFrame);
                break;
            }

            default: {
                context_->rtmDiags()
                    ->of(RuntimeDiag::UnsupportedBytecode)
                    .commit(to_string(bc.opcode));
            }
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

    slot_t res = currFrame->get<slot_t>(input.front()->index());

    if (twinFrame != nullptr) {
        // 说明已经触发了相互尾调用，要把孪生帧也释放掉
        // 相互尾调用优化时，currFrame 和 twinFrame 会互相切换
        // 把与传入的 frame 不相等的那个先释放掉即可
        if (currFrame == frame) {
            framePool_.release(twinFrame);
        } else {
            framePool_.release(currFrame);
        }
    }
    framePool_.release(frame);

    return res;
}

graph_ptr_t FastVMSchedPass::apply(graph_ptr_t &graph, std::ostream &os) {
    if (!graph->hasOutput()) {
        context_->rtmDiags()
            ->of(RuntimeDiag::MissingMainFunction)
            .commit(context_->mainModule()->name());
    }
    Frame *rootFrame = framePool_.acquire(graph.get());
    call(graph.get(), rootFrame);
    return Graph::null();
}

void FastVMSchedPass::evalMarkedOperator(
    const MarkOpCode op, data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    switch (op) {
    case MarkOpCode::MapArr:
        evalMarkedOperator_map_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::ApplyArr:
        evalMarkedOperator_apply_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::FilterArr:
        evalMarkedOperator_filter_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::ReduceArr:
        evalMarkedOperator_reduce_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::ForeachArr:
        evalMarkedOperator_foreach_arr(self, nargs, wargs, currFrame);
        break;
    default:
        ASSERT(false, "Unsupported marked operator in FastVM.");
    }
}

void FastVMSchedPass::evalMarkedOperator_map_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    // auto targetData = currFrame.get(nargs[0]);
    // auto funcData   = currFrame.get(wargs[0]);

    // auto func              = tt::as_shared<FunctionData>(funcData);
    // type_ptr_t funcRetType = func->funcType()->exitType();

    // auto applyMap = [&](const data_vec_t &inputVec) -> data_vec_t {
    //     data_vec_t res;
    //     res.reserve(inputVec.size());
    //     for (const auto &item : inputVec) {
    //         auto &targetGraph = func->graph();
    //         Frame frame(&targetGraph);
    //         if (targetGraph.hasClosure()) {
    //             const auto &functionData = tt::as_shared<FunctionData>(funcData);
    //             const auto &closureData  = functionData->closure();
    //             ASSERT(
    //                 closureData.size() == targetGraph.closure().size(),
    //                 "Function closure size mismatch.");
    //             for (size_t ci = 0; ci < closureData.size(); ++ci) {
    //                 frame.set(ci + 2, closureData[ci]);
    //             }
    //         }
    //         frame.set(1, item);
    //         res.push_back(call(&targetGraph, frame));
    //     }
    //     return res;
    // };

    // auto arrayData = tt::as_shared<ArrayData>(targetData);
    // currFrame.set(self, ArrayData::from(ArrayType::create(funcRetType),
    // applyMap(arrayData->raw())));
}

void FastVMSchedPass::evalMarkedOperator_apply_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    // auto targetData = currFrame.get(nargs[0]);
    // auto funcData   = currFrame.get(wargs[0]);
    // auto func       = tt::as_shared<FunctionData>(funcData);

    // auto applyFunc = [&](const data_ptr_t &item) -> data_ptr_t {
    //     auto &targetGraph = func->graph();
    //     Frame frame(&targetGraph);
    //     if (targetGraph.hasClosure()) {
    //         const auto &functionData = tt::as_shared<FunctionData>(funcData);
    //         const auto &closureData  = functionData->closure();
    //         ASSERT(
    //             closureData.size() == targetGraph.closure().size(),
    //             "Function closure size mismatch.");
    //         for (size_t ci = 0; ci < closureData.size(); ++ci) {
    //             frame.set(ci + 2, closureData[ci]);
    //         }
    //     }
    //     frame.set(1, item);
    //     return call(&targetGraph, frame);
    // };

    // for (auto &item : tt::as_shared<ArrayData>(targetData)->raw()) {
    //     item = applyFunc(item);
    // }

    // currFrame.set(self, targetData);
}

void FastVMSchedPass::evalMarkedOperator_filter_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    // auto targetData = currFrame.get(nargs[0]);
    // auto funcData   = currFrame.get(wargs[0]);
    // auto func       = tt::as_shared<FunctionData>(funcData);

    // auto shouldKeep = [&](const data_ptr_t &item) -> bool {
    //     auto &targetGraph = func->graph();
    //     Frame frame(&targetGraph);
    //     if (targetGraph.hasClosure()) {
    //         const auto &functionData = tt::as_shared<FunctionData>(funcData);
    //         const auto &closureData  = functionData->closure();
    //         ASSERT(
    //             closureData.size() == targetGraph.closure().size(),
    //             "Function closure size mismatch.");
    //         for (size_t ci = 0; ci < closureData.size(); ++ci) {
    //             frame.set(ci + 2, closureData[ci]);
    //         }
    //     }
    //     frame.set(1, item);
    //     auto result = call(&targetGraph, frame);
    //     return result->as<BoolData>(Type::Bool())->data();
    // };

    // auto filterSequence = [&](auto containerData, auto createFunc) {
    //     const auto &raw = containerData->raw();
    //     data_vec_t res;
    //     for (const auto &item : raw) {
    //         if (shouldKeep(item))
    //             res.push_back(item);
    //     }
    //     currFrame.set(self, createFunc(containerData->type(), std::move(res)));
    // };

    // filterSequence(tt::as_shared<ArrayData>(targetData), [](auto t, data_vec_t v) {
    //     return ArrayData::from(t, std::move(v));
    // });
}

void FastVMSchedPass::evalMarkedOperator_reduce_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    // auto targetData = currFrame.get(nargs[0]);
    // auto funcData   = currFrame.get(wargs[0]);
    // auto initData   = currFrame.get(wargs[1]);

    // auto func = tt::as_shared<FunctionData>(funcData);

    // data_vec_t elements = tt::as_shared<ArrayData>(targetData)->raw();

    // if (elements.empty()) {
    //     currFrame.set(self, initData);
    //     return;
    // }

    // data_ptr_t result = initData;
    // for (const auto &item : elements) {
    //     auto &targetGraph = func->graph();
    //     Frame frame(&targetGraph);

    //     if (targetGraph.hasClosure()) {
    //         const auto &functionData = tt::as_shared<FunctionData>(funcData);
    //         const auto &closureData  = functionData->closure();
    //         ASSERT(
    //             closureData.size() == targetGraph.closure().size(),
    //             "Function closure size mismatch.");
    //         for (size_t ci = 0; ci < closureData.size(); ++ci) {
    //             frame.set(ci + 3, closureData[ci]);
    //         }
    //     }

    //     const auto &ports = targetGraph.ports();
    //     frame.set(ports[0]->index(), result); // acc
    //     frame.set(ports[1]->index(), item);   // cur

    //     result = call(&targetGraph, frame); // 更新 result
    // }

    // currFrame.set(self, result);
}

void FastVMSchedPass::evalMarkedOperator_foreach_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    // auto targetData = currFrame.get(nargs[0]);
    // auto funcData   = currFrame.get(wargs[0]);
    // auto func       = tt::as_shared<FunctionData>(funcData);

    // auto applyFunc = [&](const data_ptr_t &item) {
    //     auto &targetGraph = func->graph();
    //     Frame frame(&targetGraph);
    //     if (targetGraph.hasClosure()) {
    //         const auto &functionData = tt::as_shared<FunctionData>(funcData);
    //         const auto &closureData  = functionData->closure();
    //         ASSERT(
    //             closureData.size() == targetGraph.closure().size(),
    //             "Function closure size mismatch.");
    //         for (size_t ci = 0; ci < closureData.size(); ++ci) {
    //             frame.set(ci + 2, closureData[ci]);
    //         }
    //     }
    //     frame.set(1, item);
    //     call(&targetGraph, frame); // 忽略返回值
    // };

    // for (const auto &item : tt::as_shared<ArrayData>(targetData)->raw()) {
    //     applyFunc(item);
    // }

    // currFrame.set(self, Data::null());
}
