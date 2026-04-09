/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Created: Dec. 16, 2025
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "bytecode.h"
#include "camel/core/source/ids.h"

#include <unordered_map>

enum class OptimizationStrategyCode : uint32_t {
    None                    = 0x00000000,
    All                     = 0xFFFFFFFF,
    JumpToJumpOptimization  = 1 << 0,
    JumpToNextOptimization  = 1 << 1,
    JumpToRetnOptimization  = 1 << 2,
    JoinCleanupOptimization = 1 << 3
};

struct OptimizationStrategyConfig {
    OptimizationStrategyCode flags = OptimizationStrategyCode::None;

    OptimizationStrategyConfig(OptimizationStrategyCode f) : flags(f) {}
    bool enabled(OptimizationStrategyCode code) const {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(code)) != 0;
    }
};

struct IOptimizeStrategy {
    virtual ~IOptimizeStrategy() = default;
    // Returns the next scan position; std::nullopt if nothing changed.
    virtual std::optional<size_t> apply(
        bytecode_vec_t &codes, size_t curr,
        std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins) = 0;
    virtual OpCode triggerOp() const                                       = 0;
};

void moveup(
    bytecode_vec_t &codes, size_t from, size_t to,
    std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins = nullptr);
void redirect(
    bytecode_vec_t &codes, size_t start, int step = -1,
    std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins = nullptr);
void removeop(
    bytecode_vec_t &codes, size_t index,
    std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins = nullptr);
size_t findPrev(bytecode_vec_t &codes, size_t index);
size_t findNext(bytecode_vec_t &codes, size_t index);

inline size_t brchArmDispatchCount(const Bytecode &bc) {
    ASSERT(bc.opcode == OpCode::BRCH, "Dispatch-arm count query requires a BRCH opcode.");
    return bc.withCnt() == 0 ? 2 : (bc.withCnt() + 1);
}

inline bool isBrchDispatchJump(bytecode_vec_t &codes, size_t index) {
    if (index >= codes.size() || codes[index].opcode != OpCode::JUMP) {
        return false;
    }
    for (size_t pc = 0; pc < codes.size();) {
        const Bytecode &bc = codes[pc];
        if (bc.opcode == OpCode::BRCH) {
            const size_t dispatchBegin = pc + bc.opsize;
            const size_t dispatchEnd   = dispatchBegin + brchArmDispatchCount(bc);
            if (index >= dispatchBegin && index < dispatchEnd) {
                return true;
            }
            if (pc >= index) {
                break;
            }
        }
        pc += bc.opsize;
    }
    return false;
}

/**
 * If this JUMP's target is another JUMP, fold the chain:
 * remove the skipped middle JUMP so the first JUMP targets the final destination.
 * Example:
 *   JUMP 1 -> JUMP 2 -> JUMP 3
 * Remove JUMP 2 so JUMP 1 jumps straight to JUMP 3's target.
 */
class JumpToJumpStrategy : public IOptimizeStrategy {
  public:
    OpCode triggerOp() const override { return OpCode::JUMP; }

    std::optional<size_t> apply(
        bytecode_vec_t &codes, size_t curr,
        std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins) override {
        if (isBrchDispatchJump(codes, curr)) {
            return std::nullopt;
        }
        Bytecode &bc  = codes[curr];
        size_t next   = bc.fastop[0];
        Bytecode &tbc = codes[next];

        // Fold when the target is also a JUMP
        if (tbc.opcode == OpCode::JUMP) {
            // Point at the second jump's target
            bc.fastop[0] = tbc.fastop[0];

            // Remove the skipped middle JUMP
            removeop(codes, next, pcOrigins);
            redirect(codes, curr, -1, pcOrigins);

            return curr;
        }

        return std::nullopt;
    }
};

/**
 * If this JUMP targets the next instruction, remove the redundant JUMP.
 */
class JumpToNextStrategy : public IOptimizeStrategy {
  public:
    OpCode triggerOp() const override { return OpCode::JUMP; }

    std::optional<size_t> apply(
        bytecode_vec_t &codes, size_t curr,
        std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins) override {
        if (isBrchDispatchJump(codes, curr)) {
            return std::nullopt;
        }
        Bytecode &bc = codes[curr];
        size_t next  = bc.fastop[0];

        // Target is fall-through; the JUMP is redundant
        if (next == curr + 1) {
            removeop(codes, curr, pcOrigins);
            redirect(codes, curr, -1, pcOrigins);

            return curr;
        }

        return std::nullopt;
    }
};

