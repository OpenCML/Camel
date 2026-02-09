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
 * Updated: Feb. 09, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "builtin/passes/sched/linear/fastvm/bytecode.h"
#include "builtin/passes/sched/linear/fastvm/jit/mir/mir.h"

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

/**
 * 生成“全部溢出”的分配结果，用于 MIR 可读 dump（所有 slot 均从 [rdi+disp] 访问）
 */
AllocationResult
makeAllSpilledAlloc(std::span<const Bytecode> bytecodes, size_t entryPc, size_t pcEnd);

// -----------------------------------------------------------------------------
// 虚拟寄存器分配：对 MIR 中的 V* 指令做统一寄存器分配，便于消除“经 rax 中转”等冗余
// -----------------------------------------------------------------------------

/**
 * 约束（预留）：在指定指令处某 vreg 必须位于某物理寄存器，用于 idiv(rax)、cmov 等
 * 后续可在 linearScanVReg 中解析并插入 move 或优先分配。
 */
struct VRegConstraint {
    size_t instrIndex;
    x64::VRegId vreg;
    uint8_t requiredPreg;
};

struct VRegAllocOptions {
    std::vector<VRegConstraint> fixedAssignments; // 预留，当前未使用
};

/**
 * 虚拟寄存器分配结果：vregToPreg[v] = 物理寄存器 0..7 或 kSpilled
 */
struct VRegAllocation {
    std::vector<int> vregToPreg;

    int pregForVReg(x64::VRegId v) const {
        if (v >= vregToPreg.size())
            return kSpilled;
        return vregToPreg[v];
    }
};

/**
 * 对 MIR buffer 中的 V* 指令做线性扫描寄存器分配
 * @param buf 含 VLoadFromFrame、VCopy、VTest、VCmove 等指令的 MIR
 * @param out 输出分配结果
 * @param opts 可选约束（预留）
 * @return 是否成功（当前恒为 true）
 */
bool linearScanVReg(
    const x64::MirBuffer &buf, VRegAllocation *out, const VRegAllocOptions *opts = nullptr);

} // namespace camel::jit
