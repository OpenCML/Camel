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

#include <string>
#include <unordered_map>

#include "../backend.h"
#include "x64_encoder.h"

namespace camel::jit {

class X64Backend : public IJitBackend {
  public:
    X64Backend();

    std::unique_ptr<CompiledCode>
    compile(const CompilationUnit &unit, std::string *failureReason = nullptr) override;
    std::vector<JitBlocker> blockers(const CompilationUnit &unit) const override;
    std::vector<JitBlocker> runtimeConstructs(const CompilationUnit &unit) const override;
    void registerTrampoline(const char *name, void *addr) override;
    JitEntryFn load(std::unique_ptr<CompiledCode> code) override;
    void unload(JitEntryFn fn) override;

  private:
    bool compileBytecode(
        const CompilationUnit &unit, std::vector<uint8_t> &code,
        std::string *failureReason = nullptr);
    enum class LowerResult { Ok, Failed, OutOfRegisters };
    /// One lowering of the unit. With `keepValuesInRegisters`, a slot that was loaded or stored
    /// stays in its register for the rest of the basic block; without, no value outlives the
    /// bytecode that computes it, which always fits the registers.
    LowerResult compileBytecodeWith(
        const CompilationUnit &unit, std::vector<uint8_t> &code, std::string *failureReason,
        bool keepValuesInRegisters);
    int slotDisp(int idx) const;

    std::unordered_map<std::string, void *> trampolines_;
    std::vector<void *> allocatedPages_;
    size_t frameBaseOffset_{0};
    size_t jitEntryOffset_{0}; // set by compileBytecode, used by compile
    bool framed_{false};       // likewise: whether the unit became framed code
};

} // namespace camel::jit
