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

#include "backend.h"

namespace camel::jit {

class FallbackBackend : public IJitBackend {
  public:
    std::unique_ptr<CompiledCode> compile(const CompilationUnit &) override { return nullptr; }
    void registerTrampoline(const char *, void *) override {}
    JitEntryFn load(std::unique_ptr<CompiledCode>) override { return nullptr; }
    void unload(JitEntryFn) override {}
};

} // namespace camel::jit
