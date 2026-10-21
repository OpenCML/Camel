/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Feb. 06, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

/**
 * JIT slot[0] convention:
 * 1. Dynamic slot[0] always stores the current Frame* as reinterpret_cast<slot_t>.
 *    Interpreters write it before entering JIT, and trampoline code preserves it
 *    for JIT-to-JIT calls. User-visible arguments start at slot[1].
 * 2. trampolineFunc and TrampolineTail treat callerSlots[0] as the caller Frame*
 *    and read operands through callerFrame->get(...), which keeps negative
 *    static-slot operands working correctly.
 * 3. trampolineOper also reads the current Frame* from slots[0] and uses
 *    FrameArgsView instead of SlotArgsView, so it does not need a separate
 *    compile-graph carrier.
 */

#include "trampoline.h"

#include "camel/core/context/frame.h"
#include "camel/core/rtdata.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace camel::core::context;

#if ENABLE_FASTVM_JIT
#include "../../bytecode.h"
#include "../../fastvm.h"
#include "../../runtime_support.h"
#include "camel/core/context/frame.h"
#include "camel/core/effect_trace.h"
#include "camel/core/error/runtime.h"
#include "camel/core/global_config.h"
#include "camel/core/mm.h"
#include "camel/core/operator.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/utils/int_div.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"
#include "jit_debug_trace.h"

#include <iomanip>
#include <sstream>

using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;
using namespace camel::jit;
#endif

extern "C" {

#if ENABLE_FASTVM_JIT
namespace {

constexpr size_t kInlineTailArgCount = 8;

class TailArgStorage {
  public:
    explicit TailArgStorage(size_t count)
        : data_(count <= kInlineTailArgCount ? inline_.data() : heap_.data()) {
        if (count > kInlineTailArgCount) {
            heap_.resize(count);
            data_ = heap_.data();
        }
    }

    slot_t *data() { return data_; }

  private:
    std::array<slot_t, kInlineTailArgCount> inline_{};
    std::vector<slot_t> heap_;
    slot_t *data_ = nullptr;
};

inline void copyOperandsToBuffer(slot_t *dst, Frame *frame, const Bytecode &bc, size_t argsCnt) {
    for (size_t i = 0; i < argsCnt; ++i) {
        dst[i] = frame->get<slot_t>(bc.directCallSrcArgs()[i]);
    }
}

inline void
writeDirectCallArgsToFrame(Frame *frame, data_arr_t dstSlots, const slot_t *args, size_t argsCnt) {
    ASSERT(frame != nullptr, "JIT trampoline target frame is null.");
    ASSERT(argsCnt == dstSlots.size(), "JIT encoded direct-call layout is arity-mismatched.");
    for (size_t argIndex = 0; argIndex < argsCnt; ++argIndex) {
        frame->set(dstSlots[argIndex], args[argIndex]);
    }
}

} // namespace

// Comment normalized during runtime-graph refactor.
// Comment normalized during runtime-graph refactor.
extern "C" void jitDebugTraceBody(const void *ctx) {
    if (!ctx)
        return;
    const auto *c = static_cast<const JitDebugContext *>(ctx);
    std::ostringstream os;
    os << "trace pc=" << static_cast<uint32_t>(c->pc) << "\n";
    os << std::hex;
    const auto one = [&os](const char *name, uint64_t val) {
        os << "  " << std::setfill(' ') << std::setw(4) << std::left << name << " "
           << std::setfill('0') << std::right << "0x" << std::setw(16) << val;
    };
    one("rax", c->rax);
    os << "    ";
    one("r8", c->r8);
    os << "\n";
    one("rcx", c->rcx);
    os << "    ";
    one("r9", c->r9);
    os << "\n";
    one("rdx", c->rdx);
    os << "    ";
    one("r10", c->r10);
    os << "\n";
    one("rbx", c->rbx);
    os << "    ";
    one("r11", c->r11);
    os << "\n";
    one("rbp", c->rbp);
    os << "    ";
    one("r12", c->r12);
    os << "\n";
    one("rsi", c->rsi);
    os << "    ";
    one("r13", c->r13);
    os << "\n";
    one("rdi", c->rdi);
    os << "    ";
    one("r14", c->r14);
    os << "\n";
    one("r15", c->r15);
    os << "\n";
    const auto *slots    = reinterpret_cast<const slot_t *>(c->rdi);
    const auto *framePtr = reinterpret_cast<const Frame *>(slots[0]);
    if (framePtr) {
        framePtr->printSlotsTo(os);
    }
#ifdef NDEBUG
    std::cerr << os.str() << std::flush;
#else
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S("JIT.Debug", "{}", os.str()));
#endif
}

