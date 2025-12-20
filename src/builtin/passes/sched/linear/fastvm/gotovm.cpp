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
 * Updated: Dec. 20, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "fastvm.h"

#ifdef ENABLE_COMPUTED_GOTO

/**
 * Computed Goto implementation of FastVM.
 *
 * This version of FastVM uses the "Computed Goto" technique for bytecode dispatch.
 *
 * Basic idea:
 * Instead of using a large switch-case statement to handle different opcodes,
 * this approach maintains a jump table that maps each opcode directly to the
 * corresponding instruction handler (label address). The VM fetches the next
 * opcode, looks up the handler's address in the jump table, and jumps to it
 * directly via an indirect goto instruction.
 *
 * Why it's faster:
 * - Avoids the repeated branch comparisons in a switch-based dispatcher.
 * - Reduces branch misprediction penalties, since the CPU can learn common
 *   jump patterns in bytecode sequences more easily.
 * - Eliminates unnecessary instruction dispatch overhead and improves I-cache
 *   locality in the main interpreter loop.
 *
 * In high-frequency dispatch scenarios (such as bytecode interpreters or
 * state machines), this approach can typically yield a **10%–20% performance
 * improvement** compared to a traditional switch-based implementation,
 * depending on workload and CPU architecture.
 */

#include "utils/opperf.h"

#ifndef NDEBUG
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"
#endif

#define NEXT()                                                                                     \
    do {                                                                                           \
        pc += bc->opsize;                                                                          \
        bc = &base[pc];                                                                            \
        goto *dispatchTable[static_cast<size_t>(bc->opcode)];                                      \
    } while (0)

#define JUMP()                                                                                     \
    do {                                                                                           \
        bc = &base[pc];                                                                            \
        goto *dispatchTable[static_cast<size_t>(bc->opcode)];                                      \
    } while (0)

#define DEF_BIN_OP_LABEL(LABEL, TYPE, OP)                                                          \
    label_##LABEL : {                                                                              \
        TYPE lhs = currFrame->get<TYPE>(bc->fastop[0]);                                            \
        TYPE rhs = currFrame->get<TYPE>(bc->fastop[1]);                                            \
        TYPE res = lhs OP rhs;                                                                     \
        currFrame->set(bc->result, res);                                                           \
        NEXT();                                                                                    \
    }

#define DEF_BIN_DIV_LABEL(LABEL, TYPE, ZERO_CHECK)                                                 \
    label_##LABEL : {                                                                              \
        TYPE lhs = currFrame->get<TYPE>(bc->fastop[0]);                                            \
        TYPE rhs = currFrame->get<TYPE>(bc->fastop[1]);                                            \
        if (rhs == ZERO_CHECK) {                                                                   \
            context_->rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();                        \
        }                                                                                          \
        TYPE res = lhs / rhs;                                                                      \
        currFrame->set(bc->result, res);                                                           \
        NEXT();                                                                                    \
    }

using namespace std;
using namespace GraphIR;

slot_t FastVMSchedPass::call(size_t pc, Frame *rootFrame) {
    Frame *currFrame     = rootFrame;
    const Bytecode *base = bytecodes_.data();
    const Bytecode *bc;

    static void *dispatchTable[256] = {
        &&label_RETN, // 0
        &&label_CAST, // 1
        &&label_COPY, // 2
        &&label_ACCS, // 3
        &&label_JUMP, // 4
        &&label_BRCH, // 5
        &&label_JOIN, // 6
        &&label_FILL, // 7
        &&label_CALL, // 8
        &&label_FUNC, // 9
        &&label_TAIL, // 10
        &&label_OPER, // 11
        &&label_SCHD, // 12

        // 内联算子
        &&label_IADD,
        &&label_LADD,
        &&label_FADD,
        &&label_DADD,
        &&label_ISUB,
        &&label_LSUB,
        &&label_FSUB,
        &&label_DSUB,
        &&label_IMUL,
        &&label_LMUL,
        &&label_FMUL,
        &&label_DMUL,
        &&label_IDIV,
        &&label_LDIV,
        &&label_FDIV,
        &&label_DDIV,
        &&label_ILT,
        &&label_LLT,
        &&label_FLT,
        &&label_DLT,
        &&label_IGT,
        &&label_LGT,
        &&label_FGT,
        &&label_DGT,
        &&label_IEQ,
        &&label_LEQ,
        &&label_FEQ,
        &&label_DEQ,
        &&label_INE,
        &&label_LNE,
        &&label_FNE,
        &&label_DNE,
        &&label_ILE,
        &&label_LLE,
        &&label_FLE,
        &&label_DLE,
        &&label_IGE,
        &&label_LGE,
        &&label_FGE,
        &&label_DGE,
    };

    // 初次分派
    JUMP();

label_RETN: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    slot_t result = currFrame->get<slot_t>(bc->fastop[0]);

    if (currFrame == rootFrame) {
        return result;
    }

    framePool_.release(currFrame);

    auto [lastPC, lastFrame] = pop();
    pc                       = lastPC;
    bc                       = &bytecodes_[pc];
    currFrame                = lastFrame;

    Bytecode &lbc = bytecodes_[pc];
    currFrame->set(lbc.result, result);

    NEXT();
}

