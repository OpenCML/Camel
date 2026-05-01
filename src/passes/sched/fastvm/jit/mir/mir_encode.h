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
void encodeMirBuffer(
    const MirBuffer &buf, std::vector<uint8_t> &code, std::ostream *asmOut, size_t baseOffset = 0,
    const jit::VRegAllocation *vregAlloc                                        = nullptr,
    std::vector<std::tuple<size_t, size_t, std::string>> *instructionBoundaries = nullptr,
    void *debugTraceFn                                                          = nullptr);

} // namespace camel::jit::x64
