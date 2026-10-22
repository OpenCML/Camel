// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "../../bytecode.h"
#include "../../jit/mir/mir.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace camel::jit {

// Allocatable physical register IDs (x64: rax=0, rcx=1, rdx=2, rbx=3, r8=4, r9=5, r10=6, r11=7)
inline constexpr int kNumAllocatableRegs = 8;
inline constexpr int kSpilled            = -1;
// rbx (index 3) is reserved for &FramePool::top_ caching in JIT internal convention
inline constexpr int kReservedRegRbx = 3;
// r11 (index 7) is never given to a virtual register: the MIR encoder uses it as scratch (the
// divisor of idiv, the second flag of a float comparison, frame set-up of a call).
inline constexpr int kReservedRegScratch = 7;

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
 * Virtual register allocation result: vregToPreg[v] = physical register 0..7, or kSpilled for
 * a virtual register that is defined and never used (the encoder skips its definition).
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
 *
 * The encoder has no spill code: every value that is used must be in a register for its whole
 * live range. The allocation therefore fails (returns false, with the reason) rather than leave
 * a used value without one: when more values are live at once than there are registers, when a
 * value would have to survive a call (calls clobber every allocatable register), or when a
 * value is used without a definition. The caller lowers again with shorter live ranges.
 *
 * @param buf MIR containing VLoadFromFrame, VCopy, VTest, VMove, etc.
 * @param out output allocation result
 * @param failureReason set when the allocation fails
 */
bool linearScanVReg(
    const x64::MirBuffer &buf, VRegAllocation *out, std::string *failureReason = nullptr);

} // namespace camel::jit
