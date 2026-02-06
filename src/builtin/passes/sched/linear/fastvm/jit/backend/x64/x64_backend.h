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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <unordered_map>

#include "../backend.h"
#include "x64_encoder.h"

namespace camel::jit {

class X64Backend : public IJitBackend {
  public:
    X64Backend();

    std::unique_ptr<CompiledCode> compile(const CompilationUnit &unit) override;
    void registerTrampoline(const char *name, void *addr) override;
    JitEntryFn load(std::unique_ptr<CompiledCode> code) override;
    void unload(JitEntryFn fn) override;

  private:
    bool compileBytecode(const CompilationUnit &unit, std::vector<uint8_t> &code);
    int slotDisp(int idx) const;

    std::unordered_map<std::string, void *> trampolines_;
    std::vector<void *> allocatedPages_;
    size_t frameBaseOffset_{0};
};

} // namespace camel::jit