// Comment normalized during runtime-graph refactor.
// rdx/r8-r11
void jitDebugTraceWrapper(const void *ctx) {
    if (!ctx)
        return;
    static thread_local JitDebugContext buf;
    std::memcpy(&buf, ctx, sizeof(JitDebugContext));
    jitDebugTrace(&buf);
}

// Comment normalized during runtime-graph refactor.
extern "C" void jitDebugTraceNoOp(const void *ctx) { jitDebugTraceWrapper(ctx); }

#if defined(__GNUC__) || defined(__clang__)
// Comment normalized during runtime-graph refactor.
// Comment normalized during runtime-graph refactor.
__attribute__((naked)) void jitDebugTrace(const void *ctx) {
    __asm__ volatile("mov %%rdx, 0x60(%%rcx)\n"
                     "mov %%r8, 0x38(%%rcx)\n"
                     "mov %%r9, 0x30(%%rcx)\n"
                     "mov %%r10, 0x28(%%rcx)\n"
                     "mov %%r11, 0x20(%%rcx)\n"
                     "mov %%rbx, 0x58(%%rcx)\n"
                     "mov %%rbp, 0x50(%%rcx)\n"
                     "mov %%rsi, 0x48(%%rcx)\n"
                     "mov %%rdi, 0x40(%%rcx)\n"
                     "mov %%r12, 0x18(%%rcx)\n"
                     "mov %%r13, 0x10(%%rcx)\n"
                     "mov %%r14, 0x08(%%rcx)\n"
                     "mov %%r15, 0x00(%%rcx)\n"
                     "jmp jitDebugTraceBody\n" ::
                         : "memory");
}
#else
// Comment normalized during runtime-graph refactor.
void jitDebugTrace(const void *ctx) { jitDebugTraceBody(ctx); }
#endif

