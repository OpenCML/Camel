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
 * Updated: Mar. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "regalloc.h"

#include <algorithm>
#include <utility>

namespace camel::jit {

namespace {

using namespace x64;

static bool isVRegOp(MirOp op) {
    switch (op) {
    case MirOp::VLoadFromFrame:
    case MirOp::VStoreToFrame:
    case MirOp::VLoadFromMemAt:
    case MirOp::VLoadImm32:
    case MirOp::VLoadImm64:
    case MirOp::VCopy:
    case MirOp::VTest:
    case MirOp::VCmove:
    case MirOp::VCmovnz:
    case MirOp::VMovFromRax:
    case MirOp::VMovToRax:
    case MirOp::VRet:
    case MirOp::VAddImm:
    case MirOp::VSubImm:
    case MirOp::VCmpSetLImm:
    case MirOp::VCmpSetLEImm:
    case MirOp::VCmpSetGImm:
    case MirOp::VCmpSetGEImm:
    case MirOp::VCmpSetEImm:
    case MirOp::VCmpSetNEImm:
    case MirOp::VCmpRegImm:
    case MirOp::VAdd:
    case MirOp::VSub:
    case MirOp::VMul:
    case MirOp::VIdiv:
    case MirOp::VCmpSetL:
    case MirOp::VCmpSetLE:
    case MirOp::VCmpSetG:
    case MirOp::VCmpSetGE:
    case MirOp::VCmpSetE:
    case MirOp::VCmpSetNE:
    case MirOp::VXmmLoadFromFrame:
    case MirOp::VXmmStoreToFrame:
    case MirOp::VXmmLoadFromMemAt:
    case MirOp::VXmmAdd:
    case MirOp::VXmmSub:
    case MirOp::VXmmMul:
    case MirOp::VXmmDiv:
    case MirOp::VXmmCmpSetB:
    case MirOp::VXmmCmpSetBE:
    case MirOp::VXmmCmpSetE:
    case MirOp::VXmmCmpSetA:
    case MirOp::VXmmCmpSetAE:
    case MirOp::VXmmCmpSetNZ:
    case MirOp::VAdd32:
    case MirOp::VSub32:
    case MirOp::VMul32:
    case MirOp::VIdiv32:
    case MirOp::VCmpSetL32:
    case MirOp::VCmpSetLE32:
    case MirOp::VCmpSetG32:
    case MirOp::VCmpSetGE32:
    case MirOp::VCmpSetE32:
    case MirOp::VCmpSetNE32:
    case MirOp::VXmm32LoadFromFrame:
    case MirOp::VXmm32StoreToFrame:
    case MirOp::VXmm32LoadFromMemAt:
    case MirOp::VXmm32Add:
    case MirOp::VXmm32Sub:
    case MirOp::VXmm32Mul:
    case MirOp::VXmm32Div:
    case MirOp::VXmm32CmpSetB:
    case MirOp::VXmm32CmpSetBE:
    case MirOp::VXmm32CmpSetE:
    case MirOp::VXmm32CmpSetA:
    case MirOp::VXmm32CmpSetAE:
    case MirOp::VXmm32CmpSetNZ:
        return true;
    default:
        return false;
    }
}

// Collect the virtual-register live range in MIR space. The allocator only
// needs the first definition point and the last use point because it uses a
// plain linear-scan strategy instead of graph coloring.
void collectVRegDefUse(
    const MirBuffer &buf, std::vector<size_t> &firstDef, std::vector<size_t> &lastUse) {
    auto ensureSize = [&](VRegId v) {
        if (v != kInvalidVReg && v < kMaxVRegs) {
            size_t s = static_cast<size_t>(v) + 1;
            if (firstDef.size() < s) {
                firstDef.resize(s, static_cast<size_t>(-1));
                lastUse.resize(s, 0);
            }
        }
    };
    auto useVReg = [&](size_t idx, VRegId v) {
        if (v != kInvalidVReg) {
            ensureSize(v);
            lastUse[static_cast<size_t>(v)] = idx;
        }
    };
    auto defVReg = [&](size_t idx, VRegId v) {
        if (v != kInvalidVReg) {
            ensureSize(v);
            if (firstDef[static_cast<size_t>(v)] == static_cast<size_t>(-1))
                firstDef[static_cast<size_t>(v)] = idx;
        }
    };

    for (size_t i = 0; i < buf.size(); ++i) {
        const Mir &m = buf[i];
        // Frameless NativeJitFuncCall can consume already-loaded argument vregs
        // and define its result directly into a new vreg. These data edges live
        // outside the normal r0/r1/imm32 encoding, so they must be modeled here
        // explicitly or the allocator will treat them as invisible.
        if (m.op == MirOp::NativeJitFuncCall) {
            auto *p = reinterpret_cast<const NativeJitCallParams *>(m.imm64);
            if (p && p->frameless) {
                uint8_t nArgs = p->argsCnt < 7 ? p->argsCnt : 7;
                for (uint8_t ai = 0; ai < nArgs; ++ai) {
                    if (p->argVRegs[ai] != 0xFF)
                        useVReg(i, static_cast<VRegId>(p->argVRegs[ai]));
                }
                if (p->resultVReg != 0xFF)
                    defVReg(i, static_cast<VRegId>(p->resultVReg));
            }
            continue;
        }
        if (!isVRegOp(m.op))
            continue;
        VRegId r0   = static_cast<VRegId>(m.r0);
        VRegId r1   = static_cast<VRegId>(m.r1);
        VRegId immV = static_cast<VRegId>(m.imm32);
        switch (m.op) {
        case MirOp::VLoadFromFrame:
        case MirOp::VLoadFromMemAt:
        case MirOp::VMovFromRax:
        case MirOp::VLoadImm32:
        case MirOp::VLoadImm64:
        case MirOp::VXmmLoadFromFrame:
        case MirOp::VXmmLoadFromMemAt:
        case MirOp::VXmm32LoadFromFrame:
        case MirOp::VXmm32LoadFromMemAt:
            defVReg(i, r0);
            break;
        case MirOp::VStoreToFrame:
        case MirOp::VMovToRax:
        case MirOp::VXmmStoreToFrame:
        case MirOp::VXmm32StoreToFrame:
            useVReg(i, r0);
            break;
        case MirOp::VCopy:
        case MirOp::VAddImm:
        case MirOp::VSubImm:
        case MirOp::VCmpSetLImm:
        case MirOp::VCmpSetLEImm:
        case MirOp::VCmpSetGImm:
        case MirOp::VCmpSetGEImm:
        case MirOp::VCmpSetEImm:
        case MirOp::VCmpSetNEImm:
            defVReg(i, r0);
            useVReg(i, r1);
            break;
        case MirOp::VTest:
        case MirOp::VRet:
        case MirOp::VCmpRegImm:
            useVReg(i, r0);
            break;
        case MirOp::VCmove:
        case MirOp::VCmovnz:
            defVReg(i, r0);
            useVReg(i, r0);
            useVReg(i, r1);
            break;
        case MirOp::VAdd:
        case MirOp::VSub:
        case MirOp::VMul:
        case MirOp::VIdiv:
        case MirOp::VCmpSetL:
        case MirOp::VCmpSetLE:
        case MirOp::VCmpSetG:
        case MirOp::VCmpSetGE:
        case MirOp::VCmpSetE:
        case MirOp::VCmpSetNE:
        case MirOp::VXmmAdd:
        case MirOp::VXmmSub:
        case MirOp::VXmmMul:
        case MirOp::VXmmDiv:
        case MirOp::VXmmCmpSetB:
        case MirOp::VXmmCmpSetBE:
        case MirOp::VXmmCmpSetE:
        case MirOp::VXmmCmpSetA:
        case MirOp::VXmmCmpSetAE:
        case MirOp::VXmmCmpSetNZ:
        case MirOp::VAdd32:
        case MirOp::VSub32:
        case MirOp::VMul32:
        case MirOp::VIdiv32:
        case MirOp::VCmpSetL32:
        case MirOp::VCmpSetLE32:
        case MirOp::VCmpSetG32:
        case MirOp::VCmpSetGE32:
        case MirOp::VCmpSetE32:
        case MirOp::VCmpSetNE32:
        case MirOp::VXmm32Add:
        case MirOp::VXmm32Sub:
        case MirOp::VXmm32Mul:
        case MirOp::VXmm32Div:
        case MirOp::VXmm32CmpSetB:
        case MirOp::VXmm32CmpSetBE:
        case MirOp::VXmm32CmpSetE:
        case MirOp::VXmm32CmpSetA:
        case MirOp::VXmm32CmpSetAE:
        case MirOp::VXmm32CmpSetNZ:
            defVReg(i, r0);
            useVReg(i, r1);
            useVReg(i, immV);
            break;
        default:
            break;
        }
    }
}

} // namespace

namespace {

struct LiveInterval {
    int slot;
    size_t start;
    size_t end;
    // Slot-based allocation only uses caller-saved registers, so any value that
    // survives across a call is forced to spill.
    bool spansCall = false;
};

inline bool isCallOpcode(OpCode op) {
    return op == OpCode::FUNC || op == OpCode::TAIL || op == OpCode::CALL || op == OpCode::OPER;
}

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

