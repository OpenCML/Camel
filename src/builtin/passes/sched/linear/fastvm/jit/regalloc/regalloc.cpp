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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "regalloc.h"

#include <algorithm>
#include <utility>

namespace camel::jit {

namespace {

struct LiveInterval {
    int slot;
    size_t start;
    size_t end;
};

void collectDefUse(
    const Bytecode &bc, size_t pc, std::vector<size_t> &firstDef, std::vector<size_t> &lastUse) {
    auto ensureSize = [&](int idx) {
        if (idx > 0) {
            size_t s = static_cast<size_t>(idx) + 1;
            if (firstDef.size() < s) {
                firstDef.resize(s, static_cast<size_t>(-1));
                lastUse.resize(s, 0);
            }
        }
    };
    auto useSlot = [&](data_idx_t idx) {
        if (idx > 0) {
            ensureSize(idx);
            lastUse[static_cast<size_t>(idx)] = pc;
        }
    };
    auto defSlot = [&](data_idx_t idx) {
        if (idx > 0) {
            ensureSize(idx);
            if (firstDef[static_cast<size_t>(idx)] == static_cast<size_t>(-1))
                firstDef[static_cast<size_t>(idx)] = pc;
        }
    };

    switch (bc.opcode) {
    case OpCode::LADD:
    case OpCode::LSUB:
        useSlot(bc.fastop[0]);
        useSlot(bc.fastop[1]);
        defSlot(bc.result);
        break;
    case OpCode::LLE:
        useSlot(bc.fastop[0]);
        defSlot(bc.result);
        break;
    case OpCode::BRCH:
        if (bc.normCnt() > 0)
            useSlot(bc.nargs()[0]);
        break;
    case OpCode::FUNC:
    case OpCode::TAIL:
        for (size_t i = 0; i < bc.normCnt(); ++i)
            useSlot(bc.operands()[i]);
        defSlot(bc.result);
        break;
    case OpCode::RETN:
        useSlot(bc.fastop[0]);
        break;
    case OpCode::JUMP:
        break;
    default:
        break;
    }
}

} // namespace

AllocationResult
linearScanAllocate(std::span<const Bytecode> bytecodes, size_t entryPc, size_t pcEnd) {
    std::vector<size_t> firstDef, lastUse;
    const Bytecode *base = bytecodes.data();

    for (size_t pc = entryPc; pc < pcEnd;) {
        const Bytecode &bc = base[pc];
        collectDefUse(bc, pc, firstDef, lastUse);
        pc += bc.opsize;
    }

    std::vector<LiveInterval> intervals;
    for (size_t i = 1; i < firstDef.size(); ++i) {
        if (firstDef[i] != static_cast<size_t>(-1) && lastUse[i] >= firstDef[i]) {
            intervals.push_back({static_cast<int>(i), firstDef[i], lastUse[i]});
        }
    }
    std::sort(intervals.begin(), intervals.end(), [](const LiveInterval &a, const LiveInterval &b) {
        return a.start < b.start;
    });

    AllocationResult result;
    result.slotToReg.resize(firstDef.size(), kSpilled);

    std::vector<std::pair<LiveInterval, int>> active; // (interval, reg)
    int freeRegs[kNumAllocatableRegs];
    for (int i = 0; i < kNumAllocatableRegs; ++i)
        freeRegs[i] = 1;

    for (const LiveInterval &cur : intervals) {
        for (auto it = active.begin(); it != active.end();) {
            if (it->first.end <= cur.start) {
                freeRegs[it->second] = 1;
                it                   = active.erase(it);
            } else {
                ++it;
            }
        }

        int reg = -1;
        for (int r = 0; r < kNumAllocatableRegs; ++r) {
            if (freeRegs[r]) {
                reg         = r;
                freeRegs[r] = 0;
                break;
            }
        }
        if (reg < 0) {
            auto spill =
                std::max_element(active.begin(), active.end(), [](const auto &a, const auto &b) {
                    return a.first.end < b.first.end;
                });
            if (spill != active.end() && spill->first.end > cur.end) {
                result.slotToReg[static_cast<size_t>(spill->first.slot)] = kSpilled;
                reg                                                      = spill->second;
                active.erase(spill);
            } else {
                reg = kSpilled; // 当前区间溢出，不分配寄存器
            }
        }
        if (reg >= 0) {
            result.slotToReg[static_cast<size_t>(cur.slot)] = reg;
            active.emplace_back(cur, reg);
        } else {
            result.slotToReg[static_cast<size_t>(cur.slot)] = kSpilled;
        }
    }

    return result;
}

} // namespace camel::jit
