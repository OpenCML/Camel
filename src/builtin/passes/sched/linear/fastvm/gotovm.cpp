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
 * Updated: Feb. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "fastvm.h"

#if ENABLE_FASTVM_COMPUTED_GOTO

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

#include "opperf.h"

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
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
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
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    ASSERT(false, "CAST opcode not implemented in FastVM.");

    NEXT();
}

label_COPY: {
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    TypeCode srcCode = currFrame->codeAt(bc->fastop[0]);
    if (isGCTraced(srcCode)) {
        Object *srcData  = currFrame->get<Object *>(bc->fastop[0]);
        Type *srcTypePtr = currFrame->typeAt<Type>(bc->fastop[0]);
        currFrame->set(bc->result, srcData->clone(mm::autoSpace(), srcTypePtr, false));
    } else {
        slot_t srcData = currFrame->get<slot_t>(bc->fastop[0]);
        currFrame->set(bc->result, srcData);
    }

    NEXT();
}

label_ACCS: {
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    TypeCode srcType = currFrame->codeAt(bc->fastop[0]);
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
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    pc = static_cast<arr_size_t>(bc->fastop[0]);

    JUMP();
}

label_BRCH: {
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
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
        TypeCode condType = currFrame->codeAt(nargs[0]);

        if (isGCTraced(condType)) {
            Type *condTypePtr = currFrame->typeAt<Type>(nargs[0]);
            auto condData     = currFrame->get<Object *>(nargs[0]);
            for (; j < bc->withCnt(); ++j) {
                auto caseData = currFrame->get<Object *>(wargs[j]);
                if (condData->equals(caseData, condTypePtr, false)) {
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

    currFrame->set(bc->result, fromSlot<Int32>(jumpIdx));
    pc += bc->opsize + jumpIdx;

    JUMP();
}

label_JOIN: {
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
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
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();

    TypeCode srcCode = currFrame->codeAt(nargs[0]);
    Type *srcType    = currFrame->typeAt<Type>(nargs[0]);
    ASSERT(isGCTraced(srcCode), "FILL target type is not GC-traced in FastVM.");
    Object *srcObj = currFrame->get<Object *>(nargs[0])->clone(mm::autoSpace(), srcType, false);

    ASSERT(srcObj != nullptr, "FILL target data is null.");

    switch (srcCode) {
    case TypeCode::Tuple: {
        auto type = tt::as_ptr<TupleType>(srcType);
        auto tup  = tt::as_ptr<Tuple>(srcObj);
        ASSERT(
            type->refCount() == bc->withCnt(),
            std::format(
                "Tuple layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
                bc->withCnt(),
                type->refCount()));
        const size_t *refs = type->refs();
        for (size_t j = 0; j < bc->withCnt(); ++j) {
            tup->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
        }
    } break;

    case TypeCode::Array: {
        auto arr = tt::as_ptr<Array>(srcObj);
        // 对于数组，如果 elemType 是 Ref，所有元素都是 Ref，直接使用索引
        ASSERT(
            arr->size() >= bc->withCnt(),
            std::format(
                "Array size mismatch in FastVM. Expected at least {}, Actual: {}",
                bc->withCnt(),
                arr->size()));
        for (size_t j = 0; j < bc->withCnt(); ++j) {
            arr->set<slot_t>(j, currFrame->get<slot_t>(wargs[j]));
        }
    } break;

    case TypeCode::Struct: {
        auto type = tt::as_ptr<StructType>(srcType);
        auto str  = tt::as_ptr<Struct>(srcObj);
        ASSERT(
            type->refCount() == bc->withCnt(),
            std::format(
                "Struct layout refs size mismatch in FastVM. Expected: {}, Actual: {}",
                bc->withCnt(),
                type->refCount()));
        const size_t *refs = type->refs();
        for (size_t j = 0; j < bc->withCnt(); ++j) {
            str->set<slot_t>(refs[j], currFrame->get<slot_t>(wargs[j]));
        }
    } break;

    case TypeCode::Function: {
        auto func          = tt::as_ptr<Function>(srcObj);
        Tuple *closureData = func->tuple();
        for (size_t j = 0; j < bc->withCnt(); ++j) {
            closureData->set<slot_t>(j, currFrame->get<slot_t>(wargs[j]));
        }
    } break;

    default:
        ASSERT(
            false,
            std::format("Unsupported FILL target type {} in FastVM.", typeCodeToString(srcCode)));
    }

    currFrame->set(bc->result, srcObj);

    NEXT();
}

label_CALL: {
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
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
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

#if ENABLE_FASTVM_JIT
    // fastop[1] == 0 means this is a direct call to a JIT function
    if (bc->fastop[1] == 0) {
        Graph *targetGraph        = getFuncExtraGraph(bc);
        camel::jit::JitEntryFn fn = reinterpret_cast<camel::jit::JitEntryFn>(getFuncExtraFn(bc));
        Frame *funcFrame          = framePool_.acquire(targetGraph);
        size_t argsCnt            = bc->normCnt();
        const data_idx_t *args    = bc->operands();
        for (size_t i = 0; i < argsCnt; ++i) {
            funcFrame->set(i + 1, currFrame->get<slot_t>(args[i]));
        }
        EXEC_WHEN_DEBUG({
            std::string argsStr;
            for (size_t i = 0; i < argsCnt; ++i) {
                argsStr += std::format("{} ", currFrame->get<slot_t>(args[i]));
            }
            l.in("FastVM").debug(
                "Calling JIT function of graph <{}> with args: {}",
                targetGraph->name(),
                argsStr);
        });
        funcFrame->slotBase()[0] = reinterpret_cast<slot_t>(funcFrame);
        slot_t result            = fn(funcFrame->slotBase(), currentJitCtx_);
        l.in("FastVM").debug("JIT function at pc={} returned result={}.", pc, result);
        framePool_.release(funcFrame);
        currFrame->set(bc->result, result);
        NEXT();
    } else {
        // fastop[1] != 0 means this is a call to a JIT function that needs to be compiled
        Graph *targetGraph = getFuncExtraGraph(bc);
        size_t targetPc    = static_cast<size_t>(bc->fastop[1]);
        uint32_t count     = incFuncExtraCount(const_cast<Bytecode *>(bc));
        if (tierPolicy_.shouldJit(count)) {
            compileAndCacheGraph(targetGraph, targetPc);
            bc = &bytecodes_[pc];
            if (bc->fastop[1] == 0) {
                Graph *g = getFuncExtraGraph(bc);
                camel::jit::JitEntryFn fn =
                    reinterpret_cast<camel::jit::JitEntryFn>(getFuncExtraFn(bc));
                Frame *funcFrame       = framePool_.acquire(g);
                size_t argsCnt         = bc->normCnt();
                const data_idx_t *args = bc->operands();
                for (size_t i = 0; i < argsCnt; ++i) {
                    funcFrame->set(i + 1, currFrame->get<slot_t>(args[i]));
                }
                EXEC_WHEN_DEBUG({
                    std::string argsStr;
                    for (size_t i = 0; i < argsCnt; ++i) {
                        argsStr += std::format("{} ", currFrame->get<slot_t>(args[i]));
                    }
                    l.in("FastVM").debug(
                        "Calling JIT function of graph <{}> with args: {}",
                        targetGraph->name(),
                        argsStr);
                });
                funcFrame->slotBase()[0] = reinterpret_cast<slot_t>(funcFrame);
                slot_t result            = fn(funcFrame->slotBase(), currentJitCtx_);
                l.in("FastVM").debug("JIT function at pc={} returned result={}.", targetPc, result);
                _timer.resume();
                framePool_.release(funcFrame);
                currFrame->set(bc->result, result);
                NEXT();
            }
        }
        push(pc, currFrame);
        Frame *funcFrame       = framePool_.acquire(targetGraph);
        size_t argsCnt         = bc->normCnt();
        const data_idx_t *args = bc->operands();
        for (size_t i = 0; i < argsCnt; ++i) {
            funcFrame->set(i + 1, currFrame->get<slot_t>(args[i]));
        }
        pc        = targetPc;
        currFrame = funcFrame;
        JUMP();
    }
#else
    push(pc, currFrame);
    Frame *funcFrame       = framePool_.acquire(bc->extra()->graph);
    size_t argsCnt         = bc->normCnt();
    const data_idx_t *args = bc->operands();
    for (size_t i = 0; i < argsCnt; ++i) {
        funcFrame->set(i + 1, currFrame->get<slot_t>(args[i]));
    }
    pc        = bc->fastop[1];
    currFrame = funcFrame;
    JUMP();
#endif
}

label_TAIL: {
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

#if ENABLE_FASTVM_JIT
    FrameView lastFrame(currFrame);
    framePool_.release(currFrame);
    if (bc->fastop[1] == 0) {
        Graph *g                  = getFuncExtraGraph(bc);
        camel::jit::JitEntryFn fn = reinterpret_cast<camel::jit::JitEntryFn>(getFuncExtraFn(bc));
        Frame *newFrame           = framePool_._acquire(g);
        size_t argsCnt            = bc->normCnt();
        const data_idx_t *args    = bc->operands();
        for (size_t i = 0; i < argsCnt; ++i) {
            newFrame->set(i + 1, lastFrame.get<slot_t>(args[i]));
        }
        framePool_._resetTop();
        EXEC_WHEN_DEBUG({
            std::string argsStr;
            for (size_t i = 0; i < argsCnt; ++i) {
                argsStr += std::format("{} ", lastFrame.get<slot_t>(args[i]));
            }
            l.in("FastVM").debug(
                "Calling JIT function of graph <{}> with args: {}",
                g->name(),
                argsStr);
        });
        newFrame->slotBase()[0] = reinterpret_cast<slot_t>(newFrame);
        slot_t result           = fn(newFrame->slotBase(), currentJitCtx_);
        l.in("FastVM").debug("JIT function at pc={} returned result={}.", pc, result);
        return result;
    }
    Graph *targetGraph = getFuncExtraGraph(bc);
    size_t targetPc    = static_cast<size_t>(bc->fastop[1]);
    uint32_t count     = incFuncExtraCount(const_cast<Bytecode *>(bc));
    if (tierPolicy_.shouldJit(count)) {
        compileAndCacheGraph(targetGraph, targetPc);
        bc = &bytecodes_[pc];
        if (bc->fastop[1] == 0) {
            Graph *g = getFuncExtraGraph(bc);
            camel::jit::JitEntryFn fn =
                reinterpret_cast<camel::jit::JitEntryFn>(getFuncExtraFn(bc));
            Frame *newFrame        = framePool_._acquire(g);
            size_t argsCnt         = bc->normCnt();
            const data_idx_t *args = bc->operands();
            for (size_t i = 0; i < argsCnt; ++i) {
                newFrame->set(i + 1, lastFrame.get<slot_t>(args[i]));
            }
            framePool_._resetTop();
            EXEC_WHEN_DEBUG({
                std::string argsStr;
                for (size_t i = 0; i < argsCnt; ++i) {
                    argsStr += std::format("{} ", lastFrame.get<slot_t>(args[i]));
                }
                l.in("FastVM").debug(
                    "Calling JIT function of graph <{}> with args: {}",
                    g->name(),
                    argsStr);
            });
            newFrame->slotBase()[0] = reinterpret_cast<slot_t>(newFrame);
            slot_t result           = fn(newFrame->slotBase(), currentJitCtx_);
            l.in("FastVM").debug("JIT function at pc={} returned result={}.", pc, result);
            return result;
        }
    }
    currFrame              = framePool_._acquire(targetGraph);
    size_t argsCnt         = bc->normCnt();
    const data_idx_t *args = bc->operands();
    for (size_t i = 0; i < argsCnt; ++i) {
        currFrame->set(i + 1, lastFrame.get<slot_t>(args[i]));
    }
    framePool_._resetTop();
    pc = targetPc;
    JUMP();
#else
    FrameView lastFrame(currFrame);
    framePool_.release(currFrame);
    Graph *lastGraph       = currFrame->graph();
    Graph *targetGraph     = bc->extra()->graph;
    currFrame              = framePool_._acquire(targetGraph);
    size_t argsCnt         = bc->normCnt();
    const data_idx_t *args = bc->operands();
    for (size_t i = 0; i < argsCnt; ++i) {
        currFrame->set(i + 1, lastFrame.get<slot_t>(args[i]));
    }
    if (targetGraph != lastGraph) {
        framePool_._resetTop();
    }
    pc = bc->fastop[1];
    JUMP();
#endif
}

label_OPER: {
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    {
        const data_arr_t nargs = bc->nargs();
        const data_arr_t wargs = bc->wargs();
        auto func              = bc->extra()->func;
        EXEC_WHEN_DEBUG(l.in("FastVM").debug(
            "Executing operator {}.",
            context_->execMgr().getNameOfAnOperator(func)));
        FrameArgsView withView(*currFrame, wargs);
        FrameArgsView normView(*currFrame, nargs);
        slot_t result = func(withView, normView, *context_);
        currFrame->set(bc->result, result);
    }

    NEXT();
}

label_SCHD: {
    EXEC_WHEN_DEBUG(l.in("FastVM").debug("Executing bytecode: {}", opCodeToString(*bc, context_)));
    opperf::ScopeTimer _timer(bc->opcode);

    const data_arr_t nargs = bc->nargs();
    const data_arr_t wargs = bc->wargs();
    auto mark              = bc->extra()->mark;
    evalMarkedOperator(mark, bc->result, nargs, wargs, *currFrame);

    NEXT();
}

    DEF_BIN_OP_LABEL(IADD, Int32, +);
    DEF_BIN_OP_LABEL(LADD, Int64, +);
    DEF_BIN_OP_LABEL(FADD, Float32, +);
    DEF_BIN_OP_LABEL(DADD, Float64, +);

    DEF_BIN_OP_LABEL(ISUB, Int32, -);
    DEF_BIN_OP_LABEL(LSUB, Int64, -);
    DEF_BIN_OP_LABEL(FSUB, Float32, -);
    DEF_BIN_OP_LABEL(DSUB, Float64, -);

    DEF_BIN_OP_LABEL(IMUL, Int32, *);
    DEF_BIN_OP_LABEL(LMUL, Int64, *);
    DEF_BIN_OP_LABEL(FMUL, Float32, *);
    DEF_BIN_OP_LABEL(DMUL, Float64, *);

    DEF_BIN_DIV_LABEL(IDIV, Int32, 0);
    DEF_BIN_DIV_LABEL(LDIV, Int64, 0);
    DEF_BIN_DIV_LABEL(FDIV, Float32, 0.0f);
    DEF_BIN_DIV_LABEL(DDIV, Float64, 0.0);

    DEF_BIN_OP_LABEL(ILT, Int32, <);
    DEF_BIN_OP_LABEL(LLT, Int64, <);
    DEF_BIN_OP_LABEL(FLT, Float32, <);
    DEF_BIN_OP_LABEL(DLT, Float64, <);

    DEF_BIN_OP_LABEL(IGT, Int32, >);
    DEF_BIN_OP_LABEL(LGT, Int64, >);
    DEF_BIN_OP_LABEL(FGT, Float32, >);
    DEF_BIN_OP_LABEL(DGT, Float64, >);

    DEF_BIN_OP_LABEL(IEQ, Int32, ==);
    DEF_BIN_OP_LABEL(LEQ, Int32, ==);
    DEF_BIN_OP_LABEL(FEQ, Float32, ==);
    DEF_BIN_OP_LABEL(DEQ, Float64, ==);

    DEF_BIN_OP_LABEL(INE, Int32, !=);
    DEF_BIN_OP_LABEL(LNE, Int64, !=);
    DEF_BIN_OP_LABEL(FNE, Float32, !=);
    DEF_BIN_OP_LABEL(DNE, Float64, !=);

    DEF_BIN_OP_LABEL(ILE, Int32, <=);
    DEF_BIN_OP_LABEL(LLE, Int64, <=);
    DEF_BIN_OP_LABEL(FLE, Float32, <=);
    DEF_BIN_OP_LABEL(DLE, Float64, <=);

    DEF_BIN_OP_LABEL(IGE, Int32, >=);
    DEF_BIN_OP_LABEL(LGE, Int64, >=);
    DEF_BIN_OP_LABEL(FGE, Float32, >=);
    DEF_BIN_OP_LABEL(DGE, Float64, >=);
}

#endif // ENABLE_FASTVM_COMPUTED_GOTO
