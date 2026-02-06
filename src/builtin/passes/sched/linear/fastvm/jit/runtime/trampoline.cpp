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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "trampoline.h"

#include "builtin/passes/sched/linear/fastvm/bytecode.h"
#include "builtin/passes/sched/linear/fastvm/fastvm.h"
#include "core/context/frame.h"

extern "C" {

slot_t trampolineFunc(Frame *frame, void *ctx, size_t pc) {
    auto *jc       = static_cast<camel::jit::JitContext *>(ctx);
    auto *vm       = jc->vm;
    auto *base     = static_cast<const Bytecode *>(jc->base);
    const auto &bc = base[pc];

    GraphIR::Graph *targetGraph = bc.extra()->graph;
    size_t targetPc             = static_cast<size_t>(bc.fastop[1]);

    Frame *newFrame = vm->acquireFrameForCall(targetGraph);
    size_t argsCnt  = bc.normCnt();
    for (size_t i = 0; i < argsCnt; ++i) {
        newFrame->set(i + 1, frame->get<slot_t>(bc.operands()[i]));
    }

    slot_t result = vm->invokeCallOrJit(targetPc, targetGraph, newFrame, ctx);
    vm->releaseFrameForCall(newFrame);
    return result;
}

slot_t trampolineTail(Frame *frame, void *ctx, size_t pc) {
    auto *jc       = static_cast<camel::jit::JitContext *>(ctx);
    auto *vm       = jc->vm;
    auto *base     = static_cast<const Bytecode *>(jc->base);
    const auto &bc = base[pc];

    GraphIR::Graph *targetGraph = bc.extra()->graph;
    size_t targetPc             = static_cast<size_t>(bc.fastop[1]);

    FrameView lastFrame(frame);
    vm->releaseFrameForTail(frame);

    Frame *newFrame = vm->acquireFrameForTail(targetGraph);
    size_t argsCnt  = bc.normCnt();
    for (size_t i = 0; i < argsCnt; ++i) {
        newFrame->set(i + 1, lastFrame.get<slot_t>(bc.operands()[i]));
    }

    return vm->invokeCallOrJit(targetPc, targetGraph, newFrame, ctx);
}

} // extern "C"
