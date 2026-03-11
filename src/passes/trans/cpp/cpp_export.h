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
 * Created: Mar. 11, 2026
 * Updated: Mar. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/execute/pass/trans.h"
#include "lower.h"

class CppDumpPass : public GraphTranslatePass {
  public:
    CppDumpPass(
        const camel::core::context::context_ptr_t &ctx, CppEmitMode mode = CppEmitMode::Plain)
        : GraphTranslatePass(ctx), mode_(mode) {}

    GIR::graph_ptr_t apply(GIR::graph_ptr_t &graph, std::ostream &os) override;

  private:
    CppEmitMode mode_;
};

class CppModuleDumpPass : public CppDumpPass {
  public:
    CppModuleDumpPass(const camel::core::context::context_ptr_t &ctx)
        : CppDumpPass(ctx, CppEmitMode::Module) {}
};

class CppInspectDumpPass : public CppDumpPass {
  public:
    CppInspectDumpPass(const camel::core::context::context_ptr_t &ctx)
        : CppDumpPass(ctx, CppEmitMode::Inspect) {}
};

class CppBenchDumpPass : public CppDumpPass {
  public:
    CppBenchDumpPass(const camel::core::context::context_ptr_t &ctx)
        : CppDumpPass(ctx, CppEmitMode::Bench) {}
};