label_CAST: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    ASSERT(false, "CAST opcode not implemented in FastVM.");

    NEXT();
}

label_COPY: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    TypeCode srcType = currFrame->typeAt(bc->fastop[0]);
    if (isGCTraced(srcType)) {
        Object *srcData = currFrame->get<Object *>(bc->fastop[0]);
        currFrame->set(bc->result, srcData->clone(mm::autoSpace(), false));
    } else {
        slot_t srcData = currFrame->get<slot_t>(bc->fastop[0]);
        currFrame->set(bc->result, srcData);
    }

    NEXT();
}

label_ACCS: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    TypeCode srcType = currFrame->typeAt(bc->fastop[0]);
    if (srcType == TypeCode::Tuple) {
        Tuple *t = currFrame->get<Tuple *>(bc->fastop[0]);
        ASSERT(
            static_cast<size_t>(bc->fastop[1]) < t->size(),
            "Tuple access index out of range in FastVM.");
        currFrame->set(bc->result, t->get<slot_t>(static_cast<size_t>(bc->fastop[1])));
    } else if (srcType == TypeCode::Struct) {
        Struct *s = currFrame->get<Struct *>(bc->fastop[0]);
        ASSERT(
            static_cast<size_t>(bc->fastop[1]) < s->size(),
            "Struct access index out of range in FastVM.");
        currFrame->set(bc->result, s->get<slot_t>(static_cast<size_t>(bc->fastop[1])));
    } else {
        ASSERT(false, "ACCS opcode unsupported source type in FastVM.");
    }

    NEXT();
}

label_JUMP: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    pc = static_cast<arr_size_t>(bc->fastop[0]);

    JUMP();
}

label_BRCH: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();

    size_t jumpIdx = 0;
    if (bc->withCnt() == 0) {
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
            for (; j < bc->withCnt(); ++j) {
                auto caseData = currFrame->get<Object *>(wargs[j]);
                if (condData->equals(caseData)) {
                    jumpIdx = j; // jump to matched case
                    break;
                }
            }
        } else {
            auto condData = currFrame->get<slot_t>(nargs[0]);
            for (; j < bc->withCnt(); ++j) {
                auto caseData = currFrame->get<slot_t>(wargs[j]);
                if (condData == caseData) {
                    jumpIdx = j; // jump to matched case
                    break;
                }
            }
        }

        if (j == bc->withCnt()) {
            // fallthrough to else case if no match
            jumpIdx = bc->withCnt();
        }
    }

    currFrame->set(bc->result, fromSlot<Int>(jumpIdx));
    pc += bc->opsize + jumpIdx;

    JUMP();
}

label_JOIN: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();
    int32_t brIndex        = currFrame->get<int32_t>(nargs[0]);
    ASSERT(
        brIndex >= 0 && static_cast<size_t>(brIndex) < bc->withCnt(),
        "JOIN opcode choosen index out of range in FastVM.");
    slot_t result = currFrame->get<slot_t>(wargs[static_cast<size_t>(brIndex)]);
    currFrame->set(bc->result, result);

    NEXT();
}

label_FILL: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();

    TypeCode targetType = currFrame->typeAt(nargs[0]);
    ASSERT(isGCTraced(targetType), "FILL target type is not GC-traced in FastVM.");

    Object *target = currFrame->get<Object *>(nargs[0])->clone(mm::autoSpace());
    ASSERT(target != nullptr, "FILL target data is null.");

    switch (targetType) {
    case TypeCode::Tuple: {
        const auto &type = currFrame->typePtrAt<TupleType>(bc->result);
        auto t           = static_cast<Tuple *>(target);
        const auto &refs = t->layout().refs();
        ASSERT(
            refs.size() == bc->withCnt(),
            std::format(
                "Tuple layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
                bc->withCnt(),
                refs.size()));
        for (size_t j = 0; j < bc->withCnt(); ++j) {
            t->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
        }
        t->updateLayout(&type->layout());
    } break;

    case TypeCode::Array: {
        const auto &type = currFrame->typePtrAt<ArrayType>(bc->result);
        auto a           = static_cast<Array *>(target);
        const auto &refs = a->layout().refs();
        ASSERT(
            refs.size() == bc->withCnt(),
            std::format(
                "Array layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
                bc->withCnt(),
                refs.size()));
        for (size_t j = 0; j < bc->withCnt(); ++j) {
            a->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
        }
        a->updateLayout(&type->layout());
    } break;

    case TypeCode::Struct: {
        const auto &type = currFrame->typePtrAt<StructType>(bc->result);
        auto s           = static_cast<Struct *>(target);
        const auto &refs = s->layout().refs();
        ASSERT(
            refs.size() == bc->withCnt(),
            std::format(
                "Struct layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
                bc->withCnt(),
                refs.size()));
        for (size_t j = 0; j < bc->withCnt(); ++j) {
            s->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
        }
        s->updateLayout(&type->layout());
    } break;

    case TypeCode::Function: {
        auto f             = static_cast<Function *>(target);
        Tuple *closureData = f->tuple();
        for (size_t j = 0; j < bc->withCnt(); ++j) {
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

    currFrame->set(bc->result, target);

    NEXT();
}

label_CALL: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();
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

    currFrame->set(bc->result, result);

    NEXT();
}

label_FUNC: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    // 保存当前程序计数器和栈帧
    push(pc, currFrame);

    // 创建新的栈帧并设置参数
    Frame *funcFrame       = framePool_.acquire(bc->extra()->graph);
    size_t argsCnt         = bc->normCnt();
    const data_idx_t *args = bc->operands();
    for (size_t i = 0; i < argsCnt; ++i) {
        funcFrame->set(i + 1, currFrame->get<slot_t>(args[i]));
    }

    // 切换到目标图的字节码位置和栈帧
    pc        = bc->fastop[1];
    currFrame = funcFrame;

    JUMP();
}

