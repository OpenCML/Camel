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
 * Updated: May. 02, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "fastvm.h"

#include "camel/core/error/runtime.h"
#include "camel/core/global_config.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "runtime_support.h"
#include <array>
#include <iostream>

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
#if ENABLE_FASTVM_JIT
using namespace camel::jit;
#endif

#if ENABLE_FASTVM_COMPUTED_GOTO

#if (defined(__x86_64__) || defined(_M_X64)) && defined(__clang__) && defined(_WIN32)
/* Clang Win64: wrapper in trampoline.cpp. Use a fixed-size per-thread stack so nested direct JIT
 * calls do not overwrite the caller's saved pc/bc, while keeping the hot path allocation-free. */
static constexpr size_t kJitSaveStackLimit = 8192;
static thread_local std::array<size_t, kJitSaveStackLimit> s_jit_pc_stack;
static thread_local std::array<const Bytecode *, kJitSaveStackLimit> s_jit_bc_stack;
static thread_local size_t s_jit_save_depth = 0;
#define JIT_SAVE_PC_BC()                                                                           \
    do {                                                                                           \
        ASSERT(s_jit_save_depth < kJitSaveStackLimit, "Nested JIT save stack overflow.");          \
        s_jit_pc_stack[s_jit_save_depth] = pc;                                                     \
        s_jit_bc_stack[s_jit_save_depth] = bc;                                                     \
        ++s_jit_save_depth;                                                                        \
    } while (0)
#define JIT_RESTORE_PC_BC()                                                                        \
    do {                                                                                           \
        ASSERT(s_jit_save_depth > 0, "Nested JIT save stack underflow.");                          \
        --s_jit_save_depth;                                                                        \
        pc = s_jit_pc_stack[s_jit_save_depth];                                                     \
        bc = s_jit_bc_stack[s_jit_save_depth];                                                     \
    } while (0)
#else
#define JIT_SAVE_PC_BC() ((void)0)
#define JIT_RESTORE_PC_BC() ((void)0)
#endif

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
#include <unordered_set>

#define SYNC_RUNTIME_ORIGIN()                                                                      \
    do {                                                                                           \
        if (InternalGlobalConfig::IsInspectionMode() && context_) {                                \
            if (auto _sourceContext = context_->sourceContext()) {                                 \
                _sourceContext->setCurrentRuntimeOrigin(_sourceContext->debugMap().pcOrigin(pc));  \
            }                                                                                      \
        }                                                                                          \
    } while (0)

#define NEXT()                                                                                     \
    do {                                                                                           \
        pc += bc->opsize;                                                                          \
        SYNC_RUNTIME_ORIGIN();                                                                     \
        bc = &base[pc];                                                                            \
        goto *dispatchTable[static_cast<size_t>(bc->opcode)];                                      \
    } while (0)

#define JUMP()                                                                                     \
    do {                                                                                           \
        SYNC_RUNTIME_ORIGIN();                                                                     \
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
            throwRuntimeFault(RuntimeDiag::DivisionByZero);                                        \
        }                                                                                          \
        TYPE res = lhs / rhs;                                                                      \
        currFrame->set(bc->result, res);                                                           \
        NEXT();                                                                                    \
    }

using namespace std;

static void writeComputedGotoFillSlots(
    Frame *frame, const Bytecode *bc, Object *target, Type *targetType,
    const camel::runtime::GCFillBody *fillBody) {
    const data_arr_t wargs = bc->wargs();
    std::vector<slot_t> fillValues;
    fillValues.reserve(bc->withCnt());
    for (size_t j = 0; j < bc->withCnt(); ++j) {
        fillValues.push_back(frame->get<slot_t>(wargs[j]));
    }
    camel::execute::writeRuntimeFillSlots(target, targetType, fillBody, fillValues);
}

