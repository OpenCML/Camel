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
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 *
 */

/**
 * ---
 * JIT 调用约定与 slot[0] 规范：
 * 1. 动态区 slot[0] 保留，始终存放当前 Frame*（reinterpret_cast<slot_t>）。解释器/gotovm/casevm
 *    在调用 JIT 入口前写入；trampoline 在 JIT->JIT 调用前写入。slot[1..] 为正常数据槽。
 * 2. trampolineFunc/TrampolineTail：callerSlots[0] 即 caller Frame*，用 callerFrame->get(operand)
 *    取参，以支持 operand 为负（静态区索引）。
 * 3. trampolineOper：slots[0] 即当前 Frame*，用 FrameArgsView 取代 SlotArgsView，无需传 Graph。
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
#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/utils/log.h"
#include "jit_debug_trace.h"

#include <iomanip>
#include <sstream>

using namespace camel::core::error;
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
        dst[i] = frame->get<slot_t>(bc.operands()[i]);
    }
}

inline void
writeCallArgsToFrame(Frame *frame, const slot_t *args, size_t argsCnt, size_t dstOffset = 1) {
    for (size_t i = 0; i < argsCnt; ++i) {
        frame->set(dstOffset + i, args[i]);
    }
}

} // namespace

// 由 jitDebugTrace 存根 tail-call，C linkage 供 asm "jmp jitDebugTraceBody" 解析
// rdi 为当前 JIT 的 slot 基址；slot[0] 恒存 Frame*，据此打印当前帧（见上方 slot[0] 规范）
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
    EXEC_WHEN_DEBUG(GetDefaultLogger().in("JIT.Debug").debug("{}", os.str()));
#endif
}

// 将 ctx 拷入 thread_local 再调 stub，避免 stub 写回时覆盖 JIT 栈上的保存区，从而正确恢复
// rdx/r8-r11
void jitDebugTraceWrapper(const void *ctx) {
    if (!ctx)
        return;
    static thread_local JitDebugContext buf;
    std::memcpy(&buf, ctx, sizeof(JitDebugContext));
    jitDebugTrace(&buf);
}

// Release 用：走完整 trace 路径（wrapper→stub→body），Build 模式下 body 输出到 stderr
extern "C" void jitDebugTraceNoOp(const void *ctx) { jitDebugTraceWrapper(ctx); }

#if defined(__GNUC__) || defined(__clang__)
// Naked 存根：在 prologue 前将 rdx,r8..r11（caller-saved）和
// rbx,rbp,rdi,rsi,r12..r15（callee-saved）写入 ctx
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
// 非 GCC/Clang（如 MSVC）：无 naked，callee-saved 可能被 prologue 覆盖，仅打印 caller 已保存部分
void jitDebugTrace(const void *ctx) { jitDebugTraceBody(ctx); }
#endif

slot_t trampolineFunc(slot_t *callerSlots, void *ctx, size_t pc) {
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("JIT.Trampoline")
            .info(
                "trampolineFunc ENTER callerSlots={} ctx={} pc={}",
                static_cast<void *>(callerSlots),
                ctx,
                pc));
    auto *jc     = static_cast<JitContext *>(ctx);
    auto *vm     = jc->vm;
    auto *base   = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc = base[pc];

    const data_idx_t targetSlot = bc.fastop[1];
    size_t targetPc             = targetSlot < 0 ? 0 : static_cast<size_t>(targetSlot);
    size_t argsCnt              = bc.normCnt();
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("JIT.Trampoline")
            .info("trampolineFunc bc: targetPc={} argsCnt={}", targetPc, argsCnt));
    uint32_t count = 0;
    if (targetSlot >= 0)
        count = incFuncExtraCount(&bc);

    if (targetSlot < 0) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("JIT.Trampoline").info("trampolineFunc path: JIT->JIT"));
        GIR::Graph *g = getFuncExtraGraph(&bc);
        JitEntryFn fn = reinterpret_cast<JitEntryFn>(getFuncExtraFn(&bc));
        EXEC_WHEN_DEBUG(
            GetDefaultLogger()
                .in("JIT.Trampoline")
                .info(
                    "trampolineFunc JIT->JIT graph='{}' fn={}",
                    g->name(),
                    static_cast<void *>(reinterpret_cast<void *>(fn))));
        Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]); // slot[0] = Frame*
        Frame *newFrame    = vm->acquireFrameForCall(g);
        for (size_t i = 0; i < argsCnt; ++i) {
            newFrame->set(
                i + 1,
                callerFrame->get<slot_t>(bc.operands()[i])); // operand 可负（静态区）
        }

        EXEC_WHEN_DEBUG({
            std::ostringstream os;
            os << "trampolineFunc callee frame <" << g->name() << "> (after copy):\n";
            newFrame->printSlotsTo(os);
            GetDefaultLogger().in("JIT.Trampoline").info("{}", os.str());
        });
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("JIT.Trampoline").info("trampolineFunc about to call JIT entry"));
        slot_t result = vm->invokeOwnedJitFrame(fn, newFrame, ctx);
        EXEC_WHEN_DEBUG(
            GetDefaultLogger()
                .in("JIT.Trampoline")
                .info("trampolineFunc JIT->JIT return result={}", result));
        return result;
    }

    GIR::Graph *targetGraph = getFuncExtraGraph(&bc);
    Frame *callerFrame      = reinterpret_cast<Frame *>(callerSlots[0]);
    Frame *newFrame         = vm->acquireFrameForCall(targetGraph);
    for (size_t i = 0; i < argsCnt; ++i) {
        newFrame->set(i + 1, callerFrame->get<slot_t>(bc.operands()[i]));
    }
    (void)count;
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("JIT.Trampoline")
            .info("trampolineFunc target='{}' targetPc={}", targetGraph->name(), targetPc));
    return vm->invokeCallOrJit(targetPc, targetGraph, newFrame, ctx, count);
}

slot_t trampolineTail(slot_t *callerSlots, void *ctx, size_t pc) {
    auto *jc     = static_cast<JitContext *>(ctx);
    auto *vm     = jc->vm;
    auto *base   = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc = base[pc];

    const data_idx_t targetSlot = bc.fastop[1];
    size_t targetPc             = targetSlot < 0 ? 0 : static_cast<size_t>(targetSlot);
    size_t argsCnt              = bc.normCnt();
    uint32_t count              = 0;
    if (targetSlot >= 0)
        count = incFuncExtraCount(&bc);

    if (targetSlot < 0) {
        Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]);
        GIR::Graph *g      = getFuncExtraGraph(&bc);
        TailArgStorage args(argsCnt);
        copyOperandsToBuffer(args.data(), callerFrame, bc, argsCnt);
        vm->releaseFrameForTail(callerFrame);
        Frame *newFrame = vm->acquireFrameForTail(g);
        writeCallArgsToFrame(newFrame, args.data(), argsCnt);
        EXEC_WHEN_DEBUG(
            GetDefaultLogger()
                .in("JIT.Trampoline")
                .debug(
                    "trampolineTail: JIT->interpreter(target already compiled) target='{}'",
                    g->name()));
        // For non-self tail calls, prefer the interpreter entry even if the
        // callee has compiled code. This keeps mutual-tail recursion semantics
        // stable across graph boundaries while self-tail recursion still uses
        // the dedicated zero-overhead fast path in the x64 backend.
        return vm->call(vm->graphEntryPc(g), newFrame);
    }

    GIR::Graph *targetGraph = getFuncExtraGraph(&bc);
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("JIT.Trampoline")
            .debug("trampolineTail: JIT->interpreter target='{}'", targetGraph->name()));

    Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]);
    TailArgStorage args(argsCnt);
    copyOperandsToBuffer(args.data(), callerFrame, bc, argsCnt);
    vm->releaseFrameForTail(callerFrame);
    Frame *newFrame = vm->acquireFrameForTail(targetGraph);
    writeCallArgsToFrame(newFrame, args.data(), argsCnt);
    (void)count;
    return vm->call(targetPc, newFrame);
}

slot_t *prepareDirectJitCall(slot_t *callerSlots, void *ctx, const Bytecode *bc) {
    auto *jc = static_cast<JitContext *>(ctx);
    auto *vm = jc->vm;
    ASSERT(bc, "prepareDirectJitCall requires non-null bytecode");

    GIR::Graph *targetGraph = getFuncExtraGraph(bc);
    size_t argsCnt          = bc->normCnt();
    Frame *callerFrame      = reinterpret_cast<Frame *>(callerSlots[0]);
    Frame *newFrame         = vm->acquireFrameForCall(targetGraph);
    for (size_t i = 0; i < argsCnt; ++i) {
        newFrame->set(i + 1, callerFrame->get<slot_t>(bc->operands()[i]));
    }
    newFrame->slotBase()[0] = reinterpret_cast<slot_t>(newFrame);
    return newFrame->slotBase();
}

slot_t *prepareDirectJitTailCall(slot_t *callerSlots, void *ctx, const Bytecode *bc) {
    auto *jc = static_cast<JitContext *>(ctx);
    auto *vm = jc->vm;
    ASSERT(bc, "prepareDirectJitTailCall requires non-null bytecode");

    GIR::Graph *targetGraph = getFuncExtraGraph(bc);
    size_t argsCnt          = bc->normCnt();
    Frame *callerFrame      = reinterpret_cast<Frame *>(callerSlots[0]);
    TailArgStorage args(argsCnt);
    copyOperandsToBuffer(args.data(), callerFrame, *bc, argsCnt);

    vm->releaseFrameForTail(callerFrame);
    Frame *newFrame = vm->acquireFrameForTail(targetGraph);
    writeCallArgsToFrame(newFrame, args.data(), argsCnt);
    newFrame->slotBase()[0] = reinterpret_cast<slot_t>(newFrame);
    return newFrame->slotBase();
}

slot_t finishDirectJitCall(slot_t result, slot_t *calleeSlots, void *ctx, GIR::Graph *owner) {
    auto *jc    = static_cast<JitContext *>(ctx);
    auto *vm    = jc->vm;
    auto *frame = reinterpret_cast<Frame *>(calleeSlots[0]);
    vm->releaseFrameForCall(frame, owner);
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
    GIR::Graph *graph  = getFuncExtraGraph(bc);
    JitEntryFn fn      = reinterpret_cast<JitEntryFn>(getFuncExtraFn(bc));
    size_t argsCnt     = bc->normCnt();
    Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]);
    Frame *newFrame    = vm->acquireFrameForCall(graph);
    for (size_t i = 0; i < argsCnt; ++i) {
        newFrame->set(i + 1, callerFrame->get<slot_t>(bc->operands()[i]));
    }
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
    auto *jc     = static_cast<JitContext *>(ctx);
    auto *vm     = jc->vm;
    auto *base   = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc = base[pc];

    Frame *frame      = reinterpret_cast<Frame *>(slots[0]); // 规范：slot[0] = Frame*
    operator_t func   = bc.extra()->func;
    data_arr_t nargs  = bc.nargs();
    data_arr_t wargs  = bc.wargs();
    data_idx_t result = bc.result;

    FrameArgsView withView(*frame, wargs);
    FrameArgsView normView(*frame, nargs);

    slot_t ret;
    try {
        ret = func(withView, normView, vm->context());
    } catch (const RuntimeFault &fault) {
        throw reportRuntimeFault(
            vm->context(),
            fault,
            makePcExecutionSite(
                vm->context().sourceContext(),
                frame->graph(),
                pc,
                0,
                "",
                ExecutionSiteKind::JitPc));
    }
    frame->set(result, ret);
    return ret;
}

slot_t trampolineCast(slot_t *slots, void *ctx, size_t pc) {
    auto *jc     = static_cast<JitContext *>(ctx);
    auto *base   = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc = base[pc];

    Frame *frame           = reinterpret_cast<Frame *>(slots[0]); // 规范：slot[0] = Frame*
    data_idx_t srcIdx      = bc.fastop[0];
    type::Type *targetType = bc.extra()->pType;
    data_idx_t resultSlot  = bc.result;

    type::Type *srcType = frame->typeAt<type::Type>(srcIdx);
    slot_t value        = frame->get<slot_t>(srcIdx);
    slot_t result       = targetType->castSlotFrom(value, srcType);
    frame->set(resultSlot, result);
    return result;
}

#else
// JIT 关闭时仅提供占位符号，避免链接未定义；不应被调用
slot_t trampolineFunc(slot_t *, void *, size_t) { return {}; }
slot_t trampolineTail(slot_t *, void *, size_t) { return {}; }
slot_t trampolineOper(slot_t *, void *, size_t) { return {}; }
slot_t trampolineCast(slot_t *, void *, size_t) { return {}; }
slot_t *prepareDirectJitCall(slot_t *, void *, const Bytecode *) { return nullptr; }
slot_t *prepareDirectJitTailCall(slot_t *, void *, const Bytecode *) { return nullptr; }
slot_t finishDirectJitCall(slot_t, slot_t *, void *, GIR::Graph *) { return {}; }
#endif

} // extern "C"
