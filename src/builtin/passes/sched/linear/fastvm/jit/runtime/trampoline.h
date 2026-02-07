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
 * Updated: Feb. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/context/frame.h"
#include "core/rtdata/data.h"

class FastVMSchedPass;

namespace camel::jit {

struct JitContext {
    FastVMSchedPass *vm;
    const void *base; // Bytecode*
};

} // namespace camel::jit

// C linkage for JIT call from machine code
// JIT 传入 slot_t*（调用方动态区），不再依赖 Frame
extern "C" slot_t trampolineFunc(slot_t *callerSlots, void *ctx, size_t pc);
extern "C" slot_t trampolineTail(slot_t *callerSlots, void *ctx, size_t pc);
extern "C" slot_t trampolineOper(slot_t *slots, void *ctx, size_t pc, GraphIR::Graph *graph);