slot_t trampolineFunc(slot_t *callerSlots, void *ctx, size_t pc) {
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "JIT.Trampoline",
        "trampolineFunc ENTER callerSlots={} ctx={} pc={}",
        static_cast<void *>(callerSlots),
        ctx,
        pc));
    auto *jc     = static_cast<JitContext *>(ctx);
    auto *vm     = jc->vm;
    auto *base   = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc = base[pc];

    size_t targetPc     = getFuncExtraTargetPc(&bc);
    size_t argsCnt      = bc.directCallArgCnt();
    const auto dstSlots = bc.directCallDstSlots();
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "JIT.Trampoline",
        "trampolineFunc bc: targetPc={} argsCnt={}",
        targetPc,
        argsCnt));
    uint32_t count = 0;
    if (getFuncExtraFn(&bc) == nullptr)
        count = incFuncExtraCount(&bc);

    if (getFuncExtraFn(&bc) != nullptr) {
        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S("JIT.Trampoline", "trampolineFunc path: JIT->JIT"));
        auto *runtimeTarget = getFuncExtraRuntimeGraph(&bc);
        ASSERT(
            runtimeTarget != nullptr,
            std::format(
                "FastVM trampoline FUNC JIT target '{}' must have a materialized runtime graph.",
                runtimeTarget ? runtimeTarget->name() : "<null>"));
        JitEntryFn fn = reinterpret_cast<JitEntryFn>(getFuncExtraFn(&bc));
        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
            "JIT.Trampoline",
            "trampolineFunc JIT->JIT graph='{}' fn={}",
            runtimeTarget->name(),
            static_cast<void *>(reinterpret_cast<void *>(fn))));
        Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]); // slot[0] = Frame*
        Frame *newFrame    = vm->acquireFrameForCall(runtimeTarget);
        TailArgStorage args(argsCnt);
        copyOperandsToBuffer(args.data(), callerFrame, bc, argsCnt);
        writeDirectCallArgsToFrame(newFrame, dstSlots, args.data(), argsCnt);

        EXEC_WHEN_DEBUG({
            std::ostringstream os;
            os << "trampolineFunc callee frame <" << runtimeTarget->name() << "> (after copy):\n";
            newFrame->printSlotsTo(os);
            CAMEL_LOG_DEBUG_S("JIT.Trampoline", "{}", os.str());
        });
        EXEC_WHEN_DEBUG(
            CAMEL_LOG_DEBUG_S("JIT.Trampoline", "trampolineFunc about to call JIT entry"));
        slot_t result = vm->invokeOwnedJitFrame(fn, newFrame, ctx);
        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
            "JIT.Trampoline",
            "trampolineFunc JIT->JIT return result={}",
            result));
        return result;
    }

    auto *runtimeTarget = getFuncExtraRuntimeGraph(&bc);
    ASSERT(
        runtimeTarget != nullptr,
        std::format(
            "FastVM trampoline FUNC target '{}' must have a materialized runtime graph.",
            runtimeTarget ? runtimeTarget->name() : "<null>"));
    Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]);
    Frame *newFrame    = vm->acquireFrameForCall(runtimeTarget);
    TailArgStorage args(argsCnt);
    copyOperandsToBuffer(args.data(), callerFrame, bc, argsCnt);
    writeDirectCallArgsToFrame(newFrame, dstSlots, args.data(), argsCnt);
    (void)count;
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "JIT.Trampoline",
        "trampolineFunc target='{}' targetPc={}",
        runtimeTarget->name(),
        targetPc));
    return vm->invokeCallOrJit(targetPc, runtimeTarget, newFrame, ctx, count);
}

slot_t trampolineTail(slot_t *callerSlots, void *ctx, size_t pc) {
    auto *jc     = static_cast<JitContext *>(ctx);
    auto *vm     = jc->vm;
    auto *base   = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc = base[pc];

    size_t targetPc     = getFuncExtraTargetPc(&bc);
    size_t argsCnt      = bc.directCallArgCnt();
    const auto dstSlots = bc.directCallDstSlots();
    uint32_t count      = 0;
    if (getFuncExtraFn(&bc) == nullptr)
        count = incFuncExtraCount(&bc);

    if (getFuncExtraFn(&bc) != nullptr) {
        Frame *callerFrame  = reinterpret_cast<Frame *>(callerSlots[0]);
        auto *runtimeTarget = getFuncExtraRuntimeGraph(&bc);
        ASSERT(
            runtimeTarget != nullptr,
            std::format(
                "FastVM trampoline TAIL target '{}' must have a materialized runtime graph.",
                runtimeTarget ? runtimeTarget->name() : "<null>"));
        TailArgStorage args(argsCnt);
        copyOperandsToBuffer(args.data(), callerFrame, bc, argsCnt);
        vm->releaseFrameForTail(callerFrame);
        Frame *newFrame = vm->acquireFrameForTail(runtimeTarget);
        writeDirectCallArgsToFrame(newFrame, dstSlots, args.data(), argsCnt);
        JitEntryFn fn = reinterpret_cast<JitEntryFn>(getFuncExtraFn(&bc));
        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
            "JIT.Trampoline",
            "trampolineTail: JIT->JIT target='{}'",
            runtimeTarget->name()));
        return vm->invokeOwnedJitFrame(fn, newFrame, ctx);
    }

    auto *runtimeTarget = getFuncExtraRuntimeGraph(&bc);
    ASSERT(
        runtimeTarget != nullptr,
        std::format(
            "FastVM trampoline TAIL target '{}' must have a materialized runtime graph.",
            runtimeTarget->name()));
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "JIT.Trampoline",
        "trampolineTail: JIT->interpreter target='{}'",
        runtimeTarget->name()));

    Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]);
    TailArgStorage args(argsCnt);
    copyOperandsToBuffer(args.data(), callerFrame, bc, argsCnt);
    vm->releaseFrameForTail(callerFrame);
    Frame *newFrame = vm->acquireFrameForTail(runtimeTarget);
    writeDirectCallArgsToFrame(newFrame, dstSlots, args.data(), argsCnt);
    (void)count;
    return vm->call(targetPc, newFrame);
}

