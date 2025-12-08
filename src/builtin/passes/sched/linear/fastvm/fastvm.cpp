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
 * Updated: Dec. 08, 2025
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

GraphExecInfo *FastVMSchedPass::getExecInfoGraph(Graph *graph) {
    auto it = graphExecInfoMap_.find(graph);
    if (it == graphExecInfoMap_.end()) {
        auto codes = precompile(
            context_,
            graph,
            {
                .enableInlineOperators = true,
            });

        auto [insertedIt, success] = graphExecInfoMap_.emplace(
            graph,
            GraphExecInfo{
                .ftemp = FrameTemplate(graph, staticAllocator_, stackAllocator_),
                .codes = std::move(codes),
            });

        return &insertedIt->second;
    }

    return &it->second;
}

data_ptr_t FastVMSchedPass::call(Graph *graph, Frame &frame) {
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Evaluating graph: {}", graph->name()));

    if (currRecursionDepth_++ > maxRecursionDepth_) {
        context_->rtmDiags()
            ->of(RuntimeDiag::MaxRecursionDepthExceeded)
            .commit(graph->name(), maxRecursionDepth_);
    }

    bool loop  = false;
    auto info  = getExecInfoGraph(graph);
    auto codes = &info->codes;

    Frame nextFrame(info->ftemp); // for mutual-tail-recursion optimization
    frame_rptr_t currFrame = &frame, twinFrame = nullptr;

    auto __call =
        [&](GraphIR::Graph &targetGraph, const Bytecode &bc, bool isTailCall) -> data_ptr_t {
        EXEC_WHEN_DEBUG(l.in("FastVM").debug(
            "Calling function: {} (tail-call: {})",
            targetGraph.name(),
            isTailCall ? "yes" : "no"));

        ASSERT(
            bc.argsCnt() == targetGraph.normPorts().size() + targetGraph.withPorts().size(),
            std::format(
                "Function {} expects {} arguments, but got {}.",
                targetGraph.name(),
                targetGraph.normPorts().size() + targetGraph.withPorts().size(),
                bc.argsCnt()));

        // 如果是尾调用，则尝试复用当前 C++ 栈帧
        if (isTailCall) {
            // enable tail-call optimization
            // 设置 loop flag 为 true
            // 当前字节码执行子循环结束后不会退出大循环
            // 而会根据新指定的 currFrame 和 bytecodes 执行新图
            // 这样可以复用当前 C++ 栈帧，避免 C++ 栈溢出
            loop = true;
            // 设置 lastFrame 为当前帧 currFrame 备用
            const frame_rptr_t lastFrame = currFrame;

            // 下面准备将 currFrame 重新指向新栈帧
            if (&targetGraph == currFrame->graph()) {
                // 如果目标图就是当前帧的图，说明在进行自递归尾调用
                // 此时可以复用当前栈帧和字节码，无需修改栈帧指向
                EXEC_WHEN_DEBUG(l.in("FastVM").debug(
                    "Optimizing self-recursion for graph: {}",
                    currFrame->graph()->name()));
            } else {
                // 否则需要切换到目标图的新字节码，并修改 currFrame 指向
                auto targetInfo = getExecInfoGraph(&targetGraph);
                codes           = &targetInfo->codes;

                // 即便目标图不是当前图，仍可能存在互调用尾递归现象
                // 即 A 尾调用 B，B 又尾调用 A
                // Camel 中分支默认被编译为子图，互调用非常常见，必须针对性优化
                // 思路是在互调用时维护一个孪生栈帧 twinFrame
                // 在执行 A 时将孪生栈帧指向 B，同理在执行 B 时将其指向 A
                // 复用 C++ 的栈帧和循环，但在 A/B 两个栈帧中切换

                if (twinFrame && twinFrame->graph() == &targetGraph) {
                    // 如果缓存的孪生栈帧刚好是目标栈帧，则复用
                    EXEC_WHEN_DEBUG(l.in("FastVM").debug(
                        "Optimizing mutual-tail-recursion for graph: {}",
                        currFrame->graph()->name()));
                    currFrame = twinFrame;
                    twinFrame = lastFrame;
                } else {
                    // 将当前栈帧设置为孪生帧
                    twinFrame = currFrame;

                    // 创建新的栈帧并设置参数
                    Frame targetFrame(targetInfo->ftemp);
                    size_t argsCnt         = bc.argsCnt();
                    const data_idx_t *args = bc.operands();
                    for (size_t i = 0; i < argsCnt; ++i) {
                        targetFrame.set(i + 1, currFrame->get<slot_t>(args[i]));
                    }

                    // 将栈帧对象保存到 nextFrame 中，以免离开作用域后销毁
                    // 这里不能直接覆写 currFrame 所指向的栈帧
                    // 因为该栈帧要保留以备互调用使用
                    nextFrame = std::move(targetFrame);

                    // 切换到新栈帧
                    currFrame = &nextFrame;
                    return nullptr; // indicate tail-call
                }
            }

            // 自递归和互递归的情况在这里集中设置参数
            size_t argsCnt         = bc.argsCnt();
            const data_idx_t *args = bc.operands();
            for (size_t i = 0; i < argsCnt; ++i) {
                currFrame->set(i + 1, lastFrame->get<slot_t>(args[i]));
            }

            return nullptr; // indicate tail-call
        }

        // 如果不是尾调用，则新建栈帧并递归调用 call 函数
        auto targetInfo = getExecInfoGraph(&targetGraph);
        Frame targetFrame(targetInfo->ftemp);

        size_t argsCnt         = bc.argsCnt();
        const data_idx_t *args = bc.operands();
        for (size_t i = 0; i < argsCnt; ++i) {
            targetFrame.set(i + 1, currFrame->get<slot_t>(args[i]));
        }

        return call(&targetGraph, targetFrame);
    };

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
                    GCRef srcData = currFrame->get<GCRef>(bc.fastop[0]);
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
                    GCTuple *t = currFrame->get<GCTuple *>(bc.fastop[0]);
                    ASSERT(
                        static_cast<size_t>(bc.fastop[1]) < t->size(),
                        "Tuple access index out of range in FastVM.");
                    currFrame->set(bc.result, t->get<slot_t>(static_cast<size_t>(bc.fastop[1])));
                } else if (srcType == TypeCode::Struct) {
                    GCStruct *s = currFrame->get<GCStruct *>(bc.fastop[0]);
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
                slot_t condData        = currFrame->get<slot_t>(nargs[0]);

                size_t jumpIdx = 0;

                if (bc.withCnt() == 0) {
                    if (static_cast<bool>(condData)) {
                        jumpIdx = 0; // jump to true branch
                    } else {
                        jumpIdx = 1; // jump to false branch
                    }
                } else {
                    size_t j = 0;
                    for (; j < bc.withCnt(); ++j) {
                        auto caseData = currFrame->get(wargs[j]);
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

                currFrame->set(bc.result, std::make_shared<IntData>(static_cast<int32_t>(jumpIdx)));
                i += bc.opsize + jumpIdx;

                continue; // skip i increment
            }

            case OpCode::JOIN: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();
                const auto &input      = currFrame->get(nargs[0]);
                int32_t choosenIdx     = tt::as_shared<IntData>(input)->data();
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
                auto target            = currFrame->get(nargs[0])->clone();
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
                auto funcData          = currFrame->get(wargs[0]);
                auto function          = tt::as_shared<FunctionData>(funcData);
                auto &targetGraph      = function->graph();
                Frame funcFrame(&targetGraph);

                size_t i = 0;
                for (; i < nargs.size; ++i) {
                    funcFrame.set(i + 1, currFrame->get(nargs[i]));
                }

                const auto &closureData = function->closure();
                ASSERT(
                    targetGraph.closure().size() == closureData.size(),
                    "Function closure size mismatch in FastVM.");
                for (size_t j = 0; j < closureData.size(); ++j) {
                    funcFrame.set(i + j + 1, closureData[j]);
                }

                const auto &result = call(&targetGraph, funcFrame);
                currFrame->set(bc.result, result);
                break;
            }

            case OpCode::FUNC: {
                currFrame->set(bc.result, __call(*bc.extra()->graph, bc, false));
                break;
            }

            case OpCode::TAIL: {
                __call(*bc.extra()->graph, bc, true);

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
                if (isOpCodeOfInlinedOperator(bc.opcode)) {
                    const data_ptr_t &lhs = currFrame->get(bc.fastop[0]);
                    const data_ptr_t &rhs = currFrame->get(bc.fastop[1]);

                    switch (bc.opcode) {
                    case OpCode::IADD: {
                        int32_t res = tt::as_shared<IntData>(lhs)->data() +
                                      tt::as_shared<IntData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<IntData>(res));
                        break;
                    }
                    case OpCode::LADD: {
                        int64_t res = tt::as_shared<LongData>(lhs)->data() +
                                      tt::as_shared<LongData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<LongData>(res));
                        break;
                    }
                    case OpCode::FADD: {
                        float res = tt::as_shared<FloatData>(lhs)->data() +
                                    tt::as_shared<FloatData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<FloatData>(res));
                        break;
                    }
                    case OpCode::DADD: {
                        double res = tt::as_shared<DoubleData>(lhs)->data() +
                                     tt::as_shared<DoubleData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<DoubleData>(res));
                        break;
                    }

                    case OpCode::ISUB: {
                        int32_t res = tt::as_shared<IntData>(lhs)->data() -
                                      tt::as_shared<IntData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<IntData>(res));
                        break;
                    }
                    case OpCode::LSUB: {
                        int64_t res = tt::as_shared<LongData>(lhs)->data() -
                                      tt::as_shared<LongData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<LongData>(res));
                        break;
                    }
                    case OpCode::FSUB: {
                        float res = tt::as_shared<FloatData>(lhs)->data() -
                                    tt::as_shared<FloatData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<FloatData>(res));
                        break;
                    }
                    case OpCode::DSUB: {
                        double res = tt::as_shared<DoubleData>(lhs)->data() -
                                     tt::as_shared<DoubleData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<DoubleData>(res));
                        break;
                    }

                    case OpCode::IMUL: {
                        int32_t res = tt::as_shared<IntData>(lhs)->data() *
                                      tt::as_shared<IntData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<IntData>(res));
                        break;
                    }
                    case OpCode::LMUL: {
                        int64_t res = tt::as_shared<LongData>(lhs)->data() *
                                      tt::as_shared<LongData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<LongData>(res));
                        break;
                    }
                    case OpCode::FMUL: {
                        float res = tt::as_shared<FloatData>(lhs)->data() *
                                    tt::as_shared<FloatData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<FloatData>(res));
                        break;
                    }
                    case OpCode::DMUL: {
                        double res = tt::as_shared<DoubleData>(lhs)->data() *
                                     tt::as_shared<DoubleData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<DoubleData>(res));
                        break;
                    }

                    case OpCode::IDIV: {
                        int32_t divisor = tt::as_shared<IntData>(rhs)->data();
                        if (divisor == 0) {
                            context_->rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
                        }
                        int32_t res = tt::as_shared<IntData>(lhs)->data() / divisor;
                        currFrame->set(bc.result, std::make_shared<IntData>(res));
                        break;
                    }
                    case OpCode::LDIV: {
                        int64_t divisor = tt::as_shared<LongData>(rhs)->data();
                        if (divisor == 0) {
                            context_->rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
                        }
                        int64_t res = tt::as_shared<LongData>(lhs)->data() / divisor;
                        currFrame->set(bc.result, std::make_shared<LongData>(res));
                        break;
                    }
                    case OpCode::FDIV: {
                        float divisor = tt::as_shared<FloatData>(rhs)->data();
                        if (divisor == 0.0f) {
                            context_->rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
                        }
                        float res = tt::as_shared<FloatData>(lhs)->data() / divisor;
                        currFrame->set(bc.result, std::make_shared<FloatData>(res));
                        break;
                    }
                    case OpCode::DDIV: {
                        double divisor = tt::as_shared<DoubleData>(rhs)->data();
                        if (divisor == 0.0) {
                            context_->rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
                        }
                        double res = tt::as_shared<DoubleData>(lhs)->data() / divisor;
                        currFrame->set(bc.result, std::make_shared<DoubleData>(res));
                        break;
                    }

                    case OpCode::ILT: {
                        bool res = tt::as_shared<IntData>(lhs)->data() <
                                   tt::as_shared<IntData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::LLT: {
                        bool res = tt::as_shared<LongData>(lhs)->data() <
                                   tt::as_shared<LongData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::FLT: {
                        bool res = tt::as_shared<FloatData>(lhs)->data() <
                                   tt::as_shared<FloatData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::DLT: {
                        bool res = tt::as_shared<DoubleData>(lhs)->data() <
                                   tt::as_shared<DoubleData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }

                    case OpCode::IGT: {
                        bool res = tt::as_shared<IntData>(lhs)->data() >
                                   tt::as_shared<IntData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::LGT: {
                        bool res = tt::as_shared<LongData>(lhs)->data() >
                                   tt::as_shared<LongData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::FGT: {
                        bool res = tt::as_shared<FloatData>(lhs)->data() >
                                   tt::as_shared<FloatData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::DGT: {
                        bool res = tt::as_shared<DoubleData>(lhs)->data() >
                                   tt::as_shared<DoubleData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }

                    case OpCode::IEQ: {
                        bool res = tt::as_shared<IntData>(lhs)->data() ==
                                   tt::as_shared<IntData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::LEQ: {
                        bool res = tt::as_shared<LongData>(lhs)->data() ==
                                   tt::as_shared<LongData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::FEQ: {
                        bool res = tt::as_shared<FloatData>(lhs)->data() ==
                                   tt::as_shared<FloatData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::DEQ: {
                        bool res = tt::as_shared<DoubleData>(lhs)->data() ==
                                   tt::as_shared<DoubleData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }

                    case OpCode::INE: {
                        bool res = tt::as_shared<IntData>(lhs)->data() !=
                                   tt::as_shared<IntData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::LNE: {
                        bool res = tt::as_shared<LongData>(lhs)->data() !=
                                   tt::as_shared<LongData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::FNE: {
                        bool res = tt::as_shared<FloatData>(lhs)->data() !=
                                   tt::as_shared<FloatData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::DNE: {
                        bool res = tt::as_shared<DoubleData>(lhs)->data() !=
                                   tt::as_shared<DoubleData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }

                    case OpCode::ILE: {
                        bool res = tt::as_shared<IntData>(lhs)->data() <=
                                   tt::as_shared<IntData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::LLE: {
                        bool res = tt::as_shared<LongData>(lhs)->data() <=
                                   tt::as_shared<LongData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::FLE: {
                        bool res = tt::as_shared<FloatData>(lhs)->data() <=
                                   tt::as_shared<FloatData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::DLE: {
                        bool res = tt::as_shared<DoubleData>(lhs)->data() <=
                                   tt::as_shared<DoubleData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }

                    case OpCode::IGE: {
                        bool res = tt::as_shared<IntData>(lhs)->data() >=
                                   tt::as_shared<IntData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::LGE: {
                        bool res = tt::as_shared<LongData>(lhs)->data() >=
                                   tt::as_shared<LongData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::FGE: {
                        bool res = tt::as_shared<FloatData>(lhs)->data() >=
                                   tt::as_shared<FloatData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }
                    case OpCode::DGE: {
                        bool res = tt::as_shared<DoubleData>(lhs)->data() >=
                                   tt::as_shared<DoubleData>(rhs)->data();
                        currFrame->set(bc.result, std::make_shared<BoolData>(res));
                        break;
                    }

                    default:
                        ASSERT(false, "Unsupported inlined operator.");
                    }

                    break;
                }

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

    return result;
}

graph_ptr_t FastVMSchedPass::apply(graph_ptr_t &graph, std::ostream &os) {
    if (!graph->hasOutput()) {
        context_->rtmDiags()
            ->of(RuntimeDiag::MissingMainFunction)
            .commit(context_->mainModule()->name());
    }
    Frame rootFrame(graph.get());
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
    auto targetData = currFrame.get(nargs[0]);
    auto funcData   = currFrame.get(wargs[0]);

    auto func              = tt::as_shared<FunctionData>(funcData);
    type_ptr_t funcRetType = func->funcType()->exitType();

    auto applyMap = [&](const data_vec_t &inputVec) -> data_vec_t {
        data_vec_t res;
        res.reserve(inputVec.size());
        for (const auto &item : inputVec) {
            auto &targetGraph = func->graph();
            Frame frame(&targetGraph);
            if (targetGraph.hasClosure()) {
                const auto &functionData = tt::as_shared<FunctionData>(funcData);
                const auto &closureData  = functionData->closure();
                ASSERT(
                    closureData.size() == targetGraph.closure().size(),
                    "Function closure size mismatch.");
                for (size_t ci = 0; ci < closureData.size(); ++ci) {
                    frame.set(ci + 2, closureData[ci]);
                }
            }
            frame.set(1, item);
            res.push_back(call(&targetGraph, frame));
        }
        return res;
    };

    auto arrayData = tt::as_shared<ArrayData>(targetData);
    currFrame.set(self, ArrayData::from(Type::Array(funcRetType), applyMap(arrayData->raw())));
}

void FastVMSchedPass::evalMarkedOperator_apply_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    auto targetData = currFrame.get(nargs[0]);
    auto funcData   = currFrame.get(wargs[0]);
    auto func       = tt::as_shared<FunctionData>(funcData);

    auto applyFunc = [&](const data_ptr_t &item) -> data_ptr_t {
        auto &targetGraph = func->graph();
        Frame frame(&targetGraph);
        if (targetGraph.hasClosure()) {
            const auto &functionData = tt::as_shared<FunctionData>(funcData);
            const auto &closureData  = functionData->closure();
            ASSERT(
                closureData.size() == targetGraph.closure().size(),
                "Function closure size mismatch.");
            for (size_t ci = 0; ci < closureData.size(); ++ci) {
                frame.set(ci + 2, closureData[ci]);
            }
        }
        frame.set(1, item);
        return call(&targetGraph, frame);
    };

    for (auto &item : tt::as_shared<ArrayData>(targetData)->raw()) {
        item = applyFunc(item);
    }

    currFrame.set(self, targetData);
}

void FastVMSchedPass::evalMarkedOperator_filter_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    auto targetData = currFrame.get(nargs[0]);
    auto funcData   = currFrame.get(wargs[0]);
    auto func       = tt::as_shared<FunctionData>(funcData);

    auto shouldKeep = [&](const data_ptr_t &item) -> bool {
        auto &targetGraph = func->graph();
        Frame frame(&targetGraph);
        if (targetGraph.hasClosure()) {
            const auto &functionData = tt::as_shared<FunctionData>(funcData);
            const auto &closureData  = functionData->closure();
            ASSERT(
                closureData.size() == targetGraph.closure().size(),
                "Function closure size mismatch.");
            for (size_t ci = 0; ci < closureData.size(); ++ci) {
                frame.set(ci + 2, closureData[ci]);
            }
        }
        frame.set(1, item);
        auto result = call(&targetGraph, frame);
        return result->as<BoolData>(Type::Bool())->data();
    };

    auto filterSequence = [&](auto containerData, auto createFunc) {
        const auto &raw = containerData->raw();
        data_vec_t res;
        for (const auto &item : raw) {
            if (shouldKeep(item))
                res.push_back(item);
        }
        currFrame.set(self, createFunc(containerData->type(), std::move(res)));
    };

    filterSequence(tt::as_shared<ArrayData>(targetData), [](auto t, data_vec_t v) {
        return ArrayData::from(t, std::move(v));
    });
}

void FastVMSchedPass::evalMarkedOperator_reduce_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    auto targetData = currFrame.get(nargs[0]);
    auto funcData   = currFrame.get(wargs[0]);
    auto initData   = currFrame.get(wargs[1]);

    auto func = tt::as_shared<FunctionData>(funcData);

    data_vec_t elements = tt::as_shared<ArrayData>(targetData)->raw();

    if (elements.empty()) {
        currFrame.set(self, initData);
        return;
    }

    data_ptr_t result = initData;
    for (const auto &item : elements) {
        auto &targetGraph = func->graph();
        Frame frame(&targetGraph);

        if (targetGraph.hasClosure()) {
            const auto &functionData = tt::as_shared<FunctionData>(funcData);
            const auto &closureData  = functionData->closure();
            ASSERT(
                closureData.size() == targetGraph.closure().size(),
                "Function closure size mismatch.");
            for (size_t ci = 0; ci < closureData.size(); ++ci) {
                frame.set(ci + 3, closureData[ci]);
            }
        }

        const auto &ports = targetGraph.ports();
        frame.set(ports[0]->index(), result); // acc
        frame.set(ports[1]->index(), item);   // cur

        result = call(&targetGraph, frame); // 更新 result
    }

    currFrame.set(self, result);
}

void FastVMSchedPass::evalMarkedOperator_foreach_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    auto targetData = currFrame.get(nargs[0]);
    auto funcData   = currFrame.get(wargs[0]);
    auto func       = tt::as_shared<FunctionData>(funcData);

    auto applyFunc = [&](const data_ptr_t &item) {
        auto &targetGraph = func->graph();
        Frame frame(&targetGraph);
        if (targetGraph.hasClosure()) {
            const auto &functionData = tt::as_shared<FunctionData>(funcData);
            const auto &closureData  = functionData->closure();
            ASSERT(
                closureData.size() == targetGraph.closure().size(),
                "Function closure size mismatch.");
            for (size_t ci = 0; ci < closureData.size(); ++ci) {
                frame.set(ci + 2, closureData[ci]);
            }
        }
        frame.set(1, item);
        call(&targetGraph, frame); // 忽略返回值
    };

    for (const auto &item : tt::as_shared<ArrayData>(targetData)->raw()) {
        applyFunc(item);
    }

    currFrame.set(self, Data::null());
}