label_TAIL: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

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
    currFrame              = framePool_._acquire(bc->extra()->graph);
    size_t argsCnt         = bc->normCnt();
    const data_idx_t *args = bc->operands();
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
    pc = bc->fastop[1];

    JUMP();
}

label_OPER: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();
    auto func              = bc->extra()->func;
    EXEC_WHEN_DEBUG(l.in("FastVM").debug(
        "Executing operator {}.",
        context_->execMgr().getNameOfAnOperator(func)));
    func(bc->result, nargs, wargs, *currFrame, *context_);

    NEXT();
}

label_SCHD: {
    EXEC_WHEN_DEBUG(
        l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, pc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();
    auto mark              = bc->extra()->mark;
    evalMarkedOperator(mark, bc->result, nargs, wargs, *currFrame);

    NEXT();
}

    DEF_BIN_OP_LABEL(IADD, Int, +);
    DEF_BIN_OP_LABEL(LADD, Long, +);
    DEF_BIN_OP_LABEL(FADD, Float, +);
    DEF_BIN_OP_LABEL(DADD, Double, +);

    DEF_BIN_OP_LABEL(ISUB, Int, -);
    DEF_BIN_OP_LABEL(LSUB, Long, -);
    DEF_BIN_OP_LABEL(FSUB, Float, -);
    DEF_BIN_OP_LABEL(DSUB, Double, -);

    DEF_BIN_OP_LABEL(IMUL, Int, *);
    DEF_BIN_OP_LABEL(LMUL, Long, *);
    DEF_BIN_OP_LABEL(FMUL, Float, *);
    DEF_BIN_OP_LABEL(DMUL, Double, *);

    DEF_BIN_DIV_LABEL(IDIV, Int, 0);
    DEF_BIN_DIV_LABEL(LDIV, Long, 0);
    DEF_BIN_DIV_LABEL(FDIV, Float, 0.0f);
    DEF_BIN_DIV_LABEL(DDIV, Double, 0.0);

    DEF_BIN_OP_LABEL(ILT, Int, <);
    DEF_BIN_OP_LABEL(LLT, Long, <);
    DEF_BIN_OP_LABEL(FLT, Float, <);
    DEF_BIN_OP_LABEL(DLT, Double, <);

    DEF_BIN_OP_LABEL(IGT, Int, >);
    DEF_BIN_OP_LABEL(LGT, Long, >);
    DEF_BIN_OP_LABEL(FGT, Float, >);
    DEF_BIN_OP_LABEL(DGT, Double, >);

    DEF_BIN_OP_LABEL(IEQ, Int, ==);
    DEF_BIN_OP_LABEL(LEQ, Int, ==);
    DEF_BIN_OP_LABEL(FEQ, Float, ==);
    DEF_BIN_OP_LABEL(DEQ, Double, ==);

    DEF_BIN_OP_LABEL(INE, Int, !=);
    DEF_BIN_OP_LABEL(LNE, Long, !=);
    DEF_BIN_OP_LABEL(FNE, Float, !=);
    DEF_BIN_OP_LABEL(DNE, Double, !=);

    DEF_BIN_OP_LABEL(ILE, Int, <=);
    DEF_BIN_OP_LABEL(LLE, Long, <=);
    DEF_BIN_OP_LABEL(FLE, Float, <=);
    DEF_BIN_OP_LABEL(DLE, Double, <=);

    DEF_BIN_OP_LABEL(IGE, Int, >=);
    DEF_BIN_OP_LABEL(LGE, Long, >=);
    DEF_BIN_OP_LABEL(FGE, Float, >=);
    DEF_BIN_OP_LABEL(DGE, Double, >=);
}

#endif // ENABLE_COMPUTED_GOTO