slot_t *prepareDirectJitCall(slot_t *callerSlots, void *ctx, const Bytecode *bc) {
    auto *jc = static_cast<JitContext *>(ctx);
    auto *vm = jc->vm;
    ASSERT(bc, "prepareDirectJitCall requires non-null bytecode");

    auto *runtimeTarget = getFuncExtraRuntimeGraph(bc);
    ASSERT(
        runtimeTarget != nullptr,
        std::format(
            "FastVM prepareDirectJitCall target '{}' must have a materialized runtime graph.",
            runtimeTarget->name()));
    size_t argsCnt     = bc->directCallArgCnt();
    Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]);
    Frame *newFrame    = vm->acquireFrameForCall(runtimeTarget);
    TailArgStorage args(argsCnt);
    copyOperandsToBuffer(args.data(), callerFrame, *bc, argsCnt);
    writeDirectCallArgsToFrame(newFrame, bc->directCallDstSlots(), args.data(), argsCnt);
    newFrame->slotBase()[0] = reinterpret_cast<slot_t>(newFrame);
    return newFrame->slotBase();
}

slot_t *prepareDirectJitTailCall(slot_t *callerSlots, void *ctx, const Bytecode *bc) {
    auto *jc = static_cast<JitContext *>(ctx);
    auto *vm = jc->vm;
    ASSERT(bc, "prepareDirectJitTailCall requires non-null bytecode");

    auto *runtimeTarget = getFuncExtraRuntimeGraph(bc);
    ASSERT(
        runtimeTarget != nullptr,
        std::format(
            "FastVM prepareDirectJitTailCall target '{}' must have a materialized runtime graph.",
            runtimeTarget->name()));
    size_t argsCnt     = bc->directCallArgCnt();
    Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]);
    TailArgStorage args(argsCnt);
    copyOperandsToBuffer(args.data(), callerFrame, *bc, argsCnt);

    vm->releaseFrameForTail(callerFrame);
    Frame *newFrame = vm->acquireFrameForTail(runtimeTarget);
    writeDirectCallArgsToFrame(newFrame, bc->directCallDstSlots(), args.data(), argsCnt);
    newFrame->slotBase()[0] = reinterpret_cast<slot_t>(newFrame);
    return newFrame->slotBase();
}

slot_t finishDirectJitCall(slot_t result, slot_t *calleeSlots, void *ctx) {
    auto *jc    = static_cast<JitContext *>(ctx);
    auto *vm    = jc->vm;
    auto *frame = reinterpret_cast<Frame *>(calleeSlots[0]);
    vm->releaseFrameForCall(frame);
    return result;
}

slot_t finishDirectJitCallFast(slot_t *calleeSlots, void *ctx, slot_t result) {
    auto *jc    = static_cast<JitContext *>(ctx);
    auto *vm    = jc->vm;
    auto *frame = reinterpret_cast<Frame *>(calleeSlots[0]);
    vm->releaseFrameForCall(frame);
    return result;
}

slot_t directSelfFuncInvoke(slot_t *callerSlots, void *ctx, const Bytecode *bc) {
    auto *jc           = static_cast<JitContext *>(ctx);
    auto *vm           = jc->vm;
    auto *runtimeGraph = getFuncExtraRuntimeGraph(bc);
    ASSERT(
        runtimeGraph != nullptr,
        std::format(
            "FastVM directSelfFuncInvoke target '{}' must have a materialized runtime graph.",
            runtimeGraph->name()));
    JitEntryFn fn      = reinterpret_cast<JitEntryFn>(getFuncExtraFn(bc));
    size_t argsCnt     = bc->directCallArgCnt();
    Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]);
    Frame *newFrame    = vm->acquireFrameForCall(runtimeGraph);
    TailArgStorage args(argsCnt);
    copyOperandsToBuffer(args.data(), callerFrame, *bc, argsCnt);
    writeDirectCallArgsToFrame(newFrame, bc->directCallDstSlots(), args.data(), argsCnt);
    slot_t *slots = newFrame->slotBase();
    slots[0]      = reinterpret_cast<slot_t>(newFrame);
    slot_t result;
    try {
        result = fn(slots, ctx);
    } catch (...) {
        vm->releaseFrameUnchecked(newFrame);
        throw;
    }
    vm->releaseFrameUnchecked(newFrame);
    return result;
}

