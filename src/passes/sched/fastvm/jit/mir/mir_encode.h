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
 * Updated: Feb. 20, 2026
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

// 将 buf 编码到 code，并可选输出汇编到 asmOut。跳转目标由 buf 内 pc 解析为 MIR 下标，再以
// startOffset[targetMirIndex] 修补 rel32。 vregAlloc 非空时，V*
// 指令按分配结果编码为物理寄存器指令。
// 若 instructionBoundaries 非空，填入每条指令的 (起始偏移, 长度, 汇编文本)。
// debugTraceFn：Debug 构建下对 MirOp::DebugTrace 调用的函数指针（如 jitDebugTrace），null 则跳过。
void encodeMirBuffer(
    const MirBuffer &buf, std::vector<uint8_t> &code, std::ostream *asmOut, size_t baseOffset = 0,
    const ::camel::jit::VRegAllocation *vregAlloc                               = nullptr,
    std::vector<std::tuple<size_t, size_t, std::string>> *instructionBoundaries = nullptr,
    void *debugTraceFn                                                          = nullptr);

} // namespace camel::jit::x64
