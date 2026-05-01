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
 * Created: Feb. 09, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "mir.h"

namespace camel::jit::x64 {

// Win64: remove the `mov rcx, rdi; mov rdx, rsi` pair right after the prologue (rcx/rdx are still
// unchanged there, so the moves are redundant).
void optimizeWin64RedundantArgSetup(MirBuffer &buf);

// Remove no-op: mov rax, rax (MovRegFromRax(0)).
void optimizeRemoveNoopMovRaxRax(MirBuffer &buf);

// Peephole: eliminate store-reload and dead stores.
void optimizePeephole(MirBuffer &buf);

// Dead frame store elimination: backward liveness analysis removes VStoreToFrame
// whose slot is never read again before function exit.
void eliminateDeadFrameStores(MirBuffer &buf);

// Multi-pass optimization entry point.
inline void runMirOptimizationPasses(MirBuffer &buf) {
    optimizeWin64RedundantArgSetup(buf);
    optimizeRemoveNoopMovRaxRax(buf);
    optimizePeephole(buf);
    eliminateDeadFrameStores(buf);
}

} // namespace camel::jit::x64