slot_t trampolineOper(slot_t *slots, void *ctx, size_t pc) {
    auto *jc          = static_cast<JitContext *>(ctx);
    auto *vm          = jc->vm;
    auto *base        = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc      = base[pc];
    Frame *frame      = reinterpret_cast<Frame *>(slots[0]); // Convention: slot[0] stores Frame*.
    data_idx_t result = bc.result;

    slot_t ret;
    try {
        ret = vm->invokeNativeOperator(bc, *frame);
    } catch (const RuntimeFault &fault) {
        throw reportRuntimeFault(
            vm->context(),
            fault,
            makePcExecutionSite(
                vm->context().sourceContext(),
                frame,
                pc,
                0,
                "",
                ExecutionSiteKind::JitPc));
    }
    frame->set(result, ret);
    return ret;
}

slot_t trampolineCast(slot_t *slots, void *ctx, size_t pc) {
    auto *jc          = static_cast<JitContext *>(ctx);
    auto *base        = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc      = base[pc];
    Frame *frame      = reinterpret_cast<Frame *>(slots[0]); // Convention: slot[0] stores Frame*.
    data_idx_t srcIdx = bc.fastop[0];
    type::Type *targetType = bc.extra()->pType;
    data_idx_t resultSlot  = bc.result;

    type::Type *srcType = frame->typeAt<type::Type>(srcIdx);
    slot_t value        = frame->get<slot_t>(srcIdx);
    slot_t result       = targetType->castSlotFrom(value, srcType);
    frame->set(resultSlot, result);
    return result;
}

