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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 *
 * ---
 * JIT 调用约定与 slot[0] 规范：
 * 1. 动态区 slot[0] 保留，始终存放当前 Frame*（reinterpret_cast<slot_t>）。解释器/gotovm/casevm
 *    在调用 JIT 入口前写入；trampoline 在 JIT->JIT 调用前写入。slot[1..] 为正常数据槽。
 * 2. trampolineFunc/TrampolineTail：callerSlots[0] 即 caller Frame*，用 callerFrame->get(operand)
 *    取参，以支持 operand 为负（静态区索引）。
 * 3. trampolineOper：slots[0] 即当前 Frame*，用 FrameArgsView 取代 SlotArgsView，无需传 Graph。
 */

#include "trampoline.h"

#include "camel/core/rtdata.h"

#include <cstdint>
#include <cstring>

#if ENABLE_FASTVM_JIT
#include "../../bytecode.h"
#include "../../fastvm.h"
#include "camel/core/context/frame.h"
#include "camel/core/operator.h"
#include "camel/utils/log.h"
#include "jit_debug_trace.h"

#include <iomanip>
#include <sstream>
#endif

extern "C" {

#if ENABLE_FASTVM_JIT
// 由 jitDebugTrace 存根 tail-call，C linkage 供 asm "jmp jitDebugTraceBody" 解析
// rdi 为当前 JIT 的 slot 基址；slot[0] 恒存 Frame*，据此打印当前帧（见上方 slot[0] 规范）
extern "C" void jitDebugTraceBody(const void *ctx) {
    if (!ctx)
        return;
    const auto *c = static_cast<const camel::jit::JitDebugContext *>(ctx);
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
    static thread_local camel::jit::JitDebugContext buf;
    std::memcpy(&buf, ctx, sizeof(camel::jit::JitDebugContext));
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
    auto *jc     = static_cast<camel::jit::JitContext *>(ctx);
    auto *vm     = jc->vm;
    auto *base   = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc = base[pc];

    size_t targetPc = static_cast<size_t>(bc.fastop[1]);
    size_t argsCnt  = bc.normCnt();
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("JIT.Trampoline")
            .info("trampolineFunc bc: targetPc={} argsCnt={}", targetPc, argsCnt));
    uint32_t count = 0;
    if (targetPc != 0)
        count = incFuncExtraCount(&bc);

    if (targetPc == 0) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("JIT.Trampoline").info("trampolineFunc path: JIT->JIT"));
        GraphIR::Graph *g         = getFuncExtraGraph(&bc);
        camel::jit::JitEntryFn fn = reinterpret_cast<camel::jit::JitEntryFn>(getFuncExtraFn(&bc));
        EXEC_WHEN_DEBUG(
            GetDefaultLogger()
                .in("JIT.Trampoline")
                .info(
                    "trampolineFunc JIT->JIT graph='{}' fn={}",
                    g->name(),
                    static_cast<void *>(reinterpret_cast<void *>(fn))));
        Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]); // slot[0] = Frame*
        Frame *newFrame    = vm->acquireFrameForCall(g);
        for (size_t i = 0; i < argsCnt; ++i)
            newFrame->set(
                i + 1,
                callerFrame->get<slot_t>(bc.operands()[i])); // operand 可负（静态区）

        EXEC_WHEN_DEBUG({
            std::ostringstream os;
            os << "trampolineFunc callee frame <" << g->name() << "> (after copy):\n";
            newFrame->printSlotsTo(os);
            GetDefaultLogger().in("JIT.Trampoline").info("{}", os.str());
        });
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("JIT.Trampoline").info("trampolineFunc about to call JIT entry"));
        newFrame->slotBase()[0] = reinterpret_cast<slot_t>(newFrame); // 规范：slot[0] 存 Frame*
        slot_t result;
        try {
            result = fn(newFrame->slotBase(), ctx);
        } catch (...) {
            vm->releaseFrameForCall(newFrame);
            throw;
        }
        EXEC_WHEN_DEBUG(
            GetDefaultLogger()
                .in("JIT.Trampoline")
                .info("trampolineFunc JIT->JIT return result={}", result));
        vm->releaseFrameForCall(newFrame);
        return result;
    }

    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("JIT.Trampoline").info("trampolineFunc path: JIT->interpreter"));
    GraphIR::Graph *targetGraph = getFuncExtraGraph(&bc);
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("JIT.Trampoline")
            .info(
                "trampolineFunc JIT->interpreter target='{}' targetPc={}",
                targetGraph->name(),
                targetPc));

    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("JIT.Trampoline").info("trampolineFunc before acquireFrameForCall"));
    Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]);
    Frame *newFrame    = vm->acquireFrameForCall(targetGraph);
    for (size_t i = 0; i < argsCnt; ++i)
        newFrame->set(i + 1, callerFrame->get<slot_t>(bc.operands()[i]));
    (void)count;
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("JIT.Trampoline")
            .info("trampolineFunc before vm->call targetPc={}", targetPc));
    slot_t result;
    try {
        result = vm->call(targetPc, newFrame);
    } catch (...) {
        vm->releaseFrameForCall(newFrame);
        throw;
    }
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("JIT.Trampoline")
            .info("trampolineFunc after vm->call result={}", result));
    vm->releaseFrameForCall(newFrame);
    EXEC_WHEN_DEBUG(GetDefaultLogger().in("JIT.Trampoline").info("trampolineFunc EXIT"));
    return result;
}

