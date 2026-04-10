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
 * Created: Feb. 08, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/execute/pass/runtime_trans.h"

namespace ctx = camel::core::context;

/**
 * JitRmirDumpPass (std::rmir): MIR translated directly from bytecode, based on
 * virtual registers and not yet optimized.
 */
class JitRmirDumpPass : public RuntimeGraphTranslatePass {
  public:
    JitRmirDumpPass(const ctx::context_ptr_t &ctx) : RuntimeGraphTranslatePass(ctx) {}
    virtual ~JitRmirDumpPass() = default;
    virtual camel::runtime::GCGraph *
    apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};

/**
 * JitMirDumpPass (std::mir): final optimized vreg MIR after multiple passes
 * (optimization entry point: runMirOptimizationPasses).
 */
class JitMirDumpPass : public RuntimeGraphTranslatePass {
  public:
    JitMirDumpPass(const ctx::context_ptr_t &ctx) : RuntimeGraphTranslatePass(ctx) {}
    virtual ~JitMirDumpPass() = default;
    virtual camel::runtime::GCGraph *
    apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};