slot_t trampolineBytecode(slot_t *slots, void *ctx, size_t pc) {
    auto *jc     = static_cast<JitContext *>(ctx);
    auto *vm     = jc->vm;
    auto *base   = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc = base[pc];
    Frame *frame = reinterpret_cast<Frame *>(slots[0]); // Convention: slot[0] stores Frame*.

    switch (bc.opcode) {
    case OpCode::COPY: {
        const data_idx_t srcIdx = bc.fastop[0];
        TypeCode srcCode        = frame->codeAt(srcIdx);
        slot_t result;
        if (isGCTraced(srcCode)) {
            Object *srcData  = frame->get<Object *>(srcIdx);
            Type *srcTypePtr = frame->typeAt<Type>(srcIdx);
            result           = reinterpret_cast<slot_t>(
                srcData->clone(camel::core::mm::autoSpace(), srcTypePtr, false));
        } else {
            result = frame->get<slot_t>(srcIdx);
        }
        if (bc.result != 0) {
            frame->set(bc.result, result);
        }
        return result;
    }
    case OpCode::ACCS: {
        const data_idx_t srcIdx   = bc.fastop[0];
        const data_idx_t indexIdx = bc.fastop[1];
        TypeCode srcType          = frame->codeAt(srcIdx);
        slot_t result             = NullSlot;
        if (srcType == TypeCode::Tuple) {
            Tuple *t = frame->get<Tuple *>(srcIdx);
            ASSERT(
                static_cast<size_t>(indexIdx) < t->size(),
                "Tuple access index out of range in JIT trampoline.");
            result = t->get<slot_t>(static_cast<size_t>(indexIdx));
        } else if (srcType == TypeCode::Struct) {
            Struct *s = frame->get<Struct *>(srcIdx);
            ASSERT(
                static_cast<size_t>(indexIdx) < s->size(),
                "Struct access index out of range in JIT trampoline.");
            result = s->get<slot_t>(static_cast<size_t>(indexIdx));
        } else if (srcType == TypeCode::Array) {
            Array *a = frame->get<Array *>(srcIdx);
            ASSERT(
                static_cast<size_t>(indexIdx) < a->size(),
                "Array access index out of range in JIT trampoline.");
            result = a->get<slot_t>(static_cast<size_t>(indexIdx));
        } else {
            ASSERT(false, "Unsupported source type for ACCS in JIT trampoline.");
        }
        if (bc.result != 0) {
            frame->set(bc.result, result);
        }
        return result;
    }
    case OpCode::FILL: {
        const data_arr_t nargs = bc.nargs();
        const data_arr_t wargs = bc.wargs();
        ASSERT(!nargs.empty(), "FILL requires one norm input as destination template.");
        const data_idx_t srcIdx = nargs[0];
        Type *srcType           = bc.extra()->pType;
        auto *fillBody = *reinterpret_cast<const camel::runtime::GCFillBody *const *>(bc.extra2());
        ASSERT(isGCTraced(srcType->code()), "FILL target type is not GC-traced in JIT trampoline.");
        Object *sourceObj = frame->get<Object *>(srcIdx);
        ASSERT(sourceObj != nullptr, "FILL source object is null in JIT trampoline.");
        Object *srcObj = sourceObj->clone(camel::core::mm::autoSpace(), srcType, false);
        ASSERT(srcObj != nullptr, "FILL target data is null in JIT trampoline.");
        std::vector<slot_t> fillValues;
        fillValues.reserve(wargs.size());
        for (size_t j = 0; j < wargs.size(); ++j) {
            fillValues.push_back(frame->get<slot_t>(wargs[j]));
        }
        camel::execute::writeRuntimeFillSlots(srcObj, srcType, fillBody, fillValues);

        slot_t result = reinterpret_cast<slot_t>(srcObj);
        if (bc.result != 0) {
            frame->set(bc.result, result);
        }
        return result;
    }
    case OpCode::CALL: {
        const data_arr_t nargs = bc.nargs();
        const data_arr_t wargs = bc.wargs();
        ASSERT(!wargs.empty(), "CALL requires with-arg[0] as Function.");
        auto *function = frame->get<Function *>(wargs[0]);
        ASSERT(function != nullptr, "JIT CALL resolved a null Function callee.");
        auto *runtimeTarget = vm->graphOfFunction(function);
        ASSERT(
            runtimeTarget != nullptr,
            "FastVM JIT indirect CALL requires a materialized runtime graph target.");
        Frame *funcFrame = vm->acquireFrameForCall(runtimeTarget);
        vm->populateIndirectCallFrame(frame, funcFrame, function, nargs, wargs);

        // Indirect CALL still crosses a runtime Function carrier and closure
        // object. Keep this path on the interpreter entry for now until the
        // JIT indirect-call path is proven against recursive closure-heavy
        // workloads such as perf::timeit.
        slot_t result = vm->call(vm->graphEntryPc(runtimeTarget), funcFrame);
        if (bc.result != 0) {
            frame->set(bc.result, result);
        }
        return result;
    }
    default:
        ASSERT(false, "Unsupported opcode in trampolineBytecode.");
        return NullSlot;
    }
}

// ---------------------------------------------------------------------------------------------
// Helpers of framed code (trampoline.h). Each does for one bytecode what the interpreters do
// (gotovm.cpp / casevm.cpp), on the pool frame of the activation.

extern "C++" {
namespace {

// An exception must not leave a helper: the frame above it is compiled code, which has no
// unwind information. It is kept for the driver, and the compiled code, which checks faultKind
// after every helper call, leaves through its fault exit.
template <typename Body> inline slot_t runJitHelper(void *ctx, Body &&body) noexcept {
    try {
        return body();
    } catch (...) {
        auto *jc      = static_cast<JitContext *>(ctx);
        jc->pending   = std::current_exception();
        jc->faultKind = static_cast<uint32_t>(x64::JitFaultKind::PendingException);
        return NullSlot;
    }
}

inline void syncHelperOrigin(FastVMSchedPass *vm, size_t pc) {
    if (InternalGlobalConfig::IsInspectionMode()) [[unlikely]] {
        if (auto sourceContext = vm->context().sourceContext()) {
            sourceContext->setCurrentRuntimeOrigin(sourceContext->debugMap().pcOrigin(pc));
        }
    }
}

} // namespace
} // extern "C++"