    // This is the older slot-level allocator used outside the MIR backend.
    // It mirrors bytecode semantics directly, so uses/defs are expressed in
    // frame-slot indices instead of virtual registers.
    switch (bc.opcode) {
    // 定长：RETN, CAST, COPY, ACCS, JUMP
    case OpCode::RETN:
        useSlot(bc.fastop[0]);
        break;
    case OpCode::CAST:
        useSlot(bc.fastop[0]);
        defSlot(bc.result);
        break;
    case OpCode::COPY:
        useSlot(bc.fastop[0]);
        defSlot(bc.result);
        break;
    case OpCode::ACCS:
        useSlot(bc.fastop[0]);
        useSlot(bc.fastop[1]);
        defSlot(bc.result);
        break;
    case OpCode::JUMP:
        break;

    // 变长：BRCH, JOIN, FILL, CALL, FUNC, TAIL, OPER, SCHD
    case OpCode::BRCH:
        for (size_t i = 0; i < bc.normCnt(); ++i)
            useSlot(bc.nargs()[i]);
        break;
    case OpCode::JOIN:
        for (size_t i = 0; i < bc.argsCnt(); ++i)
            useSlot(bc.operands()[i]);
        defSlot(bc.result);
        break;
    case OpCode::FILL:
        for (size_t i = 0; i < bc.argsCnt(); ++i)
            useSlot(bc.operands()[i]);
        defSlot(bc.result);
        break;
    case OpCode::CALL:
        for (size_t i = 0; i < bc.argsCnt(); ++i)
            useSlot(bc.operands()[i]);
        defSlot(bc.result);
        break;
    case OpCode::FUNC:
    case OpCode::TAIL:
        for (size_t i = 0; i < bc.normCnt(); ++i)
            useSlot(bc.operands()[i]);
        defSlot(bc.result);
        break;
    case OpCode::OPER:
        for (size_t i = 0; i < bc.argsCnt(); ++i)
            useSlot(bc.operands()[i]);
        defSlot(bc.result);
        break;
    case OpCode::SCHD:
        for (size_t i = 0; i < bc.argsCnt(); ++i)
            useSlot(bc.operands()[i]);
        defSlot(bc.result);
        break;

    // 二元算术：use fastop[0], fastop[1]; def result
    case OpCode::IADD:
    case OpCode::LADD:
    case OpCode::FADD:
    case OpCode::DADD:
    case OpCode::ISUB:
    case OpCode::LSUB:
    case OpCode::FSUB:
    case OpCode::DSUB:
    case OpCode::IMUL:
    case OpCode::LMUL:
    case OpCode::FMUL:
    case OpCode::DMUL:
    case OpCode::IDIV:
    case OpCode::LDIV:
    case OpCode::FDIV:
    case OpCode::DDIV:
        useSlot(bc.fastop[0]);
        useSlot(bc.fastop[1]);
        defSlot(bc.result);
        break;

    // 二元比较：use fastop[0], fastop[1]; def result
    case OpCode::ILT:
    case OpCode::LLT:
    case OpCode::FLT:
    case OpCode::DLT:
    case OpCode::IGT:
    case OpCode::LGT:
    case OpCode::FGT:
    case OpCode::DGT:
    case OpCode::IEQ:
    case OpCode::LEQ:
    case OpCode::FEQ:
    case OpCode::DEQ:
    case OpCode::INE:
    case OpCode::LNE:
    case OpCode::FNE:
    case OpCode::DNE:
    case OpCode::ILE:
    case OpCode::LLE:
    case OpCode::FLE:
    case OpCode::DLE:
    case OpCode::IGE:
    case OpCode::LGE:
    case OpCode::FGE:
    case OpCode::DGE:
        useSlot(bc.fastop[0]);
        useSlot(bc.fastop[1]);
        defSlot(bc.result);
        break;

    default:
        break;
    }
}

} // namespace

