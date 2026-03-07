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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "optimize.h"

namespace {

void shiftPcOrigins(
    std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins, size_t start, int step) {
    if (!pcOrigins || step == 0) {
        return;
    }
    std::unordered_map<size_t, camel::source::origin_id_t> updated;
    updated.reserve(pcOrigins->size());
    for (const auto &[pc, origin] : *pcOrigins) {
        size_t mapped = pc;
        if (pc > start) {
            mapped = static_cast<size_t>(static_cast<int64_t>(pc) + step);
        }
        updated[mapped] = origin;
    }
    *pcOrigins = std::move(updated);
}

void erasePcOrigins(
    std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins, size_t start, size_t len) {
    if (!pcOrigins || len == 0) {
        return;
    }
    std::unordered_map<size_t, camel::source::origin_id_t> kept;
    kept.reserve(pcOrigins->size());
    for (const auto &[pc, origin] : *pcOrigins) {
        if (pc < start || pc >= start + len) {
            kept[pc] = origin;
        }
    }
    *pcOrigins = std::move(kept);
}

} // namespace

void moveup(
    bytecode_vec_t &codes, size_t from, size_t to,
    std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins) {
    ASSERT(from > to, "from should be larger than to");
    Bytecode &bc  = codes[from];
    size_t opsize = bc.opsize;
    std::rotate(codes.begin() + to, codes.begin() + from, codes.begin() + from + opsize);
    if (pcOrigins) {
        std::unordered_map<size_t, camel::source::origin_id_t> updated;
        updated.reserve(pcOrigins->size());
        for (const auto &[pc, origin] : *pcOrigins) {
            size_t mapped = pc;
            if (pc >= to && pc < from) {
                mapped = pc + opsize;
            } else if (pc >= from && pc < from + opsize) {
                mapped = pc - (from - to);
            }
            updated[mapped] = origin;
        }
        *pcOrigins = std::move(updated);
    }
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

void redirect(
    bytecode_vec_t &codes, size_t start, int step,
    std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins) {
    for (size_t i = 0; i < codes.size(); i++) {
        Bytecode &bc = codes[i];
        if (bc.opcode == OpCode::JUMP) {
            if (static_cast<size_t>(bc.fastop[0]) > start) {
                bc.fastop[0] += step;
            }
        }
    }
    shiftPcOrigins(pcOrigins, start, step);
}

void removeop(
    bytecode_vec_t &codes, size_t index,
    std::unordered_map<size_t, camel::source::origin_id_t> *pcOrigins) {
    Bytecode &bc = codes[index];
    erasePcOrigins(pcOrigins, index, bc.opsize);
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
