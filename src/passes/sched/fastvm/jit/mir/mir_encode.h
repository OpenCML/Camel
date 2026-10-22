// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "../backend/x64/x64_encoder.h"
#include "../mir/mir.h"
#include "../regalloc/regalloc.h"

#include <string>
#include <tuple>
#include <vector>

namespace camel::jit::x64 {

namespace jit = ::camel::jit;

// Encode buf into code and optionally emit assembly to asmOut. Jump targets are resolved from the
// pc in buf to MIR indices, then rel32 is patched via startOffset[targetMirIndex]. When vregAlloc
// is non-null, V* instructions are encoded using the allocated physical registers. If
// instructionBoundaries is non-null, fill each instruction's (start offset, length, assembly text).
// debugTraceFn is the function pointer used for MirOp::DebugTrace calls in Debug builds (for
// example jitDebugTrace); null skips them.
// faultExitOffset is the offset in `code` of the entry wrapper's epilogue (the instruction after
// its call of the body): where a fault site continues once it has recorded the fault and
// restored the stack pointer the wrapper saved in r14.
void encodeMirBuffer(
    const MirBuffer &buf, std::vector<uint8_t> &code, std::ostream *asmOut, size_t baseOffset = 0,
    const jit::VRegAllocation *vregAlloc                                        = nullptr,
    std::vector<std::tuple<size_t, size_t, std::string>> *instructionBoundaries = nullptr,
    void *debugTraceFn = nullptr, size_t faultExitOffset = 0);

} // namespace camel::jit::x64
