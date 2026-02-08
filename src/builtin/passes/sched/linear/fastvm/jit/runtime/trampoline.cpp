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
 * Updated: Feb. 08, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "trampoline.h"

#include "builtin/passes/sched/linear/fastvm/bytecode.h"
#include "builtin/passes/sched/linear/fastvm/fastvm.h"
#include "core/context/frame.h"
#include "core/operator.h"
#include "utils/log.h"

extern "C" {

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

} // extern "C"