FastVMSchedPass::CallResult FastVMSchedPass::callBorrowed(size_t pc, Frame *rootFrame) {
    Frame *currFrame            = rootFrame;
    Frame *rootActiveFrame      = rootFrame;
    const Bytecode *base        = bytecodes_.data();
    const Bytecode *bc          = nullptr;
    const size_t stackDepthBase = stackDepth_;
#if (defined(__x86_64__) || defined(_M_X64)) && defined(__clang__) && defined(_WIN32)
    const size_t jitSaveBase = s_jit_save_depth;
#endif

    try {
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

            // Inline operators
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

        // Initial dispatch
        JUMP();

    label_RETN: {
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
        opperf::ScopeTimer _timer(bc->opcode);

        slot_t result = bc->fastop[0] == 0 ? NullSlot : currFrame->get<slot_t>(bc->fastop[0]);
        if (currFrame == rootActiveFrame) {
            return CallResult{result, currFrame};
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
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
        opperf::ScopeTimer _timer(bc->opcode);

        Type *targetType  = bc->extra()->pType;
        data_idx_t srcIdx = bc->fastop[0];
        Type *srcType     = currFrame->typeAt<Type>(srcIdx);
        slot_t value      = currFrame->get<slot_t>(srcIdx);
        slot_t result     = targetType->castSlotFrom(value, srcType);
        if (bc->result != 0) {
            currFrame->set(bc->result, result);
        }

        NEXT();
    }

    label_COPY: {
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
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
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
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
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
        opperf::ScopeTimer _timer(bc->opcode);

        pc = static_cast<arr_size_t>(bc->fastop[0]);

        JUMP();
    }

    label_BRCH: {
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
        opperf::ScopeTimer _timer(bc->opcode);

        size_t jumpIdx;
        if (bc->withCnt() == 0) {
            jumpIdx = currFrame->get<bool>(bc->operands()[0]) ? 0 : 1;
        } else {
            jumpIdx = camel::passes::sched::fastvm::selectBranchArm(*bc, currFrame);
        }

        currFrame->set(bc->result, fromSlot<Int32>(jumpIdx));
        pc += bc->opsize + jumpIdx;

        JUMP();
    }

    label_JOIN: {
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
        opperf::ScopeTimer _timer(bc->opcode);

        const data_arr_t nargs = bc->nargs();
        const data_arr_t wargs = bc->wargs();
        int32_t brIndex        = currFrame->get<int32_t>(nargs[0]);
        ASSERT(
            brIndex >= 0 && static_cast<size_t>(brIndex) < bc->withCnt(),
            "JOIN opcode choosen index out of range in FastVM.");
        if (bc->result != 0) {
            if (bc->extra()->pType == Type::Void()) {
                currFrame->set(bc->result, NullSlot);
                NEXT();
            }
            slot_t result = currFrame->get<slot_t>(wargs[static_cast<size_t>(brIndex)]);
            currFrame->set(bc->result, result);
        }
        NEXT();
    }

    label_FILL: {
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
        opperf::ScopeTimer _timer(bc->opcode);

        const data_arr_t nargs = bc->nargs();

        Type *srcType  = bc->extra()->pType;
        auto *fillBody = *reinterpret_cast<const camel::runtime::GCFillBody *const *>(bc->extra2());
        ASSERT(isGCTraced(srcType->code()), "FILL target type is not GC-traced in FastVM.");
        Object *sourceObj = currFrame->get<Object *>(nargs[0]);
        ASSERT(sourceObj != nullptr, "FILL source object is null in FastVM.");
        Object *srcObj = sourceObj->clone(mm::autoSpace(), srcType, false);

        ASSERT(srcObj != nullptr, "FILL target data is null.");
        writeComputedGotoFillSlots(currFrame, bc, srcObj, srcType, fillBody);

        currFrame->set(bc->result, srcObj);

        NEXT();
    }

    label_CALL: {
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
        opperf::ScopeTimer _timer(bc->opcode);

        const data_arr_t nargs = bc->nargs();
        const data_arr_t wargs = bc->wargs();
        auto function          = currFrame->get<Function *>(wargs[0]);
        ASSERT(function != nullptr, "GotoVM CALL resolved a null Function callee.");
        auto *runtimeTarget = function->graph();
        ASSERT(
            runtimeTarget != nullptr,
            "FastVM indirect CALL requires a materialized runtime graph target.");

        const uint32_t callCount = noteIndirectCall(runtimeTarget);
        Frame *funcFrame         = framePool_.acquire(runtimeTarget);
        populateIndirectCallFrame(currFrame, funcFrame, function, nargs, wargs);

        _timer.pause();
        const auto &result = jitEnabled() ? invokeCallOrJit(
                                                graphEntryPc(runtimeTarget),
                                                runtimeTarget,
                                                funcFrame,
                                                currentJitCtx_,
                                                callCount)
                                          : call(graphEntryPc(runtimeTarget), funcFrame);
        _timer.resume();

        currFrame->set(bc->result, result);

        NEXT();
    }

    label_FUNC: {
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
        opperf::ScopeTimer _timer(bc->opcode);
        const data_arr_t srcArgs  = bc->directCallSrcArgs();
        const data_arr_t dstSlots = bc->directCallDstSlots();

#if ENABLE_FASTVM_JIT
        if (!jitEnabled()) {
            push(pc, currFrame);
            auto *runtimeTarget = getFuncExtraRuntimeGraph(bc);
            ASSERT(
                runtimeTarget != nullptr,
                "FastVM direct FUNC target must have a materialized runtime graph.");
            Frame *funcFrame = acquireFrameForCall(runtimeTarget);
            populateDirectCallFrame(currFrame, funcFrame, srcArgs, dstSlots);
            pc        = getFuncExtraTargetPc(bc);
            currFrame = funcFrame;
            JUMP();
        }
        bc                            = materializeCallTarget(pc, const_cast<Bytecode *>(bc));
        const data_arr_t callSrcArgs  = bc->directCallSrcArgs();
        const data_arr_t callDstSlots = bc->directCallDstSlots();
        auto *runtimeTarget           = getFuncExtraRuntimeGraph(bc);
        ASSERT(
            runtimeTarget != nullptr,
            "FastVM direct FUNC target must have a materialized runtime graph.");
        if (getFuncExtraFn(bc) != nullptr) {
            JIT_SAVE_PC_BC(); /* save pc/bc before any call can clobber them (Build opt) */
            JitEntryFn fn    = reinterpret_cast<JitEntryFn>(getFuncExtraFn(bc));
            Frame *funcFrame = [&]() {
                Frame *frame = framePool_.acquire(runtimeTarget);
                populateDirectCallFrame(currFrame, frame, callSrcArgs, callDstSlots);
                return frame;
            }();
            slot_t result = invokeOwnedJitFrame(fn, funcFrame, currentJitCtx_);
            JIT_RESTORE_PC_BC();
            if (bc->result != 0) {
                currFrame->set(bc->result, result);
            }
            NEXT();
        }
        push(pc, currFrame);
        Frame *funcFrame = [&]() {
            Frame *frame = framePool_.acquire(runtimeTarget);
            populateDirectCallFrame(currFrame, frame, callSrcArgs, callDstSlots);
            return frame;
        }();
        pc        = getFuncExtraTargetPc(bc);
        currFrame = funcFrame;
        JUMP();
#else
        push(pc, currFrame);
        auto *runtimeTarget = getFuncExtraRuntimeGraph(bc);
        ASSERT(
            runtimeTarget != nullptr,
            "FastVM direct FUNC target must have a materialized runtime graph.");
        Frame *funcFrame = acquireFrameForCall(runtimeTarget);
        populateDirectCallFrame(currFrame, funcFrame, srcArgs, dstSlots);
        pc        = getFuncExtraTargetPc(bc);
        currFrame = funcFrame;
        JUMP();
#endif
    }

    label_TAIL: {
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
        opperf::ScopeTimer _timer(bc->opcode);
        const data_arr_t srcArgs  = bc->directCallSrcArgs();
        const data_arr_t dstSlots = bc->directCallDstSlots();

#if ENABLE_FASTVM_JIT
        bc                      = materializeCallTarget(pc, const_cast<Bytecode *>(bc));
        auto *runtimeTailTarget = getFuncExtraRuntimeGraph(bc);
        [[maybe_unused]] auto *tailTargetGraph = runtimeTailTarget;
        ASSERT(
            runtimeTailTarget != nullptr,
            std::format(
                "FastVM direct TAIL target '{}' must have a materialized runtime graph.",
                tailTargetGraph->name()));
        if (getFuncExtraFn(bc) != nullptr) {
            captureCallArgValues(currFrame, srcArgs, tailArgValuesScratch_);
            framePool_.release(currFrame);
            Frame *newFrame = [&]() {
                Frame *frame = framePool_._acquire(runtimeTailTarget);
                populateDirectCallFrameFromValues(frame, dstSlots, tailArgValuesScratch_);
                framePool_._resetTop();
                return frame;
            }();
            if (currFrame == rootActiveFrame) {
                rootActiveFrame = newFrame;
            }
            pc        = getFuncExtraTargetPc(bc);
            currFrame = newFrame;
            JUMP();
        }
        size_t targetPc = getFuncExtraTargetPc(bc);
        captureCallArgValues(currFrame, srcArgs, tailArgValuesScratch_);
        framePool_.release(currFrame);
        currFrame = [&]() {
            Frame *frame = framePool_._acquire(runtimeTailTarget);
            populateDirectCallFrameFromValues(frame, dstSlots, tailArgValuesScratch_);
            framePool_._resetTop();
            return frame;
        }();
        pc = targetPc;
        JUMP();
#else
        auto *lastGraph         = currFrame->graph();
        auto *targetGraph       = bc->extra()->runtimeGraph;
        auto *runtimeTailTarget = getFuncExtraRuntimeGraph(bc);
        ASSERT(
            runtimeTailTarget != nullptr,
            "FastVM direct TAIL target must have a materialized runtime graph.");
        captureCallArgValues(currFrame, srcArgs, tailArgValuesScratch_);
        framePool_.release(currFrame);
        currFrame = acquireFrameForTail(runtimeTailTarget);
        populateDirectCallFrameFromValues(currFrame, dstSlots, tailArgValuesScratch_);
        if (targetGraph != lastGraph) {
            framePool_._resetTop();
        }
        pc = getFuncExtraTargetPc(bc);
        JUMP();
#endif
    }

    label_OPER: {
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
        opperf::ScopeTimer _timer(bc->opcode);

        {
            const data_arr_t nargs = bc->nargs();
            const data_arr_t wargs = bc->wargs();
            auto func              = bc->extra()->func;
            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "FastVM",
                "Executing operator {}.",
                context_->execMgr().getNameOfAnOperator(func)));
            FrameArgsView withView(*currFrame, wargs);
            FrameArgsView normView(*currFrame, nargs);
            slot_t result;
            try {
                result = func(withView, normView, *context_);
            } catch (const RuntimeFault &fault) {
                throw reportRuntimeFault(
                    *context_,
                    fault,
                    makePcExecutionSite(context_->sourceContext(), currFrame, pc));
            }
            currFrame->set(bc->result, result);
        }

        NEXT();
    }

    label_SCHD: {
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("FastVM", "Executing bytecode: {}", opCodeToString(*bc, context_)));
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
    } catch (const RuntimeFault &fault) {
#if (defined(__x86_64__) || defined(_M_X64)) && defined(__clang__) && defined(_WIN32)
        s_jit_save_depth = jitSaveBase;
#endif
        std::unordered_set<Frame *> released;
        auto releaseFrame = [&](Frame *frame) {
            if (!frame || released.count(frame) != 0) {
                return;
            }
            if (!framePool_.isActive(frame)) {
                return;
            }
            framePool_.release(frame);
            released.insert(frame);
        };

        Frame *siteFrame = currFrame ? currFrame : rootActiveFrame;
        releaseFrame(currFrame);
        if (currFrame != rootActiveFrame) {
            releaseFrame(rootActiveFrame);
        }
        while (stackDepth_ > stackDepthBase) {
            releaseFrame(frameStack_[stackDepth_ - 1]);
            --stackDepth_;
        }

        throw reportRuntimeFault(
            *context_,
            fault,
            makePcExecutionSite(context_->sourceContext(), siteFrame, pc));
    } catch (Diagnostic &) {
#if (defined(__x86_64__) || defined(_M_X64)) && defined(__clang__) && defined(_WIN32)
        s_jit_save_depth = jitSaveBase;
#endif
        std::unordered_set<Frame *> released;
        auto releaseFrame = [&](Frame *frame) {
            if (!frame || released.count(frame) != 0) {
                return;
            }
            if (!framePool_.isActive(frame)) {
                return;
            }
            framePool_.release(frame);
            released.insert(frame);
        };

        releaseFrame(currFrame);
        if (currFrame != rootActiveFrame) {
            releaseFrame(rootActiveFrame);
        }
        while (stackDepth_ > stackDepthBase) {
            releaseFrame(frameStack_[stackDepth_ - 1]);
            --stackDepth_;
        }
        throw;
    }
}

#endif // ENABLE_FASTVM_COMPUTED_GOTO
