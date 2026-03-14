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
 * Updated: Mar. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "mir_optimize.h"

#include <unordered_map>
#include <unordered_set>

namespace camel::jit::x64 {

void optimizeWin64RedundantArgSetup(MirBuffer &buf) {
    // The C++ ABI wrapper already moves rcx/rdx into rdi/rsi. If the MIR body
    // immediately copies them back to rcx/rdx for a helper call, the middle two
    // copies are redundant and only lengthen the dependency chain.
    for (size_t i = 0; i + 3 < buf.size(); ++i) {
        const Mir &a = buf[i];
        const Mir &b = buf[i + 1];
        const Mir &c = buf[i + 2];
        const Mir &d = buf[i + 3];
        if (a.op != MirOp::MovRegReg || a.r0 != kRegRdi || a.r1 != kRegRcx)
            continue;
        if (b.op != MirOp::MovRegReg || b.r0 != kRegRsi || b.r1 != kRegRdx)
            continue;
        if (c.op != MirOp::MovRegReg || c.r0 != kRegRcx || c.r1 != kRegRdi)
            continue;
        if (d.op != MirOp::MovRegReg || d.r0 != kRegRdx || d.r1 != kRegRsi)
            continue;
        buf.erase(
            buf.begin() + static_cast<std::ptrdiff_t>(i + 2),
            buf.begin() + static_cast<std::ptrdiff_t>(i + 4));
        return;
    }
}

void optimizeRemoveNoopMovRaxRax(MirBuffer &buf) { (void)buf; }

static bool isJumpOp(MirOp op) {
    switch (op) {
    case MirOp::JzRel32:
    case MirOp::JmpRel32:
    case MirOp::JleRel32:
    case MirOp::JlRel32:
    case MirOp::JgRel32:
    case MirOp::JgeRel32:
    case MirOp::JeRel32:
    case MirOp::JneRel32:
    case MirOp::JmpRel8:
    case MirOp::JleRel8:
        return true;
    default:
        return false;
    }
}

static bool isBarrierOp(MirOp op) {
    switch (op) {
    case MirOp::Ret:
    case MirOp::JmpRax:
    case MirOp::NativeJitFuncCall:
    case MirOp::CallRax:
    case MirOp::CallRel32:
    case MirOp::PushRdi:
    case MirOp::PopRdi:
        return true;
    default:
        return isJumpOp(op);
    }
}

static bool readsFrameSlot(MirOp op) {
    switch (op) {
    case MirOp::VLoadFromFrame:
    case MirOp::VXmmLoadFromFrame:
    case MirOp::VXmm32LoadFromFrame:
        return true;
    default:
        return false;
    }
}

static bool writesFrameSlot(MirOp op) {
    switch (op) {
    case MirOp::VStoreToFrame:
    case MirOp::VXmmStoreToFrame:
    case MirOp::VXmm32StoreToFrame:
        return true;
    default:
        return false;
    }
}

void optimizePeephole(MirBuffer &buf) {
    const size_t n = buf.size();
    if (n == 0)
        return;

    // Only bytecode-PC jump targets act as control-flow merge points here.
    // Once we hit one, any pending linear-store knowledge must be discarded.
    std::unordered_set<uint32_t> jumpTargets;
    for (const auto &m : buf) {
        if (isJumpOp(m.op))
            jumpTargets.insert(m.imm32);
    }

    // Track the latest integer frame store per disp. We only keep a single
    // linear history entry because this pass is intentionally local and
    // conservative; non-integer stores simply invalidate the slot.
    struct StoreInfo {
        uint8_t vreg;
        size_t index;
        bool read;
    };
    std::unordered_map<int32_t, StoreInfo> pendingStore;

    for (size_t i = 0; i < n; ++i) {
        Mir &m = buf[i];

        if (m.op == MirOp::Nop || m.op == MirOp::DebugTrace)
            continue;

        if (m.hasPc() && jumpTargets.count(m.pc))
            pendingStore.clear();

        if (isBarrierOp(m.op)) {
            // Calls and explicit control-flow transfers may clobber registers or
            // jump to code we have not analyzed linearly, so cached knowledge is
            // no longer trustworthy past this point.
            pendingStore.clear();
            continue;
        }

        if (writesFrameSlot(m.op)) {
            int32_t d = m.disp;
            if (m.op == MirOp::VStoreToFrame) {
                auto it = pendingStore.find(d);
                if (it != pendingStore.end() && !it->second.read)
                    buf[it->second.index].op = MirOp::Nop;
                pendingStore[d] = {m.r0, i, false};
            } else {
                pendingStore.erase(d);
            }
            continue;
        }

        if (readsFrameSlot(m.op)) {
            int32_t d = m.disp;
            auto it   = pendingStore.find(d);
            if (it != pendingStore.end()) {
                if (m.op == MirOp::VLoadFromFrame) {
                    // The stored value already exists in a vreg, so turn the
                    // reload into a pure copy and let register allocation decide
                    // whether it stays in-place or becomes a move.
                    m.op   = MirOp::VCopy;
                    m.r1   = it->second.vreg;
                    m.disp = 0;
                }
                it->second.read = true;
            }
            continue;
        }
    }
}

void eliminateDeadFrameStores(MirBuffer &buf) {
    const size_t n = buf.size();
    if (n == 0)
        return;

    // We still do not build a full CFG. Instead, record the liveness snapshot at
    // every jump target and merge it back when visiting the corresponding jump
    // source in reverse order.
    std::unordered_set<uint32_t> jumpTargets;
    for (const auto &m : buf) {
        if (isJumpOp(m.op))
            jumpTargets.insert(m.imm32);
    }

    // Backward pass: a slot is live if some later instruction reads it before a
    // dominating overwrite. Any store to a non-live slot is dead.
    std::unordered_set<int32_t> liveSlots;
    // Save liveSlots snapshot at each jump target so forward jumps can merge.
    std::unordered_map<uint32_t, std::unordered_set<int32_t>> targetLiveness;

    for (size_t i = n; i > 0; --i) {
        Mir &m = buf[i - 1];

        if (m.op == MirOp::Nop || m.op == MirOp::DebugTrace)
            continue;

        // At jump targets, save current liveness for merge when we encounter the jump source.
        if (m.hasPc() && jumpTargets.count(m.pc))
            targetLiveness[m.pc] = liveSlots;

        // Function exit: no frame slots are live after return.
        if (m.op == MirOp::Ret || m.op == MirOp::JmpRax) {
            liveSlots.clear();
            continue;
        }
        if (m.op == MirOp::VRet) {
            liveSlots.clear();
            continue;
        }

        // Jump source: merge liveness from the target (already saved above).
        if (isJumpOp(m.op)) {
            auto it = targetLiveness.find(m.imm32);
            if (it != targetLiveness.end())
                liveSlots.insert(it->second.begin(), it->second.end());
            continue;
        }

        // NativeJitFuncCall: reads arg disps (if not pre-loaded), writes result disp (if opaque).
        if (m.op == MirOp::NativeJitFuncCall) {
            auto *p = reinterpret_cast<const NativeJitCallParams *>(m.imm64);
            if (p) {
                uint8_t nArgs = p->argsCnt < 7 ? p->argsCnt : 7;
                for (uint8_t ai = 0; ai < nArgs; ++ai) {
                    if (p->argVRegs[ai] == 0xFF)
                        liveSlots.insert(p->argSrcDisps[ai]);
                }
                // Frameless calls can expose the result as a vreg def instead of
                // an implicit frame write. Only the opaque path keeps resultDisp
                // alive as a memory destination.
                if (p->resultVReg == 0xFF)
                    liveSlots.erase(p->resultDisp);
            }
            continue;
        }

        if (readsFrameSlot(m.op)) {
            liveSlots.insert(m.disp);
            continue;
        }

        if (writesFrameSlot(m.op)) {
            if (liveSlots.find(m.disp) == liveSlots.end())
                m.op = MirOp::Nop;
            else
                liveSlots.erase(m.disp);
            continue;
        }
    }
}

} // namespace camel::jit::x64
