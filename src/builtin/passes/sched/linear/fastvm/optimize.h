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
 * Updated: Dec. 20, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "bytecode.h"

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
    // 返回下一次扫描的起始位置；若没有修改则返回 std::nullopt
    virtual std::optional<size_t> apply(bytecode_vec_t &codes, size_t curr) = 0;
    virtual OpCode triggerOp() const                                        = 0;
};

void moveup(bytecode_vec_t &codes, size_t from, size_t to);
void redirect(bytecode_vec_t &codes, size_t start, int step = -1);
void removeop(bytecode_vec_t &codes, size_t index);
size_t findPrev(bytecode_vec_t &codes, size_t index);
size_t findNext(bytecode_vec_t &codes, size_t index);

/**
 * 若当前 JUMP 的目标也是一个 JUMP，则进行折叠
 * 删除被中间跳过的那个 JUMP，使第一个 JUMP 直接跳向第二个 JUMP 的目标
 * 例如：
 * JUMP 1 -> JUMP 2 -> JUMP 3
 * 删除 JUMP 2，使 JUMP 1 直接跳向 JUMP 3
 */
class JumpToJumpStrategy : public IOptimizeStrategy {
  public:
    OpCode triggerOp() const override { return OpCode::JUMP; }

    std::optional<size_t> apply(bytecode_vec_t &codes, size_t curr) override {
        Bytecode &bc  = codes[curr];
        size_t next   = bc.fastop[0];
        Bytecode &tbc = codes[next];

        // 若当前 JUMP 的目标也是一个 JUMP，则进行折叠
        if (tbc.opcode == OpCode::JUMP) {
            // 修改目标为第二个跳转的目标
            bc.fastop[0] = tbc.fastop[0];

            // 删除被中间跳过的那个 JUMP
            removeop(codes, next);
            redirect(codes, curr, -1);

            return curr;
        }

        return std::nullopt;
    }
};

/**
 * 若当前 JUMP 跳向的是下一条指令，则删除该 JUMP
 */
class JumpToNextStrategy : public IOptimizeStrategy {
  public:
    OpCode triggerOp() const override { return OpCode::JUMP; }

    std::optional<size_t> apply(bytecode_vec_t &codes, size_t curr) override {
        Bytecode &bc = codes[curr];
        size_t next  = bc.fastop[0];

        // 若跳转目标刚好是下一条指令，说明此 JUMP 没有意义
        if (next == curr + 1) {
            removeop(codes, curr);
            redirect(codes, curr, -1);

            return curr;
        }

        return std::nullopt;
    }
};

/**
 * 若当前 JUMP 跳向的是一个 RETN，则将 RETN 复制到 JUMP 位置
 * 以免除一次跳转，提高执行效率
 */
class JumpToRetnStrategy : public IOptimizeStrategy {
  public:
    OpCode triggerOp() const override { return OpCode::JUMP; }

    std::optional<size_t> apply(bytecode_vec_t &codes, size_t curr) override {
        Bytecode &bc = codes[curr];
        size_t next  = bc.fastop[0];
        if (next >= codes.size())
            return std::nullopt;
        Bytecode &tbc = codes[next];

        // 如果 JUMP 跳向的是一个 RETN，直接替换为 RETN
        if (tbc.opcode == OpCode::RETN) {
            bc = tbc; // 覆盖当前字节码为 RETN
            // 从下一条扫描
            return curr + 1;
        }

        return std::nullopt;
    }
};

/**
 * 清理跳转到 JOIN 的 JUMP，使 JOIN 前面没有无意义的跳转
 * 如果所有的 JUMP 都被清理干净，则删除该 JOIN
 */
class JoinCleanupStrategy : public IOptimizeStrategy {
  public:
    OpCode triggerOp() const override { return OpCode::JOIN; }

    std::optional<size_t> apply(bytecode_vec_t &codes, size_t curr) override {
        Bytecode &bc = codes[curr];
        if (bc.opcode != OpCode::JOIN)
            return std::nullopt;

        bool hasJumpToSelf = false;

        // 遍历所有 JUMP，查找是否有跳转到当前 JOIN 的
        for (size_t j = 0; j < codes.size(); j++) {
            Bytecode &nbc = codes[j];
            if (nbc.opcode != OpCode::JUMP)
                continue;

            size_t next = nbc.fastop[0];
            if (next == curr) {
                // 找到一个跳向自己的 JUMP，检查其前一条是否是 TAIL 或 RETN
                size_t prev   = findPrev(codes, j);
                Bytecode &pbc = codes[prev];

                // 如果前一条是 TAIL 或 RETN，则该 JUMP 无效，删除它
                if (pbc.opcode == OpCode::TAIL || pbc.opcode == OpCode::RETN) {
                    removeop(codes, j);
                    redirect(codes, j, -1);

                    // 改动后需要重新从 JOIN 前一个位置开始扫描，
                    return curr - 1;
                } else {
                    // 有至少一个有效的跳转指令 -> JOIN 不能删除
                    hasJumpToSelf = true;
                }
            }
        }

        // 如果没有跳向 JOIN 的 JUMP，可以安全删除此 JOIN
        if (!hasJumpToSelf) {
            removeop(codes, curr);
            redirect(codes, curr, -1);

            // 若删除后当前位置是 RETN，也一并删除它
            // 因为如果前面还有直接跳转到 RETN 指令的地方，已经被优化掉了
            // 这里的 RETN 一定是没用的
            if (curr < codes.size()) {
                Bytecode &tbc = codes[curr];
                if (tbc.opcode == OpCode::RETN) {
                    removeop(codes, curr);
                    redirect(codes, curr, -1);
                }
            }

            // 修改后从 curr 重新扫描
            return curr;
        }

        return std::nullopt;
    }
};

// 全局策略注册表
// 懒加载
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

    void optimize(bytecode_vec_t &codes, size_t start = 0) {
        for (size_t curr = start; curr < codes.size();) {
            std::optional<size_t> restartPos;

            for (auto *s : strategies) {
                auto trigOp = s->triggerOp();
                if (codes[curr].opcode != trigOp)
                    continue;
                if (auto res = s->apply(codes, curr)) {
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