slot_t jitHelperOper(slot_t *slots, void *ctx, size_t pc) {
    return runJitHelper(ctx, [&]() -> slot_t {
        auto *jc     = static_cast<JitContext *>(ctx);
        auto *vm     = jc->vm;
        Bytecode &bc = static_cast<Bytecode *>(const_cast<void *>(jc->base))[pc];
        Frame *frame = reinterpret_cast<Frame *>(slots[0]);
        syncHelperOrigin(vm, pc);
        if (camel::core::effect_trace::enabled()) [[unlikely]] {
            if (auto sourceContext = vm->context().sourceContext()) {
                camel::core::effect_trace::setSite(sourceContext->debugMap().pcOrigin(pc));
            }
        }
        slot_t result;
        try {
            result = vm->invokeNativeOperator(bc, *frame);
        } catch (const RuntimeFault &fault) {
            // The fault of the operator, at this bytecode: what the interpreter reports.
            throw reportRuntimeFault(
                vm->context(),
                fault,
                makePcExecutionSite(vm->context().sourceContext(), frame, pc));
        } catch (const camel::core::mm::MemoryExhausted &e) {
            // The exception is stored by runJitHelper and rethrown by the JIT; it carries the
            // place the program asked for the memory.
            throw e.during(
                std::string(),
                vm->context().sourceContext()
                    ? vm->context().sourceContext()->debugMap().pcOrigin(pc)
                    : camel::source::kInvalidOriginId);
        }
        frame->set(bc.result, result);
        return result;
    });
}

