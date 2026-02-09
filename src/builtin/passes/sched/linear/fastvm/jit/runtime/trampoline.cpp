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
 * Updated: Feb. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "trampoline.h"

#include "core/rtdata/data.h"

#if ENABLE_FASTVM_JIT
#include "builtin/passes/sched/linear/fastvm/bytecode.h"
#include "builtin/passes/sched/linear/fastvm/fastvm.h"
#include "core/context/frame.h"
#include "core/operator.h"
#include "jit_debug_trace.h"
#include "utils/log.h"

#include <iomanip>
#include <sstream>
#endif

extern "C" {

#if ENABLE_FASTVM_JIT
// 由 jitDebugTrace 存根 tail-call，C linkage 供 asm "jmp jitDebugTraceBody" 解析
extern "C" void jitDebugTraceBody(const void *ctx) {
    if (!ctx)
        return;
    const auto *c = static_cast<const camel::jit::JitDebugContext *>(ctx);
    EXEC_WHEN_DEBUG({
        std::ostringstream os;
        os << std::hex << std::setfill(' ');
        os << "trace pc=" << c->pc << "\n";
        const auto one = [&os](const char *name, uint64_t val) {
            os << "  " << std::setw(4) << std::left << name << " " << std::right << "0x"
               << std::setw(16) << val;
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
        l.in("JIT.Debug").debug("{}", os.str());
    });
}

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
        FrameMeta *meta           = g->getExtra<FrameMeta, 0>();
        if (!meta)
            meta = installFrameMetaInfoForGraph(g);
        size_t calleeSlotCount = meta->runtimeDataType->size();
        slot_t *calleeSlots    = static_cast<slot_t *>(alloca(calleeSlotCount * sizeof(slot_t)));
        for (size_t i = 0; i < argsCnt; ++i)
            calleeSlots[i + 1] = callerSlots[bc.operands()[i]];
        EXEC_WHEN_DEBUG(
            l.in("JIT.Trampoline").debug("trampolineFunc: JIT->JIT target='{}'", g->name()));
        return fn(calleeSlots, ctx);
    }

    GraphIR::Graph *targetGraph = getFuncExtraGraph(&bc);
    EXEC_WHEN_DEBUG(
        l.in("JIT.Trampoline")
            .debug("trampolineFunc: JIT->interpreter target='{}'", targetGraph->name()));

    Frame *newFrame = vm->acquireFrameForCall(targetGraph);
    for (size_t i = 0; i < argsCnt; ++i)
        newFrame->set(i + 1, callerSlots[bc.operands()[i]]);
    (void)count;
    slot_t result = vm->call(targetPc, newFrame);
    vm->releaseFrameForCall(newFrame);
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
        FrameMeta *meta           = g->getExtra<FrameMeta, 0>();
        if (!meta)
            meta = installFrameMetaInfoForGraph(g);
        size_t calleeSlotCount = meta->runtimeDataType->size();
        slot_t *calleeSlots    = static_cast<slot_t *>(alloca(calleeSlotCount * sizeof(slot_t)));
        for (size_t i = 0; i < argsCnt; ++i)
            calleeSlots[i + 1] = callerSlots[bc.operands()[i]];
        EXEC_WHEN_DEBUG(
            l.in("JIT.Trampoline").debug("trampolineTail: JIT->JIT target='{}'", g->name()));
        return fn(calleeSlots, ctx);
    }

    GraphIR::Graph *targetGraph = getFuncExtraGraph(&bc);
    EXEC_WHEN_DEBUG(
        l.in("JIT.Trampoline")
            .debug("trampolineTail: JIT->interpreter target='{}'", targetGraph->name()));

    Frame *newFrame = vm->acquireFrameForTail(targetGraph);
    for (size_t i = 0; i < argsCnt; ++i)
        newFrame->set(i + 1, callerSlots[bc.operands()[i]]);
    (void)count;
    return vm->call(targetPc, newFrame);
}

slot_t trampolineOper(slot_t *slots, void *ctx, size_t pc, GraphIR::Graph *graph) {
    auto *jc     = static_cast<camel::jit::JitContext *>(ctx);
    auto *vm     = jc->vm;
    auto *base   = static_cast<Bytecode *>(const_cast<void *>(jc->base));
    Bytecode &bc = base[pc];

    operator_t func   = bc.extra()->func;
    data_arr_t nargs  = bc.nargs();
    data_arr_t wargs  = bc.wargs();
    data_idx_t result = bc.result;
    FrameMeta *meta   = graph->getExtra<FrameMeta, 0>();
    if (!meta)
        meta = installFrameMetaInfoForGraph(graph);

    SlotArgsView
        withView(slots, meta->staticArea, meta->runtimeDataType, graph->staticDataType(), wargs);
    SlotArgsView
        normView(slots, meta->staticArea, meta->runtimeDataType, graph->staticDataType(), nargs);

    slot_t ret    = func(withView, normView, vm->context());
    slots[result] = ret;
    return ret;
}

#else
// JIT 关闭时仅提供占位符号，避免链接未定义；不应被调用
slot_t trampolineFunc(slot_t *, void *, size_t) { return {}; }
slot_t trampolineTail(slot_t *, void *, size_t) { return {}; }
slot_t trampolineOper(slot_t *, void *, size_t, GraphIR::Graph *) { return {}; }
#endif

} // extern "C"