/**
 * If this JUMP targets a RETN, copy RETN to the JUMP site to avoid an extra branch.
 */
class JumpToRetnStrategy : public IOptimizeStrategy {
  public:
    OpCode triggerOp() const override { return OpCode::JUMP; }

    std::optional<size_t> apply(
        bytecode_vec_t &codes, size_t curr,
        std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins) override {
        if (isBrchDispatchJump(codes, curr)) {
            return std::nullopt;
        }
        Bytecode &bc = codes[curr];
        size_t next  = bc.fastop[0];
        if (next >= codes.size())
            return std::nullopt;
        Bytecode &tbc = codes[next];

        // JUMP to RETN: replace with RETN in place
        if (tbc.opcode == OpCode::RETN) {
            bc = tbc; // Overwrite current insn with RETN
            // Resume scan at the following instruction
            return curr + 1;
        }

        return std::nullopt;
    }
};

/**
 * Clean up JUMPs that target a JOIN so useless branches before JOIN are removed.
 * If no JUMPs target the JOIN anymore, drop the JOIN itself.
 */
class JoinCleanupStrategy : public IOptimizeStrategy {
  public:
    OpCode triggerOp() const override { return OpCode::JOIN; }

    std::optional<size_t> apply(
        bytecode_vec_t &codes, size_t curr,
        std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins) override {
        Bytecode &bc = codes[curr];
        if (bc.opcode != OpCode::JOIN)
            return std::nullopt;

        bool hasJumpToSelf = false;

        // Scan all JUMPs that target this JOIN
        for (size_t j = 0; j < codes.size(); j++) {
            Bytecode &nbc = codes[j];
            if (nbc.opcode != OpCode::JUMP)
                continue;

            size_t next = nbc.fastop[0];
            if (next == curr) {
                // JUMP targets this JOIN: check whether the previous insn is TAIL or RETN
                size_t prev   = findPrev(codes, j);
                Bytecode &pbc = codes[prev];

                // After TAIL/RETN this JUMP is unreachable; remove it
                if (pbc.opcode == OpCode::TAIL || pbc.opcode == OpCode::RETN) {
                    removeop(codes, j, pcOrigins);
                    redirect(codes, j, -1, pcOrigins);

                    // Rescan from just before the JOIN after the edit
                    return curr - 1;
                } else {
                    // At least one live edge into JOIN; cannot delete JOIN yet
                    hasJumpToSelf = true;
                }
            }
        }

        // No JUMPs target this JOIN; safe to remove it
        if (!hasJumpToSelf) {
            removeop(codes, curr, pcOrigins);
            redirect(codes, curr, -1, pcOrigins);

            // Keep the trailing RETN intact. Optimization runs on one
            // graph-local bytecode chunk before cross-graph linking, so RETN is
            // still the boundary that prevents fallthrough into the next graph.
            return curr;
        }

        return std::nullopt;
    }
};

// Global strategy registry (lazily initialized)
const std::unordered_map<OptimizationStrategyCode, std::unique_ptr<IOptimizeStrategy>> &
getGlobalOptimizationStrategyRegistry();

class BytecodeOptimizer {
  public:
    explicit BytecodeOptimizer(const OptimizationStrategyCode flags) {
        OptimizationStrategyConfig cfg(flags);
        for (auto &[code, strategy] : getGlobalOptimizationStrategyRegistry()) {
            if (cfg.enabled(code)) {
                strategies.push_back(strategy.get());
            }
        }
    }

    void optimize(
        bytecode_vec_t &codes, size_t start = 0,
        std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins = nullptr) {
        for (size_t curr = start; curr < codes.size();) {
            std::optional<size_t> restartPos;

            for (auto *s : strategies) {
                auto trigOp = s->triggerOp();
                if (codes[curr].opcode != trigOp)
                    continue;
                if (auto res = s->apply(codes, curr, pcOrigins)) {
                    restartPos = res;
                    break;
                }
            }

            if (restartPos)
                curr = *restartPos;
            else if (curr < codes.size())
                curr += codes[curr].opsize;
            else
                break;
        }
    }

  private:
    std::vector<IOptimizeStrategy *> strategies;
};
