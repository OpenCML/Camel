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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../../bytecode.h"
#include "../../jit/mir/mir.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace camel::jit {

// Allocatable physical register IDs (x64: rax=0, rcx=1, rdx=2, rbx=3, r8=4, r9=5, r10=6, r11=7)
inline constexpr int kNumAllocatableRegs = 8;
inline constexpr int kSpilled            = -1;
// rbx (index 3) is reserved for &FramePool::top_ caching in JIT internal convention
inline constexpr int kReservedRegRbx = 3;

/**
 * Allocation result: slot index -> physical register (0..7) or kSpilled.
 * slotRegs_[i] stores the register assigned to slot i within its live range.
 */
struct AllocationResult {
    std::vector<int> slotToReg; // data_idx_t range; negative indices are not allocated.
    int maxSlot() const { return static_cast<int>(slotToReg.size()) - 1; }
    int regForSlot(int slot) const {
        if (slot < 0 || slot >= static_cast<int>(slotToReg.size()))
            return kSpilled;
        return slotToReg[static_cast<size_t>(slot)];
    }
};

/**
 * Perform linear-scan register allocation over a bytecode sequence.
 * @param bytecodes bytecode sequence
 * @param entryPc entry pc
 * @param pcEnd end pc (exclusive)
 * @return allocation result; empty on failure
 */
AllocationResult
linearScanAllocate(std::span<const Bytecode> bytecodes, size_t entryPc, size_t pcEnd);

/**
 * Produce an "all spilled" allocation result for readable MIR dumps (all slots are accessed
 * from
 * [rdi+disp]).
 */
AllocationResult
makeAllSpilledAlloc(std::span<const Bytecode> bytecodes, size_t entryPc, size_t pcEnd);

// -----------------------------------------------------------------------------
// Virtual register allocation: assign registers uniformly to MIR V* instructions to eliminate
// redundant moves such as rax staging.

/**
 * Constraint (reserved): at a given instruction, a vreg must reside in a specific physical
 *
 * register, for idiv(rax), cmov, etc. Later linearScanVReg can honor it by inserting moves or
 *
 * preferring that assignment.
 */
struct VRegConstraint {
    size_t instrIndex;
    x64::VRegId vreg;
    uint8_t requiredPreg;
};

struct VRegAllocOptions {
    std::vector<VRegConstraint> fixedAssignments; // Reserved; not used yet.
};

/**
 * Virtual register allocation result: vregToPreg[v] = physical register 0..7 or kSpilled.
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
 * Perform linear-scan register allocation for V* instructions in a MIR buffer.
 * @param buf MIR containing VLoadFromFrame, VCopy, VTest, VMove, etc.
 * @param out output allocation result
 * @param opts optional constraints (reserved)
 * @return whether allocation succeeded (currently always true)
 */
bool linearScanVReg(
    const x64::MirBuffer &buf, VRegAllocation *out, const VRegAllocOptions *opts = nullptr);

} // namespace camel::jit
