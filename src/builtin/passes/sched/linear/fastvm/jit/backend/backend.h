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
 * Updated: Feb. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "builtin/passes/sched/linear/fastvm/bytecode.h"
#include "compile/gir.h"
#include "core/context/frame.h"
#include "core/rtdata/data.h"

#include <ostream>

namespace camel::jit {

struct CompilationUnit {
    GraphIR::Graph *graph;
    FrameMeta *frameMeta = nullptr; // 编译期使用的 Frame 布局，来自 graph->getExtra<FrameMeta,0>()
    std::span<const Bytecode> bytecodes;
    size_t entryPc;
    void *trampolineFunc = nullptr; // FUNC trampoline
    void *trampolineTail = nullptr; // TAIL trampoline
    void *trampolineOper = nullptr; // OPER trampoline
    std::ostream *asmOut = nullptr; // 若设置，编译时输出汇编指令
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

class IJitBackend {
  public:
    virtual ~IJitBackend() = default;

    virtual std::unique_ptr<CompiledCode> compile(const CompilationUnit &unit) = 0;
    virtual void registerTrampoline(const char *name, void *addr)              = 0;
    virtual JitEntryFn load(std::unique_ptr<CompiledCode> code)                = 0;
    virtual void unload(JitEntryFn fn)                                         = 0;
};

std::unique_ptr<IJitBackend> createBackend();

} // namespace camel::jit
