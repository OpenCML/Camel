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
 * Created: Dec. 20, 2025
 * Updated: Dec. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "fastvm.h"

#ifndef ENABLE_COMPUTED_GOTO

#include "utils/opperf.h"

#ifndef NDEBUG
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"
#endif

#define DEF_BIN_OP_CASE(CODE, TYPE, OP)                                                            \
    case OpCode::CODE: {                                                                           \
        TYPE lhs = currFrame->get<TYPE>(bc.fastop[0]);                                             \
        TYPE rhs = currFrame->get<TYPE>(bc.fastop[1]);                                             \
        TYPE res = lhs OP rhs;                                                                     \
        currFrame->set(bc.result, res);                                                            \
    } break;

#define DEF_BIN_DIV_CASE(CODE, TYPE, ZERO_CHECK)                                                   \
    case OpCode::CODE: {                                                                           \
        TYPE lhs = currFrame->get<TYPE>(bc.fastop[0]);                                             \
        TYPE rhs = currFrame->get<TYPE>(bc.fastop[1]);                                             \
        if (rhs == ZERO_CHECK) {                                                                   \
            context_->rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();                        \
        }                                                                                          \
        TYPE res = lhs / rhs;                                                                      \
        currFrame->set(bc.result, res);                                                            \
    } break;

