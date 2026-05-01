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
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "fastvm.h"

#include "camel/core/error/runtime.h"
#include "camel/core/global_config.h"
#include "camel/execute/graph_runtime_support.h"
#include "runtime_support.h"

using namespace camel::core::error;
#if ENABLE_FASTVM_JIT
using namespace camel::jit;
#endif
#if !ENABLE_FASTVM_COMPUTED_GOTO

#include "camel/utils/opperf.h"

#ifndef NDEBUG
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"
#endif
#include <unordered_set>

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
            throwRuntimeFault(RuntimeDiag::DivisionByZero);                                        \
        }                                                                                          \
        TYPE res = lhs / rhs;                                                                      \
        currFrame->set(bc.result, res);                                                            \
    } break;

FastVMSchedPass::CallResult FastVMSchedPass::callBorrowed(size_t pc, Frame *rootFrame) {
    Frame *currFrame            = rootFrame;
    Frame *rootActiveFrame      = rootFrame;
    const size_t stackDepthBase = stackDepth_;

    try {
        while (true) {
            if (InternalGlobalConfig::IsInspectionMode() && context_) {
                if (auto sourceContext = context_->sourceContext()) {
                    sourceContext->setCurrentRuntimeOrigin(sourceContext->debugMap().pcOrigin(pc));
                }
            }
            const Bytecode &bc = bytecodes_[pc];
            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "FastVM",
                "Executing bytecode: {}",
                opCodeToString(bc, context_)));

#ifdef OPPERF_ENABLED
            std::string tag;
            if (bc.opcode == OpCode::OPER) {
                tag = context_->execMgr().getNameOfAnOperator(bc.extra()->func);
            } else if (bc.opcode == OpCode::FUNC) {
#if ENABLE_FASTVM_JIT
                if (bc.fastop[1] >= 0)
                    tag = getFuncExtraRuntimeGraph(&bc)->name();
                else
                    tag = jitFnToGraph_[reinterpret_cast<JitEntryFn>(getFuncExtraFn(&bc))]->name();
#else
                tag = bc.extra()->runtimeGraph->name();
#endif
            }
            opperf::ScopeTimer _timer(bc.opcode, tag);
#else
            opperf::ScopeTimer _timer(bc.opcode);
#endif

            switch (static_cast<OpCode>(bc.opcode)) {
            case OpCode::RETN: {
                slot_t result = bc.fastop[0] == 0 ? NullSlot : currFrame->get<slot_t>(bc.fastop[0]);

                if (currFrame == rootActiveFrame) {
                    return CallResult{result, currFrame};
                }

                framePool_.release(currFrame);

                auto [lastPC, lastFrame] = pop();
                pc                       = lastPC;
                currFrame                = lastFrame;

                Bytecode &lbc = bytecodes_[pc];
                currFrame->set(lbc.result, result);

                // Continue execution from the next instruction.
                pc += lbc.opsize;
                continue;
            } break;

            case OpCode::CAST: {
                Type *targetType  = bc.extra()->pType;
                data_idx_t srcIdx = bc.fastop[0];
                Type *srcType     = currFrame->typeAt<Type>(srcIdx);
                slot_t value      = currFrame->get<slot_t>(srcIdx);
                slot_t result     = targetType->castSlotFrom(value, srcType);
                if (bc.result != 0) {
                    currFrame->set(bc.result, result);
                }
            } break;

            case OpCode::COPY: {
                TypeCode srcCode = currFrame->codeAt(bc.fastop[0]);
                if (isGCTraced(srcCode)) {
                    Object *srcData  = currFrame->get<Object *>(bc.fastop[0]);
                    Type *srcTypePtr = currFrame->typeAt<Type>(bc.fastop[0]);
                    currFrame->set(bc.result, srcData->clone(mm::autoSpace(), srcTypePtr, false));
                } else {
                    slot_t srcData = currFrame->get<slot_t>(bc.fastop[0]);
                    currFrame->set(bc.result, srcData);
                }
            } break;

            case OpCode::ACCS: {
                TypeCode srcType = currFrame->codeAt(bc.fastop[0]);
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
                size_t jumpIdx;
                if (bc.withCnt() == 0) {
                    jumpIdx = currFrame->get<bool>(bc.operands()[0]) ? 0 : 1;
                } else {
                    jumpIdx = camel::passes::sched::fastvm::selectBranchArm(bc, currFrame);
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
                if (bc.result != 0 && bc.extra()->pType != Type::Void()) {
                    currFrame->set(bc.result, result);
                }
            } break;

            case OpCode::FILL: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();

                Type *srcType = bc.extra()->pType;
                auto *fillBody =
                    *reinterpret_cast<const camel::runtime::GCFillBody *const *>(bc.extra2());
                ASSERT(isGCTraced(srcType->code()), "FILL target type is not GC-traced in FastVM.");
                Object *sourceObj = currFrame->get<Object *>(nargs[0]);
                ASSERT(sourceObj != nullptr, "FILL source object is null in FastVM.");
                Object *srcObj = sourceObj->clone(mm::autoSpace(), srcType, false);

                ASSERT(srcObj != nullptr, "FILL target data is null.");
                std::vector<slot_t> fillValues;
                fillValues.reserve(bc.withCnt());
                for (size_t j = 0; j < bc.withCnt(); ++j) {
                    fillValues.push_back(currFrame->get<slot_t>(wargs[j]));
                }
                camel::execute::writeRuntimeFillSlots(srcObj, srcType, fillBody, fillValues);

                currFrame->set(bc.result, srcObj);
            } break;

            case OpCode::CALL: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();
                auto function          = currFrame->get<Function *>(wargs[0]);
                ASSERT(function != nullptr, "FastVM CALL resolved a null Function callee.");
                auto *runtimeTarget = function->graph();
                ASSERT(
                    runtimeTarget != nullptr,
                    "FastVM indirect CALL requires a materialized runtime graph target.");
                const uint32_t callCount = noteIndirectCall(runtimeTarget);

                Frame *funcFrame = framePool_.acquire(runtimeTarget);
                seedIndirectCallFrame(currFrame, funcFrame, function, nargs, wargs);

                _timer.pause();
                const auto &result = jitEnabled() ? invokeCallOrJit(
                                                        graphEntryPc(runtimeTarget),
                                                        runtimeTarget,
                                                        funcFrame,
                                                        currentJitCtx_,
                                                        callCount)
                                                  : call(graphEntryPc(runtimeTarget), funcFrame);
                _timer.resume();

                currFrame->set(bc.result, result);
            } break;

            case OpCode::FUNC: {
#if ENABLE_FASTVM_JIT
                if (!jitEnabled()) {
                    push(pc, currFrame);
                    auto *runtimeTarget = getFuncExtraRuntimeGraph(&bc);
                    ASSERT(
                        runtimeTarget != nullptr,
                        "FastVM direct FUNC target must have a materialized runtime graph.");
                    Frame *funcFrame       = acquireFrameForCall(runtimeTarget);
                    size_t argsCnt         = bc.normCnt();
                    const data_idx_t *args = bc.operands();
                    if (!trySeedSingleArgDirectCallFrameAtPc(
                            currFrame,
                            funcFrame,
                            pc,
                            args,
                            argsCnt)) {
                        seedDirectCallFrameWithPorts(
                            currFrame,
                            funcFrame,
                            directCallPortSlotsAtPc(pc, runtimeTarget),
                            args,
                            argsCnt);
                    }
                    pc        = static_cast<size_t>(bc.fastop[1]);
                    currFrame = funcFrame;
                    continue;
                }
                const Bytecode *callBc = materializeCallTarget(pc, const_cast<Bytecode *>(&bc));
                auto *runtimeTarget    = runtimeCallTarget(pc);
                ASSERT(
                    runtimeTarget != nullptr,
                    "FastVM direct FUNC target must have a materialized runtime graph.");
                size_t argsCnt         = callBc->normCnt();
                const data_idx_t *args = callBc->operands();
                if (callBc->fastop[1] < 0) {
                    JitEntryFn fn    = reinterpret_cast<JitEntryFn>(getFuncExtraFn(callBc));
                    Frame *funcFrame = [&]() {
                        Frame *frame = framePool_.acquire(runtimeTarget);
                        populateCallFrame(frame, runtimeTarget, args, argsCnt, [&](data_idx_t idx) {
                            return currFrame->get<slot_t>(idx);
                        });
                        return frame;
                    }();
                    slot_t result = invokeOwnedJitFrame(fn, funcFrame, currentJitCtx_);
                    if (bc.result != 0) {
                        currFrame->set(bc.result, result);
                    }
                    break;
                }
                push(pc, currFrame);
                Frame *funcFrame = [&]() {
                    Frame *frame = framePool_.acquire(runtimeTarget);
                    if (!trySeedSingleArgDirectCallFrameAtPc(currFrame, frame, pc, args, argsCnt)) {
                        seedDirectCallFrameWithPorts(
                            currFrame,
                            frame,
                            directCallPortSlotsAtPc(pc, runtimeTarget),
                            args,
                            argsCnt);
                    }
                    return frame;
                }();
                pc        = static_cast<size_t>(callBc->fastop[1]);
                currFrame = funcFrame;
                continue;
#else
                push(pc, currFrame);
                auto *runtimeTarget = getFuncExtraRuntimeGraph(&bc);
                ASSERT(
                    runtimeTarget != nullptr,
                    "FastVM direct FUNC target must have a materialized runtime graph.");
                Frame *funcFrame       = acquireFrameForCall(runtimeTarget);
                size_t argsCnt         = bc.normCnt();
                const data_idx_t *args = bc.operands();
                if (!trySeedSingleArgDirectCallFrameAtPc(currFrame, funcFrame, pc, args, argsCnt)) {
                    seedDirectCallFrameWithPorts(
                        currFrame,
                        funcFrame,
                        directCallPortSlotsAtPc(pc, runtimeTarget),
                        args,
                        argsCnt);
                }
                pc        = bc.fastop[1];
                currFrame = funcFrame;
                continue;
#endif
            } break;

            case OpCode::TAIL: {
#if ENABLE_FASTVM_JIT
                const Bytecode *tailBc = materializeCallTarget(pc, const_cast<Bytecode *>(&bc));
                auto *runtimeTailTarget =
                    jitEnabled() ? runtimeCallTarget(pc) : getFuncExtraRuntimeGraph(tailBc);
                auto *tailTargetGraph = runtimeTailTarget;
                ASSERT(
                    runtimeTailTarget != nullptr,
                    std::format(
                        "FastVM direct TAIL target '{}' must have a materialized runtime graph.",
                        tailTargetGraph->name()));
                if (tailBc->fastop[1] < 0) {
                    size_t argsCnt         = tailBc->normCnt();
                    const data_idx_t *args = tailBc->operands();
                    captureDirectCallArgs(currFrame, args, argsCnt, tailArgScratch_);
                    framePool_.release(currFrame);
                    Frame *newFrame = [&]() {
                        Frame *frame = framePool_._acquire(runtimeTailTarget);
                        if (!trySeedSingleArgDirectCallFrameFromSlotsAtPc(
                                frame,
                                pc,
                                tailArgScratch_)) {
                            seedDirectCallFrameFromSlotsWithPorts(
                                frame,
                                directCallPortSlotsAtPc(pc, runtimeTailTarget),
                                tailArgScratch_);
                        }
                        framePool_._resetTop();
                        return frame;
                    }();
                    if (currFrame == rootActiveFrame) {
                        rootActiveFrame = newFrame;
                    }
                    pc        = getFuncExtraTargetPc(tailBc);
                    currFrame = newFrame;
                    continue;
                }
                auto *targetGraph      = tailTargetGraph;
                size_t targetPc        = static_cast<size_t>(tailBc->fastop[1]);
                size_t argsCnt         = tailBc->normCnt();
                const data_idx_t *args = tailBc->operands();
                captureDirectCallArgs(currFrame, args, argsCnt, tailArgScratch_);
                framePool_.release(currFrame);
                currFrame = [&]() {
                    Frame *frame = framePool_._acquire(runtimeTailTarget);
                    if (!trySeedSingleArgDirectCallFrameFromSlotsAtPc(frame, pc, tailArgScratch_)) {
                        seedDirectCallFrameFromSlotsWithPorts(
                            frame,
                            directCallPortSlotsAtPc(pc, runtimeTailTarget),
                            tailArgScratch_);
                    }
                    framePool_._resetTop();
                    return frame;
                }();
                pc = targetPc;
                continue;
#else
                auto *runtimeTailTarget = runtimeCallTarget(pc);
                ASSERT(
                    runtimeTailTarget != nullptr,
                    "FastVM direct TAIL target must have a materialized runtime graph.");
                size_t argsCnt         = bc.normCnt();
                const data_idx_t *args = bc.operands();
                captureDirectCallArgs(currFrame, args, argsCnt, tailArgScratch_);
                framePool_.release(currFrame);
                currFrame = acquireFrameForTail(runtimeTailTarget);
                if (!trySeedSingleArgDirectCallFrameFromSlotsAtPc(currFrame, pc, tailArgScratch_)) {
                    seedDirectCallFrameFromSlotsWithPorts(
                        currFrame,
                        directCallPortSlotsAtPc(pc, runtimeTailTarget),
                        tailArgScratch_);
                }
                framePool_._resetTop();
                pc = bc.fastop[1];
                continue;
#endif
            } break;

            case OpCode::OPER: {
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
                currFrame->set(bc.result, result);
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
                throwRuntimeFault(RuntimeDiag::UnsupportedBytecode, to_string(bc.opcode));
            }
            }

            // move to the next bytecode
            pc += bc.opsize;
        }
    } catch (const RuntimeFault &fault) {
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

#endif // !ENABLE_FASTVM_COMPUTED_GOTO
