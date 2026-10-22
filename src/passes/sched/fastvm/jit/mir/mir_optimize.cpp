// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "mir_optimize.h"

#include <cstddef>
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
    case MirOp::CallHelper:
    case MirOp::ExitToDriver:
    case MirOp::SafepointPoll:
    case MirOp::PushRdi:
    case MirOp::PopRdi:
    case MirOp::PushFrameSlot:
    case MirOp::PushImm64:
    case MirOp::PopFrameSlot:
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

void optimizePeephole(MirBuffer &buf, bool forwardStores) {
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
        VRegId vreg;
        size_t index;
        bool read;
    };
    std::unordered_map<int32_t, StoreInfo> pendingStore;

    for (size_t i = 0; i < n; ++i) {
        Mir &m = buf[i];

        // Before anything is skipped: the first instruction of a jump target can be a Nop.
        if (m.hasPc() && jumpTargets.count(m.pc))
            pendingStore.clear();

        if (m.op == MirOp::Nop || m.op == MirOp::DebugTrace)
            continue;

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
                if (m.op == MirOp::VLoadFromFrame && forwardStores) {
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

    // Sentinel value inserted into liveSlots to mean "all frame slots are live".
    // This is used when we encounter an opaque call (CallRax/CallRel32) that
    // receives rdi=slot_base and can therefore read any slot implicitly (e.g.,
    // trampolineFunc resolves argument slots by bytecode operand index at
    // runtime). Valid frame disps are always positive multiples of 8, so
    // INT32_MIN is a safe sentinel that never collides with a real disp.
    constexpr int32_t kAllSlotsLive = INT32_MIN;

    // What one instruction does to the set, going backwards: the set after it becomes the set
    // before it.
    auto transfer = [&](Mir &m) {
        if (m.op == MirOp::Nop || m.op == MirOp::DebugTrace)
            return;

        // Function exit: no frame slots are live after return.
        if (m.op == MirOp::Ret || m.op == MirOp::JmpRax || m.op == MirOp::VRet) {
            liveSlots.clear();
            return;
        }

        // Jump source: merge liveness from the target (already saved below). A target not seen
        // yet lies before the jump (the self tail call jumps back to the entry): what it reads
        // is not known at this point of a single backward pass, so every slot stays live.
        if (isJumpOp(m.op)) {
            auto it = targetLiveness.find(m.imm32);
            if (it != targetLiveness.end())
                liveSlots.insert(it->second.begin(), it->second.end());
            else
                liveSlots.insert(kAllSlotsLive);
            return;
        }

        // Stack-staged tail-call arguments: the push reads its slot; the pop writes one but is
        // never removed (it balances the push).
        if (m.op == MirOp::PushFrameSlot) {
            liveSlots.insert(m.disp);
            return;
        }
        if (m.op == MirOp::PopFrameSlot) {
            liveSlots.erase(m.disp);
            return;
        }

        // NativeJitFuncCall: reads the caller slots of the arguments it copies itself, writes
        // the result slot unless the result is a vreg. The callee has a frame of its own.
        if (m.op == MirOp::NativeJitFuncCall) {
            auto *p = reinterpret_cast<const NativeJitCallParams *>(m.imm64);
            if (p) {
                if (p->resultVReg == kInvalidVReg)
                    liveSlots.erase(p->resultDisp);
                for (const auto &arg : p->args) {
                    if (arg.vreg == kInvalidVReg && !arg.isImm)
                        liveSlots.insert(arg.srcDisp);
                }
            }
            return;
        }

        // Opaque calls through rax or a relative target: the callee receives rdi=slot_base and
        // may implicitly read any frame slot. Framed code: a helper reads its operands from
        // the frame, and at an exit to the driver the frame is all there is of the activation
        // (the driver, the argument copy of the callee and the collector read it). Every slot
        // is live at these: the sentinel keeps any preceding store from being declared dead.
        if (m.op == MirOp::CallRax || m.op == MirOp::CallRel32 || m.op == MirOp::CallHelper ||
            m.op == MirOp::ExitToDriver || m.op == MirOp::SafepointPoll) {
            liveSlots.insert(kAllSlotsLive);
            return;
        }

        if (readsFrameSlot(m.op)) {
            liveSlots.insert(m.disp);
            return;
        }

        if (writesFrameSlot(m.op)) {
            // A store is dead only if neither its specific slot nor the
            // all-slots-live sentinel is present in liveSlots.
            if (liveSlots.count(m.disp) == 0 && liveSlots.count(kAllSlotsLive) == 0)
                m.op = MirOp::Nop;
            else
                liveSlots.erase(m.disp); // sentinel is never erased (disp != INT32_MIN)
            return;
        }
    };

    for (size_t i = n; i > 0; --i) {
        Mir &m = buf[i - 1];
        transfer(m);
        // A jump lands before the first instruction of its target bytecode, so the set saved
        // for a target is the one before that instruction: it includes what the instruction
        // itself reads (a JOIN reads the value of its first arm first).
        if (m.hasPc() && jumpTargets.count(m.pc))
            targetLiveness[m.pc] = liveSlots;
    }
}

} // namespace camel::jit::x64
