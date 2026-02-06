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

#include "builtin/passes/sched/linear/fastvm/bytecode.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace camel::jit {

// 可分配的物理寄存器编号 (x64: rax=0, rcx=1, rdx=2, rbx=3, r8=4, r9=5, r10=6, r11=7)
inline constexpr int kNumAllocatableRegs = 8;
inline constexpr int kSpilled            = -1;

/**
 * 分配结果：slot 索引 -> 物理寄存器 (0..7) 或 kSpilled
 * slotRegs_[i] 表示 slot i 在存活区间内分配的寄存器
 */
struct AllocationResult {
    std::vector<int> slotToReg; // data_idx_t 范围，负索引不分配
    int maxSlot() const { return static_cast<int>(slotToReg.size()) - 1; }
    int regForSlot(int slot) const {
        if (slot < 0 || slot >= static_cast<int>(slotToReg.size()))
            return kSpilled;
        return slotToReg[static_cast<size_t>(slot)];
    }
};

/**
 * 对字节码序列进行线性扫描寄存器分配
 * @param bytecodes 字节码
 * @param entryPc 入口 pc
 * @param pcEnd 结束 pc (不包含)
 * @return 分配结果，失败返回空
 */
AllocationResult
linearScanAllocate(std::span<const Bytecode> bytecodes, size_t entryPc, size_t pcEnd);

} // namespace camel::jit
