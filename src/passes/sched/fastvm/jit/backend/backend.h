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

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "camel/compile/gir.h"
#include "camel/core/context/frame.h"
#include "camel/core/rtdata.h"
#include "passes/sched/fastvm/bytecode.h"

#include <ostream>
#include <unordered_map>

namespace camel::jit {

namespace ctx = camel::core::context;

struct CompilationDebugOptions {
    std::ostream *asmOut = nullptr; // 若设置，编译时输出汇编指令
    // 若设置，编译后填入每条指令的 (起始偏移, 长度, 汇编文本)，供 bindump 按指令分解机器码
    std::vector<std::tuple<size_t, size_t, std::string>> *instructionBoundaries = nullptr;
    std::ostream *mirOut = nullptr; // 若设置，仅输出 MIR 后返回（不编码机器码）
    bool mirSlotOnly = false; // 若 true 且 mirOut 设置：输出 rmir（未优化 vreg MIR）并立即返回；若
                              // false 且 mirOut：输出 mir（优化后 vreg MIR）并返回
    bool enableDebugTrace = false; // 默认运行态不生成 DebugTrace MIR，仅 dump/诊断模式打开
    // MIR 可读打印：地址/常量 -> 符号名（如 trampolineFunc、static slot -1）；disp -> 槽位名（如
    // "n"） 当 mirOut 非空时，backend 可向 mirSymbolNames 写入静态槽注释（调用方传非 const 指针）
    std::unordered_map<uint64_t, std::string> *mirSymbolNames = nullptr;
    const std::unordered_map<int, std::string> *mirSlotNames  = nullptr;
};

struct CompilationUnit {
    GIR::Graph *graph;
    ctx::FrameMeta *frameMeta =
        nullptr; // 编译期使用的 Frame 布局，来自 graph->getExtra<FrameMeta,0>()
    std::span<const Bytecode> bytecodes;
    size_t entryPc;
    void *trampolineFunc                 = nullptr; // FUNC trampoline
    void *trampolineTail                 = nullptr; // TAIL trampoline
    void *trampolineOper                 = nullptr; // OPER trampoline
    void *trampolineCast                 = nullptr; // CAST trampoline
    void *poolTopAddr                    = nullptr; // &FramePool::top_ for inline frame mgmt
    void *directSelfFuncInvokeAddr       = nullptr; // slow path fallback
    const CompilationDebugOptions *debug = nullptr;
};

struct RelocInfo {
    size_t offset; // 机器码中的字节偏移
    uint32_t kind; // 重定位类型
    size_t target; // 目标 pc 或 trampoline id
};

struct CompiledCode {
    std::vector<uint8_t> code;
    size_t entryOffset;
    std::vector<RelocInfo> relocs;
};

using JitEntryFn = slot_t (*)(slot_t *slots, void *ctx);

// Extra index 1 on GIR::Graph — O(1) JIT entry lookup
struct JitGraphInfo {
    JitEntryFn fn = nullptr;
};
inline JitEntryFn getGraphJitFn(GIR::Graph *g) {
    auto *info = g->getExtra<JitGraphInfo, 1>();
    return info ? info->fn : nullptr;
}
inline void setGraphJitFn(GIR::Graph *g, JitEntryFn fn) {
    auto *info = g->getExtra<JitGraphInfo, 1>();
    if (!info) {
        info = new JitGraphInfo{fn};
        g->setExtra<JitGraphInfo, 1>(info);
    } else {
        info->fn = fn;
    }
}

class IJitBackend {
  public:
    virtual ~IJitBackend() = default;

    virtual std::unique_ptr<CompiledCode>
    compile(const CompilationUnit &unit, std::string *failureReason = nullptr) = 0;
    virtual void registerTrampoline(const char *name, void *addr)              = 0;
    virtual JitEntryFn load(std::unique_ptr<CompiledCode> code)                = 0;
    virtual void unload(JitEntryFn fn)                                         = 0;
};

std::unique_ptr<IJitBackend> createBackend();

} // namespace camel::jit