slot_t trampolineTail(slot_t *callerSlots, void *ctx, size_t pc) {
    auto *jc     = static_cast<camel::jit::JitContext *>(ctx);
    auto *vm     = jc->vm;
    auto *base   = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc = base[pc];

    size_t targetPc = static_cast<size_t>(bc.fastop[1]);
    size_t argsCnt  = bc.normCnt();
    uint32_t count  = 0;
    if (targetPc != 0)
        count = incFuncExtraCount(&bc);

    if (targetPc == 0) {
        GraphIR::Graph *g         = getFuncExtraGraph(&bc);
        camel::jit::JitEntryFn fn = reinterpret_cast<camel::jit::JitEntryFn>(getFuncExtraFn(&bc));
        Frame *callerFrame        = reinterpret_cast<Frame *>(callerSlots[0]);
        Frame *newFrame           = vm->acquireFrameForTail(g);
        for (size_t i = 0; i < argsCnt; ++i)
            newFrame->set(i + 1, callerFrame->get<slot_t>(bc.operands()[i]));
        EXEC_WHEN_DEBUG(
            GetDefaultLogger()
                .in("JIT.Trampoline")
                .debug("trampolineTail: JIT->JIT target='{}'", g->name()));
        newFrame->slotBase()[0] = reinterpret_cast<slot_t>(newFrame);
        slot_t result;
        try {
            result = fn(newFrame->slotBase(), ctx);
        } catch (...) {
            vm->releaseFrameForTail(newFrame);
            throw;
        }
        vm->releaseFrameForTail(newFrame);
        return result;
    }

    GraphIR::Graph *targetGraph = getFuncExtraGraph(&bc);
    EXEC_WHEN_DEBUG(
        GetDefaultLogger()
            .in("JIT.Trampoline")
            .debug("trampolineTail: JIT->interpreter target='{}'", targetGraph->name()));

    Frame *callerFrame = reinterpret_cast<Frame *>(callerSlots[0]);
    Frame *newFrame    = vm->acquireFrameForTail(targetGraph);
    for (size_t i = 0; i < argsCnt; ++i)
        newFrame->set(i + 1, callerFrame->get<slot_t>(bc.operands()[i]));
    (void)count;
    try {
        return vm->call(targetPc, newFrame);
    } catch (...) {
        vm->releaseFrameForTail(newFrame);
        throw;
    }
}

slot_t trampolineOper(slot_t *slots, void *ctx, size_t pc) {
    auto *jc     = static_cast<camel::jit::JitContext *>(ctx);
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
    auto *jc     = static_cast<camel::jit::JitContext *>(ctx);
    auto *base   = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc = base[pc];

    Frame *frame          = reinterpret_cast<Frame *>(slots[0]); // 规范：slot[0] = Frame*
    data_idx_t srcIdx     = bc.fastop[0];
    Type *targetType      = bc.extra()->pType;
    data_idx_t resultSlot = bc.result;

    Type *srcType = frame->typeAt<Type>(srcIdx);
    slot_t value  = frame->get<slot_t>(srcIdx);
    slot_t result = targetType->castSlotFrom(value, srcType);
    frame->set(resultSlot, result);
    return result;
}

#else
// JIT 关闭时仅提供占位符号，避免链接未定义；不应被调用
slot_t trampolineFunc(slot_t *, void *, size_t) { return {}; }
slot_t trampolineTail(slot_t *, void *, size_t) { return {}; }
slot_t trampolineOper(slot_t *, void *, size_t) { return {}; }
slot_t trampolineCast(slot_t *, void *, size_t) { return {}; }
#endif

} // extern "C"
