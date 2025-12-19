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
 * Updated: Dec. 19, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "optimize.h"

void moveup(bytecode_vec_t &codes, size_t from, size_t to) {
    ASSERT(from > to, "from should be larger than to");
    Bytecode &bc  = codes[from];
    size_t opsize = bc.opsize;
    std::rotate(codes.begin() + to, codes.begin() + from, codes.begin() + from + opsize);
    for (size_t i = 0; i < codes.size(); i++) {
        Bytecode &bc = codes[i];
        if (bc.opcode == OpCode::JUMP) {
            size_t target = static_cast<size_t>(bc.fastop[0]);
            if (target >= to && target < from) {
                bc.fastop[0] += opsize;
            } else if (target >= from && target < from + opsize) {
                bc.fastop[0] -= from - to;
            }
        }
    }
}

void redirect(bytecode_vec_t &codes, size_t start, int step) {
    for (size_t i = 0; i < codes.size(); i++) {
        Bytecode &bc = codes[i];
        if (bc.opcode == OpCode::JUMP) {
            if (static_cast<size_t>(bc.fastop[0]) > start) {
                bc.fastop[0] += step;
            }
        }
    }
}

void removeop(bytecode_vec_t &codes, size_t index) {
    Bytecode &bc = codes[index];
    codes.erase(codes.begin() + index, codes.begin() + index + bc.opsize);
}

size_t findPrev(bytecode_vec_t &codes, size_t index) {
    // 必须从头开始查找，因为字节码是变长的
    for (size_t i = 0; i < index;) {
        Bytecode &bc = codes[i];
        size_t next  = i + bc.opsize;
        if (next == index) {
            return i; // 如果找到，则返回该指令的索引，表示该指令就是前一个指令
        } else {
            i = next;
        }
    }
    return index; // 如果找不到，则返回 index，表示当前指令就是第一个指令
}

size_t findNext(bytecode_vec_t &codes, size_t index) { return index + codes[index].opsize; }

const std::unordered_map<OptimizationStrategyCode, std::unique_ptr<IOptimizeStrategy>> &
getGlobalOptimizationStrategyRegistry() {
    static const auto reg = [] {
        std::unordered_map<OptimizationStrategyCode, std::unique_ptr<IOptimizeStrategy>> m;
        m.emplace(
            OptimizationStrategyCode::JumpToJumpOptimization,
            std::make_unique<JumpToJumpStrategy>());
        m.emplace(
            OptimizationStrategyCode::JumpToNextOptimization,
            std::make_unique<JumpToNextStrategy>());
        m.emplace(
            OptimizationStrategyCode::JumpToRetnOptimization,
            std::make_unique<JumpToRetnStrategy>());
        m.emplace(
            OptimizationStrategyCode::JoinCleanupOptimization,
            std::make_unique<JoinCleanupStrategy>());
        return m;
    }();
    return reg;
}