AllocationResult
linearScanAllocate(std::span<const Bytecode> bytecodes, size_t entryPc, size_t pcEnd) {
    std::vector<size_t> firstDef, lastUse;
    std::vector<size_t> callPcs;
    const Bytecode *base = bytecodes.data();

    for (size_t pc = entryPc; pc < pcEnd;) {
        const Bytecode &bc = base[pc];
        collectDefUse(bc, pc, firstDef, lastUse);
        if (isCallOpcode(bc.opcode))
            callPcs.push_back(pc);
        pc += bc.opsize;
    }

    auto intervalSpansCall = [&callPcs](size_t start, size_t end) {
        for (size_t cp : callPcs) {
            if (start < cp && cp < end)
                return true;
        }
        return false;
    };
    auto defAtCall = [&callPcs](size_t defPc) {
        for (size_t cp : callPcs) {
            if (defPc == cp)
                return true;
        }
        return false;
    };

    std::vector<LiveInterval> intervals;
    for (size_t i = 1; i < firstDef.size(); ++i) {
        if (firstDef[i] != static_cast<size_t>(-1) && lastUse[i] >= firstDef[i]) {
            // 跨越 call 或 在 call 处定义：caller-saved 会被破坏，且 FUNC 结果总是存到内存
            bool spans = intervalSpansCall(firstDef[i], lastUse[i]) || defAtCall(firstDef[i]);
            intervals.push_back({static_cast<int>(i), firstDef[i], lastUse[i], spans});
        }
    }
    std::sort(intervals.begin(), intervals.end(), [](const LiveInterval &a, const LiveInterval &b) {
        return a.start < b.start;
    });

    AllocationResult result;
    result.slotToReg.resize(firstDef.size(), kSpilled);

    // Active intervals are always kept as the currently-live set. When a new
    // interval arrives, expired ones are removed and their registers reused.
    std::vector<std::pair<LiveInterval, int>> active; // (interval, reg)
    int freeRegs[kNumAllocatableRegs];
    for (int i = 0; i < kNumAllocatableRegs; ++i)
        freeRegs[i] = 1;
    freeRegs[kReservedRegRbx] = 0; // rbx reserved for poolTopAddr caching

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
        if (!cur.spansCall) {
            for (int r = 0; r < kNumAllocatableRegs; ++r) {
                if (freeRegs[r]) {
                    reg         = r;
                    freeRegs[r] = 0;
                    break;
                }
            }
        }
        if (reg < 0) {
            // Classic linear-scan heuristic: spill the interval whose end is
            // furthest in the future. If the current interval ends earlier, it
            // is cheaper to keep the current one in a register instead.
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

AllocationResult
makeAllSpilledAlloc(std::span<const Bytecode> bytecodes, size_t entryPc, size_t pcEnd) {
    std::vector<size_t> firstDef, lastUse;
    const Bytecode *base = bytecodes.data();
    for (size_t pc = entryPc; pc < pcEnd;) {
        const Bytecode &bc = base[pc];
        collectDefUse(bc, pc, firstDef, lastUse);
        pc += bc.opsize;
    }
    AllocationResult result;
    result.slotToReg.resize(firstDef.size(), kSpilled);
    return result;
}

// -----------------------------------------------------------------------------
// 虚拟寄存器线性扫描分配
// -----------------------------------------------------------------------------

struct VLiveInterval {
    x64::VRegId vreg;
    size_t start;
    size_t end;
};

bool linearScanVReg(
    const x64::MirBuffer &buf, VRegAllocation *out, const VRegAllocOptions * /*opts*/) {
    std::vector<size_t> firstDef, lastUse;
    collectVRegDefUse(buf, firstDef, lastUse);
    if (firstDef.empty()) {
        out->vregToPreg.clear();
        return true;
    }

    std::vector<VLiveInterval> intervals;
    for (size_t v = 0; v < firstDef.size(); ++v) {
        if (firstDef[v] != static_cast<size_t>(-1) && lastUse[v] >= firstDef[v]) {
            intervals.push_back({static_cast<x64::VRegId>(v), firstDef[v], lastUse[v]});
        }
    }
    std::sort(
        intervals.begin(),
        intervals.end(),
        [](const VLiveInterval &a, const VLiveInterval &b) { return a.start < b.start; });

    out->vregToPreg.resize(firstDef.size(), kSpilled);
    // MIR allocation is intentionally simple: it ignores calling-convention
    // classes and lets later lowering spill where needed. That keeps the pass
    // fast and predictable while the MIR optimizer is still mostly linear.
    std::vector<std::pair<VLiveInterval, int>> active;
    int freeRegs[kNumAllocatableRegs];
    for (int i = 0; i < kNumAllocatableRegs; ++i)
        freeRegs[i] = 1;
    freeRegs[kReservedRegRbx] = 0; // rbx reserved for poolTopAddr caching

    for (const VLiveInterval &cur : intervals) {
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
                out->vregToPreg[static_cast<size_t>(spill->first.vreg)] = kSpilled;
                reg                                                     = spill->second;
                active.erase(spill);
            }
        }
        if (reg >= 0) {
            out->vregToPreg[static_cast<size_t>(cur.vreg)] = reg;
            active.emplace_back(cur, reg);
        }
    }
    return true;
}

} // namespace camel::jit