slot_t FastVMSchedPass::call(size_t pc, Frame *rootFrame) {
    Frame *currFrame = rootFrame;

    while (true) {
        const Bytecode &bc = bytecodes_[pc];
        EXEC_WHEN_DEBUG(
            l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(bc, context_)));

#ifdef OPPERF_ENABLED
        std::string tag;
        if (bc.opcode == OpCode::OPER) {
            tag = context_->execMgr().getNameOfAnOperator(bc.extra()->func);
        } else if (bc.opcode == OpCode::FUNC) {
            tag = bc.extra()->graph->name();
        }
        opperf::ScopeTimer _timer(bc.opcode, tag);
#else
        opperf::ScopeTimer _timer(bc.opcode);
#endif

        switch (static_cast<OpCode>(bc.opcode)) {
        case OpCode::RETN: {
            slot_t result = currFrame->get<slot_t>(bc.fastop[0]);

            if (currFrame == rootFrame) {
                return result;
            }

            framePool_.release(currFrame);

            auto [lastPC, lastFrame] = pop();
            pc                       = lastPC;
            currFrame                = lastFrame;

            Bytecode &lbc = bytecodes_[pc];
            currFrame->set(lbc.result, result);

            // 从下一条指令继续执行
            pc += lbc.opsize;
            continue;
        } break;

        case OpCode::CAST: {
            ASSERT(false, "CAST opcode not implemented in FastVM.");
        } break;

        case OpCode::COPY: {
            TypeCode srcType = currFrame->typeAt(bc.fastop[0]);
            if (isGCTraced(srcType)) {
                Object *srcData = currFrame->get<Object *>(bc.fastop[0]);
                currFrame->set(bc.result, srcData->clone(mm::autoSpace(), false));
            } else {
                slot_t srcData = currFrame->get<slot_t>(bc.fastop[0]);
                currFrame->set(bc.result, srcData);
            }
        } break;

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
        } break;

        case OpCode::JUMP: {
            pc = static_cast<arr_size_t>(bc.fastop[0]);
            continue; // skip i increment
        } break;

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
            pc += bc.opsize + jumpIdx;

            continue; // skip i increment
        } break;

        case OpCode::JOIN: {
            const data_arr_t nargs = bc.nargs();
            const data_arr_t wargs = bc.wargs();
            int32_t brIndex        = currFrame->get<int32_t>(nargs[0]);
            ASSERT(
                brIndex >= 0 && static_cast<size_t>(brIndex) < bc.withCnt(),
                "JOIN opcode choosen index out of range in FastVM.");
            slot_t result = currFrame->get<slot_t>(wargs[static_cast<size_t>(brIndex)]);
            currFrame->set(bc.result, result);
        } break;

        case OpCode::FILL: {
            const data_arr_t nargs = bc.nargs();
            const data_arr_t wargs = bc.wargs();

            TypeCode targetType = currFrame->typeAt(nargs[0]);
            ASSERT(isGCTraced(targetType), "FILL target type is not GC-traced in FastVM.");

            Object *target = currFrame->get<Object *>(nargs[0])->clone(mm::autoSpace());
            ASSERT(target != nullptr, "FILL target data is null.");

            switch (targetType) {
            case TypeCode::Tuple: {
                const auto &type = currFrame->typePtrAt<TupleType>(bc.result);
                auto t           = static_cast<Tuple *>(target);
                const auto &refs = t->layout().refs();
                ASSERT(
                    refs.size() == bc.withCnt(),
                    std::format(
                        "Tuple layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
                        bc.withCnt(),
                        refs.size()));
                for (size_t j = 0; j < bc.withCnt(); ++j) {
                    t->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
                }
                t->updateLayout(&type->layout());
            } break;

            case TypeCode::Array: {
                const auto &type = currFrame->typePtrAt<ArrayType>(bc.result);
                auto a           = static_cast<Array *>(target);
                const auto &refs = a->layout().refs();
                ASSERT(
                    refs.size() == bc.withCnt(),
                    std::format(
                        "Array layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
                        bc.withCnt(),
                        refs.size()));
                for (size_t j = 0; j < bc.withCnt(); ++j) {
                    a->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
                }
                a->updateLayout(&type->layout());
            } break;

            case TypeCode::Struct: {
                const auto &type = currFrame->typePtrAt<StructType>(bc.result);
                auto s           = static_cast<Struct *>(target);
                const auto &refs = s->layout().refs();
                ASSERT(
                    refs.size() == bc.withCnt(),
                    std::format(
                        "Struct layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
                        bc.withCnt(),
                        refs.size()));
                for (size_t j = 0; j < bc.withCnt(); ++j) {
                    s->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
                }
                s->updateLayout(&type->layout());
            } break;

            case TypeCode::Function: {
                // const auto &type   = currFrame->typePtrAt<FunctionType>(bc.result);
                auto f             = static_cast<Function *>(target);
                Tuple *closureData = f->tuple();
                for (size_t j = 0; j < bc.withCnt(); ++j) {
                    closureData->set<slot_t>(j, currFrame->get<slot_t>(wargs[j]));
                }
            } break;

            default:
                ASSERT(
                    false,
                    std::format(
                        "Unsupported FILL target type {} in FastVM.",
                        typeCodeToString(targetType)));
            }

            currFrame->set(bc.result, target);
        } break;

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

            _timer.pause();
            const auto &result = call(offsetMap_.at(targetGraph), funcFrame);
            _timer.resume();

            framePool_.release(funcFrame);

            currFrame->set(bc.result, result);
        } break;

        case OpCode::FUNC: {
            // 保存当前程序计数器和栈帧
            push(pc, currFrame);

            // 创建新的栈帧并设置参数
            Frame *funcFrame       = framePool_.acquire(bc.extra()->graph);
            size_t argsCnt         = bc.normCnt();
            const data_idx_t *args = bc.operands();
            for (size_t i = 0; i < argsCnt; ++i) {
                funcFrame->set(i + 1, currFrame->get<slot_t>(args[i]));
            }

            // 切换到目标图的字节码位置和栈帧
            pc        = bc.fastop[1];
            currFrame = funcFrame;

            continue;
        } break;

        case OpCode::TAIL: {
            // 尾调用不保存程序计数器和栈帧
            // 直接释放当前栈帧
            // 如果当前栈帧和目标栈帧属于同一个图
            // 栈帧池会自动复用
            // 这间接实现了尾调用优化
            FrameView lastFrame(currFrame);
            framePool_.release(currFrame);

            // 创建新的栈帧并设置参数
            // 对于刚刚释放的栈帧，栈帧池会自动复用
            // 所以这里 currFrame 就是目标栈帧
            currFrame              = framePool_._acquire(bc.extra()->graph);
            size_t argsCnt         = bc.normCnt();
            const data_idx_t *args = bc.operands();
            for (size_t i = 0; i < argsCnt; ++i) {
                // 注意，这里的 currFrame 已经被释放了
                // 但由于栈帧池不会对已经释放的栈帧进行格式化
                // 所以这里仍然可以安全地获取原栈帧的数据
                // 当然，需要通过 lastFrame 来获取数据
                // lastFrame 中保存了原栈帧的静态数据区指针
                currFrame->set(i + 1, lastFrame.get<slot_t>(args[i]));
            }
            // 这里需要手动 resetTop，因为 _acquire 不会 resetTop
            // 之所以延迟 resetTop，是为了避免 resetTop 破坏刚刚释放的栈帧的数据
            framePool_._resetTop();

            // 切换到目标图的字节码位置
            pc = bc.fastop[1];

            continue;
        } break;

        case OpCode::OPER: {
            const data_arr_t nargs = bc.nargs();
            const data_arr_t wargs = bc.wargs();
            auto func              = bc.extra()->func;
            EXEC_WHEN_DEBUG(l.in("FastVM").debug(
                "Executing operator {}.",
                context_->execMgr().getNameOfAnOperator(func)));
            func(bc.result, nargs, wargs, *currFrame, *context_);
        } break;

        case OpCode::SCHD: {
            const data_arr_t nargs = bc.nargs();
            const data_arr_t wargs = bc.wargs();
            auto mark              = bc.extra()->mark;
            evalMarkedOperator(mark, bc.result, nargs, wargs, *currFrame);
        } break;

            DEF_BIN_OP_CASE(IADD, Int, +);
            DEF_BIN_OP_CASE(LADD, Long, +);
            DEF_BIN_OP_CASE(FADD, Float, +);
            DEF_BIN_OP_CASE(DADD, Double, +);

            DEF_BIN_OP_CASE(ISUB, Int, -);
            DEF_BIN_OP_CASE(LSUB, Long, -);
            DEF_BIN_OP_CASE(FSUB, Float, -);
            DEF_BIN_OP_CASE(DSUB, Double, -);

            DEF_BIN_OP_CASE(IMUL, Int, *);
            DEF_BIN_OP_CASE(LMUL, Long, *);
            DEF_BIN_OP_CASE(FMUL, Float, *);
            DEF_BIN_OP_CASE(DMUL, Double, *);

            DEF_BIN_DIV_CASE(IDIV, Int, 0);
            DEF_BIN_DIV_CASE(LDIV, Long, 0);
            DEF_BIN_DIV_CASE(FDIV, Float, 0.0f);
            DEF_BIN_DIV_CASE(DDIV, Double, 0.0);

            DEF_BIN_OP_CASE(ILT, Int, <);
            DEF_BIN_OP_CASE(LLT, Long, <);
            DEF_BIN_OP_CASE(FLT, Float, <);
            DEF_BIN_OP_CASE(DLT, Double, <);

            DEF_BIN_OP_CASE(IGT, Int, >);
            DEF_BIN_OP_CASE(LGT, Long, >);
            DEF_BIN_OP_CASE(FGT, Float, >);
            DEF_BIN_OP_CASE(DGT, Double, >);

            DEF_BIN_OP_CASE(IEQ, Int, ==);
            DEF_BIN_OP_CASE(LEQ, Int, ==);
            DEF_BIN_OP_CASE(FEQ, Float, ==);
            DEF_BIN_OP_CASE(DEQ, Double, ==);

            DEF_BIN_OP_CASE(INE, Int, !=);
            DEF_BIN_OP_CASE(LNE, Long, !=);
            DEF_BIN_OP_CASE(FNE, Float, !=);
            DEF_BIN_OP_CASE(DNE, Double, !=);

            DEF_BIN_OP_CASE(ILE, Int, <=);
            DEF_BIN_OP_CASE(LLE, Long, <=);
            DEF_BIN_OP_CASE(FLE, Float, <=);
            DEF_BIN_OP_CASE(DLE, Double, <=);

            DEF_BIN_OP_CASE(IGE, Int, >=);
            DEF_BIN_OP_CASE(LGE, Long, >=);
            DEF_BIN_OP_CASE(FGE, Float, >=);
            DEF_BIN_OP_CASE(DGE, Double, >=);

        default: {
            context_->rtmDiags()->of(RuntimeDiag::UnsupportedBytecode).commit(to_string(bc.opcode));
        }
        }

        // move to the next bytecode
        pc += bc.opsize;
    }
}

#endif // ENABLE_COMPUTED_GOTO