slot_t jitHelperBytecode(slot_t *slots, void *ctx, size_t pc) {
    return runJitHelper(ctx, [&]() -> slot_t {
        auto *jc     = static_cast<JitContext *>(ctx);
        auto *vm     = jc->vm;
        Bytecode &bc = static_cast<Bytecode *>(const_cast<void *>(jc->base))[pc];
        Frame *frame = reinterpret_cast<Frame *>(slots[0]);
        syncHelperOrigin(vm, pc);
        slot_t result = NullSlot;
        try {
            switch (bc.opcode) {
            case OpCode::CAST: {
                Type *targetType        = bc.extra()->pType;
                const data_idx_t srcIdx = bc.fastop[0];
                Type *srcType           = frame->typeAt<Type>(srcIdx);
                result = targetType->castSlotFrom(frame->get<slot_t>(srcIdx), srcType);
                break;
            }
            case OpCode::COPY: {
                const data_idx_t srcIdx = bc.fastop[0];
                if (isGCTraced(frame->codeAt(srcIdx))) {
                    Object *source = frame->get<Object *>(srcIdx);
                    result         = reinterpret_cast<slot_t>(source->clone(
                        camel::core::mm::autoSpace(),
                        frame->typeAt<Type>(srcIdx),
                        false));
                } else {
                    result = frame->get<slot_t>(srcIdx);
                }
                break;
            }
            case OpCode::ACCS: {
                const TypeCode srcCode = frame->codeAt(bc.fastop[0]);
                const auto index       = static_cast<size_t>(bc.fastop[1]);
                if (srcCode == TypeCode::Tuple) {
                    result = frame->get<Tuple *>(bc.fastop[0])->get<slot_t>(index);
                } else if (srcCode == TypeCode::Struct) {
                    result = frame->get<Struct *>(bc.fastop[0])->get<slot_t>(index);
                } else {
                    ASSERT(false, "ACCS opcode unsupported source type in JIT helper.");
                }
                break;
            }
            case OpCode::FILL: {
                const data_arr_t nargs = bc.nargs();
                const data_arr_t wargs = bc.wargs();
                Type *type             = bc.extra()->pType;
                auto *fillBody =
                    *reinterpret_cast<const camel::runtime::GCFillBody *const *>(bc.extra2());
                Object *source = frame->get<Object *>(nargs[0]);
                ASSERT(source != nullptr, "FILL source object is null in JIT helper.");
                Object *target = source->clone(camel::core::mm::autoSpace(), type, false);
                std::vector<slot_t> fillValues;
                fillValues.reserve(wargs.size());
                for (const data_idx_t arg : wargs) {
                    fillValues.push_back(frame->get<slot_t>(arg));
                }
                camel::execute::writeRuntimeFillSlots(target, type, fillBody, fillValues);
                result = reinterpret_cast<slot_t>(target);
                break;
            }
            case OpCode::JOIN: {
                // Reached for a JOIN of more than two arms only; a void JOIN yields null.
                if (bc.extra()->pType != Type::Void()) {
                    const auto arm = static_cast<size_t>(frame->get<int32_t>(bc.nargs()[0]));
                    ASSERT(arm < bc.withCnt(), "JOIN arm index out of range in JIT helper.");
                    result = frame->get<slot_t>(bc.wargs()[arm]);
                }
                break;
            }
            case OpCode::IMOD:
            case OpCode::LMOD: {
                // The remainder of the language: x % -1 is 0, and a zero divisor is a fault
                // (camel/utils/int_div.h, the same as the interpreters' IMOD / LMOD).
                if (bc.opcode == OpCode::IMOD) {
                    const Int32 rhs = frame->get<Int32>(bc.fastop[1]);
                    if (rhs == 0) {
                        throwRuntimeFault(RuntimeDiag::DivisionByZero);
                    }
                    result = toSlot(camel::utils::remainder(frame->get<Int32>(bc.fastop[0]), rhs));
                } else {
                    const Int64 rhs = frame->get<Int64>(bc.fastop[1]);
                    if (rhs == 0) {
                        throwRuntimeFault(RuntimeDiag::DivisionByZero);
                    }
                    result = toSlot(camel::utils::remainder(frame->get<Int64>(bc.fastop[0]), rhs));
                }
                break;
            }
            default:
                ASSERT(false, "Unsupported opcode in jitHelperBytecode.");
            }
        } catch (const RuntimeFault &fault) {
            throw reportRuntimeFault(
                vm->context(),
                fault,
                makePcExecutionSite(vm->context().sourceContext(), frame, pc));
        }
        if (bc.result != 0) {
            frame->set(bc.result, result);
        }
        return result;
    });
}

slot_t jitHelperBranchArm(slot_t *slots, void *ctx, size_t pc) {
    return runJitHelper(ctx, [&]() -> slot_t {
        auto *jc         = static_cast<JitContext *>(ctx);
        Bytecode &bc     = static_cast<Bytecode *>(const_cast<void *>(jc->base))[pc];
        Frame *frame     = reinterpret_cast<Frame *>(slots[0]);
        const size_t arm = camel::passes::sched::fastvm::selectBranchArm(bc, frame);
        frame->set(bc.result, fromSlot<Int32>(arm));
        return static_cast<slot_t>(arm);
    });
}

#else
// Comment normalized during runtime-graph refactor.
slot_t trampolineFunc(slot_t *, void *, size_t) { return {}; }
slot_t trampolineTail(slot_t *, void *, size_t) { return {}; }
slot_t trampolineOper(slot_t *, void *, size_t) { return {}; }
slot_t trampolineCast(slot_t *, void *, size_t) { return {}; }
slot_t trampolineBytecode(slot_t *, void *, size_t) { return {}; }
slot_t *prepareDirectJitCall(slot_t *, void *, const Bytecode *) { return nullptr; }
slot_t *prepareDirectJitTailCall(slot_t *, void *, const Bytecode *) { return nullptr; }
slot_t finishDirectJitCall(slot_t, slot_t *, void *) { return {}; }
slot_t jitHelperOper(slot_t *, void *, size_t) { return {}; }
slot_t jitHelperBytecode(slot_t *, void *, size_t) { return {}; }
slot_t jitHelperBranchArm(slot_t *, void *, size_t) { return {}; }
#endif

} // extern "C"
