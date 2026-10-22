// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "mir.h"

namespace camel::jit::x64 {

// Win64: remove the `mov rcx, rdi; mov rdx, rsi` pair right after the prologue (rcx/rdx are still
// unchanged there, so the moves are redundant).
void optimizeWin64RedundantArgSetup(MirBuffer &buf);

// Remove no-op: mov rax, rax (MovRegFromRax(0)).
void optimizeRemoveNoopMovRaxRax(MirBuffer &buf);

// Peephole: eliminate store-reload and dead stores. Turning a reload into a copy keeps the stored
// value in its register up to the reload; `forwardStores` = false leaves reloads alone.
void optimizePeephole(MirBuffer &buf, bool forwardStores = true);

// Dead frame store elimination: backward liveness analysis removes VStoreToFrame
// whose slot is never read again before function exit.
void eliminateDeadFrameStores(MirBuffer &buf);

// Multi-pass optimization entry point. `keepValuesInRegisters` = false keeps every value's live
// range inside the lowering of one bytecode (see X64Backend::compileBytecode).
inline void runMirOptimizationPasses(MirBuffer &buf, bool keepValuesInRegisters = true) {
    optimizeWin64RedundantArgSetup(buf);
    optimizeRemoveNoopMovRaxRax(buf);
    optimizePeephole(buf, keepValuesInRegisters);
    eliminateDeadFrameStores(buf);
}

} // namespace camel::jit::x64
